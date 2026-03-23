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

Empfehlungen für robuste Inbetriebnahme:
- Primär die jeweilige **ID (RID)** verwenden, nicht den Namen.
- Namen nur verwenden, wenn sie im Hue-System eindeutig sind.
- Optional können Präfixe genutzt werden: `room:<id|name>` bzw. `zone:<id|name>`.

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
### Pairing-Zeitfenster

Legt fest, wie lange das Modul nach einem Pairing-Trigger auf den Link-Button der Hue Bridge wartet.

Hinweise:
- Bereich: `5..120 s`
- Standard: `30 s`
- Gilt sowohl für Pairing per ETS-KO als auch für das Pairing über das Webinterface.

<!-- DOC -->
### Status Verbindung

Blendet das KO **Bridge Verbindungsstatus** ein.
- `0`: keine Verbindung
- `1`: verbunden

<!-- DOC -->
### Anzahl aktiver Kanäle

Legt fest, wie viele Hue-Kanäle (0-24) in ETS sichtbar/aktiv sind.
Empfehlung: nur tatsächlich benötigte Kanäle aktivieren.

Hinweis:
- Viele aktive Kanäle erhöhen Parametrierungsumfang, Kommunikationsobjekte und die Größe der ETS-Projektierung.

<!-- DOC -->
### Einschaltverhalten

Globale Einstellung für die Übergangszeit bei Zustandswechseln aller Hue-Kanäle.

- **Einschaltgeschwindigkeit (Sekunden)**: Übergangszeit beim Wechsel von `Aus` nach `Ein`
- **Ausschaltgeschwindigkeit (Sekunden)**: Übergangszeit beim Wechsel von `Ein` nach `Aus`

Hinweise:
- Die Werte gelten für alle Kanäle (keine kanal-spezifische Einstellung).
- Die Einstellungen wirken sowohl mit als auch ohne aktive Lichtmanager-Zuordnung.
- Die Zeiten werden insbesondere beim Schalten sowie bei Helligkeitswerten verwendet, die ein automatisches Ein- oder Ausschalten auslösen.
- Reine Dimmänderungen ohne Zustandswechsel verwenden diese Parameter nicht automatisch.
- Standardwerte: `2 s` für Ein, `6 s` für Aus.
- Für typische Praxisanforderungen: Einschalten eher kurz, Ausschalten eher länger.

<!-- DOC -->
### Relatives Dimmen

Legt fest, wie schnell ein relatives Dimmkommando (DPT 3.007) pro Schritt übernommen wird.

- **Dimmgeschwindigkeit (ms)**: Pause in Millisekunden zwischen zwei Dimm-Schritten.

Hinweise:
- Kleinere Werte = schnelleres Dimmen.
- Gilt für alle Kanäle mit Lampentyp `Dimmbar` oder höher.
- Typischer Richtwert: `80..150 ms`.

<!-- DOC -->
### Hue Szenen aktivieren

Aktiviert die globale Szenen-Zuordnungsseite (Reiter **Hue Szenen**), auf der bis zu 8 Hue-Szenen-RIDs hinterlegt werden können.

- **Deaktiviert**: Der Szenen-Reiter ist ausgeblendet.
- **Aktiviert**: Der Reiter **Hue Szenen** erscheint und ermöglicht die Zuordnung von Hue-Szenen-RIDs zu den 8 globalen Szenen-Slots.

Hinweis: Kanalspezifische Szenensteuerung (DPT 18.001) wird separat je Kanal unter **Szenensteuerung** konfiguriert.

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

Empfehlung:
- Für produktive Projekte bevorzugt die ID (RID) eintragen.
- Namen nur bei eindeutiger Benennung verwenden.

<!-- DOC -->
### Hue Lampen-ID (UUID)

Legacy-/Fallback-Feld für bestehende Projektierungen.

Für neue Projektierungen bitte **Hue Ziel (Light-/Room-/Zone-ID oder Name)** verwenden.

Gilt nur für Zieltyp **Licht**.

Hinweis:
- Dieses Feld dient primär der Migration älterer ETS-Projekte.

