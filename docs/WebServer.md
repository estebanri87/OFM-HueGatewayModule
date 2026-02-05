# HueModule WebServer

## Übersicht

Der HueModule enthält einen **integrierten HTTP-Webserver**, der eine komfortable Inbetriebnahme über den Browser ermöglicht.

## Zugriff

```
http://<device-ip>/
http://<device-ip>:<port>/         (bei abweichendem Port)
```

**Standard-Port**: 80 (konfigurierbar in ETS)

## Verfügbare Endpunkte

### 🏠 Homepage: `/`
**Funktion**: Übersichtsseite mit Navigation

**Beispiel**:
```
http://192.168.1.100/
```

**Anzeige**:
- Links zu allen Funktionen
- Device IP-Adresse
- Modul-Version

---

### 🔍 Bridge Scan: `/hue/scan`
**Funktion**: Scannt die Hue Bridge und zeigt alle gefundenen Lichter

**Beispiel**:
```
http://192.168.1.100/hue/scan
```

**Ausgabe**:
```
🔍 Hue Bridge Scan Results
Found 3 lights:

Light 1: Wohnzimmer Decke
ID: abc123-456-789-def-ghi-jkl
Status: ON | Brightness: 200/254

Light 2: Küche LED Strip
ID: def456-789-012-345-678-901
Status: OFF | Brightness: 0/254

Light 3: Schlafzimmer
ID: ghi789-012-345-678-901-234
Status: ON | Brightness: 150/254
```

**Workflow**:
1. Browser öffnen
2. URL aufrufen
3. Light IDs markieren & kopieren
4. In ETS Kanal-Parameter einfügen

---

### 📊 Status: `/hue/status`
**Funktion**: Zeigt Modul-Status und System-Info

**Beispiel**:
```
http://192.168.1.100/hue/status
```

**Anzeige**:
- Initialisierungsstatus
- Device IP-Adresse
- Anzahl aktiver Lichter
- WiFi-Signalstärke (RSSI)

---

## ETS Konfiguration

### Port-Einstellung

**Parameter**: `HTTP Server Port`  
**Standard**: 80  
**Bereich**: 1-65535

**Empfohlen**:
- **Port 80**: Standard HTTP (keine Port-Angabe in URL nötig)
- **Port 8080**: Alternative (z.B. bei Konflikten)

**Beispiel**:
```
Port 80:    http://192.168.1.100/hue/scan
Port 8080:  http://192.168.1.100:8080/hue/scan
```

### Sicherheit

⚠️ **Kein Passwortschutz**: Der Webserver ist offen zugänglich im lokalen Netzwerk.

**Empfehlung**:
- Nur für Inbetriebnahme verwenden
- Nach Konfiguration: Server deaktivieren (Port = 0)
- Oder Firewall-Regel erstellen

---

## Verwendung (Schritt-für-Schritt)

### 1. Device IP ermitteln

**Variante A: ETS**
- ETS → Gerät → Diagnose → IP-Adresse ablesen

**Variante B: Router**
- Router-Webinterface → DHCP-Clients → "OpenKNX-Bridge" suchen

**Variante C: Serial Console**
```
> network
Device IP: 192.168.1.100
```

### 2. Browser öffnen

```
http://192.168.1.100/
```

### 3. Bridge scannen

- Klick auf **"🔍 Bridge Scannen"**
- Oder direkt: `http://192.168.1.100/hue/scan`

### 4. Light IDs kopieren

- Light ID markieren (z.B. `abc123-456-789-def-ghi-jkl`)
- **Rechtsklick** → Kopieren (oder Strg+C)

### 5. ETS Parameter ausfüllen

- ETS öffnen
- **HueModule** → **Kanal 1** → **Light ID**
- Einfügen (Strg+V)
- Wiederholen für alle Kanäle

### 6. Gerät programmieren

- ETS → **Programmieren**
- Fertig! ✅

---

## Troubleshooting

### Browser zeigt "Seite nicht erreichbar"
**Ursachen**:
- Device nicht im Netzwerk
- Falsche IP-Adresse
- WiFi nicht verbunden
- Port falsch konfiguriert

**Lösung**:
1. IP-Adresse im Router prüfen
2. Ping-Test: `ping 192.168.1.100`
3. Port in ETS prüfen (Standard: 80)
4. Serial Console checken: WiFi-Status

### "Module not initialized"
**Ursache**: Bridge-Authentifizierung fehlt

**Lösung**:
1. Button auf Hue Bridge drücken
2. Gerät neustarten
3. Serial Console: "[HueModule] Authenticated successfully"

### "No lights found"
**Ursachen**:
- Bridge nicht erreichbar
- Bridge-IP falsch konfiguriert
- Keine Hue-Geräte gekoppelt

**Lösung**:
1. Bridge-IP in ETS prüfen
2. Hue App öffnen: Geräte sichtbar?
3. Bridge Ping: `ping <bridge-ip>`

### Seite lädt sehr langsam
**Ursache**: Schwaches WiFi-Signal

**Lösung**:
- WiFi-Signalstärke prüfen: `/hue/status`
- Sollte > -70 dBm sein
- Gerät näher an Access Point positionieren

---

## Vergleich: WebServer vs. Console

| Kriterium | WebServer | Serial Console |
|-----------|-----------|----------------|
| **Verbindung** | ✅ WiFi (kabellos) | ❌ USB-Kabel |
| **Zugriff** | ✅ Browser (jedes Gerät) | ❌ Serial Monitor |
| **Benutzerfreundlichkeit** | ✅✅✅ Sehr einfach | ⚠️ Technisch |
| **Copy-Paste** | ✅ Direkt möglich | ✅ Möglich |
| **Formatierung** | ✅ HTML-Styling | ⚠️ Plain Text |
| **Remote-Zugriff** | ✅ Im LAN | ❌ Nur lokal |

**Fazit**: WebServer ist für Inbetriebnahme die **beste Lösung**! 🚀

---

## API für Automatisierung (Optional)

### JSON-Format (zukünftig)

```bash
# Scan (JSON-Response)
curl http://192.168.1.100/hue/scan?format=json

# Response:
{
  "count": 3,
  "lights": [
    {
      "id": "abc123-456-789-def-ghi-jkl",
      "name": "Wohnzimmer Decke",
      "on": true,
      "brightness": 200
    },
    ...
  ]
}
```

*Hinweis: JSON-API ist noch nicht implementiert, kann bei Bedarf ergänzt werden.*

---

## Technische Details

### Implementierung
- **Library**: ESP32 WebServer (Arduino Framework)
- **Port**: Konfigurierbar via ETS (Parameter `HUE_WebServerPort`)
- **Threads**: Single-threaded (handleClient() in loop())
- **Encoding**: UTF-8

### Speicherverbrauch
- **RAM**: ~10 KB (WebServer-Instanz + HTML-Buffer)
- **Flash**: ~50 KB (WebServer-Library)

### Performance
- **Gleichzeitige Requests**: 1 (ESP32-Limit)
- **Response-Zeit**: < 500ms (bei 5 Lights)
- **HTML-Größe**: ~3-5 KB (abhängig von Anzahl Lights)

### Routes
```cpp
GET /              → handleRoot()
GET /hue/scan      → handleScan()
GET /hue/status    → handleStatus()
*                  → handleNotFound() (404)
```

---

**Version**: 0.1.0  
**Datum**: 2026-02-03  
**Autor**: OpenKNX HueModule
