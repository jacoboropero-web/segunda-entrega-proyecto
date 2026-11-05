// Estación de Monitoreo Ambiental con Ventilación Automática - Firmware v0.2 (preliminar)
// ESP32-WROOM-32E, framework Arduino. La lógica de decisión está en lib/control.
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_AHTX0.h>
#include "config.h"
#include "control.h"
#include "secrets.h"   // WIFI_SSID, WIFI_PASS, MQTT_HOST, MQTT_PORT (ver secrets.example.h)

static Adafruit_AHTX0 aht;
static WiFiClient wifiCli;
static PubSubClient mqtt(wifiCli);
static Controlador ctrl({T_ABRIR_C, T_CERRAR_C, RH_ABRIR, RH_CERRAR, T_VERDE_MAX, RH_VERDE_MAX},
                        {T_CIERRE_SEGURO_MS, T_ALARMA_MS, T_OVERRIDE_MS, T_MOTOR_MAX_MS, T_MANT_MAX_MS});
static char idEst[13];
static char topEstado[40], topCmd[40];
static Comando cmdRemoto = Comando::NINGUNO;   // protegido por mux
static bool ahtOk = false;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// ---------------- Antirrebote simple para entradas activas en bajo ----------------
struct Boton {
  bool estable = false; bool previo = false; uint32_t t = 0;
  bool flanco(uint32_t ahora, bool activo, uint32_t rebote = 30) {
    if (activo != previo) { previo = activo; t = ahora; }
    if ((ahora - t) >= rebote && activo != estable) { estable = activo; return activo; }
    return false;
  }
};
static Boton btnMant, sensIr, reed;

// ---------------- Actuadores ----------------
// Compatibilidad con el núcleo Arduino-ESP32 2.x (canales LEDC) y 3.x (API por pin)
#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void pwmInit(uint8_t pin, uint8_t) { ledcAttach(pin, MOTOR_PWM_FREQ, MOTOR_PWM_BITS); }
static void pwmWrite(uint8_t pin, uint8_t, uint32_t d) { ledcWrite(pin, d); }
#else
static void pwmInit(uint8_t pin, uint8_t ch) { ledcSetup(ch, MOTOR_PWM_FREQ, MOTOR_PWM_BITS); ledcAttachPin(pin, ch); }
static void pwmWrite(uint8_t, uint8_t ch, uint32_t d) { ledcWrite(ch, d); }
#endif

static void motor(MotorCmd m) {
  // L9110S: IA=PWM, IB=0 -> abrir; IA=0, IB=PWM -> cerrar; ambos en 0 -> parado
  pwmWrite(PIN_MOT_IA, 0, m == MotorCmd::ABRIR ? MOTOR_DUTY : 0);
  pwmWrite(PIN_MOT_IB, 1, m == MotorCmd::CERRAR ? MOTOR_DUTY : 0);
}

static void leds(ColorLed c, bool parpadeo, uint32_t ahora) {
  const bool on = !parpadeo || ((ahora / 250) & 1);
  digitalWrite(PIN_LED_G, on && c == ColorLed::VERDE);
  digitalWrite(PIN_LED_A, on && c == ColorLed::AMBAR);
  digitalWrite(PIN_LED_R, on && c == ColorLed::ROJO);
}

// ---------------- Sensor IR con alimentación conmutada ----------------
static bool leerIr() {
  digitalWrite(PIN_IR_EN, LOW);            // energiza el FC-51 (Q4)
  delayMicroseconds(T_IR_ESTAB_US);
  const bool det = digitalRead(PIN_IR_OUT) == LOW;
  digitalWrite(PIN_IR_EN, HIGH);           // apaga el módulo para ahorrar energía
  return det;
}

static uint32_t vbatMv() {
  uint32_t acc = 0;
  for (int i = 0; i < 8; i++) acc += analogReadMilliVolts(PIN_VBAT);
  return (uint32_t)(acc / 8 * DIVISOR_VBAT);
}
static bool usbPresente() { return analogReadMilliVolts(PIN_VBUS) > 1500; }

