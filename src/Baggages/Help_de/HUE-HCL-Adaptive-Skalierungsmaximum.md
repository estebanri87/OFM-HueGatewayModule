### HCL-Adaptive-Skalierungsmaximum

Gibt den maximalen Lux-Wert des Sensors an, bei dem die Regelung vollständig ausgesteuert ist. Dieser Parameter muss auf den tatsächlichen Messbereich des verwendeten Sensors abgestimmt werden — er ist kein Raumtyp-Richtwert, sondern ein Sensor-Kennwert.

**Wie der Wert verwendet wird:**

- *Open-Loop*: Bei Sensorwert ≥ Skalierungsmaximum wird die Helligkeit maximal reduziert (auf Mindesthelligkeit). Dazwischen wird linear skaliert.
- *Closed-Loop*: Das Skalierungsmaximum rechnet den HCL-Sollwert (%) in einen Lux-Zielwert um: `Ziel-Lux = HCL-Sollwert% × Skalierungsmaximum`. Der Regler arbeitet auf diesen Zielwert hin.

**Beispiel mit einem Innensensor (max. 1000 Lux):**

Skalierungsmaximum = 1000 Lux, HCL-Sollwert = 80 %, Sensorwert = 500 Lux, Stärke = 80 %:
- Open-Loop: `80% × (1 − 500/1000 × 0,8) = 80% × 0,6 = 48%`
- Closed-Loop: Ziel = 800 Lux, Ist = 500 Lux → Regler erhöht die Helligkeit

**Beispiel mit einem Außensensor (max. 50.000 Lux):**

Wird ein Außensensor verwendet, muss das Skalierungsmaximum entsprechend hoch eingestellt werden.
Skalierungsmaximum = 1000 Lux, Sensorwert = 50.000 Lux → Sensorwert weit über Maximum → Helligkeit fällt sofort auf Mindesthelligkeit, völlig unabhängig vom HCL-Profil. Das ist in diesem Fall falsch.
Korrekt wäre Skalierungsmaximum = 50.000 Lux, damit der Sensor seinen vollen Bereich nutzt.

**Orientierungswerte je Sensortyp:**

- Einfacher Innensensor (bis 1.000 Lux): 500–1.000 Lux
- Hochwertiger Innensensor (bis 10.000 Lux): 1.000–5.000 Lux
- Außensensor / Dachsensor (bis 100.000 Lux): 20.000–65.000 Lux
