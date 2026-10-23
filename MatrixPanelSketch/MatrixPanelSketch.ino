#include <Adafruit_Protomatter.h>
#include <LittleFS.h>
#include <PNGdec.h>

// Matrix Configuration
#define MATRIX_WIDTH  384  // 3 panels x 128 pixels
#define MATRIX_HEIGHT 64
#define BIT_DEPTH     4    // 1 to 6

#define SERIAL_BAUD     921600
#define CMD_MAX_LEN     512
#define PIXEL_CHUNK     512   // pixels pulled from Serial per block
#define PAYLOAD_TIMEOUT 5000  // ms to wait for image data
#define FILE_CHUNK      4096  // bytes pulled from Serial per block when saving files

#define DEFAULT_IMAGE_PATH "/default.png"
#define DEFAULT_IMAGE_TMP  "/default.tmp"

// MatrixPortal S3 Pin Map
uint8_t rgbPins[]  = {40, 41, 42, 37, 39, 38}; // R1, G1, B1, R2, G2, B2
uint8_t addrPins[] = {45, 36, 48, 35, 21};     // A, B, C, D, E
uint8_t clockPin   = 2;
uint8_t latchPin   = 47;
uint8_t oePin      = 14;

Adafruit_Protomatter matrix(
  MATRIX_WIDTH, BIT_DEPTH, 1, rgbPins,
  sizeof(addrPins), addrPins, clockPin, latchPin, oePin, true
);

static char cmdBuf[CMD_MAX_LEN];
static uint16_t cmdLen = 0;
static bool autoShow = true;

