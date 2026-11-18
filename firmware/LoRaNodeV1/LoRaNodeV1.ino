#include <SPI.h>
#include <Wire.h>
#include <LoRa.h>
#include <Adafruit_BME280.h>
#include <math.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/wdt.h>

#include "Config.h"
#include "Telemetria.h"

#if !defined(__AVR_ATmega328P__)
#error "Seleccionar ATmega328P: Arduino Pro or Pro Mini, 3.3V, 8 MHz."
#endif
#if F_CPU != 8000000UL
#error "La LoRa Radio Node v1.0 usa reloj de 8 MHz. Revisar Procesador en Arduino IDE."
#endif

Adafruit_BME280 bme;
bool sensorReady = false;
uint32_t sequence = 0;
volatile bool txDone = false;
volatile bool watchdogFired = false;

void measureAndSend();

void onTxDone() {
  txDone = true;
}

// El watchdog se usa SOLO en modo interrupcion (WDIE, sin WDE): nunca
// reinicia el micro. Importante con el bootloader de la Pro Mini, que no
// limpia el watchdog tras un reset y quedaria en un ciclo de reinicios.
ISR(WDT_vect) {
  watchdogFired = true;
}

// Duerme ~8 s en power-down. Radio y BME280 ya quedaron dormidos.
void sleep8s() {
  Serial.flush();  // Terminar de enviar el texto antes de apagar el reloj.
  const uint8_t adcState = ADCSRA;
  ADCSRA &= ~_BV(ADEN);  // ADC apagado: si no, sigue consumiendo dormido.

  watchdogFired = false;
  cli();
  MCUSR &= ~_BV(WDRF);
  wdt_reset();
  WDTCSR = _BV(WDCE) | _BV(WDE);                // Secuencia temporizada.
  WDTCSR = _BV(WDIE) | _BV(WDP3) | _BV(WDP0);   // 8 s, solo interrupcion.
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  // Otra interrupcion (p. ej. DIO0) podria despertar antes: volver a dormir
  // hasta que dispare el watchdog.
  while (!watchdogFired) {
    sleep_enable();
    sleep_bod_disable();  // BOD apagado solo mientras duerme (ATmega328P).
    sei();
    sleep_cpu();
    sleep_disable();
    cli();
  }
  sei();
  wdt_disable();
  ADCSRA = adcState;
}

bool startSensor() {
  const bool found = bme.begin(Config::BME_ADDRESS_1, &Wire) ||
                     bme.begin(Config::BME_ADDRESS_2, &Wire);
  if (found) {
    bme.setSampling(Adafruit_BME280::MODE_FORCED,
                    Adafruit_BME280::SAMPLING_X1,
                    Adafruit_BME280::SAMPLING_X1,
                    Adafruit_BME280::SAMPLING_X1,
                    Adafruit_BME280::FILTER_OFF);
  }
  return found;
}

void readSensor(Telemetria::Sample& sample) {
  if (!sensorReady) sensorReady = startSensor();
  if (!sensorReady || !bme.takeForcedMeasurement()) {
    sensorReady = false;
    Serial.println(F("BME280 no disponible; se enviara el estado de error."));
    return;
  }

  const float temperature = bme.readTemperature();
  const float humidity = bme.readHumidity();
  const float pressure = bme.readPressure();
  if (!isfinite(temperature) || !isfinite(humidity) || !isfinite(pressure) ||
      temperature < -40.0f || temperature > 85.0f ||
      humidity < 0.0f || humidity > 100.0f ||
      pressure < 30000.0f || pressure > 110000.0f) {
    sensorReady = false;
    Serial.println(F("Lectura BME280 invalida."));
    return;
  }

  sample.temperatureCentiC = static_cast<int16_t>(lroundf(temperature * 100));
  sample.humidityCentiPct = static_cast<uint16_t>(lroundf(humidity * 100));
  sample.pressurePa = static_cast<uint32_t>(lroundf(pressure));
  sample.flags |= Telemetria::SENSOR_VALID;
}

void readBattery(Telemetria::Sample& sample) {
  if (!Config::BATTERY_CONNECTED) return;

  // Descartar primera conversion; el capacitor del divisor ayuda al ADC.
  analogRead(Config::BATTERY_PIN);
  delay(5);
  uint32_t total = 0;
  for (uint8_t i = 0; i < Config::BATTERY_SAMPLES; ++i) {
    total += analogRead(Config::BATTERY_PIN);
    delay(2);
  }
  const float adc = static_cast<float>(total) / Config::BATTERY_SAMPLES;
  const float divider = (Config::BATTERY_R_TOP_OHMS + Config::BATTERY_R_BOTTOM_OHMS) /
                        Config::BATTERY_R_BOTTOM_OHMS;
  const float mv = adc * Config::ADC_REFERENCE_MV / 1023.0f * divider *
                   Config::BATTERY_CALIBRATION;
  if (adc >= 1023.0f || mv < 2000.0f || mv > 4500.0f) {
    Serial.println(F("Bateria: lectura fuera de rango; revisar divisor y referencia."));
    return;
  }
  sample.batteryMv = static_cast<uint16_t>(lroundf(mv));
  sample.flags |= Telemetria::BATTERY_VALID;
}

