// =====================================================================
//  Tamagotchi del Osito - ESP32 + TFT (TFT_eSPI)
//  Un osito rosa con ojos en X de chocolate y nariz de chocolate.
// =====================================================================
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Preferences.h>
#include "config.h"

#if HAS_TOUCH
  #include <SPI.h>
  #include <XPT2046_Touchscreen.h>
  SPIClass touchSpi(VSPI);
  XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);
#endif

TFT_eSPI    tft;
TFT_eSprite spr(&tft);
Preferences prefs;

// ---------------------------------------------------------------------
//  Colores (RGB565)
// ---------------------------------------------------------------------
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
constexpr uint16_t C_BG         = rgb(255, 240, 236);
constexpr uint16_t C_FLOOR      = rgb(246, 220, 214);
constexpr uint16_t C_NIGHT      = rgb(38, 34, 72);
constexpr uint16_t C_NIGHT_FLR  = rgb(28, 26, 56);
constexpr uint16_t C_PINK       = rgb(244, 168, 184);
constexpr uint16_t C_PINK_DARK  = rgb(226, 128, 154);
constexpr uint16_t C_PINK_NIGHT = rgb(150, 110, 150);
constexpr uint16_t C_MUZZLE     = rgb(252, 208, 214);
constexpr uint16_t C_BLUSH      = rgb(240, 110, 145);
constexpr uint16_t C_SICK       = rgb(170, 200, 140);
constexpr uint16_t C_CHOCO      = rgb(78, 40, 28);
constexpr uint16_t C_CHOCO_HI   = rgb(150, 92, 66);
constexpr uint16_t C_CHOCO_MID  = rgb(112, 62, 42);
constexpr uint16_t C_MOUTH      = rgb(120, 40, 50);
constexpr uint16_t C_TONGUE     = rgb(240, 110, 120);
constexpr uint16_t C_WHITE      = rgb(255, 255, 255);
constexpr uint16_t C_CREAM      = rgb(255, 246, 232);
constexpr uint16_t C_SHADOW     = rgb(232, 200, 196);
constexpr uint16_t C_HEART      = rgb(235, 60, 100);
constexpr uint16_t C_TEAR       = rgb(120, 180, 250);
constexpr uint16_t C_BUBBLE     = rgb(170, 220, 255);
constexpr uint16_t C_STAR       = rgb(255, 240, 160);
constexpr uint16_t C_MOON       = rgb(255, 225, 120);
constexpr uint16_t C_BAR_OK     = rgb(120, 200, 110);
constexpr uint16_t C_BAR_MID    = rgb(240, 190, 70);
constexpr uint16_t C_BAR_LOW    = rgb(230, 80, 70);
constexpr uint16_t C_BAR_BG     = rgb(60, 32, 22);
constexpr uint16_t C_BALL       = rgb(90, 170, 240);

// ---------------------------------------------------------------------
//  Estado de la mascota (se guarda en NVS)
// ---------------------------------------------------------------------
constexpr uint32_t SAVE_MAGIC = 0x4F534F31;  // "OSO1"

struct Pet {
  uint32_t magic;
  float    food, fun, energy, hygiene;
  uint32_t ageSec;
  uint16_t poopTimer;   // segundos hasta la proxima caquita (0 = ninguna pendiente)
  uint8_t  poops;
  uint8_t  sleeping;
  uint8_t  sick;
  uint16_t neglectSec;  // segundos seguidos con alguna necesidad a 0
};
Pet pet;

enum Anim : uint8_t { A_NONE, A_EAT, A_PLAY, A_PET, A_CLEAN, A_REFUSE, A_LICK, A_WAKE };
Anim     anim = A_NONE;
uint32_t animStart = 0, animDur = 0;

enum MenuItem : uint8_t { M_FEED, M_PLAY, M_PET, M_SLEEP, M_CLEAN, M_COUNT };
const char* const MENU_LABEL[M_COUNT] = {"Comer", "Jugar", "Mimar", "Dormir", "Limpiar"};
uint8_t menuSel   = 0;
bool    menuDirty = true;

char     msg[40]  = "";
uint32_t msgUntil = 0;

// Layout (se calcula en setup segun la pantalla)
int W, H, statusH, menuH, AW, AH;

// Animacion "de vida"
uint32_t nextBlink = 0, blinkStart = 0;
bool     doubleBlink = false;
float    lookX = 0, lookTarget = 0;
uint32_t nextLook = 0;

uint32_t lastTick = 0, lastFrame = 0, lastSave = 0, lastNeedMsg = 0;
int      drawnBars[4] = {-1, -1, -1, -1};

// ---------------------------------------------------------------------
//  Utilidades
// ---------------------------------------------------------------------
static float clamp100(float v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }
static float minStat() { return min(min(pet.food, pet.fun), min(pet.energy, pet.hygiene)); }
static float avgStat() { return (pet.food + pet.fun + pet.energy + pet.hygiene) / 4.0f; }

