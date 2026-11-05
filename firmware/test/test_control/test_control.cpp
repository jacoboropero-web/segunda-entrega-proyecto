// Pruebas unitarias de la lógica de control (PlatformIO: pio test -e native)
#include <unity.h>
#include "control.h"

static const Umbrales U{35.0f, 33.0f, 80.0f, 75.0f, 30.0f, 65.0f};
static const Tiempos T{30000, 5000, 60000, 1500, 900000};

static Entradas muestra(float t, float rh) { Entradas e{}; e.muestra_nueva = true; e.sensor_ok = true; e.temp_c = t; e.rh = rh; return e; }
static Entradas nada() { return Entradas{}; }

// Avanza el tiempo en pasos de 10 ms (igual que el lazo principal) sin entradas nuevas
static Salidas correr(Controlador& c, uint32_t& t, uint32_t dur, Entradas base = Entradas{}) {
  Salidas s{};
  for (uint32_t k = 0; k < dur; k += 10) { t += 10; s = c.paso(t, base); }
  return s;
}

void setUp() {}
void tearDown() {}

void test_inicio_cerrada_y_led_verde() {
  Controlador c(U, T); uint32_t t = 0;
  Salidas s = c.paso(t, muestra(25, 50));
  TEST_ASSERT_TRUE(c.rejilla() == EstadoRejilla::CERRADA);
  TEST_ASSERT_TRUE(s.motor == MotorCmd::PARAR);
  TEST_ASSERT_TRUE(s.led == ColorLed::VERDE);
}

void test_fun002_abre_en_menos_de_2s() {
  Controlador c(U, T); uint32_t t = 1000;
  Salidas s = c.paso(t, muestra(36, 50));
  TEST_ASSERT_TRUE(s.motor == MotorCmd::ABRIR);          // la orden sale en el mismo ciclo
  TEST_ASSERT_TRUE(s.led == ColorLed::ROJO);
  s = correr(c, t, 1490);
  TEST_ASSERT_TRUE(s.motor == MotorCmd::ABRIR);
  s = correr(c, t, 20);
  TEST_ASSERT_TRUE(c.rejilla() == EstadoRejilla::ABIERTA);  // 1.5 s < 2 s
  TEST_ASSERT_TRUE(s.motor == MotorCmd::PARAR);
}

void test_final_de_carrera_detiene_motor() {
  Controlador c(U, T); uint32_t t = 0;
  c.paso(t, muestra(36, 50));
  Entradas e{}; e.lim_abierta = true;
  Salidas s = c.paso(t + 300, e);
  TEST_ASSERT_TRUE(c.rejilla() == EstadoRejilla::ABIERTA);
  TEST_ASSERT_TRUE(s.motor == MotorCmd::PARAR);
}

void test_humedad_alta_abre() {
  Controlador c(U, T); uint32_t t = 0;
  Salidas s = c.paso(t, muestra(25, 85));
  TEST_ASSERT_TRUE(s.motor == MotorCmd::ABRIR);
}

void test_fun003_cierra_tras_30s_seguros() {
  Controlador c(U, T); uint32_t t = 0;
  c.paso(t, muestra(36, 50)); correr(c, t, 2000);
  t += 10; c.paso(t, muestra(30, 60));                       // entra al rango seguro
  for (int i = 0; i < 5; i++) { correr(c, t, 4990); t += 10; c.paso(t, muestra(30, 60)); }  // ~25 s
  TEST_ASSERT_TRUE(c.rejilla() == EstadoRejilla::ABIERTA);
  correr(c, t, 4900);                                          // ~29.9 s
  TEST_ASSERT_TRUE(c.rejilla() == EstadoRejilla::ABIERTA);
  Salidas s = correr(c, t, 200);                               // supera 30 s
  TEST_ASSERT_TRUE(s.motor == MotorCmd::CERRAR);
}

void test_histeresis_no_cierra_a_34C() {
  Controlador c(U, T); uint32_t t = 0;
  c.paso(t, muestra(36, 50)); correr(c, t, 2000);
  for (int i = 0; i < 20; i++) { t += 5000; c.paso(t, muestra(34, 60)); }
  TEST_ASSERT_TRUE(c.rejilla() == EstadoRejilla::ABIERTA);
}

void test_interrupcion_del_rango_seguro_reinicia_cuenta() {
  Controlador c(U, T); uint32_t t = 0;
  c.paso(t, muestra(36, 50)); correr(c, t, 2000);
  t += 10; c.paso(t, muestra(30, 60)); correr(c, t, 20000);
  t += 10; c.paso(t, muestra(34, 60));                         // sale del rango seguro
  t += 10; c.paso(t, muestra(30, 60)); correr(c, t, 20000);
  TEST_ASSERT_TRUE(c.rejilla() == EstadoRejilla::ABIERTA);    // 20 s < 30 s desde el reingreso
}

void test_fun004_alarma_buzzer_5s() {
  Controlador c(U, T); uint32_t t = 0;
  c.paso(t, muestra(25, 50));
  Entradas e{}; e.puerta_abierta = true;
  t += 10; Salidas s = c.paso(t, e);
  TEST_ASSERT_TRUE(s.buzzer);
  TEST_ASSERT_TRUE(s.led == ColorLed::ROJO && s.led_parpadeo);
  s = correr(c, t, 4980, e);
  TEST_ASSERT_TRUE(s.buzzer);
  s = correr(c, t, 30, e);
  TEST_ASSERT_FALSE(s.buzzer);                                 // termina a los 5 s
  TEST_ASSERT_TRUE(s.led == ColorLed::ROJO);                   // el LED de alarma sigue mientras esté abierta
  t += 10; s = c.paso(t, nada());                              // se cierra la puerta
  TEST_ASSERT_TRUE(s.led == ColorLed::VERDE);
}

