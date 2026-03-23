@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0modtool.ps1" %*
exit /b %ERRORLEVEL%
