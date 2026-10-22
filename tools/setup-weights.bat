@echo off
cd /d "%~dp0.."
echo PS5MG: copy the model weights to the console.
set /p IP=Console address (for example 192.168.1.179): 
if "%IP%"=="" goto :eof
powershell -NoProfile -ExecutionPolicy Bypass -File "tools\setup-weights.ps1" "%IP%"
echo.
pause