void test_fun004_sin_alarma_en_mantenimiento() {
  Controlador c(U, T); uint32_t t = 0;
  Entradas m{}; m.pulso_mant = true; c.paso(t, m);
  TEST_ASSERT_TRUE(c.mantenimiento());
  Entradas e{}; e.puerta_abierta = true;
  t += 10; Salidas s = c.paso(t, e);
  TEST_ASSERT_FALSE(s.buzzer);
}

void test_mantenimiento_expira_a_15_min() {
  Controlador c(U, T); uint32_t t = 0;
  Entradas m{}; m.pulso_mant = true; c.paso(t, m);
  t += 899000; c.paso(t, nada());
  TEST_ASSERT_TRUE(c.mantenimiento());
  t += 2000; c.paso(t, nada());
  TEST_ASSERT_FALSE(c.mantenimiento());
}

void test_fun005_ir_alterna_y_vuelve_a_auto_en_60s() {
  Controlador c(U, T); uint32_t t = 0;
  c.paso(t, muestra(25, 50));
  Entradas ir{}; ir.pulso_ir = true;
  t += 10; Salidas s = c.paso(t, ir);
  TEST_ASSERT_TRUE(s.motor == MotorCmd::ABRIR);                // apertura manual con ambiente seguro
  correr(c, t, 59000);
  TEST_ASSERT_TRUE(c.rejilla() == EstadoRejilla::ABIERTA && c.manual());
  s = correr(c, t, 1100);
  TEST_ASSERT_FALSE(c.manual());
  TEST_ASSERT_TRUE(s.motor == MotorCmd::CERRAR || c.rejilla() == EstadoRejilla::CERRADA);
}

void test_fun005_ir_cierra_aunque_haga_calor() {
  Controlador c(U, T); uint32_t t = 0;
  c.paso(t, muestra(36, 50)); correr(c, t, 2000);
  Entradas ir{}; ir.pulso_ir = true;
  t += 10; Salidas s = c.paso(t, ir);
  TEST_ASSERT_TRUE(s.motor == MotorCmd::CERRAR);
  correr(c, t, 61000);
  TEST_ASSERT_TRUE(c.rejilla() == EstadoRejilla::ABRIENDO || c.rejilla() == EstadoRejilla::ABIERTA);
}

void test_falla_sensor_abre_por_seguridad() {
  Controlador c(U, T); uint32_t t = 0;
  c.paso(t, muestra(25, 50));
  Entradas f{}; f.muestra_nueva = true; f.sensor_ok = false;
  for (int i = 0; i < 2; i++) { t += 5000; c.paso(t, f); }
  TEST_ASSERT_TRUE(c.rejilla() == EstadoRejilla::CERRADA);
  t += 5000; Salidas s = c.paso(t, f);
  TEST_ASSERT_TRUE(c.falla_sensor());
  TEST_ASSERT_TRUE(s.motor == MotorCmd::ABRIR);
  TEST_ASSERT_TRUE(s.led == ColorLed::ROJO && s.led_parpadeo);
}

void test_comando_remoto_hasta_auto() {
  Controlador c(U, T); uint32_t t = 0;
  c.paso(t, muestra(25, 50));
  Entradas e{}; e.cmd = Comando::ABRIR;
  t += 10; c.paso(t, e); correr(c, t, 120000);
  TEST_ASSERT_TRUE(c.rejilla() == EstadoRejilla::ABIERTA);    // no expira a los 60 s
  e.cmd = Comando::AUTO; t += 10; Salidas s = c.paso(t, e);
  TEST_ASSERT_TRUE(s.motor == MotorCmd::CERRAR);
}

void test_desborde_de_millis() {
  Controlador c(U, T); uint32_t t = 0xFFFFFFFFu - 500;
  c.paso(t, muestra(36, 50));
  correr(c, t, 1600);                                          // t pasa por 0
  TEST_ASSERT_TRUE(c.rejilla() == EstadoRejilla::ABIERTA);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_inicio_cerrada_y_led_verde);
  RUN_TEST(test_fun002_abre_en_menos_de_2s);
  RUN_TEST(test_final_de_carrera_detiene_motor);
  RUN_TEST(test_humedad_alta_abre);
  RUN_TEST(test_fun003_cierra_tras_30s_seguros);
  RUN_TEST(test_histeresis_no_cierra_a_34C);
  RUN_TEST(test_interrupcion_del_rango_seguro_reinicia_cuenta);
  RUN_TEST(test_fun004_alarma_buzzer_5s);
  RUN_TEST(test_fun004_sin_alarma_en_mantenimiento);
  RUN_TEST(test_mantenimiento_expira_a_15_min);
  RUN_TEST(test_fun005_ir_alterna_y_vuelve_a_auto_en_60s);
  RUN_TEST(test_fun005_ir_cierra_aunque_haga_calor);
  RUN_TEST(test_falla_sensor_abre_por_seguridad);
  RUN_TEST(test_comando_remoto_hasta_auto);
  RUN_TEST(test_desborde_de_millis);
  return UNITY_END();
}
