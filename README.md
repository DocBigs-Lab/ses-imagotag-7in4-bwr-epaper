# ses-imagotag-7in4-bwr-epaper

Treiber (Bibliothek `Epd74`) für das 7,4"-E-Paper-Panel in Schwarz/Weiß/Rot aus SES-imagotag-Preisschildern, für ESP32 und Adafruit GFX.

*Driver for the 7.4" black/white/red e-paper panel from SES-imagotag shelf labels (Pervasive Displays E2741ES083, IST7x32 controller, 800 × 480) on ESP32, based on Adafruit GFX. Documentation is in German.*

## Passt das zu meinem Panel?

| Merkmal | Wert |
|---|---|
| Etikett auf dem Panel | `VE2741ES083...` (Pervasive **E2741ES083**) |
| Flexkabel | 24 Pin, Aufdruck `A1360059-00` |
| Auflösung, Farben | 800 × 480, Schwarz/Weiß/Rot |
| Chip-ID (Befehl `0x70`) | `71 32 5A` |

Das Panel versteht **keinen** der üblichen Befehlssätze (UC81xx, SSD16xx, aktuelle Pervasive-Treiber). GxEPD2 und ähnliche Bibliotheken funktionieren damit nicht. Wie wir das herausgefunden haben, steht in [doc/panel-7.4-zoll.md](doc/panel-7.4-zoll.md).

## Hardware

Getestet mit dem **Waveshare e-Paper ESP32 Driver Board** (SKU 15823). Das Flexkabel des Panels steckt direkt in der Buchse (Kontakte zeigen nach unten zum Board).

- Schalter 1 auf **B**, Schalter 2 auf **ON** (nur ohne USB umschalten)
- Pins: SCK 13, DIN 14, CS 15, DC 27, RST 26, BUSY 25. Andere Pins über `Epd74Pins`.

## Einbinden (PlatformIO)

```ini
lib_deps =
    https://github.com/DocBigs-Lab/ses-imagotag-7in4-bwr-epaper#v1.0.0
```

```cpp
#include <Epd74.h>

Epd74 epd;

void setup() {
    epd.begin();
    epd.fillScreen(EPD_WHITE);
    epd.setTextColor(EPD_RED);
    epd.setCursor(20, 40);
    epd.print("Hallo");
    epd.display();   // Refresh dauert ca. 28 s
}

void loop() {}
```

- Farben: `EPD_WHITE`, `EPD_BLACK`, `EPD_RED`
- Querformat 800 × 480, Flexkabel unten, (0,0) oben links. `setRotation()` wie bei GFX.
- `setTemperature()` übergibt die Umgebungstemperatur an den Chip (Standard 25 °C).
- `readChipId()` liefert `0x71325A`.
- Zwei Bildpuffer zu je 48 000 Byte im RAM.

## Geisterbilder

Gebrauchte Panels zeigen nach dem ersten Bild oft noch einen Schatten des alten Etiketts. Mehrere Reinigungsrunden (ganze Fläche schwarz, rot, weiß) entfernen ihn soweit noch möglich. Die Demo macht das mit `c` im seriellen Monitor.

## Inhalt

| Ordner | Inhalt |
|---|---|
| [src/](src/) | Bibliothek `Epd74` |
| [examples/Demo/](examples/Demo/) | Demo mit Text, Formen und Reinigung. Im Hauptordner mit `pio run -t upload` bauen |
| [extras/diagnose/](extras/diagnose/) | Diagnosefirmware aus der Fehlersuche, eigenes PlatformIO-Projekt |
| [doc/](doc/) | Hardware, Befehlsfolge, Bildformat, Fehlersuche |

## Anwendung

[esp32-epaper74-weather-station](https://github.com/DocBigs-Lab/esp32-epaper74-weather-station): Wetterstation mit Open-Meteo.

## Lizenz

MIT, siehe [LICENSE](LICENSE). Die Befehlsfolge des Panels stammt aus [PervasiveDisplays/EPD_Driver_GU_mid](https://github.com/PervasiveDisplays/EPD_Driver_GU_mid) (© Pervasive Displays Inc., BSD-Lizenz). Deren Hinweis steht ebenfalls in `LICENSE`.
