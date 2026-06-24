import hashlib
import struct
import unittest

from tools.analyze_flash_metadata import analyze_flash


FLASH_SIZE = 4 * 1024 * 1024
PARTITION_TABLE_OFFSET = 0x8000


def partition_entry(label, partition_type, subtype, offset, size, flags=0):
    label_bytes = label.encode("ascii")
    if len(label_bytes) > 15:
        raise ValueError("label too long for ESP32 partition table")
    return (
        b"\xAA\x50"
        + bytes([partition_type, subtype])
        + struct.pack("<II", offset, size)
        + label_bytes.ljust(16, b"\x00")
        + struct.pack("<I", flags)
    )


def write_app_image(data, offset, project_name, version, compile_date, compile_time, idf_version):
    data[offset : offset + 24] = bytes(
        [
            0xE9,  # image magic
            0x06,  # segment count
            0x02,  # SPI mode
            0x2F,  # SPI speed/size byte
        ]
    ) + struct.pack("<I", 0x40375C84) + bytes(16)

    desc_offset = offset + 0x20
    desc = bytearray(256)
    desc[0:4] = b"\x32\x54\xCD\xAB"
    desc[16:48] = version.encode("utf-8").ljust(32, b"\x00")
    desc[48:80] = project_name.encode("utf-8").ljust(32, b"\x00")
    desc[80:96] = compile_time.encode("utf-8").ljust(16, b"\x00")
    desc[96:112] = compile_date.encode("utf-8").ljust(16, b"\x00")
    desc[112:144] = idf_version.encode("utf-8").ljust(32, b"\x00")
    desc[144:176] = bytes(range(32))
    data[desc_offset : desc_offset + len(desc)] = desc


def build_flash_image():
    data = bytearray(b"\xFF" * FLASH_SIZE)
    entries = [
        partition_entry("nvs", 0x01, 0x02, 0x009000, 0x5000),
        partition_entry("otadata", 0x01, 0x00, 0x00E000, 0x2000),
        partition_entry("ota_0", 0x00, 0x10, 0x010000, 0x2C0000),
        partition_entry("uf2", 0x00, 0x00, 0x2D0000, 0x40000),
        partition_entry("ffat", 0x01, 0x81, 0x310000, 0xF0000),
    ]
    cursor = PARTITION_TABLE_OFFSET
    for entry in entries:
        data[cursor : cursor + len(entry)] = entry
        cursor += len(entry)
    data[cursor : cursor + 32] = b"\xEB" * 32

    # Bytes intentionally placed in a data partition. Analyzer output reports
    # structure, not data-partition payloads.
    data_marker = b"runtime_setting=example-value"
    data[0x009000 : 0x009000 + 32] = data_marker.ljust(32, b"\x00")

    write_app_image(
        data,
        0x010000,
        project_name="arduino-lib-builder",
        version="43a8f6d",
        compile_date="Jun  2 2026",
        compile_time="11:17:54",
        idf_version="v5.5.4",
    )
    write_app_image(
        data,
        0x2D0000,
        project_name="tinyuf2",
        version="0.35.0",
        compile_date="Jul  3 2025",
        compile_time="10:50:48",
        idf_version="v5.3.2",
    )
    return bytes(data)


class AnalyzeFlashMetadataTests(unittest.TestCase):
    def test_analyze_flash_returns_partition_and_app_metadata(self):
        data = build_flash_image()

        result = analyze_flash(data)

        self.assertEqual(result["size_bytes"], FLASH_SIZE)
        self.assertEqual(result["sha256"], hashlib.sha256(data).hexdigest().upper())
        self.assertEqual(
            [partition["label"] for partition in result["partitions"]],
            ["nvs", "otadata", "ota_0", "uf2", "ffat"],
        )
        self.assertEqual(result["partitions"][0]["role"], "Configuration storage")
        self.assertEqual(result["partitions"][2]["content_scope"], "app-metadata")
        self.assertEqual(result["applications"][0]["project_name"], "arduino-lib-builder")
        self.assertEqual(result["applications"][0]["version"], "43a8f6d")
        self.assertEqual(result["applications"][1]["project_name"], "tinyuf2")
        self.assertEqual(result["applications"][1]["idf_version"], "v5.3.2")

    def test_analyze_flash_does_not_expose_data_partition_contents(self):
        data = build_flash_image()

        result_text = repr(analyze_flash(data))

        self.assertNotIn("runtime_setting", result_text)
        self.assertNotIn("example-value", result_text)


if __name__ == "__main__":
    unittest.main()
