### HCL-Adaptive-KOs

Kommunikationsobjekte für die adaptive Helligkeit je Lichtmanager:

- **Helligkeitssensor (Lux)** (Eingang, DPT 9.004): Umgebungslichtstärke vom Sensor
- **Tag/Nacht** (Eingang, DPT 1.001): Aktivierungssignal (Polarität konfigurierbar)
- **Adaptive Helligkeit aktiv** (Ausgang, DPT 1.011): Status: Regelung aktuell aktiv

Das KO „Tag/Nacht" ist nur sichtbar, wenn Aktivierung = „Nur tagsüber (per KO)".
Das KO „Adaptive Helligkeit aktiv" meldet, ob die Regelung gerade eingreift (z. B. für Logiken oder Visualisierung).
