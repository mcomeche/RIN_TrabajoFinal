#include <SPI.h>
#include <Wire.h>
#include <mcp_can.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <time.h>
#include <ArduinoJson.h>

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
#define SDA_PIN 21
#define SCL_PIN 19

// Semáforos
#define SEMAFORO_ENTRADA_VERDE 2
#define SEMAFORO_ENTRADA_ROJO 3
#define SEMAFORO_SALIDA_VERDE 4
#define SEMAFORO_SALIDA_ROJO 7

// CAN
#define CAN_CS 10
#define SPI_SCK 13
#define SPI_MISO 12
#define SPI_MOSI 11

// WiFi
const char* ssid = "vodafoneBA1417";
const char* password = "K9Y7G7X3A6F7ANL9";
#define WIFI_CONNECTION_TIMEOUT_SECONDS 15

// MQTT
#define MQTT_SERVER_IP "broker.hivemq.com"
#define MQTT_SERVER_PORT 1883
#define MQTT_TOPIC "RIN"
#define MQTT_CONNECTION_RETRIES 3

// Temporización
#define TIEMPO_VERDE 5000  // 5 segundos en verde
#define TIEMPO_AMARILLO 2000  // 2 segundos en amarillo (opcional)
#define TIEMPO_MINIMO_ENTRE_CAMBIOS 3000  // 3 segundos entre cambios

WiFiClient espWifiClient;
PubSubClient mqttClient(espWifiClient);
String deviceID = String("ESP32ClientRIN_") + String(WiFi.macAddress());
String mqttClientID;

// CAN object
MCP_CAN CAN(CAN_CS);
Preferences preferences;
int contador = 0;
bool cocheAntesEntrada = false;
bool cocheAntesSalida = false;

// Variables para control de tiempo
unsigned long ultimoCambio = 0;
bool ultimaPrioridadEntrada = false; // Alternar prioridad

void wifi_reconnect(uint retries) {
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  uint8_t r = 0;
  while (WiFi.status() != WL_CONNECTED && r<retries ) {
    r++;
    delay(1000);
    Serial.print(".");
  }
  Serial.println("");

  if ( WiFi.isConnected() ) {
    Serial.println("Conectado al WiFi");
    Serial.print("Local ESP32 IP: ");
    Serial.println(WiFi.localIP().toString());
  } else {
    Serial.println("No se puedo conectar al WiFi");
  }
}

void wifi_connect() {
  delay(10);
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());
  wifi_reconnect(WIFI_CONNECTION_TIMEOUT_SECONDS);
}

void wifi_loop() {
  if ( !WiFi.isConnected() )
    wifi_reconnect(WIFI_CONNECTION_TIMEOUT_SECONDS);
}

void mostrarOLED(String linea1, String linea2) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(linea1);
  display.setCursor(0, 16);
  display.setTextSize(1);
  display.println(linea2);
  display.display();
}

void mqttCallback(char* topic, byte* message, unsigned int length) {
  // No necesario para este proyecto aún
}

void mqtt_loop() {
  if (!mqttClient.connected()) {
    mqtt_reconnect(MQTT_CONNECTION_RETRIES);
    mqtt_subscribe(MQTT_TOPIC);
  }
  mqttClient.loop();
}

void mqtt_connect(String clientID) {
    mqttClientID = String(clientID);
    mqttClient.setServer(MQTT_SERVER_IP, MQTT_SERVER_PORT);
    mqttClient.setCallback(mqttCallback);
    mqtt_reconnect(MQTT_CONNECTION_RETRIES);
}

