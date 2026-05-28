#include <Wire.h>            // Comunicacion I2C para la pantalla OLED
#include <Adafruit_GFX.h>    // Libreria base de graficos para pantallas Adafruit
#include <Adafruit_SH110X.h> /
+/ Libreria especifica para pantallas OLED con controlador SH1106
#include <ESP8266WiFi.h>     // Permite conectar el ESP8266 a redes WiFi
#include <PubSubClient.h>    // Cliente MQTT para publicar y suscribirse a topicos
#include <ArduinoJson.h>     // Permite parsear mensajes en formato JSON

// ── CONFIGURACION WIFI Y MQTT ─────────────────────────────────
const char* WIFI_SSID   = "";       // Nombre de la red WiFi a la que se conecta
const char* WIFI_PASS   = "";  // Contrasena de la red WiFi
const char* MQTT_BROKER = "";   // IP del PC donde corre el broker Mosquitto

// ── DEFINICION DE PINES ───────────────────────────────────────
#define LED_ROJO  D6  // LED rojo: se enciende cuando el precio baja
#define LED_VERDE D5  // LED verde: se enciende cuando el precio sube
#define BUZZER    D8  // Buzzer: emite sonido cuando el precio cambia o supera el umbral
#define BOTON_1   D4  // Boton fisico 1: selecciona la primera criptomoneda
#define BOTON_2   D7  // Boton fisico 2: selecciona la segunda criptomoneda

// Pantalla OLED 128x64 pixeles con controlador SH1106 via I2C
Adafruit_SH1106G display(128, 64, &Wire, -1);

// Cliente WiFi y cliente MQTT que usa esa conexion WiFi
WiFiClient espClient;
PubSubClient mqtt(espClient);

// ── MONEDAS CONFIGURADAS ──────────────────────────────────────
String monedaBtn1   = "BTC";  // Moneda asignada al boton 1 (por defecto Bitcoin)
String monedaBtn2   = "SOL";  // Moneda asignada al boton 2 (por defecto Solana)
String monedaActual = "BTC";  // Moneda que se esta mostrando en pantalla ahora mismo

// ── PRECIOS ───────────────────────────────────────────────────
float precioBTC = 0;  // Ultimo precio recibido de Bitcoin
float precioSOL = 0;  // Ultimo precio recibido de Solana
float antBTC    = 0;  // Precio anterior de Bitcoin (para comparar si subio o bajo)
float antSOL    = 0;  // Precio anterior de Solana

// ── ESTADO ANTERIOR DE LOS BOTONES ───────────────────────────
// Se usan para detectar cuando el boton pasa de no presionado a presionado
bool antBoton1 = HIGH;  // Estado anterior del boton 1
bool antBoton2 = HIGH;  // Estado anterior del boton 2

// ── FUNCIONES DE LEDS ─────────────────────────────────────────

// Apaga todos los LEDs (LOW = apagado porque son activos en LOW)
void apagarLEDs() {
  digitalWrite(LED_ROJO,  HIGH);
  digitalWrite(LED_VERDE, HIGH);
}

// Apaga todos los LEDs cuando el precio se mantiene estable
void ledEstable() {
  apagarLEDs();
}

// ── FUNCIONES DE BUZZER ───────────────────────────────────────

// Emite un pitido corto cuando el precio sube o baja
void beepCorto() {
  tone(BUZZER, 1600);  // Frecuencia de 1600 Hz
  delay(150);          // Durante 150 milisegundos
  noTone(BUZZER);      // Apagar el buzzer
}

// ── FUNCIONES DE UTILIDAD ─────────────────────────────────────

// Convierte el nombre de una moneda a su abreviatura estandar en mayusculas
// Ejemplo: "bitcoin" -> "BTC", "solana" -> "SOL"
String normalizar(String m) {
  m.trim();        // Eliminar espacios al inicio y al final
  m.toUpperCase(); // Convertir a mayusculas
  if (m == "BITCOIN") return "BTC";
  if (m == "SOLANA")  return "SOL";
  return m; // Si no coincide con ninguno, retornar tal cual
}

// Retorna el precio actual de la moneda indicada
float precioDe(String moneda) {
  moneda = normalizar(moneda);
  if (moneda == "BTC") return precioBTC;
  if (moneda == "SOL") return precioSOL;
  return 0; // Si la moneda no existe, retornar 0
}

