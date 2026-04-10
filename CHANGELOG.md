# Changelog

Alle wesentlichen Änderungen an diesem Projekt werden in dieser Datei dokumentiert.

## [Unveröffentlicht 0.4.0]

### Hinzugefügt
- Dedizierte Szenen-Aktionen für Hue-Taster ergänzt.
- ETS-Tasteraktionen und Drehregler-Handling erweitert.
- behavior_instance wird jetzt automatisch via SSE-Event gelöscht (kein Neustart erforderlich).

### Geändert
- Replaced all `Serial.print*` calls in `HueGatewayAuth`, `HueGatewayClient`, and `HueGatewayButton` with OpenKNX logger (`logInfo`, `logDebug`, `logWarning`, `logError`). Debug output is now stripped in release builds and routed through the standard OpenKNX logging infrastructure.
- Taster-Modell von Gewerk-basiertem Dispatch auf DPT-Typ-Dispatch umgestellt.
- Jalousie: Langdruck sendet kein Stop-Telegramm beim Loslassen; Kurzdruck in „Auf/Stop" umbenannt.
- Dimm-Wiederholungsintervall präzisiert und Beta-Labels ergänzt.
- ETS-Sichtbarkeit für Taster-Parameter und HCL-Parameter überarbeitet.

### Behoben
- HCL-Update-Intervall für KNX-Bus-Sends wird nun korrekt berücksichtigt.
- Jalousie-DPT-Mapping in ETS-Tasterobjekten korrigiert.
- DPT 3.007 Dimm-Schritte werden konsistent kodiert.
- Dimm-Button-Objekte für Licht-Kurzdruckaktionen sichtbar gemacht.
- ETS-Parameter und Labels für gemeinsame Hue-Parameter verfeinert.
- Drehregler-Steuerelemente als Beta markiert.
- Langdruck- und Jalousie-Mapping für Taster verfeinert.
- Runtime-Handling und Diagnoseausgaben gehärtet.

## [0.3.9] - 2026-03-23

### Hinzugefügt
- Neue Basisklasse `HueGatewayDevice` mit fünf konkreten Gerätetypen:
  - **Licht** (`HueGatewayLight`) – Schalten, Dimmen, CT, RGB
  - **Steckdose** (`HueGatewayPlug`) – Schalten; Erkennung über Archetype-Feld der Bridge
  - **Bewegungsmelder** (`HueGatewaySensor`) – Präsenz, optional Temperatur, Lux, Batterie
  - **Kontaktsensor** (`HueGatewayContact`) – Offen/Geschlossen, optional Manipulation, Batterie
  - **Taster/Schalter** (`HueGatewayButton`) – bis zu 4 Tasten, Kurz-/Langdruck, Drehregler
- Szenen-Engine: bis zu 8 Szenen pro Kanal (Schalten, Dimmen, CT, RGB); Recall und Store über KNX-Gruppenadresse.
- ETS-Parameter für Tasterfunktion, Invertierung, Szenen und Drehregler (`relative_rotary`).
- Konfigurierbares Dimm-Wiederholungsintervall `HUERelDimRepeatMs` (20–500 ms, Standard 120 ms).
- NativeHueAction-Parameter pro Kanal für Taster/Schalter.
- Accessory-Device-Scan (`getAccessoryDevices`, `HueGatewayAccessoryDevice`).
- HCL-Lock-Diagnose und Button-ETS-Mapping im SSE-Eventhandler.
- behavior_instance DELETE via Hue API v2.

### Geändert
- HCL-Lock wird automatisch freigegeben, wenn das Licht ausgeschaltet wird.
- SSE-Eventerkennung für Button, Motion, Contact und Rotary auf Service-RID-Auflösung umgestellt.

### Behoben
- Taster-Dispatch: Jalousie-DPT-Tausch, `hasLong`/`hasShort`-Logik und `invertLong` korrigiert.
- UIHint `CheckBox` auf `TypeRestriction` ist in ETS ungültig – auf `DropDown` geändert.

## [0.3.5] - 2026-03-18

