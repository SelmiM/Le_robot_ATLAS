# ============================================================================
#  Build rules for the robot.
#
#  This replaces the Arduino IDE's hidden build. Nothing here is magic: it
#  lists the source files, says which parts of the Pico SDK we use, and asks
#  for a .uf2 at the end.
#
#  You do not run cmake by hand. Use:
#
#      python3 tools/setup.py     once, to fetch the SDK
#      python3 tools/build.py     every time you change the code
# ============================================================================

cmake_minimum_required(VERSION 3.13)

# Find the Pico SDK. tools/setup.py puts it in .pico-sdk/ beside this folder,
# which is what the default below points at; setting the PICO_SDK_PATH
# environment variable overrides it if you keep the SDK somewhere else.
if(DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
else()
    set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/../.pico-sdk)
endif()

if(NOT EXISTS ${PICO_SDK_PATH}/pico_sdk_init.cmake)
    message(FATAL_ERROR
        "Pico SDK not found at ${PICO_SDK_PATH}.\n"
        "Run:  python3 tools/setup.py")
endif()

# The Pico W: an RP2040 plus the CYW43439 WiFi chip. The WH is the same board
# with headers soldered on, so it uses the same setting.
#
# This MUST come before pico_sdk_init.cmake is included. Set it afterwards and
# the SDK has already decided you have a plain Pico -- it then quietly leaves
# out the WiFi support, and the first sign of trouble is
# "pico/cyw43_arch.h: No such file or directory".
set(PICO_BOARD pico_w CACHE STRING "Board type")

# ---------------------------------------------------------------------------
#  Two bits of housekeeping, so that a first build is quiet and a rebuild is
#  quick. Neither changes what the robot does.
#
#  BLUETOOTH. The SDK can build Bluetooth support from a submodule that
#  tools/setup.py deliberately does not fetch -- this robot has no use for it,
#  and it is a large download. Left to itself the SDK spots the gap and prints
#  a CMake WARNING about it, which looks like something you broke. SKIP_<NAME>
#  is the SDK's own supported way to say "not building this part", and it
#  turns that warning into one quiet line.
set(SKIP_PICO_BTSTACK TRUE)

#  PICOTOOL is what turns the compiled .elf into the .uf2 you copy onto the
#  Pico. There is no packaged copy here, so the SDK fetches and builds one --
#  and by default it does that INSIDE build/, which means `build.py --clean`
#  throws it away and pays for it all over again. Give it its own home beside
#  the SDK, where a clean cannot reach it. Gitignored, like .pico-sdk/.
set(PICOTOOL_FETCH_FROM_GIT_PATH ${CMAKE_CURRENT_LIST_DIR}/../.picotool)
# ---------------------------------------------------------------------------

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(robot C CXX ASM)

set(CMAKE_C_STANDARD   11)
set(CMAKE_CXX_STANDARD 17)

# Turn off two C++ features that cost memory and that we never use. Embedded
# C++ is ordinary C++ minus the parts that assume a computer with a hard disk.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti")

pico_sdk_init()

# Every .cpp in the project. Headers are found automatically.
add_executable(robot
    main.cpp
    drivers/HM01B0.cpp
    drivers/VL53L1X.cpp
)

# So that #include "Config.h" works from anywhere in the project.
target_include_directories(robot PRIVATE ${CMAKE_CURRENT_LIST_DIR})

# The phone page. Everything in web/ is turned into WebFiles.h, an array of
# bytes per file, which WebRadio.h serves. See EmbedWeb.cmake. The GLOB is
# re-checked on every build, so a file added to web/ is picked up without
# reconfiguring by hand.
file(GLOB WEB_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/web/*)
set(WEB_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/WebFiles.h)
add_custom_command(
    OUTPUT  ${WEB_HEADER}
    COMMAND ${CMAKE_COMMAND}
            -DWEB_DIR=${CMAKE_CURRENT_LIST_DIR}/web
            -DOUT=${WEB_HEADER}
            -P ${CMAKE_CURRENT_LIST_DIR}/EmbedWeb.cmake
    DEPENDS ${WEB_SOURCES} ${CMAKE_CURRENT_LIST_DIR}/EmbedWeb.cmake
    COMMENT "Embedding robot/web/ into WebFiles.h"
)
target_sources(robot PRIVATE ${WEB_HEADER})
target_include_directories(robot PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

# The parts of the Pico SDK we use. Each one is a real, documented library:
#   pico_stdlib      GPIO, timers, printf over USB
#   hardware_pwm     the motor speed control
#   hardware_i2c     the two-wire bus the display and the side sensor use
#   hardware_pio     \  the camera: custom hardware that shovels pixels
#   hardware_dma     /  into memory without the processor helping
#   pico_cyw43_arch_lwip_poll   WiFi, plus the lwIP network stack
#
# "poll" is deliberate. The alternative runs the network from an interrupt,
# which is faster but means network code and robot code can run at the same
# time -- and then every shared variable needs protecting. Polling keeps
# everything on one thread, in the order you can read in main().
target_link_libraries(robot
    pico_stdlib
    hardware_pwm
    hardware_i2c
    hardware_pio
    hardware_dma
    pico_cyw43_arch_lwip_poll
)

# Where to print. USB means messages arrive over the same cable you flash
# with; no second adapter needed.
pico_enable_stdio_usb(robot 1)
pico_enable_stdio_uart(robot 0)

# ...and over the debugger, if one is attached. RTT carries stdio through the
# SWD wires the Debug Probe already uses, so printf reaches the editor without
# a serial port existing at all. Two consequences worth knowing:
#
#   the output survives a halt. The USB serial above is served by code running
#   on the RP2040, so stopping at a breakpoint disconnects it and the port
#   disappears from your machine. RTT is read out through the probe, and the
#   probe does not care that the target is halted.
#
#   it is bidirectional, so a robot on RADIO_SERIAL takes its commands this
#   way too. SerialRadio in Radio.h calls getchar_timeout_us(), which reads
#   whichever stdio is enabled, and RTT is one of them.
#
# With no debugger attached this costs a buffer in RAM and nothing else: the
# writes see no reader and are dropped. See .vscode/launch.json, rttConfig.
pico_enable_stdio_rtt(robot 1)

# Produce robot.uf2 alongside the .elf. The .uf2 is the file you copy onto
# the Pico when it appears as a USB drive.
pico_add_extra_outputs(robot)
