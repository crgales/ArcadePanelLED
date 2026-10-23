#!/bin/bash

# LED Marquee Game Trigger Script
LED_MARQUEE_HOME="/userdata/system/ledmarquee"
SERIAL_PORT="/dev/ttyACM0"

PIXELCADE_HOME="/userdata/system/pixelcade"

# Define the log file location
LOG_FILE="/tmp/ledmarquee_game_trigger.log"

# Add a timestamp to every log entry
timestamp() {
    date "+%Y-%m-%d %H:%M:%S"
}

case "$1" in
    gameStart)
        if [[ -z "${2:-}" || -z "${5:-}" ]]; then
            echo "[$(timestamp)] [gameStart] ERROR: Missing system or ROM path." >> "$LOG_FILE"
            exit 1
        fi

        # 1. Parse the clean ROM name (removes path and extension)
        rom_name="$(basename "$5")"
        rom_no_ext="${rom_name%.*}"
        image_path="$PIXELCADE_HOME/$2/$rom_no_ext.png"
        
        # 2. Log the execution data
        echo "[$(timestamp)] [gameStart] System: $2 | ROM Path: $5" >> "$LOG_FILE"
        echo "[$(timestamp)] [gameStart] Extracted clean ROM name: \"$rom_no_ext\"" >> "$LOG_FILE"
        echo "[$(timestamp)] [gameStart] Image path: \"$image_path\"" >> "$LOG_FILE"

        if [[ ! -f "$image_path" ]]; then
            echo "[$(timestamp)] [gameStart] ERROR: Image not found: \"$image_path\"" >> "$LOG_FILE"
            exit 1
        fi

        echo "[$(timestamp)] [gameStart] Displaying image: \"$image_path\"" >> "$LOG_FILE"
        "$LED_MARQUEE_HOME/venv/bin/python3" \
            "$LED_MARQUEE_HOME/MatrixPanelHost/MatrixPanelHost.py" \
            -p "$SERIAL_PORT" image "$image_path" >> "$LOG_FILE" 2>&1
        status=$?
        if (( status != 0 )); then
            echo "[$(timestamp)] [gameStart] ERROR: Image command failed (exit $status)." >> "$LOG_FILE"
        fi
        exit "$status"
        ;;
        
    gameStop)
        echo "[$(timestamp)] [gameStop] Game exited. Clearing marquee." >> "$LOG_FILE"
        "$LED_MARQUEE_HOME/venv/bin/python3" \
            "$LED_MARQUEE_HOME/MatrixPanelHost/MatrixPanelHost.py" \
            -p "$SERIAL_PORT" image "$LED_MARQUEE_HOME/arcade.png" >> "$LOG_FILE" 2>&1
        ;;
esac
