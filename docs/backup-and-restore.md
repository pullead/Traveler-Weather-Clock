# Backup and Restore Notes

These notes describe the local backup and restore workflow used during analysis.

## Read Full Flash Backup

Example backup command:

```powershell
python -m esptool --no-stub --port COM3 --baud 115200 read-flash 0x0 0x400000 local-flash-backup.bin
```

Notes:

- `read-flash` reads the Flash contents from the board.
- `--no-stub` can be slower but may be more reliable on this board.
- The bootloader port may differ from the normal runtime port.
- Store `local-flash-backup.bin` in a local backup location.

## Verify Backup Size and Hash

```powershell
Get-ChildItem -LiteralPath local-flash-backup.bin | Select-Object FullName,Length
Get-FileHash -Algorithm SHA256 -LiteralPath local-flash-backup.bin
```

Expected full Flash size for this board: `4,194,304` bytes.

## Analyze Metadata

```powershell
python tools\analyze_flash_metadata.py local-flash-backup.bin --format markdown
```

The analyzer reports the partition table and app image metadata.

## Restore Reference

Restoring writes to the board and replaces the current Flash contents.

Example restore command pattern:

```powershell
python -m esptool --port COM3 --baud 460800 write-flash 0x0 local-flash-backup.bin
```

Before restoring, confirm the backup file hash, board model, target port, and intended Flash image.
