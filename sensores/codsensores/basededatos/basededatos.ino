#include <WiFi.h>
#include <HTTPClient.h>

// 1. Escribe aquí el nombre y contraseña de tu red Wi-Fi (zona portátil o red local)
const char* ssid = "NOMBRE_DE_TU_RED";
const char* password = "CLAVE_DE_TU_RED";

// 2. Coloca la IP de tu laptop (la que te sale en ipconfig) manteniendo el puerto 3000
const char* serverUrl = "http://IP_DEL_SERVIDOR:3000/api/lecturas";

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);

  Serial.print("Conectando a Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n¡Conectado exitosamente al Wi-Fi!");
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");

    // Datos del sensor (aquí luego sustituyes por las lecturas reales de tu pin analógico)
    float voltaje = 2.15;
    int valorCalculado = 2668;
    String sensorNombre = "ESP32_Hidroponia";

    // Creamos la estructura JSON que espera tu servidor Node.js
    String jsonPayload = "{\"voltaje\":" + String(voltaje) + 
                         ",\"valor_calculado\":" + String(valorCalculado) + 
                         ",\"sensor_nombre\":\"" + sensorNombre + "\"}";

    // Enviamos la petición POST
    int httpResponseCode = http.POST(jsonPayload);

    if (httpResponseCode > 0) {
      Serial.print("Código de respuesta HTTP: ");
      Serial.println(httpResponseCode);
      String response = http.getString();
      Serial.println("Respuesta del servidor: " + response);
    } else {
      Serial.print("Error al enviar la petición POST. Código: ");
      Serial.println(httpResponseCode);
    }

    http.end();
  } else {
    Serial.println("Error: Conexión Wi-Fi perdida");
  }

  // Envía un nuevo dato cada 2 minutos 
  delay(120000);
}