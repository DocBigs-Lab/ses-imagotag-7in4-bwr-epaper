/* =========================================================================
 * Diagnosefirmware aus der Fehlersuche am SES-imagotag 7,4" BWR
 * (Pervasive E2741ES083, IST7x32) auf dem Waveshare e-Paper ESP32 Driver Board.
 *
 * Eigenstaendig, nutzt die Bibliothek Epd74 nicht. Bedienung ueber den seriellen
 * Monitor (115200 Baud). Funktionierende Befehle fuer dieses Panel:
 *   v / V  Testbild mit der Pervasive-mid-size-Sequenz (DCTL 08 / 00)
 *   W / X  ganze Flaeche weiss (DCTL 08 / 00)
 *   i      Chip-ID lesen (0x70 -> 71 32 5A)
 *   x, z   Lesescans (z sendet auch unbekannte Befehle, danach ggf. Reset)
 *   g      Gegentest fuer ein 4,2"-Panel mit UC8176
 * Die uebrigen Befehle sind Fehlversuche (UC81xx, SSD16xx, 3-Draht-SPI usw.).
 * Testbild: Balken schwarz, weiss, rot, schwarz und rotes Quadrat oben links.
 * Hintergrund: doc/panel-7.4-zoll.md
 * ========================================================================= */
#include <Arduino.h>

#define PIN_SCK   13
#define PIN_DIN   14
#define PIN_CS    15
#define PIN_DC    27
#define PIN_RST   26
#define PIN_BUSY  25

static int g_w = 800, g_h = 480;
static bool g_fixedWait = false;   // 'f': BUSY ignorieren, feste Wartezeiten

// ---------- Bit-Bang SPI ----------------------------------------------------
static void bbByte(uint8_t v) {
    for (int i = 7; i >= 0; i--) {
        digitalWrite(PIN_SCK, LOW);
        digitalWrite(PIN_DIN, (v >> i) & 1);
        delayMicroseconds(1);
        digitalWrite(PIN_SCK, HIGH);
        delayMicroseconds(1);
    }
    digitalWrite(PIN_SCK, LOW);
}
static uint8_t bbRead(void) {
    uint8_t v = 0;
    pinMode(PIN_DIN, INPUT_PULLUP);
    delayMicroseconds(2);
    for (int i = 7; i >= 0; i--) {
        digitalWrite(PIN_SCK, LOW);  delayMicroseconds(1);
        digitalWrite(PIN_SCK, HIGH); delayMicroseconds(1);
        v = (uint8_t)((v << 1) | digitalRead(PIN_DIN));
    }
    digitalWrite(PIN_SCK, LOW);
    pinMode(PIN_DIN, OUTPUT);
    return v;
}

// Pervasive-Stil: Index mit DC=Low, Daten mit DC=High, CS dazwischen hoch
static void sendIndex(uint8_t idx) {
    digitalWrite(PIN_DC, LOW);
    digitalWrite(PIN_CS, LOW);
    bbByte(idx);
    digitalWrite(PIN_CS, HIGH);
    digitalWrite(PIN_DC, HIGH);
}
static void sendIndexData(uint8_t idx, const uint8_t *d, int n) {
    sendIndex(idx);
    digitalWrite(PIN_CS, LOW);
    for (int i = 0; i < n; i++) bbByte(d[i]);
    digitalWrite(PIN_CS, HIGH);
}
static void sendIndex1(uint8_t idx, uint8_t v) { sendIndexData(idx, &v, 1); }

// BUSY low-aktiv: warten bis HIGH. Gibt Dauer aus, false bei Timeout.
static bool waitBusy(const char *what, uint32_t timeoutMs) {
    uint32_t t0 = millis();
    delay(1);
    while (digitalRead(PIN_BUSY) == LOW) {
        if (millis() - t0 > timeoutMs) {
            Serial.printf("  %s: TIMEOUT nach %lu ms (BUSY bleibt LOW)\n", what, (unsigned long)timeoutMs);
            return false;
        }
        delay(5);
    }
    Serial.printf("  %s: %lu ms\n", what, (unsigned long)(millis() - t0));
    return true;
}

static void hwReset(void) {
    digitalWrite(PIN_RST, HIGH); delay(5);
    digitalWrite(PIN_RST, LOW);  delay(10);
    digitalWrite(PIN_RST, HIGH); delay(10);
}

static void readId(void) {
    hwReset();
    digitalWrite(PIN_CS, LOW);
    digitalWrite(PIN_DC, LOW); bbByte(0x70);
    digitalWrite(PIN_DC, HIGH);
    uint8_t a = bbRead(), b = bbRead(), c = bbRead();
    digitalWrite(PIN_CS, HIGH);
    Serial.printf("ID 0x70: %02X %02X %02X (Scanner: 71 32 5A)\n", a, b, c);
}

