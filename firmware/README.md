# Firmware preliminar - Estación de Monitoreo Ambiental (v0.2)

Firmware para la tarjeta v2.0 (ESP32-WROOM-32E). Framework Arduino, compilable con PlatformIO o Arduino CLI.

## Estructura

| Ruta | Contenido |
|---|---|
| `include/config.h` | Asignación de pines, tiempos y umbrales |
| `include/secrets.example.h` | Plantilla de credenciales Wi-Fi/MQTT (copiar como `secrets.h`) |
| `lib/control/` | Lógica de control independiente del hardware (máquina de estados de la rejilla, alarma, modo mantenimiento, LED de estado) |
| `src/main.cpp` | Manejo de periféricos (AHT20, FC-51, reed, botones, L9110S, LEDs, buzzer, ADC) y tarea de red MQTT en el núcleo 0 |
| `test/test_control/` | 15 pruebas unitarias (Unity) de la lógica de control |

## Compilar y cargar

```
cp include/secrets.example.h include/secrets.h   # completar credenciales
pio run -e esp32 -t upload
pio device monitor
pio test -e native                                # pruebas en el PC
```

Compilado y verificado con Arduino-ESP32 2.0.17: 771 741 B de flash (58 %) y 45 752 B de RAM (13 %), sin advertencias. El código también es compatible con el núcleo 3.x (capa `pwmInit`/`pwmWrite`).

## Pines

| Señal | GPIO | Señal | GPIO |
|---|---|---|---|
| SDA / SCL (AHT20) | 21 / 22 | LED verde / ámbar / rojo | 16 / 17 / 4 |
| IR_OUT (FC-51) | 26 | Buzzer | 13 |
| IR_EN (alimentación FC-51) | 25 | MOT_IA / MOT_IB (L9110S) | 18 / 19 |
| Botón mantenimiento | 23 | VBAT (ADC1_CH6) | 34 |
| Reed | 27 | VBUS (ADC1_CH3) | 39 |
| Final de carrera abierta / cerrada | 32 / 33 | UART0 (CH340C) | 1 / 3 |

## Interfaz MQTT

- `estacion/<id>/estado` (retenido, cada 30 s): `{"t","rh","rejilla","manual","mant","alarma","falla_sensor","vbat_mv","usb"}`
- `estacion/<id>/cmd`: `abrir`, `cerrar`, `auto`, `mant_on`, `mant_off`

`<id>` corresponde a la dirección MAC del módulo.

## Notas

- Si no se instala el sensor reed, puentear J6. Con la entrada abierta el sistema interpreta el gabinete como abierto y activa la alarma.
- Sin finales de carrera, el recorrido de la rejilla se limita por tiempo (`T_MOTOR_MAX_MS` = 1.5 s); este valor se ajusta con el mecanismo real en la Fase 3.
