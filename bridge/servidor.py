"""
App del Osito  -  ESP32  <->  este PC  <->  Claude

La placa se conecta por USB (el mismo cable con el que se graba) o por WiFi.
Este programa:
  - le da "cerebro" al osito con Claude (la API key solo vive en este PC),
  - abre una ventana (app) con chat, barras en vivo y botones,
  - graba el firmware en la placa,
  - y conecta al osito con Claude Code (hooks).

Arranque normal:  doble clic en Osito.bat  (o:  python servidor.py)
Opciones:  --sin-usb   no buscar la placa por USB
           --puerto COM5   usar ese puerto
"""
from __future__ import annotations

import json
import os
import random
import socket
import subprocess
import sys
import threading
import time
import unicodedata
from collections import deque
from pathlib import Path
from typing import Literal

import anthropic
import uvicorn
from fastapi import FastAPI, Header, HTTPException, Request
from fastapi.responses import HTMLResponse, RedirectResponse
from pydantic import BaseModel, Field

try:
    import serial
    import serial.tools.list_ports
except ImportError:  # sin pyserial: solo WiFi
    serial = None

AQUI = Path(__file__).resolve().parent
CONFIG_FILE = AQUI / "config.json"
MEMORY_FILE = AQUI / "memoria.json"
FIRMWARE_DIR = AQUI.parent / "firmware"
MEMORY_TURNS = 30

# ---------------------------------------------------------------------------
# Configuracion (variables de entorno > config.json > valores por defecto)
# ---------------------------------------------------------------------------
def cargar_config() -> dict:
    try:
        return json.loads(CONFIG_FILE.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}


config = cargar_config()
PET_NAME = os.environ.get("OSITO_NOMBRE", config.get("nombre", "Osito"))
TOKEN = os.environ.get("OSITO_TOKEN", config.get("token", "osito"))
MODEL = os.environ.get("OSITO_MODELO", config.get("modelo", "claude-opus-5"))
PORT = int(os.environ.get("OSITO_PUERTO", "8765"))

EMOCIONES = ["normal", "feliz", "triste", "sorprendido", "sueno", "enamorado"]
Emocion = Literal["normal", "feliz", "triste", "sorprendido", "sueno", "enamorado"]

SYSTEM_PROMPT = f"""Eres {PET_NAME}, un osito de peluche rosa que vive dentro de una \
pantallita (un tamagotchi en un ESP32). Tus ojos son dos X de chocolate y tu nariz \
es un bombon de chocolate. Te encanta el chocolate, los mimos y jugar a la pelota.

Tu forma de hablar:
- Siempre en espanol, tierno, juguetón y un poco dramatico, como un peluche con alma.
- Frases MUY cortas: maximo 55 caracteres, porque caben en una pantalla diminuta.
- Nada de emojis ni markdown (la pantalla no los puede dibujar).
- Habla desde tus necesidades: si tienes hambre, sueno, estas sucio o aburrido, se nota.
- Varia: no repitas frases que ya dijiste en la memoria reciente.
- Si la persona te escribe, respondele a ella directamente y con carino.

Recibiras un evento y tu estado (0-100 en cada barra). Devuelve la frase y la emocion \
que debe poner tu cara."""


class Stats(BaseModel):
    food: float = 50
    fun: float = 50
    energy: float = 50
    hygiene: float = 50


class Evento(BaseModel):
    evento: str = Field(description="comer, jugar, mimar, dormir, despertar, limpiar, nariz, tick, necesidad, saludo...")
    stats: Stats = Stats()
    sleeping: bool = False
    sick: bool = False
    poops: int = 0
    age_min: int = 0
    detalle: str = ""


class Respuesta(BaseModel):
    texto: str
    emocion: Emocion