// ---------- Testbild ----------------------------------------------------------
// pattern: 0 = Balken, 1 = alles 0 (beide Ebenen leer)
static bool plane1Bit(int x, int y, int pattern) {
    if (pattern == 1) return false;
    if (x < 64 && y < 64) return false;
    int bar = x * 4 / g_w;
    return bar == 0 || bar == 3;
}
static bool plane2Bit(int x, int y, int pattern) {
    if (pattern == 1) return false;
    if (x < 64 && y < 64) return true;
    int bar = x * 4 / g_w;
    return bar == 2 || bar == 3;
}

static bool g_native = false;   // Pervasive mid: 800 Zeilen x 480 Pixel (60 Byte)

static void sendPlane(uint8_t idx, int plane, int pattern) {
    sendIndex(idx);
    digitalWrite(PIN_CS, LOW);
    if (g_native) {
        // native Zeile L = x im Querformat (0..799), Pixel p in der Zeile = y (0..479)
        for (int L = 0; L < 800; L++) {
            for (int pb = 0; pb < 60; pb++) {
                uint8_t v = 0;
                for (int k = 0; k < 8; k++) {
                    int y = pb * 8 + k;
                    int x = 799 - L;   // Panel-Zeile 0 liegt rechts (Flexkabel unten)
                    bool bit = plane == 1 ? plane1Bit(x, y, pattern) : plane2Bit(x, y, pattern);
                    if (bit) v |= (uint8_t)(0x80 >> k);
                }
                bbByte(v);
            }
        }
        digitalWrite(PIN_CS, HIGH);
        return;
    }
    for (int y = 0; y < g_h; y++) {
        for (int xb = 0; xb < g_w / 8; xb++) {
            uint8_t v = 0;
            for (int k = 0; k < 8; k++) {
                int x = xb * 8 + k;
                bool bit = plane == 1 ? plane1Bit(x, y, pattern) : plane2Bit(x, y, pattern);
                if (bit) v |= (uint8_t)(0x80 >> k);
            }
            bbByte(v);
        }
    }
    digitalWrite(PIN_CS, HIGH);
}

// psrMode: 0 = kein PSR senden, 1 = CF 8D, 2 = CF 89
static void runTest(int psrMode, int pattern) {
    Serial.printf("\n--- Test: %dx%d, PSR-Modus %d, Muster %d ---\n", g_w, g_h, psrMode, pattern);
    hwReset();
    if (!waitBusy("nach HW-Reset", 2000)) return;
    sendIndex1(0x00, 0x0E);                       // Soft-Reset
    if (!waitBusy("Soft-Reset", 2000)) return;
    sendIndex1(0xE5, 0x19);                       // Temperatur 25 C
    sendIndex1(0xE0, 0x02);                       // Temperatur aktivieren
    if (psrMode == 1) { const uint8_t p[2] = {0xCF, 0x8D}; sendIndexData(0x00, p, 2); }
    if (psrMode == 2) { const uint8_t p[2] = {0xCF, 0x89}; sendIndexData(0x00, p, 2); }

    uint32_t t0 = millis();
    sendPlane(0x10, 1, pattern);
    sendPlane(0x13, 2, pattern);
    Serial.printf("  Daten gesendet: %lu ms\n", (unsigned long)(millis() - t0));

    sendIndex1(0x04, 0x00);                       // DC/DC Power ON
    if (!waitBusy("Power ON", 5000)) { sendIndex1(0x02, 0x00); return; }
    if (g_fixedWait) { Serial.println("  feste Wartezeit Power ON: 300 ms"); delay(300); }
    sendIndex(0x12);                              // Refresh
    bool ok = waitBusy("Refresh", 60000);
    if (g_fixedWait) {
        Serial.println("  feste Wartezeit Refresh: 30 s - Display beobachten!");
        for (int i = 0; i < 30; i++) {            // BUSY trotzdem mitloggen
            Serial.print(digitalRead(PIN_BUSY)); delay(1000);
        }
        Serial.println();
    }
    sendIndex1(0x02, 0x00);                       // DC/DC Power OFF
    waitBusy("Power OFF", 5000);
    Serial.println(ok ? "Fertig. Was zeigen die 4 Balken (links->rechts) und das Quadrat oben links?"
                      : "Refresh nicht sauber beendet - Power OFF trotzdem gesendet.");
}

