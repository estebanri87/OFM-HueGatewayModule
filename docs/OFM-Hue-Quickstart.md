# OFM-HueModule Integration - Schnellübersicht

## Warum ein separates Modul?

| Aspekt | OFM-SmartHomeBridge | OFM-HueModule (neu) |
|--------|---------------------|---------------|
| **Richtung** | KNX → Smart Home | Hue → KNX |
| **Rolle** | Server (emuliert Hue) | Client (nutzt Hue API) |
| **Zweck** | KNX-Geräte in HomeKit/Alexa | Hue-Geräte in KNX |
| **Hardware** | Keine zusätzliche HW | Hue Bridge erforderlich |
| **Protokoll** | Hue-Emulation (fake) | Hue API v2 (echt) |

## Wie funktioniert die IP-Konfiguration?

### Option 1: Automatische Erkennung (empfohlen)
```
1. ESP32 startet
2. mDNS Scan nach "_hue._tcp.local"
3. Bridge gefunden → IP gespeichert
4. Verbindung herstellen
```

### Option 2: Manuelle Konfiguration
```
1. In ETS IP-Adresse eingeben: z.B. 192.168.1.50
2. Firmware liest IP aus Parameter
3. Direkte Verbindung zur Bridge
```

### Option 3: Hybrid (beste UX)
```
ETS Parameter:
[x] Automatische Erkennung
    Gefundene Bridge: 192.168.1.50
    
[ ] Manuelle IP-Adresse
    IP: [___].[___].[___].[___]
```

## Authentifizierung Flow (User-Perspektive)

```
┌─────────────────────────────────────────────────────┐
│ 1. ETS: Hue Integration aktivieren                  │
│    ├─ Automatische Discovery: [x]                   │
│    └─ IP-Adresse: [Auto erkannt: 192.168.1.50]     │
└─────────────────────────────────────────────────────┘
                      ↓
┌─────────────────────────────────────────────────────┐
│ 2. ESP32 Programmierung & Start                     │
│    ├─ Firmware versucht Hue Bridge zu erreichen     │
│    └─ Kein App-Key vorhanden → Pairing-Modus        │
└─────────────────────────────────────────────────────┘
                      ↓
┌─────────────────────────────────────────────────────┐
│ 3. LED am ESP32 blinkt schnell (Pairing-Modus)      │
│    Console zeigt: "Waiting for Bridge button..."    │
└─────────────────────────────────────────────────────┘
                      ↓
┌─────────────────────────────────────────────────────┐
│ 4. USER: Drückt Button auf Hue Bridge               │
│    (Innerhalb 30 Sekunden)                          │
└─────────────────────────────────────────────────────┘
                      ↓
┌─────────────────────────────────────────────────────┐
│ 5. App-Key generiert und im Flash gespeichert       │
│    LED wechselt zu langsamem Blinken (verbunden)    │
│    Console: "Connected to Hue Bridge"               │
└─────────────────────────────────────────────────────┘
                      ↓
┌─────────────────────────────────────────────────────┐
│ 6. Geräte-Scan startet automatisch                  │
│    ├─ Alle Hue-Geräte werden erkannt                │
│    └─ Liste in ETS verfügbar (per Config Transfer)  │
└─────────────────────────────────────────────────────┘
```

## ETS Konfiguration - Benutzeroberfläche

### Allgemeine Einstellungen (share.xml)
```
┌─ Philips Hue Integration ──────────────────────────┐
│                                                     │
│ ☑ Philips Hue Integration aktivieren               │
│                                                     │
│ Bridge-Verbindung:                                  │
│   ○ Automatisch (mDNS)                              │
│   ● Manuelle IP-Adresse                             │
│   IP: [192].[168].[  1].[50 ]                       │
│                                                     │
│ Status:                                             │
│   Verbunden: ✓                                      │
│   App-Key: abc123...xyz (generiert)                 │
│   Erkannte Geräte: 12                               │
│                                                     │
│ Erweiterte Einstellungen:                           │
│   Update-Intervall: [5] Sekunden (Fallback)        │
│   Timeout: [30] Sekunden                            │
│   Max. Kanäle: [50]                                 │
│                                                     │
└─────────────────────────────────────────────────────┘
```

