#include <Arduino.h>
#include <WiFi.h>

#if __has_include("BlynkSecrets.h")
#include "BlynkSecrets.h"
#else
#include "BlynkSecrets.example.h"
#endif

#define BLYNK_PRINT Serial
#define BLYNK_NO_BUILTIN
#define BLYNK_NO_DEFAULT_BANNER
#include <BlynkSimpleEsp32_SSL.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "BlynkTelemetry.h"
#include "Config.h"

namespace {
constexpr uint32_t PUBLISH_INTERVAL_MS = 1000;

struct Reading {
  Telemetria::Sample sample;
  float rssi;
  uint32_t receivedMs;
};

QueueHandle_t pendingReading = nullptr;

void publish(const Reading& reading) {
  const Telemetria::Sample& sample = reading.sample;
  if (sample.flags & Telemetria::SENSOR_VALID) {
    Blynk.virtualWrite(V0, sample.temperatureCentiC / 100.0f);
    Blynk.virtualWrite(V1, sample.humidityCentiPct / 100.0f);
    Blynk.virtualWrite(V2, sample.pressurePa / 100.0f);
  }
  Blynk.virtualWrite(V3, sample.sequence);
  Blynk.virtualWrite(V4, static_cast<int>(reading.rssi));
  if (sample.flags & Telemetria::BATTERY_VALID) {
    Blynk.virtualWrite(V9, Telemetria::batteryPercent(sample.batteryMv));
  }
}

void run(void*) {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Blynk.config(BLYNK_AUTH_TOKEN);
  Serial.println(F("Blynk: esperando Wi-Fi."));

  bool wasWifiConnected = false;
  uint32_t lastPublishMs = millis() - PUBLISH_INTERVAL_MS;
  for (;;) {
    const bool wifiConnected = WiFi.status() == WL_CONNECTED;
    if (wifiConnected != wasWifiConnected) {
      Serial.println(wifiConnected ? F("Blynk: Wi-Fi conectado.")
                                  : F("Blynk: Wi-Fi desconectado."));
      wasWifiConnected = wifiConnected;
    }

    if (wifiConnected && Blynk.run() && Blynk.connected()) {
      const uint32_t now = millis();
      Reading reading;
      if (now - lastPublishMs >= PUBLISH_INTERVAL_MS &&
          xQueueReceive(pendingReading, &reading, 0) == pdPASS &&
          millis() - reading.receivedMs < Config::STALE_AFTER_MS) {
        publish(reading);
        lastPublishMs = millis();
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
}

namespace BlynkTelemetry {
void begin() {
  if (WIFI_SSID[0] == '\0' || BLYNK_AUTH_TOKEN[0] == '\0') {
    Serial.println(F("Blynk: completar BlynkSecrets.h para publicar."));
    return;
  }

  pendingReading = xQueueCreate(1, sizeof(Reading));
  if (!pendingReading) {
    Serial.println(F("Blynk: no se pudo crear la cola."));
    return;
  }

  // La red puede tardar en responder sin frenar la recepcion LoRa ni la TFT.
  if (xTaskCreate(run, "Blynk", 8192, nullptr, 1, nullptr) != pdPASS) {
    vQueueDelete(pendingReading);
    pendingReading = nullptr;
    Serial.println(F("Blynk: no se pudo iniciar la tarea Wi-Fi."));
  }
}

void enqueue(const Telemetria::Sample& sample, float rssi, uint32_t receivedMs) {
  if (pendingReading) {
    // Durante un corte se conserva solo la muestra mas reciente.
    const Reading reading = {sample, rssi, receivedMs};
    xQueueOverwrite(pendingReading, &reading);
  }
}
}