// BUSY-Diagnose: Pull-up vs. Pull-down (floatet der Pin?) und Verlauf nach Soft-Reset
static void busyDiag(void) {
    Serial.println("\n--- BUSY-Diagnose ---");
    hwReset();
    pinMode(PIN_BUSY, INPUT_PULLUP);   delay(5); int up = digitalRead(PIN_BUSY);
    pinMode(PIN_BUSY, INPUT_PULLDOWN); delay(5); int dn = digitalRead(PIN_BUSY);
    Serial.printf("  Ruhe: Pull-up=%d Pull-down=%d -> %s\n", up, dn,
                  up == dn ? "getrieben" : "OFFEN (Pin nicht verbunden?)");
    // Verlauf waehrend HW-Reset + Soft-Reset mit Pull-down abtasten
    digitalWrite(PIN_RST, LOW);  delay(10);
    digitalWrite(PIN_RST, HIGH);
    String s;
    for (int i = 0; i < 50; i++) { s += digitalRead(PIN_BUSY) ? '1' : '0'; delayMicroseconds(200); }
    sendIndex1(0x00, 0x0E);
    s += '|';
    for (int i = 0; i < 200; i++) { s += digitalRead(PIN_BUSY) ? '1' : '0'; delayMicroseconds(500); }
    Serial.printf("  Verlauf (Pull-down, Reset 10ms | Soft-Reset 100ms):\n  %s\n", s.c_str());
    pinMode(PIN_BUSY, INPUT);
}

// Liest n Bytes nach cmd mit gegebenem Pull; BUSY-Minimum waehrend 50 ms danach
static void rd(uint8_t cmd, uint8_t pull, uint8_t *buf, int n, int *busyLow) {
    hwReset();
    digitalWrite(PIN_CS, LOW);
    digitalWrite(PIN_DC, LOW); bbByte(cmd);
    digitalWrite(PIN_DC, HIGH);
    for (int i = 0; i < n; i++) {
        pinMode(PIN_DIN, pull); delayMicroseconds(2);
        uint8_t v = 0;
        for (int b = 7; b >= 0; b--) {
            digitalWrite(PIN_SCK, LOW);  delayMicroseconds(1);
            digitalWrite(PIN_SCK, HIGH); delayMicroseconds(1);
            v = (uint8_t)((v << 1) | digitalRead(PIN_DIN));
        }
        digitalWrite(PIN_SCK, LOW);
        buf[i] = v;
    }
    pinMode(PIN_DIN, OUTPUT);
    digitalWrite(PIN_CS, HIGH);
    int low = 0;
    for (int i = 0; i < 500; i++) { if (!digitalRead(PIN_BUSY)) low++; delayMicroseconds(100); }
    if (busyLow) *busyLow = low;
}

// Nur Lese-/Abfragebefehle. Ausgelassen: 0x04/0x05 (Power ON), 0x12 (Refresh),
// 0x80 (VCOM-Messung), 0x07 (Deep Sleep), OTP-Schreibbefehle.
static void scanReads(void) {
    const uint8_t cmds[] = {0x40, 0x43, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
                            0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F, 0x81, 0x82,
                            0x92, 0xB9, 0x1B, 0x2D, 0x2E, 0x2F, 0xF0, 0xF1};
    Serial.println("\n--- Lese-Scan (nur getriebene Antworten, 8 Byte) ---");
    uint8_t up[8], dn[8];
    for (uint8_t c : cmds) {
        int bl1, bl2;
        rd(c, INPUT_PULLUP, up, 8, &bl1);
        rd(c, INPUT_PULLDOWN, dn, 8, &bl2);
        int drv = 0;
        for (int i = 0; i < 8; i++) if (up[i] == dn[i]) drv++;
        if (drv == 0 && bl1 == 0) continue;
        Serial.printf("  %02X: ", c);
        for (int i = 0; i < 8; i++) {
            if (up[i] == dn[i]) Serial.printf("%02X ", up[i]); else Serial.print("-- ");
        }
        Serial.printf(" BUSY-low %d/500\n", bl1);
    }
    Serial.println("--- Scan fertig ---");
}