RESPUESTA_SCHEMA = {
    "type": "object",
    "properties": {
        "texto": {"type": "string", "description": "Frase del osito, maximo 55 caracteres"},
        "emocion": {"type": "string", "enum": EMOCIONES},
    },
    "required": ["texto", "emocion"],
    "additionalProperties": False,
}

# ---------------------------------------------------------------------------
# Memoria (ultimas frases, persistida en disco) y chat que ve la app
# ---------------------------------------------------------------------------
_lock = threading.Lock()
memoria: deque[dict] = deque(maxlen=MEMORY_TURNS)
if MEMORY_FILE.exists():
    try:
        memoria.extend(json.loads(MEMORY_FILE.read_text(encoding="utf-8")))
    except (OSError, json.JSONDecodeError):
        pass

chat: deque[dict] = deque(maxlen=60)  # {"id", "quien": "osito"|"tu"|"sistema", "texto", "emocion"}
_chat_id = 0


def al_chat(quien: str, texto: str, emocion: str = "normal") -> None:
    global _chat_id
    with _lock:
        _chat_id += 1
        chat.append({"id": _chat_id, "quien": quien, "texto": texto, "emocion": emocion})


def recordar(evento: str, texto: str) -> None:
    with _lock:
        memoria.append({"t": int(time.time()), "evento": evento, "osito": texto})
        MEMORY_FILE.write_text(json.dumps(list(memoria), ensure_ascii=False, indent=1), encoding="utf-8")


# Frases para placas conectadas por WiFi (las recogen en /bandeja)
bandeja: deque[Respuesta] = deque(maxlen=5)
ultimo_estado = Evento(evento="saludo")
ultimo_estado_t = 0.0


def a_pantalla(texto: str, limite: int = 60) -> str:
    """La fuente del TFT solo tiene ASCII."""
    texto = texto.replace("ñ", "n").replace("Ñ", "N").replace("¡", "").replace("¿", "")
    texto = unicodedata.normalize("NFKD", texto)
    texto = "".join(c for c in texto if 32 <= ord(c) < 127)
    texto = " ".join(texto.split())
    return texto[:limite].rstrip()


# ---------------------------------------------------------------------------
# Claude
# ---------------------------------------------------------------------------
client: anthropic.Anthropic | None = None


def crear_cliente() -> None:
    global client
    key = os.environ.get("ANTHROPIC_API_KEY") or config.get("api_key")
    if key or os.environ.get("ANTHROPIC_AUTH_TOKEN"):
        client = anthropic.Anthropic(api_key=key) if key else anthropic.Anthropic()
    else:
        client = None


crear_cliente()

FRASES_PRUEBA = {
    "comer": ("Mmm, chocolate! Mi favorito", "feliz"),
    "jugar": ("Pasamela! Otra vez, otra vez!", "feliz"),
    "mimar": ("Ay, que gustito...", "enamorado"),
    "caricia_cabeza": ("Ay, que gustito...", "enamorado"),
    "tocar_nariz": ("Eh! Mi nariz no se come!", "sorprendido"),
    "dormir": ("Buenas noches, suena bonito", "sueno"),
    "despertar": ("Uaaah... ya es de dia?", "sueno"),
    "limpiar": ("Limpito y oliendo a fresa", "feliz"),
    "te_escriben": ("Te escucho! (pon tu API key para que piense)", "feliz"),
}


def describir(ev: Evento) -> str:
    s = ev.stats
    lineas = [
        f"Evento: {ev.evento}" + (f" ({ev.detalle})" if ev.detalle else ""),
        f"Estado: comida {s.food:.0f}, diversion {s.fun:.0f}, energia {s.energy:.0f}, higiene {s.hygiene:.0f}",
        f"Dormido: {'si' if ev.sleeping else 'no'}. Malito: {'si' if ev.sick else 'no'}. "
        f"Caquitas en el suelo: {ev.poops}. Edad: {ev.age_min} minutos.",
    ]
    if memoria:
        recientes = "\n".join(f"- [{m['evento']}] {m['osito']}" for m in list(memoria)[-10:])
        lineas.append(f"Lo ultimo que dijiste:\n{recientes}")
    return "\n".join(lineas)


