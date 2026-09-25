@echo off
REM ================================================================
REM  Arranca el cerebro del osito en este PC (Windows)
REM  Necesita Python: https://www.python.org/downloads/
REM ================================================================
setlocal
cd /d "%~dp0"

echo Instalando dependencias...
python -m pip install --quiet -r requirements.txt
if errorlevel 1 (
  echo No se pudo usar Python. Instalalo marcando "Add python.exe to PATH".
  pause
  exit /b 1
)

if "%ANTHROPIC_API_KEY%"=="" (
  echo.
  echo Pega tu API key de Anthropic ^(https://console.anthropic.com/settings/keys^)
  echo o deja vacio para probar sin IA:
  set /p ANTHROPIC_API_KEY=API key:
)
if "%OSITO_TOKEN%"=="" set OSITO_TOKEN=osito

echo.
echo Si Windows pregunta por el Firewall, permite el acceso en "Redes privadas".
python servidor.py
pause
