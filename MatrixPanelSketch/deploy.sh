#!/bin/bash
# Exit immediately if any command fails
set -e

# Configuration variables
SKETCH_PATH="."
BOARD_TYPE="esp32:esp32:adafruit_matrixportal_esp32s3"
BAUD_RATE="921600"

echo "🔄 Updating index and ensuring core is installed..."
arduino-cli core update-index
arduino-cli core install esp32:esp32

echo "🔍 Autodetecting connected Matrix Portal S3..."
# Pulls the port specifically looking for the MatrixPortal or S3 identifier
PORT=$(arduino-cli board list | grep -E "adafruit_matrixportal_s3|ESP32-S3" | awk '{print $1}' | head -n 1)

if [ -z "$PORT" ]; then
    echo "⚠️  No specific board auto-detected. Falling back to /dev/ttyACM0 (Native USB default)."
    PORT="/dev/ttyACM0"
fi

echo "🔌 Target Port: $PORT"

echo "🏗️  Compiling sketch and exporting binary..."
arduino-cli compile --fqbn $BOARD_TYPE --output-dir ./build $SKETCH_PATH

echo "⚡ Triggering 1200-baud bootloader reset on native USB..."
# This forces the native ESP32-S3 USB stack into download mode headlessly
stty -F "$PORT" 1200 || true
sleep 1 

echo "🚀 Uploading firmware to Matrix Portal S3..."
arduino-cli upload -p $PORT --fqbn $BOARD_TYPE

echo "✅ Deployment successful!"
