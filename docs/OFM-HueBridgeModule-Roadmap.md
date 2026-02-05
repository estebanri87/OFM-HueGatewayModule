# OFM-HueBridgeModule - 2-3 Wochen Entwicklungsplan

**Projekt**: Philips Hue Integration in KNX  
**Timeline**: 3 Wochen (2026-02-03 bis 2026-02-24)  
**Fokus Phase 1**: Lampen und LED Stripes  
**Testing**: Hardware verfügbar

## Woche 1: Proof of Concept (03.02 - 09.02)

### Tag 1-2: Repository & Grundstruktur
**Montag 03.02 - Dienstag 04.02**

- [ ] **Eigenständiges** Repository erstellen: `github.com/OpenKNX/OFM-HueBridgeModule`
  (NICHT als Submodule von SmartHomeBridge!)
- [ ] Verzeichnisstruktur anlegen
  ```
  OFM-HueBridgeModule/
  ├── src/
  │   ├── HueBridgeModule.h/cpp
  │   ├── HueBridgeDiscovery.h/cpp
  │   ├── HueBridgeAuth.h/cpp
  │   ├── HueBridgeClient.h/cpp
  │   └── Devices/
  │       └── HueBridgeLight.h/cpp
  ├── library.json
  ├── README.md
  └── examples/
      └── SimpleLight/
  ```
- [ ] library.json mit Dependencies erstellen
  - ArduinoJson (^6.21.0)
  - ESP32 mDNS
- [ ] Eigenständiges Modul-Repository setup
- [ ] Build-Test: Kompiliert es als standalone Modul?

**Deliverable**: Leeres Modul kompiliert erfolgreich

---

### Tag 3-4: Discovery & Authentication
**Mittwoch 05.02 - Donnerstag 06.02**

**HueBridgeDiscovery.cpp:**
- [ ] mDNS Service Discovery implementieren
  ```cpp
  bool findBridge(String& ipAddress)
  // Sucht nach "_hue._tcp.local"
  ```
- [ ] Fallback auf manuelle IP-Eingabe
- [ ] Bridge-IP im Flash speichern (Preferences)

**HueBridgeAuth.cpp:**
- [ ] Button-Press Authentication Flow
  ```cpp
  bool requestAppKey(const char* bridgeIP)
  // POST /api mit devicetype
  ```
- [ ] App-Key im Flash speichern
- [ ] LED-Feedback während Pairing (blinkt schnell)

**Test mit echter Hardware:**
- [ ] Bridge Discovery funktioniert
- [ ] Button drücken → App-Key erhalten
- [ ] App-Key persistent gespeichert

**Deliverable**: ESP32 kann sich mit Hue Bridge verbinden

---

### Tag 5-7: API Client & Erste Lampe
**Freitag 07.02 - Sonntag 09.02**

**HueBridgeClient.cpp:**
- [ ] HTTP(S) Client Setup
- [ ] GET /clip/v2/resource/light (alle Lampen holen)
- [ ] PUT /clip/v2/resource/light/{id} (Lampe schalten)
- [ ] Error Handling (401, 404, Timeout)

**HueBridgeLight.cpp:**
- [ ] Basis-Klasse für Lampe
- [ ] Schalten implementieren (on/off)
  ```cpp
  void setState(bool on)
  void processInputKo(GroupObject& ko)
  ```
- [ ] Status von Hue lesen und auf KNX ausgeben

**HueBridgeModule.cpp:**
- [ ] OpenKNX Modul Interface
  ```cpp
  void setup()
  void loop()
  void processInputKo(GroupObject& ko)
  ```
- [ ] 1 Kanal konfigurierbar (hardcoded)

**Test:**
- [ ] KNX GA schalten → Hue Lampe reagiert
- [ ] Lampe in Hue App schalten → KNX Status-KO aktualisiert

**Deliverable**: Eine Hue-Lampe über KNX steuerbar (Schalten)

---

## Woche 2: Erweiterte Features (10.02 - 16.02)

### Tag 8-9: Dimmen & Status-Updates
**Montag 10.02 - Dienstag 11.02**

**HueBridgeLight erweitern:**
- [ ] Dimmen implementieren
  ```cpp
  void setBrightness(uint8_t brightness)
  // PUT mit "dimming": {"brightness": 0-100}
  ```
- [ ] KO für Helligkeit (DPT 5.001)
- [ ] Übergangszeit (transition_duration)

**Polling:**
- [ ] Status-Polling implementieren (alle 5s)
- [ ] GET /clip/v2/resource/light/{id}
- [ ] Status-Änderungen auf KNX ausgeben

