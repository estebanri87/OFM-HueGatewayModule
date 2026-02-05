# OFM-HueModule

OpenKNX Funktionsmodul zur Integration von Philips Hue Geräten in KNX-Systeme.

## Überblick

**OFM-HueModule** ermöglicht die Steuerung von Philips Hue Lampen, LED Stripes und Sensoren über KNX. Das Modul kommuniziert mit der Hue Bridge via Hue API v2 und stellt die Geräte als KNX-Kommunikationsobjekte bereit.

### Features (Phase 1)
- ✅ Automatische Hue Bridge Discovery (mDNS)
- ✅ Einfache Authentifizierung (Button-Press)
- ✅ Lampen schalten (Ein/Aus)
- ✅ Dimmen (0-100%)
- ✅ Farbtemperatur (2000-6500K)
- ✅ RGB Farben
- ✅ LED Stripes
- ✅ Bidirektionale Status-Updates (Echtzeit via SSE)
- ✅ Offline-Handling mit automatischer Wiederverbindung
- ✅ ETS-Konfiguration

### Datenfluss
```
Hue Bridge ←→ ESP32 (OFM-HueModule) ←→ KNX Bus
```

## Voraussetzungen

### Hardware
- ESP32-basierte OpenKNX Hardware (z.B. Adafruit Feather ESP32 V2)
- Philips Hue Bridge (Gen 2 oder neuer)
- Philips Hue Lampen/LED Stripes
- KNX-Anbindung (über NanoBCU oder ähnlich)

### Software
- PlatformIO
- OpenKNX Framework
- OGM-Common (^1.0.0)
- OFM-Network oder WLAN Module
- ETS6 (für Konfiguration)

## Installation

### Als eigenständiges Modul

OFM-HueModule ist ein **eigenständiges OpenKNX Modul**, das in verschiedene Firmware-Projekte integriert werden kann.

**Option 1: In bestehendes Firmware-Projekt einbinden**

```bash
cd <Ihr-OpenKNX-Firmware-Projekt>/lib
git submodule add https://github.com/OpenKNX/OFM-HueModule.git
git submodule update --init --recursive
```

**Option 2: Standalone Entwicklung/Test**

```bash
git clone https://github.com/OpenKNX/OFM-HueModule.git
cd OFM-HueModule
pio run
```

### SmartHomeBridge.xml erweitern

```xml
<!-- Nach BRI-Definition einfügen -->
<op:define prefix="HUE" ModuleType="9"
  share="../lib/OFM-HueModule/src/HueModule.share.xml"
  template="../lib/OFM-HueModule/src/HueModule.templ.xml"
  NumChannels="50" 
  KoOffset="1000">
  <op:verify File="../lib/OFM-HueModule/library.json" ModuleVersion="%HUE_VerifyVersion%" />
</op:define>
```

### main.cpp erweitern

```cpp
#include "HueModule.h"

void setup() {
  // ... andere Module ...
  openknx.addModule(9, openknxHueModule);  // Modul-ID 9
  openknx.setup();
}
```

### Build

```bash
# OpenKNXproducer ausführen
cd src
~/bin/OpenKNXproducer.exe create SmartHomeBridge-Dev.xml -h ../include/knxprod.h -debug

# Firmware bauen
cd ..
pio run -e develop_ESP32_USB

# Hochladen
pio run -e develop_ESP32_USB -t upload
```

## Konfiguration

### 1. Hue Bridge verbinden

**Option A: Automatische Erkennung (empfohlen)**
1. In ETS: "Automatische Bridge-Erkennung" aktivieren
2. Firmware flashen und starten
3. Bridge wird automatisch gefunden

**Option B: Manuelle IP-Adresse**
1. In ETS: IP-Adresse der Hue Bridge eingeben
2. Firmware flashen und starten

### 2. Authentifizierung

1. Nach dem Start blinkt die LED am ESP32 schnell
2. Console zeigt: "Waiting for Bridge button press..."
3. **Drücken Sie den Button auf der Hue Bridge** (innerhalb 30 Sekunden)
4. App-Key wird generiert und gespeichert
5. LED blinkt langsam = Verbunden

### 3. Geräte konfigurieren

In der ETS:

1. **Allgemeine Einstellungen**
   - Hue Integration aktivieren: ☑
   - Bridge-IP: Auto oder manuell
   - Anzahl Kanäle: z.B. 10