Ermittlung über:
- Webinterface: `http://<IP-des-OpenKNX-Geräts>/openknx/hue/scan`
- Konsole: `hue scan`
- Hue API v2 (`/clip/v2/resource/light`)

Wichtig: Die UUID muss je Kanal exakt zur gewünschten Leuchte passen.

<!-- DOC -->
### Gerätetyp

Legt fest, welche Art von Hue-Gerät der Kanal steuert. Je nach Gerätetyp werden unterschiedliche Parameter und KOs sichtbar:

| Gerätetyp | Beschreibung | Verfügbare Funktionen |
|---|---|---|
| **Licht** | Hue-Leuchte (On/Off, Dimmbar, CT, RGB) | Schalten, Dimmen, CT, RGB, Szenen, HCL |
| **Steckdose** | Smart Plug | Schalten, Szenen (nur Ein/Aus) |
| **Taster/Schalter** | Hue-Schalter mit Tasten | KOs je Taste (Kurz/Lang), Drehregler |
| **Bewegungsmelder** | Hue-Bewegungssensor | Präsenz-KO, optional Lux/Temperatur |
| **Kontaktsensor** | Hue-Tür-/Fensterkontakt | Kontakt-KO |

<!-- DOC -->
### Taster/Schalter – Konfiguration

Bei Gerätetyp **Taster/Schalter** werden Tastenereignisse (Kurz-/Langdruck) vom Hue-System empfangen und als KNX-Telegramme auf den Bus gesendet.

#### Anzahl Tasten

Legt fest, wie viele Tasten des Hue-Geräts konfiguriert werden (1–4). Entsprechend viele Taste-N-Sektionen werden eingeblendet.

#### Native Hue Aktion

Steuert, ob das Hue-Gerät zusätzlich seine eigene Hue-Nativaktion ausführt, wenn eine Taste gedrückt wird:

- **Beibehalten**: Das Gerät führt seine native Hue-Aktion UND das KNX-Telegramm aus (Parallelausführung).
- **Deaktivieren**: Das Gerät führt nur das KNX-Telegramm aus – die native Hue-Aktion wird über die API unterdrückt.

Empfehlung: **Deaktivieren**, wenn die Hue-Leuchten vollständig über KNX gesteuert werden sollen, um Doppelreaktionen zu vermeiden.

<!-- DOC -->
### Taste Gewerk

Legt das Gewerk (die Funktion) einer Taste fest. Je nach gewähltem Gewerk werden unterschiedliche Kurz- und Langdruck-Optionen eingeblendet:

| Gewerk | Beschreibung |
|---|---|
| **Licht** | Schalten oder Szene (Kurzdruck), Dimmen (Langdruck) |
| **Jalousie** | Lamellensteuerung (Kurzdruck), Behangsteuerung (Langdruck) |
| **Medien** | Play/Pause (Kurzdruck), Lautstärke (Langdruck) |
| **Generisch** | Zwei frei belegbare KNX-Objekte (A = Kurzdruck, B = Langdruck) |

<!-- DOC -->
### Taste Kurzdruck

Bestimmt die Aktion beim kurzen Tastendruck. Die verfügbaren Optionen hängen vom Gewerk ab:

**Gewerk Licht:**
- **Kein Kurzdruck**: Kurzdruck ohne KNX-Aktion (sinnvoll z. B. für reine Dimmtaster)
- **Schalten**: Togglet den Schaltzustand (EIN/AUS wechselnd). Standardwert.
- **Szene abrufen**: Ruft eine KNX-Szene ab (DPT 17.001). Die Szenennummer wird unterhalb eingeblendet.

**Gewerk Jalousie:**
- **Kein Kurzdruck**: Kurzdruck ohne KNX-Aktion
- **Lamelle Auf/Stop**: Sendet `Auf`-Befehl (DPT 1.008)
- **Lamelle Ab/Stop**: Sendet `Ab`-Befehl (DPT 1.008)

**Gewerk Medien:**
- **Play/Pause**: Togglet Play/Pause (DPT 1.001)

