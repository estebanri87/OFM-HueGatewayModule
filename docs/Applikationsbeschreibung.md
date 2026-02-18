# Applikationsbeschreibung Philips Hue Gateway Modul

Das OFM-HueGatewayModule verbindet Philips Hue Leuchten mit dem KNX-Bus.
Die Steuerung erfolgt über eine Hue Bridge, die im lokalen Netzwerk verfügbar ist.
Pro KNX-Kanal wird eine Hue-Leuchte gesteuert mit Funktionen wie Ein/Aus, Helligkeit, Farbtemperatur und RGB-Farbe - abhängig vom gewählten Lampentyp.

**Wichtiger Hinweis zur Projektierung:**
- Das Modul arbeitet mit der Hue API v2.
- Parameter und Kommunikationsobjekte müssen konsistent in ETS gepflegt werden (insbesondere Synchronisationsrichtung, Polling und Status-KOs).

## Schnellstart (5 Schritte)

1. **Bridge-Erkennung wählen** (automatisch oder manuell per IP).
2. **Authentifizieren** (Link-Button an der Hue Bridge innerhalb des Pairing-Fensters drücken).
3. **Anzahl aktiver Kanäle** setzen (nur tatsächlich benötigte Kanäle).
4. **Pro Kanal** UUID, Lampentyp und Synchronisationsrichtung konfigurieren.
5. **Gruppenadressen** für Eingangs- und Status-KOs zuordnen und Funktionstest durchführen.

**Schnelltest nach Download:**
- `Schalten` senden → Lampe reagiert.
- Änderung in Hue-App → Status-KOs werden (je nach Sync/Polling) aktualisiert.

## Funktionsumfang

### Bridge-Erkennung und Authentifizierung

Das Modul bietet zwei Methoden zur Verbindung mit der Hue Bridge:

- **Automatische Erkennung (mDNS)**: Die Hue Bridge wird automatisch im lokalen Netzwerk gesucht
- **Manuelle IP-Adresse**: Die IP-Adresse der Hue Bridge wird manuell in der ETS-Konfiguration angegeben

Die Authentifizierung erfolgt durch Drücken des Link-Buttons an der Hue Bridge. Der generierte App-Key wird im nichtflüchtigen Speicher des ESP32 gespeichert und bleibt auch nach Neustarts erhalten.

### Lichtsteuerung pro Kanal

Jeder Kanal steuert eine Hue-Leuchte und bietet - abhängig vom konfigurierten Lampentyp - folgende Funktionen:

**Lampentyp "Nur schalten":**
- Ein/Aus-Steuerung

**Lampentyp "Dimmbar":**
- Ein/Aus-Steuerung
- Absolute Helligkeitsvorgabe (0-100%)
- Relatives Dimmen (heller/dunkler)

**Lampentyp "Farbtemperatur":**
- Ein/Aus-Steuerung  
- Absolute Helligkeitsvorgabe (0-100%)
- Relatives Dimmen (heller/dunkler)
- Farbtemperatur in Kelvin (2000-6500K)

**Lampentyp "Farbe (RGB)":**
- Ein/Aus-Steuerung
- Absolute Helligkeitsvorgabe (0-100%)
- Relatives Dimmen (heller/dunkler)
- Farbtemperatur in Kelvin (2000-6500K)
- RGB-Farbsteuerung

### Synchronisation

Die Synchronisation kann pro Kanal in vier Modi betrieben werden:

- **Keine Synchronisation**: Keine automatische Statusrückmeldung
- **Nur KNX zu Hue**: KNX-Befehle werden an Hue gesendet, aber keine Statusrückmeldung
- **Nur Hue zu KNX**: Status wird von Hue gelesen und auf KNX gesendet (Polling)
- **Bidirektional**: Volle Synchronisation in beide Richtungen

### HCL-Manager (Human Centric Lighting)

Der HCL-Manager ermöglicht zeitabhängige Verläufe für Farbtemperatur und Helligkeit über bis zu 4 HCL-Master.

**Kurventypen:**

- **FixedTime (Stützpunkte)**  
   Verwendet Stützpunkte mit **Zeit, Kelvin, Helligkeit**.

- **SunPosition**  
   Verwendet ebenfalls Stützpunkte mit **Zeit, Kelvin, Helligkeit** und zusätzlich Sunrise/Sunset inkl. Offsets.

- **Manual Kelvin**  
   Die Farbtemperatur wird fest über den Parameter "Manual Kelvin" vorgegeben.  
   In den Stützpunkten werden in ETS nur **Zeit und Helligkeit** angezeigt (Kelvin ist ausgeblendet).

**Wichtiger Hinweis:**
- Für einen gültigen HCL-Master werden weiterhin mindestens **2 gültige Stützpunkte** benötigt.
- Im Modus **Manual Kelvin** dienen die Stützpunkte damit der Helligkeitskurve.

