#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <HardwareSerial.h>
#include <esp_now.h>

// Configuración de los pines para el ESP32 FireBeetle
#define RX_PIN 16
#define TX_PIN 17
#define LED_VERDE 25     // GPIO25
#define LED_ROJO 26      // GPIO26
#define LED_AMARILLO 27  // GPIO27
#define BOMBA 9

HardwareSerial mySerial(1);

// Configuración de la red Wi-Fi
const char* ssid = "dani";
const char* password = "certificado";

// Configuración de OpenWeatherMap
const char* apiKey = "c230d3b8f999062d20466f9e1974b892";
String weatherData;

// Configuración del cliente NTP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);  // Ajusta la zona horaria según sea necesario

// Variables para controlar las peticiones
bool sunsetRequested = false;
bool rainForecastRequested = false;
String sunsetTime = ""; // Horario de atardecer
String poblacion = "";
String latitude = "";
String longitude = "";
bool rainInNext12Hours = false;
String planta = "";
int humedad = 0;  // Variable para almacenar el valor de humedad
int humedadIdeal = 0; // Variable para almacenar la humedad ideal para cada planta en concreto
int tipoPlanta = 0;  // 0 = Tomate, 1 = Maíz, 2 = Trigo
int sunsetHour = 0;
int sunsetMinute = 0;
int checkHour = 0;
int checkMinute = 0;


// Inicializamos el servidor web
WebServer server(80);

// Estructura para recibir el mensaje
typedef struct Message {
  int valorHumedad;  
} Message;

Message receivedMessage;  // Instancia para almacenar el mensaje recibido

void onDataReceive(const esp_now_recv_info_t* recvInfo, const uint8_t* incomingData, int len) {
  // Copiar los datos recibidos en la estructura
  memcpy(&receivedMessage, incomingData, sizeof(receivedMessage));
  // Guardar el valor de humedad en la variable
  humedad = receivedMessage.valorHumedad;
}

void configurarPortalCautivo() {
  // Página principal del portal cautivo
  server.on("/", []() {
    String pagina = "<h1>Portal Cautivo</h1>";
    pagina += "<p>Bienvenido al portal cautivo. Seleccione una opcion:</p>";
    pagina += "<form action='/guardar' method='POST'>";
    pagina += "<label for='planta'>Seleccione una planta:</label><br>";
    pagina += "<select name='planta' id='planta'>";
    pagina += "<option value='0'>Tomate</option>";
    pagina += "<option value='1'>Maiz</option>";
    pagina += "<option value='2'>Trigo</option>";
    pagina += "</select><br><br>";
    pagina += "<input type='submit' value='Guardar'>";
    pagina += "</form>";
    server.send(200, "text/html", pagina);
  });

  // Página para guardar la selección
  server.on("/guardar", HTTP_POST, []() {
    if (server.hasArg("planta")) {
      tipoPlanta = server.arg("planta").toInt();
      planta = server.arg("planta");
      Serial.print("Planta seleccionada: ");
      Serial.println(planta);
      server.send(200, "text/plain", "Planta seleccionada correctamente.");
    } else {
      server.send(400, "text/plain", "No se recibió la selección.");
    }
  });
}

void connectWiFi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Conectando a WiFi...");
  }
  Serial.println("Conectado a la red WiFi");
  Serial.print("IP del portal cautivo: ");
  Serial.println(WiFi.localIP());
}


void obtenerLocalizacion() {
  // Enviar comando AT para obtener datos GPS
  String gpsData = sendATCommand("AT+CGNSINF", 1000, true);
  // Procesar la respuesta para extraer latitud y longitud
  if (gpsData.indexOf("+CGNSINF: 1,1,") != -1) {
    int startIndex = gpsData.indexOf(",") + 1;
    startIndex = gpsData.indexOf(",", startIndex) + 1;
    startIndex = gpsData.indexOf(",", startIndex) + 1;
    int endIndex = gpsData.indexOf(",", startIndex);
    latitude = gpsData.substring(startIndex, endIndex);
    startIndex = endIndex + 1;
    endIndex = gpsData.indexOf(",", startIndex);
    longitude = gpsData.substring(startIndex, endIndex);
    obtenerLocalidad();
  } else {
    Serial.println("Waiting for GPS fix...");
  }
}

