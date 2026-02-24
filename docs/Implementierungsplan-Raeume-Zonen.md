# Implementierungsplan: Hue Räume/Zonen im OFM-HueGatewayModule

## Zielbild (MVP)

Kanäle können statt einzelner Lights auch einen Hue Raum oder eine Hue Zone als Ziel steuern.
Dadurch reagieren neu hinzugefügte Lampen in diesem Raum/Zone automatisch ohne erneute ETS-Parametrierung.

Zusätzlich gelten für den MVP folgende Qualitätsziele:

- Kein negativer Einfluss auf bestehende Light-Kanäle
- Stabiler Betrieb bei Bridge-Reconnect und Topologieänderungen
- Nachvollziehbare Diagnose bei Mapping-/Routing-Fehlern

## Scope (MVP)

- Zieltypen pro Kanal: `Light`, `Room`, `Zone`
- KNX -> Hue für alle Zieltypen:
  - Schalten
  - Helligkeit absolut
  - Relativdimmen
- Discovery/Mapping für Räume/Zonen und Auflösung auf `grouped_light`
- Topologie-Refresh bei Start und Reconnect
- Konsolen- und Diagnoseausgaben für Zieltyp/-name/-RID

MVP-Statusregel:

- Kanalstatus für `Room`/`Zone`: **zuletzt gesetzter Sollwert** (deterministisch)
- Diese Regel wird in Hilfe/README klar dokumentiert

Nicht im MVP:

- Hue Szenen
- Vollständige dynamische ETS-Dropdown-Befüllung mit Live-Ressourcen
- Erweiterte Gruppenstatus-Logiken (`any_on`, `all_on` als eigene Modi)

## Technischer Ansatz

1. **Kanalmodell erweitern**
   - Aktuell ist die Kanalzuordnung auf `CHLightUUIDStr` fokussiert.
   - Neu: allgemeines Kanal-Ziel (`TargetType`, `TargetRid`).
   - `TargetName` nur Diagnose (nicht als führende Steuerinformation).

2. **Hue-Topologie aufbauen**
   - Ressourcen lesen: `light`, `room`, `zone`, `grouped_light`.
   - Lookup-Tabellen:
     - `roomRid -> groupedLightRid`
     - `zoneRid -> groupedLightRid`
     - optional `lightRid -> room/zone` für Diagnose.

3. **Resolver-Spezifikation (kritischer Pfad)**
   - Ziel: `TargetType + TargetRid` deterministisch auf steuerbares RID auflösen.
   - Regeln:
     - `Light` -> direkt `light/{rid}`
     - `Room` -> `grouped_light` über Resource-Beziehungen des Raums
     - `Zone` -> `grouped_light` über Resource-Beziehungen der Zone
   - Bei nicht auflösbarer Beziehung: kein Absturz, Warnung + Retry-Queue.

4. **Befehlsrouting**
   - `Light`: bestehender Pfad über `/resource/light/{id}`
   - `Room`/`Zone`: über zugeordnetes `/resource/grouped_light/{id}`

5. **Capability-Matrix**
   - Kommandos nur senden, wenn Zieltyp-Fähigkeit bekannt unterstützt wird.
   - Bei nicht unterstütztem Kommando: klare Log-Meldung + Statusflag.

6. **Robustheit**
   - Wenn Ziel nicht auflösbar: Kanal deaktiviert nicht, aber Warnung + Retry beim nächsten Topologie-Refresh.
   - Refresh-Trigger: Boot, Reconnect, zyklischer Guard-Refresh.

## Kritische Pfade

1. Korrekte Auflösung `Room/Zone -> grouped_light`
2. Laufzeitstabilität (Loopzeit/Heap) trotz zusätzlicher Discovery-Aufrufe
3. Konsistentes Verhalten bei Reconnect und zwischenzeitlich gelöschten/umbenannten Ressourcen

## AP0 - Architektur- und Resolver-Design (Pflicht vor Umsetzung)

Dateien:

- `docs/Implementierungsplan-Raeume-Zonen.md` (dieses Dokument)
- optional ergänzende Notizen unter `docs/`

Tasks:

- Resolver-Algorithmus final festlegen (inkl. Fehler- und Retrypfade)
- Capability-Matrix festlegen (`switch`, `brightness`, `dimming_delta` je Zieltyp)
- Laufzeitbudget definieren (Refresh-Intervalle, Timeouts, Backoff)

Definition of Done:

- Technisches Design ist abgestimmt und freigegeben.
- Offene API-/Status-Fragen sind beantwortet.

## Arbeitspakete (Umsetzungsreihenfolge)

## AP1 - Datenmodell & Client vorbereiten

Dateien:

- `src/HueGatewayClient.h`
- `src/HueGatewayClient.cpp`

Tasks:

- Strukturen für Room/Zone/Grouped-Light ergänzen.
- Discovery-Methoden ergänzen (Räume/Zonen + grouped_light-Auflösung).
- Hilfsfunktionen für Mapping/Lookup implementieren.
- Metriken ergänzen: `topologyRefreshMs`, `mappingFailCount`, `lastTopologyOkMs`.

Definition of Done:

- Client liefert stabile Topologie-Daten für mindestens 1 Bridge mit Room/Zone-Struktur.
- Heap bleibt stabil (kein kontinuierlicher Abfall über 30 min Soak-Test).

## AP2 - ETS Parameter erweitern

Dateien:

- `src/HueGatewayModule.share.xml`
- ggf. `src/HueGatewayModule.templ.xml`