// Retorna el precio anterior de la moneda indicada (para comparar variacion)
float anteriorDe(String moneda) {
  moneda = normalizar(moneda);
  if (moneda == "BTC") return antBTC;
  if (moneda == "SOL") return antSOL;
  return 0;
}

// ── FUNCIONES DE PANTALLA ─────────────────────────────────────

// Muestra en la pantalla OLED el nombre de la moneda, su precio y el estado
void mostrarPantalla(String moneda, float precio, String estado) {
  display.clearDisplay(); // Limpiar pantalla antes de dibujar
  display.setTextColor(SH110X_WHITE);

  // Nombre de la moneda en tamano grande en la parte superior
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println(moneda);

  // Linea horizontal separadora
  display.drawLine(0, 18, 128, 18, SH110X_WHITE);

  // Precio de la moneda con 2 decimales
  display.setTextSize(1);
  display.setCursor(0, 25);
  display.print("$");
  display.println(String(precio, 2));

  // Estado del precio: SUBE, BAJA o ESTABLE
  display.setTextSize(2);
  display.setCursor(0, 42);
  display.println(estado);

  // Enviar todo a la pantalla para que se muestre
  display.display();
}

// Compara el precio actual con el anterior, controla los LEDs y retorna el estado
String obtenerEstado(float actual, float anterior) {
  // Si no hay precio anterior, no se puede comparar
  if (anterior == 0) {
    ledEstable();
    return "ESTABLE";
  }
  // Precio subio: encender LED verde y emitir beep
  if (actual > anterior) {
    apagarLEDs();
    digitalWrite(LED_VERDE, LOW); // LOW enciende el LED
    beepCorto();
    return "SUBE";
  }
  // Precio bajo: encender LED rojo y emitir beep
  if (actual < anterior) {
    apagarLEDs();
    digitalWrite(LED_ROJO, LOW); // LOW enciende el LED
    beepCorto();
    return "BAJA";
  }
  // Precio igual: apagar todos los LEDs
  ledEstable();
  return "ESTABLE";
}

// Actualiza la pantalla con la informacion de la moneda que se esta mostrando
void mostrarMonedaActual() {
  float actual   = precioDe(monedaActual);
  float anterior = anteriorDe(monedaActual);
  String estado  = obtenerEstado(actual, anterior);
  mostrarPantalla(monedaActual, actual, estado);
}

// ── PROCESAMIENTO DE PRECIOS ──────────────────────────────────

// Recibe el nuevo precio de una moneda, lo guarda y actualiza la pantalla si es necesario
void procesarPrecio(String moneda, float nuevoPrecio) {
  moneda = normalizar(moneda);

  // Guardar el precio actual como anterior y actualizar con el nuevo
  if (moneda == "BTC") {
    antBTC    = precioBTC;   // Guardar precio actual como anterior
    precioBTC = nuevoPrecio; // Actualizar con el nuevo precio
  } else if (moneda == "SOL") {
    antSOL    = precioSOL;
    precioSOL = nuevoPrecio;
  }

  // Solo actualizar la pantalla si la moneda que cambio es la que se esta mostrando
  if (moneda == monedaActual) {
    mostrarMonedaActual();
  }
}

// ── MQTT: RECEPCION DE MENSAJES ───────────────────────────────

// Se ejecuta automaticamente cada vez que llega un mensaje MQTT
void callback(char* topic, byte* payload, unsigned int length) {
  String topico  = String(topic);
  String mensaje = "";

  // Convertir el payload de bytes a String
  for (unsigned int i = 0; i < length; i++) {
    mensaje += (char)payload[i];
  }

  Serial.println("MQTT [" + topico + "]: " + mensaje);

  // Precio de Bitcoin recibido
  if (topico == "cripto/precios/btc") {
    procesarPrecio("BTC", mensaje.toFloat());

  // Precio de Solana recibido
  } else if (topico == "cripto/precios/sol") {
    procesarPrecio("SOL", mensaje.toFloat());

  // Mensaje de configuracion recibido desde el Configurador Java
  } else if (topico == "cripto/config/monitor01") {
    StaticJsonDocument<256> doc;
    // Parsear el JSON de configuracion
    if (!deserializeJson(doc, mensaje)) {
      // Actualizar las monedas asignadas a cada boton
      monedaBtn1   = normalizar(doc["btn1"].as<String>());
      monedaBtn2   = normalizar(doc["btn2"].as<String>());
      // Cambiar a mostrar la moneda del boton 1 por defecto
      monedaActual = monedaBtn1;
      mostrarMonedaActual();
    }
  }
}