def pensar(ev: Evento) -> Respuesta:
    """Pide a Claude la frase del osito. Nunca lanza: si algo falla, frase de reserva."""
    if client is None:
        texto, emo = FRASES_PRUEBA.get(ev.evento, ("Hola! Estoy aqui", "normal"))
        return Respuesta(texto=texto, emocion=emo)
    try:
        response = client.beta.messages.create(
            model=MODEL,
            max_tokens=1024,
            system=SYSTEM_PROMPT,
            messages=[{"role": "user", "content": describir(ev)}],
            output_config={"effort": "low", "format": {"type": "json_schema", "schema": RESPUESTA_SCHEMA}},
            betas=["server-side-fallback-2026-07-01"],
            fallbacks="default",
        )
        if response.stop_reason == "refusal":
            return Respuesta(texto="Hmm... me he quedado sin palabras", emocion="sorprendido")
        texto_json = next(b.text for b in reversed(response.content) if b.type == "text")
        r = Respuesta.model_validate_json(texto_json)
        return Respuesta(texto=a_pantalla(r.texto), emocion=r.emocion)
    except anthropic.AuthenticationError:
        al_chat("sistema", "API key invalida: revisala en Ajustes")
    except anthropic.RateLimitError:
        al_chat("sistema", "Limite de peticiones de la API, espera un poco")
    except anthropic.APIStatusError as e:
        al_chat("sistema", f"Error de la API ({e.status_code}): {e.message}")
    except anthropic.APIConnectionError:
        al_chat("sistema", "Sin conexion con la API de Anthropic")
    except (StopIteration, ValueError) as e:
        print(f"!! Respuesta inesperada: {e}")
    return Respuesta(texto="Estoy pensando en chocolate...", emocion="normal")


# ---------------------------------------------------------------------------
# Enlace USB con la placa (lineas "@O {json}" a 115200)
# ---------------------------------------------------------------------------
USB_VIDS = {0x1A86: "CH340", 0x10C4: "CP210x", 0x0403: "FTDI", 0x303A: "ESP32 USB"}


class EnlaceUSB:
    def __init__(self, puerto: str | None) -> None:
        self.puerto_fijo = puerto
        self.ser = None
        self.puerto = ""
        self.ultima_linea = 0.0
        self.pausado = False
        self._wlock = threading.Lock()

    @property
    def conectado(self) -> bool:
        return self.ser is not None and time.time() - self.ultima_linea < 10

    def buscar(self) -> str | None:
        if self.puerto_fijo:
            return self.puerto_fijo
        puertos = list(serial.tools.list_ports.comports())
        conocidos = [p.device for p in puertos if p.vid in USB_VIDS]
        return conocidos[0] if conocidos else None

    def enviar(self, obj: dict) -> bool:
        if self.ser is None:
            return False
        try:
            with self._wlock:
                self.ser.write(("@O " + json.dumps(obj, ensure_ascii=True) + "\n").encode())
            return True
        except (OSError, serial.SerialException):
            self.cerrar()
            return False

    def cerrar(self) -> None:
        if self.ser is not None:
            try:
                self.ser.close()
            except Exception:
                pass
        self.ser = None

    def abrir(self, dev: str) -> None:
        s = serial.Serial()
        s.port, s.baudrate, s.timeout = dev, 115200, 0.5
        s.dtr = False  # que no reinicie la placa al abrir
        s.rts = False
        s.open()
        self.ser, self.puerto = s, dev
        print(f"[ usb ] conectado a {dev}")

    def bucle(self) -> None:
        ultimo_hola = 0.0
        while True:
            if self.pausado:
                self.cerrar()
                time.sleep(0.5)
                continue
            if self.ser is None:
                dev = self.buscar()
                if dev:
                    try:
                        self.abrir(dev)
                    except (OSError, serial.SerialException):
                        time.sleep(3)
                        continue
                else:
                    time.sleep(2)
                    continue
            if time.time() - ultimo_hola > 2:
                ultimo_hola = time.time()
                self.enviar({"t": "hola"})
            try:
                linea = self.ser.readline().decode("utf-8", "replace").strip() if self.ser else ""
            except (OSError, serial.SerialException):
                print("[ usb ] desconectado")
                self.cerrar()
                continue
            if linea.startswith("@O "):
                self.ultima_linea = time.time()
                try:
                    self.recibir(json.loads(linea[3:]))
                except (json.JSONDecodeError, ValueError):
                    pass

    def recibir(self, d: dict) -> None:
        global ultimo_estado, ultimo_estado_t
        estado = Evento(evento=d.get("evento", "estado"), stats=Stats(**d.get("stats", {})),
                        sleeping=d.get("sleeping", False), sick=d.get("sick", False),
                        poops=d.get("poops", 0), age_min=d.get("age_min", 0))
        ultimo_estado, ultimo_estado_t = estado, time.time()
        if d.get("t") == "ev":
            threading.Thread(target=self.contestar, args=(estado,), daemon=True).start()

    def contestar(self, ev: Evento) -> None:
        r = pensar(ev)
        recordar(ev.evento, r.texto)
        al_chat("osito", r.texto, r.emocion)
        print(f"[{ev.evento:>9}] {r.emocion:>11} | {r.texto}")
        self.enviar({"t": "say", "texto": r.texto, "emocion": r.emocion})