String sendATCommand(const char* command, const int timeout, const bool debug) {
  mySerial.println(command);
  long int time = millis();
  String response = "";
  while ((time + timeout) > millis()) {
    while (mySerial.available()) {
      char c = mySerial.read();
      response += c;
      if (debug) Serial.write(c);
    }
  }
  return response;
}


void obtenerLocalidad() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    // Construir URL para la solicitud
    String url = "https://nominatim.openstreetmap.org/reverse?format=jsonv2&lat=" + latitude + "&lon=" + longitude;
    http.begin(url);                                         // Inicia conexión
    http.addHeader("User-Agent", "ESP32 Nominatim Client");  // Obligatorio por política de Nominatim
    // Enviar solicitud GET
    int httpCode = http.GET();
    if (httpCode > 0) {
      // Si hay respuesta del servidor
      String payload = http.getString();
      // Extraer solo el nombre de la ciudad del JSON
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, payload);
      const char* city = doc["address"]["city"];
      if (city == nullptr) {
        city = doc["address"]["town"];
      }
      if (city == nullptr) {
        city = doc["address"]["village"];
      }
      if (city != nullptr) {
        poblacion = String(city);
      } else {
        Serial.println("No se pudo encontrar el nombre de la ciudad.");
      }
    } else {
      Serial.println("Error en la solicitud HTTP: " + String(httpCode));
    }
    http.end();  // Finaliza la conexión
  } else {
    Serial.println("WiFi no está conectado.");
  }
}

void obtenerDatosClimaActual() {
  String url = "http://api.openweathermap.org/data/2.5/weather?q=" + poblacion + "&appid=" + String(apiKey) + "&units=metric";
  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();
  String payload = "{}";
  if (httpCode > 0) {
    payload = http.getString();
  }
  http.end();
  extractWeatherInfoActual(payload);
}

void extractWeatherInfoActual(String data) {
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, data);
  long sunsetTimestamp = doc["sys"]["sunset"];
  time_t rawtime = sunsetTimestamp;
  struct tm* timeinfo = localtime(&rawtime);
  char buffer[6];
  strftime(buffer, 6, "%H:%M", timeinfo);
  sunsetTime = String(buffer);
  Serial.println("Latitud: " + latitude);
  Serial.println("Longitud: " + longitude);
  Serial.println("Localidad: " + poblacion);
  Serial.println("Puesta del sol: " + sunsetTime);

  sunsetRequested = true;
  rainForecastRequested = false;
}

void obtenerDatosClimaPrevision() {
  String url = "http://api.openweathermap.org/data/2.5/forecast?q=" + poblacion + "&appid=" + String(apiKey) + "&units=metric";
  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();
  String payload = "{}";
  if (httpCode > 0) {
    payload = http.getString();
  }
  http.end();
  extractWeatherInfoPrevision(payload);
}

void extractWeatherInfoPrevision(String data) {
  DynamicJsonDocument doc(4096);
  deserializeJson(doc, data);
  rainInNext12Hours = false;
  for (int i = 0; i < 12; i++) {
    if (doc["list"][i]["weather"][0]["main"] == "Rain") {
      rainInNext12Hours = true;
      break;
    }
  }
  Serial.println("¿Lloverá en las próximas 12 horas?: " + String(rainInNext12Hours ? "true" : "false"));
  rainForecastRequested = true;
  sunsetRequested = false;
}

