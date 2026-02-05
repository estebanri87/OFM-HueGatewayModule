# OFM-HueBridgeModule - Start Checkliste

**Datum**: 2026-02-03  
**Projekt**: Philips Hue Integration in KNX  
**Timeline**: 3 Wochen (bis 2026-02-24)

---

## Vor dem Start

### Repository Setup
- [ ] Github Repository erstellen: `github.com/OpenKNX/OFM-HueBridgeModule`
- [ ] `.gitignore` erstellen (ESP32, PlatformIO, VS Code)
- [ ] `LICENSE` Datei (GPL-3.0)
- [ ] Initial Commit mit README

### Entwicklungsumgebung
- [ ] VS Code installiert
- [ ] PlatformIO Extension installiert
- [ ] OpenKNX Workspace geöffnet
- [ ] Python für OpenKNXproducer verfügbar

### Hardware bereit
- [ ] ESP32 Board (Adafruit Feather V2 oder ähnlich)
- [ ] Philips Hue Bridge (Gen 2+) online
- [ ] Mindestens 2-3 Hue Lampen/Stripes verfügbar
- [ ] KNX-Anbindung (NanoBCU) funktionsfähig
- [ ] USB-Kabel für ESP32 Programmierung

### Dokumentation
- [ ] Alle Templates aus `doc/` in neues Repo kopieren:
    - `OFM-HueBridgeModule-README-Template.md` → `README.md`
    - `OFM-HueBridgeModule-library.json-Template` → `library.json`
  - `OFM-Hue-Konzept.md` → `docs/Konzept.md`
    - `OFM-HueBridgeModule-Roadmap.md` → `docs/Roadmap.md`

---

## Woche 1 - Tag 1 (Montag 03.02)

### Morgen: Repository & Struktur
- [ ] Repository klonen
- [ ] Verzeichnisstruktur erstellen:
  ```
    OFM-HueBridgeModule/
  ├── src/
  │   ├── HueBridgeModule.h
  │   ├── HueBridgeModule.cpp
  │   ├── HueBridgeDiscovery.h
  │   ├── HueBridgeDiscovery.cpp
  │   ├── HueBridgeAuth.h
  │   ├── HueBridgeAuth.cpp
  │   ├── HueBridgeClient.h
  │   ├── HueBridgeClient.cpp
  │   ├── HueEventStream.h
  │   ├── HueEventStream.cpp
  │   ├── Devices/
  │   │   ├── HueDeviceBase.h
  │   │   ├── HueDeviceBase.cpp
  │   │   ├── HueBridgeLight.h
  │   │   └── HueBridgeLight.cpp
  │   ├── HueBridgeModule.share.xml
  │   └── HueBridgeModule.templ.xml
  ├── examples/
  │   └── SimpleLight/
  │       └── SimpleLight.ino
  ├── docs/
  │   ├── Konzept.md
  │   ├── Roadmap.md
  │   └── API.md
  ├── library.json
  ├── README.md
  ├── CHANGELOG.md
  ├── LICENSE
  └── .gitignore
  ```
- [ ] `library.json` erstellen (Dependencies: ArduinoJson)
- [ ] Commit & Push

### Nachmittag: Standalone Build Setup
- [ ] `platformio.ini` erstellen:
  ```ini
  [env:esp32dev]
  platform = espressif32
  board = esp32dev
  framework = arduino
  lib_deps = 
      openknx/OGM-Common
      buelowp/ArduinoJson
  ```
- [ ] Minimal `main.cpp` für standalone Tests
- [ ] Build-Test: `pio run`
- [ ] Sollte kompilieren (auch wenn Module leer ist)

**Optional**: Test-Integration in SmartHomeBridge vorbereiten (später)

**Abends**: Git Commit "Initial standalone module structure"

---

## Woche 1 - Tag 2 (Dienstag 04.02)

### HueBridgeModule Basis implementieren

