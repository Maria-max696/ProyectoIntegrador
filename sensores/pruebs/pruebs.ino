#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <HX711.h>
#include <math.h>
#include <time.h>
#include <string.h>
void guardarNumero(JsonObject obj, const char *clave, float valor);

// -------------------- CONFIGURACION WI-FI / FIREBASE --------------------
const char *WIFI_SSID = "NOMBRE_DE_TU_RED";
const char *WIFI_PASSWORD = "CLAVE_DE_TU_RED";
const char *FIREBASE_DATABASE_URL = "https://TU-PROYECTO-default-rtdb.firebaseio.com";
// Si las reglas requieren autenticacion, coloca un token valido aqui. No uses reglas publicas en produccion.
const char *FIREBASE_AUTH_TOKEN = "";
const char *SERVIDOR_URL = "http://IP_DEL_SERVIDOR:3000/api/lecturas";

// -------------------- UART2: ESP32 B TX -> ESP32 A RX --------------------
static const int PIN_RX2 = 16;
static const uint32_t BAUDIOS_UART = 115200;
static const size_t MAX_LINEA_JSON_B = 1100;
static const uint32_t TIMEOUT_ESP32_B_MS = 15000UL;
static const int MAX_SENSORES_TEMP_B = 5;
String lineaEntranteB;
bool avisoSinTramaBEmitido = false;

// -------------------- TRES CELDAS HX711 --------------------
static const int CELDA1_DOUT = 22;
static const int CELDA1_SCK  = 23;
static const int CELDA2_DOUT = 19;
static const int CELDA2_SCK  = 21;
static const int CELDA3_DOUT = 27;
static const int CELDA3_SCK  = 14;
static const float CAL_FACTOR_1 = -1220.0f;
static const float CAL_FACTOR_2 = -1220.0f; // confirmar/calibrar individualmente
static const float CAL_FACTOR_3 = -1220.0f; // confirmar/calibrar individualmente

HX711 celda1, celda2, celda3;
bool taraLista1 = false, taraLista2 = false, taraLista3 = false;
bool taraCompleta = false;
uint32_t instanteTaraCompletaMs = 0;
uint32_t ultimaLecturaRAWms = 0;

// -------------------- DOS CAUDALIMETROS --------------------
static const int PIN_CAUDAL1 = 25;
static const int PIN_CAUDAL2 = 26;
static const float PULSOS_POR_LITRO = 600.0f;
static const float UMBRAL_PULSOS_POR_MINUTO = 500.0f;
static const uint8_t VENTANAS_BAJO_CAUDAL_PARA_ALERTA = 2;

volatile uint32_t pulsosCaudal1 = 0;
volatile uint32_t pulsosCaudal2 = 0;
portMUX_TYPE muxPulsos = portMUX_INITIALIZER_UNLOCKED;
float caudal1LMin = 0.0f, caudal2LMin = 0.0f;
float litrosAcumulados1 = 0.0f, litrosAcumulados2 = 0.0f;
uint32_t pulsosVentana1 = 0, pulsosVentana2 = 0;
uint8_t ventanasBajoCaudal1 = 0, ventanasBajoCaudal2 = 0;
bool alertaCaudal1 = false, alertaCaudal2 = false;

// -------------------- BOMBA --------------------
static const int PIN_BOMBA = 33;
static const uint32_t BOMBA_TIEMPO_ENCENDIDA_MS = 300000UL;
static const uint32_t BOMBA_CICLO_MS = 360000UL; 
bool bombaEncendida = false;
bool primerCicloBomba = false;
uint32_t inicioBombaMs = 0;
uint32_t ultimoInicioBombaMs = 0;

// -------------------- PERIODOS --------------------
// Muestreo/control interno: cada 1 segundo (no ralentizar la seguridad/caudal).
// Monitor serial: JSON bonito cada 3 minutos.
// Firebase: actualizacion cada 10 segundos.
// A recibe las tramas UART de B continuamente.
static const uint32_t INTERVALO_MUESTREO_MS = 1000UL;
static const uint32_t INTERVALO_MONITOR_MS = 180000UL;
static const uint32_t INTERVALO_IOT_MS = 10000UL;
static const uint32_t INTERVALO_REINTENTO_WIFI_MS = 15000UL;
uint32_t ultimaMuestraMs = 0;
uint32_t ultimaImpresionMonitorMs = 0;
uint32_t contadorTramas = 0;

