#include "control.h"

Controlador::Controlador(const Umbrales& u, const Tiempos& t) : u_(u), t_(t) {}

bool Controlador::objetivo_abierta() const {
  return override_activo_ ? override_abrir_ : auto_abrir_;
}

void Controlador::ordenar(bool abrir, uint32_t ahora) {
  if (abrir && (rejilla_ == EstadoRejilla::CERRADA || rejilla_ == EstadoRejilla::CERRANDO)) {
    rejilla_ = EstadoRejilla::ABRIENDO; t_motor_ = ahora;
  } else if (!abrir && (rejilla_ == EstadoRejilla::ABIERTA || rejilla_ == EstadoRejilla::ABRIENDO)) {
    rejilla_ = EstadoRejilla::CERRANDO; t_motor_ = ahora;
  }
}

Salidas Controlador::paso(uint32_t ahora, const Entradas& in) {
  // ---- 1. Modo mantenimiento (botón SW3 o comando remoto); se desactiva solo tras T_MANT_MAX
  if (in.pulso_mant) { mant_ = !mant_; t_mant_ = ahora; }
  if (in.cmd == Comando::MANT_ON)  { mant_ = true;  t_mant_ = ahora; }
  if (in.cmd == Comando::MANT_OFF) { mant_ = false; }
  if (mant_ && vencio(ahora, t_mant_, t_.mant_max)) mant_ = false;

  // ---- 2. Lectura ambiental y decisión automática (REQ-FUN-002 / REQ-FUN-003)
  if (in.muestra_nueva) {
    if (in.sensor_ok) { fallas_ = 0; temp_ = in.temp_c; rh_ = in.rh; hay_dato_ = true; }
    else if (fallas_ < 255) { fallas_++; }
    const bool falla = falla_sensor();
    const bool exceso = falla || (hay_dato_ && (temp_ > u_.t_abrir || rh_ > u_.rh_abrir));
    const bool dentro = !falla && hay_dato_ && temp_ < u_.t_cerrar && rh_ < u_.rh_cerrar;
    if (exceso) { auto_abrir_ = true; seguro_ = false; }
    if (dentro) { if (!seguro_) { seguro_ = true; t_seguro_ = ahora; } }
    else        { seguro_ = false; }
  }
  if (auto_abrir_ && seguro_ && vencio(ahora, t_seguro_, t_.cierre_seguro)) auto_abrir_ = false;

  // ---- 3. Control manual: sensor IR (60 s) o comando remoto (hasta AUTO) (REQ-FUN-005)
  if (in.pulso_ir) {
    override_abrir_ = !objetivo_abierta();
    override_activo_ = true; override_permanente_ = false; t_override_ = ahora;
  }
  if (in.cmd == Comando::ABRIR || in.cmd == Comando::CERRAR) {
    override_abrir_ = (in.cmd == Comando::ABRIR);
    override_activo_ = true; override_permanente_ = true;
  }
  if (in.cmd == Comando::AUTO) override_activo_ = false;
  if (override_activo_ && !override_permanente_ && vencio(ahora, t_override_, t_.override_ms))
    override_activo_ = false;

  // ---- 4. Movimiento de la rejilla: termina por final de carrera o por tiempo máximo
  ordenar(objetivo_abierta(), ahora);
  if (rejilla_ == EstadoRejilla::ABRIENDO && (in.lim_abierta || vencio(ahora, t_motor_, t_.motor_max)))
    rejilla_ = EstadoRejilla::ABIERTA;
  if (rejilla_ == EstadoRejilla::CERRANDO && (in.lim_cerrada || vencio(ahora, t_motor_, t_.motor_max)))
    rejilla_ = EstadoRejilla::CERRADA;

  // ---- 5. Apertura no autorizada del gabinete (REQ-FUN-004)
  if (in.puerta_abierta && !puerta_prev_ && !mant_) { alarma_activa_ = true; t_alarma_ = ahora; }
  if (!in.puerta_abierta || mant_) alarma_activa_ = false;
  puerta_prev_ = in.puerta_abierta;
  const bool buzzer = alarma_activa_ && !vencio(ahora, t_alarma_, t_.alarma);

  // ---- 6. Indicación de estado (REQ-FUN-006)
  Salidas s{};
  s.motor = rejilla_ == EstadoRejilla::ABRIENDO ? MotorCmd::ABRIR
          : rejilla_ == EstadoRejilla::CERRANDO ? MotorCmd::CERRAR : MotorCmd::PARAR;
  s.buzzer = buzzer;
  if (alarma_activa_ || falla_sensor()) { s.led = ColorLed::ROJO; s.led_parpadeo = true; }
  else if (!hay_dato_) { s.led = ColorLed::AMBAR; s.led_parpadeo = true; }
  else if (temp_ >= u_.t_abrir || rh_ >= u_.rh_abrir) s.led = ColorLed::ROJO;
  else if (temp_ < u_.t_verde && rh_ < u_.rh_verde) s.led = ColorLed::VERDE;
  else s.led = ColorLed::AMBAR;
  return s;
}