// ---------- 3-Draht-SPI (9 Bit: D/C-Bit + 8 Datenbits, DC-Pin ungenutzt) ----
static void bb9(bool isData, uint8_t v) {
    digitalWrite(PIN_SCK, LOW);
    digitalWrite(PIN_DIN, isData ? HIGH : LOW);
    delayMicroseconds(1);
    digitalWrite(PIN_SCK, HIGH); delayMicroseconds(1);
    bbByte(v);
}
static void cmd9(uint8_t c, const uint8_t *d, int n) {
    digitalWrite(PIN_CS, LOW);
    bb9(false, c);
    for (int i = 0; i < n; i++) bb9(true, d[i]);
    digitalWrite(PIN_CS, HIGH);
    delayMicroseconds(10);
}
static void read9(uint8_t c, uint8_t pull, uint8_t *buf, int n) {
    digitalWrite(PIN_CS, LOW);
    bb9(false, c);
    for (int i = 0; i < n; i++) {
        pinMode(PIN_DIN, pull); delayMicroseconds(2);
        uint8_t v = 0;
        for (int b = 7; b >= 0; b--) {
            digitalWrite(PIN_SCK, LOW);  delayMicroseconds(1);
            digitalWrite(PIN_SCK, HIGH); delayMicroseconds(1);
            v = (uint8_t)((v << 1) | digitalRead(PIN_DIN));
        }
        digitalWrite(PIN_SCK, LOW);
        buf[i] = v;
    }
    pinMode(PIN_DIN, OUTPUT);
    digitalWrite(PIN_CS, HIGH);
}

static void wire3Test(void) {
    Serial.println("\n--- 3-Draht-SPI-Test (9 Bit) ---");
    digitalWrite(PIN_DC, HIGH);
    const uint8_t rc[] = {0x70, 0x71, 0x40, 0x2F};
    uint8_t up[6], dn[6];
    for (uint8_t c : rc) {
        hwReset(); read9(c, INPUT_PULLUP, up, 6);
        hwReset(); read9(c, INPUT_PULLDOWN, dn, 6);
        Serial.printf("  %02X: ", c);
        for (int i = 0; i < 6; i++) {
            if (up[i] == dn[i]) Serial.printf("%02X ", up[i]); else Serial.print("-- ");
        }
        Serial.println();
    }
    // Soft-Reset + Power ON im 9-Bit-Format, BUSY beobachten
    hwReset();
    String s;
    const uint8_t sr = 0x0E;
    cmd9(0x00, &sr, 1);
    for (int i = 0; i < 50; i++) { s += digitalRead(PIN_BUSY) ? '1' : '0'; delay(1); }
    s += '|';
    cmd9(0x04, nullptr, 0);
    uint32_t t0 = millis(), firstLow = 0, lastLow = 0;
    while (millis() - t0 < 1000) {
        bool b = digitalRead(PIN_BUSY);
        if (!b) { if (!firstLow) firstLow = millis() - t0 + 1; lastLow = millis() - t0; }
        if ((millis() - t0) % 10 == 0) { s += b ? '1' : '0'; delay(1); }
    }
    cmd9(0x02, nullptr, 0);
    delay(100);
    Serial.printf("  BUSY (Soft-Reset 50ms | PON alle 10ms, 1s):\n  %s\n", s.c_str());
    Serial.printf("  BUSY low ab %lu bis %lu ms nach PON\n", (unsigned long)firstLow, (unsigned long)lastLow);
    hwReset(); read9(0x71, INPUT_PULLUP, up, 1);
    Serial.printf("  0x71 (9 Bit) danach: %02X\n", up[0]);
}

// Gegentest: 4,2" SES-imagotag BWR, UC8176, 400x300 (Sequenz wie GxEPD2_420c)
static void uc8176Test(void) {
    int w = g_w, h = g_h;
    g_w = 400; g_h = 300;
    Serial.println("\n--- UC8176-Test 4,2\" 400x300 (GxEPD2_420c) ---");
    hwReset();
    if (!waitBusy("nach HW-Reset", 2000)) { g_w = w; g_h = h; return; }
    const uint8_t bst[3] = {0x17, 0x17, 0x17};
    sendIndexData(0x06, bst, 3);                  // Booster Soft Start
    sendIndex(0x04);                              // Power ON
    bool pon = waitBusy("Power ON", 5000);
    sendIndex1(0x00, 0x0F);                       // PSR: LUT aus OTP, KWR
    uint32_t t0 = millis();
    sendPlane(0x10, 1, 0);
    sendPlane(0x13, 2, 0);
    Serial.printf("  Daten gesendet: %lu ms\n", (unsigned long)(millis() - t0));
    sendIndex(0x12);                              // Refresh
    bool ok = waitBusy("Refresh", 60000);
    sendIndex(0x02);                              // Power OFF
    waitBusy("Power OFF", 5000);
    Serial.println(pon && ok ? "Fertig - was zeigt das Panel?" : "Ablauf nicht sauber (siehe Zeiten).");
    g_w = w; g_h = h;
}

