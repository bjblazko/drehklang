#!/usr/bin/env python3
"""Builds Drehklang from source and flashes it onto the board.

Works the same on Windows, macOS and Linux. There are no binary releases
(ADR 0027): this script is how a board gets Drehklang. It

  1. finds PlatformIO, or installs it into .venv-pio/ after asking,
  2. builds both firmwares: Drehklang itself for the ESP32-S3 and the
     Bluetooth firmware (bt/) for the ESP32-U4WDH,
  3. flashes whichever chip the USB-C plug reaches, then asks to turn the
     plug over and flashes the other one (device.md: the way the plug is
     turned decides which chip answers).

Usage:
  python3 scripts/install.py            # build and flash both chips
  python3 scripts/install.py --only s3  # just one of them (s3 or bt)
  python3 scripts/install.py --skip-build

On Windows run it as `py scripts\\install.py`. Only the standard library is
used; the serial work goes through PlatformIO's own Python, which has
pyserial.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
import venv
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
LOCAL_VENV = REPO / ".venv-pio"
PLATFORMIO_REQUIREMENT = "platformio>=6.1,<7"

NATIVE_USB = "303A:1001"  # the S3's own USB port (TinyUSB or bootloader)
CH340 = "1A86:7523"       # the USB-serial chip in front of the U4WDH

TARGETS = {
    "s3": {
        "name": "Drehklang (ESP32-S3)",
        "build": ["run", "-e", "esp32-s3"],
        "chip": "ESP32-S3",
    },
    "bt": {
        "name": "Bluetooth firmware (ESP32-U4WDH)",
        "build": ["run", "-d", "bt", "-e", "esp32-bt"],
        "chip": "ESP32",
    },
}
CHIP_TO_TARGET = {spec["chip"]: key for key, spec in TARGETS.items()}


class InstallError(Exception):
    pass


# --- PlatformIO -----------------------------------------------------------

def venv_executable(venv_dir, name):
    scripts = "Scripts" if os.name == "nt" else "bin"
    suffix = ".exe" if os.name == "nt" else ""
    return venv_dir / scripts / (name + suffix)


def find_platformio():
    candidates = [
        shutil.which("pio"),
        venv_executable(Path.home() / ".platformio" / "penv", "pio"),
        venv_executable(LOCAL_VENV, "pio"),
    ]
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return Path(candidate)
    return None


def install_platformio():
    answer = input(f"PlatformIO was not found. Install it into {LOCAL_VENV.name}/ "
                   "(about 50 MB, from PyPI)? [Y/n] ").strip().lower()
    if answer not in ("", "y", "yes", "j", "ja"):
        raise InstallError("PlatformIO is needed: https://platformio.org/install/cli")
    venv.create(LOCAL_VENV, with_pip=True)
    python = venv_executable(LOCAL_VENV, "python")
    subprocess.run([str(python), "-m", "pip", "install", "--upgrade",
                    PLATFORMIO_REQUIREMENT], check=True)
    return venv_executable(LOCAL_VENV, "pio")


def platformio_python(pio):
    """PlatformIO's own interpreter, which has pyserial. Asked for, since
    it is not always next to pio (Homebrew, for one)."""
    result = subprocess.run([str(pio), "system", "info", "--json-output"],
                            capture_output=True, text=True, check=True)
    return json.loads(result.stdout)["python_exe"]["value"]


# --- Building -------------------------------------------------------------

def build(pio, targets):
    print("The first build downloads the toolchain (about 1 GB) and takes a while.")
    for key in targets:
        print(f"\n== Building {TARGETS[key]['name']} ==")
        subprocess.run([str(pio)] + TARGETS[key]["build"], cwd=REPO, check=True)


# --- Finding the board ----------------------------------------------------

def list_ports(pio):
    result = subprocess.run([str(pio), "device", "list", "--json-output"],
                            capture_output=True, text=True, check=True)
    ports = {}
    for device in json.loads(result.stdout or "[]"):
        hwid = device.get("hwid", "").upper()
        for kind in (NATIVE_USB, CH340):
            if kind in hwid:
                ports[device["port"]] = kind
    return ports


def wait_for_board(pio, seconds=30):
    ports = wait_until(lambda: list_ports(pio), seconds)
    return next(iter(ports.items())) if ports else None


def find_board(pio):
    print("\nLooking for the board on USB ...")
    while True:
        board = wait_for_board(pio)
        if board:
            return board
        input("No board found. Plug it in (or turn the USB-C plug over), "
              "then press Enter. Ctrl+C stops. ")


def guess_target(kind, remaining):
    """Native USB is always the S3; the CH340 usually reaches the U4WDH."""
    if kind == NATIVE_USB:
        return "s3"
    return "bt" if "bt" in remaining else "s3"


# --- Flashing -------------------------------------------------------------

def native_ports(pio):
    return [p for p, kind in list_ports(pio).items() if kind == NATIVE_USB]


def wait_until(condition, seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        result = condition()
        if result:
            return result
        time.sleep(0.5)
    return None


def touch_into_bootloader(pio, port):
    """Drehklang uses TinyUSB, whose port reboots into the ROM bootloader
    when opened at 1200 baud. The bootloader may come back under another
    port name. A chip already in the bootloader ignores the touch."""
    code = "import serial, sys; serial.Serial(sys.argv[1], 1200).close()"
    subprocess.run([str(platformio_python(pio)), "-c", code, port],
                   capture_output=True)
    if not wait_until(lambda: port not in native_ports(pio), 3):
        return port
    # The old name can show up once more while the port goes away; wait
    # for a new one, and take any after that (Windows may keep the name).
    ports = wait_until(lambda: [p for p in native_ports(pio) if p != port], 10)
    ports = ports or wait_until(lambda: native_ports(pio), 5)
    return ports[0] if ports else port


def run_and_capture(command):
    process = subprocess.Popen(command, cwd=REPO, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True,
                               errors="replace")
    lines = []
    for line in process.stdout:
        print(line, end="")
        lines.append(line)
    return process.wait(), "".join(lines)


def answering_chip(output):
    match = re.search(r"This chip is (ESP32-S3|ESP32)\b", output)
    return match.group(1) if match else None


def explain_failure(output, port):
    if "Permission denied" in output or "could not open port" in output:
        hint = "No permission for the serial port."
        if sys.platform.startswith("linux"):
            hint += " Add yourself to the 'dialout' group and log in again."
        return hint
    return (f"Flashing over {port} failed (see above). Turn the USB-C plug "
            "over, or hold BOOT while plugging in, and run this again.")


def flash(pio, key, port, kind, redirected=False):
    if kind == NATIVE_USB:
        port = touch_into_bootloader(pio, port)
    print(f"\n== Flashing {TARGETS[key]['name']} over {port} ==")
    command = [str(pio)] + TARGETS[key]["build"] + ["-t", "upload",
                                                    "--upload-port", port]
    status, output = run_and_capture(command)
    if status == 0:
        return key
    other = CHIP_TO_TARGET.get(answering_chip(output))
    if other and other != key and not redirected:
        print(f"\nThat port reaches the {TARGETS[other]['chip']}, "
              f"so it gets {TARGETS[other]['name']}.")
        return flash(pio, other, port, kind, redirected=True)
    raise InstallError(explain_failure(output, port))


def ask_to_turn_plug(key):
    input(f"\nNext: {TARGETS[key]['name']}. Unplug the board, turn the USB-C "
          "plug over, plug it back in, then press Enter. ")


def flash_all(pio, targets):
    remaining = list(targets)
    while remaining:
        port, kind = find_board(pio)
        key = guess_target(kind, remaining)
        if key not in remaining:
            ask_to_turn_plug(remaining[0])
            continue
        flashed = flash(pio, key, port, kind)
        if flashed in remaining:
            remaining.remove(flashed)
            print(f"== {TARGETS[flashed]['name']}: done ==")
        if remaining:
            ask_to_turn_plug(remaining[0])


# --- Main -----------------------------------------------------------------

def parse_args():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--only", choices=sorted(TARGETS),
                        help="build and flash just this chip")
    parser.add_argument("--skip-build", action="store_true",
                        help="flash what was built last")
    return parser.parse_args()


def main():
    args = parse_args()
    targets = [args.only] if args.only else ["s3", "bt"]
    try:
        pio = find_platformio() or install_platformio()
        if not args.skip_build:
            build(pio, targets)
        flash_all(pio, targets)
    except InstallError as error:
        print(f"\n!! {error}", file=sys.stderr)
        return 1
    except subprocess.CalledProcessError as error:
        print(f"\n!! {' '.join(map(str, error.cmd))} failed.", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\nStopped.")
        return 130
    print("\nAll done. Drehklang starts by itself; if the screen stays dark, "
          "unplug the board and plug it back in.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
