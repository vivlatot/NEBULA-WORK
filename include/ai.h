#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------
//  "Cerebro" del osito: habla con el servidor puente (bridge/servidor.py)
//  por WiFi. Todo el trafico de red va en una tarea aparte para que las
//  animaciones nunca se congelen.
// ---------------------------------------------------------------------

enum Emotion : uint8_t { EMO_NORMAL, EMO_HAPPY, EMO_SAD, EMO_SURPRISED, EMO_SLEEPY, EMO_LOVE };

struct AiState {
  float    food, fun, energy, hygiene;
  bool     sleeping, sick;
  uint8_t  poops;
  uint32_t ageMin;
};

struct AiReply {
  char    text[64];
  Emotion emotion;
};

void aiBegin();                                        // arranca WiFi + tarea (si hay secrets.h)
bool aiEnabled();                                      // hay configuracion de WiFi/servidor
bool aiOnline();                                       // WiFi conectado
bool aiBusy();                                         // esperando respuesta
bool aiPortal();                                       // red "Osito-Config" abierta
bool aiRequest(const char* event, const AiState& st);  // encola un evento; false si ocupado
bool aiPoll(AiReply& out);                             // true si ha llegado una frase