**Gewerk Generisch:**
- **Objekt A**: Sendet auf das primäre KO (DPT 1.001)

Hinweis: Bei **Kein Kurzdruck** wird für diese Taste kein Kurzdruck-KO in ETS eingeblendet.

<!-- DOC -->
### Taste Langdruck

Bestimmt die Aktion beim langen Tastendruck. Optionen je Gewerk:

**Gewerk Licht:**
- **Kein Langdruck**: Kein Langdruck-KO, kein Dimm-Verhalten
- **Heller dimmen**: Sendet relatives Dimmen `heller` (DPT 3.007)
- **Dunkler dimmen**: Sendet relatives Dimmen `dunkler` (DPT 3.007)

Hinweis: Ist Kurzdruck = **Szene abrufen**, ist kein Langdruck möglich (Szenen-Taste hat keinen Langdruck-Modus).

**Gewerk Jalousie:**
- **Kein Langdruck**: Keine Behangsteuerung
- **Behang Auf**: Sendet `Auf`-Befehl (DPT 1.007)
- **Behang Ab**: Sendet `Ab`-Befehl (DPT 1.007)

**Gewerk Medien:**
- **Kein Langdruck**: Keine Lautstärkesteuerung
- **Lauter**: Sendet relatives Dimmen `heller` (repurposed, DPT 3.007)
- **Leiser**: Sendet relatives Dimmen `dunkler` (repurposed, DPT 3.007)

**Gewerk Generisch:**
- **Kein Langdruck**: Kein Sekundär-KO
- **Objekt B**: Sendet auf das sekundäre KO (DPT 1.001)

<!-- DOC -->
### Drehregler

Wenn der Hue-Schalter über einen Drehregler verfügt (z. B. Hue Tap Dial):

- **Drehregler vorhanden**: Aktiviert die Drehregler-Konfiguration
- **Funktion**: Legt fest, ob der Drehregler **Helligkeit absolut**, **Helligkeit relativ** oder **Farbtemperatur** steuert
- **Schrittweite (%)**: Prozentwert je Rastschritt (Standardwert: 5 %)

<!-- DOC -->
### Lampentyp

Der Lampentyp bestimmt, welche KOs sichtbar/aktiv sind:
- **Nur schalten**: Schalten + Status Schalten
- **Dimmbar**: zusätzlich Helligkeit/Dimmen + Status Helligkeit
- **Farbtemperatur**: zusätzlich Farbtemperatur + Status Farbtemperatur
- **Farbe RGB**: zusätzlich RGB + Status RGB

Hinweis: Sichtbar nur bei Gerätetyp **Licht**.

Hinweise:
- Für Zieltyp **Raum/Zone** sind Schalten, Dimmen, Farbtemperatur und RGB grundsätzlich nutzbar.
- Die tatsächliche Wirkung hängt von den Fähigkeiten der im Raum/der Zone enthaltenen Leuchten und der Bridge-Antwort ab.

<!-- DOC -->
### Kanal deaktivieren

Deaktiviert den Kanal ohne Verlust der Parametrierung.

<!-- DOC -->
### Synchronisationsrichtung
Es stehen drei Betriebsarten zur Verfügung:

1. **Nur KNX zu Hue**: Telegramme steuern Hue, Statusrückmeldungen aus Hue werden ignoriert. Sichtbar sind nur die Steuer-KOs.
2. **Nur Hue zu KNX**: KNX-Kommandos werden blockiert, Status wird aus Hue übernommen. Sichtbar sind nur die Status-KOs.
3. **Bidirektional**: KNX-Kommandos und Hue-Statusübernahme aktiv. Sichtbar sind Steuer- und Status-KOs. (Standardempfehlung)

Hinweis:
- Für relatives Dimmen gibt es kein separates KO `Status Dimmen`. Die Rückmeldung des aktuellen Dimmstands erfolgt über `Status Helligkeit`.

<!-- DOC -->
### Polling-Intervall

Status-Abfrageintervall in Sekunden.
Empfehlung: `5..30 s` je nach Kanalzahl und Netzlast.