struct DatosA {
  uint32_t contador = 0;
  uint32_t uptime_ms = 0;
  float peso1_g = NAN, peso2_g = NAN, peso3_g = NAN;
  float caudal1_l_min = 0.0f, caudal2_l_min = 0.0f;
  float litros1_total = 0.0f, litros2_total = 0.0f;
  uint32_t pulsos1_ventana = 0, pulsos2_ventana = 0;
  bool bomba_encendida = false;
  bool alerta_caudal1 = false, alerta_caudal2 = false;
  bool datosListos = false;
};

DatosA datosActuales;
portMUX_TYPE muxDatos = portMUX_INITIALIZER_UNLOCKED;


// Ultimas mediciones recibidas desde ESP32 B.
struct DatosB {
  bool validos = false;
  uint32_t contador = 0;
  uint32_t uptime_ms = 0;
  uint32_t ultimaRecepcionMs = 0;
  float ph = NAN;
  float ph_voltaje_v = NAN;
  float tds_ppm = NAN;
  float temperatura_c = NAN;
  uint8_t cantidad_sensores_temp = 0;
  float temperaturas_c[MAX_SENSORES_TEMP_B] = {NAN, NAN, NAN, NAN, NAN};
  float nivel_pct = NAN;
  float distancia_nivel_cm = NAN;
};

DatosB datosBRecibidos;
portMUX_TYPE muxDatosB = portMUX_INITIALIZER_UNLOCKED;

void copiarDatosB(DatosB &destino) {
  portENTER_CRITICAL(&muxDatosB);
  destino = datosBRecibidos;
  portEXIT_CRITICAL(&muxDatosB);
}

float leerNumeroJson(JsonVariantConst valor) {
  if (valor.isNull()) return NAN;
  const float resultado = valor.as<float>();
  return isfinite(resultado) ? resultado : NAN;
}

bool procesarTramaESP32B(const String &linea) {
  StaticJsonDocument<1024> doc;
  const DeserializationError error = deserializeJson(doc, linea);
  if (error) {
    Serial.printf("UART B: JSON invalido: %s\n", error.c_str());
    return false;
  }

  const char *schema = doc["schema"] | "";
  const char *source = doc["source"] | "";
  if (strcmp(schema, "escalones_vida_v1") != 0 || strcmp(source, "esp32B") != 0) {
    Serial.println("UART B: trama ignorada; schema/source no esperado.");
    return false;
  }

  JsonObjectConst m = doc["mediciones"].as<JsonObjectConst>();
  if (m.isNull()) {
    Serial.println("UART B: falta objeto mediciones.");
    return false;
  }

  DatosB nuevo;
  nuevo.validos = true;
  nuevo.contador = doc["contador"] | 0UL;
  nuevo.uptime_ms = doc["uptime_ms"] | 0UL;
  nuevo.ultimaRecepcionMs = millis();
  nuevo.ph = leerNumeroJson(m["ph"]);
  nuevo.ph_voltaje_v = leerNumeroJson(m["ph_voltaje_v"]);
  nuevo.tds_ppm = leerNumeroJson(m["tds_ppm"]);
  nuevo.temperatura_c = leerNumeroJson(m["temperatura_c"]);
  nuevo.cantidad_sensores_temp = (uint8_t)constrain((int)(m["sensores_temperatura_detectados"] | 0), 0, MAX_SENSORES_TEMP_B);
  for (int i = 0; i < MAX_SENSORES_TEMP_B; ++i) {
    char nombreCampo[24];
    snprintf(nombreCampo, sizeof(nombreCampo), "temperatura%d_c", i + 1);
    nuevo.temperaturas_c[i] = leerNumeroJson(m[nombreCampo]);
  }
  nuevo.nivel_pct = leerNumeroJson(m["nivel_pct"]);
  nuevo.distancia_nivel_cm = leerNumeroJson(m["distancia_nivel_cm"]);

  portENTER_CRITICAL(&muxDatosB);
  datosBRecibidos = nuevo;
  portEXIT_CRITICAL(&muxDatosB);
  avisoSinTramaBEmitido = false;
  return true;
}