static void say(const char* text, uint32_t ms = 2200) {
  strncpy(msg, text, sizeof(msg) - 1);
  msg[sizeof(msg) - 1] = 0;
  msgUntil = millis() + ms;
}

static void startAnim(Anim a, uint32_t dur) {
  anim = a;
  animStart = millis();
  animDur = dur;
}

static void resetPet() {
  pet = Pet{};
  pet.magic   = SAVE_MAGIC;
  pet.food    = 80;
  pet.fun     = 80;
  pet.energy  = 90;
  pet.hygiene = 100;
}

static void savePet() {
  prefs.putBytes("pet", &pet, sizeof(pet));
  lastSave = millis();
}

static void loadPet() {
  Pet p;
  if (prefs.getBytes("pet", &p, sizeof(p)) == sizeof(p) && p.magic == SAVE_MAGIC) {
    pet = p;
  } else {
    resetPet();
  }
}

// ---------------------------------------------------------------------
//  Primitivas de dibujo (coordenadas en "unidades": el area del osito
//  mide 100 unidades de alto, centradas en (ox, oy))
// ---------------------------------------------------------------------
struct Pen {
  float ox, oy, s;
  float x(float u) const { return ox + u * s; }
  float y(float v) const { return oy + v * s; }
  float d(float u) const { return u * s; }
};

static void wline(const Pen& p, float x0, float y0, float x1, float y1, float w, uint16_t c) {
  spr.drawWideLine(p.x(x0), p.y(y0), p.x(x1), p.y(y1), max(1.0f, p.d(w)), c);
}

static void ellipse(const Pen& p, float x, float y, float rx, float ry, uint16_t c) {
  spr.fillEllipse(lroundf(p.x(x)), lroundf(p.y(y)), max(1L, lroundf(p.d(rx))), max(1L, lroundf(p.d(ry))), c);
}

static void circle(const Pen& p, float x, float y, float r, uint16_t c) {
  spr.fillCircle(lroundf(p.x(x)), lroundf(p.y(y)), max(1L, lroundf(p.d(r))), c);
}

static void heart(const Pen& p, float x, float y, float r, uint16_t c) {
  circle(p, x - r * 0.5f, y, r * 0.55f, c);
  circle(p, x + r * 0.5f, y, r * 0.55f, c);
  spr.fillTriangle(p.x(x - r * 1.02f), p.y(y + r * 0.15f), p.x(x + r * 1.02f), p.y(y + r * 0.15f),
                   p.x(x), p.y(y + r * 1.15f), c);
}

static void chocolateBar(const Pen& p, float x, float y, float w, float h, uint16_t c) {
  spr.fillRoundRect(p.x(x - w / 2), p.y(y - h / 2), p.d(w), p.d(h), max(1.0f, p.d(1.5f)), c);
  // cuadritos
  for (int i = 1; i < 2; i++) spr.drawFastVLine(p.x(x - w / 2 + w * i / 2), p.y(y - h / 2) + 1, p.d(h) - 2, C_CHOCO_HI);
  for (int j = 1; j < 3; j++) spr.drawFastHLine(p.x(x - w / 2) + 1, p.y(y - h / 2 + h * j / 3), p.d(w) - 2, C_CHOCO_HI);
}

static void poop(const Pen& p, float x, float y) {
  ellipse(p, x, y, 8, 3.5f, C_CHOCO_MID);
  ellipse(p, x, y - 4, 6, 3, C_CHOCO_MID);
  ellipse(p, x + 0.5f, y - 7.5f, 3.5f, 2.6f, C_CHOCO_MID);
  circle(p, x - 2, y - 4.5f, 0.9f, C_CHOCO_HI);
}

// ---------------------------------------------------------------------
//  El osito
// ---------------------------------------------------------------------
enum EyeMode : uint8_t { EYE_X, EYE_HAPPY, EYE_CLOSED };
enum MouthMode : uint8_t { MOUTH_W, MOUTH_SMILE, MOUTH_SAD, MOUTH_OPEN, MOUTH_LICK, MOUTH_SLEEP };

struct Face {
  EyeMode   eyes    = EYE_X;
  MouthMode mouth   = MOUTH_W;
  float     squash  = 1.0f;  // 1 = ojos abiertos, 0 = parpadeo cerrado
  float     eyeSize = 1.0f;
  float     tremble = 0.0f;
  float     open    = 0.0f;  // apertura de boca (comer)
  float     tongueX = 0.0f;
  bool      blush   = false;
  bool      tear    = false;
};

