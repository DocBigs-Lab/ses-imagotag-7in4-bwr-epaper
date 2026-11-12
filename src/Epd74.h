// SPDX-License-Identifier: MIT
// Copyright (c) 2026 DocBigs-Lab
// Befehlsfolge abgeleitet von PervasiveDisplays/EPD_Driver_GU_mid,
// Copyright (c) 2012-2021 Pervasive Displays Inc. (BSD-Lizenz, Hinweis in LICENSE).
// Treiber fuer SES-imagotag 7,4" BWR (Pervasive E2741ES083, Treiberchip IST7x32)
// am Waveshare e-Paper ESP32 Driver Board. Ablauf nach PervasiveDisplays/EPD_Driver_GU_mid.
// Details: doc/panel-7.4-zoll.md
#pragma once

#include <Adafruit_GFX.h>
#include <Arduino.h>
#include <SPI.h>

enum : uint16_t {
    EPD_WHITE = 0,
    EPD_BLACK = 1,
    EPD_RED   = 2,
};

struct Epd74Pins {
    int8_t sck  = 13;
    int8_t din  = 14;
    int8_t cs   = 15;
    int8_t dc   = 27;
    int8_t rst  = 26;
    int8_t busy = 25;
};

class Epd74 : public Adafruit_GFX {
public:
    static constexpr int16_t  WIDTH_PX  = 800;   // Querformat, Flexkabel unten
    static constexpr int16_t  HEIGHT_PX = 480;
    static constexpr uint32_t PLANE_BYTES = (uint32_t)WIDTH_PX * HEIGHT_PX / 8;

    explicit Epd74(const Epd74Pins &pins = Epd74Pins());
    ~Epd74();

    // Speicher anlegen, Pins und SPI einrichten. false, wenn kein Speicher frei ist.
    bool begin();

    // Chip-ID per Befehl 0x70 (24 Bit). Erwartet: 0x71325A.
    uint32_t readChipId();

    // Temperatur fuer die Ansteuerung, -40 bis 87 Grad C (Standard 25).
    void setTemperature(float celsius);

    // DCTL-Wert: 0x08 (IST7236, Standard) oder 0x00 (IST7232).
    void setDctl(uint8_t dctl) { _dctl = dctl; }

    // Puffer an das Panel senden und Refresh ausfuehren (dauert ca. 28 s).
    // false bei BUSY-Timeout.
    bool display();

    // Dauer des letzten Refresh in ms.
    uint32_t lastRefreshMs() const { return _lastRefreshMs; }

    // Adafruit_GFX
    void drawPixel(int16_t x, int16_t y, uint16_t color) override;
    void fillScreen(uint16_t color) override;

private:
    void sendCommand(uint8_t index, const uint8_t *data, uint32_t len);
    void sendCommand(uint8_t index, std::initializer_list<uint8_t> data);
    bool waitWhileBusy(uint32_t timeoutMs);
    void hardwareReset();
    void dcdcSoftStart();
    void dcdcSoftShutdown();

    Epd74Pins _pins;
    SPIClass *_spi = &SPI;
    SPISettings _spiSettings{4000000, MSBFIRST, SPI_MODE0};
    uint8_t *_black = nullptr;   // 1 = schwarz, natives Format: 800 Zeilen x 60 Byte
    uint8_t *_red = nullptr;     // 1 = rot
    uint8_t _temperature = 0x82; // 25 Grad C
    uint8_t _dctl = 0x08;
    uint32_t _lastRefreshMs = 0;
};
