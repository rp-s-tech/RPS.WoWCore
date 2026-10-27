@echo off
rem Machine check without make (same as: make doctor). Run from a terminal.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\doctor.ps1"
exit /b %errorlevel%
