// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jens Blasche
// Befehlsfolge abgeleitet von PervasiveDisplays/EPD_Driver_GU_mid,
// Copyright (c) 2012-2021 Pervasive Displays Inc. (BSD-Lizenz, Hinweis in LICENSE).
#include "Epd74.h"

// Natives Format: 800 Zeilen zu je 480 Pixeln (60 Byte), MSB = erstes Pixel.
// Querformat (Flexkabel unten): Zeile = 799 - x, Pixel in der Zeile = y.
static constexpr uint32_t LINE_BYTES = 60;

Epd74::Epd74(const Epd74Pins &pins) : Adafruit_GFX(WIDTH_PX, HEIGHT_PX), _pins(pins) {}

Epd74::~Epd74() {
    free(_black);
    free(_red);
}

bool Epd74::begin() {
    if (!_black) _black = (uint8_t *)malloc(PLANE_BYTES);
    if (!_red) _red = (uint8_t *)malloc(PLANE_BYTES);
    if (!_black || !_red) return false;
    fillScreen(EPD_WHITE);

    pinMode(_pins.busy, INPUT);
    pinMode(_pins.rst, OUTPUT);
    digitalWrite(_pins.rst, HIGH);
    pinMode(_pins.dc, OUTPUT);
    digitalWrite(_pins.dc, HIGH);
    pinMode(_pins.cs, OUTPUT);
    digitalWrite(_pins.cs, HIGH);
    _spi->begin(_pins.sck, -1, _pins.din, -1);
    return true;
}

void Epd74::setTemperature(float celsius) {
    // Skala laut Pervasive: -40 C = 0x00, 0 C = 0x50, 25 C = 0x82, 87 C = 0xFE (0,5-C-Schritte)
    float v = 0x50 + celsius * 2.0f;
    if (v < 0) v = 0;
    if (v > 0xFE) v = 0xFE;
    _temperature = (uint8_t)(v + 0.5f);
}

// ---------- Zeichnen --------------------------------------------------------

void Epd74::drawPixel(int16_t x, int16_t y, uint16_t color) {
    if (x < 0 || y < 0 || x >= width() || y >= height()) return;
    int16_t t;
    switch (getRotation()) {
        case 1: t = x; x = WIDTH_PX - 1 - y; y = t; break;
        case 2: x = WIDTH_PX - 1 - x; y = HEIGHT_PX - 1 - y; break;
        case 3: t = x; x = y; y = HEIGHT_PX - 1 - t; break;
        default: break;
    }
    uint32_t i = (uint32_t)(WIDTH_PX - 1 - x) * LINE_BYTES + (y >> 3);
    uint8_t m = 0x80 >> (y & 7);
    switch (color) {
        case EPD_BLACK: _black[i] |= m;  _red[i] &= ~m; break;
        case EPD_RED:   _black[i] &= ~m; _red[i] |= m;  break;
        default:        _black[i] &= ~m; _red[i] &= ~m; break;
    }
}

void Epd74::fillScreen(uint16_t color) {
    memset(_black, color == EPD_BLACK ? 0xFF : 0x00, PLANE_BYTES);
    memset(_red, color == EPD_RED ? 0xFF : 0x00, PLANE_BYTES);
}

// ---------- SPI ---------------------------------------------------------------

void Epd74::sendCommand(uint8_t index, const uint8_t *data, uint32_t len) {
    _spi->beginTransaction(_spiSettings);
    digitalWrite(_pins.dc, LOW);
    digitalWrite(_pins.cs, LOW);
    _spi->transfer(index);
    digitalWrite(_pins.cs, HIGH);
    digitalWrite(_pins.dc, HIGH);
    if (len) {
        digitalWrite(_pins.cs, LOW);
        _spi->writeBytes(data, len);
        digitalWrite(_pins.cs, HIGH);
    }
    _spi->endTransaction();
}

void Epd74::sendCommand(uint8_t index, std::initializer_list<uint8_t> data) {
    sendCommand(index, data.begin(), data.size());
}

bool Epd74::waitWhileBusy(uint32_t timeoutMs) {
    uint32_t t0 = millis();
    while (digitalRead(_pins.busy) == LOW) {   // LOW = beschaeftigt
        if (millis() - t0 > timeoutMs) return false;
        delay(10);
    }
    return true;
}

void Epd74::hardwareReset() {
    delay(200);
    digitalWrite(_pins.rst, HIGH); delay(20);
    digitalWrite(_pins.rst, LOW);  delay(200);
    digitalWrite(_pins.rst, HIGH); delay(50);
    digitalWrite(_pins.cs, HIGH);  delay(5);
}