static uint16_t rgb888to565(uint32_t c) {
  return matrix.color565((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

// Accepts decimal or 0x-prefixed values; returns fallback for missing tokens.
static long parseNum(const char *tok, long fallback) {
  if (!tok || !*tok) return fallback;
  return (long)strtol(tok, nullptr, 0);
}

static void maybeShow() {
  if (autoShow) matrix.show();
}

static bool readExact(uint8_t *dst, size_t len) {
  size_t got = 0;
  uint32_t last = millis();
  while (got < len) {
    int n = Serial.readBytes(dst + got, len - got);
    if (n > 0) {
      got += n;
      last = millis();
    } else if (millis() - last > PAYLOAD_TIMEOUT) {
      return false;
    }
  }
  return true;
}

// IMG <x> <y> <w> <h> [bpp] -- raw pixels follow, row-major from top-left.
// bpp 16 = RGB565 big-endian (default), bpp 24 = RGB888.
static void cmdImage(char *args) {
  int16_t x0  = (int16_t)parseNum(strtok(args, " "), 0);
  int16_t y0  = (int16_t)parseNum(strtok(nullptr, " "), 0);
  int32_t w   = (int32_t)parseNum(strtok(nullptr, " "), 0);
  int32_t h   = (int32_t)parseNum(strtok(nullptr, " "), 0);
  int32_t bpp = (int32_t)parseNum(strtok(nullptr, " "), 16);

  if (w <= 0 || h <= 0 || w > MATRIX_WIDTH * 4 || h > MATRIX_HEIGHT * 4 ||
      (bpp != 16 && bpp != 24)) {
    Serial.println("ERR bad IMG header");
    return;
  }

  const uint8_t bytesPerPixel = bpp / 8;
  Serial.println("RDY");

  static uint8_t buf[PIXEL_CHUNK * 3];
  int32_t remaining = w * h;
  int32_t index = 0;

  while (remaining > 0) {
    int32_t count = remaining > PIXEL_CHUNK ? PIXEL_CHUNK : remaining;
    if (!readExact(buf, (size_t)count * bytesPerPixel)) {
      Serial.println("ERR timeout");
      return;
    }
    for (int32_t i = 0; i < count; i++) {
      const uint8_t *p = buf + i * bytesPerPixel;
      uint16_t color = (bpp == 16)
          ? (uint16_t)((p[0] << 8) | p[1])
          : rgb888to565(((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]);
      matrix.drawPixel(x0 + (index % w), y0 + (index / w), color);
      index++;
    }
    remaining -= count;
  }

  maybeShow();
  Serial.println("OK IMG");
}

// ---- PNG decoding from LittleFS ----

struct PngDrawCtx {
  int16_t x0, y0;
  uint16_t *line;
};

static PNG *activePng = nullptr;

static void *pngOpen(const char *path, int32_t *size) {
  File *f = new File(LittleFS.open(path, "r"));
  if (!f || !*f) {
    delete f;
    return nullptr;
  }
  *size = f->size();
  return f;
}

static void pngClose(void *handle) {
  File *f = static_cast<File *>(handle);
  if (f) {
    f->close();
    delete f;
  }
}

static int32_t pngRead(PNGFILE *pf, uint8_t *buf, int32_t len) {
  File *f = static_cast<File *>(pf->fHandle);
  int32_t n = f->read(buf, len);
  pf->iPos = f->position();
  return n;
}

static int32_t pngSeek(PNGFILE *pf, int32_t pos) {
  File *f = static_cast<File *>(pf->fHandle);
  f->seek(pos);
  pf->iPos = f->position();
  return pf->iPos;
}

static int pngDraw(PNGDRAW *d) {
  PngDrawCtx *ctx = static_cast<PngDrawCtx *>(d->pUser);
  activePng->getLineAsRGB565(d, ctx->line, PNG_RGB565_LITTLE_ENDIAN, 0x000000);
  int16_t y = ctx->y0 + d->y;
  if (y < 0 || y >= MATRIX_HEIGHT) return 1;
  for (int i = 0; i < d->iWidth; i++) {
    int16_t x = ctx->x0 + i;
    if (x >= 0 && x < MATRIX_WIDTH) matrix.drawPixel(x, y, ctx->line[i]);
  }
  return 1;
}

// Clears the panel and draws the PNG centered. Returns false if missing/invalid.
static bool showPngFile(const char *path) {
  if (!LittleFS.exists(path)) return false;

  PNG *png = new PNG();  // ~48KB decoder state; keep it off the stack
  if (!png) return false;

  bool ok = false;
  if (png->open(path, pngOpen, pngClose, pngRead, pngSeek, pngDraw) == PNG_SUCCESS) {
    int w = png->getWidth();
    int h = png->getHeight();
    PngDrawCtx ctx;
    ctx.x0 = (MATRIX_WIDTH - w) / 2;
    ctx.y0 = (MATRIX_HEIGHT - h) / 2;
    ctx.line = (uint16_t *)malloc(w * sizeof(uint16_t));
    if (ctx.line) {
      activePng = png;
      matrix.fillScreen(0);
      ok = png->decode(&ctx, 0) == PNG_SUCCESS;
      activePng = nullptr;
      free(ctx.line);
    }
    png->close();
  }
  delete png;

  if (ok) matrix.show();
  return ok;
}

static void showWaitingMessage() {
  matrix.fillScreen(0);
  matrix.setTextWrap(false);
  matrix.setTextSize(1);
  matrix.setTextColor(0xffff);
  matrix.setCursor(5, 5);
  matrix.print("Display initialized..waiting for command");
  matrix.show();
}

// IMAGED <bytes>: RDY <block size>, then ACK <total bytes> after each saved block.
// The host must wait for each ACK before sending more PNG data.
static void cmdImageDefault(char *args) {
  int32_t size = (int32_t)parseNum(strtok(args, " "), 0);
  size_t freeBytes = LittleFS.totalBytes() - LittleFS.usedBytes();
  if (LittleFS.exists(DEFAULT_IMAGE_PATH)) {
    File old = LittleFS.open(DEFAULT_IMAGE_PATH, "r");
    freeBytes += old.size();
    old.close();
  }
  if (size <= 0 || (size_t)size > freeBytes) {
    Serial.println("ERR bad IMAGED size");
    return;
  }

  File f = LittleFS.open(DEFAULT_IMAGE_TMP, "w");
  if (!f) {
    Serial.println("ERR cannot open file");
    return;
  }

  Serial.printf("RDY %d\n", FILE_CHUNK);

  static uint8_t buf[FILE_CHUNK];
  int32_t remaining = size;
  while (remaining > 0) {
    int32_t count = remaining > FILE_CHUNK ? FILE_CHUNK : remaining;
    if (!readExact(buf, count)) {
      f.close();
      LittleFS.remove(DEFAULT_IMAGE_TMP);
      Serial.println("ERR timeout");
      return;
    }
    if (f.write(buf, count) != (size_t)count) {
      f.close();
      LittleFS.remove(DEFAULT_IMAGE_TMP);
      Serial.println("ERR write failed");
      return;
    }
    remaining -= count;
    Serial.printf("ACK %ld\n", (long)(size - remaining));
  }
  f.close();

  // Validate before replacing the existing default image.
  if (!showPngFile(DEFAULT_IMAGE_TMP)) {
    LittleFS.remove(DEFAULT_IMAGE_TMP);
    Serial.println("ERR invalid PNG");
    return;
  }

  LittleFS.remove(DEFAULT_IMAGE_PATH);
  if (!LittleFS.rename(DEFAULT_IMAGE_TMP, DEFAULT_IMAGE_PATH)) {
    Serial.println("ERR rename failed");
    return;
  }
  Serial.println("OK IMAGED");
}

// TEXT <x> <y> <color> <size> <wrap> <message...>
static void cmdText(char *args) {
  int16_t x      = (int16_t)parseNum(strtok(args, " "), 0);
  int16_t y      = (int16_t)parseNum(strtok(nullptr, " "), 0);
  uint32_t color = (uint32_t)parseNum(strtok(nullptr, " "), 0xFFFFFF);
  uint8_t size   = (uint8_t)parseNum(strtok(nullptr, " "), 1);
  bool wrap      = parseNum(strtok(nullptr, " "), 0) != 0;
  char *msg = strtok(nullptr, "");

  matrix.setTextWrap(wrap);
  matrix.setTextSize(size ? size : 1);
  matrix.setTextColor(rgb888to565(color));
  matrix.setCursor(x, y);
  if (msg) matrix.print(msg);
  maybeShow();
  Serial.println("OK TEXT");
}

// FILL [color]
static void cmdFill(char *args) {
  uint32_t color = (uint32_t)parseNum(strtok(args, " "), 0x000000);
  matrix.fillScreen(rgb888to565(color));
  maybeShow();
  Serial.println("OK FILL");
}

static void handleCommand(char *line) {
  while (*line == ' ') line++;
  if (!*line) return;

  char *args = strchr(line, ' ');
  if (args) *args++ = '\0';

  if (!strcasecmp(line, "PING")) {
    Serial.println("OK PONG");
  } else if (!strcasecmp(line, "INFO")) {
    Serial.printf("OK %d %d %d\n", MATRIX_WIDTH, MATRIX_HEIGHT, BIT_DEPTH);
  } else if (!strcasecmp(line, "IMG")) {
    cmdImage(args);
  } else if (!strcasecmp(line, "IMAGED")) {
    cmdImageDefault(args);
  } else if (!strcasecmp(line, "TEXT")) {
    cmdText(args);
  } else if (!strcasecmp(line, "FILL")) {
    cmdFill(args);
  } else if (!strcasecmp(line, "CLEAR")) {
    matrix.fillScreen(0);
    maybeShow();
    Serial.println("OK CLEAR");
  } else if (!strcasecmp(line, "SHOW")) {
    matrix.show();
    Serial.println("OK SHOW");
  } else if (!strcasecmp(line, "AUTOSHOW")) {
    autoShow = parseNum(args, 1) != 0;
    Serial.println("OK AUTOSHOW");
  } else {
    Serial.println("ERR unknown command");
  }
}

void setup() {
  // Buffer a complete IMAGED block while LittleFS is busy writing flash.
  if (Serial.setRxBufferSize(FILE_CHUNK) < FILE_CHUNK) {
    Serial.begin(SERIAL_BAUD);
    Serial.println("ERR cannot allocate serial receive buffer");
    while (1) delay(100);
  }
  Serial.begin(SERIAL_BAUD);
  Serial.println("MatrixPortal_S3 binary stream boot");

  // Delay for USB Serial initialization
  delay(2000);

  // * Setup LittleFS
  if (!LittleFS.begin(true, "/littlefs", 10, "ffat")) {
    Serial.println("LittleFS mount failed");
    while (1) delay(100);
  }

  Serial.println("LittleFS mounted...");

  ProtomatterStatus status = matrix.begin();
  if (status != PROTOMATTER_OK) {
    Serial.println("Matrix init FAILED");
    while (1) delay(100);
  }

  if (showPngFile(DEFAULT_IMAGE_PATH)) {
    Serial.println("Default image displayed");
  } else {
    showWaitingMessage();
  }

  Serial.println("LED Display initialized...");
  Serial.println("READY");
}

void loop() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      cmdBuf[cmdLen] = '\0';
      handleCommand(cmdBuf);
      cmdLen = 0;
    } else if (cmdLen < CMD_MAX_LEN - 1) {
      cmdBuf[cmdLen++] = c;
    } else {
      cmdLen = 0;
      Serial.println("ERR command too long");
    }
  }
}