### Diagnose und Monitoring

- **LED-Signalisierung**: Zeigt den Verbindungsstatus zur Hue Bridge an
- **Statusobjekt**: Optionales KNX-Kommunikationsobjekt für die Bridge-Verbindung
- **Web-Interface**: Statusanzeige unter `http://<ip-adresse>:<port>/`
- **Konsolenbefehle**: `hue scan` und `hue status` für Diagnose

# Applikationsprogramm

<!-- DOC -->
## Allgemein

(c) OpenKNX, Steffen Rittmeier 2026

Die vollständige Anwendungsbeschreibung ist im Web unter https://github.com/OpenKNX/OFM-HueGatewayModule zu finden.

Das OFM-HueGatewayModule ist ein OpenKNX-Modul zur Integration von Philips Hue Beleuchtung in KNX-Anlagen. Es verwendet die Hue API v2 für eine zuverlässige und performante Kommunikation mit der Hue Bridge.

**Datenfluss (vereinfacht):**

`KNX Telegramm` → `OFM-HueGatewayModule` → `Hue Bridge API` → `Hue-Leuchte`

und für Rückmeldungen:

`Hue-Leuchte/Bridge` → `OFM-HueGatewayModule` → `KNX Status-KO`

**Abgrenzung:**
- Das Modul übernimmt die Kopplung zwischen KNX und Hue.
- Logikverknüpfungen (z.B. Zentralfunktionen, Zeitlogik) sollten weiterhin in dafür vorgesehenen KNX-/OpenKNX-Logikmodulen erfolgen.


<!-- DOC -->
## Bridge-Konfiguration

<!-- DOC -->
### Bridge Erkennung

Legt fest, wie die Hue Bridge gefunden wird:

- **Automatisch (mDNS)**  
  Die Hue Bridge wird automatisch im lokalen Netzwerk über mDNS (Multicast DNS) gesucht. Dies ist die empfohlene Einstellung für die meisten Installationen, da keine manuelle Konfiguration der IP-Adresse erforderlich ist.

- **Manuelle IP-Adresse**  
  Die IP-Adresse der Hue Bridge wird manuell eingegeben. Diese Option sollte gewählt werden, wenn:
  - mDNS im Netzwerk nicht funktioniert (z.B. bei getrennten VLANs)
  - eine statische IP-Adresse für die Bridge konfiguriert ist
  - die automatische Erkennung aus anderen Gründen nicht zuverlässig funktioniert

<!-- DOC -->
### Bridge IP-Adresse

<!-- DOC Skip="2" -->
Diese Einstellung ist nur sichtbar, wenn "Bridge Erkennung" auf "Manuelle IP-Adresse" gesetzt ist.

Geben Sie hier die IPv4-Adresse Ihrer Hue Bridge ein (z.B. 192.168.1.100).

**Hinweis:** Die IP-Adresse muss im gleichen Netzwerk wie das OpenKNX-Gerät liegen oder über Routing erreichbar sein.

<!-- DOC -->
### Erstinbetriebnahme und Authentifizierung

Bei der ersten Inbetriebnahme muss das Modul mit der Hue Bridge authentifiziert werden. Der Ablauf unterscheidet sich je nach gewählter Erkennungsmethode:

#### Automatische Erkennung (mDNS):

1. Laden Sie die Konfiguration per ETS-Download auf das OpenKNX-Gerät
2. Das Gerät startet neu und sucht automatisch nach der Hue Bridge im Netzwerk
3. Während der Suche blinkt die FUNC-LED (Info1) schnell blau (ca. 5 Hz / 0,2 Sekunden)
4. Sobald die Bridge gefunden wurde, blinkt die FUNC-LED (Info1) langsam blau (ca. 1 Hz / 1 Sekunde) für ca. 30 Sekunden
5. **JETZT: Drücken Sie den Link-Button an der Hue Bridge** (innerhalb von 30 Sekunden)
6. Das Gerät authentifiziert sich automatisch
7. Bei erfolgreicher Verbindung leuchtet die FUNC-LED (Info1) dauerhaft grün
8. Der App-Key wird im Speicher abgelegt und bleibt auch nach Neustarts erhalten

#### Manuelle IP-Adresse:

1. Geben Sie die IP-Adresse der Hue Bridge in der ETS ein
2. Laden Sie die Konfiguration per ETS-Download auf das OpenKNX-Gerät
3. Das Gerät startet neu und verbindet sich mit der eingegebenen IP-Adresse
4. Bei erstmaliger Verbindung blinkt die FUNC-LED (Info1) langsam blau (ca. 1 Hz / 1 Sekunde) für ca. 30 Sekunden
5. **JETZT: Drücken Sie den Link-Button an der Hue Bridge** (innerhalb von 30 Sekunden)
6. Das Gerät authentifiziert sich automatisch
7. Bei erfolgreicher Verbindung leuchtet die FUNC-LED (Info1) dauerhaft grün
8. Der App-Key wird im Speicher abgelegt und bleibt auch nach Neustarts erhalten

