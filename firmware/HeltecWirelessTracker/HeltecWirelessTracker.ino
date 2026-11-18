#include <SPI.h>
#include <RadioLib.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

#include "BlynkTelemetry.h"
#include "Config.h"
#include "Telemetria.h"

#if !defined(ARDUINO_ARCH_ESP32) || !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "Seleccionar Heltec Wireless Tracker (ESP32-S3) en Arduino IDE."
#endif

// SPI por software para TFT, como en el codigo original: no comparte el bus RF.
Adafruit_ST7735 tft(Config::TFT_CS, Config::TFT_DC, Config::TFT_MOSI,
                     Config::TFT_SCLK, Config::TFT_RST);
SX1262 radio = new Module(Config::LORA_NSS, Config::LORA_DIO1,
                           Config::LORA_RST, Config::LORA_BUSY);

volatile bool packetReceived = false;
Telemetria::Sample latest = {};
bool hasSample = false;
uint32_t lastReceivedMs = 0;
uint32_t lastScreenMs = 0;
float lastRssi = 0;

void IRAM_ATTR onPacketReceived() {
  packetReceived = true;  // No acceder a SPI ni dibujar desde la interrupcion.
}

void fatalRadioError(const char* message, int16_t state) {
  Serial.print(message); Serial.print(F(": ")); Serial.println(state);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_RED);
  tft.setCursor(4, 10); tft.println(message);
  tft.setCursor(4, 26); tft.print("Codigo: "); tft.println(state);
  while (true) delay(1000);
}

void drawScreen() {
  const bool stale = hasSample &&
      static_cast<uint32_t>(millis() - lastReceivedMs) >= Config::STALE_AFTER_MS;
  // 160x80: seis renglones, fuente de 6x8. Solo se limpia el area de datos.
  tft.fillRect(0, 12, 160, 68, ST77XX_BLACK);
  tft.setTextColor(stale ? ST77XX_RED : ST77XX_GREEN);
  tft.setCursor(4, 14);
  if (!hasSample) {
    tft.print("Esperando nodo...");
    return;
  }
  if (stale) {
    tft.print("SIN DATOS RECIENTES");
  } else {
    tft.print("Nodo "); tft.print(latest.nodeId);
    tft.print("  RSSI "); tft.print(lastRssi, 0); tft.print(" dBm");
  }

  tft.setTextColor(stale ? ST77XX_YELLOW : ST77XX_WHITE);
  if (latest.flags & Telemetria::SENSOR_VALID) {
    tft.setCursor(4, 26); tft.print("Temp: ");
    tft.print(latest.temperatureCentiC / 100.0f, 2); tft.print(" C");
    tft.setCursor(4, 37); tft.print("Hum:  ");
    tft.print(latest.humidityCentiPct / 100.0f, 2); tft.print(" %");
    tft.setCursor(4, 48); tft.print("Pres: ");
    tft.print(latest.pressurePa / 100.0f, 1); tft.print(" hPa");
  } else {
    tft.setTextColor(ST77XX_RED);
    tft.setCursor(4, 30); tft.print("BME280 sin lectura");
    tft.setCursor(4, 43); tft.print("Revisar sensor/I2C");
  }

  tft.setTextColor(stale ? ST77XX_YELLOW : ST77XX_CYAN);
  tft.setCursor(4, 59);
  if (latest.flags & Telemetria::BATTERY_VALID) {
    tft.print("Bat: "); tft.print(latest.batteryMv / 1000.0f, 2);
    tft.print(" V ~"); tft.print(Telemetria::batteryPercent(latest.batteryMv));
    tft.print("%");
  } else {
    tft.print("Bat: -- (sin medicion)");
  }
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(4, 71);
  tft.print("Edad ");
  tft.print(static_cast<uint32_t>(millis() - lastReceivedMs) / 1000);
  tft.print("s  #"); tft.print(latest.sequence);
}

