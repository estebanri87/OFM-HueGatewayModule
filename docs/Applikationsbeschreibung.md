# Applikationsbeschreibung Philips Hue Gateway Modul

Das OFM-HueGatewayModule verbindet Philips Hue Leuchten mit dem KNX-Bus.
Die Kommunikation erfolgt über die Hue Bridge (Hue API v2).

# Applikationsprogramm

<!-- DOC -->
## Allgemein

(c) OpenKNX, Steffen Rittmeier 2026

Die vollständige Projektdokumentation ist unter https://github.com/OpenKNX/OFM-HueGatewayModule verfügbar.

Das Modul übernimmt die Kopplung:

`KNX Telegramm` → `OFM-HueGatewayModule` → `Hue Bridge API` → `Hue-Leuchte`

und für Rückmeldungen:

`Hue-Leuchte/Bridge` → `OFM-HueGatewayModule` → `KNX Status-KO`

**Wichtig:**
- Das Modul verwendet Hue API v2.
- Parameter und KOs müssen in ETS konsistent projektiert werden.
- Logikfunktionen (Szenenlogik, Zentralfunktionen) gehören in dedizierte Logikmodule.

<!-- DOC -->
## Bridge-Konfiguration

<!-- DOC -->
### Bridge Erkennung

Legt fest, wie die Hue Bridge gefunden wird:
- **Automatisch (mDNS)**: empfohlener Standard.
- **Manuelle IP-Adresse**: für VLAN-/mDNS-Sonderfälle oder feste IP.

<!-- DOC -->
### Bridge IP-Adresse

Nur sichtbar bei **Manuelle IP-Adresse**.

Beispiel: `192.168.1.100`

<!-- DOC -->
### Erstinbetriebnahme und Authentifizierung

Die Inbetriebnahme kann auf zwei Arten erfolgen:

#### Variante 1: Pairing per ETS (Pairing Trigger KO)

1. ETS-Download ausführen.
2. Warten, bis das Modul die Bridge gefunden hat bzw. zur Bridge verbunden ist.
3. Auf das KO **Pairing Trigger** eine `1` senden.
4. Innerhalb des Pairing-Fensters den Link-Button an der Hue Bridge drücken.
5. Das Modul speichert den App-Key persistent.

#### Variante 2: Pairing per Webinterface

1. Webinterface öffnen: `http://<IP-des-OpenKNX-Geräts>/openknx/hue`
2. **Kopplung starten** aufrufen (`/openknx/hue/pair`).
3. Innerhalb des Pairing-Fensters den Link-Button an der Hue Bridge drücken.
4. Auf der Statusseite den Verbindungsaufbau prüfen (`/openknx/hue/status`).
5. Das Modul speichert den App-Key persistent.

