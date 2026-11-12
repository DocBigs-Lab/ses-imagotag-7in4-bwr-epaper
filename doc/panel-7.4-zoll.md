# SES-imagotag 7,4" BWR an Waveshare ESP32 Driver Board

Stand: 12.11.2026. Das Panel zeigt Text, Grafik und Flächen sauber in Schwarz, Weiß und Rot.

## Kurzfassung

Das Panel stammt aus einem SES-imagotag-Preisschild und ist ein älteres Pervasive-Displays-Modell mit internem Timing-Controller (iTC) und einem Treiberchip der Reihe **IST7x32**. Es versteht **keinen** der üblichen Befehlssätze (UC81xx, SSD16xx, aktuelle Pervasive-Treiber). Es braucht die alte Pervasive-Ansteuerung für „mid-size“-Panels, bei der der Host die Hochspannung selbst schrittweise hochfährt. Vorlage: [PervasiveDisplays/EPD_Driver_GU_mid](https://github.com/PervasiveDisplays/EPD_Driver_GU_mid).

## Hardware

| Teil | Angabe |
|---|---|
| Panel | 7,4", Schwarz/Weiß/Rot, 800 × 480 |
| Etikett auf dem Panel | `10180279-05`, `P-1710015`, `VE2741ES083...` → Pervasive **E2741ES083** |
| Flexkabel | 24 Pin, Aufdruck `A1360059-00 01`, Datumscode `1735` (KW 35/2017) |
| Treiberchip | IST7x32 (Antwort auf `0x70`: `71 32 5A`) |
| Board | Waveshare e-Paper ESP32 Driver Board (SKU 15823) |
| Anschluss | Panel-Flexkabel direkt in die Board-Buchse, ohne Verlängerung |

### Schalter auf dem Board

| Schalter | Stellung | Bedeutung |
|---|---|---|
| 1 (A/B) | **B** | Messwiderstand des Hochspannungswandlers: B = 0,47 Ω (große Panels), A = 3 Ω (kleine Panels) |
| 2 (ON/OFF) | **ON** | Versorgung des USB-Seriell-Chips. Bei OFF erkennt der Rechner das Board nicht und es lässt sich nicht flashen |

Nur bei abgezogenem USB umschalten.

### Pins (ESP32 → Panel)

| Signal | GPIO | FPC-Pin |
|---|---|---|
| SCK | 13 | 13 |
| DIN (SDA, bidirektional) | 14 | 14 |
| CS | 15 | 12 |
| DC | 27 | 11 |
| RST | 26 | 10 |
| BUSY | 25 | 9 |

BUSY: **LOW = beschäftigt**, HIGH = bereit.

## Ansteuerung

### SPI

- 4-Draht-SPI, Mode 0, MSB zuerst. DC = LOW für das Register, DC = HIGH für die Daten.
- Pro Befehl: CS low, Index senden, CS high, dann CS low, Daten senden, CS high.
- Schreiben bis 5 MHz. Lesen braucht mindestens 600 ns pro Takt. Die Firmware arbeitet mit Bit-Bang und etwa 2–4 µs pro Takt.

### Ablauf eines vollständigen Bildaufbaus

Alle Werte stammen aus `EPD_Driver_GU_mid` (Zweig für 7,41"). Umgesetzt in `Epd74::display()` in [src/Epd74.cpp](../src/Epd74.cpp).

1. **Reset:** 200 ms warten, RST high, 20 ms, RST low, 200 ms, RST high, 50 ms, CS high, 5 ms.
2. **DCTL:** `01 08` (laut Herstellercode: 0 = IST7232, 8 = IST7236; `08` funktioniert).
3. **Bildfenster:**
   - `13 00 3B 00 00 1F 03` (DUW)
   - `90 00 3B 00 C9` (DRFW)
   - `12 3B 00 14` (RAM_RW)
4. **Ebene Schwarz:** `10` + 48 000 Byte
5. `12 3B 00 14`
6. **Ebene Rot:** `11` + 48 000 Byte
7. **DC/DC-Soft-Start:**
   - `05 7D`, 200 ms, `05 00`, 10 ms
   - `C2 3F`, 1 ms, `D8 00`, `D6 00`
   - `A7 10`, 100 ms, `A7 00`, 100 ms
   - `03 00 01`, `44 00`, `45 80`, `A7 10`, 100 ms, `A7 00`, 100 ms
   - `44 06`, `45 82` (Temperatur 25 °C), `A7 10`, 100 ms, `A7 00`, 100 ms
   - `60 25`, `61 00`, `01 00`, `02 00`
   - danach die Rampe über `09` und `51` in vier Schleifen (genaue Werte im Code)
8. **Refresh:** warten bis BUSY high, `15 3C`. BUSY bleibt etwa **27,5 s** LOW.
9. **Abschalten:** `09 7F`, `05 7D`, `09 00`, 200 ms, warten bis BUSY high, danach DC, CS und RST auf LOW.

### Bildformat

- Pro Ebene 1 Bit pro Pixel, das höchstwertige Bit ist das erste Pixel.
- Ebene `0x10`: 1 = **schwarz**. Ebene `0x11`: 1 = **rot**. Beide 0 = weiß. Beide 1 = schwarz.
- Das Panel ist intern hochkant organisiert: **800 Zeilen zu je 480 Pixeln (60 Byte)**.
- Umrechnung für Querformat 800 × 480 mit Flexkabel unten:
  - native Zeile `L` = `799 − x`
  - Pixel in der Zeile = `y`

## Firmware

| Teil | Ort | Bauen und flashen |
|---|---|---|
| Treiber (Bibliothek) | [src/Epd74.h](../src/Epd74.h), [src/Epd74.cpp](../src/Epd74.cpp) | wird von Projekten per `lib_deps` eingebunden |
| Demo | [examples/Demo/Demo.ino](../examples/Demo/Demo.ino) | im Repo-Hauptordner: `pio run -t upload` |
| Diagnosefirmware | [extras/diagnose/](../extras/diagnose/) | in `extras/diagnose`: `pio run -t upload` |

### Treiber `Epd74`

Abgeleitet von Adafruit GFX, also mit allen GFX-Funktionen (Text, Fonts, Linien, Kreise, Bitmaps). Zwei Bildpuffer zu je 48 000 Byte im RAM. Hardware-SPI mit 4 MHz, nur `readChipId()` liest per Bit-Bang.

```cpp
#include <Epd74.h>
Epd74 epd;                       // Standard-Pins des Waveshare-Boards

epd.begin();                     // Puffer anlegen, Pins und SPI einrichten
epd.setTemperature(21.5);        // optional, Standard 25 Grad C
epd.fillScreen(EPD_WHITE);
epd.setTextColor(EPD_RED);
epd.setCursor(20, 40);
epd.print("Hallo");
epd.display();                   // Bild senden und Refresh, ca. 28 s
```

- Farben: `EPD_WHITE`, `EPD_BLACK`, `EPD_RED`
- Koordinaten: Querformat 800 × 480, Flexkabel unten, (0,0) oben links. `setRotation(1..3)` dreht wie bei GFX üblich.
- `readChipId()` liefert `0x71325A`.
- `setDctl(0x00)` schaltet auf die Chipvariante IST7232 (Standard `0x08`).

Die Demo zeichnet beim Start ein Beispielbild. Die Taste EN auf dem Board zeichnet es neu. Befehle im seriellen Monitor (115200 Baud):

| Taste | Funktion |
|---|---|
| `c` | Panel reinigen: 2 Runden schwarz, rot, weiß (ca. 3 Minuten) |
| `d` | Demo neu zeichnen |

### Diagnosefirmware

Bedienung über den seriellen Monitor (115200 Baud). Die wichtigsten Befehle:

| Taste | Funktion |
|---|---|
| `v` | Testbild 7,4" mit der funktionierenden Pervasive-Sequenz (DCTL `08`) |
| `V` | dasselbe mit DCTL `00` |
| `W` | ganze Fläche weiß (DCTL `08`) |
| `X` | ganze Fläche weiß (DCTL `00`) |
| `g` | Gegentest für das 4,2"-Panel (UC8176, 400 × 300) |
| `i` | ID lesen (`0x70`) |
| `x` | Lesescan ausgewählter Befehle |

Testbild: vier senkrechte Balken (schwarz, weiß, rot, schwarz) und ein rotes Quadrat 64 × 64 oben links.

Die übrigen Befehle (`1`, `2`, `3`, `c`, `s`, `b`, `f`, `r`, `p`, `k`, `u`, `w`, `q`, `z`) stammen aus der Fehlersuche und funktionieren mit diesem Panel nicht. `z` (Vollscan aller 256 Befehle) und `q` senden auch unbekannte Befehle wie `A1 FF`. Das Panel geht danach in einen Zustand, aus dem erst ein Reset es wieder holt.

## Fehlersuche: was nicht funktioniert hat

| Versuch | Ergebnis |
|---|---|
| Pervasive iTC klassisch (`00 0E`, `E5`, `E0`, `04`, `12`) | Befehle werden angenommen, aber kein BUSY und kein Bild |
| UC8179 (`01`, `04`) | keine Reaktion |
| Pervasive BWRY (`70` → `0D 04`) und Wide (`B9` → `16 95`) | andere ID, keine OTP-Daten |
| SSD16xx (`2F`, `2E`) | keine Antwort |
| 3-Draht-SPI (9 Bit) | gar keine Antwort, Panel läuft also im 4-Draht-Modus |
| Panel gedreht oder gespiegelt | durch Messungen ausgeschlossen |
| 4,2"-SES-Panel (UC8176) am selben Board | läuft sofort; das Board ist in Ordnung |

Die Antworten im Lesescan (`70` → `71 32 5A`, `72`, `A8` → `A5`, BUSY bei `A1 FF`) passten zu keinem bekannten Controller. Den Ausschlag gab die Pervasive-Doku zu älteren großen Panels mit Treiberchip IST7136, daraus ergab sich der Verweis auf `EPD_Driver_GU_mid`.

## Geisterbild des alten Etiketts entfernen

Panels aus gebrauchten Preisschildern haben oft jahrelang dasselbe Bild gezeigt. Ein einzelner Refresh lässt davon einen Schatten stehen (bei uns: der QR-Code des Etiketts). Abhilfe ist ein Reinigungsdurchlauf: die ganze Fläche mehrmals nacheinander schwarz, rot und weiß schalten.

In der Demo: `c` im seriellen Monitor startet 2 Runden (6 Refreshes, ca. 3 Minuten), `d` zeichnet danach die Demo neu. Jeder Durchgang hilft etwas. Nach insgesamt 10 Runden war der Schatten komplett verschwunden. Vor dem ersten Einsatz eines Panels daher mehrmals `c` senden.

## Offene Punkte

- **Temperatur** ist fest auf 25 °C (`45 82`). Bei deutlich anderen Temperaturen kann Rot blasser werden oder Restbilder bleiben. Die Skala laut Pervasive: −40 °C = `0x00`, 0 °C = `0x50`, 25 °C = `0x82`, 87 °C = `0xFE`, Schritt 0,5 °C.
- **DCTL** `08` oder `00` – beide noch nicht verglichen.
- Das Panel hat ein OTP mit Panel-Parametern (bei verwandten Panels über `B9` lesbar). Der Herstellercode nutzt feste Werte; auslesen haben wir es noch nicht.

## Quellen

- [PervasiveDisplays/EPD_Driver_GU_mid](https://github.com/PervasiveDisplays/EPD_Driver_GU_mid): Herstellertreiber für 5,81" und 7,41", Grundlage der Ansteuerung
- [Pervasive Knowledge Base, Legacy Large EPDs](https://docs.pervasivedisplays.com/knowledge/Archives/Hardware/Screens/large-epds/index.html): SPI-Protokoll und Abläufe der IST7136-Familie
- [Waveshare Wiki: E-Paper ESP32 Driver Board](https://www.waveshare.com/wiki/E-Paper_ESP32_Driver_Board): Schalter und Pinbelegung
- [TenOfNine/imagotag-vusion-esl-reverse-engineering](https://github.com/TenOfNine/imagotag-vusion-esl-reverse-engineering): verwandtes 7,4"-Vierfarbpanel, FPC-Belegung
