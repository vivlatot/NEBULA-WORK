# Investigación: mascotas de escritorio (desk pets) y tamagotchis en ESP32

Objetivo: un tamagotchi de un **osito rosa con ojos en X de chocolate y nariz de chocolate**
en un ESP32 con pantalla, con opción de darle "cerebro" con un agente de IA.

## 1. Tamagotchis open-source para ESP32 / MCU

| Proyecto | Hardware | Gráficos | Lo que vale la pena copiar |
|---|---|---|---|
| [TamaFi](https://github.com/cifertech/TamaFi) | ESP32-S3 + ST7789 240x240, 6 botones | TFT_eSPI + sprite | Sprite a pantalla completa (sin parpadeo), motor de ánimo, evolución en 4 etapas, `Preferences` |
| [TamaClaude](https://github.com/pronphat-dev/tamaclaude) | **CYD ESP32-2432S028R** | — | Mascota que reacciona a Claude Code por BLE, páginas con swipe |
| [tamagotchi-arcade](https://github.com/AtlasInMind/tamagotchi-arcade) | **TTGO T-Display** (2 botones) | TFT_eSPI | UI con solo 2 botones, minijuegos, monedas, tienda de cosméticos |
| [Tamaguino](https://github.com/alojzjakob/Tamaguino) | Arduino + OLED | Adafruit SSD1306 | Menú de iconos, "poopómetro", minijuego de saltos, modo noche |
| [esp32-artoria-tamagotchi](https://github.com/fluphus/esp32-artoria-tamagotchi) | ESP32-S3 + SSD1351 | TFT_eSPI | Botón "poke" (mimar), cola única de animaciones, guardado con checksum, evolución ramificada |
| [ESP32-TamaPetchi](https://github.com/CyberXcyborg/ESP32-TamaPetchi) | ESP32 (+S3 táctil LVGL) | LVGL | Panel web por WiFi, minijuegos, logros, escrituras atómicas |
| [EspTama](https://github.com/netraular/espTama-Round-Cheap-Yellow-Display) | CYD redonda | LVGL | Una pantalla por acción, tienda de accesorios |
| [ArduinoGotchi](https://github.com/GaryZ88/ArduinoGotchi) / [TamaLIB](https://github.com/jcrona/tamalib/) | UNO / STM32 | U8g2 | Emulación del P1 original; separar motor de juego y hardware (HAL) |
| [EggSP32-Tama](https://github.com/luxk3/EggSP32-Tama) | ESP32 + ST7789 | TFT_eSPI | Pixel-art escalado ×N, deep sleep, reset con pulsación larga |

## 2. Mascotas con agentes de IA

| Proyecto | Cómo integra la IA |
|---|---|
| [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) | Asistente de voz: wake-word local, audio Opus → servidor (STT→LLM→TTS), **MCP** en dispositivo y en la nube. El más maduro |
| [Claude Desktop Buddy](https://github.com/anthropics/claude-desktop-buddy) | Mascota ESP32 que recibe por BLE el estado de las sesiones de Claude (dormido, ocupado, pide permiso, celebra) |
| [Stack-chan](https://github.com/stack-chan/stack-chan) | Robot M5Stack con cara; variante que llama a ChatGPT directamente |
| [ElatoAI](https://github.com/akdeb/ElatoAI) | Voz en tiempo real; ESP32 → servidor edge → proveedor LLM (API key en servidor) |
| [TokenGochi](https://github.com/hugo-alves/TokenGochi) | Tamagotchi cuyo ánimo depende del uso de Claude Code/Codex (puente Node.js en el PC) |
| [OpenPets](https://github.com/alvinunreal/openpets) / [PetGPT](https://github.com/JulesLiu390/PetGPT) | Mascotas de escritorio en PC con LLM y memoria |
| [wire-pod](https://github.com/kercre123/wire-pod) | Servidor propio para el robot Vector con LLM |

**Patrón común:** el ESP32 es un cliente ligero (pantalla, audio, sensores); la IA, la
memoria y las API keys viven en un servidor o en el PC.

### Arquitecturas posibles para el osito

- **A. ESP32 → API de Claude directo.** Simple, pero la API key queda en la flash
  (extraíble), TLS consume ~40-50 KB de RAM. Solo para prototipos con key de gasto limitado.
- **B. ESP32 → servidor puente (PC / Vercel / Cloudflare) → Claude.** ⭐ Recomendada.
  Key segura en el servidor, personalidad y memoria en el servidor, el ESP32 recibe JSON
  corto: `{"texto":"...","emocion":"feliz"}`. Se puede cambiar de modelo sin reflashear.
- **C. El osito como periférico de Claude (MCP / hooks / BLE).** El osito reacciona a lo que
  hace Claude Code en tu PC (como Desktop Buddy / TamaClaude). Coste de API cero.

## 3. Diseño y animación (qué hace que parezca vivo)

1. **Sin parpadeo:** dibujar en un sprite y enviarlo de una vez (`TFT_eSprite`).
   ILI9341 320x240 sin PSRAM: sprite 16 bits solo de la zona del personaje, o 8 bits a
   pantalla completa. ST7789 240x135: cabe entero a 16 bits.
2. **Las X significan "K.O." en cartoon:** deben leerse como rasgo del oso, no como estado.
   Animarlas: parpadeo = la X se aplasta a línea; feliz = brazos curvados (^ ^);
   cansado = X pequeña y caída; hambre = X temblando.
3. **Capas de vida siempre activas:** respiración (seno ~3,5 s), parpadeo aleatorio 2-6 s
   (cierre rápido, apertura lenta, a veces doble), micro-saltos de mirada, nada quieto >1 s.
4. **Necesidades visibles en la cara antes que en barras** (como Pou): hambre, diversión,
   energía, higiene.
5. **Reacciones inmediatas (<100 ms):** tocar la cabeza → rubor + corazones + rebote;
   tocar la nariz de chocolate → se relame; ignorarlo → se aburre y juega solo.
6. **Ciclo día/noche** con Zzz, cielo estrellado y respiración más lenta.
7. **Castigo suave, sin muerte** (como Tamagotchi Uni): triste → enfermo (nariz
   "derretida") → se recupera con cuidados.
8. **Persistencia** en NVS (`Preferences`) con autoguardado y hora del último guardado.
9. **Largo plazo:** crecer por etapas (bebé → osito → adulto), animaciones raras, días juntos.
10. **Sonido** RTTTL no bloqueante en buzzer (opcional).

## 4. Plan propuesto

1. **Fase 1 – Tamagotchi offline:** comer chocolate, jugar, mimar, dormir, limpiar;
   animaciones de las capas de vida; guardado en NVS. Placas: CYD (táctil) y T-Display (2 botones).
2. **Fase 2 – Cerebro IA (arquitectura B):** WiFi + servidor puente con la personalidad del
   osito; frases generadas según su estado.
3. **Fase 3 – Opcional (arquitectura C):** que reaccione a Claude Code / agentes en el PC.
