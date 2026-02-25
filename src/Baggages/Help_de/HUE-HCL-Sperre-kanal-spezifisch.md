### HCL Sperre (kanal-spezifisch)

Sperrt die HCL-Ausgabe nur für den jeweiligen Hue-Kanal.
Andere Kanäle mit gleicher HCL-Manager-Zuordnung bleiben unverändert aktiv.

Sichtbarkeit:
- Nur bei Lampentyp `Farbtemperatur` oder `Farbe (RGB)`.
- Nur wenn beim Kanal ein HCL-Manager `1..4` zugeordnet ist.

Optionen je Kanal:
- **Rückfall aktivieren**
- **Rückfallzeit nach HCL-Sperre** (inkl. Tageswechsel)

KOs je Kanal:
- `HCL Sperre` (Eingang)
- `Status HCL Sperre` (Ausgang)

Praxisbeispiel:
- Wohn-/Essbereich mit gemeinsamem HCL-Manager.
- Am Abend läuft im Essbereich HCL weiter, im Wohnzimmer wird per Taster `HCL Sperre` aktiviert, damit dort eine feste, warme Szene bleibt.
- Am nächsten Morgen hebt die konfigurierte Rückfallzeit die Sperre automatisch auf und der Kanal folgt wieder der HCL-Kurve.