void readPacket() {
  packetReceived = false;
  // Detener RX mientras se extrae el paquete para no mezclar dos recepciones.
  const int16_t standbyState = radio.standby();
  if (standbyState != RADIOLIB_ERR_NONE) {
    fatalRadioError("Error standby", standbyState);
  }
  const size_t length = radio.getPacketLength();
  uint8_t packet[Telemetria::PACKET_SIZE];
  int16_t state = RADIOLIB_ERR_NONE;
  bool accepted = false;
  Telemetria::Sample sample = {};
  if (length == sizeof(packet)) {
    state = radio.readData(packet, sizeof(packet));
    accepted = state == RADIOLIB_ERR_NONE &&
               Telemetria::decode(packet, sizeof(packet), sample) &&
               sample.nodeId == Config::EXPECTED_NODE_ID;
    if (accepted) lastRssi = radio.getRSSI();
  }
  // Volver a escuchar antes de actualizar la TFT.
  packetReceived = false;
  const int16_t rxState = radio.startReceive();
  if (rxState != RADIOLIB_ERR_NONE) fatalRadioError("Error recepcion", rxState);

  if (!accepted) {
    Serial.print(F("Paquete descartado. Longitud=")); Serial.print(length);
    Serial.print(F(" estado=")); Serial.println(state);
    return;
  }
  latest = sample;
  hasSample = true;
  lastReceivedMs = millis();
  BlynkTelemetry::enqueue(latest, lastRssi, lastReceivedMs);
  Serial.print(F("Nodo=")); Serial.print(latest.nodeId);
  Serial.print(F(" #")); Serial.print(latest.sequence);
  Serial.print(F(" flags=")); Serial.print(latest.flags);
  Serial.print(F(" T=")); Serial.print(latest.temperatureCentiC / 100.0f, 2);
  Serial.print(F(" H=")); Serial.print(latest.humidityCentiPct / 100.0f, 2);
  Serial.print(F(" P=")); Serial.print(latest.pressurePa / 100.0f, 1);
  Serial.print(F(" Bat_mV=")); Serial.print(latest.batteryMv);
  Serial.print(F(" RSSI=")); Serial.println(lastRssi, 0);
  drawScreen();
  lastScreenMs = millis();
}

void setup() {
  Serial.begin(115200);
  pinMode(Config::VEXT_PIN, OUTPUT); digitalWrite(Config::VEXT_PIN, HIGH);
  pinMode(Config::TFT_CTRL, OUTPUT); digitalWrite(Config::TFT_CTRL, LOW);
  pinMode(Config::TFT_LED, OUTPUT); digitalWrite(Config::TFT_LED, HIGH);
  tft.initR(INITR_MINI160x80);
  tft.setRotation(1);
  tft.invertDisplay(true);
  tft.setTextSize(1);
  tft.setTextWrap(false);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(4, 2); tft.print("BME280 + BATERIA NODO");

  SPI.begin(Config::LORA_SCK, Config::LORA_MISO, Config::LORA_MOSI, Config::LORA_NSS);
  // Se conservan los argumentos de radio.begin() del enlace ya probado.
  int16_t state = radio.begin(Config::LORA_FREQUENCY_MHZ, Config::LORA_BANDWIDTH_KHZ,
                              Config::LORA_SF, Config::LORA_CODING_RATE,
                              Config::LORA_SYNC_WORD, Config::LORA_TX_DBM);
  if (state != RADIOLIB_ERR_NONE) fatalRadioError("Error inicio LoRa", state);
  state = radio.setCRC(2);
  if (state != RADIOLIB_ERR_NONE) fatalRadioError("Error CRC", state);
  radio.setDio1Action(onPacketReceived);
  state = radio.startReceive();
  if (state != RADIOLIB_ERR_NONE) fatalRadioError("Error recepcion", state);
  drawScreen();
  Serial.println(F("Tracker escuchando: 915 MHz, SF7, BW125, CR4/5, sync 0x12."));
  BlynkTelemetry::begin();
}

void loop() {
  if (packetReceived) readPacket();
  if (static_cast<uint32_t>(millis() - lastScreenMs) >= Config::SCREEN_REFRESH_MS) {
    lastScreenMs = millis();
    drawScreen();
  }
  delay(1);
}
