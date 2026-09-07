#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
BUILD_DIR=$(mktemp -d "${TMPDIR:-/tmp}/crosspoint-power-clock.XXXXXX")
trap 'rm -rf "$BUILD_DIR"' EXIT
c++ -std=c++20 -O1 -Wall -Wextra \
  -Itest/power_clock/stubs -Ilib/hal -Ifreeink-sdk/libs/hardware/Rtc/include -Isrc \
  lib/hal/HalClock.cpp test/power_clock/main.cpp -o "$BUILD_DIR/power_clock"
"$BUILD_DIR/power_clock"
c++ -std=c++20 -O1 -Wall -Wextra -Isrc/network/ipp \
  src/network/ipp/RasterDecoder.cpp src/network/ipp/PageScaler.cpp \
  src/network/ipp/IppWriter.cpp src/network/ipp/IppParser.cpp \
  src/network/ipp/IppPrintService.cpp src/network/ipp/HttpIppConnection.cpp \
  test/power_clock/connection.cpp -o "$BUILD_DIR/connection"
"$BUILD_DIR/connection"