static void drawEye(const Pen& p, float ex, float ey, const Face& f) {
  const float a = 6.5f * f.eyeSize;
  const float w = 4.6f * f.eyeSize;
  if (f.eyes == EYE_HAPPY) {  // ^ ^
    wline(p, ex - a, ey + a * 0.35f, ex, ey - a * 0.55f, w, C_CHOCO);
    wline(p, ex, ey - a * 0.55f, ex + a, ey + a * 0.35f, w, C_CHOCO);
    return;
  }
  if (f.eyes == EYE_CLOSED) {  // u u
    wline(p, ex - a, ey, ex, ey + a * 0.45f, w * 0.75f, C_CHOCO);
    wline(p, ex, ey + a * 0.45f, ex + a, ey, w * 0.75f, C_CHOCO);
    return;
  }
  // X de chocolate que se aplasta al parpadear
  const float b = a * max(0.12f, f.squash);
  wline(p, ex - a, ey - b, ex + a, ey + b, w, C_CHOCO);
  wline(p, ex - a, ey + b, ex + a, ey - b, w, C_CHOCO);
  // brillo del chocolate
  if (f.squash > 0.6f) {
    wline(p, ex - a * 0.7f, ey - b * 0.7f - 0.9f, ex - a * 0.25f, ey - b * 0.25f - 0.9f, w * 0.28f, C_CHOCO_HI);
  }
}

static void drawBear(const Pen& p, const Face& f, bool night, uint32_t now) {
  const uint16_t pink = night ? C_PINK_NIGHT : C_PINK;
  const uint16_t earIn = night ? rgb(120, 80, 120) : C_PINK_DARK;
  const uint16_t muzzle = night ? rgb(180, 140, 170) : C_MUZZLE;

  // Orejas
  for (int k = -1; k <= 1; k += 2) {
    circle(p, k * 31, -22, 14, pink);
    circle(p, k * 31, -22, 8, earIn);
  }
  // Cabeza
  ellipse(p, 0, 8, 42, 36, pink);
  // Suciedad
  if (pet.hygiene < 30) {
    ellipse(p, -26, -8, 3.5f, 2.5f, C_CHOCO_HI);
    ellipse(p, 24, -16, 2.5f, 2, C_CHOCO_HI);
    ellipse(p, 18, 34, 3, 2, C_CHOCO_HI);
  }
  // Mejillas
  if (f.blush || pet.sick) {
    uint16_t c = pet.sick ? C_SICK : C_BLUSH;
    ellipse(p, -27, 18, 6.5f, 3.8f, c);
    ellipse(p, 27, 18, 6.5f, 3.8f, c);
  }
  // Ojos (con temblor y mirada)
  float jx = f.tremble ? (random(-10, 11) / 10.0f) * f.tremble : 0;
  float jy = f.tremble ? (random(-10, 11) / 10.0f) * f.tremble : 0;
  float dy = (f.eyeSize < 1.0f) ? 2.0f : 0.0f;
  drawEye(p, -17 + lookX + jx, 2 + dy + jy, f);
  drawEye(p, 17 + lookX + jx, 2 + dy + jy, f);
  // Lagrima
  if (f.tear) {
    float t = (now % 1400) / 1400.0f;
    circle(p, -20 + lookX, 10 + t * 10, 2.2f, C_TEAR);
  }
  // Hocico
  float mx = lookX * 0.4f;
  ellipse(p, mx, 24, 20, 13, muzzle);

  // Boca
  const float mw = 1.7f;
  switch (f.mouth) {
    case MOUTH_W:
      wline(p, mx, 22, mx, 26, mw, C_CHOCO);
      wline(p, mx - 5, 26, mx - 2.5f, 28.5f, mw, C_CHOCO);
      wline(p, mx - 2.5f, 28.5f, mx, 26, mw, C_CHOCO);
      wline(p, mx, 26, mx + 2.5f, 28.5f, mw, C_CHOCO);
      wline(p, mx + 2.5f, 28.5f, mx + 5, 26, mw, C_CHOCO);
      break;
    case MOUTH_SLEEP:
      wline(p, mx, 22, mx, 26, mw, C_CHOCO);
      ellipse(p, mx, 29, 2.2f, 1.6f + 0.6f * sinf(now / 600.0f), C_MOUTH);
      break;
    case MOUTH_SAD:
      wline(p, mx, 22, mx, 27, mw, C_CHOCO);
      wline(p, mx - 6, 31, mx, 27.5f, mw, C_CHOCO);
      wline(p, mx, 27.5f, mx + 6, 31, mw, C_CHOCO);
      break;
    case MOUTH_SMILE:
      wline(p, mx, 22, mx, 25, mw, C_CHOCO);
      ellipse(p, mx, 27, 6, 5, C_MOUTH);
      spr.fillRect(p.x(mx - 7), p.y(21.5f), p.d(14) + 1, p.d(5.3f), muzzle);
      wline(p, mx, 22, mx, 26.5f, mw, C_CHOCO);
      ellipse(p, mx, 29.5f, 3, 1.6f, C_TONGUE);
      break;
    case MOUTH_OPEN:
      wline(p, mx, 22, mx, 24, mw, C_CHOCO);
      ellipse(p, mx, 28, 5, 1.2f + 4.5f * f.open, C_MOUTH);
      if (f.open > 0.4f) ellipse(p, mx, 29 + 3 * f.open, 3, 1.4f, C_TONGUE);
      break;
    case MOUTH_LICK:
      wline(p, mx, 22, mx, 26, mw, C_CHOCO);
      wline(p, mx - 5, 26, mx - 2.5f, 28.5f, mw, C_CHOCO);
      wline(p, mx - 2.5f, 28.5f, mx, 26, mw, C_CHOCO);
      wline(p, mx, 26, mx + 2.5f, 28.5f, mw, C_CHOCO);
      wline(p, mx + 2.5f, 28.5f, mx + 5, 26, mw, C_CHOCO);
      ellipse(p, mx + f.tongueX, 30.5f, 3.2f, 3.2f, C_TONGUE);
      break;
  }

  // Nariz de chocolate (se "derrite" si esta enfermo)
  ellipse(p, mx, 17, 8.5f, 6, C_CHOCO);
  if (pet.sick) {
    float drip = 2 + 2 * ((now % 2000) / 2000.0f);
    ellipse(p, mx + 3, 21 + drip * 0.5f, 1.5f, drip, C_CHOCO);
  }
  ellipse(p, mx - 3, 15, 2.6f, 1.5f, C_CHOCO_HI);
}

