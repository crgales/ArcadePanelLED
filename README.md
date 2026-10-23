# ArcadePanelLED

An LED marquee for an arcade cabinet running Batocera, driven over USB serial
by an Adafruit MatrixPortal S3.

The current configuration uses three 128 x 64 panels for a total display size of
384 x 64 pixels. The default serial speed is 921600 baud.

## Setup

### Firmware

Install Arduino CLI, the `esp32:esp32` board core, and the required libraries:

```bash
arduino-cli core install esp32:esp32
arduino-cli lib install "Adafruit Protomatter" PNGdec
```

LittleFS is provided by the ESP32 core. The sketch mounts it using the partition
label `ffat`; the board's partition layout must include that partition.

From the project root, compile and upload:

```bash
arduino-cli compile --fqbn esp32:esp32:adafruit_matrixportal_esp32s3 MatrixPanelSketch
arduino-cli upload --fqbn esp32:esp32:adafruit_matrixportal_esp32s3 \
  --port /dev/ttyACM0 MatrixPanelSketch
```

Replace `/dev/ttyACM0` with the panel's serial port. Use `arduino-cli board list`
to identify it. Uploading resets the board; wait for startup to finish before
sending commands.

### Python host

The host requires Python, Pillow, and pyserial:

```bash
python3 -m venv .venv
.venv/bin/python -m pip install Pillow pyserial
```

The examples below run from the project root. The user running them must have
permission to access the serial port, and no serial monitor or other application
should be using that port.

## Commands

```bash
# Check communication; expected response: OK PONG.
.venv/bin/python MatrixPanelHost/MatrixPanelHost.py -p /dev/ttyACM0 ping

# Display an image without saving it as the startup default.
.venv/bin/python MatrixPanelHost/MatrixPanelHost.py -p /dev/ttyACM0 image arcade.png

# Save an image as the startup default and display it immediately.
.venv/bin/python MatrixPanelHost/MatrixPanelHost.py -p /dev/ttyACM0 imaged arcade.png

# Display text.
.venv/bin/python MatrixPanelHost/MatrixPanelHost.py -p /dev/ttyACM0 \
  text "HIGH SCORE" --x 2 --y 20 --color FF00FF --size 2

# Fill the panel with a color, or blank it.
.venv/bin/python MatrixPanelHost/MatrixPanelHost.py -p /dev/ttyACM0 fill FF8800
.venv/bin/python MatrixPanelHost/MatrixPanelHost.py -p /dev/ttyACM0 clear
```

Global options go before the command:

- `-p` / `--port`: serial port (required).
- `-b` / `--baud`: serial speed, defaulting to 921600.
- `--no-clear`: skip the automatic clear before `image` or `text`, allowing
  content to be drawn over the existing display.

Images are converted to RGB and scaled to fit the panel while preserving aspect
ratio by default. Both image commands support `--width`, `--height`, and
`--no-scale`. With `--no-scale`, explicit width or height values resize the image
instead of defining an aspect-preserving bounding box.

The `image` command also accepts `--x` and `--y`; otherwise it centers the image.
The `imaged` command always centers the saved image and shrinks images larger
than the panel even when `--no-scale` is used.

For all options:

```bash
.venv/bin/python MatrixPanelHost/MatrixPanelHost.py --help
.venv/bin/python MatrixPanelHost/MatrixPanelHost.py -p /dev/ttyACM0 imaged --help
```

## Batocera game trigger

[Batocera/game_trigger.sh](Batocera/game_trigger.sh) handles Batocera's
`gameStart` and `gameStop` events. It expects the project at
`/userdata/system/ledmarquee`, with Python and its dependencies installed in
`/userdata/system/ledmarquee/venv`. Adjust `LED_MARQUEE_HOME` and `SERIAL_PORT`
in the script if your installation differs.

