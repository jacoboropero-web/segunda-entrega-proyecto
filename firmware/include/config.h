// Estación de Monitoreo Ambiental con Ventilación Automática - Firmware v0.2
// Asignación de pines (ESP32-WROOM-32E) según esquemático v2.0 y parámetros de operación.
#pragma once
#include <stdint.h>

// ---------------- Pines ----------------
#define PIN_SDA         21   // Bus I2C sensor AHT20 (J4)
#define PIN_SCL         22
#define PIN_IR_OUT      26   // Salida del módulo FC-51 (activa en bajo), vía R22
#define PIN_IR_EN       25   // Habilita alimentación del FC-51 (Q4, activo en bajo)
#define PIN_BTN_MANT    23   // Botón SW3 modo mantenimiento (activo en bajo)
#define PIN_REED        27   // Sensor magnético: LOW = puerta cerrada, HIGH = abierta
#define PIN_LIM_OPEN    32   // Final de carrera "rejilla abierta" (activo en bajo, opcional)
#define PIN_LIM_CLOSE   33   // Final de carrera "rejilla cerrada" (activo en bajo, opcional)
#define PIN_BUZZER      13   // Buzzer activo vía Q5
#define PIN_LED_R        4
#define PIN_LED_G       16
#define PIN_LED_A       17
#define PIN_MOT_IA      18   // L9110S IA (PWM)
#define PIN_MOT_IB      19   // L9110S IB (PWM)
#define PIN_VBAT        34   // Divisor 100k/100k desde BAT+ (ADC1_CH6)
#define PIN_VBUS        39   // Divisor 100k/100k desde VBUS (ADC1_CH3)

// ---------------- Temporización (REQ-FUN-001..005) ----------------
#define T_MUESTREO_MS        5000UL   // REQ-FUN-001: lectura cada 5 s
#define T_CIERRE_SEGURO_MS  30000UL   // REQ-FUN-003: 30 s en rango seguro antes de cerrar
#define T_ALARMA_MS          5000UL   // REQ-FUN-004: buzzer 5 s
#define T_OVERRIDE_MS       60000UL   // REQ-FUN-005: control manual 60 s
#define T_MOTOR_MAX_MS       1500UL   // tiempo máximo de recorrido (REQ-FUN-002: < 2 s)
#define T_MANT_MAX_MS      900000UL   // el modo mantenimiento se desactiva solo a los 15 min
#define T_IR_POLL_MS          100UL   // muestreo del sensor IR con alimentación conmutada
#define T_IR_ESTAB_US        3000UL   // estabilización del FC-51 tras energizarlo
#define T_PUBLICAR_MS       30000UL   // envío de telemetría por MQTT
#define FALLAS_SENSOR_MAX        3    // lecturas fallidas consecutivas para declarar falla

// ---------------- Umbrales por defecto ----------------
#define T_ABRIR_C          35.0f   // REQ-FUN-002
#define T_CERRAR_C         33.0f   // histéresis de 2 °C
#define RH_ABRIR           80.0f
#define RH_CERRAR          75.0f
#define T_VERDE_MAX        30.0f   // REQ-FUN-006: verde por debajo de 30 °C y 65 %RH
#define RH_VERDE_MAX       65.0f

// ---------------- Batería ----------------
#define VBAT_BAJA_MV        3550   // alerta: por debajo de este valor el LDO puede salir de regulación en ráfagas Wi-Fi
#define VBAT_CRITICA_MV     3400   // apagado preventivo (deep sleep)
#define DIVISOR_VBAT        2.0f

// ---------------- Motor ----------------
#define MOTOR_PWM_FREQ     20000
#define MOTOR_PWM_BITS         8
#define MOTOR_DUTY           204   // 80 %
