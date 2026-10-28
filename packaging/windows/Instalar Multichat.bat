@echo off
setlocal
rem Instala Multichat para OBS Studio 33 o superior en
rem   C:\ProgramData\obs-studio\plugins\obs-multichat
rem y quita la copia antigua de C:\Program Files\obs-studio (si la hay), que OBS 33
rem marca como "Legacy" y que OBS 34 ya no carga.

rem Hace falta permiso de administrador para escribir en ProgramData y en Program Files.
net session >nul 2>&1
if errorlevel 1 (
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

tasklist /FI "IMAGENAME eq obs64.exe" 2>nul | find /I "obs64.exe" >nul
if not errorlevel 1 (
    echo Cierra OBS antes de instalar Multichat y vuelve a abrir este archivo.
    pause
    exit /b 1
)

set "SRC=%~dp0obs-multichat"
set "DEST=%ProgramData%\obs-studio\plugins\obs-multichat"
if not exist "%SRC%\obs-multichat.dll" (
    echo No encuentro la carpeta obs-multichat junto a este archivo. Descomprime el zip completo.
    pause
    exit /b 1
)

if exist "%DEST%" rmdir /S /Q "%DEST%"
xcopy "%SRC%\*" "%DEST%" /E /I /Y /Q >nul
if errorlevel 1 (
    echo No se pudo copiar el plugin a %DEST%
    pause
    exit /b 1
)

set "OLD=%ProgramFiles%\obs-studio"
if exist "%OLD%\obs-plugins\64bit\obs-multichat.dll" del /F /Q "%OLD%\obs-plugins\64bit\obs-multichat.dll"
if exist "%OLD%\obs-plugins\64bit\obs-multichat.pdb" del /F /Q "%OLD%\obs-plugins\64bit\obs-multichat.pdb"
if exist "%OLD%\data\obs-plugins\obs-multichat" rmdir /S /Q "%OLD%\data\obs-plugins\obs-multichat"

echo.
echo Multichat instalado en:
echo   %DEST%
echo Ya puedes abrir OBS.
echo.
pause
