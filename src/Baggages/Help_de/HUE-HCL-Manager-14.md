### HCL Manager 1..4

Jeder Manager besitzt identischen Aufbau:

#### Bezeichnung
Freie ETS-Bezeichnung des Managers.

#### HCL Sperre (spezifisch)
Sperrt nur den jeweiligen Manager.

Optionen je Manager:
- **Rückfall aktivieren**
- **Rückfallzeit nach HCL-Sperre**

KOs je Manager:
- `HCL Sperre Mx` (Eingang)
- `Status HCL Sperre Mx` (Ausgang)

#### Erweiterte Kurve
Kurventyp:
- **FixedTime**
- **SunPosition**
- **Manual Kelvin**

#### Slew-Rate (K/min)
Die Slew-Rate begrenzt die Änderungsrate der Farbtemperatur pro Minute.

- `0` bedeutet: keine Begrenzung, Ziel-Kelvin wird direkt übernommen.
- Werte `>0` bedeuten: weiche Annäherung an den Zielwert mit maximaler Änderungsrate in K/min.

Beispiel:
- `10 K/min` entspricht ungefähr `0,167 K/s`.
- Bei 60 s Aktualisierung sind das maximal etwa `10 K` pro Aktualisierungsschritt.

Hinweis zum Zusammenspiel:
- Die vollständige Abstimmung von **Aktualisierungsintervall**, **Überblendzeit** und **Slew-Rate** inkl. Profilempfehlungen ist in der Kontexthilfe **HUE-Einstellungen** beschrieben.

#### Stützpunkte
Bis zu 10 Stützpunkte je Manager.

Hinweise:
- Mindestens 2 gültige Zeit-Stützpunkte erforderlich.
- Bei **Manual Kelvin** werden Zeit + Helligkeit verwendet; Kelvin kommt aus dem Manual-Kelvin-Parameter.

Beispiel:
- SP1 `06:00 / 3000K / 30%`
- SP2 `12:00 / 5000K / 90%`
- SP3 `20:00 / 2700K / 35%`

