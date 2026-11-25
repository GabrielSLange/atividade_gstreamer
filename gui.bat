@echo off
rem Compila e executa as interfaces graficas (servidor e cliente).
rem Precisam do MSYS2 com GTK 3 e GStreamer no ambiente UCRT64 (veja o README).
rem   gui.bat build       compila bin\servidor.exe e bin\cliente.exe
rem   gui.bat servidor    abre o servidor (transmissor)
rem   gui.bat cliente     abre o cliente (receptor)
cd /d "%~dp0"

rem O MSYS2 vai na frente do PATH para o programa usar as bibliotecas dele (e nao as do GStreamer antigo).
set "PATH=C:\msys64\ucrt64\bin;%PATH%"

if /i "%1"=="build" goto compilar
if /i "%1"=="servidor" goto executar
if /i "%1"=="cliente" goto executar
echo Uso: gui.bat build ^| servidor ^| cliente
exit /b 1

:compilar
if not exist bin mkdir bin
for /f "delims=" %%i in ('pkg-config --cflags --libs gtk+-3.0 gstreamer-1.0') do set "FLAGS=%%i"
gcc src\servidor.c -Wall -o bin\servidor.exe %FLAGS% || goto erro
gcc src\cliente.c -Wall -o bin\cliente.exe %FLAGS% || goto erro
echo [+] Compilado: bin\servidor.exe e bin\cliente.exe
exit /b 0

:executar
"bin\%1.exe"
exit /b 0

:erro
echo [!] Erro durante a compilacao!
exit /b 1
