# OFM-HueModule

OpenKNX Funktionsmodul zur Integration von Philips Hue Geräten in KNX-Systeme.

## Status

🚧 **In Entwicklung** - Version 0.1.0 (Proof of Concept)

## Features (Phase 1 - geplant)

- Automatische Hue Bridge Discovery (mDNS)
- Button-Press Authentifizierung
- Lampen & LED Stripes
  - Schalten (Ein/Aus)
  - Dimmen (0-100%)
  - Farbtemperatur (2000-6500K)
  - RGB Farben
- Bidirektionale Status-Updates (Event Stream)
- ETS-Konfiguration

## Development

```bash
git clone https://github.com/OpenKNX/OFM-HueModule.git
cd OFM-HueModule
pio run
```

## Lizenz

GPL-3.0 (wie alle OpenKNX Module)

## Links

- OpenKNX: https://openknx.de
- Hue API: https://developers.meethue.com/develop/hue-api-v2/