usb: EnlaceUSB | None = None


def entregar(r: Respuesta) -> None:
    """Hace llegar una frase a la placa: por USB si esta enchufada, si no por WiFi."""
    al_chat("osito", r.texto, r.emocion)
    if usb and usb.conectado:
        usb.enviar({"t": "say", "texto": r.texto, "emocion": r.emocion})
    else:
        bandeja.append(r)


# ---------------------------------------------------------------------------
# HTTP: placa por WiFi
# ---------------------------------------------------------------------------
app = FastAPI(title="Osito")


def comprobar(token: str | None) -> None:
    if token != TOKEN:
        raise HTTPException(status_code=401, detail="token incorrecto")


def solo_local(request: Request) -> None:
    if request.client is None or request.client.host not in ("127.0.0.1", "::1", "localhost"):
        raise HTTPException(status_code=403, detail="solo desde este PC")


@app.post("/hablar", response_model=Respuesta)
def hablar(ev: Evento, x_osito_token: str | None = Header(default=None)) -> Respuesta:
    """La placa (WiFi) cuenta que ha pasado; el osito contesta."""
    global ultimo_estado, ultimo_estado_t
    comprobar(x_osito_token)
    ultimo_estado, ultimo_estado_t = ev, time.time()
    r = pensar(ev)
    recordar(ev.evento, r.texto)
    al_chat("osito", r.texto, r.emocion)
    print(f"[{ev.evento:>9}] {r.emocion:>11} | {r.texto}")
    return r


# ---------------------------------------------------------------------------
# Claude Code -> osito (lo envia claude_hook.py en cada evento)
# ---------------------------------------------------------------------------
agente = {"estado": "nada", "herramienta": "", "seq": 0, "t": 0.0}

ESTADO_POR_EVENTO = {
    "SessionStart": "saludo",
    "UserPromptSubmit": "pensando",
    "PreToolUse": "trabajando",
    "PostToolUse": "pensando",
    "PermissionRequest": "permiso",
    "Stop": "terminado",
    "SessionEnd": "nada",
}

FRASES_AGENTE = {
    "saludo": ["Hola Claude! A trabajar juntos", "Nueva sesion! Traigo chocolate"],
    "permiso": ["Claude te necesita! Mira la pantalla", "Psst! Claude pide permiso", "Eh! Claude te esta esperando"],
}