// ---------------- Comunicación ----------------
static void alRecibir(char* topic, byte* p, unsigned int n) {
  String s; for (unsigned int i = 0; i < n; i++) s += (char)p[i];
  s.trim(); s.toLowerCase();
  Comando c = Comando::NINGUNO;
  if (s == "abrir") c = Comando::ABRIR;
  else if (s == "cerrar") c = Comando::CERRAR;
  else if (s == "auto") c = Comando::AUTO;
  else if (s == "mant_on") c = Comando::MANT_ON;
  else if (s == "mant_off") c = Comando::MANT_OFF;
  if (c != Comando::NINGUNO) { portENTER_CRITICAL(&mux); cmdRemoto = c; portEXIT_CRITICAL(&mux); }
}

static const char* txtRejilla(EstadoRejilla r) {
  switch (r) { case EstadoRejilla::ABIERTA: return "abierta"; case EstadoRejilla::CERRADA: return "cerrada";
               case EstadoRejilla::ABRIENDO: return "abriendo"; default: return "cerrando"; }
}

// Instantánea del estado que el lazo de control comparte con la tarea de red
struct Instantanea {
  float t, rh; EstadoRejilla rejilla; bool manual, mant, alarma, falla; uint32_t vbat; bool usb;
};
static Instantanea snap{};

static void publicar(const Instantanea& e) {
  char js[220];
  snprintf(js, sizeof js,
    "{\"t\":%.1f,\"rh\":%.1f,\"rejilla\":\"%s\",\"manual\":%d,\"mant\":%d,\"alarma\":%d,"
    "\"falla_sensor\":%d,\"vbat_mv\":%u,\"usb\":%d}",
    e.t, e.rh, txtRejilla(e.rejilla), e.manual, e.mant, e.alarma, e.falla, (unsigned)e.vbat, e.usb);
  mqtt.publish(topEstado, js, true);
}

