@echo off
rem Prepare Fracture from your own disc image. Drag the .iso onto this file,
rem or run: setup.bat path\to\Fracture.iso
where py >nul 2>nul
if %ERRORLEVEL%==0 (
  py -3 "%~dp0tools\setup.py" %*
) else (
  python "%~dp0tools\setup.py" %*
)
set "SETUP_EXIT=%ERRORLEVEL%"
rem Keep the window open when started from Explorer.
echo %CMDCMDLINE% | "%SystemRoot%\System32\find.exe" /i "%~0" >nul && pause
exit /b %SETUP_EXIT%
