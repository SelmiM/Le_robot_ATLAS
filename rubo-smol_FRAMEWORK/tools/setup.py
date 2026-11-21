#!/usr/bin/env python3
"""
Check build tools and fetch the pinned Pico SDK. Run once outside the container.

    python3 tools/setup.py             fetch the SDK, then build
    python3 tools/setup.py --no-build  fetch the SDK only

The SDK and its USB, WiFi and TCP/IP submodules land in the ignored
.pico-sdk/ directory. The compiler, CMake and Ninja must already be installed;
missing tools are reported with Debian/Ubuntu package hints. The devcontainer
provides these tools and runs this script when it is created.
"""

import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SDK = REPO / ".pico-sdk"

# Pinned so that everyone gets an identical toolchain, and so that an SDK
# release cannot break the course halfway through a term.
SDK_VERSION = "2.2.0"

# Only the submodules we actually use. The full set is much larger and mostly
# Bluetooth and TLS, which this robot does not touch.
SDK_SUBMODULES = [
    "lib/tinyusb",       # USB, for printf over the programming cable
    "lib/cyw43-driver",  # the WiFi chip
    "lib/lwip",          # TCP/IP
]

# Debian and Ubuntu package names for the tools we cannot install ourselves.
# The dev container installs the same set; see .devcontainer/Dockerfile.
APT_PACKAGES = [
    "gcc-arm-none-eabi",
    "libnewlib-arm-none-eabi",
    "libstdc++-arm-none-eabi-newlib",
    "libusb-1.0-0-dev",   # the SDK builds its own picotool, and needs this
    "picotool",           # ...but that one cannot flash; see tools/pico.py
    "picocom",            # the serial terminal
    "cmake",
    "ninja-build",
    "git",
]


# Without these you cannot build at all.
REQUIRED = {
    "arm-none-eabi-gcc": "the compiler that makes ARM code",
    "cmake": "works out what to build",
    "ninja": "runs the build",
    "git": "fetches the SDK",
}

# Without these you can build, but you cannot put the result on a board or
# read what it says. Worth a word, not worth stopping for.
OPTIONAL = {
    "picotool": "puts the program on the board (tools/pico.py flash)",
    "picocom": "the serial terminal (tools/pico.py console)",
}


def install_hint():
    print("\nOn Debian or Ubuntu, install them with:\n")
    print("    sudo apt-get update")
    print("    sudo apt-get install -y " + " ".join(APT_PACKAGES))


def check_tools():
    """Make sure the compiler and build tools are present before we start."""
    missing = [name for name in REQUIRED if shutil.which(name) is None]

    if missing:
        print("Some tools are missing:\n")
        for name in missing:
            print(f"    {name:<20} {REQUIRED[name]}")
        install_hint()
        print("\nThen run this script again.")
        sys.exit(1)

    absent = [name for name in OPTIONAL if shutil.which(name) is None]
    if absent:
        print("Note: building will work, but these are not installed:\n")
        for name in absent:
            print(f"    {name:<20} {OPTIONAL[name]}")
        install_hint()
        print()


def run(*args, cwd=None):
    subprocess.run(list(args), cwd=cwd or REPO, check=True)


def install_sdk():
    if (SDK / "pico_sdk_init.cmake").exists():
        print(f"Pico SDK already present in {SDK.relative_to(REPO)}/")
        return

    print(f"fetching the Pico SDK {SDK_VERSION} (a few hundred megabytes) ...")
    # --depth 1 fetches only the one version we asked for, rather than the
    # whole history. Much faster, and we are never going to look at the log.
    run("git", "clone", "--quiet", "--branch", SDK_VERSION, "--depth", "1",
        "https://github.com/raspberrypi/pico-sdk.git", str(SDK))

    print("fetching the SDK's own dependencies ...")
    run("git", "submodule", "update", "--init", "--depth", "1",
        *SDK_SUBMODULES, cwd=SDK)


def main():
    check_tools()
    install_sdk()

    if "--no-build" not in sys.argv:
        subprocess.run([sys.executable, str(REPO / "tools" / "build.py")], check=True)

    print("\nReady. From now on you only need:  python3 tools/build.py")


if __name__ == "__main__":
    main()
