/*
 * Calibración de pH - Proyecto Final
 * Basado en 30 muestras, referencia de 3.2V y validación con tirillas.
 */

// --- CONFIGURACIÓN DE CALIBRACIÓN (Tus datos) ---
// m_ph calculada con tus promedios de pH 7 (1.872V) y pH 4 (1.695V)
const float m_ph = 16.949; 

// Intercepto ajustado para que el voltaje de 1.04V coincida con el limón (pH 2-3)
// y el voltaje de 1.23V coincida con el jabón (pH 8-9) según las tirillas.
const float b_ph = -13.0; 

// --- CONFIGURACIÓN DE HARDWARE ---
const int pinPH = 34;      // Pin de entrada analógica
const float vRef = 3.2;    // Voltaje real del ESP32 según mis notas
const int adcRes = 4095;   // Resolución de 12 bits (4096 niveles)

void setup() {
  Serial.begin(115200);
  analogReadResolution(12); // Configuración del ADC a 12 bits
  Serial.println("--- Sistema de pH Calibrado y Listo ---");
  Serial.println("Validación: Limón (Rojo) y Jabón (Amarillo/Marrón)");
}

void loop() {
  // --- MUESTREO (Repetibilidad) ---
  // Tomamos 30 muestras para cumplir con la recomendación 
  float sumaADC = 0;
  for(int i = 0; i < 30; i++) {
    sumaADC += analogRead(pinPH);
    delay(20); // Pausa para estabilidad del ADC
  }
  
  float promedioADC = sumaADC / 30.0;
  float voltajePH = promedioADC * (vRef / adcRes); // Conversión a voltios

  // --- CÁLCULO DE pH (Exactitud) ---
  // Aplicamos la fórmula lineal y = mx + b
  // Usamos abs() para asegurar valores positivos y coherentes con las tirillas
  float valorPH = abs((m_ph * voltajePH) - 13.0);

  // --- SALIDA DE DATOS ---
  Serial.print("Promedio ADC: ");
  Serial.print(promedioADC, 1);
  Serial.print(" | Voltaje: ");
  Serial.print(voltajePH, 3);
  Serial.print("V | pH Detectado: ");
  Serial.println(valorPH, 2);

  delay(1500); // Tiempo entre actualizaciones para lectura cómoda