### Hinzugefügt
- **Astronomische HCL-Kurve**: Farbtemperatur und Helligkeit werden anhand der Sonnenhöhe berechnet.
- Fallback-Policies, Diagnose-Ringpuffer und Retry-Aktionen für stabilen Dauerbetrieb.
- Transition-Getter in `HueGatewayLight` und verbessertes Switch-Logging.

### Geändert
- ETS-Hilfetexte und Applikationsbeschreibung vollständig überarbeitet.

### Behoben
- DPT 3.007 Halten/Loslassen stabilisiert; Statusrückmeldung verbessert.
- Hue API sendet bei AUS-Befehlen keine überflüssigen Dimm- und CT-Werte mehr.
- Grouped-Light-Status und Helligkeits-Feedback präzisiert.
- RAM-Verbrauch reduziert; Diagnose-Ringpuffer auf sinnvolle Tiefe begrenzt.

## [0.3.2] - 2026-03-04

### Hinzugefügt
- Event-Stream-Absicherung und Setup-Leitplanken für stabilen Langzeitbetrieb.
- Verbesserte KNX-Write-Trace-Telemetrie für Diagnose.

### Behoben
- Channel-Mappings bleiben bei Bridge-Scan-Neustarts erhalten.
- Grouped-Light: letzter Helligkeitswert wird nach Verbindungsunterbrechung korrekt wiederhergestellt.
- ETS-Layout-Mappings und Schaltverhaltens-Hilfetext verfeinert.

## [0.3] - 2026-02-26

### Hinzugefügt
- Räume und Zonen als eigener Kanaltyp (`grouped_light`-Ressource der Hue Bridge); unterstützt Schalten, Dimmen, CT und RGB analog zu Einzelleuchten.
- Kanal-Template mit kumulativer KO-Sichtbarkeit und dynamischen KO-Labels.

### Geändert
- Versionszweig 0.3.x gestartet (Nachfolger von 0.2.x).
- HCL-Manager-Handling und Übergangszeitverhalten überarbeitet.

## [0.2.2] - 2026-02-26

### Hinzugefügt
- Globale ETS-Parameter für Schalt-Übergangszeiten (separates Ein- und Ausschalten).

### Geändert
- Übergangszeiten werden konsistent in der Runtime sowohl für HCL- als auch Nicht-HCL-Schaltwege angewendet.

### Behoben
- HCL-Manager 5–8: ETS-Zuweisungen wurden zur Laufzeit nicht korrekt angewendet.

## [0.2.1] - 2026-02-25

### Hinzugefügt
- Kanal-spezifischer HCL-Lock: Parameter, Kommunikationsobjekt und Runtime-Verhalten.

### Behoben
- Doppelter globaler HCL-Manager-Status-KO-Bereich in der ETS-Oberfläche entfernt.
- Build-Warnung für ungenutzte Variable im Channel-Lock-Status-Handler beseitigt.

## [0.2.0] - 2026-02-24

### Geändert
- Start des 0.2.x-Entwicklungszweigs für Raum-/Zonen-Unterstützung.
- Modulversion von 0.1.0 auf 0.2.0 erhöht.

## [0.1.0] - 2026-02-03

### Hinzugefügt
- Projektinitialisierung mit grundlegender Modulstruktur.
- Hue Bridge Discovery via mDNS; Button-Press-Authentifizierung ohne manuelle Token-Konfiguration.
- Bidirektionale Lichtkanal-Steuerung: Schalten, Dimmen (0–100 %), Farbtemperatur (2000–6500 K), RGB.
- Bidirektionale Statusaktualisierung via Hue API v2 Event Stream (SSE); Polling als Fallback.
- HCL-Master-Funktion (Alpha): tageszeit- und kanal-abhängige Farbtemperatur-/Helligkeitssteuerung.
- OFM-WebUI-Anbindung für kanalbasierte Hue-UUID-Konfiguration.
- TLS-Unterstützung für HTTPS-Kommunikation mit der Hue Bridge.
- NVS-Persistierung von Bridge-UUID und API-Key.