void leerUARTDesdeB() {
  while (Serial2.available() > 0) {
    const char c = (char)Serial2.read();
    if (c == '\n') {
      if (lineaEntranteB.length() > 0) {
        lineaEntranteB.trim();
        procesarTramaESP32B(lineaEntranteB);
      }
      lineaEntranteB = "";
    } else if (c != '\r') {
      if (lineaEntranteB.length() < MAX_LINEA_JSON_B) {
        lineaEntranteB += c;
      } else {
        lineaEntranteB = "";
        Serial.println("UART B: trama demasiado larga; buffer limpiado.");
      }
    }
  }
}

void agregarDatosAObjeto(JsonObject destino, const DatosA &a) {
  destino["datos_validos"] = a.datosListos;
  destino["contador"] = a.contador;
  destino["uptime_ms"] = a.uptime_ms;
  JsonObject m = destino.createNestedObject("mediciones");
  guardarNumero(m, "peso1_g", a.peso1_g);
  guardarNumero(m, "peso2_g", a.peso2_g);
  guardarNumero(m, "peso3_g", a.peso3_g);
  guardarNumero(m, "caudal1_l_min", a.caudal1_l_min);
  guardarNumero(m, "caudal2_l_min", a.caudal2_l_min);
  guardarNumero(m, "litros1_total", a.litros1_total);
  guardarNumero(m, "litros2_total", a.litros2_total);
  m["pulsos1_ventana"] = a.pulsos1_ventana;
  m["pulsos2_ventana"] = a.pulsos2_ventana;
  m["bomba_encendida"] = a.bomba_encendida;
  m["alerta_caudal1"] = a.alerta_caudal1;
  m["alerta_caudal2"] = a.alerta_caudal2;
}

void agregarDatosBObjeto(JsonObject destino, const DatosB &b) {
  const bool online = b.validos && (uint32_t)(millis() - b.ultimaRecepcionMs) <= TIMEOUT_ESP32_B_MS;
  destino["datos_validos"] = b.validos;
  destino["online"] = online;
  destino["contador"] = b.contador;
  destino["uptime_ms"] = b.uptime_ms;
  JsonObject m = destino.createNestedObject("mediciones");
  guardarNumero(m, "ph", b.ph);
  guardarNumero(m, "ph_voltaje_v", b.ph_voltaje_v);
  guardarNumero(m, "tds_ppm", b.tds_ppm);
  guardarNumero(m, "temperatura_c", b.temperatura_c); // primera temperatura valida
  m["sensores_temperatura_detectados"] = b.cantidad_sensores_temp;
  for (int i = 0; i < MAX_SENSORES_TEMP_B; ++i) {
    char nombreCampo[24];
    snprintf(nombreCampo, sizeof(nombreCampo), "temperatura%d_c", i + 1);
    guardarNumero(m, nombreCampo, b.temperaturas_c[i]);
  }
  guardarNumero(m, "nivel_pct", b.nivel_pct);
  guardarNumero(m, "distancia_nivel_cm", b.distancia_nivel_cm);
}

String serializarDatosCompletos(const DatosA &a, const DatosB &b, bool incluirIoT = false, bool formatoBonito = false) {
  StaticJsonDocument<2048> doc;
  doc["schema"] = "escalones_vida_v1";
  doc["source"] = "sistema_completo";
  doc["esp32A_online"] = a.datosListos;
  doc["esp32B_online"] = b.validos && (uint32_t)(millis() - b.ultimaRecepcionMs) <= TIMEOUT_ESP32_B_MS;
  if (incluirIoT) {
    doc["wifi_rssi_dbm"] = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
    const time_t ahora = time(nullptr);
    if (ahora > 1700000000) doc["timestamp_unix"] = (uint32_t)ahora;
    else doc["timestamp_unix"] = nullptr;
  }
  JsonObject objA = doc.createNestedObject("esp32A");
  agregarDatosAObjeto(objA, a);
  JsonObject objB = doc.createNestedObject("esp32B");
  agregarDatosBObjeto(objB, b);

  String salida;
  salida.reserve(1600);
  if (formatoBonito) serializeJsonPretty(doc, salida);
  else serializeJson(doc, salida);
  return salida;
}

bool configuracionIoTLista() {
  return String(WIFI_SSID).length() > 0;
}

