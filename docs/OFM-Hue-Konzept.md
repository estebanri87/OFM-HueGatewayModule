# OFM-HueModule Konzept - Philips Hue Integration in KNX

## 1. Übersicht

### Zielsetzung
Integration von Philips Hue Leuchten und Sensoren in ein KNX-System über ein neues OpenKNX-Modul.

### Datenfluss
**Hue Bridge → ESP32/OFM-HueModule → KNX Bus**

```
┌─────────────┐      Hue API v2      ┌──────────────┐      KNX       ┌─────────┐
│ Hue Bridge  │ ◄─────────────────► │ OFM-HueModule│ ◄───────────► │ KNX Bus │
│ (Phillips)  │   HTTP/JSON/SSE      │  (ESP32)     │               │         │
└─────────────┘                      └──────────────┘               └─────────┘
      │                                      │
      ├── Hue Lampen                        ├── KO: Schalten
      ├── Hue Dimmer                        ├── KO: Helligkeit
      ├── Hue Sensoren                      ├── KO: Farbe (HSV/RGB)
      └── Hue Schalter                      └── KO: Status/Sensor-Werte
```

## 2. Architekturentscheidung

### ✅ Empfehlung: Separates OFM-Hue Modul

**Begründung:**
- **OFM-SmartHomeBridge**: KNX → Smart Home (Server/Emulation)
- **OFM-HueModule**: Hue → KNX (Client/Consumer)
- Unterschiedliche technische Anforderungen
- Klare Trennung der Verantwortlichkeiten
- Kann unabhängig entwickelt und deployed werden

### Alternative: Erweiterung SmartHomeBridge
❌ **Nicht empfohlen** - würde Modul zu komplex machen und verschiedene Datenrichtungen vermischen

## 3. Technische Anforderungen

### 3.1 Hue API v2
- **Protokoll**: HTTPS/HTTP REST API + Server-Sent Events (SSE) für Events
- **Authentifizierung**: App-Key Generierung per Button-Press auf Bridge
- **Endpoints**:
  - `/clip/v2/resource` - Alle Ressourcen
  - `/clip/v2/resource/light` - Lampen
  - `/clip/v2/resource/device` - Geräte
  - `/eventstream/clip/v2` - Event Stream (SSE)

### 3.2 Bridge Discovery
1. **mDNS** (empfohlen): `_hue._tcp.local`
2. **SSDP** (UPnP): `urn:schemas-upnp-org:device:basic:1`
3. **Manuelle Eingabe**: IP-Adresse in ETS konfigurierbar

### 3.3 Authentifizierung Flow
```
1. User konfiguriert Bridge-IP in ETS
2. Firmware versucht Verbindung
3. Falls kein App-Key vorhanden:
   - Blinkendes Signal auf ESP32 (LED)
   - User drückt Button auf Hue Bridge
   - App-Key wird generiert und im Flash gespeichert
4. Verbindung hergestellt
```

### 3.4 Offline-Betrieb
**Problem**: Hue Bridge erforderlich für Steuerung
**Lösung**: 
- Letzter bekannter Status wird gecacht
- KNX-Befehle werden an Hue weitergeleitet (wenn online)
- Bei Offline: Status-KOs zeigen letzten bekannten Zustand
- Automatische Wiederverbindung bei Verfügbarkeit

## 4. ETS Konfiguration

### 4.1 Allgemeine Einstellungen

```xml
<op:define prefix="HUE" ModuleType="9"
  share="../lib/OFM-HueModule/src/HueModule.share.xml"
  template="../lib/OFM-HueModule/src/HueModule.templ.xml"
  NumChannels="50" 
  KoOffset="1000">
  <op:verify File="../lib/OFM-Hue/library.json" ModuleVersion="%HUE_VerifyVersion%" />
</op:define>
```