Tasks:

- Pro Kanal `TargetType` ergänzen.
- Pro Kanal `TargetRID` ergänzen.
- Legacy-Feld `CHLightUUIDStr` als Migrationspfad berücksichtigen (`TargetType=Light`).
- Bestehende Kanalparameter (Sync, Poll, MinBrightness, HCL-Master) beibehalten.

Definition of Done:

- OpenKNXproducer läuft ohne Fehler.
- Neue Parameter sind in ETS sichtbar und logisch gruppiert.
- Bestehende Light-Konfigurationen bleiben funktional.

## AP3 - Kanalinitialisierung umbauen

Dateien:

- `src/HueGatewayModule.cpp`
- `src/Devices/HueGatewayLight.h`
- `src/Devices/HueGatewayLight.cpp`

Tasks:

- `setupDevices()` von Light-only auf Zieltyp-Routing umbauen.
- Kanäle intern mit effektivem Steuer-RID initialisieren (Light oder grouped_light).
- Logging erweitern: Kanal, Zieltyp, Zielname, RID, Fallback-Grund.
- Retry-Queue für nicht auflösbare Targets einbauen.

Definition of Done:

- Gemischte Kanäle (Light + Room + Zone) initialisieren reproduzierbar.
- Nicht auflösbare Targets blockieren nicht die Initialisierung anderer Kanäle.

## AP4 - KNX Befehlsrouting und Statusregel

Dateien:

- `src/HueGatewayModule.cpp`
- `src/Devices/HueGatewayLight.cpp`

Tasks:

- Schalten, Absolutdimmen, Relativdimmen für alle Zieltypen sicherstellen.
- Capability-Matrix im Routing durchsetzen.
- MVP-Statusregel implementieren: zuletzt gesetzter Sollwert.
- Bei Kommandofehlern Telemetrie/Log inkl. HTTP-Code ausgeben.

Definition of Done:

- KNX-Telegramme wirken korrekt auf Light/Room/Zone-Ziele.
- Kein stilles Verwerfen von Telegrammen ohne Diagnoseeintrag.

## AP5 - Stabilität & Diagnose

Dateien:

- `src/HueGatewayModule.cpp`
- `README.md`
- Baggage-Hilfe in `src/Baggages/Help_de/`

Tasks:

- Topologie-Refresh bei Reconnect und periodisch ergänzen.
- Console-Ausgabe (`hue scan`/`hue status`) um Zieltypen und Mappingzustand erweitern.
- Hilfetexte für ETS ergänzen.
- Backoff-Strategie bei wiederholten API-Fehlern implementieren.
- Guard-Refresh so takten, dass Loopwarnungen vermieden werden.

Definition of Done:

- Nach Bridge-Reconnect wird Mapping automatisch wiederhergestellt.
- Bei Bridge-Störungen bleibt das Modul bedienbar und stabil.

## AP6 - Verifikation

Testszenarien:

1. Kanal auf `Light` verhält sich wie bisher.
2. Kanal auf `Room`: Schalten + Dimmen wirkt auf alle Lichter im Raum.
3. Neue Lampe in Hue-Raum hinzufügen -> Kanal wirkt ohne ETS-Änderung.
4. Kanal auf `Zone`: analog zu Room.
5. Bridge-Reboot/Reconnect -> Mapping wird wieder aufgebaut.
6. Ziel-RID gelöscht/umbenannt -> sauberer Fehlerpfad + Wiederanlauf nach Korrektur.
7. 20 Kanäle Mischbetrieb über 30 Minuten ohne Instabilität.
8. Netzwerkflaps (kurze Disconnects) ohne deadlock oder Watchdog-Probleme.

Abnahmekriterium:

- MVP-Nutzen ist erfüllt: keine ETS-Neuparametrierung bei Lampen-Zuwachs im Raum/Zone.
- Alle kritischen Pfade bestehen die Verifikation.

## Aufwandsschätzung

- AP0: 0.5 Tage
- AP1: 0.5-1.0 Tage
- AP2: 0.5-1.0 Tage
- AP3: 1.0-1.5 Tage
- AP4: 0.5-1.0 Tage
- AP5: 0.5-1.0 Tage
- AP6: 0.5-1.0 Tage

Gesamt MVP: **4.0-7.0 Arbeitstage**

## Risiken & Gegenmaßnahmen

- **Uneinheitlicher Group-Status**: klare MVP-Regel dokumentieren.
- **RID-Änderungen auf Bridge**: robuste Refresh-/Retry-Strategie.
- **Speicherlast (JSON)**: bestehende Dokumentgrößen prüfen, falls nötig stufenweise Discovery.
- **Loopzeit-Spitzen durch Discovery**: time-sliced Refresh und Backoff.
- **Teilweise API-Fehler**: pro Zieltyp differenzierte Fehlerbehandlung statt globalem Fail.

## Branch- und Release-Strategie

- Arbeitsbranch: `v1dev_addroomandzones`
- Kleine, thematische Commits je AP.
- Merge-Ziel nach erfolgreicher Verifikation: `v1dev_Alpha`.

Empfohlene Commit-Reihenfolge:

1. `AP0 design freeze (resolver + capability matrix)`
2. `AP1 topology model and discovery`
3. `AP2 ETS target model`
4. `AP3 channel setup routing`
5. `AP4 command/status routing`
6. `AP5 diagnostics and recovery`
7. `AP6 verification fixes`