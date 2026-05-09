### HCL-Manager-18

Jeder Lichtmanager 1..8 besitzt identischen Aufbau (HCL-Konfiguration):

- **Bezeichnung**: Freie ETS-Bezeichnung des Lichtmanagers.
- **Lichtmanager Sperre (spezifisch)**: Sperrt nur den jeweiligen Manager.
- **Erweiterte Kurve**: Kurventyp `FixedTime`, `SunPosition`, `Manual` oder `Astronomischer Sonnenstand`.
- **Stützpunkte**: Bis zu 10 Stützpunkte je Manager (bei `FixedTime` oder `SunPosition`).
- **Saison-Profil**: Optionale Sommer-/Winter-Stützpunkte (Modus `Standard`, `Auto-DST`, `Festes Datum` oder `Per Objekt`).