Wichtig:
- `0` = zyklisches Polling für diesen Kanal deaktiviert.
- Bei `>0` wird der Kanal gemäß Intervall aus Hue gelesen (abhängig von Sync-Richtung).
- Nach KNX-Kommandos erfolgt zusätzlich ein kurzer Fast-Track-Statusabgleich.

<!-- DOC -->
### Statusverhalten bei Zieltyp Raum/Zone

Für Raum/Zone wird intern über `grouped_light` gesteuert.

Praxisverhalten:
- KNX->Hue-Kommandos (Schalten/Dimmen/CT/RGB) werden auf das Gruppen-Ziel gesendet.
- Status-KOs werden nach erfolgreichen Kommandos aktualisiert.
- Externe Änderungen (z. B. Hue App) werden je nach Event-/Polling-Zuordnung übernommen.

Hinweis:
- In bestimmten Konstellationen kann kein exakter physischer Gruppen-Istzustand aller Mitglieder abgebildet werden.
- Für streng deterministische Rückmeldung den gewünschten Sync-/Polling-Modus gezielt testen.

<!-- DOC -->
### Minimale Helligkeit

Mindestwert für Helligkeit >0, um Flackern bei niedrigen Dimmwerten zu vermeiden.

Beispiel: `5 %` für kritische Leuchten.

<!-- DOC -->
### Szenensteuerung

Ermöglicht die Zuordnung von KNX-Szenen (DPT 18.001) zu Hue-Aktionen. Bis zu **8 Szenen-Slots** je Kanal stehen zur Verfügung.

#### Szenensteuerung aktivieren

Aktiviert die Szenensteuerung für den Kanal und blendet das Szenen-KO sowie die Szenen-Konfigurationsseite ein.

#### Szene speichern

Legt fest, ob das Szenen-KO auch Speicherbefehle (DPT 18.001, Bit 7 = 1) auswertet.

- **Deaktiviert**: Nur Abruf (DPT 17.001-kompatibel, Bit 7 wird ignoriert)
- **Aktiviert**: Abruf und Speichern (DPT 18.001); ein Speicherbefehl sichert den aktuellen Istzustand in den jeweiligen Slot

#### Szene 1..8 (Slots)

Jeder Slot kann unabhängig parametriert werden. Ist die **Szenennummer** auf `0` (inaktiv) gesetzt, wird der Slot ignoriert.

**Szenennummer**: KNX-Szenennummer 1..64 (entspricht Bit 0..5 im DPT, also KNX-intern 0..63)

**Aktion**: Legt fest, was beim Abruf dieser Szene passiert. Die verfügbaren Optionen hängen vom **Lampentyp** des Kanals ab:

| Aktion | Beschreibung | Lampentyp |
|---|---|---|
| Ausschalten | Licht aus | alle |
| Einschalten | Licht ein (letzte Helligkeit) | alle |
| Helligkeit setzen | Ein + Helligkeitswert | Dimmbar, CT, RGB |
| Farbtemperatur setzen | Ein + CT-Wert | CT, RGB |
| Helligkeit + Farbtemperatur | Ein + Helligkeit + CT | CT, RGB |
| Farbe (RGB) setzen | Ein + RGB-Wert | RGB |
| Helligkeit + Farbe (RGB) | Ein + Helligkeit + RGB | RGB |
| Hue Szene abrufen | Ruft eine Hue-Szene per RID ab | alle (außer Steckdose) |

**Helligkeit**: Prozentwert 0..100 % (wird bei Aktionen mit Helligkeit verwendet)

**Farbtemperatur**: Kelvin-Wert 2000..6500 K (wird bei Aktionen mit CT verwendet)

**Rot / Grün / Blau**: RGB-Werte 0..255 (werden bei Aktionen mit Farbe verwendet)