// Tarea de comunicación en el núcleo 0: una conexión lenta al broker (connect() puede
// bloquear varios segundos) no retrasa el lazo de control, que corre en el núcleo 1.
static void tareaRed(void*) {
  uint32_t tIntento = 0, tPub = 0;
  for (;;) {
    const uint32_t ahora = millis();
    if (WiFi.status() == WL_CONNECTED) {
      if (!mqtt.connected() && ahora - tIntento >= 5000) {
        tIntento = ahora;
        if (mqtt.connect(idEst)) mqtt.subscribe(topCmd);
      }
      if (mqtt.connected()) {
        mqtt.loop();
        if (ahora - tPub >= T_PUBLICAR_MS) {
          tPub = ahora;
          Instantanea e; portENTER_CRITICAL(&mux); e = snap; portEXIT_CRITICAL(&mux);
          publicar(e);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_IR_EN, OUTPUT); digitalWrite(PIN_IR_EN, HIGH);
  pinMode(PIN_IR_OUT, INPUT);                 // sin pull-up interno: evita alimentar el módulo apagado
  pinMode(PIN_BTN_MANT, INPUT);               // pull-up externo R26
  pinMode(PIN_REED, INPUT);                   // pull-up externo R23
  pinMode(PIN_LIM_OPEN, INPUT); pinMode(PIN_LIM_CLOSE, INPUT);
  pinMode(PIN_BUZZER, OUTPUT); digitalWrite(PIN_BUZZER, LOW);
  pinMode(PIN_LED_G, OUTPUT); pinMode(PIN_LED_A, OUTPUT); pinMode(PIN_LED_R, OUTPUT);
  pwmInit(PIN_MOT_IA, 0);
  pwmInit(PIN_MOT_IB, 1);
  motor(MotorCmd::PARAR);
  analogSetPinAttenuation(PIN_VBAT, ADC_11db);
  analogSetPinAttenuation(PIN_VBUS, ADC_11db);

  // Protección de la celda: no arrancar Wi-Fi con batería crítica y sin USB
  if (!usbPresente() && vbatMv() < VBAT_CRITICA_MV) {
    Serial.println("Bateria critica: deep sleep");
    esp_sleep_enable_timer_wakeup(10ULL * 60 * 1000000);
    esp_deep_sleep_start();
  }

  Wire.begin(PIN_SDA, PIN_SCL);
  ahtOk = aht.begin(&Wire);
  if (!ahtOk) Serial.println("AHT20 no detectado");

  uint64_t mac = ESP.getEfuseMac();
  snprintf(idEst, sizeof idEst, "%04X%08X", (uint16_t)(mac >> 32), (uint32_t)mac);
  snprintf(topEstado, sizeof topEstado, "estacion/%s/estado", idEst);
  snprintf(topCmd, sizeof topCmd, "estacion/%s/cmd", idEst);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);                        // modem-sleep: reduce el consumo promedio (REQ-PER-001/003)
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(alRecibir);
  xTaskCreatePinnedToCore(tareaRed, "red", 6144, nullptr, 1, nullptr, 0);
}

void loop() {
  const uint32_t ahora = millis();
  static uint32_t tMuestra = 0, tIr = 0, tPub = 0;
  static bool primera = true;
  Entradas in{};

  // REQ-FUN-001: muestreo periódico; el período se ancla al instante programado para no acumular deriva
  if (primera || ahora - tMuestra >= T_MUESTREO_MS) {
    tMuestra = primera ? ahora : tMuestra + T_MUESTREO_MS;
    primera = false;
    sensors_event_t h, t;
    in.muestra_nueva = true;
    in.sensor_ok = ahtOk && aht.getEvent(&h, &t) && !isnan(t.temperature) && !isnan(h.relative_humidity);
    if (in.sensor_ok) { in.temp_c = t.temperature; in.rh = h.relative_humidity; }
    else if (!ahtOk) ahtOk = aht.begin(&Wire);   // reintento si el sensor se reconecta
  }
  if (ahora - tIr >= T_IR_POLL_MS) { tIr = ahora; in.pulso_ir = sensIr.flanco(ahora, leerIr(), 0); }
  in.pulso_mant = btnMant.flanco(ahora, digitalRead(PIN_BTN_MANT) == LOW);
  reed.flanco(ahora, digitalRead(PIN_REED) == HIGH, 50);
  in.puerta_abierta = reed.estable;
  in.lim_abierta = digitalRead(PIN_LIM_OPEN) == LOW;
  in.lim_cerrada = digitalRead(PIN_LIM_CLOSE) == LOW;
  portENTER_CRITICAL(&mux); in.cmd = cmdRemoto; cmdRemoto = Comando::NINGUNO; portEXIT_CRITICAL(&mux);

  const Salidas s = ctrl.paso(ahora, in);
  motor(s.motor);
  digitalWrite(PIN_BUZZER, s.buzzer);
  leds(s.led, s.led_parpadeo, ahora);

  static uint32_t vb = 0; static bool usb = true;
  if (ahora - tPub >= T_PUBLICAR_MS || vb == 0) {      // supervisión de batería
    tPub = ahora;
    vb = vbatMv();
    usb = usbPresente();
    if (!usb && vb < VBAT_BAJA_MV) Serial.printf("Bateria baja: %u mV\n", (unsigned)vb);
    if (!usb && vb < VBAT_CRITICA_MV) {                // REQ-PER-005
      motor(MotorCmd::PARAR);
      esp_sleep_enable_timer_wakeup(10ULL * 60 * 1000000);
      esp_deep_sleep_start();
    }
  }
  Instantanea e{ctrl.temp(), ctrl.rh(), ctrl.rejilla(), ctrl.manual(), ctrl.mantenimiento(),
                ctrl.alarma(), ctrl.falla_sensor(), vb, usb};
  portENTER_CRITICAL(&mux); snap = e; portEXIT_CRITICAL(&mux);
  delay(10);
}
