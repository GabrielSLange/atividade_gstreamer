@echo off
cd /d "%~dp0"
echo =====================================================================
echo    Compilando e Executando a Atividade Pratica de GStreamer
echo    Disciplina: Sistemas Multimidias
echo =====================================================================

set "PATH=C:\Program Files\gstreamer\1.0\mingw_x86_64\bin;C:\mingw64\mingw64\bin;%PATH%"

rem Encerra instancias antigas que ainda estejam rodando (senao o .exe fica travado e o gcc nao consegue sobrescrever)
taskkill /f /im pipeline.exe >nul 2>&1

echo [*] Compilando pipeline.c...
gcc pipeline.c -I"C:\Program Files\gstreamer\1.0\mingw_x86_64\include\gstreamer-1.0" -I"C:\Program Files\gstreamer\1.0\mingw_x86_64\include\glib-2.0" -I"C:\Program Files\gstreamer\1.0\mingw_x86_64\lib\glib-2.0\include" -L"C:\Program Files\gstreamer\1.0\mingw_x86_64\lib" -lgstreamer-1.0 -lgobject-2.0 -lglib-2.0 -o pipeline.exe

if %ERRORLEVEL% NEQ 0 (
    echo [!] Erro durante a compilacao!
    pause
    exit /b %ERRORLEVEL%
)

echo [+] Compilado com sucesso!
echo [*] Executando pipeline.exe...
echo.
.\pipeline.exe %*

echo.
echo [*] Execucao finalizada.
pause
