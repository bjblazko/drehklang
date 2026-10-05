#!/usr/bin/env bash
# Flashes the Bluetooth firmware (bt/) to the board's SECOND MCU, the
# ESP32-U4WDH (ADR 0027). It is reached through the CH340 with the USB-C
# plug turned so the CH340 shows up (/dev/cu.usbserial-*), not the S3's
# native USB port. Checks with esptool that an ESP32 -- not the S3 --
# answers there before writing anything.
#
# Back to the factory image: hardware-backups/restore.sh secondary
#
# Usage: flash-bt-mcu.sh [--port /dev/cu.XXXX]
set -euo pipefail
cd "$(dirname "$0")/.."

PORT=""
if [[ "${1:-}" == "--port" ]]; then
  PORT="$2"
fi

if [[ -z "$PORT" ]]; then
  PORT="$(pio device list --json-output 2>/dev/null | python3 -c '
import json, sys
for d in json.load(sys.stdin):
    if "1A86:7523" in d.get("hwid", "").upper():
        print(d["port"])
        break
' || true)"
fi

if [[ -z "$PORT" ]]; then
  echo "The CH340 is not connected. Turn the USB-C plug over and try again." >&2
  exit 1
fi

PYTHON="$HOME/.platformio/penv/bin/python"
CHIP="$("$PYTHON" -m esptool --port "$PORT" chip_id 2>&1 | grep -E '^Chip (type|is)' || true)"
echo "$CHIP"
if [[ "$CHIP" != *ESP32-U4WDH* && "$CHIP" != *"ESP32-D"* ]]; then
  echo "That port does not reach the ESP32-U4WDH. Turn the USB-C plug over and try again." >&2
  exit 1
fi

echo "== Flashing bt/ (ESP32-U4WDH) via $PORT =="
pio run -d bt -e esp32-bt -t upload --upload-port "$PORT"
echo "== Flash OK =="