**Test:**
- [ ] Dimmen über KNX funktioniert
- [ ] Änderungen in Hue App → KNX Status
- [ ] Übergangszeiten korrekt

**Deliverable**: Dimmen funktioniert bidirektional

---

### Tag 10-11: Event Stream (SSE)
**Mittwoch 12.02 - Donnerstag 13.02**

**HueEventStream.cpp:**
- [ ] SSE Client implementieren
  ```cpp
  void connect()
  void handleEvents()
  // GET /eventstream/clip/v2
  ```
- [ ] Event Parsing (data: JSON)
- [ ] Events an HueBridgeLight weiterleiten
- [ ] Reconnect bei Verbindungsabbruch

**Refactoring:**
- [ ] Polling durch SSE ersetzen (Polling nur als Fallback)
- [ ] Status-Updates in Echtzeit

**Test:**
- [ ] Lampe in App ändern → sofort auf KNX
- [ ] Verbindung unterbrechen → automatisch reconnect

**Deliverable**: Echtzeit-Updates über Event Stream

---

### Tag 12-14: Multi-Lampen & ETS XML
**Freitag 14.02 - Sonntag 16.02**

**Multi-Lampen Support:**
- [ ] Array von HueBridgeLight Objekten
- [ ] Kanal-Konfiguration (Geräte-ID pro Kanal)
- [ ] 5-10 Lampen gleichzeitig steuerbar

**ETS XML (Basis):**
- [ ] HueBridgeModule.share.xml erstellen
  ```xml
  - Hue Integration aktiv [Ja/Nein]
  - Bridge IP [Auto/Manuell]
  - Anzahl Kanäle [1-50]
  ```
- [ ] HueBridgeModule.templ.xml erstellen
  ```xml
  - Kanal aktiv
  - Hue Geräte-ID
  - Geräte-Name
  - Schalten KO
  - Helligkeit KO
  ```
- [ ] In SmartHomeBridge.xml einbinden
- [ ] OpenKNXproducer Test

**Test:**
- [ ] ETS XML generiert knxprod.h
- [ ] Parameter aus ETS lesbar
- [ ] 5 Lampen parallel steuerbar

**Deliverable**: Multi-Lampen-Support mit ETS-Konfiguration

---

## Woche 3: LED Stripes & Polish (17.02 - 24.02)

### Tag 15-17: Farbtemperatur & RGB
**Montag 17.02 - Mittwoch 19.02**

**Farbtemperatur:**
- [ ] Farbtemperatur KO (DPT 7.600 Kelvin)
  ```cpp
  void setColorTemperature(uint16_t kelvin)
  // PUT mit "color_temperature": {"mirek": ...}
  ```
- [ ] Konvertierung Kelvin ↔ Mired

**RGB Farbe:**
- [ ] RGB KO (DPT 232.600)
  ```cpp
  void setColor(uint8_t r, uint8_t g, uint8_t b)
  // PUT mit "color": {"xy": {...}}
  ```
- [ ] Konvertierung RGB → CIE xy
- [ ] Rückmeldung xy → RGB

**LED Stripes:**
- [ ] Gleiche Implementation wie Lampen
- [ ] Gerätetyp-Erkennung (Lampe vs Stripe)
- [ ] Test mit echtem LED Stripe

**Test:**
- [ ] Farbtemperatur über KNX steuerbar
- [ ] RGB Farben funktionieren
- [ ] LED Stripes reagieren wie Lampen

**Deliverable**: Vollständige Lampen-/Stripe-Steuerung (Schalten, Dimmen, Farbe)

---

### Tag 18-19: Offline-Handling & Fehlerbehandlung
**Donnerstag 20.02 - Freitag 21.02**

**Fehlerbehandlung:**
- [ ] Verbindungs-Status KO (online/offline)
- [ ] Timeout Handling (Bridge nicht erreichbar)
- [ ] Fehlermeldungen auf Console
- [ ] LED-Feedback (langsam blinken = offline)

**Offline-Modus:**
- [ ] Letzter Status im RAM cachen
- [ ] Bei Offline: Status-KOs zeigen Cache
- [ ] Automatische Wiederverbindung (alle 30s)
- [ ] Bei Online: Status neu syncen

**Watchdog:**
- [ ] Loop-Timeout vermeiden (SSE in eigenem Task?)
- [ ] Graceful Shutdown bei Fehlern

**Test:**
- [ ] Bridge ausschalten → Status cached
- [ ] Bridge einschalten → automatisch reconnect
- [ ] Watchdog löst nicht aus

