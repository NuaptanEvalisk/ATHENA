@echo off
setlocal
set "ATHENA_HOME=%~dp0"
cd /d "%ATHENA_HOME%"
set "PATH=%ATHENA_HOME%bin;%PATH%"
start "ATHENA" "%ATHENA_HOME%bin\ATHENA.exe" %*
