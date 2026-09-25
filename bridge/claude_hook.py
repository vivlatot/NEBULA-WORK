"""
Hook de Claude Code -> servidor del osito.

Claude Code ejecuta este script en cada evento (lo instala instalar_hooks.py).
Lee el JSON del evento por stdin y se lo pasa al servidor puente. Nunca
bloquea ni falla: si el servidor no esta, simplemente no hace nada.
Solo usa la libreria estandar.
"""
import json
import os
import sys
import urllib.request

SERVER = os.environ.get("OSITO_SERVIDOR_LOCAL", "http://127.0.0.1:8765")
TOKEN = os.environ.get("OSITO_TOKEN", "osito")


def main() -> None:
    try:
        data = json.load(sys.stdin)
    except (json.JSONDecodeError, ValueError):
        return
    # Solo mandamos lo necesario (nada de prompts ni contenido de archivos)
    tool_input = data.get("tool_input") or {}
    evento = {
        "hook_event_name": data.get("hook_event_name", ""),
        "session_id": data.get("session_id", ""),
        "cwd": data.get("cwd", ""),
        "tool_name": data.get("tool_name", ""),
        "notification_type": data.get("notification_type", ""),
        "file": os.path.basename(str(tool_input.get("file_path", ""))) if isinstance(tool_input, dict) else "",
    }
    req = urllib.request.Request(
        SERVER + "/agente",
        data=json.dumps(evento).encode(),
        headers={"Content-Type": "application/json", "X-Osito-Token": TOKEN},
        method="POST",
    )
    try:
        urllib.request.urlopen(req, timeout=2).read()
    except OSError:
        pass


if __name__ == "__main__":
    main()
    sys.exit(0)