**Deliverable**: Robustes Fehlerhandling

---

### Tag 20-21: Dokumentation & Testing
**Samstag 22.02 - Sonntag 24.02**

**Dokumentation:**
- [ ] README.md vervollständigen
  - Installation
  - Konfiguration
  - Troubleshooting
- [ ] Applikationsbeschreibung-HueBridgeModule.md
  - User-Anleitung für ETS
  - Beispiel-Konfigurationen
- [ ] Code-Dokumentation (Doxygen)
- [ ] CHANGELOG.md

**Testing:**
- [ ] Integration Test: 10 Lampen + Stripes
- [ ] Stress-Test: Schnelle Schaltbefehle
- [ ] Offline/Online-Szenarien
- [ ] Performance-Messung (Loop-Zeit)

**Release vorbereiten:**
- [ ] Version 0.1.0 taggen
- [ ] Release Notes
- [ ] Binary für SmartHomeBridge erstellen

**Deliverable**: Release 0.1.0 - Production Ready für Phase 1

---

## Definition of Done (Phase 1)

### Funktional
✅ Mindestens 10 Hue Lampen/LED Stripes steuerbar  
✅ Schalten (Ein/Aus)  
✅ Dimmen (0-100%)  
✅ Farbtemperatur (2000-6500K)  
✅ RGB Farbe  
✅ Bidirektionale Status-Updates (Hue ↔ KNX)  
✅ Automatische Bridge Discovery  
✅ Button-Press Authentication  

### Technisch
✅ Event Stream (SSE) für Echtzeit-Updates  
✅ Offline-Handling mit automatischem Reconnect  
✅ ETS-Konfiguration voll funktionsfähig  
✅ knxprod.h korrekt generiert  
✅ Integration in SmartHomeBridge-Firmware  
✅ OpenKNX Console Commands (hue status, hue scan)  

### Qualität
✅ Keine Watchdog-Resets  
✅ Loop-Zeit < 4ms  
✅ Speicherverbrauch akzeptabel  
✅ Dokumentation vollständig  
✅ Code-Review durchgeführt  

---

## Risiken & Mitigationen

| Risiko | Wahrscheinlichkeit | Impact | Mitigation |
|--------|-------------------|--------|------------|
| SSE-Implementation komplex | Mittel | Hoch | Polling als Fallback |
| SSL/TLS Performance | Mittel | Mittel | HTTP statt HTTPS testen |
| RGB→xy Konvertierung fehlerhaft | Niedrig | Mittel | Bestehende Libraries nutzen |
| Speicher knapp (PSRAM) | Niedrig | Hoch | Max 50 Geräte limit |
| Timeline zu knapp | Mittel | Niedrig | Phase 2/3 Features verschieben |

---

## Nächste Phasen (nach Woche 3)

### Phase 2: Sensoren & Schalter (optional)
- Hue Motion Sensoren
- Hue Dimmer Switches
- Hue Tap
- Batterie-Status

### Phase 3: Erweiterte Features (optional)
- Hue Szenen
- Hue Gruppen/Räume
- Hue Entertainment (Sync)
- Adaptive Lighting

---

## Team & Kommunikation

**Developer**: [Ihr Name]  
**Hardware Testing**: [Ihr Name] (Hue Bridge + Geräte verfügbar)  
**Code Review**: OpenKNX Community  

**Kommunikation:**
- Daily Progress Updates (Github Issues/Discussions)
- Wöchentliches Review (Montag)
- Probleme sofort kommunizieren

**Tools:**
- Git: github.com/OpenKNX/OFM-HueBridgeModule
- Issues: Github Issues für Bugs/Features
- Dokumentation: im Repository (docs/)

---

**Start**: Montag 03.02.2026  
**Ziel**: Sonntag 24.02.2026  
**Status**: 🟢 Ready to Start

---

## Quick Start (für Entwicklung)

```bash
# Repository klonen
git clone https://github.com/OpenKNX/OFM-HueBridgeModule.git
cd OFM-HueBridgeModule

# Als Submodul in SmartHomeBridge einbinden
cd ../OAM-SmartHomeBridge/lib
git submodule add https://github.com/OpenKNX/OFM-HueBridgeModule.git

# SmartHomeBridge.xml anpassen
# (HUE-Modul einbinden)

# Build testen
cd ../..
pio run -e develop_ESP32_USB

# OpenKNXproducer
cd src
~/bin/OpenKNXproducer.exe create SmartHomeBridge-Dev.xml -h ../include/knxprod.h -debug
```

**Los geht's! 🚀**