On `gameStart`, the script takes the system name from argument 2 and the ROM
path from argument 5, removes the ROM's directory and final extension, and
displays `/userdata/system/pixelcade/<system>/<rom_no_ext>.png`. For example,
`/userdata/roms/arcade/pacman.zip` with system `arcade` loads
`/userdata/system/pixelcade/arcade/pacman.png`.

Game images use the host's `image` command, so they do not replace the stored
startup default. Missing images are logged and return a nonzero exit status
without changing the display. On `gameStop`, the script clears the display.
Command output and errors are appended to `/tmp/game_trigger.log`.

## Default image and startup

The `imaged` command converts the prepared image to PNG, uploads it to LittleFS,
and saves it as `/default.png`. Successful uploads return `OK IMAGED`.

The firmware receives the upload into `/default.tmp` and decodes it before
replacing the existing default image. A receive timeout, write failure, or
invalid PNG aborts the upload without replacing the existing default.

At startup:

- If `/default.png` exists and can be decoded, the firmware displays it centered
  on a black background.
- Otherwise, it displays `Display initialized..waiting for command`.

The saved image survives resets and power cycles. Commands such as `image`,
`text`, `fill`, and `clear` change the current display but do not remove or replace
the saved startup image. There is currently no serial command to delete it.

## Default-image upload protocol

The Python host and firmware must both support the acknowledged-block protocol:

1. The host sends `IMAGED <bytes>` followed by a newline. The length is the
   encoded PNG file size in bytes.
2. The firmware opens the temporary file and replies `RDY 4096`, advertising its
   maximum block size.
3. The host sends up to 4096 raw PNG bytes, with no additional separator.
4. After writing the block, the firmware replies `ACK <total bytes>`, where the
   count is cumulative. The host checks this count and waits for the ACK before
   sending the next block.
5. The last block contains only the remaining bytes. It is acknowledged in the
   same way, then successful decoding and storage produce `OK IMAGED`.

Do not stream the entire PNG without waiting for acknowledgements. Filesystem
writes can pause serial consumption; an unpaced upload can lose data and leave
the panel waiting for missing bytes until it returns `ERR timeout`. The firmware
also allocates a receive buffer large enough for one complete block.

This handshake applies to `IMAGED`; the regular `IMG` command continues to use
its existing raw-pixel streaming protocol.

## Troubleshooting

- **`Panel does not advertise an IMAGED block size`**: the host received the old
  bare `RDY` response. Compile and upload the matching updated firmware.
- **`Panel error: ERR timeout`**: the firmware did not receive a complete block
  within its payload timeout. Check that the host and firmware are both updated,
  the USB connection is stable, and no other program is accessing the serial
  port.
- **`ERR write failed` or `ERR bad IMAGED size`**: check the LittleFS partition
  and available storage. Uploading temporarily requires space for the new image
  alongside the existing default.
- **No response**: confirm the serial port, access permissions, baud rate, and
  that the board has finished starting and is not in bootloader mode.

## Upload regression tests

Run the host protocol tests without connecting hardware:

```bash
.venv/bin/python -m unittest discover -s MatrixPanelHost -p test_imaged.py -v
```

The tests cover per-block pacing, PNG byte preservation, final-block boundaries,
old firmware detection, invalid block sizes, panel write errors, incorrect
acknowledgements, and incomplete serial writes.

# Images
## 1. Create a temporary folder and download the master package
mkdir -p /tmp/pixelart && cd /tmp/pixelart
wget https://pixelcade.org/pi/artwork.zip

## 2. Extract the artwork into your pixelcade root folder
unzip -o artwork.zip -d /userdata/system/pixelcade/

## 3. Clean up the zip file from /tmp
rm -rf /tmp/pixelart

# Install from Github
## 1. Navigate to your custom project directory
mkdir -p /userdata/system/ledmarquee && cd /userdata/system/ledmarquee

## 2. Download the repository's main branch as a zip file
curl -L https://github.com/crgales/ArcadePanelLED/archive/refs/heads/main.tar.gz | tar -xz -C /userdata/system/ledmarquee --strip-components=1

## 3. Initialize the required Python .venv