void controlarHumedad() {
  // Valores de humedad ideales según la planta
  switch (tipoPlanta) {
    case 0: humedadIdeal = 1000; break;  // Tomate
    case 1: humedadIdeal = 600; break;   // Maíz
    case 2: humedadIdeal = 150; break;   // Trigo
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Iniciando...");
  WiFi.mode(WIFI_STA);

  connectWiFi();

  // Obtener el canal actual de la red Wi-Fi
  int wifiChannel = WiFi.channel();

  // Inicializar ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error al inicializar ESP-NOW");
    return;
  }
  // Registrar función de devolución de llamada para recibir mensajes
  esp_now_register_recv_cb(onDataReceive);

  // Configurar el receptor en el mismo canal que la red Wi-Fi
  esp_now_peer_info_t peerInfo = {};
  peerInfo.channel = wifiChannel;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Error al configurar el canal de ESP-NOW");
    return;
  }

  Serial.println("Estación receptora lista para recibir mensajes.");
  timeClient.begin();
  mySerial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  // Enviar comando AT para encender el GPS
  sendATCommand("AT+CGNSPWR=1", 1000, true);
  // Configurar el portal cautivo
  configurarPortalCautivo();
  // Iniciar el servidor
  server.begin();
  Serial.println("Servidor iniciado. Conéctese a la red WiFi para acceder al portal cautivo.");
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_ROJO, OUTPUT);
  pinMode(LED_AMARILLO, OUTPUT);
  pinMode(BOMBA, OUTPUT);
}

void loop() {
  // Manejo de las peticiones del portal cautivo
  server.handleClient();
  controlarHumedad();
  timeClient.update();
  int currentHour = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();
  
  // Verifica si es las 4 de la tarde y aún no se ha solicitado la puesta de sol
  if (currentHour == 16 && currentMinute == 0 && !sunsetRequested) {
    obtenerLocalizacion();
    obtenerDatosClimaActual();
    delay(60000);  // Espera un minuto para evitar múltiples consultas
    sunsetHour = sunsetTime.substring(0, 2).toInt();
    sunsetMinute = sunsetTime.substring(3, 5).toInt();
  }

  // Verifica si es la hora de la puesta de sol y aún no se ha solicitado la previsión de lluvia
  if (sunsetRequested && !rainForecastRequested && currentHour == sunsetHour && currentMinute == sunsetMinute) {
    obtenerDatosClimaPrevision();
    if (humedad < humedadIdeal && !rainInNext12Hours) {
      Serial.println("HUMEDAD INSUFICIENTE SIN LLUVIA PRÓXIMA: BOMBA ACTIVA");
      digitalWrite(LED_ROJO, HIGH);
      digitalWrite(LED_VERDE, LOW);
      while (humedad < humedadIdeal) {
        digitalWrite(BOMBA, HIGH);  // Encender la bomba hasta humedad correcta.
      }
      digitalWrite(LED_ROJO, LOW);
      digitalWrite(LED_VERDE, HIGH);
      digitalWrite(BOMBA, LOW);  // Apagar bomba tras riego
    } else if (humedad < humedadIdeal && rainInNext12Hours) {
      Serial.println("HUMEDAD INSUFICIENTE PERO LLUVIA PROXIMA: LED AMARILLO Y BOMBA INACTIVA");
      digitalWrite(LED_AMARILLO, HIGH);
      digitalWrite(LED_ROJO, LOW);
      digitalWrite(LED_VERDE, LOW);
      digitalWrite(BOMBA, LOW);  // No encender bomba
      checkHour = (sunsetHour + 12) % 24;
      checkMinute = sunsetMinute;
    } else {
      Serial.println("HUMEDAD CORRECTA: BOMBA INACTIVA");
      digitalWrite(LED_ROJO, LOW);
      digitalWrite(LED_VERDE, HIGH);
      digitalWrite(BOMBA, LOW);  // No encender bomba
    }
    delay(60000);  // Espera un minuto para evitar múltiples consultas
  }
  if (currentHour == checkHour && currentMinute == checkMinute) {
    // Realiza la comprobación adicional aquí
    Serial.println("Se acaba la previsión de lluvia");  //Se comprueba si la humedad es correcta pero no se riega por si ha llovido y ya se ha secado (se deja a decisión de agricultor)
    digitalWrite(LED_AMARILLO, LOW);
    if (humedad < humedadIdeal) {
      Serial.println("HUMEDAD INSUFICIENTE");
      digitalWrite(LED_ROJO, HIGH);
      digitalWrite(LED_VERDE, LOW);
    } else {
      Serial.println("HUMEDAD CORRECTA");
      digitalWrite(LED_ROJO, LOW);
      digitalWrite(LED_VERDE, HIGH);
    }
    delay(60000);  // Espera un minuto para evitar múltiples consultas
  }

  delay(1000);  // Espera un segundo antes de verificar nuevamente
}