### 4.2 Parameter (share.xml)
```
- Hue Integration aktivieren [Ja/Nein]
- Bridge IP-Adresse [xxx.xxx.xxx.xxx] oder [Automatisch]
- Bridge Discovery Methode [Auto/mDNS/SSDP/Manuell]
- App-Key [automatisch generiert, nur anzeigen]
- Polling-Interval [1-60s] (Fallback wenn SSE nicht verfügbar)
- Anzahl Kanäle [1-50]
- Verbindungs-Timeout [5-60s]
```

### 4.3 Kanal-Parameter (templ.xml)
Pro Kanal:
```
- Kanal aktiv [Ja/Nein]
- Hue Geräte-Typ [Lampe/Dimmer/Farblampe/Sensor/Schalter]
- Hue Geräte-ID [Dropdown aus erkannten Geräten]
- Geräte-Name [aus Hue übernommen, editierbar]
- Status-Polling [Ja/Nein/Nur bei Änderung]

Für Lampen:
- Schalten KO
- Helligkeit KO [optional]
- Farbtemperatur KO [optional]
- HSV/RGB Farbe KO [optional]
- Szenen KO [optional]

Für Sensoren:
- Sensor-Wert KO
- Batterie-Status KO
- Verbindungs-Status KO
```

## 5. Modul-Struktur

### 5.1 Verzeichnisstruktur
```
lib/OFM-Hue/
├── src/
│   ├── HueModule.h                    # OpenKNX Modul Interface
│   ├── HueModule.cpp
│   │
│   ├── HueClient.h                    # Hue API v2 Client
│   ├── HueClient.cpp
│   │
│   ├── HueDiscovery.h                 # Bridge Discovery
│   ├── HueDiscovery.cpp
│   │
│   ├── HueAuth.h                      # Authentifizierung
│   ├── HueAuth.cpp
│   │
│   ├── HueEventStream.h               # SSE Event Handler
│   ├── HueEventStream.cpp
│   │
│   ├── Devices/
│   │   ├── HueDeviceBase.h           # Basis-Klasse
│   │   ├── HueDeviceBase.cpp
│   │   ├── HueLight.h                # Hue Lampen
│   │   ├── HueLight.cpp
│   │   ├── HueSensor.h               # Hue Sensoren
│   │   └── HueSensor.cpp
│   │
│   ├── Hue.share.xml                  # ETS Allgemeine Einstellungen
│   └── Hue.templ.xml                  # ETS Kanal-Einstellungen
│
├── library.json
├── README.md
└── CHANGELOG.md
```

### 5.2 Klassen-Design

#### HueModule (OpenKNX Modul)
```cpp
class HueModule : public OpenKNX::Module {
public:
    void setup() override;
    void loop() override;
    void processInputKo(GroupObject& ko) override;
    bool enabled() override;
    
private:
    HueClient* _client;
    HueDiscovery* _discovery;
    std::vector<HueDeviceBase*> _devices;
    
    void setupDevices();
    void updateDeviceStates();
};
```

#### HueClient (API Kommunikation)
```cpp
class HueClient {
public:
    bool connect(const char* ipAddress, const char* appKey);
    bool authenticate();  // Button-Press Flow
    
    // API v2 Methoden
    JsonDocument* getResources();
    JsonDocument* getLights();
    bool setLightState(const char* id, bool on, uint8_t brightness, ...);
    
    // Event Stream
    void startEventStream();
    void handleEvents();
    
private:
    WiFiClient _client;
    String _appKey;
    String _bridgeIP;
    HueEventStream* _eventStream;
};
```

#### HueLight (Lampen-Implementierung)
```cpp
class HueLight : public HueDeviceBase {
public:
    void setup(uint8_t channelIndex) override;
    void updateFromHue(JsonObject& state) override;
    void updateFromKNX(GroupObject& ko) override;
    
private:
    bool _state;           // on/off
    uint8_t _brightness;   // 0-254
    uint16_t _colorTemp;   // Mired
    float _hue;            // 0-360
    float _saturation;     // 0-100
};
```

