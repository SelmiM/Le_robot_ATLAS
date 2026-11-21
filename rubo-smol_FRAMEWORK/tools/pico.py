#!/usr/bin/env python3
"""
Find the Pico, flash it, and talk to it.

    python3 tools/pico.py list       what is plugged in, and in which state
    python3 tools/pico.py flash      put build/robot.uf2 on it
    python3 tools/pico.py console    its serial port, through picocom

All of them work from inside the dev container, over the USB cable. You do
not need a terminal on your own machine, and you do not need to press BOOTSEL.

Under RADIO_SERIAL the console is how you drive the robot: type a command,
press Enter. Under RADIO_WIFI the serial port carries the startup text and
your printf() output, and nothing typed into it reaches the robot; commands
come from the phone page instead.

HOW THE BOARD IS FOUND. Everything on USB is listed under /sys/bus/usb/devices,
one folder per device, each with an idVendor and an idProduct file. Raspberry
Pi's vendor number is 2e8a. Which product number turns up tells you what the
board is currently doing:

    2e8a:0003   sitting in the bootloader, waiting to be given a program
    2e8a:0009   running your program, and offering a serial port
    2e8a:000a   the same, on a build that also does other things over USB

That folder is also where the serial port's identity comes from -- see
tty_node() below, which is the one genuinely container-specific part of this
file.

HOW FLASHING WORKS WITHOUT THE BUTTON. Holding BOOTSEL while you plug the
cable in is the manual way of reaching 2e8a:0003. A program built from this
project can also be asked to go there: pico_enable_stdio_usb in
robot/CMakeLists.txt brings in a small piece of the SDK that listens for
exactly that request, and "picotool reboot -f -u" makes it. So a reflash is
one command, with the robot left alone on the floor.

WHY THAT IS TWO picotool RUNS AND NOT ONE. "picotool load -f" does the whole
job in a single process: it asks the board to reboot, then waits for it to
come back and looks for it again. That second look goes through libusb, which
on Debian is built against libudev. Such a build enumerates USB once at
startup and afterwards keeps its list current from the udev monitor netlink
socket. That socket carries messages sent by systemd-udevd, and it belongs to
one network namespace. This container has neither a udevd of its own nor the
host's network namespace, so no message ever arrives. A board that reappears
after picotool started is a new USB device at a new address, and that process
will never see it. Hence the old behaviour: "picotool load -f" failed the
first time, and the second run, being a fresh process that enumerates from
scratch, flashed the board that was by then already sitting in BOOTSEL.

So the reboot and the load are two picotool runs here, and the waiting in
between is done in this file against /sys. /sys is the host's own device tree
and updates as things are plugged, unplugged and rebooted, which is also what
makes tty_node() below possible.

The button is still there if the program on the board has crashed badly enough
not to answer.
"""

import os
import re
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
UF2 = REPO / "build" / "robot.uf2"
CONFIG = REPO / "robot" / "Config.h"

USB_DEVICES = Path("/sys/bus/usb/devices")

RASPBERRY_PI = "2e8a"

# What each product number means, in words we can print.
PRODUCTS = {
    "0003": "BOOTSEL mode, waiting for a program",
    "0009": "running, serial port available",
    "000a": "running, serial port available",
}

BAUD = 115200


def read(path):
    """Read one line out of sysfs, or None if it is not there."""
    try:
        return path.read_text().strip()
    except OSError:
        return None


def find_picos():
    """Every Raspberry Pi USB device the host can see, as (folder, product)."""
    found = []
    if not USB_DEVICES.is_dir():
        return found

    for device in sorted(USB_DEVICES.iterdir()):
        if read(device / "idVendor") != RASPBERRY_PI:
            continue
        product = read(device / "idProduct")
        # The Debug Probe is a Raspberry Pi device too, and is not the board.
        if product == "000c":
            continue
        found.append((device, product))

    return found


