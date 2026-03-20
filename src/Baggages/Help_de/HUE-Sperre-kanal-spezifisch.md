### Sperre (kanal-spezifisch)

Sperrt die automatische Lichtmanager-Ausgabe nur für den jeweiligen Hue-Kanal.
Andere Kanäle mit gleicher Lichtmanager-Zuordnung bleiben unverändert aktiv.
Der zugeordnete Lichtmanager selbst läuft weiter und versorgt weiterhin alle anderen ihm zugeordneten Hue-Kanäle.

Sichtbarkeit:
- Nur bei Lampentyp `Farbtemperatur` oder `Farbe RGB`.
- Nur wenn beim Kanal ein Lichtmanager `1..8` zugeordnet ist.

Option je Kanal:
- **Rückfallzeit nach Sperre** (inkl. Tageswechsel, `kein Rückfall` möglich)
- **Rückfallstrategie nach Sperre**: zentrale Vorgabe, siehe Abschnitt Rückfallstrategie nach Sperre

KOs je Kanal:
- `Sperre` (Eingang)
- `Status Sperre` (Ausgang)

Praxisbeispiel:
- Wohn-/Essbereich mit gemeinsamem Lichtmanager.
- Am Abend läuft im Essbereich der Lichtmanager weiter, im Wohnzimmer wird per Taster `Sperre` aktiviert, damit dort eine feste, warme Szene bleibt.
- Am nächsten Morgen hebt die konfigurierte Rückfallzeit die Sperre automatisch auf und der Kanal folgt wieder der Lichtmanager-Kurve.

