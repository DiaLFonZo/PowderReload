@echo off
setlocal

set "POWDERRELOAD_PIO=%USERPROFILE%\.platformio\penv\Scripts\platformio.exe"
if not exist "%POWDERRELOAD_PIO%" set "POWDERRELOAD_PIO=platformio.exe"


"%POWDERRELOAD_PIO%" run --project-dir "%~dp0." --environment powderreload --target upload
set "POWDERRELOAD_RESULT=%ERRORLEVEL%"
if not "%POWDERRELOAD_RESULT%"=="0" pause
exit /b %POWDERRELOAD_RESULT%