// ---------------------------------------------------------------------
//  Escena completa (en el sprite)
// ---------------------------------------------------------------------
static void renderScene(uint32_t now) {
  const bool night = pet.sleeping;
  spr.fillSprite(night ? C_NIGHT : C_BG);

  const float s = AH / 100.0f;
  const float cx = AW / 2.0f;
  const float cy = AH / 2.0f - 2 * s;
  Pen ground{cx, cy, s};

  // Suelo
  spr.fillRect(0, ground.y(40), AW, AH, night ? C_NIGHT_FLR : C_FLOOR);

  // Estrellas y luna de noche
  if (night) {
    randomSeed(7);
    for (int i = 0; i < 18; i++) {
      int sx = random(AW), sy = random((int)ground.y(30));
      bool on = ((now / 400 + i) % 5) != 0;
      spr.drawPixel(sx, sy, on ? C_STAR : C_NIGHT);
      if (on && i % 4 == 0) {
        spr.drawPixel(sx + 1, sy, C_STAR);
        spr.drawPixel(sx - 1, sy, C_STAR);
        spr.drawPixel(sx, sy + 1, C_STAR);
        spr.drawPixel(sx, sy - 1, C_STAR);
      }
    }
    randomSeed(esp_random());
    spr.fillCircle(AW - 22 * s, 18 * s, 9 * s, C_MOON);
    spr.fillCircle(AW - 18 * s, 15 * s, 8 * s, C_NIGHT);
  }

  // --- cara y movimiento segun estado ---
  Face f;
  float t = anim ? (now - animStart) / (float)animDur : 0;
  float bobY = sinf(now / (night ? 900.0f : 560.0f)) * 1.3f;  // respiracion
  float bobX = 0;

  const float lo = minStat();
  if (night) {
    f.eyes = EYE_CLOSED;
    f.mouth = MOUTH_SLEEP;
  } else {
    // parpadeo: cierre rapido (70 ms), apertura lenta (180 ms)
    if (now >= nextBlink) {
      blinkStart = now;
      nextBlink = now + (doubleBlink ? 260 : random(2000, 6000));
      doubleBlink = !doubleBlink && random(6) == 0;
    }
    uint32_t bt = now - blinkStart;
    if (bt < 70) f.squash = 1.0f - bt / 70.0f;
    else if (bt < 250) f.squash = (bt - 70) / 180.0f;

    if (pet.sick || lo < 12) { f.mouth = MOUTH_SAD; f.tear = true; }
    else if (lo < 25)        f.mouth = MOUTH_SAD;
    else if (avgStat() > 75) { f.mouth = MOUTH_SMILE; f.blush = true; }
    if (pet.food < 20)   f.tremble = 0.7f;
    if (pet.energy < 20) f.eyeSize = 0.8f;
  }

  switch (anim) {
    case A_EAT:
      f.mouth = MOUTH_OPEN;
      f.open = fabsf(sinf(t * PI * 6));
      f.blush = true;
      break;
    case A_PLAY:
      f.eyes = EYE_HAPPY; f.mouth = MOUTH_SMILE; f.blush = true;
      bobY -= fabsf(sinf(t * PI * 4)) * 9;
      break;
    case A_PET:
      f.eyes = EYE_HAPPY; f.mouth = MOUTH_SMILE; f.blush = true;
      bobY += sinf(t * PI * 6) * 1.5f;
      break;
    case A_CLEAN:
      f.eyes = EYE_HAPPY; f.mouth = MOUTH_W;
      break;
    case A_REFUSE:
      bobX = sinf(t * PI * 8) * 4;
      f.mouth = MOUTH_SAD;
      break;
    case A_LICK:
      f.mouth = MOUTH_LICK; f.eyes = EYE_HAPPY; f.blush = true;
      f.tongueX = sinf(t * PI * 4) * 3;
      break;
    case A_WAKE:
      f.eyes = t < 0.5f ? EYE_CLOSED : EYE_X;
      f.mouth = MOUTH_OPEN; f.open = sinf(t * PI);  // bostezo
      break;
    default: break;
  }

  // Mirada: saltos rapidos a un punto, luego vuelve
  if (!night && anim == A_NONE && now >= nextLook) {
    lookTarget = random(3) == 0 ? 0 : random(-30, 31) / 10.0f;
    nextLook = now + random(1000, 4000);
  }
  lookX += (lookTarget - lookX) * 0.35f;

  // Sombra
  Pen body{cx + bobX * s, cy + bobY * s, s};
  float shadowW = 34 - fabsf(min(0.0f, bobY)) * 0.8f;
  ellipse(ground, 0, 46, shadowW, 3.5f, night ? rgb(22, 20, 44) : C_SHADOW);

  // Caquitas (a la derecha del osito)
  for (int i = 0; i < pet.poops; i++) poop(ground, 58 + i * 14, 45);

  drawBear(body, f, night, now);

  // --- efectos encima ---
  if (night) {
    for (int i = 0; i < 3; i++) {
      float zt = fmodf(now / 2400.0f + i / 3.0f, 1.0f);
      spr.setTextColor(C_STAR);
      spr.setTextFont(AH > 120 ? 4 : 2);
      spr.drawString("z", body.x(30 + zt * 18), body.y(-28 - zt * 22));
    }
  }
  if (anim == A_EAT) {
    // Trocito de chocolate que se va comiendo a mordiscos
    int bites = (int)(t * 3.99f);
    float w = 14 * (1.0f - bites / 3.0f);
    if (w > 0.5f) chocolateBar(body, -2 - (14 - w) / 2 + 7 - w / 2 - 5, 38, w, 10, C_CHOCO);
    for (int i = 0; i < bites; i++) circle(body, -12 + i * 9 + sinf(i * 3.1f) * 3, 42 + i % 2 * 2, 1.2f, C_CHOCO_HI);
  }
  if (anim == A_PLAY) {
    float bx = -58 + sinf(t * PI * 2) * 8;
    float by = 38 - fabsf(sinf(t * PI * 5)) * 38;
    circle(ground, bx, by, 7, C_BALL);
    circle(ground, bx - 2, by - 2, 2, C_WHITE);
  }
  if (anim == A_PET || anim == A_LICK) {
    for (int i = 0; i < 4; i++) {
      float ht = fmodf(t * 1.6f + i * 0.25f, 1.0f);
      heart(body, -38 + i * 25, -20 - ht * 26, 4.5f * (1 - ht * 0.4f), C_HEART);
    }
  }
  if (anim == A_CLEAN) {
    randomSeed(3);
    for (int i = 0; i < 14; i++) {
      float bx = random(-50, 51);
      float speed = 0.6f + random(10) / 10.0f;
      float by = 45 - fmodf(t * 110 * speed + random(90), 90);
      float r = 2 + random(4);
      spr.drawCircle(body.x(bx), body.y(by), body.d(r), C_BUBBLE);
      spr.drawPixel(body.x(bx - r * 0.4f), body.y(by - r * 0.4f), C_WHITE);
    }
    randomSeed(esp_random());
  }

  // Bocadillo de texto
  if (now < msgUntil && msg[0]) {
    spr.setTextFont(AH > 120 ? 2 : 1);
    int tw = spr.textWidth(msg) + 12;
    int th = spr.fontHeight() + 8;
    spr.fillRoundRect(4, 4, tw, th, 6, C_WHITE);
    spr.drawRoundRect(4, 4, tw, th, 6, C_CHOCO);
    spr.setTextColor(C_CHOCO, C_WHITE);
    spr.drawString(msg, 10, 8);
  }

  // Nombre y edad (esquina)
  if (!(now < msgUntil && msg[0])) {
    char buf[32];
    uint32_t h = pet.ageSec / 3600, m = (pet.ageSec / 60) % 60;
    snprintf(buf, sizeof(buf), "%s  %luh%02lum", PET_NAME, (unsigned long)h, (unsigned long)m);
    spr.setTextFont(1);
    spr.setTextColor(night ? C_PINK_NIGHT : C_PINK_DARK);
    spr.drawString(buf, 6, 5);
  }

  spr.pushSprite(0, statusH);
}