#### LED-Signale im Überblick:

- **FUNC-LED (Info1) blau schnell blinkend (0,2s)**: Sucht Bridge im Netzwerk (nur bei automatischer Erkennung)
- **FUNC-LED (Info1) blau langsam blinkend (1s)**: Wartet auf Link-Button - **JETZT an der Bridge drücken!**
- **FUNC-LED (Info1) cyan schnell blinkend (0,2s)**: Authentifizierung läuft
- **FUNC-LED (Info1) grün dauerhaft**: Verbunden und betriebsbereit
- **FUNC-LED (Info1) rot blinkend (0,5s)**: Verbindung zur Bridge verloren
- **FUNC-LED (Info1) rot blinkend (1,5s)**: Bridge nicht erreichbar / nicht konfiguriert

Hinweis: Die Prog-LED bleibt unverändert im Standardverhalten des OpenKNX-Frameworks.

<!-- DOC -->
### Authentication zurücksetzen

Mit dieser Option kann die gespeicherte Authentifizierung gelöscht werden.

**Wann wird diese Option benötigt?**
- Bei Wechsel zu einer anderen Hue Bridge
- Wenn der App-Key an der Hue Bridge gelöscht wurde
- Bei Authentifizierungsproblemen

**Achtung:** Nach dem Zurücksetzen muss die Authentifizierung neu durchgeführt werden (Link-Button an der Bridge drücken).

<!-- DOC -->
### Status Verbindung

Aktiviert ein zusätzliches Kommunikationsobjekt "Bridge Verbindungsstatus", das den aktuellen Verbindungsstatus zur Hue Bridge anzeigt:

- **0 (AUS)**: Nicht verbunden oder Verbindung unterbrochen
- **1 (EIN)**: Verbunden und betriebsbereit

Dieses Objekt kann für Diagnose in der Visualisierung oder für Logikfunktionen genutzt werden (z.B. Benachrichtigung bei Verbindungsabbruch).

<!-- DOC -->
### HTTP Server Port

Legt den Port für den integrierten HTTP-Server fest (Standard: 80).

Der HTTP-Server bietet:
- Statusseite unter `http://<ip-adresse>:<port>/`
- Bridge-Scan unter `http://<ip-adresse>:<port>/hue/scan`
- Pairing-Workflow unter `http://<ip-adresse>:<port>/hue/pair`
- Status-Informationen unter `http://<ip-adresse>:<port>/hue/status`
- Text-Export des Bridge-Scans unter `http://<ip-adresse>:<port>/hue/scan.txt`

**Hinweis:** Wenn Port 80 bereits durch einen anderen Dienst belegt ist, wählen Sie einen anderen Port (z.B. 8080).

<!-- DOC -->
### Anzahl aktiver Kanäle

Legt fest, wie viele Lampen-Kanäle konfiguriert werden sollen (0-20).

Für jede Lampe, die gesteuert werden soll, wird ein Kanal benötigt. Die Kanaleinstellungen erscheinen erst, nachdem hier ein Wert größer 0 eingestellt wurde.

**Empfehlung:** Setzen Sie diese Zahl auf die tatsächliche Anzahl der zu steuernden Hue-Leuchten. Nicht verwendete Kanäle verursachen unnötigen Netzwerkverkehr.

<!-- DOC HelpContext="Kanal" -->
## Kanal 1-n (Lampen)

Jeder Kanal repräsentiert eine einzelne Hue-Leuchte. Die Anzahl der sichtbaren Kanäle wird durch die Einstellung "Anzahl aktiver Kanäle" bestimmt.

<!-- DOC -->
### Kanalbezeichnung

Die Bezeichnung wird innerhalb der ETS verwendet, um den Kanal und die Kommunikationsobjekte zu benennen.

**Empfehlung:** Verwenden Sie eine aussagekräftige Bezeichnung, die den Raum und/oder die Position der Lampe beschreibt.

**Beispiele:**
- Wohnzimmer Stehlampe
- Küche Deckenlampe
- Schlafzimmer Links
- Flur LEDs

Die Bezeichnung erscheint in der ETS bei den Kommunikationsobjekten und erleichtert die Zuordnung der Gruppenadressen.

<!-- DOC -->
### Hue Lampen-ID (UUID)

Die eindeutige ID (UUID) der Hue-Leuchte, die über diesen Kanal gesteuert werden soll.

