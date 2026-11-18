#pragma once

#include <Arduino.h>

namespace Config {
constexpr uint8_t NODE_ID = 1;
// Entre envios el ATmega duerme en power-down y lo despierta el watchdog
// cada ~8 s (oscilador interno de 128 kHz: tolerancia de algunos %).
// Intervalo aproximado = SLEEP_CYCLES x 8 s + tiempo despierto (~0,3 s).
// La Heltec marca "SIN DATOS RECIENTES" a los 20 s (STALE_AFTER_MS en su
// Config.h): con 1 ciclo se toleran una perdida de paquete. Si se sube este
// valor, subir tambien STALE_AFTER_MS a unas 3 veces el intervalo.
constexpr uint8_t SLEEP_CYCLES = 1;

// Parametros conservados de "Radio Node.txt".
constexpr long LORA_FREQUENCY_HZ = 915000000L;
constexpr uint8_t LORA_CS = 10;
constexpr uint8_t LORA_RESET = 9;
constexpr uint8_t LORA_DIO0 = 2;
constexpr uint8_t LORA_SYNC_WORD = 0x12;
constexpr uint8_t LORA_SF = 7;
constexpr long LORA_BANDWIDTH_HZ = 125000L;
constexpr uint8_t LORA_CODING_RATE = 5;
constexpr uint16_t LORA_PREAMBLE = 8;
constexpr int8_t LORA_TX_DBM = 14;

// BME280 por I2C: SDA=A4, SCL=A5, alimentacion de 3.3 V.
constexpr uint8_t BME_ADDRESS_1 = 0x76;
constexpr uint8_t BME_ADDRESS_2 = 0x77;

// Cambiar a true SOLO despues de montar el divisor descrito en README.md.
// Sin divisor se transmite "bateria no disponible", nunca un valor inventado.
constexpr bool BATTERY_CONNECTED = true;
constexpr uint8_t BATTERY_PIN = A0;
constexpr float BATTERY_R_TOP_OHMS = 150000.0f;
constexpr float BATTERY_R_BOTTOM_OHMS = 150000.0f;
// Referencia DEFAULT = VCC. Reemplazar 3300 por los mV medidos entre 3V3 y GND.
constexpr float ADC_REFERENCE_MV = 3226.0f;
constexpr float BATTERY_CALIBRATION = 1.0f;
constexpr uint8_t BATTERY_SAMPLES = 32;
}
