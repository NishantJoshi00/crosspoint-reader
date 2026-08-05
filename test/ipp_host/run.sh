#!/bin/sh
# Builds the IPP host harness from the firmware's own protocol sources
# (src/network/ipp) — no device or PlatformIO needed. Pattern follows
# freeink-sdk/libs/book/FreeInkBook/test/host/run.sh.
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/crosspoint-ipp-host"
mkdir -p "$BUILD_DIR"

IPP_SRC=../../src/network/ipp
c++ -std=c++20 -O1 -Wall -Wextra -I"$IPP_SRC" \
  "$IPP_SRC"/RasterDecoder.cpp \
  "$IPP_SRC"/PageScaler.cpp \
  "$IPP_SRC"/IppWriter.cpp \
  "$IPP_SRC"/IppParser.cpp \
  "$IPP_SRC"/IppPrintService.cpp \
  "$IPP_SRC"/HttpIppConnection.cpp \
  main.cpp \
  -o "$BUILD_DIR/ipp_host"

echo "built: $BUILD_DIR/ipp_host"
exec "$BUILD_DIR/ipp_host" "$@"