**Wie finde ich die UUID meiner Lampe?**

Es gibt mehrere Möglichkeiten:

1. **Über die Web-Oberfläche des Moduls:**
   - Öffnen Sie `http://<ip-adresse>:<port>/hue/scan` im Browser
   - Es werden alle verfügbaren Hue-Leuchten mit ihren UUIDs angezeigt
   - Kopieren Sie die UUID der gewünschten Lampe

2. **Über die Konsole:**
   - Verbinden Sie sich per serieller Konsole mit dem Gerät
   - Geben Sie den Befehl `hue scan` ein
   - Es werden alle Lampen mit ihren UUIDs aufgelistet

3. **Über die Hue API:**
   - Rufen Sie `https://<bridge-ip>/clip/v2/resource/light` auf (mit Header `hue-application-key: <app-key>`)
   - Die UUID findet sich im `id`-Feld jeder Lampe

**Format:** Die UUID hat das Format `01234567-89ab-cdef-0123-456789abcdef` (8-4-4-4-12 Hexadezimalzeichen).

<!-- DOC -->
### Lampentyp

Legt fest, welche Steuerungsmöglichkeiten für diese Lampe zur Verfügung stehen.

Wählen Sie den Typ entsprechend den Fähigkeiten Ihrer Hue-Leuchte:

- **Nur schalten (ein/aus)**  
  Für einfache Leuchten ohne Dimmfunktion (z.B. Schaltsteckdosen, einfache Lampen).  
  **Verfügbare Funktionen:** Ein/Aus

- **Dimmbar**  
  Für dimmbare Weißlicht-Leuchten ohne Farbtemperaturregelung.  
  **Verfügbare Funktionen:** Ein/Aus, Helligkeit (0-100%), relatives Dimmen

- **Farbtemperatur**  
  Für Leuchten mit einstellbarer Farbtemperatur (z.B. "White Ambiance").  
  **Verfügbare Funktionen:** Ein/Aus, Helligkeit (0-100%), relatives Dimmen, Farbtemperatur (2000-6500K)

- **Farbe (RGB)**  
  Für Farbleuchten mit voller RGB-Steuerung (z.B. "Color", "Color Ambiance").  
  **Verfügbare Funktionen:** Ein/Aus, Helligkeit (0-100%), relatives Dimmen, Farbtemperatur (2000-6500K), RGB-Farbe

**Wichtig:** Wenn Sie einen zu umfangreichen Typ wählen (z.B. "Farbe" für eine "White Ambiance"-Lampe), werden die nicht unterstützten Funktionen ignoriert oder führen zu unerwarteten Ergebnissen.

Die verfügbaren Kommunikationsobjekte passen sich automatisch an den gewählten Lampentyp an.

<!-- DOC -->
### Kanal deaktivieren

Deaktiviert diesen Kanal vorübergehend, ohne die Konfiguration zu löschen.

**Anwendungsfälle:**
- Testzwecke: Temporäres Ausschalten einzelner Lampen zur Fehlersuche
- Wartung: Deaktivierung während Umbauarbeiten
- Vorbereitung: Kanal konfigurieren, aber erst später aktivieren

**Achtung:** Ein deaktivierter Kanal:
- Sendet keine Befehle an die Hue Bridge
- Empfängt keine Updates von der Hue Bridge
- Behält alle Einstellungen und Gruppenadressen

<!-- DOC -->
### Synchronisationsrichtung

Legt fest, wie der Kanal zwischen KNX und Hue synchronisiert wird.

Es stehen vier Modi zur Verfügung:

- **Keine Synchronisation**  
  Der Kanal arbeitet rein manuell. Es erfolgt weder eine automatische Statusrückmeldung noch ein Polling.  
  **Verwendung:** Wenn nur manuelle Steuerung ohne Statusaktualisierung gewünscht ist (spart Netzwerkverkehr).

- **Nur KNX zu Hue**  
  KNX-Befehle werden an die Hue Bridge gesendet, aber es erfolgt kein Polling und keine automatische Statusrückmeldung.  
  **Verwendung:** Wenn die Lampe nur über KNX gesteuert wird und keine anderen Steuerungsquellen (Hue App, Bewegungsmelder) verwendet werden.

- **Nur Hue zu KNX**  
  Der Lampenstatus wird regelmäßig von der Hue Bridge abgerufen und auf den KNX-Bus gesendet. KNX-Eingangsobjekte werden jedoch nicht verarbeitet.  
  **Verwendung:** Wenn die Lampe nur über die Hue App oder andere Hue-Geräte gesteuert wird, der Status aber auf KNX angezeigt werden soll.