### Kanal-Konfiguration (templ.xml)
```
┌─ Kanal 1: Wohnzimmer Deckenlampe ──────────────────┐
│                                                     │
│ ☑ Kanal aktiv                                       │
│                                                     │
│ Hue-Gerät:                                          │
│   Typ: [Dimmbare Farblampe ▼]                       │
│   Gerät: [Hue color lamp 1 ▼] (ID: light-001)      │
│   Name: [Wohnzimmer Deckenlampe_____________]       │
│                                                     │
│ Kommunikationsobjekte:                              │
│   ☑ Schalten            GA: [1/2/1]                 │
│   ☑ Helligkeit          GA: [1/2/2]                 │
│   ☑ Farbtemperatur      GA: [1/2/3]                 │
│   ☑ RGB Farbe           GA: [1/2/4]                 │
│   ☑ Status-Rückmeldung  GA: [1/2/10]                │
│                                                     │
│ Optionen:                                           │
│   ☑ Bei Neustart Status einlesen                    │
│   ☑ Status bei Änderung senden                      │
│   ☐ Übergangszeit verwenden ([2] s)                 │
│                                                     │
└─────────────────────────────────────────────────────┘
```

## KNX Kommunikationsobjekte (Beispiel: Farblampe)

| KO | Name | DPT | Richtung | Beschreibung |
|----|------|-----|----------|--------------|
| 1 | Schalten | 1.001 | R/W | Ein/Aus Steuerung |
| 2 | Helligkeit | 5.001 | R/W | Helligkeit 0-100% |
| 3 | Farbtemperatur | 7.600 | R/W | Kelvin (2000-6500K) |
| 4 | RGB Farbe | 232.600 | R/W | RGB Wert |
| 5 | Status Schalten | 1.001 | R | Rückmeldung Ein/Aus |
| 6 | Status Helligkeit | 5.001 | R | Rückmeldung Helligkeit |
| 7 | Status Erreichbar | 1.001 | R | Lampe online? |
| 8 | Fehler | 1.005 | R | Verbindungsfehler |

## Code-Beispiel: Minimale Implementierung

### 1. Hue Bridge Discovery
```cpp
// HueDiscovery.cpp
bool HueDiscovery::findBridge(String& ipAddress) {
    if (!MDNS.begin("openhab")) return false;
    
    int n = MDNS.queryService("hue", "tcp");
    if (n > 0) {
        ipAddress = MDNS.IP(0).toString();
        logInfoP("Found Hue Bridge at %s", ipAddress.c_str());
        return true;
    }
    return false;
}
```

### 2. Authentifizierung
```cpp
// HueAuth.cpp
bool HueAuth::requestAppKey(const char* bridgeIP) {
    HTTPClient http;
    http.begin(String("http://") + bridgeIP + "/api");
    
    String body = "{\"devicetype\":\"openknx#esp32\"}";
    int httpCode = http.POST(body);
    
    if (httpCode == 200) {
        JsonDocument doc;
        deserializeJson(doc, http.getString());
        
        if (doc[0].containsKey("error")) {
            // Error 101: Button not pressed
            logInfoP("Please press button on Hue Bridge");
            return false;
        }
        
        if (doc[0].containsKey("success")) {
            _appKey = doc[0]["success"]["username"].as<String>();
            saveAppKey(); // In Flash speichern
            logInfoP("App-Key received: %s", _appKey.c_str());
            return true;
        }
    }
    
    return false;
}
```

