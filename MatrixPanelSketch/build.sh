#!/bin/bash
# Exit immediately if any command fails
set -e

# Configuration variables
SKETCH_PATH="."
BOARD_TYPE="esp32:esp32:adafruit_matrixportal_esp32s3"
BUILD_DIR="./build"

echo "🧹 Cleaning previous build artifacts..."
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

echo "🏗️  Compiling sketch for Adafruit Matrix Portal S3..."
arduino-cli compile --fqbn "$BOARD_TYPE" --output-dir "$BUILD_DIR" "$SKETCH_PATH"

echo "✅ Compilation complete! Binary artifacts generated in: $BUILD_DIR"
ls -lh "$BUILD_DIR"