// ---------------------------------------------------------------------
//  Barra de estado y menu (se dibujan directo en la pantalla)
// ---------------------------------------------------------------------
static void drawStatus(bool force = false) {
  const float vals[4] = {pet.food, pet.fun, pet.energy, pet.hygiene};
  const char* labels[4] = {"COM", "DIV", "ENE", "HIG"};
  const int slot = W / 4;
  if (force) tft.fillRect(0, 0, W, statusH, C_CHOCO);
  tft.setTextFont(1);
  for (int i = 0; i < 4; i++) {
    int v = (int)(vals[i] + 0.5f);
    if (!force && v == drawnBars[i]) continue;
    drawnBars[i] = v;
    int x = i * slot + 4;
    int barX = x + 21, barW = slot - 29;
    int barH = statusH >= 22 ? 10 : 7;
    int barY = (statusH - barH) / 2;
    tft.setTextColor(C_CREAM, C_CHOCO);
    tft.drawString(labels[i], x, (statusH - 8) / 2);
    uint16_t c = v > 50 ? C_BAR_OK : (v > 25 ? C_BAR_MID : C_BAR_LOW);
    int fill = (barW - 2) * v / 100;
    tft.drawRect(barX, barY, barW, barH, C_CREAM);
    tft.fillRect(barX + 1, barY + 1, fill, barH - 2, c);
    tft.fillRect(barX + 1 + fill, barY + 1, barW - 2 - fill, barH - 2, C_BAR_BG);
  }
}

