#!/usr/bin/env python3
"""Inspect ESP32 flash image metadata.

This tool intentionally reports only structural metadata:
- file size and SHA-256
- partition table entries
- ESP app image headers
- ESP app description fields

It does not decode NVS, FAT/FFAT, SPIFFS, OTA data, arbitrary strings, or
partition contents. The output is meant to describe firmware structure.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path
from typing import Any


PARTITION_TABLE_OFFSET = 0x8000
PARTITION_ENTRY_SIZE = 32
PARTITION_TABLE_MAX_SIZE = 0xC00
PARTITION_MAGIC = b"\xAA\x50"
APP_IMAGE_MAGIC = 0xE9
APP_DESC_MAGIC = b"\x32\x54\xCD\xAB"

TYPE_NAMES = {
    0x00: "app",
    0x01: "data",
}

SUBTYPE_NAMES = {
    (0x00, 0x00): "factory",
    (0x00, 0x10): "ota_0",
    (0x00, 0x11): "ota_1",
    (0x00, 0x12): "ota_2",
    (0x00, 0x13): "ota_3",
    (0x00, 0x14): "ota_4",
    (0x00, 0x15): "ota_5",
    (0x00, 0x16): "ota_6",
    (0x00, 0x17): "ota_7",
    (0x00, 0x18): "ota_8",
    (0x00, 0x19): "ota_9",
    (0x00, 0x20): "test",
    (0x01, 0x00): "ota",
    (0x01, 0x01): "phy",
    (0x01, 0x02): "nvs",
    (0x01, 0x03): "coredump",
    (0x01, 0x04): "nvs_keys",
    (0x01, 0x05): "efuse",
    (0x01, 0x80): "esphttpd",
    (0x01, 0x81): "fat",
    (0x01, 0x82): "spiffs",
}

PARTITION_ROLES = {
    "nvs": "Configuration storage",
    "nvs_keys": "NVS key storage",
    "otadata": "OTA boot state",
    "phy_init": "PHY initialization data",
    "coredump": "Core dump storage",
    "ffat": "Application file system",
    "fatfs": "Application file system",
    "spiffs": "Application file system",
}


def _clean_c_string(raw: bytes, encoding: str = "utf-8") -> str:
    return raw.split(b"\x00", 1)[0].decode(encoding, errors="replace")


def _partition_label(raw: bytes) -> str:
    return _clean_c_string(raw, encoding="ascii")


def _hex(value: int, width: int = 6) -> str:
    return f"0x{value:0{width}X}"


def _subtype_name(partition_type: int, subtype: int) -> str:
    return SUBTYPE_NAMES.get((partition_type, subtype), _hex(subtype, 2))


def _type_name(partition_type: int) -> str:
    return TYPE_NAMES.get(partition_type, _hex(partition_type, 2))


def _partition_role(label: str, partition_type: int, subtype: int) -> str:
    label_key = label.lower()
    if label_key in PARTITION_ROLES:
        return PARTITION_ROLES[label_key]
    if partition_type == 0x00:
        subtype_name = _subtype_name(partition_type, subtype)
        if subtype_name.startswith("ota_"):
            return "Application image"
        if subtype_name == "factory":
            return "Factory application image"
        return "Application image"
    subtype_name = _subtype_name(partition_type, subtype)
    if subtype_name == "ota":
        return "OTA boot state"
    if subtype_name == "nvs":
        return "Configuration storage"
    if subtype_name == "fat":
        return "Application file system"
    if subtype_name == "spiffs":
        return "Application file system"
    return "Data partition"


def _content_scope(partition_type: int) -> str:
    return "app-metadata" if partition_type == 0x00 else "partition-metadata"


def parse_partition_table(data: bytes, offset: int = PARTITION_TABLE_OFFSET) -> list[dict[str, Any]]:
    """Parse ESP32 partition entries without reading partition contents."""
    entries: list[dict[str, Any]] = []
    table = data[offset : offset + PARTITION_TABLE_MAX_SIZE]

    for index in range(0, len(table), PARTITION_ENTRY_SIZE):
        entry = table[index : index + PARTITION_ENTRY_SIZE]
        if len(entry) < PARTITION_ENTRY_SIZE:
            break
        magic = entry[:2]
        if magic == b"\xFF\xFF":
            break
        if magic != PARTITION_MAGIC:
            break

        partition_type = entry[2]
        subtype = entry[3]
        partition_offset, partition_size = struct.unpack_from("<II", entry, 4)
        label = _partition_label(entry[12:28])
        flags = struct.unpack_from("<I", entry, 28)[0]

        entries.append(
            {
                "label": label,
                "type": _type_name(partition_type),
                "type_raw": _hex(partition_type, 2),
                "subtype": _subtype_name(partition_type, subtype),
                "subtype_raw": _hex(subtype, 2),
                "offset": _hex(partition_offset),
                "offset_int": partition_offset,
                "size": _hex(partition_size, 1),
                "size_int": partition_size,
                "flags": _hex(flags, 1),
                "role": _partition_role(label, partition_type, subtype),
                "content_scope": _content_scope(partition_type),
            }
        )

    return entries


def _parse_app_description(data: bytes, app_offset: int) -> dict[str, Any] | None:
    # The ESP app description normally appears near the start of an app image.
    # Scan only the first 4 KiB and decode only fixed app_desc fields.
    scan = data[app_offset : app_offset + 4096]
    relative_offset = scan.find(APP_DESC_MAGIC)
    if relative_offset < 0 or relative_offset + 256 > len(scan):
        return None

    absolute_offset = app_offset + relative_offset
    desc = data[absolute_offset : absolute_offset + 256]
    return {
        "app_desc_offset": _hex(absolute_offset),
        "version": _clean_c_string(desc[16:48]),
        "project_name": _clean_c_string(desc[48:80]),
        "compile_time": _clean_c_string(desc[80:96]),
        "compile_date": _clean_c_string(desc[96:112]),
        "idf_version": _clean_c_string(desc[112:144]),
        "elf_sha256_prefix": desc[144:176].hex().upper()[:16],
    }


def parse_app_metadata(data: bytes, partitions: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Parse safe metadata from app partitions."""
    apps: list[dict[str, Any]] = []
    for partition in partitions:
        if partition["type"] != "app":
            continue

        app_offset = partition["offset_int"]
        header = data[app_offset : app_offset + 24]
        if len(header) < 24 or header[0] != APP_IMAGE_MAGIC:
            apps.append(
                {
                    "label": partition["label"],
                    "offset": partition["offset"],
                    "size": partition["size"],
                    "image_header": "not-found",
                }
            )
            continue

        entry_address = struct.unpack_from("<I", header, 4)[0]
        app = {
            "label": partition["label"],
            "offset": partition["offset"],
            "size": partition["size"],
            "image_header": "found",
            "segment_count": header[1],
            "spi_mode": _hex(header[2], 2),
            "spi_speed_size": _hex(header[3], 2),
            "entry_address": _hex(entry_address, 8),
        }

        description = _parse_app_description(data, app_offset)
        if description is not None:
            app.update(description)
        apps.append(app)

    return apps