void IRAM_ATTR contarPulsoCaudal1() {
  portENTER_CRITICAL_ISR(&muxPulsos);
  pulsosCaudal1++;
  portEXIT_CRITICAL_ISR(&muxPulsos);
}
void IRAM_ATTR contarPulsoCaudal2() {
  portENTER_CRITICAL_ISR(&muxPulsos);
  pulsosCaudal2++;
  portEXIT_CRITICAL_ISR(&muxPulsos);
}

void actualizarBomba() {
  const uint32_t ahora = millis();
  if (bombaEncendida && (uint32_t)(ahora - inicioBombaMs) >= BOMBA_TIEMPO_ENCENDIDA_MS) {
    digitalWrite(PIN_BOMBA, LOW);
    bombaEncendida = false;
    ventanasBajoCaudal1 = ventanasBajoCaudal2 = 0;
    alertaCaudal1 = alertaCaudal2 = false;
    Serial.println("Bomba: APAGADA");
  }

  const bool celdasListas = taraLista1 && taraLista2 && taraLista3 && taraCompleta &&
      (uint32_t)(ahora - instanteTaraCompletaMs) >= 2000UL;
  if (!bombaEncendida && celdasListas &&
      (primerCicloBomba || (uint32_t)(ahora - ultimoInicioBombaMs) >= BOMBA_CICLO_MS)) {
    digitalWrite(PIN_BOMBA, HIGH);
    bombaEncendida = true;
    primerCicloBomba = false;
    inicioBombaMs = ahora;
    ultimoInicioBombaMs = ahora;
    ventanasBajoCaudal1 = ventanasBajoCaudal2 = 0;
    alertaCaudal1 = alertaCaudal2 = false;
    Serial.println("Bomba: ENCENDIDA");
  }
}

void actualizarCaudales(uint32_t intervaloMs) {
  if (intervaloMs == 0) intervaloMs = 1000;
  portENTER_CRITICAL(&muxPulsos);
  pulsosVentana1 = pulsosCaudal1;
  pulsosVentana2 = pulsosCaudal2;
  pulsosCaudal1 = 0;
  pulsosCaudal2 = 0;
  portEXIT_CRITICAL(&muxPulsos);

  caudal1LMin = ((float)pulsosVentana1 * 60000.0f) / (PULSOS_POR_LITRO * intervaloMs);
  caudal2LMin = ((float)pulsosVentana2 * 60000.0f) / (PULSOS_POR_LITRO * intervaloMs);
  litrosAcumulados1 += (float)pulsosVentana1 / PULSOS_POR_LITRO;
  litrosAcumulados2 += (float)pulsosVentana2 / PULSOS_POR_LITRO;

  const float pulsosMin1 = (float)pulsosVentana1 * 60000.0f / intervaloMs;
  const float pulsosMin2 = (float)pulsosVentana2 * 60000.0f / intervaloMs;
  if (bombaEncendida) {
    ventanasBajoCaudal1 = (pulsosMin1 < UMBRAL_PULSOS_POR_MINUTO) ? min((int)ventanasBajoCaudal1 + 1, 255) : 0;
    ventanasBajoCaudal2 = (pulsosMin2 < UMBRAL_PULSOS_POR_MINUTO) ? min((int)ventanasBajoCaudal2 + 1, 255) : 0;
    alertaCaudal1 = ventanasBajoCaudal1 >= VENTANAS_BAJO_CAUDAL_PARA_ALERTA;
    alertaCaudal2 = ventanasBajoCaudal2 >= VENTANAS_BAJO_CAUDAL_PARA_ALERTA;
  } else {
    ventanasBajoCaudal1 = ventanasBajoCaudal2 = 0;
    alertaCaudal1 = alertaCaudal2 = false;
  }
}

float leerPeso(HX711 &celda, bool taraLista) {
  if (!taraLista || !celda.is_ready()) return NAN;
  const float peso = celda.get_units(2);
  return isfinite(peso) ? peso : NAN;
}

void guardarNumero(JsonObject obj, const char *clave, float valor) {
  if (isfinite(valor)) obj[clave] = valor;
  else obj[clave] = nullptr;
}

void copiarDatosActuales(DatosA &destino) {
  portENTER_CRITICAL(&muxDatos);
  destino = datosActuales;
  portEXIT_CRITICAL(&muxDatos);
}