void printSample(const Telemetria::Sample& sample) {
  Serial.print(F("Paquete #")); Serial.print(sample.sequence);
  if (sample.flags & Telemetria::SENSOR_VALID) {
    Serial.print(F(" | T=")); Serial.print(sample.temperatureCentiC / 100.0f, 2);
    Serial.print(F(" C | H=")); Serial.print(sample.humidityCentiPct / 100.0f, 2);
    Serial.print(F(" % | P=")); Serial.print(sample.pressurePa / 100.0f, 1);
    Serial.print(F(" hPa"));
  } else {
    Serial.print(F(" | BME280 ERROR"));
  }
  if (sample.flags & Telemetria::BATTERY_VALID) {
    Serial.print(F(" | Bat=")); Serial.print(sample.batteryMv / 1000.0f, 2);
    Serial.print(F(" V (~")); Serial.print(Telemetria::batteryPercent(sample.batteryMv));
    Serial.print(F(" %)"));
  } else {
    Serial.print(F(" | Bat=no disponible"));
  }
  Serial.println();
}

void setup() {
  Serial.begin(9600);
  Wire.begin();  // ATmega328P: A4=SDA, A5=SCL.
  Wire.setClock(100000);
  Wire.setWireTimeout(25000, true);
  analogReference(DEFAULT);
  pinMode(Config::BATTERY_PIN, INPUT);

  LoRa.setPins(Config::LORA_CS, Config::LORA_RESET, Config::LORA_DIO0);
  if (!LoRa.begin(Config::LORA_FREQUENCY_HZ)) {
    Serial.println(F("ERROR: no se pudo iniciar LoRa. Revisar placa y alimentacion."));
    while (true) delay(1000);
  }
  LoRa.setSyncWord(Config::LORA_SYNC_WORD);
  LoRa.setSpreadingFactor(Config::LORA_SF);
  LoRa.setSignalBandwidth(Config::LORA_BANDWIDTH_HZ);
  LoRa.setCodingRate4(Config::LORA_CODING_RATE);
  LoRa.setPreambleLength(Config::LORA_PREAMBLE);
  LoRa.setTxPower(Config::LORA_TX_DBM);
  LoRa.enableCrc();
  LoRa.disableInvertIQ();
  LoRa.onTxDone(onTxDone);
  LoRa.sleep();

  sensorReady = startSensor();
  Serial.println(F("Nodo listo: BME280 + bateria -> Heltec, 915 MHz."));
  if (!Config::BATTERY_CONNECTED) {
    Serial.println(F("Bateria deshabilitada: montar divisor y activar BATTERY_CONNECTED."));
  }
  Serial.print(F("Modo sueno: envio cada ~"));
  Serial.print(Config::SLEEP_CYCLES * 8);
  Serial.println(F(" s."));
}

void loop() {
  measureAndSend();  // El primer envio sale apenas arranca.
  // millis() no avanza en power-down: el intervalo lo marca el watchdog.
  for (uint8_t i = 0; i < Config::SLEEP_CYCLES; ++i) {
    sleep8s();
  }
}

void measureAndSend() {
  Telemetria::Sample sample = {};
  sample.nodeId = Config::NODE_ID;
  sample.sequence = ++sequence;
  readSensor(sample);
  readBattery(sample);  // Antes de transmitir para evitar el pico de consumo RF.
  printSample(sample);

  uint8_t packet[Telemetria::PACKET_SIZE];
  Telemetria::encode(sample, packet);
  // Cabecera explicita, igual que en el ejemplo original.
  if (!LoRa.beginPacket()) {
    Serial.println(F("ERROR: radio ocupada al iniciar el paquete."));
    LoRa.sleep();
    return;
  }
  LoRa.write(packet, sizeof(packet));
  txDone = false;
  LoRa.endPacket(true);
  const uint32_t txStarted = millis();
  while (!txDone && static_cast<uint32_t>(millis() - txStarted) < 2000) {
    delay(1);
  }
  if (!txDone) {
    Serial.println(F("ERROR: tiempo de transmision agotado."));
  } else {
    Serial.println(F("Transmitido."));
  }
  LoRa.sleep();
}