static void drawIcon(uint8_t idx, int x, int y, int r, bool dim, uint16_t bg) {
  const uint16_t choco = dim ? rgb(170, 150, 140) : C_CHOCO;
  switch (idx) {
    case M_FEED: {
      int w = r * 1.1f, h = r * 1.5f;
      tft.fillRoundRect(x - w / 2, y - h / 2, w, h, r / 5, choco);
      tft.drawFastVLine(x, y - h / 2 + 2, h - 4, dim ? bg : C_CHOCO_HI);
      tft.drawFastHLine(x - w / 2 + 2, y - h / 6, w - 4, dim ? bg : C_CHOCO_HI);
      tft.drawFastHLine(x - w / 2 + 2, y + h / 6, w - 4, dim ? bg : C_CHOCO_HI);
      tft.fillCircle(x + w / 2, y - h / 2, r / 3, bg);  // mordisco
      break;
    }
    case M_PLAY:
      tft.fillCircle(x, y, r * 0.7f, dim ? rgb(180, 200, 220) : C_BALL);
      tft.drawCircle(x, y, r * 0.7f, choco);
      tft.drawFastHLine(x - r * 0.7f, y, r * 1.4f, C_WHITE);
      tft.fillCircle(x - r / 4, y - r / 3, max(1, r / 6), C_WHITE);
      break;
    case M_PET: {
      uint16_t c = dim ? rgb(220, 170, 180) : C_HEART;
      int hr = r * 0.42f;
      tft.fillCircle(x - hr + 1, y - hr / 2, hr, c);
      tft.fillCircle(x + hr - 1, y - hr / 2, hr, c);
      tft.fillTriangle(x - 2 * hr + 1, y - hr / 3, x + 2 * hr - 1, y - hr / 3, x, y + r * 0.75f, c);
      break;
    }
    case M_SLEEP:
      tft.fillCircle(x, y, r * 0.65f, dim ? rgb(230, 220, 170) : C_MOON);
      tft.fillCircle(x + r * 0.35f, y - r * 0.25f, r * 0.55f, bg);
      break;
    case M_CLEAN: {
      uint16_t c = dim ? rgb(190, 210, 225) : C_BUBBLE;
      tft.fillCircle(x - r / 4, y + r / 5, r * 0.45f, c);
      tft.fillCircle(x + r / 3, y - r / 3, r * 0.3f, c);
      tft.drawCircle(x - r / 4, y + r / 5, r * 0.45f, choco);
      tft.drawCircle(x + r / 3, y - r / 3, r * 0.3f, choco);
      tft.fillCircle(x - r / 2.5f, y, max(1, r / 8), C_WHITE);
      break;
    }
  }
}

static bool itemEnabled(uint8_t i) { return !pet.sleeping || i == M_SLEEP; }

static void drawMenu() {
  menuDirty = false;
  const int y0 = H - menuH;
  const int cw = W / M_COUNT;
  const bool labels = menuH >= 40;
  tft.fillRect(0, y0, W, menuH, C_CHOCO);
  for (uint8_t i = 0; i < M_COUNT; i++) {
    int x = i * cw;
    bool sel = i == menuSel;
    uint16_t bg = sel ? C_PINK : C_CREAM;
    tft.fillRoundRect(x + 2, y0 + 3, cw - 4, menuH - 6, 6, bg);
    if (sel) tft.drawRoundRect(x + 2, y0 + 3, cw - 4, menuH - 6, 6, C_WHITE);
    int iconArea = labels ? menuH - 16 : menuH - 6;
    int r = min(cw - 8, iconArea) / 2 - 1;
    drawIcon(i, x + cw / 2, y0 + 3 + iconArea / 2 + (labels ? 1 : 0), r, !itemEnabled(i), bg);
    if (labels) {
      tft.setTextFont(1);
      tft.setTextColor(C_CHOCO, bg);
      tft.setTextDatum(BC_DATUM);
      tft.drawString(MENU_LABEL[i], x + cw / 2, H - 5);
      tft.setTextDatum(TL_DATUM);
    }
  }
}

