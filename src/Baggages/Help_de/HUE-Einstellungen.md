### Einstellungen

Diese Seite beschreibt das Zusammenspiel der drei relevanten HCL-Dynamikparameter:

- **Aktualisierungsintervall (Sekunden)**
- **Überblendzeit (Sekunden)**
- **Slew-Rate (K/min)** je HCL-Manager (im jeweiligen Manager-Reiter)

#### Was macht jeder Parameter?

- **Aktualisierungsintervall**
	Bestimmt, wie oft neue HCL-Sollwerte berechnet und angewendet werden (z. B. alle 60 s).

- **Überblendzeit**
	Bestimmt die Übergangszeit, mit der ein neuer Sollwert an die Leuchte gesendet wird.

- **Slew-Rate (K/min)**
	Begrenzt, wie schnell sich die Farbtemperatur intern pro Minute ändern darf.
	- `0` = keine Begrenzung (Sollwertsprung ohne Rampenbegrenzung)
	- `>0` = weiches Nachführen mit maximaler Änderung in K/min

#### Wie spielen die drei Einstellungen zusammen?

- Bei jedem **Aktualisierungsintervall** entsteht ein neuer Zielwert.
- Die **Slew-Rate** begrenzt zuerst den zulässigen Kelvin-Schritt zum Zielwert.
- Anschließend wird dieser (ggf. bereits begrenzte) Zielwert mit der eingestellten **Überblendzeit** zur Leuchte gesendet.

#### Wichtige Praxisregeln

- Für ruhige Verläufe: **Slew-Rate > 0** setzen.
- **Überblendzeit** sollte deutlich kleiner als das **Aktualisierungsintervall** sein.
- Ist die Überblendzeit zu nah am Aktualisierungsintervall, können Übergänge „nachziehen“ oder unruhig wirken.
- Bei sehr kurzer Aktualisierung + langer Überblendzeit + Slew `0` entsteht häufig ein hektischer Eindruck.

#### Beispielprofile

1) **Harte Übergänge (technisch schnell, sichtbar sprunghaft)**
- Aktualisierungsintervall: `30 s`
- Überblendzeit: `1 s`
- Slew-Rate: `0 K/min`

Effekt: sehr direkte Wechsel, gut für Test/Debug, visuell eher „hart“.

2) **Sehr weiche Übergänge (maximal ruhig)**
- Aktualisierungsintervall: `60 s`
- Überblendzeit: `8 s`
- Slew-Rate: `5 K/min`

Effekt: sehr sanfte, kaum wahrnehmbare Übergänge, träge Reaktion.

3) **Optimal / ausgewogen (empfohlen für Alltag)**
- Aktualisierungsintervall: `60 s`
- Überblendzeit: `6 s`
- Slew-Rate: `10 K/min`

Effekt: ruhige Übergänge mit guter Reaktionsfähigkeit; bewährter Kompromiss für Wohnbereiche.

Sollwerte werden aus der HCL-Kurve berechnet und bei Wertänderung als Status-KO übertragen.

