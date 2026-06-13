#!/usr/bin/env bash
set -euo pipefail

ENV="esp32-2432s028r"
BUILD_DIR=".pio/build/$ENV"
OUT_DIR="static/firmware/$ENV"

echo "Building firmware..."
pio run -e "$ENV"

echo "Merging firmware binary..."
mkdir -p "$OUT_DIR"

esptool.py --chip esp32 merge_bin \
  -o "$OUT_DIR/merged.bin" \
  --flash_mode dio \
  --flash_freq 40m \
  --flash_size 4MB \
  0x1000  "$BUILD_DIR/bootloader.bin" \
  0x8000  "$BUILD_DIR/partitions.bin" \
  0xe000  "$BUILD_DIR/boot_app0.bin" \
  0x10000 "$BUILD_DIR/firmware.bin"

echo "Done: $OUT_DIR/merged.bin"
ls -lh "$OUT_DIR/merged.bin"