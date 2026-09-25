# Cómo probar el osito paso a paso

Necesitas: la placa, un cable USB **de datos**, un PC con Windows y Chrome/Edge,
y Python (<https://www.python.org/downloads/>, marca **"Add python.exe to PATH"**).
Descarga el repositorio: botón verde **Code → Download ZIP** en GitHub y descomprímelo.

---

## Prueba 1 — El osito en la pantalla (5 min, sin IA)

1. Conecta la placa. En **Administrador de dispositivos → Puertos (COM y LPT)**
   debe salir un `COMx`. Si no sale, instala el driver CH340 o CP210x (ver README).
2. Abre <https://espressif.github.io/esptool-js/> en Chrome → **Connect** → elige el COM.
   (Si no conecta, mantén pulsado **BOOT** mientras le das a Connect.)
3. Flash Address **`0x0`**, archivo `firmware/osito-cyd.bin` (o `osito-tdisplay.bin`) → **Program**.
4. Pulsa **RST**. ✅ Debe salir el osito rosa respirando y parpadeando.

Qué probar:
- [ ] Toca **Comer**: se come un chocolate a mordiscos.
- [ ] Toca **Jugar**: salta con una pelota.
- [ ] Toca su **cabeza**: corazones. Toca su **nariz**: se relame.
- [ ] Toca **Dormir**: se hace de noche con estrellas y Zzz.
- [ ] Espera ~2 min tras comer: aparece una caquita → **Limpiar**.

> Pantalla en blanco / colores raros / toques desplazados → mira "Problemas típicos" en el README
> y dime qué ves (una foto ayuda).

---

## Prueba 2 — Que hable con Claude (10 min)

1. Doble clic en **`bridge/iniciar_servidor.bat`**. Pega tu API key de
   <https://console.anthropic.com/settings/keys> (o déjalo vacío para modo prueba).
   Si Windows pregunta por el Firewall → **Permitir en redes privadas**.
2. Apunta la línea que imprime: `OSITO_SERVER "http://192.168.X.X:8765"`.
3. En el **móvil**, conéctate a la WiFi **`Osito-Config`** (la crea la placa; punto 🟡 en la esquina).
   Se abre un formulario (o entra en <http://192.168.4.1>). Rellena:
   - WiFi de casa (2.4 GHz) y contraseña
   - Servidor: la dirección del paso 2
   - Token: `osito`
4. La placa se reinicia. ✅ Punto 🟢 en la esquina y el osito te saluda con una frase nueva.

Qué probar:
- [ ] Dale de comer → primero dice "Nam nam", a los 2-3 s llega la frase de Claude.
- [ ] En el PC abre `http://192.168.X.X:8765` en el navegador, pon el token `osito`
      y escríbele "¿qué tal estás?" → la respuesta sale en la pantalla del osito.
- [ ] En la ventana del servidor ves cada frase: `[   comer]  feliz | ...`

---

## Prueba 3 — Que reaccione a Claude Code (5 min)

Con el servidor de la prueba 2 abierto:

1. **Simulación** (sin usar Claude Code): abre otra terminal en `bridge/` y ejecuta
   ```
   python probar_agente.py
   ```
   ✅ Verás en la pantalla, en ~40 s:
   - saludo 👋 → saca un **portátil** con puntitos (pensando)
   - líneas de código en el portátil y el nombre de la herramienta (`Read main.cpp`, `Edit ...`)
   - un **"!" amarillo** y el osito saltando: *"Claude pide permiso"* → **tócalo** para calmarlo
   - al terminar: **confeti** y una frase de celebración
2. **De verdad**: doble clic en **`bridge/conectar_claude_code.bat`**
   (añade los hooks a tu `C:\Users\<tú>\.claude\settings.json`, con copia `.bak`).
   Abre una sesión **nueva** de Claude Code y pídele cualquier cosa. El osito seguirá
   a Claude en vivo. Para quitarlo: `conectar_claude_code.bat --quitar`.

> Los hooks solo envían al servidor el tipo de evento, el nombre de la herramienta y
> del archivo — nunca tus prompts ni el contenido de tu código.

---

## Si algo falla, mándame

- Foto de la pantalla
- Lo que imprime la ventana del servidor
- Lo que sale en **Serial** (en esptool-js, pestaña Console a 115200) al reiniciar la placa