String serializarDatosA(const DatosA &d, bool incluirIoT = false, bool formatoBonito = false) {
  StaticJsonDocument<1024> doc;
  doc["schema"] = "escalones_vida_v1";
  doc["source"] = "esp32A";
  doc["contador"] = d.contador;
  doc["uptime_ms"] = d.uptime_ms;
  JsonObject mediciones = doc.createNestedObject("mediciones");
  guardarNumero(mediciones, "peso1_g", d.peso1_g);
  guardarNumero(mediciones, "peso2_g", d.peso2_g);
  guardarNumero(mediciones, "peso3_g", d.peso3_g);
  guardarNumero(mediciones, "caudal1_l_min", d.caudal1_l_min);
  guardarNumero(mediciones, "caudal2_l_min", d.caudal2_l_min);
  guardarNumero(mediciones, "litros1_total", d.litros1_total);
  guardarNumero(mediciones, "litros2_total", d.litros2_total);
  mediciones["pulsos1_ventana"] = d.pulsos1_ventana;
  mediciones["pulsos2_ventana"] = d.pulsos2_ventana;
  mediciones["bomba_encendida"] = d.bomba_encendida;
  mediciones["alerta_caudal1"] = d.alerta_caudal1;
  mediciones["alerta_caudal2"] = d.alerta_caudal2;
  if (incluirIoT) {
    doc["wifi_rssi_dbm"] = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
    const time_t ahora = time(nullptr);
    if (ahora > 1700000000) doc["timestamp_unix"] = (uint32_t)ahora;
    else doc["timestamp_unix"] = nullptr;
  }
  String json;
  json.reserve(700);
  if (formatoBonito) {
    serializeJsonPretty(doc, json);  // Solo para mostrar en el monitor USB
  } else {
    serializeJson(doc, json);        // Compacto para UART y Firebase
  }
  return json;
}

bool enviarFirebase(const String &ruta, const String &payload, bool crearHistorial) {
  if (WiFi.status() != WL_CONNECTED) return false;
  String url = String(FIREBASE_DATABASE_URL) + ruta + ".json";
  if (strlen(FIREBASE_AUTH_TOKEN) > 0) url += String("?auth=") + FIREBASE_AUTH_TOKEN;

  WiFiClientSecure cliente;
  cliente.setInsecure(); // solo para prototipo: no valida el certificado del servidor
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(3000);
  if (!http.begin(cliente, url)) return false;
  http.addHeader("Content-Type", "application/json");
  const int codigo = crearHistorial
      ? http.POST((uint8_t *)payload.c_str(), payload.length())
      : http.PUT((uint8_t *)payload.c_str(), payload.length());
  if (codigo < 200 || codigo >= 300) {
    Serial.printf("ESP32 A Firebase error HTTP %d en %s\n", codigo, ruta.c_str());
    const String respuesta = (codigo > 0) ? http.getString() : String("");
    if (respuesta.length()) Serial.println(respuesta);
  }
  http.end();
  return codigo >= 200 && codigo < 300;
}

