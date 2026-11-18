# BME280 y batería por LoRa

Primera etapa: **LoRa Radio Node v1.0 (ATmega328P) → Heltec Wireless Tracker (ESP32-S3 + SX1262)**. La Tracker muestra temperatura, humedad, presión y la batería **del nodo remoto**.

Anteproyecto de hardware: [PCB integradora, gabinete y cabezal de medición](hardware/ANTEPROYECTO.md). Incluye archivos KiCad y FreeCAD provisionales; faltan medidas físicas, selección de gabinete y ruteo.

Los programas parten de `Radio Node.txt` y `Heltec Wireless Tracker.txt`. Los dos originales se conservan. Se mantienen sus pines, la inicialización TFT y los parámetros del enlace: **915 MHz, SF7, BW 125 kHz, CR 4/5, sync word 0x12**, potencia de 14 dBm y preámbulo de 8 símbolos. Se habilitó CRC en ambos extremos.

## Archivos para cargar

- `firmware/LoRaNodeV1/LoRaNodeV1.ino`: lee el BME280, transmite y duerme en power-down hasta el próximo envío (cada ~8 segundos por defecto).
- `firmware/HeltecWirelessTracker/HeltecWirelessTracker.ino` recibe, muestra los datos en la TFT y los publica en Blynk si el Wi-Fi está configurado.
- Abrí cada `.ino` desde su carpeta y conservá los demás archivos junto al sketch. Las dos copias de `Telemetria.h` deben mantenerse idénticas.

## Conexión del BME280 al nodo

| BME280 | LoRa Radio Node v1.0 |
| --- | --- |
| VCC/VIN | 3V3 |
| GND | GND |
| SDA | A4 / SDA |
| SCL | A5 / SCL |

Se prueban automáticamente las direcciones `0x76` y `0x77`. En módulos con CS/CSB y SDO expuestos, CS debe quedar alto para I2C y SDO determina la dirección; seguir el pinout de tu módulo. Un **BMP280** no mide humedad y este código requiere **BME280**.

## Conexión y configuración de la batería

El `Config.h` actual tiene `BATTERY_CONNECTED = true`, resistencias de **150 kΩ + 150 kΩ** y `ADC_REFERENCE_MV = 3226`. Esto describe la configuración del firmware; hay que verificar que coincida con el divisor y la alimentación físicos. Si el divisor no está montado, poner `BATTERY_CONNECTED = false`; la pantalla mostrará **`Bat: -- (sin medicion)`**.

Para una **Li-ion de una celda, nominal 3,7 V, máximo 4,2 V**, conectar:

```text
BAT+ antes del regulador --- R1 150 kohm ---+--- R2 150 kohm --- GND
                                          |
                                          +--- A0 del nodo
                                          |
                                          +--- C1 100 nF ----- GND

BAT- ------------------------------------------ GND del nodo
```

1. Montar el divisor y compartir GND. C1 ayuda a estabilizar la entrada por el valor alto de las resistencias. Esperar al menos 50 ms desde la alimentación antes de medir; el arranque del programa ya supera ese tiempo.
2. Verificar con multímetro que A0 esté aproximadamente a **la mitad de la tensión de batería**: 2,10 V si la batería está a 4,20 V. **No conectar BAT+ directamente a A0**.
3. En `firmware/LoRaNodeV1/Config.h`, dejar `BATTERY_CONNECTED = true` solamente si el divisor está montado y comprobado. Cargar el nodo después de cualquier cambio.
4. Medir 3V3 respecto de GND y ajustar `ADC_REFERENCE_MV` a ese valor en milivoltios. La referencia del ADC es VCC, no la tensión nominal de la batería. Si queda una diferencia con el multímetro, ajustar `BATTERY_CALIBRATION = tension_real / tension_mostrada` partiendo de 1,0.

El código promedia 32 muestras antes de transmitir. La fórmula es `VBAT = ADC × VREF / 1023 × (R1 + R2) / R2`. Recalibrar si cambia la alimentación de 3V3. Cuando la batería baja tanto que el regulador pierde regulación, VCC deja de ser constante y la lectura requiere corregir la referencia.

