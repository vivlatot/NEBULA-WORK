"""
Conecta Claude Code con el osito: anade (o quita) los hooks en
~/.claude/settings.json  (en Windows: C:\\Users\\<tu usuario>\\.claude\\settings.json)

    python instalar_hooks.py            -> instalar
    python instalar_hooks.py --quitar   -> desinstalar

Hace copia de seguridad (settings.json.bak) antes de tocar nada y respeta
los hooks que ya tengas.
"""
import json
import shutil
import sys
from pathlib import Path

SETTINGS = Path.home() / ".claude" / "settings.json"
HOOK_SCRIPT = Path(__file__).resolve().with_name("claude_hook.py")
MARCA = "claude_hook.py"  # para reconocer nuestros hooks al quitarlos

EVENTOS_CON_MATCHER = ["PreToolUse", "PostToolUse"]
EVENTOS = ["SessionStart", "UserPromptSubmit", "Notification", "PermissionRequest", "Stop", "SessionEnd"]


def comando() -> str:
    python = Path(sys.executable).as_posix()
    return f'"{python}" "{HOOK_SCRIPT.as_posix()}"'


def es_nuestro(grupo: dict) -> bool:
    return any(MARCA in h.get("command", "") for h in grupo.get("hooks", []))


def main() -> None:
    quitar = "--quitar" in sys.argv
    SETTINGS.parent.mkdir(parents=True, exist_ok=True)
    datos = {}
    if SETTINGS.exists():
        shutil.copy(SETTINGS, SETTINGS.with_suffix(".json.bak"))
        datos = json.loads(SETTINGS.read_text(encoding="utf-8") or "{}")

    hooks = datos.setdefault("hooks", {})
    for evento in EVENTOS_CON_MATCHER + EVENTOS:
        grupos = [g for g in hooks.get(evento, []) if not es_nuestro(g)]
        if not quitar:
            grupo = {"hooks": [{"type": "command", "command": comando(), "async": True, "timeout": 5}]}
            if evento in EVENTOS_CON_MATCHER:
                grupo["matcher"] = "*"
            grupos.append(grupo)
        if grupos:
            hooks[evento] = grupos
        else:
            hooks.pop(evento, None)
    if not hooks:
        datos.pop("hooks")

    SETTINGS.write_text(json.dumps(datos, indent=2, ensure_ascii=False), encoding="utf-8")
    if quitar:
        print(f"Hooks del osito quitados de {SETTINGS}")
    else:
        print(f"Hooks del osito instalados en {SETTINGS}")
        print("Abre una sesion NUEVA de Claude Code para que los cargue.")


if __name__ == "__main__":
    main()