def analyze_flash(data: bytes) -> dict[str, Any]:
    partitions = parse_partition_table(data)
    applications = parse_app_metadata(data, partitions)
    return {
        "size_bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest().upper(),
        "partition_table_offset": _hex(PARTITION_TABLE_OFFSET),
        "partitions": partitions,
        "applications": applications,
        "analysis_note": "This report describes Flash structure and app image metadata.",
    }


def render_markdown(report: dict[str, Any]) -> str:
    lines = [
        "# ESP32 Flash Metadata Report",
        "",
        f"- Size: `{report['size_bytes']}` bytes",
        f"- SHA256: `{report['sha256']}`",
        f"- Partition table offset: `{report['partition_table_offset']}`",
        "",
        "## Partitions",
        "",
        "| Label | Type | Subtype | Offset | Size | Role | Content Scope |",
        "|---|---|---|---:|---:|---|---|",
    ]
    for partition in report["partitions"]:
        lines.append(
            "| {label} | {type} | {subtype} | `{offset}` | `{size}` | {role} | {content_scope} |".format(
                **partition
            )
        )

    lines.extend(
        [
            "",
            "## Application Images",
            "",
            "| Label | Project | Version | Build Date | Build Time | ESP-IDF | Entry |",
            "|---|---|---|---|---|---|---:|",
        ]
    )
    for app in report["applications"]:
        lines.append(
            "| {label} | {project} | {version} | {date} | {time} | {idf} | `{entry}` |".format(
                label=app.get("label", ""),
                project=app.get("project_name", ""),
                version=app.get("version", ""),
                date=app.get("compile_date", ""),
                time=app.get("compile_time", ""),
                idf=app.get("idf_version", ""),
                entry=app.get("entry_address", ""),
            )
        )

    lines.extend(["", "## Analysis Note", "", report["analysis_note"]])
    return "\n".join(lines) + "\n"


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Inspect ESP32 flash image metadata and print firmware structure."
    )
    parser.add_argument("flash_image", type=Path, help="Path to a local ESP32 flash backup image.")
    parser.add_argument(
        "--format",
        choices=("json", "markdown"),
        default="json",
        help="Output format. Defaults to json.",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_arg_parser().parse_args(argv)
    data = args.flash_image.read_bytes()
    report = analyze_flash(data)
    if args.format == "markdown":
        print(render_markdown(report), end="")
    else:
        print(json.dumps(report, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
