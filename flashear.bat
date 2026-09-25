@echo off
REM ================================================================
REM  Graba el Tamagotchi del Osito en tu ESP32 (Windows)
REM  Uso:  flashear.bat            -> Cheap Yellow Display (por defecto)
REM        flashear.bat tdisplay   -> TTGO T-Display
REM        flashear.bat generic    -> ESP32 + ILI9341 cableado a mano
REM  Necesita Python instalado (https://www.python.org/downloads/)
REM ================================================================
setlocal
set PLACA=%1
if "%PLACA%"=="" set PLACA=cyd
set BIN=%~dp0firmware\osito-%PLACA%.bin

if not exist "%BIN%" (
  echo No encuentro %BIN%
  echo Placas validas: cyd, tdisplay, generic
  exit /b 1
)

echo Instalando esptool...
python -m pip install --quiet --upgrade esptool
if errorlevel 1 (
  echo.
  echo No se pudo usar Python. Instalalo desde https://www.python.org/downloads/
  echo marcando "Add python.exe to PATH", o usa el grabador web del README.
  exit /b 1
)

echo.
echo Grabando %BIN% ...
echo (si se queda en "Connecting...", manten pulsado BOOT en la placa)
python -m esptool --chip esp32 --baud 460800 write_flash 0x0 "%BIN%"
if errorlevel 1 (
  echo.
  echo Fallo la grabacion. Revisa el cable USB (que sea de datos) y el driver
  echo CH340 / CP210x (ver README).
  exit /b 1
)
echo.
echo Listo! Tu osito deberia aparecer en la pantalla.