### 3. Lampe schalten
```cpp
// HueLight.cpp
void HueLight::processInputKo(GroupObject& ko) {
    if (ko.asap() == _koSwitch) {
        bool state = ko.value(DPT_Switch);
        setLightState(state);
    }
}

void HueLight::setLightState(bool on) {
    HTTPClient http;
    String url = String("https://") + _bridgeIP + 
                 "/clip/v2/resource/light/" + _lightId;
    
    http.begin(url);
    http.addHeader("hue-application-key", _appKey);
    
    JsonDocument doc;
    doc["on"]["on"] = on;
    
    String body;
    serializeJson(doc, body);
    
    int httpCode = http.PUT(body);
    logDebugP("Set light %s: %s (HTTP %d)", 
              _lightId.c_str(), on ? "ON" : "OFF", httpCode);
}
```

### 4. Event Stream (Status-Updates von Hue)
```cpp
// HueEventStream.cpp
void HueEventStream::handleEvents() {
    if (!_client.connected()) {
        reconnect();
        return;
    }
    
    if (_client.available()) {
        String line = _client.readStringUntil('\n');
        
        if (line.startsWith("data: ")) {
            JsonDocument doc;
            deserializeJson(doc, line.substring(6));
            
            for (JsonObject event : doc[0]["data"].as<JsonArray>()) {
                String type = event["type"];
                String id = event["id"];
                
                if (type == "light") {
                    // Update für Lampe empfangen
                    bool on = event["on"]["on"];
                    uint8_t brightness = event["dimming"]["brightness"];
                    
                    // Entsprechende HueLight-Instanz informieren
                    HueLight* light = findLightById(id);
                    if (light) {
                        light->updateFromHue(on, brightness);
                    }
                }
            }
        }
    }
}
```

## Nächste Schritte - Implementierung

### Phase 1: Proof of Concept (Woche 1)
1. OFM-HueModule Repository erstellen
2. Minimales Modul mit 1 Lampe (Schalten)
3. Automatische Discovery testen
4. Button-Press Authentication

**Ziel**: Eine Hue-Lampe über KNX schalten

### Phase 2: Basis-Features (2-3 Wochen)
1. Dimmen implementieren
2. Event Stream für Status-Updates
3. ETS XML-Dateien erstellen
4. Multi-Lampen Support

**Ziel**: 5-10 Lampen steuerbar mit Status-Rückmeldung

### Phase 3: Erweiterte Features (2-3 Wochen)
1. Farbtemperatur
2. RGB/HSV Farben
3. Sensoren (Motion, Temperature)
4. Gruppen/Szenen

**Ziel**: Vollständige Hue-Funktionalität in KNX

### Phase 4: Polish & Testing (1-2 Wochen)
1. Offline-Handling
2. Fehlerbehandlung
3. Performance-Optimierung
4. Dokumentation

**Ziel**: Production-Ready Release

## Häufige Fragen (FAQ)

### Q: Funktioniert das ohne Hue Bridge?
**A:** Nein, die Hue Bridge ist zwingend erforderlich. Das Modul kommuniziert mit der Bridge, nicht direkt mit den Lampen.

### Q: Kann ich Hue und HomeKit gleichzeitig nutzen?
**A:** Ja! OFM-HueModule und OFM-SmartHomeBridge können parallel laufen.
- OFM-SmartHomeBridge: KNX → HomeKit/Alexa
- OFM-HueModule: Hue → KNX

### Q: Was passiert, wenn die Hue Bridge offline ist?
**A:** 
- Letzte bekannte Status werden angezeigt
- KNX-Befehle werden nicht weitergeleitet
- Automatische Wiederverbindung bei Verfügbarkeit
- Status-KO "Verbunden" zeigt Offline

### Q: Wie viele Hue-Geräte werden unterstützt?
**A:** Standard 50 Geräte, konfigurierbar. ESP32 Speicher ist limitierend.

### Q: Brauche ich eine statische IP für die Hue Bridge?
**A:** Empfohlen, aber nicht zwingend. mDNS findet die Bridge auch bei DHCP.

---

**Feedback & Diskussion erwünscht!**

Erstellt: 2026-02-03