- **Bidirektional** (empfohlen)  
  Volle Synchronisation in beide Richtungen: KNX-Befehle werden an Hue gesendet UND der Status wird von Hue abgerufen und auf KNX aktualisiert.  
  **Verwendung:** Normalbetrieb, wenn die Lampe sowohl über KNX als auch über die Hue App/Sensoren gesteuert werden soll.

**Empfehlung:** In den meisten Fällen ist "Bidirektional" die richtige Wahl, da sie maximale Flexibilität bietet.

**Praxisbeispiel:**
- Wohnzimmerlampe wird über Taster (KNX) und Hue-App bedient.
- Einstellung: `Bidirektional`, Polling 10s.
- Ergebnis: Bedienung aus beiden Welten möglich, Visualisierung bleibt konsistent.

<!-- DOC -->
### Polling-Intervall

Legt fest, in welchem Zeitintervall (in Sekunden) der Lampenstatus von der Hue Bridge abgefragt wird.

**Gültige Werte:** 0-255 Sekunden  
**Standardwert:** 10 Sekunden  
**Empfohlener Bereich:** 5-30 Sekunden

**Hinweise zur Wahl des Intervalls:**

- **Kürzeres Intervall (5-10s):**  
  Vorteile: Schnellere Statusaktualisierung auf KNX bei Änderungen über die Hue App  
  Nachteile: Höhere Netzwerklast, häufigere API-Abfragen

- **Längeres Intervall (20-60s):**  
  Vorteile: Geringere Netzwerklast, weniger API-Abfragen  
  Nachteile: Verzögerte Statusaktualisierung auf KNX

- **Sehr langes/kein Polling (0 oder >60s):**  
  Nur wenn Statusaktualisierung nicht zeitkritisch ist

**Achtung:** 
- Bei Wert 0 ist das Polling deaktiviert (nur bei Synchronisationsrichtung "Keine" oder "Nur KNX zu Hue" sinnvoll)
- Die Hue Bridge hat ein API-Limit. Bei vielen Kanälen sollte das Intervall nicht zu kurz gewählt werden

**Empfohlene Startwerte nach Kanalzahl:**
- 1-5 Kanäle: 5-10s
- 6-12 Kanäle: 10-20s
- 13-20 Kanäle: 20-30s

<!-- DOC -->
### Minimale Helligkeit

Legt die minimale Helligkeit fest, die an die Hue-Leuchte gesendet wird, wenn ein Wert größer 0 vom KNX empfangen wird.

**Gültige Werte:** 1-100 %  
**Standardwert:** 1 %

**Hintergrund:** Manche Hue-Leuchten können bei sehr niedrigen Helligkeitswerten (1-2%) flackern oder ungleichmäßig leuchten. Mit dieser Einstellung kann ein Minimalwert vorgegeben werden.

**Anwendungsbeispiele:**

- **Standardwert (1%):** Vollständiger Dimmbereich wird genutzt
- **5%:** Werte unter 5% werden auf 5% angehoben - verhindert Flackern bei manchen LED-Lampen
- **10%:** Für Leuchten, die erst ab 10% sauber dimmen

**Hinweis:** Ein Wert von 0% vom KNX schaltet die Lampe immer aus, unabhängig von dieser Einstellung.

**Praxisregel:**
- Starten Sie mit 5% bei LED-Leuchten, die bei kleinen Werten flackern.
- Bei sichtbaren Sprüngen reduzieren Sie schrittweise auf 3% oder 2%.

<!-- DOC -->
## Kommunikationsobjekte

Die verfügbaren Kommunikationsobjekte hängen vom konfigurierten Lampentyp ab.

**OpenKNX-Projektierungshinweis:**
- Für stabile Rückmeldungen sollten Status-KOs immer mit Gruppenadressen verbunden werden, wenn eine Visualisierung oder Logik den Zustand auswertet.
- Nicht benötigte Status-KOs können unverbunden bleiben, um die Projektkomplexität zu reduzieren.

### Globale Kommunikationsobjekte

<!-- DOC -->
#### Bridge Verbindungsstatus

**Verfügbarkeit:** Nur sichtbar wenn "Status Verbindung" aktiviert ist  
**Richtung:** Nur Senden  
**DPT:** 1.001 (Schalten)  
**Funktion:** Zeigt den Verbindungsstatus zur Hue Bridge an

- **0 (AUS)**: Verbindung unterbrochen oder nicht hergestellt
- **1 (EIN)**: Verbunden und betriebsbereit

**Verwendung:** Kann für Visualisierung oder Logikfunktionen genutzt werden (z.B. Alarm bei Verbindungsverlust).

### Pro-Kanal Kommunikationsobjekte

Die folgenden Objekte sind pro Lampen-Kanal verfügbar. Die Verfügbarkeit hängt vom gewählten Lampentyp ab:

<!-- DOC -->
#### Schalten

