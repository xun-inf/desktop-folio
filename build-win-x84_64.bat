@echo off
rem folio: one-click build for Windows x64.
rem Builds the C++ core (build/core) and the Electron app, same as:
rem     python scripts\build.py --generator "Visual Studio 17 2022" --toolset v143 [--config Debug^|Release]
setlocal
cd /d "%~dp0"

set "PY=python"
rem WindowsApps aliases may exist even when python cannot run.
python -c "import sys; sys.exit(sys.version_info.major != 3)" >nul 2>nul
if not errorlevel 1 goto build

set "PY=py -3"
py -3 -c "import sys; sys.exit(sys.version_info.major != 3)" >nul 2>nul
if not errorlevel 1 goto build

echo [build-win-x84_64] Python 3 is unavailable. Install Python 3 and enable python or py in PATH.
set "CODE=9009"
goto result

:build
%PY% "scripts\build.py" --generator "Visual Studio 17 2022" --toolset v143 %*
set "CODE=%ERRORLEVEL%"

:result
echo.
if not "%CODE%"=="0" (
  echo [build-win-x84_64] FAILED, exit code %CODE%
) else (
  echo [build-win-x84_64] OK
)

rem Pause after a double-click launch; command-line arguments disable the pause.
if "%~1"=="" pause
exit /b %CODE%