2. **Kanal 1 - Beispiel Wohnzimmerlampe**
   - Kanal aktiv: ☑
   - Geräte-Typ: Dimmbare Farblampe
   - Hue Geräte-ID: light-001 (aus Liste)
   - Name: Wohnzimmer Deckenlampe
   
3. **Kommunikationsobjekte zuweisen**
   - Schalten: 1/2/1
   - Helligkeit: 1/2/2
   - Farbtemperatur: 1/2/3
   - RGB: 1/2/4
   - Status: 1/2/10

4. **Programmieren** und testen

## Verwendung

### OpenKNX Console Commands

```
hue                     # Hue-Modul Status
hue scan                # Geräte neu scannen
hue bridge              # Bridge Info
hue auth                # Authentifizierung neu starten
hue light <id>          # Lampen-Info
```

### KNX Kommunikationsobjekte

**Pro Kanal (Beispiel Farblampe):**

| KO | Name | DPT | R/W | Beschreibung |
|----|------|-----|-----|--------------|
| 1 | Schalten | 1.001 | R/W | Ein/Aus |
| 2 | Helligkeit | 5.001 | R/W | 0-100% |
| 3 | Farbtemperatur | 7.600 | R/W | Kelvin |
| 4 | RGB | 232.600 | R/W | RGB Farbe |
| 5 | Status | 1.001 | R | Rückmeldung |
| 6 | Erreichbar | 1.001 | R | Online? |

## Troubleshooting

### Bridge wird nicht gefunden
- Überprüfen Sie, ob Bridge und ESP32 im gleichen Netzwerk sind
- Manuelle IP-Adresse in ETS eingeben
- Console-Ausgabe prüfen: `hue bridge`

### Authentifizierung schlägt fehl
- Button auf Bridge innerhalb 30 Sekunden drücken
- Bridge-Neustart versuchen
- Authentifizierung neu starten: `hue auth`

### Lampe reagiert nicht
- Lampe in Hue App prüfen (erreichbar?)
- Geräte-ID korrekt in ETS eingegeben?
- Console: `hue light <id>`

### Status-Updates verzögert
- Event Stream aktiviert? (Standard: ja)
- Netzwerk-Verbindung stabil?
- Console: `hue` → zeigt SSE Status

### Watchdog-Resets
- Loop-Zeit überprüfen (sollte <4ms sein)
- OPENKNX_WATCHDOG deaktivieren (nur für Debugging)
- Anzahl Kanäle reduzieren

## Entwicklung

### Repository-Struktur

```
OFM-HueModule/
├── src/
│   ├── HueModule.h/cpp           # Hauptmodul
│   ├── HueClient.h/cpp           # API Client
│   ├── HueDiscovery.h/cpp        # Bridge Discovery
│   ├── HueAuth.h/cpp             # Authentifizierung
│   ├── HueEventStream.h/cpp      # SSE Event Handler
│   ├── Devices/
│   │   ├── HueDeviceBase.h/cpp  # Basis-Klasse
│   │   └── HueLight.h/cpp       # Lampen
│   ├── HueModule.share.xml       # ETS Allgemein
│   └── HueModule.templ.xml       # ETS Kanäle
├── examples/
│   └── SimpleLight/              # Beispiel-Sketch
├── docs/
│   └── API.md                    # API-Dokumentation
├── library.json
├── README.md
├── CHANGELOG.md
└── LICENSE
```

### Build & Test

```bash
# Dependencies installieren
pio lib install

# Build
pio run

# Tests
pio test

# Beispiel hochladen
pio run -e example_simple_light -t upload
```

## Lizenz

GPL-3.0 (wie alle OpenKNX Module)

## Links

- **Hue API v2**: https://developers.meethue.com/develop/hue-api-v2/
- **OpenKNX**: https://github.com/OpenKNX
- **SmartHomeBridge**: https://github.com/OpenKNX/OAM-SmartHomeBridge
- **Support**: https://github.com/OpenKNX/OFM-HueModule/issues

## Roadmap

### Phase 1: Lampen & LED Stripes ✅ (Woche 1-3)
- Schalten, Dimmen, Farben
- ETS-Integration
- Event Stream

### Phase 2: Sensoren (geplant)
- Motion Sensoren
- Dimmer Switches
- Hue Tap
- Batterie-Status

### Phase 3: Erweiterte Features (geplant)
- Hue Szenen
- Hue Gruppen
- Hue Entertainment
- Adaptive Lighting

## Beiträge

Beiträge sind willkommen! Bitte Issues oder Pull Requests erstellen.

**Entwickelt von der OpenKNX Community**