bool enviarServidorLocal(const DatosA &a, const DatosB &b) {
  if (WiFi.status() != WL_CONNECTED) return false;
  const bool bOnline = b.validos && (uint32_t)(millis() - b.ultimaRecepcionMs) <= TIMEOUT_ESP32_B_MS;

  StaticJsonDocument<768> doc;
  JsonObject o = doc.to<JsonObject>();
  o["id_dispositivo"] = "escalera_01";
  for (int i = 0; i < MAX_SENSORES_TEMP_B; ++i) {
    char k[8];
    snprintf(k, sizeof(k), "temp_%d", i + 1);
    guardarNumero(o, k, bOnline ? b.temperaturas_c[i] : NAN);
  }
  guardarNumero(o, "caudal_1", a.caudal1_l_min);
  guardarNumero(o, "caudal_2", a.caudal2_l_min);
  guardarNumero(o, "sld", bOnline ? b.tds_ppm : NAN);
  guardarNumero(o, "ph", bOnline ? b.ph : NAN);
  guardarNumero(o, "altura_agua", bOnline ? b.nivel_pct : NAN);
  guardarNumero(o, "peso_1", a.peso1_g);
  guardarNumero(o, "peso_2", a.peso2_g);
  guardarNumero(o, "peso_3", a.peso3_g);

  String json;
  serializeJson(doc, json);

  WiFiClient cliente;
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(3000);
  if (!http.begin(cliente, SERVIDOR_URL)) return false;
  http.addHeader("Content-Type", "application/json");
  const int codigo = http.POST(json);
  Serial.printf("Servidor local HTTP %d\n", codigo);
  http.end();
  return codigo >= 200 && codigo < 300;
}
void tareaIoT(void *parametro) {
  (void)parametro;
  uint32_t ultimoIntentoWiFi = 0;
  uint32_t ultimoEnvio = 0;
  bool ntpIniciado = false;
  for (;;) {
    if (!configuracionIoTLista()) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    if (WiFi.status() != WL_CONNECTED) {
      if ((uint32_t)(millis() - ultimoIntentoWiFi) >= INTERVALO_REINTENTO_WIFI_MS) {
        ultimoIntentoWiFi = millis();
        Serial.println("ESP32 A: intentando/reintentando Wi-Fi...");
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      }
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    if (!ntpIniciado) {
      configTime(0, 0, "pool.ntp.org", "time.google.com");
      ntpIniciado = true;
    }
    if ((uint32_t)(millis() - ultimoEnvio) >= INTERVALO_IOT_MS) {
      ultimoEnvio = millis();
      DatosA copiaA;
      DatosB copiaB;
      copiarDatosActuales(copiaA);
      copiarDatosB(copiaB);
      if (copiaA.datosListos) {
        enviarServidorLocal(copiaA, copiaB);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}
void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println("\nESCALONES DE VIDA - ESP32 A: celdas + caudales + bomba + IoT");
  Serial2.setRxBufferSize(2048);
  Serial2.begin(BAUDIOS_UART, SERIAL_8N1, PIN_RX2, -1); // A recibe; no transmite por UART2
  lineaEntranteB.reserve(MAX_LINEA_JSON_B);

  celda1.begin(CELDA1_DOUT, CELDA1_SCK);
  celda2.begin(CELDA2_DOUT, CELDA2_SCK);
  celda3.begin(CELDA3_DOUT, CELDA3_SCK);
  celda1.set_scale(CAL_FACTOR_1);
  celda2.set_scale(CAL_FACTOR_2);
  celda3.set_scale(CAL_FACTOR_3);

  pinMode(PIN_CAUDAL1, INPUT_PULLUP);
  pinMode(PIN_CAUDAL2, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_CAUDAL1), contarPulsoCaudal1, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_CAUDAL2), contarPulsoCaudal2, FALLING);

  pinMode(PIN_BOMBA, OUTPUT);
  digitalWrite(PIN_BOMBA, LOW);
  bombaEncendida = false;

  if (configuracionIoTLista()) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  } else {
    Serial.println("AVISO IoT: completa Wi-Fi y FIREBASE_DATABASE_URL en el codigo de ESP32 A.");
  }
  xTaskCreatePinnedToCore(tareaIoT, "IoT_ESP32_A", 10000, nullptr, 1, nullptr, 0);

  ultimaMuestraMs = millis();
  // Permite mostrar el primer registro apenas haya una lectura disponible.
  ultimaImpresionMonitorMs = millis() - INTERVALO_MONITOR_MS;
  Serial.println("UART activo: TX GPIO17 de B -> RX GPIO16 de A; GND comun.");
  Serial.println("Deja las plataformas de las celdas vacias durante el arranque para la tara automatica.");
}

