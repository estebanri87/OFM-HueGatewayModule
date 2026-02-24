# Implementierungsplan: Hue Räume/Zonen im OFM-HueGatewayModule

## Zielbild (MVP)

Kanäle können statt einzelner Lights auch einen Hue Raum oder eine Hue Zone als Ziel steuern.
Dadurch reagieren neu hinzugefügte Lampen in diesem Raum/Zone automatisch ohne erneute ETS-Parametrierung.

## Scope (MVP)

- Zieltypen pro Kanal: `Light`, `Room`, `Zone`
- KNX -> Hue für alle Zieltypen:
  - Schalten
  - Helligkeit absolut
  - Relativdimmen
- Discovery/Mapping für Räume/Zonen und Auflösung auf `grouped_light`
- Topologie-Refresh bei Start und Reconnect
- Konsolen- und Diagnoseausgaben für Zieltyp/-name/-RID

Nicht im MVP:

- Hue Szenen
- Vollständige dynamische ETS-Dropdown-Befüllung mit Live-Ressourcen
- Erweiterte Gruppenstatus-Logiken (MVP-Regel genügt)

## Technischer Ansatz

1. **Kanalmodell erweitern**
   - Aktuell ist die Kanalzuordnung auf `CHLightUUIDStr` fokussiert.
   - Neu: allgemeines Kanal-Ziel (`TargetType`, `TargetRid`, `TargetName`).

2. **Hue-Topologie aufbauen**
   - Ressourcen lesen: `light`, `room`, `zone`, `grouped_light`.
   - Lookup-Tabellen:
     - `roomRid -> groupedLightRid`
     - `zoneRid -> groupedLightRid`
     - optional `lightRid -> room/zone` für Diagnose.

3. **Befehlsrouting**
   - `Light`: bestehender Pfad über `/resource/light/{id}`
   - `Room`/`Zone`: über zugeordnetes `/resource/grouped_light/{id}`

4. **Robustheit**
   - Wenn Ziel nicht auflösbar: Kanal deaktiviert nicht, aber Warnung + Retry beim nächsten Topologie-Refresh.
   - Refresh-Trigger: Boot, Reconnect, zyklischer Guard-Refresh.

## Arbeitspakete (Umsetzungsreihenfolge)

## AP1 - Datenmodell & Client vorbereiten

Dateien:

- `src/HueGatewayClient.h`
- `src/HueGatewayClient.cpp`

Tasks:

- Strukturen für Room/Zone/Grouped-Light ergänzen.
- Discovery-Methoden ergänzen (Räume/Zonen + grouped_light-Auflösung).
- Hilfsfunktionen für Mapping/Lookup implementieren.

Definition of Done:

- Client liefert stabile Topologie-Daten für mindestens 1 Bridge mit Room/Zone-Struktur.

## AP2 - ETS Parameter erweitern

Dateien:

- `src/HueGatewayModule.share.xml`
- ggf. `src/HueGatewayModule.templ.xml`

Tasks:

- Pro Kanal `TargetType` ergänzen.
- Pro Kanal allgemeine `TargetRID`/`TargetName` Parameter ergänzen.
- Bestehende Kanalparameter (Sync, Poll, MinBrightness, HCL-Master) beibehalten.

Definition of Done:

- OpenKNXproducer läuft ohne Fehler.
- Neue Parameter sind in ETS sichtbar und logisch gruppiert.

## AP3 - Kanalinitialisierung umbauen

Dateien:

- `src/HueGatewayModule.cpp`
- `src/Devices/HueGatewayLight.h`
- `src/Devices/HueGatewayLight.cpp`

Tasks:

- `setupDevices()` von Light-only auf Zieltyp-Routing umbauen.
- Kanäle intern mit effektivem Steuer-RID initialisieren (Light oder grouped_light).
- Logging erweitern: Kanal, Zieltyp, Zielname, RID, Fallback-Grund.

Definition of Done:

- Gemischte Kanäle (Light + Room + Zone) initialisieren reproduzierbar.

## AP4 - KNX Befehlsrouting und Statusregel

Dateien:

- `src/HueGatewayModule.cpp`
- `src/Devices/HueGatewayLight.cpp`

Tasks:

- Schalten, Absolutdimmen, Relativdimmen für alle Zieltypen sicherstellen.
- MVP-Statusregel für Gruppen festlegen und dokumentieren:
  - bevorzugt: zuletzt gesetzter Sollwert als Kanalstatus (deterministisch).

Definition of Done:

- KNX-Telegramme wirken korrekt auf Light/Room/Zone-Ziele.

## AP5 - Stabilität & Diagnose

Dateien:

- `src/HueGatewayModule.cpp`
- `README.md`
- Baggage-Hilfe in `src/Baggages/Help_de/`

Tasks:

- Topologie-Refresh bei Reconnect und periodisch ergänzen.
- Console-Ausgabe (`hue scan`/`hue status`) um Zieltypen und Mappingzustand erweitern.
- Hilfetexte für ETS ergänzen.

Definition of Done:

- Nach Bridge-Reconnect wird Mapping automatisch wiederhergestellt.

## AP6 - Verifikation

Testszenarien:

1. Kanal auf `Light` verhält sich wie bisher.
2. Kanal auf `Room`: Schalten + Dimmen wirkt auf alle Lichter im Raum.
3. Neue Lampe in Hue-Raum hinzufügen -> Kanal wirkt ohne ETS-Änderung.
4. Kanal auf `Zone`: analog zu Room.
5. Bridge-Reboot/Reconnect -> Mapping wird wieder aufgebaut.

Abnahmekriterium:

- MVP-Nutzen ist erfüllt: keine ETS-Neuparametrierung bei Lampen-Zuwachs im Raum/Zone.

## Aufwandsschätzung

- AP1: 0.5-1.0 Tage
- AP2: 0.5-1.0 Tage
- AP3: 1.0-1.5 Tage
- AP4: 0.5-1.0 Tage
- AP5: 0.5-1.0 Tage
- AP6: 0.5-1.0 Tage

Gesamt MVP: **3.5-6.5 Arbeitstage**

## Risiken & Gegenmaßnahmen

- **Uneinheitlicher Group-Status**: klare MVP-Regel dokumentieren.
- **RID-Änderungen auf Bridge**: robuste Refresh-/Retry-Strategie.
- **Speicherlast (JSON)**: bestehende Dokumentgrößen prüfen, falls nötig stufenweise Discovery.

## Branch- und Release-Strategie

- Arbeitsbranch: `v1dev_addroomandzones`
- Kleine, thematische Commits je AP.
- Merge-Ziel nach erfolgreicher Verifikation: `v1dev_Alpha`.