**Verfügbarkeit:** Alle Lampentypen  
**Richtung:** Empfangen  
**DPT:** 1.001 (Schalten)  
**Funktion:** Schaltet die Lampe ein oder aus

- **0 (AUS)**: Lampe ausschalten
- **1 (EIN)**: Lampe einschalten

**Hinweis:** Beim Einschalten wird die zuletzt eingestellte Helligkeit verwendet.

<!-- DOC -->
#### Helligkeit

**Verfügbarkeit:** Dimmbar, Farbtemperatur, Farbe (RGB)  
**Richtung:** Empfangen  
**DPT:** 5.001 (Prozent 0-100%)  
**Funktion:** Setzt die absolute Helligkeit der Lampe

- **0%**: Lampe ausschalten
- **1-100%**: Helligkeit in Prozent (wird ggf. durch "Minimale Helligkeit" begrenzt)

**Verhalten:**
- Werte 1-100% schalten die Lampe automatisch ein (falls sie aus ist)
- Wert 0% schaltet die Lampe aus

<!-- DOC -->
#### Dimmen

**Verfügbarkeit:** Dimmbar, Farbtemperatur, Farbe (RGB)  
**Richtung:** Empfangen  
**DPT:** 3.007 (Dimmen)  
**Funktion:** Dimmt die Lampe relativ heller oder dunkler

**Dimm-Schritte:**
- Bei kurzen Telegrammen: ca. 5-10% pro Befehl
- Bei langen Telegrammen: kontinuierliches Dimmen bis Stop-Telegramm

**Hinweis:** Das Dimmen schaltet die Lampe automatisch ein, falls sie ausgeschaltet ist.

<!-- DOC -->
#### Status Schalten

**Verfügbarkeit:** Alle Lampentypen  
**Richtung:** Nur Senden  
**DPT:** 1.001 (Schalten)  
**Funktion:** Meldet den aktuellen Ein-/Aus-Zustand der Lampe zurück

- **0 (AUS)**: Lampe ist ausgeschaltet
- **1 (EIN)**: Lampe ist eingeschaltet

**Update:** Wird beim Polling-Intervall aktualisiert (bei Synchronisationsrichtung "Nur Hue zu KNX" oder "Bidirektional").

<!-- DOC -->
#### Status Helligkeit

**Verfügbarkeit:** Dimmbar, Farbtemperatur, Farbe (RGB)  
**Richtung:** Nur Senden  
**DPT:** 5.001 (Prozent 0-100%)  
**Funktion:** Meldet die aktuelle Helligkeit der Lampe zurück

**Werte:** 0-100%

**Update:** Wird beim Polling-Intervall aktualisiert (bei Synchronisationsrichtung "Nur Hue zu KNX" oder "Bidirektional").

<!-- DOC -->
#### Farbtemperatur

**Verfügbarkeit:** Farbtemperatur, Farbe (RGB)  
**Richtung:** Empfangen  
**DPT:** 7.600 (Farbtemperatur in Kelvin)  
**Funktion:** Stellt die Farbtemperatur der Lampe ein

**Gültige Werte:** 2000-6500 Kelvin

- **2000K**: Warmweißes Licht (gemütlich, entspannend)
- **2700K**: Standard-Glühlampe
- **4000K**: Neutralweiß (bürotauglich)
- **6500K**: Tageslichtweiß (aktivierend, konzentriert)

**Hinweis:** Werte außerhalb des Bereichs werden auf den nächstgelegenen gültigen Wert begrenzt.

<!-- DOC -->
#### Status Farbtemperatur

**Verfügbarkeit:** Farbtemperatur, Farbe (RGB)  
**Richtung:** Nur Senden  
**DPT:** 7.600 (Farbtemperatur in Kelvin)  
**Funktion:** Meldet die aktuelle Farbtemperatur der Lampe zurück

**Werte:** 2000-6500 Kelvin

**Update:** Wird beim Polling-Intervall aktualisiert (bei Synchronisationsrichtung "Nur Hue zu KNX" oder "Bidirektional").

<!-- DOC -->
#### Farbe RGB

**Verfügbarkeit:** Farbe (RGB)  
**Richtung:** Empfangen  
**DPT:** 232.600 (RGB-Farbe)  
**Funktion:** Stellt die Farbe der Lampe über RGB-Werte ein

**Format:** 3 Bytes (Rot, Grün, Blau), jeweils 0-255

**Beispiele:**
- Rot: 255, 0, 0
- Grün: 0, 255, 0
- Blau: 0, 0, 255
- Gelb: 255, 255, 0
- Magenta: 255, 0, 255
- Cyan: 0, 255, 255
- Weiß: 255, 255, 255

**Hinweis:** Die Lampe schaltet automatisch ein, wenn eine Farbe gesetzt wird.

