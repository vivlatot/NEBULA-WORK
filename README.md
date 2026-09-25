# 🐻 Tamagotchi del Osito (ESP32 + pantalla)

Un osito rosa con **ojos en X de chocolate** y **nariz de chocolate** que vive en tu ESP32.
Hay que darle chocolate, jugar con él, mimarlo, dormirlo y limpiarle las caquitas.

Placas soportadas:

| Placa | Archivo listo para grabar | Controles |
|---|---|---|
| **Cheap Yellow Display** ESP32-2432S028R (2.8" 320x240 táctil) | `firmware/osito-cyd.bin` | Pantalla táctil + botón BOOT |
| **TTGO T-Display** (1.14" 240x135) | `firmware/osito-tdisplay.bin` | Botón izq. = siguiente, der. = aceptar |
| ESP32 DevKit + ILI9341 SPI cableado a mano | `firmware/osito-generic.bin` | Pulsadores en GPIO27 / GPIO26 |

---

## 1. Driver USB (Windows)

La ESP32 se conecta por un chip USB-serie. Mira el chip pequeño cerca del conector USB
o el **Administrador de dispositivos → Puertos (COM y LPT)**:

- **CH340 / CH341** (el de la mayoría de CYD y placas chinas):
  <https://www.wch-ic.com/downloads/CH341SER_EXE.html>
- **CP2102 / CP210x** (Silicon Labs):
  <https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers>

Tras instalarlo, al conectar la placa debe aparecer un **COMx** en "Puertos (COM y LPT)".
Si no aparece nada: prueba otro cable USB (muchos cables solo cargan, no llevan datos).

> El driver *DsHidMini* es para mandos de PS3, **no hace falta** para la ESP32.

## 2. Grabar el osito (elige UNA forma)

### Opción A — Desde el navegador (sin instalar nada) ⭐

1. Descarga el `.bin` de tu placa desde la carpeta [`firmware/`](firmware/).
2. Abre **Chrome o Edge** en <https://espressif.github.io/esptool-js/>
3. Baudrate `460800` → **Connect** → elige el puerto COM de la ESP32.
   (Si no conecta: mantén pulsado **BOOT** en la placa mientras le das a Connect.)
4. En *Flash Address* pon **`0x0`**, elige el archivo `.bin` y pulsa **Program**.
5. Cuando termine, pulsa **RST/EN** en la placa. ¡Hola osito!

### Opción B — Script de Windows

Con Python instalado, doble clic en `flashear.bat` (o desde una terminal):

```bat
flashear.bat            :: Cheap Yellow Display
flashear.bat tdisplay   :: TTGO T-Display
```

### Opción C — Compilar tú mismo (PlatformIO)

```bash
pip install platformio
pio run -e cyd -t upload        # o -e tdisplay / -e generic
pio device monitor              # ver mensajes por serie
```

## 3. Cómo se juega

- Barras arriba: **COM**ida, **DIV**ersión, **ENE**rgía, **HIG**iene.
- Menú abajo: 🍫 Comer · ⚽ Jugar · ❤️ Mimar · 🌙 Dormir · 🫧 Limpiar.
- **Táctil (CYD):** toca un icono para usarlo. Toca la cabeza del osito para mimarlo,
  tócale la **nariz de chocolate** y se relame.
- **Botones:** A = siguiente opción, B = aceptar.
- Mantener **BOOT / botón A 4 segundos** = osito nuevo.
- Después de comer hace caquitas: si no las limpias baja la higiene.
- Si lo descuidas mucho **se pone malito** (nariz derretida), pero no muere:
  se cura cuidándolo.
- Se guarda solo cada minuto (memoria NVS), sobrevive a apagarlo.

## 4. Problemas típicos

| Síntoma | Solución |
|---|---|
| Pantalla en blanco | Placa distinta: prueba otro `.bin` o ajusta pines en `platformio.ini` |
| Colores invertidos (CYD) | Añade `-DTFT_INVERSION_ON=1` (o `OFF`) en `[env:cyd]` y recompila |
| Imagen rara en CYD de 2 USB | Cambia `ILI9341_2_DRIVER` por `ST7789_DRIVER` |
| Los toques caen en otro sitio | Ajusta `TOUCH_X_MIN/MAX`, `TOUCH_Y_MIN/MAX` en `include/config.h` |
| Pantalla del revés | `-DSCREEN_ROTATION=3` en build_flags |

## Personalizar

Todo en [`include/config.h`](include/config.h): nombre (`PET_NAME`), velocidad del hambre,
sueño, etc.

## Siguiente fase

Ver [`docs/INVESTIGACION.md`](docs/INVESTIGACION.md): darle un cerebro con IA (Claude)
mediante un servidor puente, y que reaccione a lo que hacen tus agentes en el PC.