## 6. Integration in Firmware-Projekte

### 6.1 Beispiel: Integration in SmartHomeBridge

OFM-HueModule ist ein **eigenständiges OpenKNX Modul**, das optional in verschiedene Firmware-Projekte integriert werden kann.

**Optional in src/SmartHomeBridge.xml** erweitern:
```xml
<!-- Nach BRI-Definition -->
<op:define prefix="HUE" ModuleType="9"
  share="../lib/OFM-Hue/src/Hue.share.xml"
  template="../lib/OFM-Hue/src/Hue.templ.xml"
  NumChannels="50" 
  KoOffset="1000">
  <op:verify File="../lib/OFM-Hue/library.json" ModuleVersion="%HUE_VerifyVersion%" />
</op:define>
```

### 6.2 Optionale Integration in main.cpp

Firmware-Projekte können OFM-HueModule optional einbinden:

```cpp
#ifdef HUE_ModuleVersion  // Nur wenn Modul in XML definiert
#include "HueModule.h"
#endif

void setup() {
  // ... bestehende Module ...
  openknx.addModule(7, openknxSmartHomeBridgeModule);
  openknx.addModule(8, openknxFunctionBlocksModule);
#ifdef HUE_ModuleVersion
  openknx.addModule(9, openknxHueModule);  // Optional
#endif
  openknx.setup();
}
```

### 6.3 Modul-ID Übersicht (aktualisiert)
```
1  - Logic Module
2  - Network Module (Ethernet)
3  - WLAN Module
5  - USB Exchange (RP2040)
6  - File Transfer (RP2040)
7  - SmartHomeBridge
8  - Function Blocks
9  - Hue Integration (NEU)
10 - Common/Base
```

## 7. Entwicklungsschritte (Roadmap)

### Phase 1: Basis-Infrastruktur (Woche 1)
- [ ] OFM-HueModule Repository erstellen
- [ ] library.json mit Dependencies definieren
- [ ] HueModule Gerüst implementieren
- [ ] Integration in SmartHomeBridge.xml
- [ ] Erste Build-Tests

### Phase 2: API Client ✓
- [ ] HueDiscovery implementieren (mDNS/SSDP)
- [ ] HueAuth: Button-Press Flow
- [ ] HueClient: Basic API v2 Calls
- [ ] Verbindungstest mit echter Hue Bridge

### Phase 3: Event Handling ✓
- [ ] HueEventStream für SSE implementieren
- [ ] Event-Parser für Hue-Updates
- [ ] Status-Synchronisation Hue → KNX

### Phase 4: Geräte-Unterstützung ✓
- [ ] HueLight: Schalten
- [ ] HueLight: Dimmen
- [ ] HueLight: Farbtemperatur
- [ ] HueLight: RGB/HSV
- [ ] HueSensor: Basis-Implementierung

### Phase 5: ETS Integration ✓
- [ ] Hue.share.xml: Allgemeine Einstellungen
- [ ] Hue.templ.xml: Kanal-Konfiguration
- [ ] Parameter-IDs in knxprod.h generieren
- [ ] OpenKNXproducer Test

### Phase 6: Testing & Stabilisierung ✓
- [ ] Integration Tests mit echter Hardware
- [ ] Offline-Handling testen
- [ ] Wiederverbindung testen
- [ ] Performance-Optimierung (SSE vs Polling)

### Phase 7: Dokumentation ✓
- [ ] README.md
- [ ] Applikationsbeschreibung-Hue.md
- [ ] Code-Dokumentation
- [ ] Beispiel-Konfigurationen

## 8. Wichtige Ressourcen

### 8.1 Hue Developer Dokumentation
- **API v2**: https://developers.meethue.com/develop/hue-api-v2/
- **Getting Started**: https://developers.meethue.com/develop/get-started-2/
- **Authentication**: https://developers.meethue.com/develop/hue-api-v2/getting-started/
- **Event Stream**: https://developers.meethue.com/develop/hue-api-v2/core-concepts/#event-stream