// ── MQTT: CONEXION ────────────────────────────────────────────

// Intenta conectar al broker MQTT hasta lograrlo, mostrando el estado en pantalla
void conectarMQTT() {
  while (!mqtt.connected()) {
    mostrarPantalla("MQTT", 0, "CONECT"); // Mostrar que esta intentando conectar
    if (mqtt.connect("monitor01")) {      // Intentar conectar con ID "monitor01"
      // Suscribirse a los topicos necesarios para recibir precios y configuracion
      mqtt.subscribe("cripto/precios/btc");
      mqtt.subscribe("cripto/precios/sol");
      mqtt.subscribe("cripto/config/monitor01");
      mostrarPantalla("MQTT", 0, "OK");  // Mostrar que conecto exitosamente
      delay(1000);
      mostrarMonedaActual(); // Mostrar la pantalla principal
    } else {
      delay(3000); // Esperar 3 segundos antes de reintentar
    }
  }
}

// ── SETUP: CONFIGURACION INICIAL ──────────────────────────────

void setup() {
  Serial.begin(115200); // Iniciar comunicacion serial para ver logs en el PC

  // Configurar pines de salida y entrada
  pinMode(LED_ROJO,  OUTPUT);
  pinMode(LED_VERDE, OUTPUT);
  pinMode(BUZZER,    OUTPUT);
  pinMode(BOTON_1,   INPUT_PULLUP); // INPUT_PULLUP: boton conectado a GND sin resistencia externa
  pinMode(BOTON_2,   INPUT_PULLUP);

  apagarLEDs();    // Asegurar que los LEDs esten apagados al iniciar
  noTone(BUZZER);  // Asegurar que el buzzer este apagado al iniciar

  // Iniciar comunicacion I2C en los pines D2 (SDA) y D1 (SCL)
  Wire.begin(D2, D1);
  // Iniciar la pantalla OLED con direccion I2C 0x3C
  display.begin(0x3C, true);

  // Mostrar mensaje de conexion WiFi en pantalla
  mostrarPantalla("WiFi", 0, "CONECT");

  // Conectar a la red WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  // Esperar hasta que la conexion WiFi sea exitosa
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print("."); // Imprimir puntos en el serial mientras espera
  }
  Serial.println();
  Serial.println("WiFi conectado");

  // Mostrar que el WiFi conecto exitosamente
  mostrarPantalla("WiFi", 0, "OK");
  delay(1000);

  // Configurar el broker MQTT y la funcion que recibe mensajes
  mqtt.setServer(MQTT_BROKER, 1883);
  mqtt.setCallback(callback);

  // Conectar al broker MQTT
  conectarMQTT();
}

// ── LOOP: CICLO PRINCIPAL ─────────────────────────────────────

void loop() {
  // Reconectar al broker si se perdio la conexion
  if (!mqtt.connected()) {
    conectarMQTT();
  }

  // Mantener activa la comunicacion MQTT y procesar mensajes entrantes
  mqtt.loop();

  // Leer el estado actual de los botones
  bool estadoBoton1 = digitalRead(BOTON_1);
  bool estadoBoton2 = digitalRead(BOTON_2);

  // Detectar cuando el boton 1 pasa de no presionado (HIGH) a presionado (LOW)
  if (antBoton1 == HIGH && estadoBoton1 == LOW) {
    monedaActual = monedaBtn1;  // Cambiar a la moneda del boton 1
    mostrarMonedaActual();      // Actualizar la pantalla
    delay(250);                 // Pequena pausa para evitar rebotes del boton
  }

  // Detectar cuando el boton 2 pasa de no presionado a presionado
  if (antBoton2 == HIGH && estadoBoton2 == LOW) {
    monedaActual = monedaBtn2;  // Cambiar a la moneda del boton 2
    mostrarMonedaActual();
    delay(250);
  }

  // Guardar el estado actual de los botones para comparar en el siguiente ciclo
  antBoton1 = estadoBoton1;
  antBoton2 = estadoBoton2;
}
