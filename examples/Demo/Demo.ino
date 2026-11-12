// Demo: Epd74-Treiber mit Adafruit GFX auf dem SES-imagotag 7,4" BWR.
// Das Bild wird einmal beim Start gezeichnet; Reset-Taste (EN) zeichnet neu.
#include <Arduino.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

#include <Epd74.h>

static Epd74 epd;

static void drawDemo() {
    epd.fillScreen(EPD_WHITE);

    // Kopfzeile
    epd.fillRect(0, 0, 800, 72, EPD_RED);
    epd.setFont(&FreeSansBold24pt7b);
    epd.setTextColor(EPD_WHITE);
    epd.setCursor(24, 52);
    epd.print("epaper74");

    // Text
    epd.setFont(&FreeSans12pt7b);
    epd.setTextColor(EPD_BLACK);
    epd.setCursor(24, 120);
    epd.print("SES-imagotag 7,4\" BWR  -  Pervasive E2741ES083");
    epd.setCursor(24, 154);
    epd.print("Adafruit GFX: Text, Linien, Kreise, Flaechen");
    epd.setTextColor(EPD_RED);
    epd.setCursor(24, 188);
    epd.print("Rot geht auch.");

    // Formen
    epd.fillCircle(110, 300, 70, EPD_BLACK);
    epd.fillCircle(110, 300, 40, EPD_RED);
    epd.drawRect(220, 230, 140, 140, EPD_BLACK);
    epd.fillRect(240, 250, 100, 100, EPD_RED);
    epd.fillTriangle(400, 370, 470, 230, 540, 370, EPD_BLACK);
    for (int i = 0; i < 12; i++) {
        epd.drawLine(580, 230 + i * 12, 770, 370 - i * 12, i % 2 ? EPD_RED : EPD_BLACK);
    }

    // Rahmen und Fusszeile
    epd.drawRect(0, 0, 800, 480, EPD_BLACK);
    epd.drawFastHLine(0, 420, 800, EPD_BLACK);
    epd.setTextColor(EPD_BLACK);
    epd.setCursor(24, 458);
    epd.print("Flexkabel unten, 800 x 480");
}

static void show(const char *what) {
    Serial.printf("%s ... ", what);
    bool ok = epd.display();
    Serial.printf("%s, %lu ms\n", ok ? "ok" : "BUSY-Timeout", (unsigned long)epd.lastRefreshMs());
}

// Gegen Geisterbilder: mehrmals schwarz, rot, weiss (je ca. 28 s)
static void cleanPanel(int rounds) {
    for (int r = 1; r <= rounds; r++) {
        Serial.printf("Reinigung Runde %d/%d\n", r, rounds);
        epd.fillScreen(EPD_BLACK); show("  schwarz");
        epd.fillScreen(EPD_RED);   show("  rot");
        epd.fillScreen(EPD_WHITE); show("  weiss");
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== epaper74 Demo ===");

    if (!epd.begin()) {
        Serial.println("Kein Speicher fuer die Bildpuffer.");
        return;
    }
    uint32_t id = epd.readChipId();
    Serial.printf("Chip-ID: %06lX %s\n", (unsigned long)id, id == 0x71325A ? "(ok)" : "(unerwartet!)");

    epd.setTemperature(25);
    drawDemo();
    Serial.println("Sende Bild, Refresh dauert ca. 28 s ...");
    bool ok = epd.display();
    Serial.printf("%s, Refresh %lu ms\n", ok ? "Fertig" : "BUSY-Timeout", (unsigned long)epd.lastRefreshMs());
    Serial.println("Befehle: c = Panel reinigen (2 Runden), d = Demo neu zeichnen");
}

void loop() {
    if (!Serial.available()) return;
    switch (Serial.read()) {
        case 'c': cleanPanel(2); Serial.println("Reinigung fertig."); break;
        case 'd': drawDemo(); show("Demo"); break;
        default: break;
    }
}
