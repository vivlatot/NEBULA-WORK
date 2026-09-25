"""
Servidor puente del Osito  -  ESP32  <->  este PC  <->  Claude

El ESP32 manda eventos ("comer", "mimar", "tick"...) con sus estadisticas y
recibe una frase corta + una emocion para poner cara. La API key de Anthropic
solo vive aqui (nunca en la placa).

Arranque:
    pip install -r requirements.txt
    set ANTHROPIC_API_KEY=sk-ant-...        (Windows)   /  export ... (Linux/Mac)
    set OSITO_TOKEN=una-clave-inventada
    python servidor.py

Sin API key arranca en "modo sin IA" con frases de prueba, util para probar
la conexion con la placa.
"""
from __future__ import annotations

import json
import os
import random
import socket
import threading
import time
import unicodedata
from collections import deque
from pathlib import Path
from typing import Literal

import anthropic
import uvicorn
from fastapi import FastAPI, Header, HTTPException
from fastapi.responses import HTMLResponse
from pydantic import BaseModel, Field

# ---------------------------------------------------------------------------
# Configuracion
# ---------------------------------------------------------------------------
PET_NAME = os.environ.get("OSITO_NOMBRE", "Osito")
TOKEN = os.environ.get("OSITO_TOKEN", "osito")
MODEL = os.environ.get("OSITO_MODELO", "claude-opus-5")
PORT = int(os.environ.get("OSITO_PUERTO", "8765"))
MEMORY_FILE = Path(__file__).with_name("memoria.json")
MEMORY_TURNS = 30

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
# Memoria (ultimas frases, persistida en disco)
# ---------------------------------------------------------------------------
_lock = threading.Lock()
memoria: deque[dict] = deque(maxlen=MEMORY_TURNS)
if MEMORY_FILE.exists():
    try:
        memoria.extend(json.loads(MEMORY_FILE.read_text(encoding="utf-8")))
    except (OSError, json.JSONDecodeError):
        pass


def recordar(evento: str, texto: str) -> None:
    with _lock:
        memoria.append({"t": int(time.time()), "evento": evento, "osito": texto})
        MEMORY_FILE.write_text(json.dumps(list(memoria), ensure_ascii=False, indent=1), encoding="utf-8")


# Mensajes que la persona escribe desde la web, esperando a que la placa los recoja
bandeja: deque[Respuesta] = deque(maxlen=5)
ultimo_estado = Evento(evento="saludo")

# ---------------------------------------------------------------------------
# Texto apto para la pantalla (la fuente del TFT solo tiene ASCII)
# ---------------------------------------------------------------------------
def a_pantalla(texto: str, limite: int = 60) -> str:
    texto = texto.replace("ñ", "n").replace("Ñ", "N").replace("¡", "").replace("¿", "")
    texto = unicodedata.normalize("NFKD", texto)
    texto = "".join(c for c in texto if 32 <= ord(c) < 127)
    texto = " ".join(texto.split())
    return texto[:limite].rstrip()


# ---------------------------------------------------------------------------
# Claude
# ---------------------------------------------------------------------------
client: anthropic.Anthropic | None = None
if os.environ.get("ANTHROPIC_API_KEY") or os.environ.get("ANTHROPIC_AUTH_TOKEN"):
    client = anthropic.Anthropic()