class EventoAgente(BaseModel):
    hook_event_name: str
    session_id: str = ""
    cwd: str = ""
    tool_name: str = ""
    notification_type: str = ""
    file: str = ""


def celebrar(proyecto: str) -> None:
    ev = ultimo_estado.model_copy(update={"evento": "claude_termino_su_tarea", "detalle": f"en el proyecto {proyecto}"})
    r = pensar(ev) if client else Respuesta(texto="Claude ha terminado! Bien hecho!", emocion="feliz")
    recordar("claude_termino", r.texto)
    entregar(r)


@app.post("/agente")
def evento_agente(e: EventoAgente, x_osito_token: str | None = Header(default=None)) -> dict:
    comprobar(x_osito_token)
    nombre = e.hook_event_name
    estado = ESTADO_POR_EVENTO.get(nombre)
    if nombre == "Notification":
        # permission_prompt = pide permiso; idle_prompt = te espera para seguir
        estado = "permiso" if e.notification_type in ("permission_prompt", "idle_prompt", "") else None
    if estado is None:
        return {"ok": True}
    herramienta = e.tool_name + (f" {e.file}" if e.file else "")
    cambio = estado != agente["estado"] or (estado == "trabajando" and herramienta != agente["herramienta"])
    agente.update(estado=estado, herramienta=herramienta if estado == "trabajando" else "", t=time.time())
    if cambio:
        agente["seq"] += 1
        proyecto = Path(e.cwd).name if e.cwd else ""
        print(f"[ claude ] {estado:>11} {herramienta} {proyecto}")
        if usb:
            usb.enviar({"t": "agente", "agente": estado, "herramienta": agente["herramienta"][:24]})
        if estado in FRASES_AGENTE:
            entregar(Respuesta(texto=random.choice(FRASES_AGENTE[estado]),
                               emocion="sorprendido" if estado == "permiso" else "feliz"))
        elif estado == "terminado":
            threading.Thread(target=celebrar, args=(proyecto,), daemon=True).start()
    return {"ok": True}


@app.get("/bandeja")
def recoger(x_osito_token: str | None = Header(default=None)) -> dict:
    """La placa (WiFi) pregunta si hay algo nuevo: frases y estado de Claude Code."""
    comprobar(x_osito_token)
    if agente["estado"] in ("pensando", "trabajando") and time.time() - agente["t"] > 600:
        agente.update(estado="nada", herramienta="")
        agente["seq"] += 1
    out = {"hay": False, "agente": agente["estado"], "herramienta": agente["herramienta"][:24], "seq": agente["seq"]}
    if bandeja:
        r = bandeja.popleft()
        out.update(hay=True, texto=r.texto, emocion=r.emocion)
    return out


# ---------------------------------------------------------------------------
# Escribirle al osito (app, movil)
# ---------------------------------------------------------------------------
class Carta(BaseModel):
    mensaje: str
    token: str = ""


def responder_carta(mensaje: str) -> Respuesta:
    al_chat("tu", mensaje)
    ev = ultimo_estado.model_copy(update={"evento": "te_escriben", "detalle": f'La persona te dice: "{mensaje[:200]}"'})
    r = pensar(ev)
    recordar(f"te dicen: {mensaje[:60]}", r.texto)
    entregar(r)
    return r


@app.post("/escribir")
def escribir(c: Carta) -> dict:
    comprobar(c.token)
    r = responder_carta(c.mensaje)
    return {"texto": r.texto, "emocion": r.emocion}


# ---------------------------------------------------------------------------
# API de la app (solo desde este PC)
# ---------------------------------------------------------------------------
SETTINGS_CLAUDE = Path.home() / ".claude" / "settings.json"
grabacion = {"activa": False, "log": ""}


