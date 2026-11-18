#!/usr/bin/env python3
"""A stand-in for adb that emulates phones on this machine, to test harness.py end to end.

Each fake phone is a folder under $FAKE_ADB_ROOT/<serial>/; /data/local/tmp/ar in a shell
command or push/pull path maps into it, and `shell` runs the command locally. getprop, ip,
dumpsys, logcat, svc, am, input and install answer as a phone would. Point the harness at it
with ADB="python3 tools/fake_adb.py" is not possible (ADB is one program), so use the wrapper:

  FAKE_ADB_ROOT=/tmp/fakephones FAKE_ADB_PHONES="emu-1,emu-2,emu-3,emu-4" ADB=tools/fake_adb.py python3 harness.py devices
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(os.environ.get("FAKE_ADB_ROOT", "/tmp/fakephones"))
PHONES = [p for p in os.environ.get("FAKE_ADB_PHONES", "emu-1,emu-2").split(",") if p]
MODELS = {0: ("SM-G900V", "6.0.1", "23", "armeabi-v7a"), 1: ("Pixel 6", "14", "34", "arm64-v8a")}


def phone_dir(serial):
    path = ROOT / serial
    path.mkdir(parents=True, exist_ok=True)
    return path


def local(serial, text):
    return text.replace("/data/local/tmp/ar", str(phone_dir(serial) / "ar"))


def shell(serial, command):
    index = PHONES.index(serial)
    model, release, sdk, abi = MODELS[0 if index == 0 else 1]
    if command.startswith("getprop"):
        print(f"{model}\n{release}\n{sdk}\n{abi}")
        return 0
    if command.startswith("ip -4"):
        print("3: lo    inet 127.0.0.1/8 scope host lo")
        return 0
    if command.startswith("dumpsys battery"):
        print(f"  temperature: {300 + index * 7}")
        print("1800000\n1200000")
        return 0
    if command.startswith("logcat -d"):
        print("I ar_sim  : ar_sim locks held: multicast, wifi mode 3, wake")
        return 0
    if command.split()[0] in ("svc", "am", "input", "logcat"):
        return 0
    result = subprocess.run(["bash", "-c", local(serial, command)], capture_output=True, text=True)
    sys.stdout.write(result.stdout)
    sys.stdout.write(result.stderr)
    return result.returncode


def main():
    args = sys.argv[1:]
    serial = None
    if args[:1] == ["-s"]:
        serial, args = args[1], args[2:]
    command = args[0]
    if command == "devices":
        print("List of devices attached")
        for phone in PHONES:
            print(f"{phone}\tdevice")
        return 0
    if command == "shell":
        return shell(serial, " ".join(args[1:]))
    if command == "push":
        target = Path(local(serial, args[2]))
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(args[1], target)
        return 0
    if command == "pull":
        source = Path(local(serial, args[1]))
        if not source.is_file():
            print(f"adb: error: remote object '{args[1]}' does not exist", file=sys.stderr)
            return 1
        shutil.copy(source, args[2])
        return 0
    if command in ("install", "uninstall", "tcpip", "connect"):
        return 0
    print(f"fake adb: unsupported {args}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