### 8.2 Code-Referenzen
- **node-hue-api**: https://github.com/peter-murray/node-hue-api (Node.js Implementierung)
- **hue-control**: https://github.com/tigoe/hue-control (Arduino/Processing Beispiele)
- **phue**: https://github.com/studioimaginaire/phue (Python)

### 8.3 ESP32 Libraries
- **ArduinoJson**: JSON Parsing (bereits im Projekt)
- **WiFiClient/HTTPClient**: HTTPS Kommunikation (ESP32 Standard)
- **mDNS**: Bridge Discovery (ESP32 Standard)

## 9. Offene Fragen & Entscheidungen

### 9.1 IP-Konfiguration
**Frage**: Automatische Discovery vs. manuelle Eingabe?
**Empfehlung**: Beides unterstützen
- Standard: Automatische Discovery (mDNS)
- Fallback: Manuelle IP-Eingabe in ETS
- User-Story: "Bei statischen IPs will ich die IP direkt eingeben"

### 9.2 Offline-Strategie
**Frage**: Wie soll sich das System verhalten, wenn Hue Bridge offline ist?
**Optionen**:
1. Nur Status cachen, keine Steuerung
2. Lokale Zustandsverwaltung mit Sync bei Reconnect
3. Fehlermeldung auf KNX

**Empfehlung**: Option 1 + Verbindungs-Status-KO

### 9.3 Polling vs. Event Stream
**Frage**: SSE Event Stream oder periodisches Polling?
**Empfehlung**: Primär SSE, Polling als Fallback
- SSE: Echtzeit-Updates, geringer Overhead
- Polling: Fallback wenn SSE fehlschlägt (alle 5-10s)

### 9.4 Geräte-Erkennung
**Frage**: Automatische Erkennung oder manuelle Konfiguration?
**Empfehlung**: Hybrid-Ansatz
- ETS zeigt erkannte Geräte in Dropdown
- Geräte-ID manuell eingeben möglich
- Aktualisierung per "Scan"-Befehl über KNX oder Console

### 9.5 SSL/TLS
**Frage**: Hue API v2 nutzt HTTPS - wie mit self-signed Zertifikaten umgehen?
**Empfehlung**: Certificate Pinning optional
- Default: Self-signed akzeptieren (lokales Netzwerk)
- Option: Certificate Fingerprint validieren

## 10. Performance-Überlegungen

### 10.1 Speicher (ESP32)
- JSON Parsing benötigt RAM
- Event Stream hält Verbindung offen
- Mehrere Geräte = mehrere JSON Objects

**Maßnahmen**:
- Streaming JSON Parser (ArduinoJson)
- Selektives Laden nur benötigter Resources
- Limit auf 50 Geräte (konfigurierbar)

### 10.2 Netzwerk-Last
- SSE hält permanente Verbindung
- Polling-Intervall konfigurierbar
- Rate-Limiting beachten (Hue: max 10 req/s)

## 11. Sicherheitsaspekte

1. **App-Key Speicherung**: Flash-Encryption nutzen wenn verfügbar
2. **HTTPS**: SSL/TLS für API-Kommunikation
3. **Netzwerk-Isolation**: Nur lokales Netzwerk, keine Cloud
4. **Button-Press**: Physischer Zugriff auf Bridge erforderlich

## 12. Nächste Schritte

1. **Diskussion & Freigabe**: Dieses Konzept reviewen
2. **Repository Setup**: OFM-Hue als Git-Submodul
3. **Proof of Concept**: Minimale Implementierung (1 Lampe, Schalten)
4. **Iterative Entwicklung**: Schrittweise Features hinzufügen

---

**Autor**: OpenKNX Community  
**Datum**: 2026-02-03  
**Version**: 0.1 (Draft)  
**Status**: Konzeptphase - Feedback erwünscht
