### HCL Sperre (kanal-spezifisch)

Sperrt die HCL-Ausgabe nur für den jeweiligen Hue-Kanal.
Andere Kanäle mit gleicher HCL-Manager-Zuordnung bleiben unverändert aktiv.

Sichtbarkeit:
- Nur bei Lampentyp `Farbtemperatur` oder `Farbe (RGB)`.
- Nur wenn beim Kanal ein HCL-Manager `1..8` zugeordnet ist.

Option je Kanal:
- **Rückfallzeit nach HCL-Sperre** (inkl. Tageswechsel, `kein Rückfall` möglich)

KOs je Kanal:
- `HCL Sperre` (Eingang)
- `Status HCL Sperre` (Ausgang)

Praxisbeispiel:
- Wohn-/Essbereich mit gemeinsamem HCL-Manager.
- Am Abend läuft im Essbereich HCL weiter, im Wohnzimmer wird per Taster `HCL Sperre` aktiviert, damit dort eine feste, warme Szene bleibt.
- Am nächsten Morgen hebt die konfigurierte Rückfallzeit die Sperre automatisch auf und der Kanal folgt wieder der HCL-Kurve.