void mqtt_reconnect(int retries) {
  if(!WiFi.isConnected() )
    return;

  if(!mqttClient.connected())
    Serial.println("Disconnected from the MQTT broker");

  int r=0;
  while(!mqttClient.connected() && r<retries) {
    r++;

    Serial.print("Attempting an MQTT connection to: 'mqtt://");
    Serial.print(MQTT_SERVER_IP);
    Serial.print(":");
    Serial.print(MQTT_SERVER_PORT);
    Serial.print("' with client-id: '");
    Serial.print(mqttClientID);
    Serial.println("' ... ");
    
    if (mqttClient.connect(mqttClientID.c_str())) {
      Serial.println("-=- Connected to MQTT Broker");
      mqtt_publish(MQTT_TOPIC, "Hola");
      delay(1000);
    } else {
      Serial.print("MQTT connection failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void mqtt_subscribe(const char* topic) {
  if (!mqttClient.connected() ) {
    Serial.println("Cannot subscribe to topic ... the MQTT Client is disconnected!!");
    return;
  }
  Serial.print("Subscribed to topic: ");
  Serial.println(topic);
  mqttClient.subscribe(topic);
}

void mqtt_publish(const char* topic, String mensaje) {
  if (!mqttClient.connected()) {
    Serial.println("Cannot send message through the topic ... the MQTT Client is disconnected!!");
    return;
  }
  Serial.println("~~>> PUBLISHING an MQTT message:");
  Serial.println(topic);
  Serial.println(mensaje);
  mqttClient.publish(topic, mensaje.c_str());
}

void configurarHoraNTP() {
  configTzTime("CET-1CEST,M3.5.0/2,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    Serial.println("Esperando sincronización NTP...");
    delay(1000);
  }
  Serial.println("Hora NTP sincronizada");
}

String getHoraActual() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "00:00:00";
  }
  char hora[9];
  sprintf(hora, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  return String(hora);
}

void controlSemaforos(bool entradaVerde, bool salidaVerde) {
  digitalWrite(SEMAFORO_ENTRADA_VERDE, entradaVerde);
  digitalWrite(SEMAFORO_ENTRADA_ROJO, !entradaVerde);
  digitalWrite(SEMAFORO_SALIDA_VERDE, salidaVerde);
  digitalWrite(SEMAFORO_SALIDA_ROJO, !salidaVerde);
}

void setup() {
  Serial.begin(9600);
  
  // Inicializar semáforos (ambos en rojo)
  pinMode(SEMAFORO_ENTRADA_VERDE, OUTPUT);
  pinMode(SEMAFORO_ENTRADA_ROJO, OUTPUT);
  pinMode(SEMAFORO_SALIDA_VERDE, OUTPUT);
  pinMode(SEMAFORO_SALIDA_ROJO, OUTPUT);
  controlSemaforos(false, false);

  // Iniciar WiFi
  wifi_connect();

  // Iniciar MQTT
  mqtt_connect(deviceID);
  configurarHoraNTP();
  
  // Inicializar pantalla OLED
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Error al iniciar OLED");
    while (true);
  }
  mostrarOLED("Iniciando...", "");

  // Iniciar CAN
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, CAN_CS);
  if (CAN_OK != CAN.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ)) {
    mostrarOLED("Error CAN", "Reiniciar");
    Serial.println("Fallo inicio CAN");
    while (true);
  }

  CAN.setMode(MCP_NORMAL);
  mostrarOLED("Sistema listo", "Esperando CAN");
  Serial.println("CAN Iniciado correctamente");
}

