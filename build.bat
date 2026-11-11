@echo off
rem Compila o transmissor e o receptor (GStreamer + gcc/MinGW). Saida: bin\*.exe
rem Ajuste GST_DIR e MINGW_DIR se estiverem instalados em outro lugar.
cd /d "%~dp0"

set "GST_DIR=C:\Program Files\gstreamer\1.0\mingw_x86_64"
set "PATH=%GST_DIR%\bin;C:\mingw64\mingw64\bin;%PATH%"
set "FLAGS=-Wall -I"%GST_DIR%\include\gstreamer-1.0" -I"%GST_DIR%\include\glib-2.0" -I"%GST_DIR%\lib\glib-2.0\include" -L"%GST_DIR%\lib" -lgstreamer-1.0 -lgobject-2.0 -lglib-2.0"

if not exist bin mkdir bin

gcc src\transmissor.c %FLAGS% -o bin\transmissor.exe || goto erro
gcc src\receptor.c %FLAGS% -o bin\receptor.exe || goto erro
echo [+] Compilado: bin\transmissor.exe e bin\receptor.exe
exit /b 0

:erro
echo [!] Erro durante a compilacao!
exit /b 1