// Ein Schritt: Kommando (+Daten) senden, BUSY-LOW-Dauer messen
static void step(uint8_t c, const uint8_t *d, int n, uint32_t timeoutMs) {
    if (n > 0) sendIndexData(c, d, n); else sendIndex(c);
    uint32_t t0 = millis(), lowStart = 0, lowEnd = 0;
    while (millis() - t0 < 30) { if (!digitalRead(PIN_BUSY)) { lowStart = millis(); break; } }
    if (lowStart) {
        while (!digitalRead(PIN_BUSY) && millis() - lowStart < timeoutMs) delay(1);
        lowEnd = millis();
    }
    Serial.printf("    %02X", c);
    for (int i = 0; i < n; i++) Serial.printf(" %02X", d[i]);
    if (!lowStart) Serial.println("  -> BUSY bleibt HIGH");
    else if (!digitalRead(PIN_BUSY)) Serial.printf("  -> BUSY LOW > %lu ms (Timeout)\n", (unsigned long)timeoutMs);
    else Serial.printf("  -> BUSY LOW %lu ms\n", (unsigned long)(lowEnd - lowStart));
}

static void seqTests(void) {
    const uint8_t t19 = 0x19, e02 = 0x02, x0e = 0x0E, ff = 0xFF, z = 0x00;
    const uint8_t fa5 = 0xA5, fe3 = 0xE3;
    Serial.println("\n--- Sequenz-Tests ---");

    Serial.println("  A) Pervasive BWRY-Stil: E6 19, E0 02, A5, 04, 02");
    hwReset();
    step(0xE6, &t19, 1, 2000); step(0xE0, &e02, 1, 2000); step(0xA5, nullptr, 0, 3000);
    step(0x04, nullptr, 0, 3000); step(0x02, &z, 1, 3000);

    Serial.println("  B) wie A mit E5: E5 19, E0 02, A5, 04, 02");
    hwReset();
    step(0xE5, &t19, 1, 2000); step(0xE0, &e02, 1, 2000); step(0xA5, nullptr, 0, 3000);
    step(0x04, nullptr, 0, 3000); step(0x02, &z, 1, 3000);

    Serial.println("  C) A1 mit FF bzw. 00");
    hwReset(); step(0xA1, &ff, 1, 3000);
    hwReset(); step(0xA1, &z, 1, 3000);
    hwReset(); step(0xA1, nullptr, 0, 3000);

    Serial.println("  D) Freischalten FF A5, dann 04, 02, FF E3");
    hwReset();
    step(0xFF, &fa5, 1, 2000); step(0x04, nullptr, 0, 3000); step(0x02, &z, 1, 3000);
    step(0xFF, &fe3, 1, 2000);

    Serial.println("  E) Soft-Reset 00 0E, E5 19, E0 02, A1 FF, 04, 02");
    hwReset();
    step(0x00, &x0e, 1, 2000); step(0xE5, &t19, 1, 2000); step(0xE0, &e02, 1, 2000);
    step(0xA1, &ff, 1, 3000); step(0x04, nullptr, 0, 3000); step(0x02, &z, 1, 3000);

    hwReset();
    Serial.println("--- Sequenz-Tests fertig ---");
}

// ---------- Pervasive "mid-size" iTC (IST7232/IST7236), 7.41" 800x480 BWR -------
// Exakt nach PervasiveDisplays/EPD_Driver_GU_mid (EPD_Driver.cpp, globalUpdate)
static void sd(uint8_t idx, std::initializer_list<uint8_t> d) {
    uint8_t buf[8]; int n = 0;
    for (uint8_t v : d) buf[n++] = v;
    sendIndexData(idx, buf, n);
}