La pantalla muestra voltios y **porcentaje aproximado** (`~`) mediante una curva orientativa para Li-ion de una celda. No equivale a medir capacidad restante y no sustituye la protección de la batería. El divisor de 150 kΩ + 150 kΩ toma unos 14 µA a 4,2 V. El divisor queda conectado siempre, también mientras el ATmega duerme, y entra en el cálculo de autonomía.

## Modo sueño del nodo

Solo el nodo emisor duerme; la Heltec sigue escuchando todo el tiempo. Cada ciclo el nodo se despierta, mide el BME280 (modo forzado: el sensor vuelve a dormir solo), mide la batería, transmite, pone la radio en sueño y apaga el ATmega en **power-down**:

- Lo despierta el **watchdog cada ~8 s**, usado solo como interrupción, nunca como reset. No usar `wdt_enable()` en este sketch: el bootloader de la Pro Mini no limpia el watchdog tras un reset y la placa quedaría reiniciándose.
- Durante el sueño se apagan el ADC y el detector de bajo voltaje (BOD). `millis()` no avanza mientras duerme; el intervalo lo marca el watchdog.
- No hace falta instalar bibliotecas nuevas: se usan `avr/sleep.h` y `avr/wdt.h`, que vienen con el núcleo AVR.

El intervalo se ajusta con `SLEEP_CYCLES` en `firmware/LoRaNodeV1/Config.h`: intervalo ≈ `SLEEP_CYCLES × 8 s` más unos 0,3 s despierto. El oscilador del watchdog no es preciso, así que el período real puede variar algunos por ciento con la tensión y la temperatura. Con el valor por defecto (`1`), la pérdida de un paquete no alcanza a activar el aviso de 20 s de la Heltec. **Si se sube `SLEEP_CYCLES`, subir también `STALE_AFTER_MS` en `firmware/HeltecWirelessTracker/Config.h`** a unas tres veces el intervalo, o la Tracker mostrará `SIN DATOS RECIENTES` entre paquetes.

Para el cálculo de autonomía hay que medir con un multímetro en serie con la batería, o con un medidor de corriente, dos valores: la corriente dormido y la carga de cada ciclo despierto (corriente × duración, con el pico de TX incluido). Promedio ≈ `I_dormido + carga_por_ciclo / intervalo`, y autonomía ≈ `capacidad / promedio`. La corriente dormido incluye el regulador de la placa, LEDs de alimentación si los tiene, el BME280 en reposo y los ~14 µA del divisor. Los mensajes por el Monitor Serie también alargan el tiempo despierto (9600 baud).

## Carga en Arduino IDE

Bibliotecas del Gestor de bibliotecas:

| Placa | Bibliotecas |
| --- | --- |
| Nodo | **LoRa** de Sandeep Mistry; **Adafruit BME280 Library** y sus dependencias **Adafruit Unified Sensor** y **Adafruit BusIO** |
| Tracker | **RadioLib** de Jan Gromes, **Adafruit GFX Library**, **Adafruit ST7735 and ST7789 Library** y **Blynk** (verificado con 1.4.0), con sus dependencias |

1. Abrir `LoRaNodeV1.ino`. Seleccionar **Arduino Pro or Pro Mini → ATmega328P (3.3V, 8 MHz)**. Usar un adaptador USB-serie con **lógica de 3,3 V** según el pinout de la placa. Monitor serie: **9600 baud**. No seleccionar la variante de 16 MHz.
2. Abrir `HeltecWirelessTracker.ino`. Seleccionar **Heltec Wireless Tracker**, usando el paquete ESP32 que ya tenés instalado. Monitor serie: **115200 baud**. Mantener las opciones de carga/USB que ya funcionaban con tu ejemplo.
3. Conectar las antenas de 915 MHz antes de encender y cargar los dos programas. El nuevo formato de datos requiere ambos firmwares; el receptor nuevo descarta el antiguo mensaje `Hola Heltec!`.

## Wi-Fi y Blynk en la Heltec

