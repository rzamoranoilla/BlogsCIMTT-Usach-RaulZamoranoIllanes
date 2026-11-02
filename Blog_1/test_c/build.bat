@echo off
rem Compila test.c con el compilador de C de Visual Studio (cl.exe).
rem Uso:  build.bat            -> genera test.exe (ventanas Win32)
rem       build.bat selftest   -> genera test_selftest.exe (prueba de consola de la FFT)
rem No requiere abrir Visual Studio: basta con tener instaladas las Build Tools
rem ("Desarrollo para escritorio con C++").
setlocal
cd /d "%~dp0"

where cl.exe >nul 2>nul
if not errorlevel 1 goto :compilar

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :buscar_manual
"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TEMP%\vsdir_test_c.txt" 2>nul
set /p VSDIR=<"%TEMP%\vsdir_test_c.txt"
del /q "%TEMP%\vsdir_test_c.txt" >nul 2>nul
if defined VSDIR goto :vcvars

:buscar_manual
for /d %%d in ("%ProgramFiles%\Microsoft Visual Studio\*\*") do if exist "%%d\VC\Auxiliary\Build\vcvars64.bat" set "VSDIR=%%d"
if defined VSDIR goto :vcvars

echo No se encontro Visual Studio con herramientas de C/C++.
echo Alternativa con gcc (MSYS2 / MinGW):
echo   gcc -O2 -municode -mwindows test.c -o test.exe -lcomctl32 -lgdi32 -luser32
exit /b 1

:vcvars
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul

:compilar
if /i "%~1"=="selftest" goto :selftest
cl /nologo /W4 /O2 /utf-8 test.c /Fe:test.exe /link /SUBSYSTEM:WINDOWS
goto :fin

:selftest
cl /nologo /W4 /O2 /utf-8 /DSELFTEST test.c /Fe:test_selftest.exe /link /SUBSYSTEM:CONSOLE

:fin
if errorlevel 1 (
    echo Error de compilacion.
    exit /b 1
)
del /q *.obj >nul 2>nul
echo Listo.
