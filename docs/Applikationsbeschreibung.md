# Applikationsbeschreibung Philips Hue Gateway Modul

Das OFM-HueGatewayModule verbindet Philips Hue Leuchten mit dem KNX-Bus.
Die Kommunikation erfolgt über die Hue Bridge (Hue API v2).

# Applikationsprogramm

## Inhaltsverzeichnis

- [Allgemein](#allgemein)
- [Unterstützte Geräte](#unterstützte-geräte)
- [Bridge-Konfiguration](#bridge-konfiguration)
- [Kanal 1-n (Hue Ziele)](#kanal-1-n-hue-ziele)
- [Lichtmanager](#lichtmanager)
- [Kommunikationsobjekte](#kommunikationsobjekte)
- [Projektierungsbeispiele](#projektierungsbeispiele)
- [Performance-Empfehlungen](#performance-empfehlungen)
- [Häufige Fehler und Lösungen](#häufige-fehler-und-lösungen)
- [Inbetriebnahme-Checkliste](#inbetriebnahme-checkliste)

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
## Unterstützte Geräte

Das Modul arbeitet generisch auf Basis der Hue API v2 Ressourcentypen.
Es werden **alle Geräte** unterstützt, die an einer Hue Bridge (Zigbee) angelernt sind und dort die entsprechenden API-Ressourcen bereitstellen.
Eine Prüfung auf bestimmte Modell- oder Herstellernamen findet nicht statt – entscheidend ist ausschließlich der vom Bridge gemeldete Ressourcentyp.

### Gerätekategorien im Modul

| ETS-Gerätetyp | Hue API v2 Ressource | Erkennung | KNX-Funktionen |
|---|---|---|---|
| **Licht** | `light` | Alle Light-Ressourcen ohne Steckdosen-Archetype | Schalten, Dimmen, Farbtemperatur, RGB (je nach Fähigkeit) | Ja |
| **Steckdose** | `light` + Archetype enthält *plug*, *socket* oder *outlet* | Archetype-Substring-Prüfung | Schalten Ein/Aus |
| **Taster/Schalter** | `button`, `relative_rotary` | Service-Typ einer Geräte-Ressource | Tastenereignisse (Kurz-/Langdruck), Drehregler |
| **Bewegungsmelder** | `motion` | Service-Typ einer Geräte-Ressource | Präsenz, optional Temperatur, Helligkeit, Batterie |
| **Kontaktsensor** | `contact_sensor` | Service-Typ einer Geräte-Ressource | Kontaktstatus, optional Manipulation, Temperatur, Batterie | 
| **Raum/Zone** | `grouped_light` | Aggregierte Lichtressource eines Raumes oder einer Zone | Schalten, Dimmen, Farbtemperatur, RGB (je nach enthaltenen Leuchten) | 

Die Fähigkeiten einer Leuchte (dimmbar, Farbtemperatur, Farbe) werden automatisch anhand der von der Bridge gemeldeten JSON-Felder erkannt:

| Lampentyp in ETS | Voraussetzung auf Bridge-Seite |
|---|---|
| Ein/Aus | Nur `on/off`-Feld vorhanden |
| Dimmbar | `dimming`-Feld vorhanden | 
| Farbtemperatur | `color_temperature`-Feld vorhanden (Mirek 153–500) |
| Farbe (RGB) | `color`-Feld vorhanden (CIE 1931 XY) | 

### Philips Hue Produkte (Signify)

#### Leuchten

| Produktreihe | Typ | Lampentyp in ETS | Getestet |
|---|---|---|---|
| Hue White | E27, E14, GU10, A19, BR30, PAR38 | Dimmbar | Ja  |
| Hue White Ambiance | E27, E14, GU10, A19, BR30, Lightstrip | Farbtemperatur | Ja |
| Hue White and Color Ambiance | E27, E14, GU10, A19, BR30, Lightstrip Plus | Farbe (RGB) | Ja |
| Hue Filament | ST64, G93, G125, A60, ST72, T75 | Farbtemperatur | Nein |
| Hue Lightguide | Ellipse, Triangle, Globe | Farbe (RGB) | Nein |
| Hue Gradient Lightstrip | Lightstrip, Signe Tisch-/Stehleuchte, Tube | Farbe (RGB) | Ja |
| Hue Play | Light Bar | Farbe (RGB) | Nein |
| Hue Go | Portable | Farbe (RGB) | Nein |
| Hue Iris | Tischleuchte | Farbe (RGB) | Nein |
| Hue Bloom | Tischleuchte | Farbe (RGB) | Nein |
| Hue Centura | Einbaustrahler | Farbe (RGB) | Nein |
| Hue Fugato | Deckenstrahler | Farbe (RGB) | Nein |
| Hue Perifo | Schienensystem | Farbe (RGB) | Nein |
| Hue Xamento | Badezimmer-Einbaustrahler | Farbe (RGB) | Nein |
| Hue Aurelle | Deckenleuchte (Panel) | Farbtemperatur | Ja |
| Hue Being, Fair, Still | Deckenleuchten | Farbtemperatur | Nein |
| Hue Cher, enrave | Pendelleuchten | Farbtemperatur | Nein |
| Hue Outdoor (Lily, Calla, Appear, Nyro, Impress, Econic, Resonate, Attract, Lucca, Turaco, Daylo) | Außenleuchten | je nach Modell: Farbtemperatur oder Farbe (RGB) | Nein |

#### Smart Plugs

| Produkt | Archetype | Lampentyp | Getestet |
|---|---|---|---|
| Hue Smart Plug (EU ) | `hue_siren` / `plug` | Steckdose (Ein/Aus) | Nein |

#### Sensoren

| Produkt | Hue API Ressource | Zusatzdaten | Getestet |
|---|---|---|---|
| Hue Motion Sensor (Indoor) | `motion` | Temperatur, Helligkeit, Batterie | Nein |
| Hue Outdoor Sensor | `motion` | Temperatur, Helligkeit, Batterie | Nein |
| Hue Secure Contact Sensor | `contact_sensor` | Temperatur, Manipulation, Batterie | Nein |

#### Taster und Schalter

| Produkt | Tasten | Drehregler | Hue API Ressource | Getestet |
|---|---|---|---|---|
| Hue Dimmer Switch (V1/V2) | 4 | – | `button` | Ja |
| Hue Tap Dial Switch | 4 | 1 | `button` + `relative_rotary` | Ja |
| Hue Wall Switch Module | 2 | – | `button` | Nein |
| Hue Tap Mini | 4 | – | `button` | Nein |

### Friends of Hue (Zigbee Green Power)

Friends-of-Hue-Schalter werden vom Modul als **Taster/Schalter** mit `button`-Ressourcen erkannt.

| Hersteller | Produkt | Tasten | Getestet |
|---|---|---|---|
| Busch-Jaeger | Friends of Hue (1-fach, 2-fach) | 1–4 | Nein |
| Gira | Friends of Hue (1-fach, 2-fach) | 1–4 | Nein |
| JUNG | Friends of Hue (1-fach, 2-fach) | 1–4 | Nein |
| Niko | Friends of Hue (1-fach, 2-fach) | 1–4 | Nein |
| Vimar | Friends of Hue | 1–4 | Nein |
| Feller | Friends of Hue (Schweiz) | 1–4 | Nein |
| illumra | EnOcean/Zigbee Green Power Schalter | 1–4 | Nein |
| Senic / Nuimo | Friends of Hue Smart Switch | 1–4 | Nein |
| RunLessWire | Friends of Hue Click | 1–4 | Nein |

### Drittanbieter-Leuchten und -Steckdosen

Folgende Hersteller bieten Zigbee-Leuchtmittel und -Steckdosen an, die sich mit der Hue Bridge koppeln lassen.
Nach erfolgreicher Kopplung an der Bridge werden sie vom Modul wie native Hue-Leuchten bzw. -Steckdosen behandelt.

> **Hinweis:** Die Kompatibilität mit der Hue Bridge hängt vom jeweiligen Gerätemodell und der Firmware-Version ab.
> Offiziell von Signify unterstützte Drittanbieter sind mit (✓) markiert; bei übrigen Einträgen ist die Kopplung erfahrungsgemäß möglich, aber nicht von Signify garantiert.

#### Leuchten

| Hersteller | Beispiele | Hue-Bridge-Kompatibilität | Lampentyp | Getestet |
|---|---|---|---|---|
| innr | E27, E14, GU10, LED-Strips, Deckenleuchten | ✓ offiziell | je nach Modell: Dimmbar / CT / RGB | Nein |
| IKEA TRÅDFRI (DIRIGERA) | E27, E14, GU10, LED-Panels | erfahrungsgemäß (Touchlink) | je nach Modell: Dimmbar / CT / RGB | Nein |
| OSRAM/LEDVANCE Smart+ | E27, E14, GU10, LED-Strips (ältere ZLL-Modelle) | teilweise | je nach Modell: Dimmbar / CT / RGB | Nein |
| Müller-Licht tint | E27, E14, GU10, LED-Panels | teilweise | je nach Modell: Dimmbar / CT | Nein |
| GLEDOPTO | Zigbee LED-Controller (RGB, RGBW, CCT) | erfahrungsgemäß | CT / RGB | Nein |
| Sengled | Smart LED Bulbs (E27, BR30) | teilweise | Dimmbar / CT | Nein |
| Paulmann | SmartHome Zigbee Leuchtmittel | teilweise | je nach Modell: Dimmbar / CT | Nein |

#### Steckdosen

| Hersteller | Produkt | Hue-Bridge-Kompatibilität | Archetype | Getestet |
|---|---|---|---|---|
| innr | Smart Plug (SP 120, SP 220, SP 224) | ✓ offiziell | `plug` | Nein |
| OSRAM/LEDVANCE | Smart+ Plug | teilweise | `plug` | Nein |
| IKEA TRÅDFRI | ASKVADER Steckdose | erfahrungsgemäß (Touchlink) | `plug` | Nein |

### Hinweise zur Gerätekompatibilität

- **Entscheidend ist die Kopplung an der Hue Bridge**: Sobald ein Gerät dort angelegt ist, stellt die Bridge es über die API v2 bereit und das Gateway-Modul kann es nutzen.
- **Keine Modellprüfung im Code**: Das Modul prüft weder `model_id`, `manufacturer_name` noch `product_name`. Es arbeitet ausschließlich mit den API-Ressourcentypen (`light`, `button`, `motion`, `contact_sensor`, `relative_rotary`, `grouped_light`).
- **Steckdosen-Erkennung**: Smart Plugs werden an der Bridge als `light`-Ressource geführt. Das Modul unterscheidet sie anhand des Archetype-Feldes (Substring `plug`, `socket` oder `outlet`).
- **Zukünftige Geräte**: Neue Hue- oder Zigbee-Produkte, die sich an der Bridge anlernen lassen und Standard-Ressourcentypen verwenden, werden automatisch unterstützt – ohne Firmware-Update des Moduls.

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
3. In ETS je Kanal **Zieltyp** setzen und **Hue Ziel (Light-/Room-/Zone-ID)** eintragen.
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

Die Zielzuordnung erfolgt primär über **Hue Ziel (Light-/Room-/Zone-ID)**.

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
### Hue Ziel (Light-/Room-/Zone-ID)

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

<!-- DOC HelpContext="Hue-Ziel-Light-Room-Zone-ID-oder-Name" -->
Primäres Zielfeld für alle Zieltypen. Akzeptiert sowohl eine numerische ID (RID) als auch einen eindeutigen Namen.

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
<!-- DOCEND -->

<!-- DOC -->
### Hue Ziel (Geräte-ID)

Zielfeld für Gerätetypen, die direkt auf ein einzelnes Hue-Gerät zeigen, insbesondere:

- Bewegungsmelder
- Taster/Schalter
- Kontaktsensor
- Steckdose

Verwenden Sie hier die von der Hue Bridge gemeldete Geräte-ID oder einen eindeutigen Gerätenamen.

Ermittlung über:
- Webinterface: `http://<IP-des-OpenKNX-Geräts>/openknx/hue/scan`
- Konsole: `hue scan`

Empfehlung:
- Für produktive Projekte bevorzugt die ID (RID) eintragen.
- Namen nur bei eindeutiger Benennung verwenden.

Hinweis:
- Im Unterschied zu **Hue Ziel (Light-/Room-/Zone-ID)** wird hier kein Raum/Zone-Ziel aufgelöst, sondern ein konkretes Hue-Gerät adressiert.

<!-- DOC HelpContext="Hue-Ziel-Geraete-ID-oder-Name" -->
Zielfeld für direkte Gerätezuordnung. Akzeptiert sowohl eine numerische Geräte-ID (RID) als auch einen eindeutigen Gerätenamen.

Gerätetypen:
- Bewegungsmelder
- Taster/Schalter
- Kontaktsensor
- Steckdose

Ermittlung über:
- Webinterface: `http://<IP-des-OpenKNX-Geräts>/openknx/hue/scan`
- Konsole: `hue scan`

Empfehlung:
- Für produktive Projekte bevorzugt die ID (RID) eintragen.
- Namen nur bei eindeutiger Benennung verwenden.
<!-- DOCEND -->

<!-- DOC -->
### Hue Lampen-ID (UUID)

Legacy-/Fallback-Feld für bestehende Projektierungen.

Für neue Projektierungen bitte **Hue Ziel (Light-/Room-/Zone-ID)** verwenden.

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
| **Steckdose** | Smart Plug | Schalten |
| **Taster/Schalter** | Hue-Schalter mit Tasten | KOs je Taste (Kurz/Lang), Drehregler |
| **Bewegungsmelder** | Hue-Bewegungssensor | Präsenz-KO, optional Lux/Temperatur |
| **Kontaktsensor** | Hue-Tür-/Fensterkontakt | Kontakt-KO |

<!-- DOC -->
### Optionale Kommunikationsobjekte

Je nach Gerätetyp können zusätzliche Kommunikationsobjekte eingeblendet werden.

Typische optionale KOs sind:
- Gerät erreichbar
- Batterie
- Temperatur
- Helligkeit
- Sabotage

Die Sichtbarkeit hängt vom gewählten Gerätetyp und den von der Hue Bridge bereitgestellten Eigenschaften ab.

<!-- DOC -->
### Gerät erreichbar verwenden

Blendet ein zusätzliches Status-KO ein, das die Erreichbarkeit des Hue-Geräts signalisiert.

- `0`: Gerät nicht erreichbar
- `1`: Gerät erreichbar

Sinnvoll für:
- Diagnose
- Visualisierung
- Meldelogik

<!-- DOC -->
### Batterie verwenden

Blendet ein zusätzliches Kommunikationsobjekt für den Batteriestatus des Geräts ein.

Sinnvoll für batteriebetriebene Geräte wie:
- Bewegungsmelder
- Kontaktsensoren
- Taster/Schalter

Hinweis:
- Das KO ist nur sinnvoll, wenn die Hue Bridge für das jeweilige Gerät tatsächlich Batteriedaten bereitstellt.

<!-- DOC -->
### Temperatur verwenden

Blendet ein zusätzliches Kommunikationsobjekt für die vom Hue-Gerät gemeldete Temperatur ein.

Typisch bei:
- Bewegungsmeldern

Hinweis:
- Nicht jedes Gerät mit Präsenz- oder Kontakterkennung liefert auch Temperaturwerte.

<!-- DOC -->
### Helligkeit verwenden

Blendet ein zusätzliches Kommunikationsobjekt für den vom Hue-Gerät gemeldeten Helligkeits- bzw. Luxwert ein.

Typisch bei:
- Bewegungsmeldern

Hinweis:
- Der Wert dient der Auswertung des Umgebungslichts und ist nicht mit der Helligkeit einer Leuchte zu verwechseln.

<!-- DOC -->
### Sabotage verwenden

Blendet ein zusätzliches Kommunikationsobjekt ein, das einen Manipulations- bzw. Sabotagezustand des Geräts meldet.

Typisch bei:
- Kontaktsensoren

Hinweis:
- Das KO ist nur verfügbar, wenn das jeweilige Hue-Gerät diesen Zustand über die Bridge bereitstellt.

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

#### Beispielkonfigurationen

#### 2-Tasten-Dimmer

- Taste 1: Kurzdruck = **Schalten (DPT 1.001)**, Langdruck = **Dimmen Start/Stop (DPT 3.007)**
- Taste 2: Kurzdruck = **Schalten (DPT 1.001)**, Langdruck = **Dimmen Start/Stop (DPT 3.007)**

Geeignet für kompakte Wandtaster mit Auf/Ab-Logik.

#### 4-Tasten-Szenentaster

- Taste 1–4: Kurzdruck = **Szenennummer (DPT 18.001)**, Langdruck = **Kein Langdruck**

Geeignet für Raumsteuerungen mit fester Szenenzuordnung wie `Arbeiten`, `Entspannen`, `Abend`, `Aus`.

#### Jalousie-Taster

- Kurzdruck = **Schritt/Stop (DPT 1.007)** (Richtung: Auf oder Ab)
- Langdruck = **Fahren (DPT 1.008)** (Richtung: Auf oder Ab)

Sinnvoll, wenn ein Hue-Taster nicht für Licht, sondern für eine KNX-Jalousie-Funktion genutzt werden soll.

<!-- DOC -->
### Taste Kurzdruck

Bestimmt die Aktion beim kurzen Tastendruck. Je nach Wahl werden zusätzliche Sub-Parameter eingeblendet:

| Option | DPT | Sub-Parameter |
|---|---|---|
| **Keine Aktion** | — | — |
| **Schalten** | DPT 1.001 | **Schaltwert**: Toggle / Ein / Aus |
| **Dimmen relativ** | DPT 3.007 | **Richtung** (Heller / Dunkler), **Schrittweite** (2–25 %) |
| **Szenennummer** | DPT 18.001 | **Szenennummer** (1..64) |
| **Schritt/Stop** | DPT 1.007 | **Richtung** (Auf / Ab) |
| **Prozentwert** | DPT 5.001 | **Prozentwert** (0..100 %) |
| **Temperaturwert** | DPT 9.001 | **Temperatur** (5..40 °C) |
| **1-Byte Wert** | DPT 5.010 | **Wert** (0..255) |
| **2-Byte Wert** | DPT 7.001 | **Wert** (0..65535) |

Hinweis: Bei **Keine Aktion** wird für diese Taste kein Kurzdruck-KO in ETS eingeblendet.

<!-- DOC -->
### Taste Langdruck

Bestimmt die Aktion beim langen Tastendruck. Je nach Wahl werden zusätzliche Sub-Parameter eingeblendet:

| Option | DPT | Sub-Parameter |
|---|---|---|
| **Kein Langdruck** | — | — |
| **Schalten** | DPT 1.001 | **Schaltwert**: Ein / Aus |
| **Dimmen Start/Stop** | DPT 3.007 | **Richtung** (Heller / Dunkler), **Schrittweite** (2–25 %) |
| **Szenennummer** | DPT 18.001 | **Szenennummer** (1..64) |
| **Fahren** | DPT 1.008 | **Richtung** (Auf / Ab) |
| **Schritt/Stop** | DPT 1.007 | **Richtung** (Auf / Ab) |
| **Prozentwert** | DPT 5.001 | **Prozentwert** (0..100 %) |
| **Temperaturwert** | DPT 9.001 | **Temperatur** (5..40 °C) |
| **1-Byte Wert** | DPT 5.010 | **Wert** (0..255) |
| **2-Byte Wert** | DPT 7.001 | **Wert** (0..65535) |

Hinweis: Bei **Kein Langdruck** wird für diese Taste kein Langdruck-KO in ETS eingeblendet.

<!-- DOC -->
### Drehregler

Wenn der Hue-Schalter über einen Drehregler verfügt (z. B. Hue Tap Dial):

- **Drehregler vorhanden**: Aktiviert die Drehregler-Konfiguration
- **Funktion**: Legt fest, welche KNX-Funktion der Drehregler auslöst:
  - **Dimmen** (DPT 3.007): Relatives Dimmen
  - **Wertgeber** (DPT 5.001): Absoluter Prozentwert
  - **Lautstärke** (DPT 3.007): Lautstärkeregelung (relatives Dimmen auf Medien-KO)
  - **Farbtemperatur** (DPT 7.600): Farbtemperatur in Kelvin
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

Latenz-Hinweis:
- `Nur KNX zu Hue`: direkte KNX-Steuerung, aber keine Hue-Rückmeldung.
- `Nur Hue zu KNX`: Statusänderungen werden nur mit Polling-/Abfrage-Latenz auf KNX sichtbar.
- `Bidirektional`: meist beste Alltagswahl; Änderungen aus App oder Direktbedienung erscheinen dennoch nicht instantan, sondern gemäß Abfrageintervall.

Empfehlung: Für einzelne Leuchten meist `Bidirektional`, für reine Statusobjekte oder Monitoring-Kanäle auch `Nur Hue zu KNX`.

<!-- DOC -->
### Polling-Intervall

Status-Abfrageintervall in Sekunden.
Empfehlung: `5..30 s` je nach Kanalzahl und Netzlast.

Wichtig:
- `0` = zyklisches Polling für diesen Kanal deaktiviert.
- Bei `>0` wird der Kanal gemäß Intervall aus Hue gelesen (abhängig von Sync-Richtung).
- Nach KNX-Kommandos erfolgt zusätzlich ein kurzer Fast-Track-Statusabgleich.

Praxisempfehlungen:
- Einzelne Lampen: meist `5..15 s`
- Räume oder Zonen: meist `15..30 s`
- Viele aktive Kanäle: größere Werte wählen, um Bridge und Netzwerk zu entlasten

Zu kleine Werte bringen in großen Projekten oft keinen echten Mehrwert, erzeugen aber mehr HTTP-Last auf der Hue-Bridge.

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

#### Szene A..H (Slots)

Jeder Slot kann unabhängig parametriert werden. Ist die **Szenennummer** auf `0` (inaktiv) gesetzt, wird der Slot ignoriert.

**Szenennummer**: KNX-Szenennummer 1..64 (entspricht Bit 0..5 im DPT, also KNX-intern 0..63)

**Aktion**: Legt fest, was beim Abruf dieser Szene passiert. Die verfügbaren Optionen hängen vom **Lampentyp** des Kanals ab.

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

#### DPT 18.001 kurz erklärt

DPT 18.001 erweitert den reinen Szenenabruf um eine Speicherfunktion:

- Bit 7 = `0`: Szene abrufen
- Bit 7 = `1`: Szene speichern
- Bits 0..5: Szenennummer `1..64`

Beispiel:

- GA `2/1/10` sendet `Szene 4 abrufen` -> HueGateway führt den konfigurierten Slot 4 aus.
- Dieselbe GA sendet `Szene 4 speichern` -> HueGateway speichert den aktuellen Istzustand in Slot 4, wenn `Szene speichern` aktiviert ist.

#### Empfehlung für die Praxis

- Für klassische Tastszenen denselben GA-Typ konsequent für Abruf und optionales Speichern verwenden.
- Bei Kanälen mit Lichtmanager prüfen, ob nach dem Szenenabruf eine Sperre oder Rückfallstrategie gewünscht ist.
- Für Steckdosen gibt es eine eigene reduzierte Szenensteuerung mit Ein-/Aus-Aktionen.

<!-- DOC -->
### Szenensteuerung Steckdose

Aktiviert die Szenensteuerung für einen Kanal mit Gerätetyp **Steckdose**.

- **Szenensteuerung aktivieren**: Blendet das Szenen-KO sowie den Szenen-Unterreiter für die Steckdose ein.
- **Szene speichern**: Erlaubt zusätzlich zu Abrufbefehlen auch das Speichern des aktuellen Schaltzustands per DPT 18.001.

Hinweis:
- Bei Steckdosen werden nur Schaltzustände gespeichert und wieder abgerufen.
- Helligkeit, Farbtemperatur, RGB und Hue-Szenen sind für Steckdosen nicht verfügbar.

<!-- DOC -->
### Szenen Steckdose

Für Steckdosen stehen pro Kanal bis zu **8 Szenen-Slots** zur Verfügung.

Jeder Slot besitzt:
- **Szenennummer**: KNX-Szenennummer `1..64`
- **Aktion**: nur `Ausschalten` oder `Einschalten`

Hinweise:
- Es gibt keine zusätzlichen Parameter für Helligkeit, Farbtemperatur, RGB oder Hue-Szene.
- Wenn **Szene speichern** aktiviert ist, wird beim Speichern nur der aktuelle Ein-/Aus-Zustand der Steckdose persistent abgelegt.
- Die ETS-Hilfe für Steckdosen-Szenen ist bewusst getrennt von der Leuchten-Szenensteuerung, damit keine fachlich falschen Hinweise zu CT/RGB oder Hue-Szenen angezeigt werden.

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
Nur die hier aktivierten Lichtmanager werden als eigene ETS-Reiter eingeblendet.

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

### Sperr-Hierarchie

Die Sperren werden mit folgender Priorität ausgewertet:

```text
Kanal-Sperre
	> Manager-Sperre
		> Globale Sperre
```

Eine aktive Kanal-Sperre übersteuert also immer die managerbezogene und die globale Sperre. Das ist gewollt, damit einzelne Kanäle nach einer Szene oder einem Sonderbetrieb gezielt aus der HCL-Führung herausgenommen werden können, ohne andere Kanäle desselben Lichtmanagers zu beeinflussen.

<!-- DOC HelpContext="HCL-Sperre-global" -->
Sperrt die automatische Ausgabe aller Lichtmanager (HCL-Bereich).

Optionen:
- **Rückfallzeit nach Sperre** (inkl. Tageswechsel, `kein Rückfall` möglich)
- **Rückfallstrategie nach Sperre**: wirkt für globale, manager-spezifische und kanal-spezifische Sperren

KOs:
- `Sperre (global)` (Eingang)
- `Status Sperre` (Ausgang)
<!-- DOCEND -->

<!-- DOC HelpContext="HCL-Sperre-global-Status-HCL-Sperre" -->
Globale HCL-Sperre inkl. Statusrückmeldung.

- KO `Sperre (global)` (Eingang): Setzt die globale HCL-Sperre für alle Lichtmanager.
- KO `Status Sperre` (Ausgang): Gibt den aktuellen HCL-Sperrstatus zurück.
<!-- DOCEND -->

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

<!-- DOC HelpContext="HCL-Manager-18" -->
Jeder Lichtmanager 1..8 besitzt identischen Aufbau (HCL-Konfiguration):

- **Bezeichnung**: Freie ETS-Bezeichnung des Lichtmanagers.
- **Lichtmanager Sperre (spezifisch)**: Sperrt nur den jeweiligen Manager.
- **Erweiterte Kurve**: Kurventyp `FixedTime`, `SunPosition`, `Manual` oder `Astronomischer Sonnenstand`.
- **Stützpunkte**: Bis zu 10 Stützpunkte je Manager (bei `FixedTime` oder `SunPosition`).
- **Saison-Profil**: Optionale Sommer-/Winter-Stützpunkte (Modus `Standard`, `Auto-DST`, `Festes Datum` oder `Per Objekt`).
<!-- DOCEND -->

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
- Nicht alle 10 Stützpunkte müssen belegt werden; unbenutzte Einträge werden ignoriert.
- Bei `Manual` sind Stützpunkte optional; wenn sie gesetzt werden, definieren sie Zeit + Helligkeit, die Farbtemperatur kommt aus dem Parameter **Manuelle Farbtemperatur**.
- Bei `Manual` ohne Stützpunkte bleibt die Helligkeit konstant auf `100 %`, die Farbtemperatur auf dem konfigurierten manuellen Kelvin-Wert.
- Bei `Astronomischer Sonnenstand` werden keine Stützpunkte verwendet; stattdessen werden Minimal- und Maximalwerte für Kelvin und Helligkeit genutzt.
- Die letzte Zeit eines Tages gilt bis zum ersten Stützpunkt des nächsten Tages.

Beispiel:
- SP1 `06:00 / 3000K / 30%`
- SP2 `12:00 / 5000K / 90%`
- SP3 `20:00 / 2400K / 35%`

Praxisregel:
- `Aktualisierungsintervall`, `Überblendzeit` und `Slew-Rate` gemeinsam abstimmen, damit Übergänge ruhig bleiben.

#### Saison-Profil

Das Saison-Profil ermöglicht es, für jeden Lichtmanager zwei voneinander unabhängige Stützpunkt-Sätze zu hinterlegen: einen für **Sommer** und einen für **Winter**. Der Lichtmanager wechselt automatisch oder auf KNX-Befehl zwischen den beiden Profilen.

**Hintergrund**: Im Sommer steht die Sonne bei Sonnenuntergang (z. B. 19:00 Uhr) noch hoch, der Himmel ist hell und das Auge nimmt warmes Licht als „zu gelb" wahr. Im Winter hingegen ist die Dämmerung um dieselbe Uhrzeit längst abgeschlossen — warmes Licht fühlt sich natürlicher an. Mit dem Saison-Profil können beide Situationen optimal parametriert werden, ohne zwei separate Lichtmanager anlegen zu müssen.

##### Saison-Modus

Der Parameter **Saison-Modus** legt fest, wie der Wechsel zwischen Sommer- und Winter-Stützpunkten ausgelöst wird:

| Modus | Beschreibung |
|---|---|
| **Standard** | Immer Winter-Stützpunkte aktiv. Sommer-Stützpunkte werden ignoriert. |
| **Auto-DST** | Automatischer Wechsel anhand der mitteleuropäischen Sommerzeit (MESZ). Sommer = letzter Sonntag März bis letzter Sonntag Oktober. Kein ETS-Eingriff nötig. |
| **Festes Datum** | Sommer gilt zwischen zwei konfigurierbaren Daten (Tag+Monat). Ermöglicht individuelle Anpassung an lokale Verhältnisse oder persönliche Präferenzen. |
| **Per Objekt** | Das KNX-Kommunikationsobjekt **LM x: Sommer aktiv** steuert den Wechsel. Ermöglicht externe Steuerung z. B. über einen Kalender-Aktor oder eine Logik. |

Bei Modus **Standard** sind keine Sommer-Stützpunkte erforderlich; die Spalten werden in ETS ausgeblendet.

##### Parameter bei Modus „Festes Datum"

- **Sommer Start (Tag)** / **Sommer Start (Monat)**: Beginn des Sommerprofils (inklusiv).
- **Sommer Ende (Tag)** / **Sommer Ende (Monat)**: Ende des Sommerprofils (inklusiv).
- **DST-Offset (Tage)**: Optionaler Vorlauf/Nachlauf in Tagen (Bereich `-30..+30`, Standard `0`).

Beispiel: Start `01.04.`, Ende `31.10.` entspricht grob der MESZ — identisch mit Auto-DST, aber manuell justierbar.

##### Sommer-Stützpunkte

Bei aktivem Saison-Modus (nicht `Standard`) erscheint in ETS für jeden Stützpunkt eine zweite Spalte:

| Spalte | Beschreibung |
|---|---|
| **Sommer Aktiv** | Checkbox: Stützpunkt im Sommer-Profil verwenden. Inaktive Stützpunkte werden ignoriert. |
| **Sommer Kelvin** | Farbtemperatur für diesen Stützpunkt im Sommer (2000–6500 K). |
| **Sommer Helligkeit** | Helligkeit für diesen Stützpunkt im Sommer (0–100 %). |

Zeit und Sichtbarkeit des Stützpunkts bleiben für beide Profile gleich — nur Kelvin und Helligkeit werden saisonal überschrieben.

Hinweise:
- Nicht alle Stützpunkte müssen einen Sommer-Wert haben. Stützpunkte mit deaktiviertem **Sommer Aktiv** werden im Sommer-Profil übersprungen.
- Im Sommer-Profil müssen mindestens 2 aktive Stützpunkte vorhanden sein (bei Kurventyp `FixedTime` oder `SunPosition`), sonst fällt der Manager auf das Winter-Profil zurück.

##### KO: LM x: Sommer aktiv (nur Modus „Per Objekt")

<!-- DOC HelpContext="HCL-Saison-KO" -->
Kommunikationsobjekt **LM x: Sommer aktiv** (Eingang, 1 Bit, DPT 1.001).

Nur sichtbar wenn der Saison-Modus des Lichtmanagers auf **Per Objekt** eingestellt ist.

- Wert `1` = Sommer-Stützpunkte aktiv
- Wert `0` = Winter-Stützpunkte aktiv (Standard)

Bei den anderen Saison-Modi (Standard, Auto-DST, Festes Datum) wechselt der Manager automatisch; dieses KO ist dann nicht sichtbar.
<!-- DOCEND -->

### Lichtmanager-Konfiguration übertragen (ConfigTransfer)

Für wiederkehrende HCL-Szenarien kann die Modul-Basiskonfiguration über das OpenKNX ConfigTransfer-Modul importiert werden. Die folgenden Beispiele aktivieren den Lichtmanager global und schreiben ein Profil in **Lichtmanager 1**.

Vorgehen:

1. Im ConfigTransfer-Modul als Ziel das Hue-Modul wählen.
2. Import-Ziel auf **Modul-Basiskonfiguration** setzen.
3. Den gewünschten String in das Feld **Transfer-String** einfügen.
4. Import ausführen.
5. Anschließend die gewünschten Hue-Kanäle auf **Lichtmanager 1** zuordnen.

#### Büro-Profil

```text
OpenKNX,cv1,*/HUE/0§HUEHCLEnable=1§HUEHCLMasterCount=1§HCLM1Name=Buero§HCLM1CurveType=0§HCLM1SetpointCount=5§HCLM1SP0Time=06%3A00§HCLM1SP0Kelvin=5000§HCLM1SP0Brightness=40§HCLM1SP1Time=09%3A00§HCLM1SP1Kelvin=4600§HCLM1SP1Brightness=70§HCLM1SP2Time=12%3A00§HCLM1SP2Kelvin=4500§HCLM1SP2Brightness=85§HCLM1SP3Time=17%3A00§HCLM1SP3Kelvin=3500§HCLM1SP3Brightness=55§HCLM1SP4Time=20%3A30§HCLM1SP4Kelvin=3000§HCLM1SP4Brightness=25§;OpenKNX
```

#### Wohnzimmer-Profil

```text
OpenKNX,cv1,*/HUE/0§HUEHCLEnable=1§HUEHCLMasterCount=1§HCLM1Name=Wohnzimmer§HCLM1CurveType=0§HCLM1SetpointCount=4§HCLM1SP0Time=07%3A00§HCLM1SP0Kelvin=3000§HCLM1SP0Brightness=25§HCLM1SP1Time=12%3A00§HCLM1SP1Kelvin=3000§HCLM1SP1Brightness=45§HCLM1SP2Time=18%3A00§HCLM1SP2Kelvin=2400§HCLM1SP2Brightness=35§HCLM1SP3Time=22%3A30§HCLM1SP3Kelvin=2200§HCLM1SP3Brightness=15§;OpenKNX
```

#### Schlafzimmer-Profil

```text
OpenKNX,cv1,*/HUE/0§HUEHCLEnable=1§HUEHCLMasterCount=1§HCLM1Name=Schlafzimmer§HCLM1CurveType=0§HCLM1SlewRate=4§HCLM1SetpointCount=4§HCLM1SP0Time=06%3A30§HCLM1SP0Kelvin=2700§HCLM1SP0Brightness=20§HCLM1SP1Time=12%3A00§HCLM1SP1Kelvin=3500§HCLM1SP1Brightness=45§HCLM1SP2Time=19%3A30§HCLM1SP2Kelvin=2500§HCLM1SP2Brightness=25§HCLM1SP3Time=22%3A30§HCLM1SP3Kelvin=2200§HCLM1SP3Brightness=8§;OpenKNX
```

Hinweise:
- Die Strings sind absichtlich kanalunabhängig für die Modul-Basiskonfiguration formuliert.
- Bereits vorhandene Lichtmanager-Einstellungen in der Zielkonfiguration werden überschrieben.
- Für zusätzliche Manager die Parameternamen entsprechend auf `HCLM2...`, `HCLM3...` usw. anpassen.

## Performance-Empfehlungen

Die Last auf Hue-Bridge und OpenKNX-Gerät steigt vor allem durch viele aktive Kanäle, kurze Polling-Intervalle und häufige Statusabfragen.

Empfehlungen für typische Projekte:

| Szenario | Kanalzahl | Polling | Empfehlung |
|---|---|---|---|
| Einzelraum mit wenigen Leuchten | 1-5 | `5..10 s` | `Bidirektional` für Bedienkomfort |
| Mehrere Räume/Zonen | 6-15 | `10..20 s` | Räume/Zonen nur dort nutzen, wo Sammelstatus ausreicht |
| Große Installation | >15 | `15..30 s` | Nur wirklich benötigte Kanäle und Status-KOs aktivieren |

Faustregeln:
- Nicht benötigte Kanäle deaktivieren.
- Status-KOs nur einblenden, wenn sie im KNX-Projekt wirklich verwendet werden.
- Räume/Zonen bevorzugen, wenn nicht jede Einzelleuchte separat benötigt wird.
- Bei vielen HCL-Kanälen `Aktualisierungsintervall` und `Überblendzeit` konservativ wählen.

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

### Beispiel 6: Lichtmanager mit Saison-Profil (Auto-DST)
- Lampentyp: Farbtemperatur
- Lichtmanager: 1
- Saison-Modus: Auto-DST
- Winter-Stützpunkte: SP1 `06:30 / 3000K / 30%`, SP2 `12:00 / 5000K / 80%`, SP3 `19:00 / 2700K / 60%`, SP4 `22:00 / 2200K / 20%`
- Sommer-Stützpunkte (SP1–SP4 jeweils Sommer Aktiv = Ja): SP1 `06:30 / 4000K / 40%`, SP2 `12:00 / 5500K / 90%`, SP3 `19:00 / 3800K / 70%`, SP4 `22:00 / 2700K / 25%`
- Hinweis: Im Sommer ist SP3 um 19 Uhr deutlich kühler (3800K statt 2700K), weil das Umgebungslicht noch hell ist.

### Beispiel 7: Saison-Umschaltung per KNX-Logik (Modus „Per Objekt")
- Saison-Modus: Per Objekt
- KO `LM 1: Sommer aktiv` mit Ausgang einer Logik verbinden, die aus Datum/Uhrzeit den Sommer erkennt
- Oder: KO an einen Taster hängen, der manuell zwischen Sommer/Winter umschaltet
- Vorteil: Vollständige externe Kontrolle; z. B. auch Zwischensaison-Profile möglich

<!-- DOC -->
## Häufige Fehler und Lösungen

### Bridge wird nicht gefunden
- mDNS/VLAN prüfen oder auf manuelle IP wechseln.
- Bei getrennten Netzsegmenten funktioniert die automatische Suche meist nicht.

### Authentifizierung schlägt fehl
- Pairing-Fenster abgelaufen → neu triggern und Link-Button erneut drücken.

### Hue-Ziel reagiert nicht
- Zieltyp und **Hue Ziel (Light-/Room-/Zone-ID)** prüfen.
- Bei Legacy-Projektierung zusätzlich **Hue Lampen-ID (UUID)** prüfen.
- Kanal deaktiviert?
- Sync-Richtung passend?

### Lichtmanager-Parameter oder HCL-KOs fehlen
- Lichtmanager global aktiviert?
- Kanal ist wirklich ein CT- oder RGB-Kanal?
- Erst nach Aktivierung des globalen Lichtmanagers werden Zuordnung und Sperrparameter sichtbar.

### Status fehlt
- Sync auf **Nur Hue zu KNX** oder **Bidirektional** gesetzt?
- Polling-Intervall sinnvoll gesetzt (`0` deaktiviert zyklisches Polling)?
- Status-KO mit GA verbunden?
- Zieltyp/Hue Ziel korrekt und auflösbar?
- Bei Raum/Zone: Rückmeldeverhalten mit Hue-App-Änderungen gesondert verifizieren.

### Raum/Zone meldet unerwartete Werte
- Bei Raum- und Zonen-Zielen bildet der Status nicht immer den exakten Zustand jedes Einzelgeräts ab.
- Dieses Verhalten ist systembedingt und sollte im Projekt mit dem gewünschten Zieltyp getestet werden.

### Lichtmanager wirkt nicht
- Lichtmanager global aktiviert?
- Manager zugewiesen?
- Bei `FixedTime`/`SunPosition`: mind. 2 gültige Stützpunkte?
- Bei `Manual`: gewünschte manuelle Farbtemperatur gesetzt und optionaler Helligkeitsverlauf passend parametriert?
- Bei `Astronomischer Sonnenstand`: sinnvolle Astro-Min/Max-Werte gesetzt?
- Globale/spezifische Sperre aktiv?

### Hue-Status kommt stark verzögert an
- Polling-Intervall zu hoch?
- Bei vielen Kanälen bewusst größere Werte gesetzt?
- Hue-App-Änderungen werden nicht sofort gepusht, sondern gemäß Abfrageintervall übernommen.

### Szenen reagieren nicht wie erwartet
- DPT 18.001 korrekt verwendet?
- Richtige Szenennummer im Slot hinterlegt?
- `Szene speichern` nur aktivieren, wenn Speicherbefehle wirklich genutzt werden.
- Für Steckdosen sind keine Szenen verfügbar.

### Saison-Profil schaltet nicht um
- Saison-Modus ist `Standard`? → dann sind Sommer-Stützpunkte absichtlich deaktiviert.
- Bei Modus `Festes Datum`: Start- und Ende-Datum korrekt eingetragen? Datum liegt im aktiven Bereich?
- Bei Modus `Auto-DST`: Systemzeit korrekt? DST-Erkennung setzt korrekte Uhrzeit voraus.
- Bei Modus `Per Objekt`: KO `LM x: Sommer aktiv` mit GA verbunden und Wert `1` gesendet?
- Im Sommer-Profil mindestens 2 Stützpunkte mit **Sommer Aktiv = Ja** vorhanden (bei `FixedTime`/`SunPosition`)?

<!-- DOC -->
## Inbetriebnahme-Checkliste

- Bridge-Erkennung passend gewählt: mDNS im selben VLAN oder manuelle IP bei Segmentierung
- Bridge gefunden, authentifiziert und Verbindungsstatus geprüft
- Anzahl aktiver Kanäle passend eingestellt
- Zieltyp und Hue Ziel pro Kanal geprüft
- Gerätetyp und Lampentyp passend zur realen Hardware
- Sync-Richtung und Polling passend zur Anwendung parametriert
- Benötigte Steuer- und Status-KOs mit GAs verbunden
- Bei Raum/Zone das gewünschte Rückmeldeverhalten getestet
- Lichtmanager global aktiviert, Manager-Anzahl geprüft und Kanäle sauber zugeordnet
- Lichtmanager-Funktion inkl. globaler, managerbezogener und kanalspezifischer Sperren getestet
- Szenensteuerung: Szenennummern, Aktionen, Preset-Werte und optionales Speichern geprüft
- Bei Szene + Lichtmanager: Verhalten nach Aus-Befehl bzw. Entsperren verifiziert
- Bei Taster/Schalter: Gewerk, Kurz-/Langdruck und Native-Hue-Aktion geprüft
- Bei aktivem Saison-Profil: Saison-Modus gewählt, Sommer-Stützpunkte eingetragen, Umschaltung im laufenden Betrieb verifiziert

<!-- DOC -->
## Lizenz und Haftung

Open-Source-Modul im OpenKNX-Umfeld.
Keine Gewährleistung; Nutzung in eigener Verantwortung.
Philips Hue ist ein Warenzeichen von Signify N.V.
