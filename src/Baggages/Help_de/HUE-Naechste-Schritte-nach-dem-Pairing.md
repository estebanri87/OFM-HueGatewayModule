### Nächste Schritte nach dem Pairing

Nach erfolgreicher Kopplung (LED grün dauerhaft) erfolgt die eigentliche Gerätezuordnung:

1. **Hue-Geräte laden** öffnen: `http://<IP-des-OpenKNX-Geräts>/openknx/hue/scan`
2. In der Liste die gewünschten Leuchten inkl. UUID erfassen.
3. In ETS je konfiguriertem Kanal die passende UUID in **Hue Lampen-ID (UUID)** eintragen.
4. Pro Kanal Lampentyp, Synchronisationsrichtung und Polling prüfen.
5. Download ausführen und Funktion testen (Schalten, ggf. Helligkeit/Farbtemperatur/RGB).

Empfehlung: Erst mit 1-2 Kanälen testen, danach auf alle Kanäle ausrollen.

