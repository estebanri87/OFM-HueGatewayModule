### Status-KOs je Lichtmanager

Aktiviert pro Manager die Ausgabe:
- `Status Helligkeit Soll` (`1 Byte`, `0..100 %`)
- `Status Farbtemperatur Soll` (`2 Byte`, `2000..6500 K`)

Hinweise:
- Die Ausgabe erfolgt zyklisch gemäß **Aktualisierungsintervall** des Lichtmanager-Bereichs.
- Die KOs liefern die vom Lichtmanager berechneten Sollwerte, unabhängig davon, wie viele Kanäle diesem zugeordnet sind.