Hinweise zu Zuständen und Fortschritt finden Sie unter [LED-Signale](#led-signale).

<!-- DOC -->
### Nächste Schritte nach dem Pairing

Nach erfolgreicher Kopplung (LED grün dauerhaft) erfolgt die eigentliche Gerätezuordnung:

1. **Hue-Geräte laden** öffnen: `http://<IP-des-OpenKNX-Geräts>/openknx/hue/scan`
2. In der Liste die gewünschten Ziele (Licht/Raum/Zone) inkl. ID erfassen.
3. In ETS je Kanal **Zieltyp** setzen und **Hue Ziel (Light-/Room-/Zone-ID oder Name)** eintragen.
4. Pro Kanal Lampentyp, Synchronisationsrichtung und Polling prüfen.
5. Download ausführen und Funktion testen (Schalten, ggf. Helligkeit/Farbtemperatur/RGB).

Empfehlung: Erst mit 1-2 Kanälen testen, danach auf alle Kanäle ausrollen.

<!-- DOC -->
### LED-Signale

Die Status-LED zeigt den aktuellen Zustand während der Inbetriebnahme:

- **Blau schnell blinkend (~0,2 s):** Bridge-Suche aktiv (typisch bei mDNS).
- **Blau langsam blinkend (~1 s):** Pairing-Fenster aktiv, Link-Button jetzt drücken.
- **Cyan schnell blinkend (~0,2 s):** Authentifizierung läuft.
- **Grün dauerhaft:** Verbindung zur Bridge steht, Modul betriebsbereit.
- **Rot blinkend (~0,5 s):** Verbindung zur Bridge verloren.
- **Rot blinkend (~1,5 s):** Bridge nicht erreichbar / nicht konfiguriert.

<!-- DOC -->
### Zugriff auf das Webinterface

Für Inbetriebnahme und Diagnose steht die Hue-Seite im OpenKNX WebUI bereit.

**Aufruf:**
- `http://<IP-des-OpenKNX-Geräts>/openknx/hue`

**Beispiel:**
- `http://192.168.1.100/openknx/hue`

Von dort aus sind die Seiten für Pairing, Status und Gerätescan direkt erreichbar.

**So finden Sie die Geräte-IP:**
- DHCP-/Client-Liste im Router
- ETS Diagnose / Netzwerkansicht
- Serielle Konsole (Ausgabe beim Start)

<!-- DOC -->
### Authentication zurücksetzen

Löscht den gespeicherten App-Key.
Anschließend ist eine erneute Authentifizierung erforderlich.

<!-- DOC -->
### Status Verbindung

Blendet das KO **Bridge Verbindungsstatus** ein.
- `0`: keine Verbindung
- `1`: verbunden

<!-- DOC -->
### Anzahl aktiver Kanäle

Legt fest, wie viele Hue-Kanäle (0-20) in ETS sichtbar/aktiv sind.
Empfehlung: nur tatsächlich benötigte Kanäle aktivieren.

<!-- DOC HelpContext="Kanal" -->
## Kanal 1-n (Hue Ziele)

Jeder Kanal steuert genau ein Hue-Ziel.

Mögliche Zieltypen:
- **Licht**
- **Raum**
- **Zone**

Die Zielzuordnung erfolgt primär über **Hue Ziel (Light-/Room-/Zone-ID oder Name)**.

<!-- DOC -->
### Kanalbezeichnung

Dient zur besseren Lesbarkeit in ETS und wird in KO-Bezeichnungen übernommen.

Beispiele:
- Wohnzimmer
- Küche Decke
- Flur Spots

<!-- DOC -->
### Zieltyp

Legt fest, ob der Kanal ein **Licht**, einen **Raum** oder eine **Zone** steuert.

Hinweis:
- Bei **Raum/Zone** wird intern auf `grouped_light` aufgelöst.
- Der Kanal bleibt damit auch bei Änderungen innerhalb des Raums/der Zone nutzbar.

<!-- DOC -->
### Hue Ziel (Light-/Room-/Zone-ID oder Name)

Primäres Zielfeld für alle Zieltypen.

Verwenden Sie je nach Zieltyp:
- Light-ID oder Light-Name
- Room-ID oder Room-Name
- Zone-ID oder Zone-Name

Ermittlung über:
- Webinterface: `http://<IP-des-OpenKNX-Geräts>/openknx/hue/scan`
- Konsole: `hue scan`

<!-- DOC -->
### Hue Lampen-ID (UUID)

Legacy-/Fallback-Feld für bestehende Projektierungen.

Für neue Projektierungen bitte **Hue Ziel (Light-/Room-/Zone-ID oder Name)** verwenden.

Gilt nur für Zieltyp **Licht**.

Ermittlung über:
- Webinterface: `http://<IP-des-OpenKNX-Geräts>/openknx/hue/scan`
- Konsole: `hue scan`
- Hue API v2 (`/clip/v2/resource/light`)

Wichtig: Die UUID muss je Kanal exakt zur gewünschten Leuchte passen.

<!-- DOC -->
### Lampentyp

Der Lampentyp bestimmt, welche KOs sichtbar/aktiv sind:
- **Nur schalten**: Schalten + Status Schalten
- **Dimmbar**: zusätzlich Helligkeit/Dimmen + Status Helligkeit
- **Farbtemperatur**: zusätzlich Farbtemperatur + Status Farbtemperatur
- **Farbe (RGB)**: zusätzlich RGB + Status RGB

<!-- DOC -->
### Kanal deaktivieren

Deaktiviert den Kanal ohne Verlust der Parametrierung.

<!-- DOC -->
### Synchronisationsrichtung

- **Keine Synchronisation**
- **Nur KNX zu Hue**
- **Nur Hue zu KNX**
- **Bidirektional** (Standardempfehlung)

<!-- DOC -->
### Polling-Intervall

Status-Abfrageintervall in Sekunden.
Empfehlung: `5..30 s` je nach Kanalzahl und Netzlast.

<!-- DOC -->
### Minimale Helligkeit

Mindestwert für Helligkeit >0, um Flackern bei niedrigen Dimmwerten zu vermeiden.

Beispiel: `5 %` für kritische Leuchten.

<!-- DOC -->
### HCL Manager Zuordnung

Ordnet den Kanal einem HCL-Manager (1..4) zu.
Bei `Kein HCL` arbeitet der Kanal ohne HCL-Übernahme.

<!-- DOC -->
## HCL Manager

<!-- DOC -->
### Human Centric Lighting (HCL)

Aktiviert zeitabhängige Sollwerte für Helligkeit und Farbtemperatur.
Bis zu 4 HCL Manager können parallel definiert werden.

<!-- DOC -->
### HCL Manager Auswahl

Legt die Anzahl sichtbarer HCL-Managerseiten (1..4) fest.

<!-- DOC -->
### Einstellungen

- **Aktualisierungsintervall (Sekunden)**
- **Überblendzeit (Sekunden)**

Sollwerte werden aus der HCL-Kurve berechnet und bei Wertänderung als Status-KO übertragen.

<!-- DOC -->
### HCL Sperre (global)

Sperrt HCL-Ausgabe für alle Manager.

Optionen:
- **Rückfall aktivieren**
- **Rückfallzeit nach HCL-Sperre** (inkl. Tageswechsel)

KOs:
- `HCL Sperre (global)` (Eingang)
- `Status HCL Sperre` (Ausgang)

<!-- DOC -->
### Status-KOs je HCL Manager

Aktiviert pro Manager die Ausgabe:
- `Status Helligkeit Soll`
- `Status Farbtemperatur Soll`

<!-- DOC -->
### HCL Manager 1..4

Jeder Manager besitzt identischen Aufbau:

#### Bezeichnung
Freie ETS-Bezeichnung des Managers.

#### HCL Sperre (spezifisch)
Sperrt nur den jeweiligen Manager.

Optionen je Manager:
- **Rückfall aktivieren**
- **Rückfallzeit nach HCL-Sperre**

KOs je Manager:
- `HCL Sperre Mx` (Eingang)
- `Status HCL Sperre Mx` (Ausgang)

#### Erweiterte Kurve
Kurventyp:
- **FixedTime**
- **SunPosition**
- **Manual Kelvin**

#### Stützpunkte
Bis zu 10 Stützpunkte je Manager.

Hinweise:
- Mindestens 2 gültige Zeit-Stützpunkte erforderlich.
- Bei **Manual Kelvin** werden Zeit + Helligkeit verwendet; Kelvin kommt aus dem Manual-Kelvin-Parameter.

Beispiel:
- SP1 `06:00 / 3000K / 30%`
- SP2 `12:00 / 5000K / 90%`
- SP3 `20:00 / 2700K / 35%`

<!-- DOC -->
## Kommunikationsobjekte

### Globale Kommunikationsobjekte

<!-- DOC -->
#### Bridge Verbindungsstatus

Optionales 1-Bit Statusobjekt (0=offline, 1=online).

<!-- DOC -->
#### Pairing Trigger

1-Bit Triggerobjekt für ETS-gestützte Pairing-Auslösung.

<!-- DOC -->
#### HCL Sperre (global) / Status HCL Sperre

Globale HCL-Sperre inkl. Statusrückmeldung.

<!-- DOC -->
#### HCL Sperre Manager 1..4 / Status

Manager-spezifische Sperrobjekte inkl. Statusrückmeldung.
Sichtbarkeit abhängig von konfigurierte Manageranzahl.

<!-- DOC -->
#### HCL Status Helligkeit Soll / Farbtemperatur Soll

Je Manager zwei Sollwert-KOs; Sichtbarkeit abhängig von Option **Status-KOs je HCL Manager**.

### Pro-Kanal Kommunikationsobjekte

<!-- DOC -->
#### Schalten

1-Bit Eingang zum Ein-/Ausschalten.

<!-- DOC -->
#### Helligkeit

1-Byte Eingang (0..100%).

<!-- DOC -->
#### Dimmen

Relatives Dimmen (DPT 3.007).

<!-- DOC -->
#### Status Schalten

1-Bit Statusausgang.

<!-- DOC -->
#### Status Helligkeit

1-Byte Statusausgang.

<!-- DOC -->
#### Farbtemperatur / Status Farbtemperatur

2-Byte Kelvin-Eingang/-Ausgang (2000..6500K).

<!-- DOC -->
#### Farbe RGB / Status RGB

3-Byte RGB-Eingang/-Ausgang.

<!-- DOC -->
## Projektierungsbeispiele

### Beispiel 1: Schalten ohne Rückmeldung
- Lampentyp: Nur schalten
- Sync: Nur KNX zu Hue
- Polling: 0

### Beispiel 2: Standard-Wohnraum
- Lampentyp: Dimmbar
- Sync: Bidirektional
- Polling: 10 s

### Beispiel 3: HCL im Arbeitszimmer
- Lampentyp: Farbtemperatur
- HCL Manager: 1
- HCL Intervall: 60 s
- HCL Sperre M1 via KO auf GA für Präsenz/Abwesenheit

<!-- DOC -->
## Häufige Fehler und Lösungen

### Bridge wird nicht gefunden
- mDNS/VLAN prüfen oder auf manuelle IP wechseln.

### Authentifizierung schlägt fehl
- Pairing-Fenster abgelaufen → neu triggern und Link-Button erneut drücken.

### Hue-Ziel reagiert nicht
- Zieltyp und **Hue Ziel (Light-/Room-/Zone-ID oder Name)** prüfen.
- Bei Legacy-Projektierung zusätzlich **Hue Lampen-ID (UUID)** prüfen.
- Kanal deaktiviert?
- Sync-Richtung passend?

### Status fehlt
- Polling > 0?
- Sync auf Hue→KNX oder Bidirektional?
- Status-KO mit GA verbunden?

### HCL wirkt nicht
- HCL global aktiviert?
- Manager zugewiesen?
- Min. 2 gültige Stützpunkte?
- Globale/spezifische HCL-Sperre aktiv?

<!-- DOC -->
## Inbetriebnahme-Checkliste

- Bridge gefunden und authentifiziert
- Zieltyp und Hue Ziel pro Kanal geprüft
- Lampentyp passend zur realen Leuchte
- Sync/Polling passend zur Anwendung
- Benötigte Status-KOs mit GAs verbunden
- HCL-Funktion inkl. Sperren (global/spezifisch) getestet

<!-- DOC -->
## Lizenz und Haftung

Open-Source-Modul im OpenKNX-Umfeld.
Keine Gewährleistung; Nutzung in eigener Verantwortung.
Philips Hue ist ein Warenzeichen von Signify N.V.
