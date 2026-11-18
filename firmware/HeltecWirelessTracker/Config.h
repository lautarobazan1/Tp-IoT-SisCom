#pragma once

#include <Arduino.h>

namespace Config {
constexpr uint8_t EXPECTED_NODE_ID = 1;
constexpr uint32_t STALE_AFTER_MS = 20000;
constexpr uint32_t SCREEN_REFRESH_MS = 1000;

// Pines y configuracion que ya funcionan en "Heltec Wireless Tracker.txt".
constexpr int TFT_CS = 38;
constexpr int TFT_RST = 39;
constexpr int TFT_DC = 40;
constexpr int TFT_MOSI = 42;
constexpr int TFT_SCLK = 41;
constexpr int TFT_LED = 21;
constexpr int TFT_CTRL = 46;
constexpr int VEXT_PIN = 3;

constexpr int LORA_NSS = 8;
constexpr int LORA_DIO1 = 14;
constexpr int LORA_RST = 12;
constexpr int LORA_BUSY = 13;
constexpr int LORA_SCK = 9;
constexpr int LORA_MISO = 11;
constexpr int LORA_MOSI = 10;
constexpr float LORA_FREQUENCY_MHZ = 915.0f;
constexpr float LORA_BANDWIDTH_KHZ = 125.0f;
constexpr uint8_t LORA_SF = 7;
constexpr uint8_t LORA_CODING_RATE = 5;
constexpr uint8_t LORA_SYNC_WORD = 0x12;
constexpr int8_t LORA_TX_DBM = 14;
}
