@echo off
REM ================================================================
REM  App del Osito: doble clic para abrirla.
REM  Enchufa la placa por USB antes (o despues, la busca sola).
REM  Necesita Python: https://www.python.org/downloads/
REM ================================================================
setlocal
cd /d "%~dp0bridge"

python --version >nul 2>&1
if errorlevel 1 (
  echo Falta Python. Instalalo desde https://www.python.org/downloads/
  echo marcando "Add python.exe to PATH", y vuelve a abrir Osito.bat
  pause
  exit /b 1
)

echo Preparando el osito (la primera vez tarda un poco)...
python -m pip install --quiet --disable-pip-version-check -r requirements.txt

REM Abre la ventana de la app cuando el servidor este listo
start "" /min cmd /c "timeout /t 3 >nul & (start msedge --app=http://127.0.0.1:8765/app || start http://127.0.0.1:8765/app)"

echo.
echo  Osito en marcha. NO cierres esta ventana mientras uses la app.
echo.
python servidor.py
pause