1. Abrí `firmware/HeltecWirelessTracker/BlynkSecrets.h`. Si falta el archivo, copiá `BlynkSecrets.example.h` con ese nombre y completá `BLYNK_AUTH_TOKEN` con el token del dispositivo existente de **SistemaMedicion**. En la copia local preparada con los códigos de Adriel, ese token ya está cargado. Git ignora `BlynkSecrets.h`.
2. Escribí el nombre exacto de tu red Wi-Fi de **2,4 GHz** en `WIFI_SSID` y su contraseña en `WIFI_PASSWORD`, entre comillas. La red necesita acceso a Internet.
3. Instalá **Blynk** desde el Gestor de bibliotecas de Arduino IDE y cargá `HeltecWirelessTracker.ino` en la **Heltec Wireless Tracker**. Si el Radio Node ya tiene el firmware de `firmware/LoRaNodeV1`, alcanza con cargar la Heltec.
4. Abrí el Monitor Serie a **115200 baud**. `Blynk: Wi-Fi conectado.` indica que la Heltec se unió a la red. `Ready (ping: ...)` confirma la conexión a Blynk. En el panel web o la app, abrí el mismo dispositivo y comprobá que el número de paquete avance.

| Pin virtual | Dato |
| --- | --- |
| V0 | Temperatura (°C) |
| V1 | Humedad (%) |
| V2 | Presión (hPa) |
| V3 | Número de paquete |
| V4 | RSSI del enlace LoRa (dBm) |
| V9 | Batería del nodo (% aproximado a partir de la tensión medida) |

La TFT sigue mostrando los datos LoRa durante un corte de Wi-Fi. El receptor vuelve a publicar cuando se recupera la conexión. Si falta una medición válida del BME280 o de batería, Blynk conserva el último valor publicado en los pines correspondientes.

## Qué debe verse

```text
BME280 + BATERIA NODO
Nodo 1  RSSI -72 dBm
Temp: 23.45 C
Hum:  56.78 %
Pres: 1013.2 hPa
Bat: 3.85 V ~50%
Edad 0s  #12
```

Los números anteriores son solo un ejemplo. Sin divisor, la fila de batería indica que no hay medición. Al arrancar la Tracker espera el primer paquete. Si pasan **20 segundos sin un paquete válido del nodo 1**, muestra **`SIN DATOS RECIENTES`** y conserva los últimos valores en amarillo, con su antigüedad. Si el nodo informa un fallo del BME280, muestra el error sin inventar lecturas; el nodo reintenta detectar el sensor en el siguiente ciclo.

Para probar: comprobar cambios del sensor, desconectar el BME280 y comprobar el mensaje de error, apagar el nodo durante más de 20 segundos y comprobar el estado de datos antiguos, luego reconectarlo y comprobar recuperación. Después de montar el divisor, comparar batería con multímetro. No desconectar cables de alimentación provocando cortocircuitos.

## Formato del paquete

19 bytes, enteros little-endian; no se transmite directamente una estructura C ni valores `float`.

| Bytes | Contenido |
| --- | --- |
| 0–2 | `S`, `C`, versión `1` |
| 3 | ID de nodo, inicialmente `1` |
| 4 | bit 0: BME280 válido; bit 1: batería válida |
| 5–8 | contador de envíos (`uint32_t`, reinicia al arrancar) |
| 9–10 | temperatura en centésimas de °C (`int16_t`) |
| 11–12 | humedad en centésimas de % (`uint16_t`) |
| 13–16 | presión en Pa (`uint32_t`) |
| 17–18 | batería en mV (`uint16_t`) |

Los campos cuya bandera no está activa no se muestran como mediciones. La Tracker valida longitud, identificador, versión, banderas, ID del nodo y rangos; la radio comprueba el CRC LoRa. Es un enlace local de un solo nodo, sin confirmación de entrega ni cifrado.

## Verificación y referencias

Las versiones locales usadas para compilar se registran en `VALIDACION.md`. `tests/telemetria_test.cpp` comprueba bytes conocidos, temperaturas negativas, extremos y rechazo de paquetes inválidos. La compilación no sustituye la prueba con las placas físicas.

- [LoRa Radio Node v1.0: esquema del fabricante IOT-MCU](https://github.com/IOT-MCU/LoRa-Radio-Node-v1.0). Confirma SPI D10/D9/D2, I2C A4/A5 y reloj de 8 MHz; este esquema no incluye un divisor de batería al ADC.
- [Heltec Wireless Tracker](https://heltec.org/project/wireless-tracker/): modelo ESP32-S3 + SX1262 y pantalla TFT.
- [API de Arduino LoRa](https://github.com/sandeepmistry/arduino-LoRa/blob/master/API.md) y [configuración de RadioLib](https://github.com/jgromes/RadioLib/wiki/Default-configuration).