**Hue-Szene**: Referenz auf eine der 8 global konfigurierten Hue-Szenen (→ Abschnitt [Hue Szenen (global)](#hue-szenen-global)). Sichtbar nur bei Aktion „Hue Szene abrufen".

#### Lichtmanager-Sperre bei Szenen

Wenn einem Kanal ein Lichtmanager zugeordnet ist, **sperrt ein erfolgreicher Szenen-Abruf automatisch die HCL-Kanalausgabe** für diesen Kanal. Damit behält das Licht nach dem Szenen-Abruf seinen Szenen-Wert, ohne dass der Lichtmanager ihn überschreibt.

Die Sperre wird aufgehoben durch:
- **Aus-Befehl** per KNX-Schalten-KO → Sperre wird sofort zurückgesetzt
- Ablauf der konfigurierten Rückfallzeit (→ Abschnitt [Sperre (kanal-spezifisch)](#sperre-kanal-spezifisch))
- Globales Entsperren per KO

#### Szene speichern (Laufzeit)

Wenn **Szene speichern** aktiviert ist und ein Speicherbefehl (DPT 18.001, Bit 7 = 1) empfangen wird, wird der aktuelle Istzustand des Kanals (Schaltzustand, Helligkeit, CT, RGB) persistent gespeichert. Beim nächsten Abruf dieser Szenennummer wird der gespeicherte Wert anstelle des ETS-Preset verwendet.

Hinweis: Gespeicherte Szenen werden im Flash des Geräts abgelegt und überleben einen Neustart.

<!-- DOC -->
### Lichtmanager Zuordnung

Ordnet den Kanal einem Lichtmanager (1..8) zu.
Bei `Kein Lichtmanager` arbeitet der Kanal ohne automatische Sollwert-Übernahme.

<!-- DOC -->
### Sperre (kanal-spezifisch)

Sperrt die automatische Lichtmanager-Ausgabe nur für den jeweiligen Hue-Kanal.
Andere Kanäle mit gleicher Lichtmanager-Zuordnung bleiben unverändert aktiv.
Der zugeordnete Lichtmanager selbst läuft weiter und versorgt weiterhin alle anderen ihm zugeordneten Hue-Kanäle.

Sichtbarkeit:
- Nur bei Lampentyp `Farbtemperatur` oder `Farbe RGB`.
- Nur wenn beim Kanal ein Lichtmanager `1..8` zugeordnet ist.

Option je Kanal:
- **Rückfallzeit nach Sperre** (inkl. Tageswechsel, `kein Rückfall` möglich)
- **Rückfallstrategie nach Sperre**: zentrale Vorgabe, siehe Abschnitt [Rückfallstrategie nach Sperre](#rückfallstrategie-nach-sperre)

**Automatische Sperre durch Szenen-Abruf**: Wenn ein Kanal einem Lichtmanager zugeordnet ist und eine Szene abgerufen wird, wird die HCL-Kanalsperre automatisch aktiviert. Die Sperre wird aufgehoben durch:
- Einen **Aus-Befehl** (KNX-Schalten-KO, Wert 0) → sofortiges Aufheben
- Ablauf der konfigurierten **Rückfallzeit**
- Globales Entsperren per KO `Entsperren Trigger`

KOs je Kanal:
- `Sperre` (Eingang)
- `Status Sperre` (Ausgang)

Praxisbeispiel:
- Wohn-/Essbereich mit gemeinsamem Lichtmanager.
- Am Abend läuft im Essbereich der Lichtmanager weiter, im Wohnzimmer wird per Taster `Sperre` aktiviert, damit dort eine feste, warme Szene bleibt.
- Am nächsten Morgen hebt die konfigurierte Rückfallzeit die Sperre automatisch auf und der Kanal folgt wieder der Lichtmanager-Kurve.

<!-- DOC -->
## Lichtmanager

<!-- DOC -->
### Hue Szenen (global)

Bis zu **8 Hue-Szenen** können global (auf Modulebene) als RID-Referenz hinterlegt werden. Diese werden in der Szenensteuerung der Kanäle bei Aktion **"Hue Szene abrufen"** ausgewählt.

- **Hue Szene 1..8 (RID)**: Ressourcen-ID der Szene aus dem Hue-System

Ermittlung der Scene-RID:
- Hue App → Szenen-Details (nicht immer direkt zugänglich)
- Hue API v2: `GET /clip/v2/resource/scene`

Hinweis: Eine Hue-Szene wird direkt über die Bridge aktiviert und kann beliebig viele Leuchten umfassen. Sie eignet sich für komplexe Beleuchtungseffekte, die nicht per ETS-Preset abgebildet werden können.

<!-- DOC -->
### Human Centric Lighting

Aktiviert zeitabhängige Sollwerte für Helligkeit und Farbtemperatur.
Bis zu 8 Lichtmanager können parallel definiert werden.

<!-- DOC -->
### Lichtmanager Auswahl

Legt die Anzahl sichtbarer Lichtmanager-Seiten (1..8) fest.

<!-- DOC -->
### Einstellungen

- **Aktualisierungsintervall (Sekunden)**
- **Überblendzeit (Sekunden)**

Sollwerte werden aus der Lichtmanager-Kurve berechnet und bei Wertänderung als Status-KO übertragen.

<!-- DOC -->
### Sperre (global)

Sperrt die automatische Ausgabe aller Lichtmanager.

Option:
- **Rückfallzeit nach Sperre** (inkl. Tageswechsel, `kein Rückfall` möglich)
- **Rückfallstrategie nach Sperre**: wirkt für globale, manager-spezifische und kanal-spezifische Sperren

KOs:
- `Sperre (global)` (Eingang)
- `Status Sperre` (Ausgang)

<!-- DOC -->
### Rückfallstrategie nach Sperre

Zusätzlich zur Rückfallzeit gibt es eine zentrale Strategie, wie Sperren wieder aufgehoben werden.
Diese Vorgabe gilt für globale, manager-spezifische und kanal-spezifische Sperren.

Verfügbare Strategien:
1. **Definierte Rückfallzeit**: verwendet ausschließlich die gewählte Rückfallzeit aus der Dropdown-Liste.
2. **Freie Dauer**: verwendet den Parameter **Freie Rückfalldauer** in Sekunden.
3. **Freie Uhrzeit**: verwendet den Parameter **Rückfall-Uhrzeit (HH:MM)**.
4. **Dauer oder Uhrzeit**: hebt die Sperre auf, sobald entweder die freie Dauer abgelaufen ist oder die Rückfall-Uhrzeit erreicht wird.
5. **Nur externes Entsperren**: es erfolgt keine automatische Freigabe; die Sperre muss über ein KO aufgehoben werden.

Ergänzende Parameter:
- **Freie Rückfalldauer**: Bereich `0..65535 s`, Standard `1800 s`
- **Rückfall-Uhrzeit (HH:MM)**: Standard `03:00`

Hinweis:
- Für das externe Entsperren steht zusätzlich das globale KO `Entsperren Trigger` zur Verfügung.

<!-- DOC -->
### Status-KOs je Lichtmanager

Aktiviert pro Manager die Ausgabe:
- `Status Helligkeit Soll` (`1 Byte`, `0..100 %`)
- `Status Farbtemperatur Soll` (`2 Byte`, `2000..6500 K`)

Hinweise:
- Die Ausgabe erfolgt zyklisch gemäß **Aktualisierungsintervall** des Lichtmanager-Bereichs.
- Die KOs liefern die vom Lichtmanager berechneten Sollwerte, unabhängig davon, wie viele Kanäle diesem zugeordnet sind.

<!-- DOC -->
### Lichtmanager 1..8

Jeder Manager besitzt identischen Aufbau:

#### Bezeichnung
Freie ETS-Bezeichnung des Lichtmanagers.

#### Lichtmanager Sperre (spezifisch)
Sperrt nur den jeweiligen Lichtmanager.
Alle Hue-Kanäle, die diesem Lichtmanager zugeordnet sind, erhalten während der Sperre keine automatischen Sollwerte mehr.

Optionen je Manager:
- **Rückfallzeit nach Sperre**
- **Rückfallstrategie nach Sperre**: zentrale Vorgabe, siehe Abschnitt [Rückfallstrategie nach Sperre](#rückfallstrategie-nach-sperre)

KOs je Lichtmanager:
- `Sperre Lichtmanager x` (Eingang)
- `Status Sperre Lichtmanager x` (Ausgang)

#### Erweiterte Kurve
Kurventyp:
- **FixedTime**
- **SunPosition**
- **Manual**
- **Astronomischer Sonnenstand**

Erweiterte Parameter je Manager:
- **Slew-Rate (K/min)**: begrenzt die Kelvin-Änderung pro Minute (`0` = keine Begrenzung).
- **Manuelle Farbtemperatur**: fixer Kelvin-Sollwert bei Kurventyp `Manual` (Bereich `2000..6500 K`).
- **Sonnenaufgang/Sonnenuntergang** und **Offsets (min)**: relevant für Kurventyp `SunPosition`.
- **Astro Min/Max Kelvin** und **Astro Min/Max Helligkeit**: relevant für Kurventyp `Astronomischer Sonnenstand`.

Kurventypen im Detail:
1. **FixedTime**
	Lineare Interpolation zwischen klassischen Stützpunkten aus Zeit, Helligkeit und Farbtemperatur.
2. **SunPosition**
	Nutzt ebenfalls Stützpunkte, richtet die Tagesform aber an Sonnenaufgang und Sonnenuntergang mit konfigurierbaren Offsets aus.
	Die Minimal- und Maximalwerte werden aus den gesetzten Stützpunkten abgeleitet.
3. **Manual**
	Verwendet eine feste Farbtemperatur aus dem Parameter **Manuelle Farbtemperatur**.
	Optionale Stützpunkte beeinflussen in diesem Modus nur den Helligkeitsverlauf.
4. **Astronomischer Sonnenstand**
	Verwendet keine Stützpunkte.
	Helligkeit und Farbtemperatur werden direkt aus dem Sonnenstand berechnet und zwischen den Astro-Min-/Max-Werten skaliert.
	Grundlage sind die OpenKNX-Basisparameter für Standort und Zeitzone.

#### Stützpunkte
Bis zu 10 Stützpunkte je Manager bei Kurventyp `FixedTime` oder `SunPosition`.

Hinweise:
- Bei `FixedTime` und `SunPosition` sind mindestens 2 gültige Zeit-Stützpunkte erforderlich.
- Bei `Manual` sind Stützpunkte optional; wenn sie gesetzt werden, definieren sie Zeit + Helligkeit, die Farbtemperatur kommt aus dem Parameter **Manuelle Farbtemperatur**.
- Bei `Manual` ohne Stützpunkte bleibt die Helligkeit konstant auf `100 %`, die Farbtemperatur auf dem konfigurierten manuellen Kelvin-Wert.
- Bei `Astronomischer Sonnenstand` werden keine Stützpunkte verwendet; stattdessen werden Minimal- und Maximalwerte für Kelvin und Helligkeit genutzt.

Beispiel:
- SP1 `06:00 / 3000K / 30%`
- SP2 `12:00 / 5000K / 90%`
- SP3 `20:00 / 2400K / 35%`

Praxisregel:
- `Aktualisierungsintervall`, `Überblendzeit` und `Slew-Rate` gemeinsam abstimmen, damit Übergänge ruhig bleiben.

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
#### Sperre (global) / Status Sperre

Globale Sperre inkl. Statusrückmeldung.

<!-- DOC -->
#### Entsperren Trigger

1-Bit Triggerobjekt zum gleichzeitigen Aufheben aller globalen, manager-spezifischen und kanal-spezifischen Sperren.

<!-- DOC -->
#### Sperre Lichtmanager 1..8 / Status

Lichtmanager-spezifische Sperrobjekte inkl. Statusrückmeldung.
Sichtbarkeit abhängig von der konfigurierten Anzahl Lichtmanager.

<!-- DOC -->
#### Lichtmanager Status Helligkeit Soll / Farbtemperatur Soll

Je Lichtmanager zwei Sollwert-KOs; Sichtbarkeit abhängig von Option **Status-KOs je Lichtmanager**.

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

Hinweis:
- Es gibt bewusst kein separates KO `Status Dimmen`, da der Rückmeldewert als absolute Helligkeit über `Status Helligkeit` bereitgestellt wird.

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
#### Sperre (kanal-spezifisch) / Status Sperre

1-Bit Sperrobjekt mit 1-Bit Statusrückmeldung je Kanal.

Hinweise:
- Wirkt nur auf die automatische Ausgabe des einzelnen Kanals.
- Ein gemeinsam genutzter Lichtmanager bleibt für andere zugeordnete Hue-Kanäle weiterhin aktiv.
- Manuelle KNX-Kommandos für Schalten/Dimmen/CT/RGB bleiben möglich.
- Sichtbar nur bei aktivem Lichtmanager am Kanal und geeignetem Lampentyp.

<!-- DOC -->
#### Szenen

1-Byte KO (DPT 18.001) zum Abrufen und optionalen Speichern von Szenen.

- **Bit 7 = 0 (Abruf)**: Bits 0..5 = Szenennummer 0..63 (entspricht ETS-Szene 1..64)
- **Bit 7 = 1 (Speichern)**: Bits 0..5 = Szenennummer; nur wenn **Szene speichern** aktiviert ist

Sichtbar nur wenn **Szenensteuerung aktivieren** am Kanal gesetzt ist.

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

### Beispiel 3: Lichtmanager im Arbeitszimmer
- Lampentyp: Farbtemperatur
- Lichtmanager: 1
- Intervall Lichtmanager: 60 s
- Sperre Lichtmanager 1 via KO auf GA für Präsenz/Abwesenheit

### Beispiel 4: Raumsteuerung (Zone/Room) mit Rückmeldung
- Zieltyp: Raum
- Hue Ziel: Room-ID (RID)
- Lampentyp: Dimmbar oder höher
- Sync: Bidirektional
- Polling: 10 s

### Beispiel 5: Zone mit Farbtemperatur/RGB
- Zieltyp: Zone
- Hue Ziel: Zone-ID (RID)
- Lampentyp: Farbe RGB
- Sync: Bidirektional
- Polling: 5..15 s
- Hinweis: Wirkung abhängig von Fähigkeiten der enthaltenen Leuchten

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
- Sync auf **Nur Hue zu KNX** oder **Bidirektional** gesetzt?
- Polling-Intervall sinnvoll gesetzt (`0` deaktiviert zyklisches Polling)?
- Status-KO mit GA verbunden?
- Zieltyp/Hue Ziel korrekt und auflösbar?
- Bei Raum/Zone: Rückmeldeverhalten mit Hue-App-Änderungen gesondert verifizieren.

### Lichtmanager wirkt nicht
- Lichtmanager global aktiviert?
- Manager zugewiesen?
- Bei `FixedTime`/`SunPosition`: mind. 2 gültige Stützpunkte?
- Bei `Manual`: gewünschte manuelle Farbtemperatur gesetzt und optionaler Helligkeitsverlauf passend parametriert?
- Bei `Astronomischer Sonnenstand`: sinnvolle Astro-Min/Max-Werte gesetzt?
- Globale/spezifische Sperre aktiv?

<!-- DOC -->
## Inbetriebnahme-Checkliste

- Bridge gefunden und authentifiziert
- Zieltyp und Hue Ziel pro Kanal geprüft
- Gerätetyp und Lampentyp passend zur realen Hardware
- Sync/Polling passend zur Anwendung
- Benötigte Status-KOs mit GAs verbunden
- Lichtmanager-Funktion inkl. Sperren (global/spezifisch) getestet
- Szenensteuerung: Szenennummern, Aktionen und Preset-Werte geprüft
- Bei Szene + Lichtmanager: Verhalten nach Aus-Befehl verifiziert (Sperre wird aufgehoben)

<!-- DOC -->
## Lizenz und Haftung

Open-Source-Modul im OpenKNX-Umfeld.
Keine Gewährleistung; Nutzung in eigener Verantwortung.
Philips Hue ist ein Warenzeichen von Signify N.V.