FRASES_PRUEBA = {
    "comer": ("Mmm, chocolate! Mi favorito", "feliz"),
    "jugar": ("Pasamela! Otra vez, otra vez!", "feliz"),
    "mimar": ("Ay, que gustito...", "enamorado"),
    "nariz": ("Eh! Mi nariz no se come!", "sorprendido"),
    "dormir": ("Buenas noches, suena bonito", "sueno"),
    "despertar": ("Uaaah... ya es de dia?", "sueno"),
    "limpiar": ("Limpito y oliendo a fresa", "feliz"),
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
        print("!! API key invalida: revisa ANTHROPIC_API_KEY")
    except anthropic.RateLimitError:
        print("!! Limite de peticiones alcanzado, espera un poco")
    except anthropic.APIStatusError as e:
        print(f"!! Error de la API ({e.status_code}): {e.message}")
    except anthropic.APIConnectionError:
        print("!! Sin conexion con la API de Anthropic")
    except (StopIteration, ValueError) as e:
        print(f"!! Respuesta inesperada: {e}")
    return Respuesta(texto="Estoy pensando en chocolate...", emocion="normal")


# ---------------------------------------------------------------------------
# HTTP
# ---------------------------------------------------------------------------
app = FastAPI(title="Servidor del Osito")


def comprobar(token: str | None) -> None:
    if token != TOKEN:
        raise HTTPException(status_code=401, detail="token incorrecto")


@app.post("/hablar", response_model=Respuesta)
def hablar(ev: Evento, x_osito_token: str | None = Header(default=None)) -> Respuesta:
    """La placa cuenta que ha pasado; el osito contesta."""
    global ultimo_estado
    comprobar(x_osito_token)
    ultimo_estado = ev
    r = pensar(ev)
    recordar(ev.evento, r.texto)
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
    bandeja.append(r)


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
        if estado in FRASES_AGENTE:
            bandeja.append(Respuesta(texto=random.choice(FRASES_AGENTE[estado]),
                                     emocion="sorprendido" if estado == "permiso" else "feliz"))
        elif estado == "terminado":
            threading.Thread(target=celebrar, args=(proyecto,), daemon=True).start()
    return {"ok": True}


@app.get("/bandeja")
def recoger(x_osito_token: str | None = Header(default=None)) -> dict:
    """La placa pregunta si hay algo nuevo: mensajes de la web y estado de Claude Code."""
    comprobar(x_osito_token)
    # Si Claude Code lleva mucho sin dar senales, el osito deja el portatil
    if agente["estado"] in ("pensando", "trabajando") and time.time() - agente["t"] > 600:
        agente.update(estado="nada", herramienta="")
        agente["seq"] += 1
    out = {"hay": False, "agente": agente["estado"], "herramienta": agente["herramienta"][:24], "seq": agente["seq"]}
    if bandeja:
        r = bandeja.popleft()
        out.update(hay=True, texto=r.texto, emocion=r.emocion)
    return out


class Carta(BaseModel):
    mensaje: str
    token: str


@app.post("/escribir")
def escribir(c: Carta) -> dict:
    """Desde la web: le escribes algo al osito y su respuesta aparece en la pantalla."""
    comprobar(c.token)
    ev = ultimo_estado.model_copy(update={"evento": "te_escriben", "detalle": f'La persona te dice: "{c.mensaje[:200]}"'})
    r = pensar(ev)
    recordar(f"te dicen: {c.mensaje[:60]}", r.texto)
    bandeja.append(r)
    return {"texto": r.texto, "emocion": r.emocion}


PAGINA = """<!doctype html><html lang="es"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Habla con tu osito</title>
<style>body{font-family:system-ui;background:#fff0ec;color:#4e281c;max-width:480px;margin:0 auto;padding:16px}
h1{font-size:1.4rem}input,button{font-size:1rem;padding:10px;border-radius:10px;border:2px solid #4e281c}
input{width:100%;box-sizing:border-box;margin:6px 0}button{background:#f4a8b8;cursor:pointer}
#r p{background:#fff;border-radius:12px;padding:10px;margin:6px 0}</style></head><body>
<h1>Habla con tu osito</h1><p>Lo que escribas le llega por WiFi y responde en su pantalla.</p>
<input id="t" placeholder="Token (OSITO_TOKEN)"><input id="m" placeholder="Dile algo..." autofocus>
<button onclick="enviar()">Enviar</button><div id="r"></div>
<script>
const t=document.getElementById('t');t.value=localStorage.getItem('tok')||'';
document.getElementById('m').addEventListener('keydown',e=>{if(e.key==='Enter')enviar()});
async function enviar(){const m=document.getElementById('m');if(!m.value)return;
localStorage.setItem('tok',t.value);const txt=m.value;m.value='';
const res=await fetch('/escribir',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({mensaje:txt,token:t.value})});
const d=await res.json();const p=document.createElement('p');
p.textContent=res.ok?('Tu: '+txt+'  ->  Osito ('+d.emocion+'): '+d.texto):'Error: '+(d.detail||res.status);
document.getElementById('r').prepend(p)}
</script></body></html>"""


@app.get("/", response_class=HTMLResponse)
def portada() -> str:
    return PAGINA


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
    ip = ip_local()
    modo = f"IA con {MODEL}" if client else "SIN IA (falta ANTHROPIC_API_KEY): frases de prueba"
    print("=" * 60)
    print(f"  Servidor del {PET_NAME}  -  {modo}")
    print(f"  En secrets.h de la placa pon:  OSITO_SERVER \"http://{ip}:{PORT}\"")
    print(f"                                 OSITO_TOKEN  \"{TOKEN}\"")
    print(f"  Para escribirle desde el movil/PC: http://{ip}:{PORT}/")
    print("=" * 60)
    uvicorn.run(app, host="0.0.0.0", port=PORT, log_level="warning")