// ---------------------------------------------------------------------
//  Acciones
// ---------------------------------------------------------------------
static void doAction(uint8_t item) {
  if (anim != A_NONE && anim != A_WAKE) return;
  if (!itemEnabled(item)) {
    say("Zzz... esta dormido");
    return;
  }
  switch (item) {
    case M_FEED:
      if (pet.food >= 95) {
        say("Estoy lleno!");
        startAnim(A_REFUSE, 1000);
        break;
      }
      pet.food = clamp100(pet.food + 25);
      pet.fun = clamp100(pet.fun + 3);
      if (pet.poopTimer == 0) pet.poopTimer = random(POOP_DELAY_MIN, POOP_DELAY_MAX);
      say("Nam nam, chocolate!");
      startAnim(A_EAT, 2400);
      break;
    case M_PLAY:
      if (pet.energy < 15) {
        say("Estoy muy cansado...");
        startAnim(A_REFUSE, 1000);
        break;
      }
      pet.fun = clamp100(pet.fun + 20);
      pet.energy = clamp100(pet.energy - 8);
      pet.food = clamp100(pet.food - 4);
      say("Yupi!");
      startAnim(A_PLAY, 3000);
      break;
    case M_PET:
      pet.fun = clamp100(pet.fun + 8);
      say("Te quiero!");
      startAnim(A_PET, 2000);
      break;
    case M_SLEEP:
      pet.sleeping = !pet.sleeping;
      if (pet.sleeping) {
        say("Buenas noches...");
      } else {
        say("Buenos dias!");
        startAnim(A_WAKE, 1500);
      }
      menuDirty = true;
      break;
    case M_CLEAN:
      pet.poops = 0;
      pet.hygiene = 100;
      say("Limpito!");
      startAnim(A_CLEAN, 2000);
      break;
  }
  savePet();
}

static void petBear(bool onNose) {
  if (pet.sleeping) {
    say("Zzz...");
    return;
  }
  if (anim != A_NONE) return;
  pet.fun = clamp100(pet.fun + 5);
  if (onNose) {
    say("Mmm, sabe a chocolate");
    startAnim(A_LICK, 1400);
  } else {
    say("Jiji!");
    startAnim(A_PET, 1600);
  }
}

// ---------------------------------------------------------------------
//  Paso del tiempo (cada segundo)
// ---------------------------------------------------------------------
static void tickSecond() {
  const float m = 1.0f / 60.0f;
  if (pet.sleeping) {
    pet.energy += RATE_ENERGY_SLEEP * m;
    pet.food -= RATE_FOOD * m * 0.5f;
    pet.fun -= RATE_FUN * m * 0.3f;
    if (pet.energy >= 100) {
      pet.sleeping = 0;
      say("Buenos dias!");
      startAnim(A_WAKE, 1500);
      menuDirty = true;
    }
  } else {
    pet.food -= RATE_FOOD * m;
    pet.fun -= RATE_FUN * m;
    pet.energy -= RATE_ENERGY * m;
    if (pet.energy <= 3) {
      pet.sleeping = 1;
      say("Me caigo de sueno...");
      menuDirty = true;
    }
  }
  pet.hygiene -= (RATE_HYGIENE + pet.poops * RATE_HYGIENE_POOP) * m;
  pet.food = clamp100(pet.food);
  pet.fun = clamp100(pet.fun);
  pet.energy = clamp100(pet.energy);
  pet.hygiene = clamp100(pet.hygiene);

  if (pet.poopTimer > 0 && --pet.poopTimer == 0 && pet.poops < 3) {
    pet.poops++;
    if (!pet.sleeping) say("Uy... perdon");
  }

  // Enfermar por abandono (sin muerte: se cura cuidandolo)
  if (minStat() <= 0) {
    if (pet.neglectSec < 65000) pet.neglectSec++;
    if (pet.neglectSec > 120 && !pet.sick) {
      pet.sick = 1;
      say("No me encuentro bien...", 3000);
    }
  } else {
    pet.neglectSec = 0;
    if (pet.sick && minStat() > 40) {
      pet.sick = 0;
      say("Ya estoy mejor!");
    }
  }

  pet.ageSec++;

  // Pedir cosas de vez en cuando
  uint32_t now = millis();
  if (!pet.sleeping && anim == A_NONE && now > msgUntil && now - lastNeedMsg > 15000) {
    lastNeedMsg = now;
    if (pet.food < 30)         say("Tengo hambre...");
    else if (pet.energy < 25)  say("Tengo sueno...");
    else if (pet.hygiene < 30) say("Necesito un banito");
    else if (pet.fun < 30)     say("Juegas conmigo?");
  }
}