def hooks_instalados() -> bool:
    try:
        return "claude_hook.py" in SETTINGS_CLAUDE.read_text(encoding="utf-8")
    except OSError:
        return False


@app.get("/api/estado")
def api_estado(request: Request, desde: int = 0) -> dict:
    solo_local(request)
    placa = "usb" if usb and usb.conectado else ("wifi" if time.time() - ultimo_estado_t < 30 else "")
    return {
        "nombre": PET_NAME,
        "placa": placa,
        "puerto": usb.puerto if usb and usb.conectado else "",
        "stats": ultimo_estado.stats.model_dump() if placa else None,
        "dormido": ultimo_estado.sleeping,
        "malito": ultimo_estado.sick,
        "caquitas": ultimo_estado.poops,
        "edad_min": ultimo_estado.age_min,
        "ia": bool(client),
        "modelo": MODEL,
        "agente": agente["estado"],
        "herramienta": agente["herramienta"],
        "hooks": hooks_instalados(),
        "chat": [m for m in list(chat) if m["id"] > desde],
        "grabando": grabacion["activa"],
        "log": grabacion["log"][-2000:],
        "ip": f"http://{ip_local()}:{PORT}",
        "token": TOKEN,
        "firmwares": sorted(p.stem.replace("osito-", "") for p in FIRMWARE_DIR.glob("osito-*.bin")),
    }


class Accion(BaseModel):
    item: int


@app.post("/api/accion")
def api_accion(a: Accion, request: Request) -> dict:
    solo_local(request)
    if not (usb and usb.conectado):
        raise HTTPException(status_code=409, detail="La placa no esta conectada por USB")
    usb.enviar({"t": "accion", "item": a.item})
    return {"ok": True}


class Mensaje(BaseModel):
    mensaje: str


@app.post("/api/escribir")
def api_escribir(m: Mensaje, request: Request) -> dict:
    solo_local(request)
    threading.Thread(target=responder_carta, args=(m.mensaje,), daemon=True).start()
    return {"ok": True}


class Ajustes(BaseModel):
    api_key: str | None = None
    modelo: str | None = None


@app.post("/api/ajustes")
def api_ajustes(a: Ajustes, request: Request) -> dict:
    global MODEL
    solo_local(request)
    if a.api_key is not None:
        config["api_key"] = a.api_key.strip()
    if a.modelo:
        config["modelo"] = MODEL = a.modelo.strip()
    CONFIG_FILE.write_text(json.dumps(config, indent=2), encoding="utf-8")
    crear_cliente()
    al_chat("sistema", f"Ajustes guardados. IA: {'activada con ' + MODEL if client else 'desactivada'}")
    return {"ok": True, "ia": bool(client)}


class Hooks(BaseModel):
    instalar: bool


@app.post("/api/hooks")
def api_hooks(h: Hooks, request: Request) -> dict:
    solo_local(request)
    args = [sys.executable, str(AQUI / "instalar_hooks.py")] + ([] if h.instalar else ["--quitar"])
    out = subprocess.run(args, capture_output=True, text=True, check=False)
    al_chat("sistema", (out.stdout or out.stderr).strip().splitlines()[0] if (out.stdout or out.stderr) else "Hecho")
    return {"ok": out.returncode == 0, "hooks": hooks_instalados()}


class Grabar(BaseModel):
    placa: str


def grabar_firmware(placa: str) -> None:
    binario = FIRMWARE_DIR / f"osito-{placa}.bin"
    grabacion.update(activa=True, log=f"Grabando {binario.name}...\n")
    if usb:
        usb.pausado = True
        time.sleep(1.2)  # suelta el puerto
    try:
        puerto = (usb.puerto if usb and usb.puerto else None) or (usb.buscar() if usb else None)
        cmd = [sys.executable, "-m", "esptool", "--chip", "esp32", "--baud", "460800"]
        if puerto:
            cmd += ["--port", puerto]
        cmd += ["write_flash", "0x0", str(binario)]
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        for linea in proc.stdout:
            grabacion["log"] += linea
        proc.wait()
        grabacion["log"] += "\nListo! El osito se esta reiniciando.\n" if proc.returncode == 0 else \
            "\nFallo. Si se queda en 'Connecting...', manten pulsado BOOT en la placa y vuelve a probar.\n"
    except FileNotFoundError:
        grabacion["log"] += "No encuentro esptool: pip install esptool\n"
    finally:
        grabacion["activa"] = False
        if usb:
            usb.pausado = False


