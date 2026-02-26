### HCL Manager 1..8

Jeder Manager besitzt identischen Aufbau:

#### Bezeichnung
Freie ETS-Bezeichnung des Managers.

#### HCL Sperre (spezifisch)
Sperrt nur den jeweiligen Manager.

Optionen je Manager:
- **Rückfallzeit nach HCL-Sperre**

KOs je Manager:
- `HCL Sperre Mx` (Eingang)
- `Status HCL Sperre Mx` (Ausgang)

#### Erweiterte Kurve
Kurventyp:
- **FixedTime**
- **SunPosition**
- **Manual Kelvin**

Erweiterte Parameter je Manager:
- **Slew-Rate (K/min)**: begrenzt die Kelvin-Änderung pro Minute (`0` = keine Begrenzung).
- **Manual Kelvin**: fixer Kelvin-Sollwert bei Kurventyp `Manual Kelvin`.
- **Sonnenaufgang/Sonnenuntergang** und **Offsets (min)**: relevant für Kurventyp `SunPosition`.

#### Stützpunkte
Bis zu 10 Stützpunkte je Manager.

Hinweise:
- Mindestens 2 gültige Zeit-Stützpunkte erforderlich.
- Bei **Manual Kelvin** werden Zeit + Helligkeit verwendet; Kelvin kommt aus dem Manual-Kelvin-Parameter.

Beispiel:
- SP1 `06:00 / 3000K / 30%`
- SP2 `12:00 / 5000K / 90%`
- SP3 `20:00 / 2700K / 35%`

Praxisregel:
- `Aktualisierungsintervall`, `Überblendzeit` und `Slew-Rate` gemeinsam abstimmen, damit Übergänge ruhig bleiben.