**Hinweis zur Geräteabhängigkeit:**
- Nicht jede als „Farbe“ konfigurierte Leuchte setzt RGB identisch um.
- Farbwirkung kann je nach Leuchtmittel (Gamut, Kalibrierung) sichtbar abweichen.

<!-- DOC -->
#### Status RGB

**Verfügbarkeit:** Farbe (RGB)  
**Richtung:** Nur Senden  
**DPT:** 232.600 (RGB-Farbe)  
**Funktion:** Meldet die aktuelle RGB-Farbe der Lampe zurück

**Format:** 3 Bytes (Rot, Grün, Blau), jeweils 0-255

**Update:** Wird beim Polling-Intervall aktualisiert (bei Synchronisationsrichtung "Nur Hue zu KNX" oder "Bidirektional").

<!-- DOC -->
## Projektierungsbeispiele (ETS)

Die folgenden Beispiele dienen als praxistaugliche Startkonfigurationen.

### Beispiel 1: Einfaches Schalten im Flur

- **Lampentyp:** Nur schalten
- **Synchronisation:** Nur KNX zu Hue
- **Polling-Intervall:** 0
- **Verwendete KOs:** Schalten

**Vorteil:** Sehr geringe Netzwerklast, einfache Inbetriebnahme.

### Beispiel 2: Wohnraum mit Visualisierung

- **Lampentyp:** Dimmbar
- **Synchronisation:** Bidirektional
- **Polling-Intervall:** 10
- **Verwendete KOs:** Schalten, Helligkeit, Status Schalten, Status Helligkeit

**Vorteil:** Bedienung per Taster, App und Visualisierung mit konsistenten Statuswerten.

### Beispiel 3: Farbige Akzentbeleuchtung

- **Lampentyp:** Farbe (RGB)
- **Synchronisation:** Bidirektional
- **Polling-Intervall:** 15
- **Minimale Helligkeit:** 5
- **Verwendete KOs:** Schalten, Helligkeit, Farbe RGB, Statusobjekte

**Vorteil:** Gute Balance aus Reaktionszeit, Stabilität und Bus-/Netzlast.

<!-- DOC -->
## Häufige Fehler und Problemlösungen

### Bridge wird nicht gefunden (automatische Erkennung)

**Mögliche Ursachen und Lösungen:**

1. **Bridge und OpenKNX-Gerät in unterschiedlichen Netzwerksegmenten**  
   Lösung: Stellen Sie sicher, dass beide Geräte im gleichen Netzwerk sind oder Multicast-Routing aktiviert ist

2. **mDNS wird von Router/Firewall blockiert**  
   Lösung: Aktivieren Sie mDNS-Forwarding oder wechseln Sie zu "Manuelle IP-Adresse"

3. **Mehrere Hue Bridges im Netzwerk**  
   Lösung: Das Modul verbindet sich mit der ersten gefundenen Bridge. Bei mehreren Bridges "Manuelle IP-Adresse" verwenden

### Authentifizierung schlägt fehl

**Mögliche Ursachen und Lösungen:**

1. **Link-Button wurde nicht innerhalb von 30 Sekunden gedrückt**  
   Lösung: Gerät neu starten und beim langsamen Blinken der LED sofort den Button drücken

2. **Zu viele Apps in der Hue Bridge registriert**  
   Lösung: Alte/ungenutzte Apps in der Hue-App unter Einstellungen entfernen

3. **Link-Button an der Bridge defekt**  
   Lösung: In der Hue-App unter Einstellungen → Bridge → Neue App hinzufügen

### Lampen reagieren nicht auf KNX-Befehle

**Mögliche Ursachen und Lösungen:**

1. **Falsche UUID konfiguriert**  
   Lösung: UUID über Web-Interface (`/hue/scan`) oder Konsole (`hue scan`) überprüfen

2. **Kanal ist deaktiviert**  
   Lösung: "Kanal deaktivieren" in ETS auf "Nein" setzen

3. **Synchronisationsrichtung falsch konfiguriert**  
   Lösung: Bei reiner KNX-Steuerung mindestens "Nur KNX zu Hue" oder "Bidirektional" wählen

4. **Lampe ist offline/nicht erreichbar**  
   Lösung: Lampe in der Hue-App überprüfen, ggf. Bridge-Verbindung zur Lampe prüfen

### Status wird nicht aktualisiert

**Mögliche Ursachen und Lösungen:**

1. **Polling ist deaktiviert (Intervall = 0)**  
   Lösung: Polling-Intervall auf einen Wert > 0 setzen (empfohlen: 10 Sekunden)

2. **Synchronisationsrichtung falsch**  
   Lösung: "Nur Hue zu KNX" oder "Bidirektional" wählen

3. **Status-KOs haben keine Gruppenadressen**  
   Lösung: Gruppenadressen für Status-KOs in ETS vergeben

