@echo off
rem Load the MSVC x64 environment once, then run the given command.
rem VCVARS and VCVARS_VER come from the Makefile (dev/local.mk).
if not defined VCVARS set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS_VER set "VCVARS_VER=14.44"
if defined VSCMD_VER goto run
rem vcvars looks for vswhere.exe on PATH
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
call "%VCVARS%" -vcvars_ver=%VCVARS_VER% >nul
if errorlevel 1 (
  echo vsenv: failed to load "%VCVARS%" 1>&2
  exit /b 1
)
:run
%*
