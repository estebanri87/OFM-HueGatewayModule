// HueGatewayModule ETS Script for scanning Hue Bridge lights

function scanHueLights(device, online, progress, context) {
    progress.setText("Verbinde mit Gerät...");
    online.connect();
    
    try {
        progress.setText("Scanne Hue Bridge nach Lichtern...");
        
        // FunctionProperty aufrufen: ObjectIndex 0xA0, PropertyId 10 (Hue Scan)
        var resp = online.invokeFunctionProperty(0xA0, 10, [1]);
        
        if (resp[0] != 0) {
            throw new Error("Fehler beim Scannen der Hue Bridge (Code: " + resp[0] + ")");
        }
        
        // Anzahl der gefundenen Lichter
        var lightCount = resp[1];
        if (lightCount == 0) {
            progress.setText("Keine Lichter gefunden. Bitte prüfen Sie die Bridge-Verbindung.");
            online.disconnect();
            return;
        }
        
        progress.setText("Gefunden: " + lightCount + " Lichter. Lade Details...");
        
        // Lichter-Details abrufen
        var resultText = "=== Gefundene Hue Lichter ===\n\n";
        resultText += "Kopieren Sie die UUID des gewünschten Lichts in den entsprechenden Kanal.\n\n";
        
        for (var i = 0; i < lightCount && i < 20; i++) {
            // Detail-Request für jedes Licht: PropertyId 11, Data = [Licht-Index]
            var detailResp = online.invokeFunctionProperty(0xA0, 11, [i]);
            
            if (detailResp[0] == 0) {
                // Parse Response: [Status, NameLength, Name..., UUIDLength, UUID..., Type]
                var offset = 1;
                var nameLen = detailResp[offset++];
                var name = "";
                for (var j = 0; j < nameLen; j++) {
                    name += String.fromCharCode(detailResp[offset++]);
                }
                
                var uuidLen = detailResp[offset++];
                var uuid = "";
                for (var j = 0; j < uuidLen; j++) {
                    uuid += String.fromCharCode(detailResp[offset++]);
                }
                
                var type = detailResp[offset];
                var typeStr = ["On/Off", "Dimmbar", "Farbtemperatur", "RGB"][type] || "Unbekannt";
                
                resultText += "--- Licht " + (i + 1) + " ---\n";
                resultText += "Name: " + name + "\n";
                resultText += "UUID: " + uuid + "\n";
                resultText += "Typ:  " + typeStr + "\n\n";
            }
        }
        
        progress.setText(resultText);
        
    } catch (e) {
        progress.setText("Fehler: " + e.message);
    } finally {
        online.disconnect();
    }
}

