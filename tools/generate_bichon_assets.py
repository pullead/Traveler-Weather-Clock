#!/usr/bin/env python3
from pathlib import Path

import numpy as np
from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "firmware/TravelWeatherClockV2/source_art/bichon_animation_reference_alpha.png"
HEADER = ROOT / "firmware/TravelWeatherClockV2/bichon_asset.h"
PREVIEW = ROOT / "firmware/TravelWeatherClockV2/assets/bichon-sprites-preview.png"

FRAME_WIDTH = 28
FRAME_HEIGHT = 20


def occupied_runs(alpha: np.ndarray, y0: int, y1: int) -> list[tuple[int, int]]:
    projection = (alpha[y0:y1] > 80).any(axis=0)
    runs: list[tuple[int, int]] = []
    start = None
    for x, occupied in enumerate(np.append(projection, False)):
        if occupied and start is None:
            start = x
        elif not occupied and start is not None:
            if x - start > 40:
                runs.append((start, x))
            start = None
    return runs


def content_box(alpha: np.ndarray, x0: int, y0: int, x1: int, y1: int) -> tuple[int, int, int, int]:
    region = alpha[y0:y1, x0:x1] > 48
    ys, xs = np.where(region)
    return x0 + int(xs.min()), y0 + int(ys.min()), x0 + int(xs.max()) + 1, y0 + int(ys.max()) + 1


def fit_sprite(source: Image.Image, box: tuple[int, int, int, int], max_width: int, max_height: int) -> Image.Image:
    crop = source.crop(box).transpose(Image.Transpose.FLIP_LEFT_RIGHT)
    scale = min(max_width / crop.width, max_height / crop.height)
    size = (max(1, round(crop.width * scale)), max(1, round(crop.height * scale)))
    crop = crop.resize(size, Image.Resampling.LANCZOS)
    frame = Image.new("RGBA", (FRAME_WIDTH, FRAME_HEIGHT))
    x = (FRAME_WIDTH - crop.width) // 2
    y = FRAME_HEIGHT - crop.height
    frame.alpha_composite(crop, (x, y))
    return frame


def rgb565(rgb: tuple[int, int, int]) -> int:
    r, g, b = rgb
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def main() -> None:
    source = Image.open(SOURCE).convert("RGBA")
    alpha = np.asarray(source.getchannel("A"))

    run_boxes = [content_box(alpha, x0, 150, x1, 400) for x0, x1 in occupied_runs(alpha, 150, 400)]
    sleep_box = content_box(alpha, 0, 400, source.width, source.height)
    frames = [fit_sprite(source, box, 27, 19) for box in run_boxes]
    sleep = fit_sprite(source, sleep_box, 27, 18)

    atlas = Image.new("RGBA", (FRAME_WIDTH * len(frames), FRAME_HEIGHT * 2))
    for index, frame in enumerate(frames):
        atlas.alpha_composite(frame, (index * FRAME_WIDTH, 0))
    atlas.alpha_composite(sleep, (0, FRAME_HEIGHT))

    rgba = np.asarray(atlas).copy()
    visible = rgba[:, :, 3] >= 48
    magenta_fringe = visible & (rgba[:, :, 0] > 145) & (rgba[:, :, 2] > 145) & (rgba[:, :, 1] < 150)
    fringe_luma = np.maximum(rgba[:, :, 1], 170)
    for channel in range(3):
        rgba[:, :, channel][magenta_fringe] = fringe_luma[magenta_fringe]
    rgb = rgba[:, :, :3]
    rgb[~visible] = (0, 0, 0)
    keyed = Image.fromarray(rgb, "RGB")
    quantized = keyed.quantize(colors=16, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    indices = np.asarray(quantized, dtype=np.uint8).copy()
    key_index = int(indices[-1, -1])
    alpha_atlas = np.asarray(atlas.getchannel("A"))
    indices[alpha_atlas < 48] = key_index

    palette = quantized.getpalette()[: 16 * 3]
    order = [key_index] + [index for index in range(16) if index != key_index]
    inverse = np.zeros(16, dtype=np.uint8)
    for new_index, old_index in enumerate(order):
        inverse[old_index] = new_index
    indices = inverse[indices]
    colors = [(0, 0, 0)] + [tuple(palette[index * 3:index * 3 + 3]) for index in order[1:]]

    run_frames = [
        indices[:FRAME_HEIGHT, index * FRAME_WIDTH:(index + 1) * FRAME_WIDTH].reshape(-1)
        for index in range(len(frames))
    ]
    sleep_data = indices[FRAME_HEIGHT:FRAME_HEIGHT * 2, :FRAME_WIDTH]

    lines = [
        "#pragma once",
        "#include <Arduino.h>",
        f"constexpr uint8_t BICHON_WIDTH = {FRAME_WIDTH};",
        f"constexpr uint8_t BICHON_HEIGHT = {FRAME_HEIGHT};",
        f"constexpr uint8_t BICHON_RUN_FRAMES = {len(frames)};",
        "const uint16_t BICHON_PALETTE[16] PROGMEM = {",
        "  " + ", ".join(f"0x{rgb565(color):04X}" for color in colors),
        "};",
        "const uint8_t BICHON_RUN[] PROGMEM = {",
    ]
    flat_run = np.concatenate(run_frames)
    for start in range(0, len(flat_run), 28):
        lines.append("  " + ", ".join(str(int(value)) for value in flat_run[start:start + 28]) + ",")
    lines.extend(["};", "const uint8_t BICHON_SLEEP[] PROGMEM = {"])
    flat_sleep = sleep_data.reshape(-1)
    for start in range(0, len(flat_sleep), 28):
        lines.append("  " + ", ".join(str(int(value)) for value in flat_sleep[start:start + 28]) + ",")
    lines.extend(["};", ""])
    HEADER.write_text("\n".join(lines), encoding="utf-8")

    preview = Image.new("RGBA", atlas.size)
    for y in range(atlas.height):
        for x in range(atlas.width):
            index = int(indices[y, x])
            if index:
                preview.putpixel((x, y), (*colors[index], 255))
    preview.resize((preview.width * 6, preview.height * 6), Image.Resampling.NEAREST).save(PREVIEW)
    print(f"Generated {len(frames)} running frames and one sleeping frame")
    print(HEADER)
    print(PREVIEW)


if __name__ == "__main__":
    main()
