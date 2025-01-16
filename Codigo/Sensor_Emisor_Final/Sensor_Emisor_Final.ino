#include <esp_now.h>
#include <WiFi.h>

// Dirección MAC del receptor
uint8_t slaveAddress[] = {0xB4, 0xE6, 0x2D, 0x8D, 0x50, 0x7D};

// Pines del sensor de humedad
#define SENSOR_HUMEDAD 35  // GPIO35

// Estructura para mantener los datos de humedad
typedef struct Message {
  int valorHumedad;
} Message;

Message myMessage;

// Callback para rastrear los mensajes enviados
void OnSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\r\nEstado del envío del mensaje:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Enviado exitosamente" : "Error en el envío");
}

void setup() {
  // Iniciar el monitor serie
  Serial.begin(115200);

  // Configurar el dispositivo como estación Wi-Fi
  WiFi.mode(WIFI_STA);

  // Conectar a la red Wi-Fi para obtener el canal
  const char* ssid = "dani";
  const char* password = "certificado";
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Conectando a WiFi...");
  }
  Serial.println("Conectado a la red WiFi");

  // Obtener el canal actual de la red Wi-Fi
  int wifiChannel = WiFi.channel();

  // Inicializar ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error al inicializar ESP-NOW");
    return;
  }

  // Registrar la función de callback para responder al evento de envío
  esp_now_register_send_cb(OnSent);

  // Inicializar la estructura de información del receptor
  esp_now_peer_info_t slaveInfo = {};
  memcpy(slaveInfo.peer_addr, slaveAddress, 6);
  slaveInfo.channel = wifiChannel; // Usar el mismo canal que la red Wi-Fi
  slaveInfo.encrypt = false;

  // Añadir el receptor
  if (esp_now_add_peer(&slaveInfo) != ESP_OK) {
    Serial.println("Error al registrar el receptor");
    return;
  }

  // Configuración del pin del sensor de humedad
  pinMode(SENSOR_HUMEDAD, INPUT);

  Serial.println("Iniciando prueba del sensor de humedad...");
}

void loop() {
  // Leer el valor analógico del sensor de humedad
  myMessage.valorHumedad = analogRead(SENSOR_HUMEDAD);

  // Mostrar el valor en el monitor serie
  Serial.print("Valor del sensor de humedad: ");
  Serial.println(myMessage.valorHumedad);

  // Enviar el mensaje vía ESP-NOW
  esp_err_t result = esp_now_send(slaveAddress, (uint8_t *)&myMessage, sizeof(myMessage));

  if (result == ESP_OK) {
    Serial.println("El mensaje se envió correctamente.");
  } else {
    Serial.println("Hubo un error al enviar el mensaje.");
  }

  // Pausa para evitar lecturas constantes
  delay(2000); // 2 segundos entre lecturas
}