def describe(device, product):
    name = read(device / "product") or "Raspberry Pi device"
    state = PRODUCTS.get(product, "unrecognised")
    return f"  {name}  ({RASPBERRY_PI}:{product}) -- {state}"


def in_bootsel(picos):
    return [d for d, p in picos if p == "0003"]


def running(picos):
    return [d for d, p in picos if p in ("0009", "000a")]


def no_board():
    print("No Pico found on USB.")
    print()
    print("  - is the cable plugged into both ends?")
    print("  - some micro-USB cables carry power only, and no data")
    print("  - on macOS or Windows the container cannot see USB at all")
    print("    (see .devcontainer/devcontainer.json)")
    sys.exit(1)


# ---------------------------------------------------------------------------
#  list
# ---------------------------------------------------------------------------

def command_list():
    picos = find_picos()
    if not picos:
        no_board()

    print(f"{len(picos)} board(s):")
    for device, product in picos:
        print(describe(device, product))


# ---------------------------------------------------------------------------
#  flash
# ---------------------------------------------------------------------------

# How long to give the board to come back as 2e8a:0003, and how often to look
# while we wait. On a healthy board the bootloader is up in well under a
# second; the rest of the allowance is for a slow hub.
BOOTSEL_TIMEOUT = 10.0
BOOTSEL_POLL = 0.1


def by_hand():
    """What to do when the board will not go to the bootloader by itself."""
    print()
    print("If it is running a program that has crashed, it cannot be asked")
    print("to go to the bootloader, and you have to do it by hand: hold the")
    print("BOOTSEL button, unplug and replug the cable, then run this again.")
    sys.exit(1)


def wait_for_bootsel():
    """Watch /sys until a board shows up in BOOTSEL mode. True if one does."""
    deadline = time.monotonic() + BOOTSEL_TIMEOUT
    while True:
        if in_bootsel(find_picos()):
            return True
        if time.monotonic() >= deadline:
            return False
        time.sleep(BOOTSEL_POLL)