@app.post("/api/grabar")
def api_grabar(g: Grabar, request: Request) -> dict:
    solo_local(request)
    if grabacion["activa"]:
        raise HTTPException(status_code=409, detail="Ya estoy grabando")
    if not (FIRMWARE_DIR / f"osito-{g.placa}.bin").exists():
        raise HTTPException(status_code=404, detail="No existe ese firmware")
    threading.Thread(target=grabar_firmware, args=(g.placa,), daemon=True).start()
    return {"ok": True}


# ---------------------------------------------------------------------------
# Paginas
# ---------------------------------------------------------------------------
PAGINA_MOVIL = """<!doctype html><html lang="es"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Habla con tu osito</title>
<style>body{font-family:system-ui;background:#fff0ec;color:#4e281c;max-width:480px;margin:0 auto;padding:16px}
input,button{font-size:1rem;padding:10px;border-radius:10px;border:2px solid #4e281c}
input{width:100%;box-sizing:border-box;margin:6px 0}button{background:#f4a8b8;cursor:pointer}
#r p{background:#fff;border-radius:12px;padding:10px;margin:6px 0}</style></head><body>
<h1>Habla con tu osito</h1><input id="t" placeholder="Token"><input id="m" placeholder="Dile algo..." autofocus>
<button onclick="enviar()">Enviar</button><div id="r"></div>
<script>
const t=document.getElementById('t');t.value=localStorage.getItem('tok')||'';
document.getElementById('m').addEventListener('keydown',e=>{if(e.key==='Enter')enviar()});
async function enviar(){const m=document.getElementById('m');if(!m.value)return;
localStorage.setItem('tok',t.value);const txt=m.value;m.value='';
const res=await fetch('/escribir',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({mensaje:txt,token:t.value})});
const d=await res.json();const p=document.createElement('p');
p.textContent=res.ok?('Tu: '+txt+'  ->  Osito: '+d.texto):'Error: '+(d.detail||res.status);
document.getElementById('r').prepend(p)}
</script></body></html>"""


@app.get("/", response_class=HTMLResponse)
def portada(request: Request):
    if request.client and request.client.host in ("127.0.0.1", "::1"):
        return RedirectResponse("/app")
    return PAGINA_MOVIL


@app.get("/app", response_class=HTMLResponse)
def pagina_app(request: Request) -> str:
    solo_local(request)
    return (AQUI / "app.html").read_text(encoding="utf-8")


def ip_local() -> str:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("10.255.255.255", 1))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


if __name__ == "__main__":
    if serial and "--sin-usb" not in sys.argv:
        puerto = sys.argv[sys.argv.index("--puerto") + 1] if "--puerto" in sys.argv else None
        usb = EnlaceUSB(puerto)
        threading.Thread(target=usb.bucle, daemon=True).start()
    ip = ip_local()
    modo = f"IA con {MODEL}" if client else "SIN IA (pon tu API key en Ajustes)"
    print("=" * 60)
    print(f"  App del {PET_NAME}  -  {modo}")
    print(f"  Ventana de la app:  http://127.0.0.1:{PORT}/app")
    print(f"  Placa por WiFi:     servidor http://{ip}:{PORT}  token {TOKEN}")
    print("=" * 60)
    uvicorn.run(app, host="0.0.0.0", port=PORT, log_level="warning")
