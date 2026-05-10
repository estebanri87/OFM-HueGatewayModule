### HCL-Adaptive-Modus

Wählt den Betriebsmodus der adaptiven Helligkeitsregelung:

- **Deaktiviert**: Keine adaptive Regelung — Lichtmanager arbeitet nur nach HCL-Kurve.
- **Tageslicht-Kompensation (Open-Loop)**: Der Sensor misst das aktuelle Umgebungslicht (Lux). Dieses wird vom HCL-Sollwert abgezogen: viel Tageslicht → Kunstlicht wird reduziert, wenig Tageslicht → Kunstlicht bleibt hoch. Es gibt keine Rückkopplung — der berechnete Wert wird direkt ausgegeben, ohne zu prüfen ob das Ergebnis wirklich stimmt. Das macht den Modus einfach, stabil und für die meisten Räume ausreichend.
- **Konstantlichtregelung (Closed-Loop)**: Der Regler vergleicht den aktuellen Sensorwert laufend mit dem HCL-Sollwert und passt die Leuchten so lange nach, bis beide übereinstimmen. Im Gegensatz zu Open-Loop wird also nicht einmalig berechnet sondern fortlaufend korrigiert. Das ergibt eine präzisere Regelung, erfordert aber sorgfältige Parametrierung (Kp, Totband) damit der Regler nicht schwingt. Empfohlen für Bereiche mit genauer Beleuchtungsanforderung.