// Lesen ueber die bidirektionale Datenleitung geht nur per Bit-Bang.
uint32_t Epd74::readChipId() {
    _spi->end();
    pinMode(_pins.sck, OUTPUT);
    pinMode(_pins.din, OUTPUT);
    digitalWrite(_pins.sck, LOW);
    hardwareReset();

    auto clockOut = [&](uint8_t v) {
        for (int b = 7; b >= 0; b--) {
            digitalWrite(_pins.sck, LOW);
            digitalWrite(_pins.din, (v >> b) & 1);
            delayMicroseconds(1);
            digitalWrite(_pins.sck, HIGH);
            delayMicroseconds(1);
        }
        digitalWrite(_pins.sck, LOW);
    };

    digitalWrite(_pins.cs, LOW);
    digitalWrite(_pins.dc, LOW);
    clockOut(0x70);
    digitalWrite(_pins.dc, HIGH);
    pinMode(_pins.din, INPUT_PULLUP);
    delayMicroseconds(2);
    uint32_t id = 0;
    for (int i = 0; i < 24; i++) {
        digitalWrite(_pins.sck, LOW);  delayMicroseconds(1);
        digitalWrite(_pins.sck, HIGH); delayMicroseconds(1);
        id = (id << 1) | (uint32_t)digitalRead(_pins.din);
    }
    digitalWrite(_pins.sck, LOW);
    digitalWrite(_pins.cs, HIGH);

    _spi->begin(_pins.sck, -1, _pins.din, -1);
    return id;
}

// ---------- Bildaufbau ----------------------------------------------------------

void Epd74::dcdcSoftStart() {
    sendCommand(0x05, {0x7d}); delay(200);
    sendCommand(0x05, {0x00}); delay(10);
    sendCommand(0xc2, {0x3f}); delay(1);
    sendCommand(0xd8, {0x00});                 // MS_SYNC
    sendCommand(0xd6, {0x00});                 // BVSS
    sendCommand(0xa7, {0x10}); delay(100);
    sendCommand(0xa7, {0x00}); delay(100);
    sendCommand(0x03, {0x00, 0x01});           // OSC
    sendCommand(0x44, {0x00});
    sendCommand(0x45, {0x80});
    sendCommand(0xa7, {0x10}); delay(100);
    sendCommand(0xa7, {0x00}); delay(100);
    sendCommand(0x44, {0x06});
    sendCommand(0x45, {_temperature});
    sendCommand(0xa7, {0x10}); delay(100);
    sendCommand(0xa7, {0x00}); delay(100);
    sendCommand(0x60, {0x25});                 // TCON
    sendCommand(0x61, {0x00});                 // STV_DIR
    sendCommand(0x01, {0x00});                 // DCTL
    sendCommand(0x02, {0x00});                 // VCOM

    // Hochspannung in Stufen hochfahren (Werte aus dem Herstellercode)
    uint8_t i51[] = {0x50, 0x01, 0x0a, 0x01};
    const uint8_t i09[] = {0x1f, 0x9f, 0x7f, 0xff};
    sendCommand(0x51, &i51[0], 2);
    for (int v = 1; v <= 4; v++) {
        sendCommand(0x09, &i09[0], 1); i51[1] = v; sendCommand(0x51, &i51[0], 2);
        sendCommand(0x09, &i09[1], 1); delay(2);
    }
    for (int v = 1; v <= 10; v++) {
        sendCommand(0x09, &i09[0], 1); i51[3] = v; sendCommand(0x51, &i51[2], 2);
        sendCommand(0x09, &i09[1], 1); delay(2);
    }
    for (int v = 3; v <= 10; v++) {
        sendCommand(0x09, &i09[2], 1); i51[3] = v; sendCommand(0x51, &i51[2], 2);
        sendCommand(0x09, &i09[3], 1); delay(2);
    }
    for (int v = 9; v >= 2; v--) {
        sendCommand(0x09, &i09[2], 1); i51[2] = v; sendCommand(0x51, &i51[2], 2);
        sendCommand(0x09, &i09[3], 1); delay(2);
    }
    sendCommand(0x09, &i09[3], 1); delay(10);
}

void Epd74::dcdcSoftShutdown() {
    waitWhileBusy(5000);
    sendCommand(0x09, {0x7f});
    sendCommand(0x05, {0x7d});
    sendCommand(0x09, {0x00});
    delay(200);
    waitWhileBusy(5000);
    digitalWrite(_pins.dc, LOW);
    digitalWrite(_pins.cs, LOW);
    digitalWrite(_pins.rst, LOW);
    delay(10);
    digitalWrite(_pins.cs, HIGH);
    digitalWrite(_pins.dc, HIGH);
    digitalWrite(_pins.rst, HIGH);
}

bool Epd74::display() {
    hardwareReset();
    sendCommand(0x01, {_dctl});                                // DCTL
    sendCommand(0x13, {0x00, 0x3b, 0x00, 0x00, 0x1f, 0x03});   // DUW
    sendCommand(0x90, {0x00, 0x3b, 0x00, 0xc9});               // DRFW
    sendCommand(0x12, {0x3b, 0x00, 0x14});                     // RAM_RW
    sendCommand(0x10, _black, PLANE_BYTES);                    // Ebene schwarz
    sendCommand(0x12, {0x3b, 0x00, 0x14});                     // RAM_RW
    sendCommand(0x11, _red, PLANE_BYTES);                      // Ebene rot

    dcdcSoftStart();

    bool ok = waitWhileBusy(5000);
    sendCommand(0x15, {0x3c});                                 // Refresh
    delay(5);
    uint32_t t0 = millis();
    ok = waitWhileBusy(60000) && ok;
    _lastRefreshMs = millis() - t0;

    dcdcSoftShutdown();
    return ok;
}