### Lampen reagieren verzögert

**Mögliche Ursachen und Lösungen:**

1. **Netzwerk-Latenz**  
   Lösung: Netzwerkverbindung zwischen OpenKNX-Gerät und Hue Bridge prüfen

2. **Hue Bridge überlastet**  
   Lösung: Polling-Intervall erhöhen, Anzahl der Kanäle reduzieren

3. **Zu viele API-Anfragen**  
   Lösung: Polling-Intervall erhöhen (mind. 10 Sekunden bei vielen Kanälen)

### HCL Master wirkt nicht (insbesondere bei "Manual Kelvin")

**Mögliche Ursachen und Lösungen:**

1. **Zu wenige gültige Stützpunkte konfiguriert**  
   Lösung: Mindestens 2 gültige Stützpunkte mit Zeitwert setzen (auch im Modus "Manual Kelvin").

2. **HCL-Master nicht zugeordnet**  
   Lösung: Im jeweiligen Lampenkanal einen HCL-Master (1-4) zuweisen.

3. **HCL global deaktiviert**  
   Lösung: Im HCL-Manager die HCL-Funktion aktivieren.

<!-- DOC -->
## Technische Hinweise

### Hue API v2

Das Modul verwendet die Hue API v2, die gegenüber der alten API v1 folgende Vorteile bietet:
- Schnellere Reaktionszeiten
- Unterstützung für neue Hue-Geräte
- Stabilere Verbindungen
- Bessere Performance bei vielen Lampen

**Wichtig für Integratoren:**
- API v2 arbeitet mit Ressourcenmodellen und Application-Key.
- Bei manuellen Tests (z.B. per REST-Tool) muss der Header `hue-application-key` gesetzt werden.

### Speicherung des App-Keys

Der bei der Authentifizierung generierte App-Key wird im nichtflüchtigen Speicher (Preferences/NVS) des ESP32 gespeichert und überlebt Neustarts und Firmware-Updates.

**Löschen des App-Keys:**
- Option 1: "Authentication zurücksetzen" in der ETS-Konfiguration aktivieren
- Option 2: Konsolenbefehl verwenden (falls implementiert)
- Option 3: App-Key in der Hue-App manuell löschen

### Maximale Anzahl Kanäle

Das Modul unterstützt bis zu 20 Kanäle. Diese Begrenzung ergibt sich aus:
- Speicherplatz auf dem ESP32
- API-Limits der Hue Bridge
- Performance-Überlegungen

**Empfehlung:** Verwenden Sie nur so viele Kanäle wie tatsächlich benötigt werden.

### Netzwerkanforderungen

- **Verbindung:** TCP/IP (HTTPS) zur Hue Bridge
- **Port:** 443 (HTTPS)
- **Protokoll:** Hue API v2 über HTTPS
- **Zertifikat:** Selbstsigniertes Zertifikat der Hue Bridge wird akzeptiert

### Performance und Timing

- **Befehlsausführung:** Befehle werden sofort an die Hue Bridge gesendet (< 100ms)
- **Status-Update:** Abhängig vom Polling-Intervall
- **Maximale API-Rate:** Die Hue Bridge erlaubt ca. 10-12 Anfragen pro Sekunde

**Berechnung der API-Last:**
- Anzahl Kanäle × (1 / Polling-Intervall) = Anfragen pro Sekunde
- Beispiel: 10 Kanäle mit 10s Intervall = 1 Anfrage/s (unkritisch)
- Beispiel: 20 Kanäle mit 5s Intervall = 4 Anfragen/s (noch OK)

## Inbetriebnahme-Checkliste

Vor Übergabe an den Kunden sollten folgende Punkte geprüft sein:

- Bridge wurde gefunden und erfolgreich authentifiziert
- Alle verwendeten Kanäle haben korrekte UUIDs
- Lampentyp pro Kanal passt zur realen Leuchte
- Synchronisationsrichtung pro Kanal ist passend zur Nutzung
- Status-KOs sind in ETS sinnvoll mit Gruppenadressen verbunden
- Reaktionszeit wurde unter realer Last getestet (mehrere gleichzeitige Befehle)

Diese Checkliste reduziert typische Servicefälle nach der Erstinbetriebnahme deutlich.

<!-- DOC -->
## Lizenz und Haftung

Dieses Modul ist Open Source und wird unter einer OpenKNX-kompatiblen Lizenz bereitgestellt.

**Wichtige Hinweise:**
- Keine Garantie für Funktionsfähigkeit
- Nutzung auf eigene Gefahr
- Keine Haftung für Schäden an Hue-Geräten oder KNX-Installation
- Philips Hue ist ein eingetragenes Warenzeichen von Signify N.V.





