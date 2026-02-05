# OFM-HueModule - Eigenständiges OpenKNX Modul

## ⚠️ Wichtig: Architektur-Klarstellung

**OFM-HueModule ist ein EIGENSTÄNDIGES OpenKNX Modul-Repository**, vergleichbar mit:
- OFM-LogicModule
- OFM-Network
- OFM-FunctionBlocks
- OFM-SmartHomeBridge

Es ist **NICHT** ein Submodule oder Teil von SmartHomeBridge!

---

## ✅ Korrekte Architektur

### Repository-Struktur (OpenKNX Organisation)

```
github.com/OpenKNX/
├── OFM-LogicModule/           # Eigenständiges Modul
├── OFM-Network/                # Eigenständiges Modul
├── OFM-SmartHomeBridge/        # Eigenständiges Modul
├── OFM-FunctionBlocks/         # Eigenständiges Modul
└── OFM-HueModule/              # Eigenständiges Modul (NEU)
    ├── src/
    │   ├── HueModule.h/cpp
    │   ├── HueModule.share.xml
    │   └── HueModule.templ.xml
    ├── library.json
    ├── platformio.ini           # Standalone Build
    └── README.md
```

### Verwendung in Firmware-Projekten

**Firmware-Projekte** (wie OAM-SmartHomeBridge) können OFM-HueModule **optional** als Library einbinden:

```
OAM-SmartHomeBridge/            # Firmware-Projekt
├── lib/
│   ├── OFM-LogicModule/        # Git Submodule
│   ├── OFM-Network/            # Git Submodule
│   ├── OFM-SmartHomeBridge/    # Git Submodule
│   └── OFM-HueModule/          # Git Submodule (optional!)
├── src/
│   ├── main.cpp
│   └── SmartHomeBridge.xml
└── platformio.ini
```

---

## 🔄 Entwicklungs-Workflow

### Standalone Entwicklung (empfohlen)

```bash
# 1. Eigenständiges Repository klonen
git clone https://github.com/OpenKNX/OFM-HueModule.git
cd OFM-HueModule

# 2. Standalone entwickeln und testen
pio run
pio test

# 3. Commits direkt in OFM-HueModule
git add .
git commit -m "Feature X implementiert"
git push
```

### Integration in Firmware-Projekt (optional für Tests)

```bash
# 4. Später: In Firmware-Projekt als Library einbinden
cd ../OAM-SmartHomeBridge/lib
git submodule add https://github.com/OpenKNX/OFM-HueModule.git

# 5. SmartHomeBridge.xml erweitern (optional)
# Modul einbinden, wenn gewünscht

# 6. Build-Test
cd ../..
pio run -e develop_ESP32_USB
```

---

## 📁 OFM-HueModule Repository-Struktur

### Vollständiges, eigenständiges Projekt:

```
OFM-HueModule/
├── src/                        # Modul-Quellcode
│   ├── HueModule.h
│   ├── HueModule.cpp
│   ├── HueClient.h
│   ├── HueClient.cpp
│   ├── HueDiscovery.h
│   ├── HueDiscovery.cpp
│   ├── HueAuth.h
│   ├── HueAuth.cpp
│   ├── HueEventStream.h
│   ├── HueEventStream.cpp
│   ├── Devices/
│   │   ├── HueDeviceBase.h
│   │   ├── HueDeviceBase.cpp
│   │   ├── HueLight.h
│   │   └── HueLight.cpp
│   ├── HueModule.share.xml     # ETS Allgemein
│   └── HueModule.templ.xml     # ETS Kanäle
│
├── examples/                   # Beispiel-Anwendungen
│   ├── SimpleLight/
│   │   └── SimpleLight.ino
│   └── MultiDevice/
│       └── MultiDevice.ino
│
├── test/                       # Unit Tests
│   ├── test_discovery/
│   ├── test_auth/
│   └── test_client/
│
├── docs/                       # Dokumentation
│   ├── API.md
│   ├── Integration.md
│   └── Examples.md
│
├── platformio.ini              # Standalone Build-Config
├── library.json                # PlatformIO Library
├── README.md
├── CHANGELOG.md
├── LICENSE
└── .gitignore
```

### platformio.ini (Standalone)

```ini
[platformio]
default_envs = esp32dev

[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps = 
    openknx/OGM-Common@^1.0.0
    buelowp/ArduinoJson@^6.21.0
monitor_speed = 115200

[env:test]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps = 
    ${env:esp32dev.lib_deps}
test_framework = unity
```

---

## 🔧 Build-Optionen

### Option 1: Standalone Build (OFM-HueModule selbst)

