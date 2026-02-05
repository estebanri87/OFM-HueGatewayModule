# ETS Bridge Scan Feature

## Übersicht

Ein **Knopfdruck in ETS** löst via Kommunikationsobjekt einen Bridge-Scan aus. Die Ergebnisse (Light IDs und Namen) werden in der **Serial Console** angezeigt und können direkt kopiert werden.

## Voraussetzung

✅ **Serial Monitor muss geöffnet sein** (USB-Verbindung zum Gerät)

**Empfohlene Tools:**
- PlatformIO Monitor (integriert in VS Code)
- Putty / TeraTerm (Windows)
- screen / minicom (Linux/Mac)

**Baudrate**: 115200

## Kommunikationsobjekt

### KO 300: Bridge scannen (Trigger)
- **Typ**: DPT 1.017 (Trigger)  
- **Funktion**: Write
- **Verwendung**: Senden von `1` löst den Scan aus

⚠️ **Wichtig**: Das Feature muss in ETS aktiviert werden:
```
Parameter "Bridge-Scan via KO aktivieren" = Ja
```

## Workflow (Inbetriebnahme)

### 1. Vorbereitung
```powershell
# Serial Console öffnen (PlatformIO)
pio device monitor -b 115200

# Oder mit Putty auf COM-Port (z.B. COM3, 115200 Baud)
```

### 2. Parameter in ETS aktivieren
In den Modul-Parametern:
- **Inbetriebnahme** > **Bridge-Scan via KO aktivieren** = **Ja**
- Gruppenadressen zuweisen: z.B. **0/7/100** für KO 300

### 3. ETS Button/Objekt erstellen
**Variante A: Monitor-Tool**
- Erstellen Sie ein **Button-Objekt** mit GA 0/7/100
- Funktion: **Wert schreiben** = `1`

**Variante B: Bausteinfunktion**
- Schalter mit GA 0/7/100 verbinden
- Kurzer Tastendruck genügt

### 4. Scan auslösen
1. Serial Console ist geöffnet ✅
2. In ETS: **Button klicken** oder **Schalter betätigen**
3. Ausgabe erscheint sofort in Console:

```
========================================
   HUE BRIDGE SCAN (triggered via KO)
========================================
Found 3 Lights:
---------------------------------
 1: Wohnzimmer Decke      abc123-456-789-def-ghi-jkl
 2: Küche LED Strip       def456-789-012-345-678-901
 3: Schlafzimmer          ghi789-012-345-678-901-234
    Status: ON , Brightness: 200/254
---------------------------------
Copy Light IDs above and paste into ETS parameters.
========================================
```

### 5. Light IDs übertragen
1. **Markieren** der Light ID in Console (z.B. `abc123-456-789-def-ghi-jkl`)
2. **Kopieren** (Strg+C)
3. In ETS öffnen: **HueModule** > **Kanal 1** > **Light ID**
4. **Einfügen** (Strg+V)
5. Wiederholen für alle Kanäle
6. **Gerät programmieren**

## Console-Ausgabe Format

```
========================================
   HUE BRIDGE SCAN (triggered via KO)
========================================
Found <Anzahl> Lights:
---------------------------------
<Nr>: <Name (25 Zeichen)>      <Light UUID (36 Zeichen)>
    Status: <ON/OFF>, Brightness: <0-254>/254
---------------------------------
Copy Light IDs above and paste into ETS parameters.
========================================
```

**Beispiel:**
```
 1: Wohnzimmer Decke      abc123-456-789-def-ghi-jkl
    Status: ON , Brightness: 200/254
```

## Alternative: Manueller Console-Befehl

Wenn USB bereits angeschlossen ist, kann der Scan auch direkt eingegeben werden:

```
> hue scan

(gleiche Ausgabe wie oben)
```

## Verwendung in Visualisierung (Optional)

Der ETS-Button kann auch aus einer Visualisierung getriggert werden:

### Gira HomeServer
```yaml
Taster:
  Funktion: Schreiben
  Gruppenadresse: 0/7/100
  Wert: 1
  
Hinweis-Text:
  "Bitte Serial Console prüfen für Scan-Ergebnisse"
```

### Node-RED
```javascript
// Trigger Scan
msg.payload = true;
msg.destination = "0/7/100";
return msg;

// User muss Serial Console beobachten
```

## Troubleshooting

**Keine Console-Ausgabe:**
- Serial Monitor geöffnet? (Baudrate 115200)
- USB-Kabel korrekt angeschlossen?
- Richtiger COM-Port ausgewählt?

**"ERROR: Module not initialized":**
- Bridge-Authentifizierung fehlt
- Button auf Hue Bridge drücken → Gerät neustarten
- Netzwerkverbindung prüfen (WiFi/Ethernet)

**"ERROR: No lights found":**
- Bridge-IP korrekt in ETS konfiguriert?
- Bridge erreichbar? (Ping-Test)
- Hue-Geräte eingeschaltet?

## Sicherheit

⚠️ **Empfehlung für Produktivbetrieb**:
```
Nach Inbetriebnahme Parameter deaktivieren:
"Bridge-Scan via KO aktivieren" = Nein
```

Grund: Verhindert unbeabsichtigte Scans im laufenden Betrieb.

## Technische Details

### KO-Mapping
```
KO 300:      Scan Trigger ──┐  Module-KO
KO 301-303:  Kanal 1       ─┤
KO 304-306:  Kanal 2       ─┤  Channel-KOs
...                         ├  (20 Kanäle)
KO 358-360:  Kanal 20      ─┘
```

### Implementierung
- **Trigger**: `HueModule::processInputKo()` bei KO 300
- **Scan**: `HueModule::performBridgeScan()`
- **Ausgabe**: Serial.println() → USB Serial Port

### API-Aufruf
```cpp
GET https://{bridge-ip}/clip/v2/resource/light
Authorization: hue-application-key {app-key}
```

### Vergleich: KO vs Console-Befehl

| Methode | Vorteile | Nachteile |
|---------|----------|-----------|
| **ETS Button → Console** | ✅ Remote auslösbar<br>✅ Übersichtlich<br>✅ Copy-Paste | ❌ USB nötig |
| **Console-Befehl** | ✅ Direkt<br>✅ Schnell | ❌ USB nötig |

**Fazit**: Beide Methoden nutzen dieselbe Ausgabe. ETS-Button ist praktisch, wenn der Integrator nicht am Terminal arbeiten möchte.

---

**Version**: 0.1.0  
**Datum**: 2026-02-03
