// Lógica de control de la estación: independiente del hardware para poder probarla en PC.
#pragma once
#include <stdint.h>

enum class EstadoRejilla : uint8_t { CERRADA, ABRIENDO, ABIERTA, CERRANDO };
enum class MotorCmd : uint8_t { PARAR, ABRIR, CERRAR };
enum class ColorLed : uint8_t { APAGADO, VERDE, AMBAR, ROJO };
enum class Comando : uint8_t { NINGUNO, ABRIR, CERRAR, AUTO, MANT_ON, MANT_OFF };

struct Umbrales {
  float t_abrir, t_cerrar, rh_abrir, rh_cerrar, t_verde, rh_verde;
};

struct Entradas {
  bool  muestra_nueva;   // hay una lectura nueva del sensor ambiental
  bool  sensor_ok;       // la lectura fue válida
  float temp_c;
  float rh;
  bool  pulso_ir;        // flanco de detección del sensor IR (ya filtrado)
  bool  pulso_mant;      // flanco del botón de mantenimiento (ya filtrado)
  bool  puerta_abierta;  // estado del reed
  bool  lim_abierta;     // final de carrera "abierta" activo
  bool  lim_cerrada;     // final de carrera "cerrada" activo
  Comando cmd;           // comando remoto (MQTT)
};

struct Salidas {
  MotorCmd motor;
  bool     buzzer;
  ColorLed led;
  bool     led_parpadeo;   // alarma o falla: el LED destella
};

struct Tiempos {
  uint32_t cierre_seguro, alarma, override_ms, motor_max, mant_max;
};

class Controlador {
 public:
  Controlador(const Umbrales& u, const Tiempos& t);
  Salidas paso(uint32_t ahora, const Entradas& in);

  EstadoRejilla rejilla() const { return rejilla_; }
  bool mantenimiento() const { return mant_; }
  bool manual() const { return override_activo_; }
  bool alarma() const { return alarma_activa_; }
  bool falla_sensor() const { return fallas_ >= 3; }
  float temp() const { return temp_; }
  float rh() const { return rh_; }
  void set_umbrales(const Umbrales& u) { u_ = u; }
  const Umbrales& umbrales() const { return u_; }

 private:
  void ordenar(bool abrir, uint32_t ahora);
  bool objetivo_abierta() const;
  static bool vencio(uint32_t ahora, uint32_t t0, uint32_t dur) { return (uint32_t)(ahora - t0) >= dur; }

  Umbrales u_;
  Tiempos  t_;
  EstadoRejilla rejilla_ = EstadoRejilla::CERRADA;
  uint32_t t_motor_ = 0;
  bool     auto_abrir_ = false;      // decisión del control automático
  bool     seguro_ = false;          // condiciones dentro del rango seguro
  uint32_t t_seguro_ = 0;            // desde cuándo
  bool     override_activo_ = false;
  bool     override_abrir_ = false;
  bool     override_permanente_ = false;  // comando remoto ABRIR/CERRAR hasta AUTO
  uint32_t t_override_ = 0;
  bool     mant_ = false;
  uint32_t t_mant_ = 0;
  bool     alarma_activa_ = false;
  uint32_t t_alarma_ = 0;
  bool     puerta_prev_ = false;
  uint8_t  fallas_ = 0;
  bool     hay_dato_ = false;
  float    temp_ = 0, rh_ = 0;
};