```bash
cd OFM-HueModule
pio run                    # Build Modul
pio test                   # Run Tests
pio run -t upload          # Upload Beispiel
```

### Option 2: Als Library in Firmware-Projekt

```bash
cd OAM-SmartHomeBridge
pio run -e develop_ESP32_USB    # Build mit HueModule
```

---

## 📦 Veröffentlichung

### Als PlatformIO Library

OFM-HueModule wird als offizielle PlatformIO Library registriert:

```ini
# In anderen Projekten verwendbar via:
[env:myproject]
lib_deps = 
    openknx/OFM-HueModule@^0.1.0
```

### Releases

Versionierung nach Semantic Versioning:
- `0.1.0` - Initial Release (Phase 1: Lampen)
- `0.2.0` - Phase 2 (Sensoren)
- `0.3.0` - Phase 3 (Erweiterte Features)
- `1.0.0` - Production Ready

---

## 🎯 Warum eigenständiges Modul?

### Vorteile:

✅ **Wiederverwendbarkeit**  
Kann in verschiedenen OpenKNX-Projekten verwendet werden, nicht nur SmartHomeBridge

✅ **Unabhängige Entwicklung**  
Eigener Release-Zyklus, eigene Versionierung

✅ **Klare Verantwortlichkeiten**  
Eigenes Repository, eigenes Issue-Tracking

✅ **Bessere Testbarkeit**  
Standalone Tests möglich, keine Abhängigkeit von SmartHomeBridge

✅ **Community Contributions**  
Einfacher für externe Entwickler beizutragen

✅ **Modulare Architektur**  
Entspricht OpenKNX-Designprinzipien

---

## 🔗 Vergleich mit anderen Modulen

### OFM-LogicModule
- Eigenständiges Repo: ✅
- In SmartHomeBridge verwendbar: ✅
- Standalone entwickelbar: ✅

### OFM-Network
- Eigenständiges Repo: ✅
- In SmartHomeBridge verwendbar: ✅
- Standalone entwickelbar: ✅

### OFM-HueModule (neu)
- Eigenständiges Repo: ✅
- In SmartHomeBridge verwendbar: ✅ (optional)
- Standalone entwickelbar: ✅

**Gleiche Architektur wie alle anderen OFM-Module!**

---

## ❌ Häufige Missverständnisse

### ❌ Falsch: "OFM-HueModule ist Teil von SmartHomeBridge"
**✅ Richtig**: OFM-HueModule ist ein eigenständiges Modul, das optional in SmartHomeBridge verwendet werden kann

### ❌ Falsch: "Man muss SmartHomeBridge haben um HueModule zu nutzen"
**✅ Richtig**: HueModule kann in jedem OpenKNX-Firmware-Projekt verwendet werden

### ❌ Falsch: "Development nur innerhalb von SmartHomeBridge"
**✅ Richtig**: Standalone-Entwicklung im OFM-HueModule Repository

### ❌ Falsch: "Git Submodule von SmartHomeBridge"
**✅ Richtig**: Eigenständiges Git Repository unter github.com/OpenKNX/

---

## 🚀 Quick Start (Richtig)

### Für Modul-Entwickler:

```bash
# 1. Repository erstellen
gh repo create OpenKNX/OFM-HueModule --public

# 2. Klonen und entwickeln
git clone https://github.com/OpenKNX/OFM-HueModule.git
cd OFM-HueModule

# 3. Standalone entwickeln
# Code schreiben...
pio run
git add .
git commit -m "Feature implementiert"
git push
```

### Für Firmware-Projekt-Entwickler:

```bash
# Optional: In Firmware-Projekt einbinden
cd MeinOpenKNX-Projekt/lib
git submodule add https://github.com/OpenKNX/OFM-HueModule.git

# In XML einbinden (optional)
# In main.cpp einbinden (optional)

# Build
cd ../..
pio run
```

---

## 📋 Checkliste: Eigenständiges Modul

- [x] Eigenes GitHub Repository unter OpenKNX
- [x] Eigene library.json (PlatformIO Library)
- [x] Eigene platformio.ini (Standalone Build)
- [x] Eigene Versionierung (CHANGELOG.md)
- [x] Eigenes Issue-Tracking
- [x] Eigene Dokumentation
- [x] Eigene Examples
- [x] Eigene Tests
- [x] Kann standalone gebaut werden
- [x] Kann in verschiedenen Projekten verwendet werden

---

**Fazit**: OFM-HueModule = Eigenständiges OpenKNX Modul, genau wie alle anderen OFM-Module auch!