def command_flash():
    if not UF2.exists():
        sys.exit(f"{UF2.relative_to(REPO)} does not exist -- run: python3 tools/build.py")

    picos = find_picos()
    if not picos:
        no_board()

    if not in_bootsel(picos):
        print("asking the board to go to the bootloader ...", flush=True)
        # This run only sends the request and returns; whether the board came
        # back is not something it can tell us from in here (see the note at
        # the top of the file), so its exit status is not worth reading. /sys
        # gives the honest answer a moment later.
        subprocess.run(["picotool", "reboot", "-f", "-u"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if not wait_for_bootsel():
            print()
            print("The board did not come back in BOOTSEL mode within "
                  f"{BOOTSEL_TIMEOUT:.0f} seconds.")
            by_hand()

    # -x starts the program once it has been written, so the robot is running
    # again when this returns rather than sitting idle in the bootloader.
    print(f"flashing {UF2.relative_to(REPO)} ...", flush=True)
    result = subprocess.run(["picotool", "load", "-x", "-t", "uf2", str(UF2)])

    if result.returncode != 0:
        print()
        print("picotool could not write to the board.")
        by_hand()

    print("done -- the robot is running the new program")


# ---------------------------------------------------------------------------
#  console
# ---------------------------------------------------------------------------

def tty_node(device):
    """
    Work out the board's serial port, as a path that can be opened in here.

    On an ordinary Linux machine this is /dev/ttyACM0 and udev created it when
    you plugged the cable in. In the container there is no udev, and /dev is
    the container's own -- so the port is missing even though the board is
    plainly visible.

    What we do have is /host-dev, the host's real /dev bind-mounted in by
    devcontainer.json, and its postStartCommand symlinks /dev/ttyACM0-3 to
    /host-dev/ttyACM0-3. So /dev/ttyACM0 works as usual once a board is
    plugged in; for a higher number there is no link, and the /host-dev path
    is used directly.

    /sys tells us which ttyACMn belongs to this board -- see the module
    docstring. devcontainer.json's cgroup rule for major 166 is what makes
    the node usable once found, not this function.
    """
    matches = sorted(device.glob("*/tty/ttyACM*"))
    if not matches:
        return None

    name = matches[0].name
    for node in (Path("/dev") / name, Path("/host-dev") / name):
        if node.exists():
            return node
    return None


def config_value(name):
    """What robot/Config.h #defines name to, as text, or None."""
    # ^\s*#define skips the commented-out examples, which start with //.
    match = re.search(rf"^\s*#define\s+{name}\s+(\S+)", CONFIG.read_text(), re.MULTILINE)
    return match.group(1) if match else None


def command_console():
    if config_value("RADIO") == "RADIO_WIFI":
        print("RADIO is RADIO_WIFI in Config.h: this port carries the startup text")
        print("and printf() output only. Commands come from the phone page.")
        print()

    picos = find_picos()
    if not picos:
        no_board()

    live = running(picos)
    if not live:
        if in_bootsel(picos):
            sys.exit("The board is in BOOTSEL mode: it is not running a program, "
                     "so it has nothing to say.\nFlash it first: python3 tools/build.py --flash")
        sys.exit("Found a Raspberry Pi device, but it is not offering a serial port.")

    node = tty_node(live[0])
    if node is None:
        sys.exit("The board is running but the host has not given it a serial port.\n"
                 "Unplug it, plug it back in, and try again.")

    # Being handed the node (tty_node, above) is not the same as being
    # allowed to open it: on a host missing the tty-specific udev rule (see
    # the /host-dev comment in devcontainer.json), it is still root:dialout
    # mode 660, unreachable from in here regardless of the container's own
    # group memberships. picocom would just fail with a bare "Permission
    # denied" in that case, so check first and say why.
    try:
        os.close(os.open(node, os.O_RDWR))
    except OSError as error:
        if isinstance(error, PermissionError):
            sys.exit(f"Found {node}, but the container isn't allowed to open it.\n"
                      "Add a udev rule on the host to make it world-accessible:\n"
                      '    SUBSYSTEM=="tty", ATTRS{idVendor}=="2e8a", MODE="0666"\n'
                      f"or run the console from a terminal on the host instead: screen {node} {BAUD}")
        sys.exit(f"Found {node}, but couldn't open it: {error.strerror or error}.")

    # flush: execvp replaces this process, and anything Python is still
    # holding in its buffer (when the output is a pipe) is lost with it.
    print(f"{node} at {BAUD} baud -- ctrl-A ctrl-X to quit")
    print(flush=True)
    # --imap lfcrlf: the robot ends its lines with \n alone, and without this
    # the terminal walks diagonally down the screen.
    #
    # --omap crlf: Enter sends CR, and SerialRadio::receive() in Radio.h skips
    # CR and ends a command only on LF. Without this mapping everything you
    # type is buffered on the robot and no command ever runs.
    #
    # --echo with --emap crcrlf: the robot does not echo the characters it
    # receives, so local echo is the only way to see what you are typing.
    # crcrlf makes Enter show as a new line rather than a return to column
    # zero.
    os.execvp("picocom", ["picocom", "-b", str(BAUD),
                          "--imap", "lfcrlf",
                          "--omap", "crlf",
                          "--echo", "--emap", "crcrlf",
                          str(node)])


COMMANDS = {
    "list": command_list,
    "flash": command_flash,
    "console": command_console,
}


def main():
    args = sys.argv[1:]
    if len(args) != 1 or args[0] not in COMMANDS:
        if (__doc__):
            print(__doc__.strip().splitlines()[0])
        print()
        for name in COMMANDS:
            print(f"    python3 tools/pico.py {name}")
        sys.exit(1)

    COMMANDS[args[0]]()


if __name__ == "__main__":
    main()