**HueBridgeModule.h:**
```cpp
#pragma once
#include "OpenKNX.h"

class HueBridgeModule : public OpenKNX::Module
{
public:
    void setup() override;
    void loop() override;
    void processInputKo(GroupObject& ko) override;
    bool enabled() override;
    const std::string name() override;
    const std::string version() override;
    
private:
    void setupDevices();
};

extern HueBridgeModule openknxHueBridgeModule;
```

**HueBridgeModule.cpp:**
```cpp
#include "HueBridgeModule.h"

void HueBridgeModule::setup()
{
    logInfoP("HueBridgeModule setup");
    // TODO: Discovery, Auth, Devices
}

void HueBridgeModule::loop()
{
    // TODO: Event handling
}

void HueBridgeModule::processInputKo(GroupObject& ko)
{
    // TODO: KO handling
}

bool HueBridgeModule::enabled()
{
    return ParamHUE_Enabled; // Aus knxprod.h
}

const std::string HueBridgeModule::name()
{
    return "HueBridgeModule";
}

const std::string HueBridgeModule::version()
{
    return "0.1.0";
}

HueBridgeModule openknxHueBridgeModule;
```

- [ ] Dateien erstellen
- [ ] Build-Test
- [ ] Commit "Add HueBridgeModule skeleton"

---

## Woche 1 - Tag 3 (Mittwoch 05.02)

### HueBridgeDiscovery implementieren

**Ziel**: Bridge automatisch finden oder manuelle IP nutzen

```cpp
// HueBridgeDiscovery.h
class HueBridgeDiscovery
{
public:
    bool findBridge(String& ipAddress);
    bool setManualIP(const char* ip);
    
private:
    bool discoverMDNS(String& ip);
    bool discoverSSDP(String& ip);
    void saveIP(const String& ip);
    String loadIP();
};
```

- [ ] mDNS Discovery (`_hue._tcp.local`)
- [ ] Manuelle IP als Fallback
- [ ] IP in Preferences speichern
- [ ] Mit echter Bridge testen
- [ ] Commit "Implement Bridge Discovery"

---

## Woche 1 - Tag 4 (Donnerstag 06.02)

### HueBridgeAuth implementieren

**Ziel**: Button-Press Authentication

```cpp
// HueBridgeAuth.h
class HueBridgeAuth
{
public:
    bool authenticate(const char* bridgeIP);
    bool hasValidAppKey();
    String getAppKey();
    
private:
    String _appKey;
    bool requestAppKey(const char* ip);
    void saveAppKey(const String& key);
    String loadAppKey();
};
```

**Test-Ablauf:**
1. ESP32 starten
2. LED blinkt schnell
3. Button auf Bridge drücken
4. App-Key erhalten
5. LED blinkt langsam
6. Neustart → App-Key noch da

- [ ] POST /api Request implementieren
- [ ] App-Key in NVS speichern
- [ ] LED-Feedback implementieren
- [ ] Mit Bridge testen
- [ ] Commit "Implement Button-Press Auth"

---

## Woche 1 - Tag 5 (Freitag 07.02)

### HueBridgeClient Basis implementieren

**Ziel**: API v2 Calls

```cpp
// HueBridgeClient.h
class HueBridgeClient
{
public:
    bool connect(const char* ip, const char* appKey);
    JsonDocument* getLights();
    bool setLightState(const char* id, bool on);
    
private:
    WiFiClient _client;
    String _appKey;
    String _bridgeIP;
};
```

**API Calls:**
- `GET /clip/v2/resource/light` → Liste aller Lampen
- `PUT /clip/v2/resource/light/{id}` → Lampe schalten

- [ ] HTTP Client Setup
- [ ] GET Request testen
- [ ] PUT Request testen
- [ ] Error Handling (401, 404)
- [ ] Commit "Implement HueBridgeClient basics"

---

## Woche 1 - Tag 6-7 (Wochenende)

### HueBridgeLight implementieren

**Ziel**: Erste Lampe steuerbar