void loop() {
  unsigned long tiempoActual = millis();
  
  if (CAN_MSGAVAIL == CAN.checkReceive()) {
    byte len = 0;
    byte buf[8];
    unsigned long rxId = 0;

    // Leer mensaje
    CAN.readMsgBuf(&rxId, &len, buf);

    int distanciaEntrada = (buf[0] << 8) | buf[1];
    int distanciaSalida = (buf[2] << 8) | buf[3];
    
    bool cocheEntrando = distanciaEntrada > 0 && distanciaEntrada < 10;
    bool cocheSaliendo = distanciaSalida > 0 && distanciaSalida < 10;
    String estadoEntrada = "ENTRADA";
    String estadoSalida = "SALIDA";
    String hora = getHoraActual();

    // Mostrar estado del parking
    if (contador >= 10) {
      controlSemaforos(false, false); // Ambos semáforos en rojo
      mostrarOLED("Parking LLENO", "Coches: " + String(contador));
      
      // Si hay coche intentando entrar cuando está lleno
      if (cocheEntrando) {
        DynamicJsonDocument doc(256);
        doc["evento"] = "intento_entrada_lleno";
        doc["hora"] = hora;
        doc["totalCoches"] = contador;
        doc["distanciaEntrada"] = distanciaEntrada;
        doc["distanciaSalida"] = distanciaSalida;
        String jsonMsg;
        serializeJson(doc, jsonMsg);
        mqtt_publish("RIN", jsonMsg);
      }
      
      // Solo procesar salidas si el parking está lleno
      if (cocheSaliendo && tiempoActual - ultimoCambio > TIEMPO_MINIMO_ENTRE_CAMBIOS) {
        controlSemaforos(false, true); // Salida verde
        contador--;
        ultimoCambio = tiempoActual;
        
        DynamicJsonDocument doc(256);
        doc["evento"] = "salida";
        doc["hora"] = hora;
        doc["totalCoches"] = contador;
        doc["distanciaEntrada"] = distanciaEntrada;
        doc["distanciaSalida"] = distanciaSalida;
        String jsonMsg;
        serializeJson(doc, jsonMsg);
        mqtt_publish("RIN", jsonMsg);
      }
    } 
    else {
      // Parking no lleno - comportamiento normal
      // Solo procesar cambios si ha pasado el tiempo mínimo entre cambios
      if (tiempoActual - ultimoCambio > TIEMPO_MINIMO_ENTRE_CAMBIOS) {
        
        // Caso 1: Solo hay coche en entrada
        if (cocheEntrando && !cocheSaliendo) {
          controlSemaforos(true, false);
          estadoEntrada = "ENTRADA: VERDE";
          estadoSalida = "SALIDA: ROJO";
          contador++;
          ultimoCambio = tiempoActual;
          ultimaPrioridadEntrada = true;
          
          DynamicJsonDocument doc(256);
          doc["evento"] = "entrada";
          doc["hora"] = hora;
          doc["totalCoches"] = contador;
          doc["distanciaEntrada"] = distanciaEntrada;
          doc["distanciaSalida"] = distanciaSalida;
          String jsonMsg;
          serializeJson(doc, jsonMsg);
          mqtt_publish("RIN", jsonMsg);
        } 
        // Caso 2: Solo hay coche en salida y hay coches dentro
        else if (cocheSaliendo && !cocheEntrando && contador > 0) {
          controlSemaforos(false, true);
          estadoEntrada = "ENTRADA: ROJO";
          estadoSalida = "SALIDA: VERDE";
          contador--;
          ultimoCambio = tiempoActual;
          ultimaPrioridadEntrada = false;
          
          DynamicJsonDocument doc(256);
          doc["evento"] = "salida";
          doc["hora"] = hora;
          doc["totalCoches"] = contador;
          doc["distanciaEntrada"] = distanciaEntrada;
          doc["distanciaSalida"] = distanciaSalida;
          String jsonMsg;
          serializeJson(doc, jsonMsg);
          mqtt_publish("RIN", jsonMsg);
        }
        // Caso 3: Coches en ambos lados
        else if (cocheEntrando && cocheSaliendo) {
          // Alternar prioridad
          if (ultimaPrioridadEntrada && contador > 0) {
            // Dar prioridad a salida esta vez
            controlSemaforos(false, true);
            estadoEntrada = "ENTRADA: ROJO";
            estadoSalida = "SALIDA: VERDE";
            contador--;
            ultimaPrioridadEntrada = false;
            
            DynamicJsonDocument doc(256);
            doc["evento"] = "salida";
            doc["hora"] = hora;
            doc["totalCoches"] = contador;
            doc["distanciaEntrada"] = distanciaEntrada;
            doc["distanciaSalida"] = distanciaSalida;
            String jsonMsg;
            serializeJson(doc, jsonMsg);
            mqtt_publish("RIN", jsonMsg);

          } else {
            // Dar prioridad a entrada
            controlSemaforos(true, false);
            estadoEntrada = "ENTRADA: VERDE";
            estadoSalida = "SALIDA: ROJO";
            contador++;
            ultimaPrioridadEntrada = true;
            
            DynamicJsonDocument doc(256);
            doc["evento"] = "entrada";
            doc["hora"] = hora;
            doc["totalCoches"] = contador;
            doc["distanciaEntrada"] = distanciaEntrada;
            doc["distanciaSalida"] = distanciaSalida;
            String jsonMsg;
            serializeJson(doc, jsonMsg);
            mqtt_publish("RIN", jsonMsg);
          }
          ultimoCambio = tiempoActual;
        }
        // Caso 4: Ningún coche - ambos en rojo
        else {
          controlSemaforos(false, false);
          estadoEntrada = "ENTRADA: ROJO";
          estadoSalida = "SALIDA: ROJO";
        }
      }
      
      // Mostrar en OLED (solo si no está lleno)
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.print("Coches dentro: ");
      display.println(contador);
      display.setCursor(0, 16);
      display.print("Prioridad: ");
      display.println(ultimaPrioridadEntrada ? "ENTRADA" : "SALIDA");
      display.display();
    }
  }

  // Volver a rojo después del tiempo en verde
  if (ultimoCambio > 0 && (tiempoActual - ultimoCambio) > TIEMPO_VERDE) {
    controlSemaforos(false, false);
    ultimoCambio = 0; // Resetear el temporizador
    
    // Si el parking está lleno, volver a mostrar el mensaje
    if (contador >= 10) {
      mostrarOLED("Parking LLENO", "Coches: " + String(contador));
    }
  }

  wifi_loop();
  mqtt_loop();
  delay(100);
}