void loop() {
  // La ESP32 A es principal: recibe continuamente el JSON que envia B.
  leerUARTDesdeB();
  actualizarBomba();

  // Tara inicial: las plataformas deben estar vacias al arrancar.
  if (!taraLista1 && celda1.is_ready()) { celda1.tare(5); taraLista1 = true; Serial.println("Celda 1: tara lista"); }
  if (!taraLista2 && celda2.is_ready()) { celda2.tare(5); taraLista2 = true; Serial.println("Celda 2: tara lista"); }
  if (!taraLista3 && celda3.is_ready()) { celda3.tare(5); taraLista3 = true; Serial.println("Celda 3: tara lista"); }
  if (taraLista1 && taraLista2 && taraLista3 && !taraCompleta) {
    taraCompleta = true;
    //Inicar ciclo de bomba apagada.
    instanteTaraCompletaMs = millis();
    ultimoInicioBombaMs = instanteTaraCompletaMs;
    Serial.println("Las tres celdas terminaron tara; espera de estabilizacion de 2 s.");
  }

  // -------- DIAGNOSTICO TEMPORAL DE CELDAS HX711 --------
  if (taraCompleta &&
      (uint32_t)(millis() - ultimaLecturaRAWms) >= 2000UL) {

    ultimaLecturaRAWms = millis();

    Serial.println("\n===== LECTURAS RAW DE CELDAS =====");

    if (celda1.is_ready()) {
      long raw1 = celda1.read();
      Serial.printf("CELDA 1 | RAW: %ld | OFFSET: %ld\n",
                    raw1, celda1.get_offset());
    } else {
      Serial.println("CELDA 1 | HX711 NO DISPONIBLE");
    }

    if (celda2.is_ready()) {
      long raw2 = celda2.read();
      Serial.printf("CELDA 2 | RAW: %ld | OFFSET: %ld\n",
                    raw2, celda2.get_offset());
    } else {
      Serial.println("CELDA 2 | HX711 NO DISPONIBLE");
    }

    if (celda3.is_ready()) {
      long raw3 = celda3.read();
      Serial.printf("CELDA 3 | RAW: %ld | OFFSET: %ld\n",
                    raw3, celda3.get_offset());
    } else {
      Serial.println("CELDA 3 | HX711 NO DISPONIBLE");
    }

    Serial.println("==================================");
  }

  const uint32_t ahora = millis();
  if ((uint32_t)(ahora - ultimaMuestraMs) >= INTERVALO_MUESTREO_MS) {
    const uint32_t intervalo = ahora - ultimaMuestraMs;
    ultimaMuestraMs = ahora;
    actualizarCaudales(intervalo);
    actualizarBomba();

    DatosA nuevos;
    nuevos.contador = contadorTramas++;
    nuevos.uptime_ms = millis();
    nuevos.peso1_g = leerPeso(celda1, taraLista1);
    nuevos.peso2_g = leerPeso(celda2, taraLista2);
    nuevos.peso3_g = leerPeso(celda3, taraLista3);
    nuevos.caudal1_l_min = caudal1LMin;
    nuevos.caudal2_l_min = caudal2LMin;
    nuevos.litros1_total = litrosAcumulados1;
    nuevos.litros2_total = litrosAcumulados2;
    nuevos.pulsos1_ventana = pulsosVentana1;
    nuevos.pulsos2_ventana = pulsosVentana2;
    nuevos.bomba_encendida = bombaEncendida;
    nuevos.alerta_caudal1 = alertaCaudal1;
    nuevos.alerta_caudal2 = alertaCaudal2;
    nuevos.datosListos = true;

    portENTER_CRITICAL(&muxDatos);
    datosActuales = nuevos;
    portEXIT_CRITICAL(&muxDatos);

    // A no envia sus lecturas a B: A recibe las mediciones de B y es la principal.
  }

  // Monitor USB: muestra el JSON combinado cada 3 minutos.
  // La adquisicion/control sigue cada segundo y la recepcion UART nunca se pausa.
  const uint32_t ahoraMonitor = millis();
  if ((uint32_t)(ahoraMonitor - ultimaImpresionMonitorMs) >= INTERVALO_MONITOR_MS) {
    ultimaImpresionMonitorMs = ahoraMonitor;
    DatosA copiaA;
    DatosB copiaB;
    copiarDatosActuales(copiaA);
    copiarDatosB(copiaB);
    if (copiaA.datosListos) {
      Serial.println("\n========== ESP32 A PRINCIPAL | DATOS A + B ==========");
      Serial.println(serializarDatosCompletos(copiaA, copiaB, false, true));
      Serial.println("=====================================================");
      if (alertaCaudal1) Serial.println("ALERTA: caudalimetro 1 bajo con la bomba encendida.");
      if (alertaCaudal2) Serial.println("ALERTA: caudalimetro 2 bajo con la bomba encendida.");
      if (!copiaB.validos || (uint32_t)(millis() - copiaB.ultimaRecepcionMs) > TIMEOUT_ESP32_B_MS) {
        Serial.println("AVISO: no hay datos recientes de ESP32 B.");
      }
    }
  }

  DatosB estadoB;
  copiarDatosB(estadoB);
  if (estadoB.validos && (uint32_t)(millis() - estadoB.ultimaRecepcionMs) > TIMEOUT_ESP32_B_MS && !avisoSinTramaBEmitido) {
    Serial.println("AVISO: se perdio la comunicacion con ESP32 B.");
    avisoSinTramaBEmitido = true;
  }
  delay(2);
}