// ---------------------------------------------------------------------
//  Entrada: botones y tactil
// ---------------------------------------------------------------------
struct Button {
  int8_t   pin;
  bool     down = false;
  uint32_t downAt = 0;
  bool     longFired = false;
};
Button btnA{PIN_BTN_A};
#ifdef PIN_BTN_B
Button btnB{PIN_BTN_B};
#endif

enum BtnEvent { B_NONE, B_SHORT, B_LONG };

static BtnEvent pollButton(Button& b, uint32_t now) {
  if (b.pin < 0) return B_NONE;
  bool pressed = digitalRead(b.pin) == LOW;
  if (pressed && !b.down) {
    b.down = true;
    b.downAt = now;
    b.longFired = false;
  } else if (pressed && b.down && !b.longFired && now - b.downAt > RESET_HOLD_MS) {
    b.longFired = true;
    return B_LONG;
  } else if (!pressed && b.down) {
    b.down = false;
    if (!b.longFired && now - b.downAt > 30) return B_SHORT;
  }
  return B_NONE;
}

static void handleTap(int x, int y) {
  if (y >= H - menuH) {
    uint8_t item = constrain(x / (W / M_COUNT), 0, M_COUNT - 1);
    menuSel = item;
    menuDirty = true;
    doAction(item);
    return;
  }
  if (y < statusH) return;
  // Toque sobre el osito
  const float s = AH / 100.0f;
  const float cx = AW / 2.0f, cy = statusH + AH / 2.0f - 2 * s;
  float dx = (x - cx) / s, dy = (y - cy) / s;
  if ((dx * dx) / (44 * 44) + ((dy - 8) * (dy - 8)) / (40 * 40) <= 1.0f) {
    bool nose = (dx * dx + (dy - 20) * (dy - 20)) < 14 * 14;
    petBear(nose);
  }
}

static void handleInput(uint32_t now) {
  BtnEvent a = pollButton(btnA, now);
  if (a == B_LONG) {
    resetPet();
    savePet();
    say("Hola! Soy tu osito", 3000);
    menuDirty = true;
    drawStatus(true);
  } else if (a == B_SHORT) {
    menuSel = (menuSel + 1) % M_COUNT;
    menuDirty = true;
    say(MENU_LABEL[menuSel], 900);
  }
#ifdef PIN_BTN_B
  if (pollButton(btnB, now) == B_SHORT) doAction(menuSel);
#endif

#if HAS_TOUCH
  static bool wasTouched = false;
  bool touched = ts.tirqTouched() && ts.touched();
  if (touched && !wasTouched) {
    TS_Point p = ts.getPoint();
    int x = map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, W);
    int y = map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, H);
    handleTap(constrain(x, 0, W - 1), constrain(y, 0, H - 1));
  }
  wasTouched = touched;
#endif
}

// ---------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  pinMode(PIN_BTN_A, INPUT_PULLUP);
#ifdef PIN_BTN_B
  pinMode(PIN_BTN_B, INPUT_PULLUP);  // en T-Display GPIO35 tiene pull-up externo
#endif

  tft.init();
  tft.setRotation(SCREEN_ROTATION);
  tft.fillScreen(C_BG);

#if HAS_TOUCH
  touchSpi.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  ts.begin(touchSpi);
  ts.setRotation(SCREEN_ROTATION);
#endif

  W = tft.width();
  H = tft.height();
  statusH = H >= 200 ? 22 : 16;
  menuH = H >= 200 ? 44 : 28;
  AW = W;
  AH = H - statusH - menuH;

  // Sprite de 16 bits para la escena; si no hay RAM, cae a 8 bits
  spr.setColorDepth(16);
  if (!spr.createSprite(AW, AH)) {
    spr.setColorDepth(8);
    spr.createSprite(AW, AH);
    Serial.println("Sprite en 8 bits (poca RAM)");
  }

  prefs.begin("osito", false);
  loadPet();
  randomSeed(esp_random());

  drawStatus(true);
  drawMenu();
  say(pet.ageSec < 5 ? "Hola! Soy tu osito" : "Te he echado de menos!", 3000);
  lastTick = millis();
}

void loop() {
  uint32_t now = millis();
  handleInput(now);

  while (now - lastTick >= 1000) {
    lastTick += 1000;
    tickSecond();
  }
  if (anim != A_NONE && now - animStart >= animDur) anim = A_NONE;

  if (now - lastFrame >= 40) {  // ~25 fps
    lastFrame = now;
    renderScene(now);
    drawStatus();
  }
  if (menuDirty) drawMenu();
  if (now - lastSave > 60000) savePet();
}