static void pdiMidUpdate(uint8_t dctl, int pattern = 0) {
    Serial.printf("\n--- Pervasive mid-size 7.41\" (DCTL=0x%02X, Muster %d) ---\n", dctl, pattern);
    g_w = 800; g_h = 480;
    // _reset(200, 20, 200, 50, 5)
    delay(200); digitalWrite(PIN_RST, HIGH); delay(20);
    digitalWrite(PIN_RST, LOW); delay(200);
    digitalWrite(PIN_RST, HIGH); delay(50);
    digitalWrite(PIN_CS, HIGH); delay(5);

    sd(0x01, {dctl});                                   // DCTL
    sd(0x13, {0x00, 0x3b, 0x00, 0x00, 0x1f, 0x03});     // DUW
    sd(0x90, {0x00, 0x3b, 0x00, 0xc9});                 // DRFW
    sd(0x12, {0x3b, 0x00, 0x14});                       // RAM_RW
    uint32_t t0 = millis();
    g_native = true;
    sendPlane(0x10, 1, pattern);                        // 1. Frame (schwarz)
    sd(0x12, {0x3b, 0x00, 0x14});                       // RAM_RW
    sendPlane(0x11, 2, pattern);                        // 2. Frame (rot)
    g_native = false;
    Serial.printf("  Daten gesendet: %lu ms\n", (unsigned long)(millis() - t0));

    // _DCDC_softStart_Mid()
    sd(0x05, {0x7d}); delay(200);
    sd(0x05, {0x00}); delay(10);
    sd(0xc2, {0x3f}); delay(1);
    sd(0xd8, {0x00});                                   // MS_SYNC
    sd(0xd6, {0x00});                                   // BVSS
    sd(0xa7, {0x10}); delay(100);
    sd(0xa7, {0x00}); delay(100);
    sd(0x03, {0x00, 0x01});                             // OSC
    sd(0x44, {0x00});
    sd(0x45, {0x80});
    sd(0xa7, {0x10}); delay(100);
    sd(0xa7, {0x00}); delay(100);
    sd(0x44, {0x06});
    sd(0x45, {0x82});                                   // Temperatur 25 C
    sd(0xa7, {0x10}); delay(100);
    sd(0xa7, {0x00}); delay(100);
    sd(0x60, {0x25});                                   // TCON
    sd(0x61, {0x00});                                   // STV_DIR
    sd(0x01, {0x00});                                   // DCTL
    sd(0x02, {0x00});                                   // VCOM
    uint8_t i51[] = {0x50, 0x01, 0x0a, 0x01};
    sendIndexData(0x51, &i51[0], 2);
    const uint8_t i09[] = {0x1f, 0x9f, 0x7f, 0xff};
    for (int v = 1; v <= 4; v++)  { sendIndexData(0x09, &i09[0], 1); i51[1] = v; sendIndexData(0x51, &i51[0], 2); sendIndexData(0x09, &i09[1], 1); delay(2); }
    for (int v = 1; v <= 10; v++) { sendIndexData(0x09, &i09[0], 1); i51[3] = v; sendIndexData(0x51, &i51[2], 2); sendIndexData(0x09, &i09[1], 1); delay(2); }
    for (int v = 3; v <= 10; v++) { sendIndexData(0x09, &i09[2], 1); i51[3] = v; sendIndexData(0x51, &i51[2], 2); sendIndexData(0x09, &i09[3], 1); delay(2); }
    for (int v = 9; v >= 2; v--)  { sendIndexData(0x09, &i09[2], 1); i51[2] = v; sendIndexData(0x51, &i51[2], 2); sendIndexData(0x09, &i09[3], 1); delay(2); }
    sendIndexData(0x09, &i09[3], 1); delay(10);
    Serial.println("  DC/DC Soft-Start gesendet");

    // _displayRefresh()
    if (!waitBusy("vor Refresh", 5000)) Serial.println("  (weiter trotz BUSY)");
    sd(0x15, {0x3c}); delay(5);
    uint32_t tr = millis();
    while (digitalRead(PIN_BUSY) == LOW && millis() - tr < 60000) delay(10);
    Serial.printf("  Refresh: BUSY LOW %lu ms%s\n", (unsigned long)(millis() - tr),
                  digitalRead(PIN_BUSY) ? "" : " (Timeout!)");

    // _DCDC_softShutdown_Mid()
    sd(0x09, {0x7f});
    sd(0x05, {0x7d});
    sd(0x09, {0x00});
    delay(200);
    waitBusy("Shutdown", 5000);
    digitalWrite(PIN_DC, LOW);
    digitalWrite(PIN_CS, LOW);
    digitalWrite(PIN_RST, LOW);
    digitalWrite(PIN_CS, HIGH);
    delay(10);
    digitalWrite(PIN_DC, HIGH);
    digitalWrite(PIN_RST, HIGH);
    Serial.println("Fertig - was zeigt das Panel?");
}

static void menu(void) {
    Serial.println("v = Pervasive mid-size 7.41\" (DCTL 08), V = dito DCTL 00");
    Serial.println("g = Gegentest 4,2\" UC8176");
    Serial.println("b = BUSY-Diagnose");
    Serial.printf("\nAufloesung: %dx%d\n", g_w, g_h);
    Serial.println("1 = Testbild, PSR CF 8D");
    Serial.println("2 = Testbild, PSR CF 89");
    Serial.println("3 = Testbild, ohne PSR (Werkseinstellung aus OTP)");
    Serial.println("c = Loeschen (beide Ebenen 0), PSR CF 8D");
    Serial.println("s = Breite/Hoehe tauschen");
    Serial.println("i = ID lesen");
}

