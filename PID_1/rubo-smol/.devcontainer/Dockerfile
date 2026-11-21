FROM mcr.microsoft.com/devcontainers/cpp:3-debian13

# The Raspberry Pi Pico toolchain, for the firmware in robot/.
#   gcc-arm-none-eabi + newlib   the cross compiler and its C/C++ library
#   cmake, ninja-build           the build system
#   libusb-1.0-0-dev             needed to build picotool, which makes the .uf2
#   picotool                     flashes the board down the USB cable, from in
#                                here -- see the note below, it is not the same
#                                picotool the SDK builds
#   openocd                      drives the Debug Probe over SWD: flashes the
#                                board, and is the server behind F5
#   gdb-multiarch                the debugger itself, which cortex-debug runs
#                                and points at build/robot.elf
#   picocom                      the serial terminal, for the robot's printf
#   usbutils                     lsusb, for "is the board even plugged in"
#
# ...and Node, for the fake robot and the Node client in client/. The
# optional serialport package is not pure JavaScript, but it ships prebuilt
# binaries for this platform and does not compile anything here.
#
# openocd and gdb-multiarch are only used if you have a Raspberry Pi Debug
# Probe. Without one, picotool does the flashing over the ordinary USB cable.
# See .vscode/launch.json for what the probe serves.
#
# The Pico SDK itself is NOT installed here. tools/setup.py clones it into
# .pico-sdk/ inside the workspace, so the version is pinned by the project
# rather than baked into the image.
RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
    && apt-get -y install --no-install-recommends \
        gcc-arm-none-eabi \
        libnewlib-arm-none-eabi \
        libstdc++-arm-none-eabi-newlib \
        libusb-1.0-0-dev \
        picotool \
        openocd \
        gdb-multiarch \
        picocom \
        usbutils \
        cmake \
        ninja-build \
        nodejs \
        npm \
    && rm -rf /var/lib/apt/lists/*

# Tooling for the wiring diagram, Robot_Schema_completed.fzz. Its own layer: Fritzing
# pulls in Qt and is large.
#   fritzing       opens and exports the sketch. There is no display in here,
#                  so run it headless; this exports every view of every .fzz
#                  in <dir> to SVG:
#                    QT_QPA_PLATFORM=offscreen fritzing -svg <dir>
#   librsvg2-bin   rsvg-convert, to turn those SVGs (or a part's own SVGs)
#                  into PNGs you can look at
RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
    && apt-get -y install --no-install-recommends \
        fritzing \
        librsvg2-bin \
    && rm -rf /var/lib/apt/lists/*

# WHY picotool COMES FROM apt AND NOT FROM THE BUILD.
#
# The SDK fetches and builds its own picotool into .picotool/, but passes
# -DPICOTOOL_NO_LIBUSB=1 when it does (.pico-sdk/tools/Findpicotool.cmake).
# That copy exists only to turn the .elf into a .uf2, and it says as much if
# you run it: "compiled without USB support". It cannot talk to a board.
# Debian's package is linked against libusb and can.

# Both picotool and openocd need to write to the probe's USB node, and those
# arrive from the host owned by root:root -- setting it otherwise needs a udev
# rule on the host, and not everyone can add one. No udev runs in here to
# relabel them either. So the privilege lives in a wrapper.
#
# sudo -n never prompts; it works because the devcontainers image gives the
# vscode user NOPASSWD sudo. exec so that a SIGTERM reaches sudo, which relays
# it to the real program -- without that, a root-owned openocd would keep hold
# of the probe and the next F5 would fail.
#
# devcontainer.json runs as root by default (see ROBOT_DEVCONTAINER_USER
# there). Root does not need sudo to reach the USB nodes, and on the lab's
# rootless Docker `sudo -n` as root failed with "a password is required", so
# the wrappers skip sudo when they are already root.
#
# The openocd one is named openocd-sudo because .vscode/launch.json points
# "serverpath" at it by name. The picotool one deliberately shadows the real
# binary: /usr/local/bin comes before /usr/bin, so plain `picotool` works.
RUN printf '%s\n' '#!/bin/sh' \
        'if [ "$(id -u)" = "0" ]; then exec /usr/bin/openocd "$@"; fi' \
        'exec sudo -n /usr/bin/openocd "$@"' \
      > /usr/local/bin/openocd-sudo \
    && printf '%s\n' '#!/bin/sh' \
        'if [ "$(id -u)" = "0" ]; then exec /usr/bin/picotool "$@"; fi' \
        'exec sudo -n /usr/bin/picotool "$@"' \
      > /usr/local/bin/picotool \
    && chmod 755 /usr/local/bin/openocd-sudo /usr/local/bin/picotool

# When devcontainer.json runs as root (the default, see
# ROBOT_DEVCONTAINER_USER there) on rootful Docker, or whenever the
# workspace's owner differs from the user in here, git refuses to touch a
# repo it does not own: "detected dubious ownership". This is the fix git
# itself suggests, pre-applied so you never see the error.
#
# "*" rather than a literal /workspaces/<name> path: devcontainer.json pins no
# workspaceFolder, so the mount path is /workspaces/<host folder's own name>,
# which is whatever a student happened to clone (or fork) this repo into.
RUN git config --system --add safe.directory '*'