```cpp
// HueBridgeLight.h
class HueBridgeLight : public HueDeviceBase
{
public:
    void setup(uint8_t channelIndex);
    void setState(bool on);
    void processInputKo(GroupObject& ko);
    
private:
    String _lightId;
    bool _state;
    GroupObject* _koSwitch;
};
```

**Integration:**
- HueBridgeModule erstellt HueBridgeLight
- KO-Callback an HueBridgeLight
- HueBridgeLight ruft HueBridgeClient
- Status auf KNX ausgeben

**Test:**
- KNX GA schalten → Lampe reagiert
- Lampe in App schalten → Status auf KNX (Polling alle 5s)

- [ ] HueBridgeLight Klasse erstellen
- [ ] KO-Handling implementieren
- [ ] Status-Polling (Basis)
- [ ] End-to-End Test: KNX → Hue
- [ ] Commit "First working light control"

**🎉 Meilenstein**: Eine Lampe über KNX steuerbar!

---

## Daily Checklist (für jeden Tag)

### Morgen
- [ ] Git pull (neueste Änderungen)
- [ ] Roadmap checken (was ist heute das Ziel?)
- [ ] Hardware bereit (ESP32 + Hue Bridge)

### Während der Entwicklung
- [ ] Regelmäßige Commits (mind. 2-3 pro Tag)
- [ ] Build-Tests vor jedem Commit
- [ ] Console-Output prüfen (keine Fehler)
- [ ] Memory-Leaks vermeiden (delete nach new)

### Abend
- [ ] Fortschritt dokumentieren (Commit Messages)
- [ ] Offene Punkte notieren (Github Issues)
- [ ] Nächsten Tag planen
- [ ] Git Push

---

## Tipps für erfolgreiche Entwicklung

### Code-Qualität
✅ Aussagekräftige Variablennamen  
✅ Kommentare für komplexe Logik  
✅ Error Handling nicht vergessen  
✅ Memory-Management (RAII)  
✅ Const-Correctness  

### Testing
✅ Nach jedem Feature: Build-Test  
✅ Mit echter Hardware testen (nicht nur Compiler)  
✅ Edge Cases testen (Bridge offline, falsche ID, etc.)  
✅ Console-Output nutzen für Debugging  

### Performance
✅ Loop-Zeit im Auge behalten (<4ms)  
✅ Blocking Calls vermeiden (HTTP mit Timeout)  
✅ JSON effizient parsen (Streaming)  
✅ Memory-Footprint messen  

### Git Workflow
✅ Atomic Commits (ein Feature = ein Commit)  
✅ Aussagekräftige Commit Messages  
✅ Regelmäßig pushen (mind. 1x täglich)  
✅ Bei Breaking Changes: neuer Branch  

---

## Notfall-Kontakte

**OpenKNX Community:**
- Github Discussions: https://github.com/OpenKNX/OpenKNX/discussions
- Issues: https://github.com/OpenKNX/OFM-HueBridgeModule/issues

**Hue Developer:**
- Forum: https://developers.meethue.com/forum
- API Docs: https://developers.meethue.com/develop/hue-api-v2/

---

## Erfolgs-Metriken (Ende Woche 1)

Am Ende von Woche 1 sollte erreicht sein:

✅ Repository vollständig aufgesetzt  
✅ Bridge Discovery funktioniert  
✅ Authentifizierung erfolgreich  
✅ Erste Lampe über KNX schaltbar  
✅ Status-Rückmeldung (Polling)  
✅ Dokumentation aktuell  
✅ Code auf Github  

**Definition of Done Woche 1:**
"Ich kann eine Hue-Lampe über eine KNX-Gruppenadresse ein- und ausschalten, und sehe den Status auf einem Status-KO."

---

**Viel Erfolg! 🚀**

Bei Fragen oder Problemen: Github Issues nutzen oder OpenKNX Community fragen.

**Start**: Montag 03.02.2026, 09:00 Uhr  
**Ziel Woche 1**: Freitag 09.02.2026, 18:00 Uhr


