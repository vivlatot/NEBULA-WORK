#pragma once

// ---------------------------------------------------------------------
//  Ajustes del juego. Todo lo que quieras personalizar esta aqui.
// ---------------------------------------------------------------------

#define PET_NAME "Osito"

// Velocidad a la que bajan las necesidades (puntos por minuto, sobre 100).
// Con estos valores, sin cuidados, el osito tiene hambre en ~1 hora.
#define RATE_FOOD        1.5f   // comida
#define RATE_FUN         2.0f   // diversion
#define RATE_ENERGY      1.0f   // energia (despierto)
#define RATE_ENERGY_SLEEP 5.0f  // energia que recupera durmiendo
#define RATE_HYGIENE     0.8f   // higiene
#define RATE_HYGIENE_POOP 1.5f  // higiene extra que pierde por cada caquita

// Segundos entre comer y hacer caquita (aleatorio entre min y max)
#define POOP_DELAY_MIN   90
#define POOP_DELAY_MAX   180

// Mantener pulsado el boton A / BOOT este tiempo reinicia la mascota
#define RESET_HOLD_MS    4000

// ---------------------------------------------------------------------
//  Entrada segun la placa
// ---------------------------------------------------------------------
#if defined(BOARD_CYD)
  // Pantalla tactil + boton BOOT (GPIO0)
  #define HAS_TOUCH     1
  #define TOUCH_CS      33
  #define TOUCH_IRQ     36
  #define TOUCH_MOSI    32
  #define TOUCH_MISO    39
  #define TOUCH_CLK     25
  // Calibracion cruda del XPT2046 (ajusta si los toques no caen donde tocas)
  #define TOUCH_X_MIN   200
  #define TOUCH_X_MAX   3700
  #define TOUCH_Y_MIN   240
  #define TOUCH_Y_MAX   3800
  #ifndef PIN_BTN_A
    #define PIN_BTN_A   0
  #endif
#elif defined(BOARD_TDISPLAY)
  #define HAS_TOUCH     0
  #ifndef PIN_BTN_A
    #define PIN_BTN_A   0     // boton izquierdo: siguiente
  #endif
  #ifndef PIN_BTN_B
    #define PIN_BTN_B   35    // boton derecho: aceptar
  #endif
#else
  #define HAS_TOUCH     0
  #ifndef PIN_BTN_A
    #define PIN_BTN_A   27
  #endif
  #ifndef PIN_BTN_B
    #define PIN_BTN_B   26
  #endif
#endif

// Rotacion de la pantalla (1 = horizontal). Usa 3 para darle la vuelta.
#ifndef SCREEN_ROTATION
  #define SCREEN_ROTATION 1
#endif
