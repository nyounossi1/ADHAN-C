# USB flash procedure (partition table changes)

The partition table is written by a USB flash only. OTA updates write just the app
slot, so a device keeps whatever table it was last flashed with:

| Table | App slot size | Used by |
|---|---|---|
| Arduino `default` | 1,310,720 B | devices last USB-flashed before AD-49 |
| `min_spiffs` | 1,966,080 B | USB-flashed between AD-49 and AD-54 |
| `partitions_16mb.csv` (AD-54) | 4,194,304 B x2 | USB-flashed from AD-54 on |

All field devices use the ESP32-WROOM-32E-N16 (16 MB flash). A device on an older
table is told "Needs USB reflash" (AD-53) when an update would not fit its slot.

## Flash a device (keeps Wi-Fi credentials and settings)

1. Connect USB-C (CP2102N appears as `/dev/cu.usbserial-*`).
2. Optional backup of settings:
   `esptool read-flash 0x9000 0x5000 nvs_backup.bin`
3. `pio run -t upload --upload-port <port>`
   This writes bootloader, partition table, `boot_app0` (resets otadata to app0) and the app.
   NVS (0x9000, 0x5000) is not touched because every table keeps NVS at the same offset.
4. Do NOT run `pio run -t erase` unless you want to wipe Wi-Fi credentials and settings.
5. Verify: the clock boots, joins Wi-Fi without the portal, and
   `esptool read-flash 0x8000 0x1000 pt.bin` then `gen_esp32part.py pt.bin` shows app0/app1 as 4M.

## Restore settings
`esptool write-flash 0x9000 nvs_backup.bin`

## Layout (partitions_16mb.csv)
nvs 0x9000/0x5000, otadata 0xe000/0x2000, app0 0x10000/4 MB, app1 0x410000/4 MB,
coredump 0x810000/64 KB. 0x820000-0xFFFFFF (about 7.9 MB) is unallocated.
