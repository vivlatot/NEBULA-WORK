# Continuar el proyecto en tu PC (nuevo chat)

## Dónde está todo

- **Repositorio:** https://github.com/vivlatot/nebula-work
- **Rama:** `claude/esp32-tamagotchi-oso-iqab9t`
- **Descargar:** https://github.com/vivlatot/nebula-work/archive/refs/heads/claude/esp32-tamagotchi-oso-iqab9t.zip
- **Con git:** `git clone -b claude/esp32-tamagotchi-oso-iqab9t https://github.com/vivlatot/nebula-work.git`

## Mapa de archivos

| Ruta | Qué es |
|---|---|
| `Osito.bat` | Abre la app de escritorio (doble clic) |
| `firmware/osito-cyd.bin` | Firmware listo para grabar en `0x0` (CYD 2.8" táctil) |
| `firmware/osito-tdisplay.bin` / `osito-generic.bin` | Otras placas |
| `src/main.cpp` | Juego, dibujo del osito, animaciones, menú, táctil/botones |
| `src/ai.cpp`, `include/ai.h` | WiFi + portal "Osito-Config" + enlace USB (`@O {json}`) |
| `include/config.h` | Nombre, velocidad de hambre/sueño, pines de táctil |
| `platformio.ini` | Placas: `cyd`, `tdisplay`, `generic` |
| `bridge/servidor.py` | App/servidor en el PC: Claude, USB, chat, grabar firmware |
| `bridge/app.html` | Ventana de la app |
| `bridge/claude_hook.py`, `instalar_hooks.py` | Osito reacciona a Claude Code |
| `bridge/probar_agente.py` | Simula una sesión de Claude Code |
| `PROBAR.md` | Guía de pruebas |
| `docs/INVESTIGACION.md` | Investigación de proyectos parecidos |

## Estado

- ✅ Firmware compila para las 3 placas (probado en la nube).
- ✅ App y servidor probados con placa simulada.
- ❌ **Nunca probado en la placa real.**
- ⚠️ **Bloqueo actual:** se intentó instalar **DsHidMini (driver de mando PS3) — es el
  driver equivocado**. Hay que cancelarlo/desinstalarlo e instalar **CH340**.

## Driver correcto (Windows)

1. Cancelar DsHidMini. Desinstalar si aparece en Configuración → Aplicaciones ("Nefarius"/"DsHidMini").
2. CH340: https://www.wch-ic.com/downloads/CH341SER_EXE.html → CH341SER.EXE → **INSTALL**.
   (Si el chip es CP2102: https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers)
3. Win+X → Administrador de dispositivos → Puertos (COM y LPT) → debe salir `USB-SERIAL CH340 (COMx)`.
4. Si no aparece nada al enchufar: el cable es solo de carga, usar otro.

## Pasos después del driver

1. Python: https://www.python.org/downloads/ (marcar "Add python.exe to PATH").
2. Doble clic en `Osito.bat` → pestaña 🔌 Grabar → elegir placa → Grabar.
3. ⚙️ Ajustes → API key de https://console.anthropic.com/settings/keys
4. Probar chat y botones. 💻 Claude Code → Conectar.

## Prompt para pegar en el nuevo chat (Claude Code en tu PC)

```
Estoy en la carpeta del proyecto nebula-work (rama claude/esp32-tamagotchi-oso-iqab9t):
un tamagotchi de un osito rosa con ojos en X de chocolate para ESP32 con pantalla,
con app de PC en bridge/ (servidor.py + app.html) que habla con la placa por USB
y usa Claude. Lee CONTINUAR.md, README.md y PROBAR.md.
Tengo la placa ESP32 enchufada por USB a este PC (Windows).
1) Comprueba qué chip USB tiene y si el driver (CH340/CP210x) está instalado
   (Get-PnpDevice / Administrador de dispositivos). Instalé por error DsHidMini:
   ayúdame a quitarlo si molesta.
2) Graba firmware/osito-cyd.bin en 0x0 con esptool (o el .bin que corresponda).
3) Arranca bridge/servidor.py y comprueba que la placa conecta por USB.
4) Arregla lo que falle en la placa real (pantalla, colores, táctil) y sube los cambios a la rama.
```
