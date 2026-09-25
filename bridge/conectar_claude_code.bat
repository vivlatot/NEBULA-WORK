@echo off
REM Conecta Claude Code con el osito (instala los hooks en tu usuario)
REM Para quitarlo:  conectar_claude_code.bat --quitar
cd /d "%~dp0"
python instalar_hooks.py %1
pause
