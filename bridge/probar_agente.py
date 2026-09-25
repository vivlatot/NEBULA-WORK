"""
Simula una sesion de Claude Code para ver reaccionar al osito sin tener que
usar Claude Code de verdad. Pasa los eventos por claude_hook.py, igual que
lo haria Claude Code.

    python probar_agente.py          (con servidor.py ya arrancado)
"""
import json
import subprocess
import sys
import time
from pathlib import Path

HOOK = Path(__file__).resolve().with_name("claude_hook.py")

GUION = [
    ({"hook_event_name": "SessionStart"}, "Empieza una sesion: el osito saluda", 4),
    ({"hook_event_name": "UserPromptSubmit"}, "Le pides algo a Claude: el osito saca el portatil", 4),
    ({"hook_event_name": "PreToolUse", "tool_name": "Read", "tool_input": {"file_path": "src/main.cpp"}}, "Claude lee un archivo", 4),
    ({"hook_event_name": "PostToolUse", "tool_name": "Read"}, "...", 2),
    ({"hook_event_name": "PreToolUse", "tool_name": "Edit", "tool_input": {"file_path": "src/osito.cpp"}}, "Claude edita codigo", 4),
    ({"hook_event_name": "PermissionRequest", "tool_name": "Bash"}, "Claude pide permiso: el osito te avisa (toca al osito para calmarlo)", 10),
    ({"hook_event_name": "PreToolUse", "tool_name": "Bash"}, "Claude ejecuta un comando", 4),
    ({"hook_event_name": "Stop"}, "Claude termina: el osito lo celebra", 8),
    ({"hook_event_name": "SessionEnd"}, "Fin de la sesion", 1),
]


def main() -> None:
    for evento, texto, espera in GUION:
        evento = {"session_id": "prueba", "cwd": str(Path.cwd()), **evento}
        print(f"-> {evento['hook_event_name']:<18} {texto}")
        subprocess.run([sys.executable, str(HOOK)], input=json.dumps(evento), text=True, check=False)
        time.sleep(espera)
    print("Listo.")


if __name__ == "__main__":
    main()