void setup(void) {
    Serial.begin(115200);
    pinMode(PIN_CS, OUTPUT);   digitalWrite(PIN_CS, HIGH);
    pinMode(PIN_DC, OUTPUT);   digitalWrite(PIN_DC, HIGH);
    pinMode(PIN_RST, OUTPUT);  digitalWrite(PIN_RST, HIGH);
    pinMode(PIN_BUSY, INPUT);
    pinMode(PIN_DIN, OUTPUT);  digitalWrite(PIN_DIN, LOW);
    pinMode(PIN_SCK, OUTPUT);  digitalWrite(PIN_SCK, LOW);
    delay(1000);
    Serial.println("\n=== Testbild Pervasive E2741ES083 (Board 15823) ===");
    readId();
    menu();
}

void loop(void) {
    if (!Serial.available()) return;
    char c = (char)Serial.read();
    switch (c) {
        case '1': runTest(1, 0); menu(); break;
        case '2': runTest(2, 0); menu(); break;
        case '3': runTest(0, 0); menu(); break;
        case 'c': runTest(1, 1); menu(); break;
        case 's': { int t = g_w; g_w = g_h; g_h = t; menu(); break; }
        case 'i': readId(); break;
        case 'b': busyDiag(); break;
        case 'x': scanReads(); break;
        case 'w': wire3Test(); break;
        case 'v': pdiMidUpdate(0x08); menu(); break;
        case 'V': pdiMidUpdate(0x00); menu(); break;
        case 'W': pdiMidUpdate(0x08, 1); menu(); break;   // alles weiss
        case 'X': pdiMidUpdate(0x00, 1); menu(); break;   // alles weiss, DCTL 00
        case 'q': seqTests(); break;
        case 'z': {   // Vollscan aller 256 Kommandos: Antwort + BUSY-Aktivitaet
            Serial.println("\n--- Vollscan 00..FF (getrieben / BUSY-low) ---");
            uint8_t up[4], dn[4];
            for (int c = 0; c < 256; c++) {
                int bl, bl2;
                rd((uint8_t)c, INPUT_PULLUP, up, 4, &bl);
                rd((uint8_t)c, INPUT_PULLDOWN, dn, 4, &bl2);
                bool drv = false;
                for (int i = 0; i < 4; i++) if (up[i] == dn[i]) drv = true;
                if (!drv && bl == 0 && bl2 == 0) continue;
                Serial.printf("  %02X: ", c);
                for (int i = 0; i < 4; i++) {
                    if (up[i] == dn[i]) Serial.printf("%02X ", up[i]); else Serial.print("-- ");
                }
                Serial.printf(" BUSY-low %d/%d\n", bl, bl2);
            }
            hwReset();
            Serial.println("--- Vollscan fertig ---");
            break;
        }
        case 'g': uc8176Test(); menu(); break;
        case 'u': {   // UC8179-Stil: PWR + PON, BUSY beobachten, sofort POF
            Serial.println("\n--- UC8179-Test: PWR, PON, POF ---");
            hwReset();
            String s;
            for (int i = 0; i < 20; i++) { s += digitalRead(PIN_BUSY) ? '1' : '0'; delay(1); }
            const uint8_t pwr[4] = {0x07, 0x07, 0x3F, 0x3F};   // GxEPD2 GDEW075Z08
            sendIndexData(0x01, pwr, 4);
            sendIndex(0x04);                                   // PON
            s += '|';
            uint32_t t0 = millis(); uint32_t firstLow = 0, lastLow = 0;
            while (millis() - t0 < 1000) {
                bool b = digitalRead(PIN_BUSY);
                if (!b) { if (!firstLow) firstLow = millis() - t0 + 1; lastLow = millis() - t0; }
                if ((millis() - t0) % 10 == 0) { s += b ? '1' : '0'; delay(1); }
            }
            sendIndex(0x02);                                   // POF
            delay(100);
            Serial.printf("  BUSY (Reset 20ms | nach PON alle 10ms, 1s):\n  %s\n", s.c_str());
            Serial.printf("  BUSY low ab %lu ms bis %lu ms nach PON\n", (unsigned long)firstLow, (unsigned long)lastLow);
            // FLG nach Power ON lesen (UC: 0x71)
            digitalWrite(PIN_CS, LOW); digitalWrite(PIN_DC, LOW); bbByte(0x71);
            digitalWrite(PIN_DC, HIGH); uint8_t f = bbRead(); digitalWrite(PIN_CS, HIGH);
            Serial.printf("  0x71 nach POF: %02X\n", f);
            break;
        }
        case 'k': {   // Kopplung GPIO25 <-> GPIO26 in beiden Richtungen, je nur kurz treiben
            Serial.println("\n--- Kopplung GPIO26(RST) -> GPIO25(BUSY) ---");
            for (int lvl = 0; lvl < 2; lvl++) {
                pinMode(PIN_RST, OUTPUT); digitalWrite(PIN_RST, lvl);
                pinMode(PIN_BUSY, INPUT_PULLUP);   delay(2); int u = digitalRead(PIN_BUSY);
                pinMode(PIN_BUSY, INPUT_PULLDOWN); delay(2); int d = digitalRead(PIN_BUSY);
                Serial.printf("  RST=%d: BUSY up=%d down=%d\n", lvl, u, d);
            }
            pinMode(PIN_BUSY, INPUT);
            pinMode(PIN_RST, INPUT);
            Serial.println("--- Kopplung GPIO25(BUSY) -> GPIO26(RST) ---");
            for (int lvl = 0; lvl < 2; lvl++) {
                pinMode(PIN_BUSY, OUTPUT); digitalWrite(PIN_BUSY, lvl);
                pinMode(PIN_RST, INPUT_PULLUP);   delay(2); int u = digitalRead(PIN_RST);
                pinMode(PIN_RST, INPUT_PULLDOWN); delay(2); int d = digitalRead(PIN_RST);
                pinMode(PIN_BUSY, INPUT);
                Serial.printf("  BUSY=%d: RST up=%d down=%d\n", lvl, u, d);
            }
            pinMode(PIN_RST, OUTPUT); digitalWrite(PIN_RST, HIGH);
            break;
        }
        case 'p': {   // GPIO25/26 nur als Eingang: wer verhaelt sich wie BUSY?
            Serial.println("\n--- Pin-Diagnose GPIO25 (BUSY) / GPIO26 (RST), beide nur Eingang ---");
            pinMode(PIN_RST, INPUT_PULLUP);  pinMode(PIN_BUSY, INPUT_PULLUP);  delay(5);
            int u26 = digitalRead(PIN_RST), u25 = digitalRead(PIN_BUSY);
            pinMode(PIN_RST, INPUT_PULLDOWN); pinMode(PIN_BUSY, INPUT_PULLDOWN); delay(5);
            int d26 = digitalRead(PIN_RST), d25 = digitalRead(PIN_BUSY);
            Serial.printf("  GPIO26: up=%d down=%d  GPIO25: up=%d down=%d\n", u26, d26, u25, d25);
            String s25, s26;
            sendIndex1(0x00, 0x0E);                         // Soft-Reset
            for (int i = 0; i < 100; i++) { s25 += digitalRead(PIN_BUSY) ? '1':'0'; s26 += digitalRead(PIN_RST) ? '1':'0'; delay(1); }
            s25 += '|'; s26 += '|';
            sendIndex1(0x04, 0x00);                         // Power ON
            for (int i = 0; i < 200; i++) { s25 += digitalRead(PIN_BUSY) ? '1':'0'; s26 += digitalRead(PIN_RST) ? '1':'0'; delay(2); }
            sendIndex1(0x02, 0x00);                         // Power OFF
            Serial.println("  Verlauf (Pull-down; Soft-Reset 100ms | Power ON 400ms):");
            Serial.printf("  GPIO25: %s\n  GPIO26: %s\n", s25.c_str(), s26.c_str());
            pinMode(PIN_BUSY, INPUT);
            pinMode(PIN_RST, OUTPUT); digitalWrite(PIN_RST, HIGH);
            break;
        }
        case 'r': {   // Antwortet der Chip auch bei gehaltenem Reset?
            for (int k = 0; k < 2; k++) {
                hwReset();
                if (k == 1) { digitalWrite(PIN_RST, LOW); delay(20); }
                digitalWrite(PIN_CS, LOW);
                digitalWrite(PIN_DC, LOW); bbByte(0x70);
                digitalWrite(PIN_DC, HIGH);
                uint8_t a = bbRead(), b2 = bbRead(), c2 = bbRead();
                digitalWrite(PIN_CS, HIGH);
                Serial.printf("RST %s: 0x70 -> %02X %02X %02X\n", k ? "LOW gehalten" : "HIGH (normal)", a, b2, c2);
                digitalWrite(PIN_RST, HIGH); delay(20);
            }
            break;
        }
        case 'f': g_fixedWait = true; runTest(1, 0); g_fixedWait = false; menu(); break;
        default: break;
    }
}
