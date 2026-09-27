@echo off
rem ============================================================
rem  Lesson compile script: run  compile.bat L01  (or L02 ...)
rem  from anywhere - it cds to tank-battle-cpp, finds a compiler,
rem  builds lessons\<name>_*.cpp into <name>.exe next to the DLLs.
rem
rem  Compiler order: (1) known MSYS2 location (dev machine - needs
rem  -B, its PATH-launched children are broken by security tools),
rem  (2) g++ on PATH (classroom portable kit).
rem ============================================================
setlocal
cd /d %~dp0..

if "%1"=="" (
    echo usage: compile.bat L01
    echo lessons: L01 L02 L03 L04 L05 L06 L07 L08
    exit /b 1
)

set SRC=
for %%f in (lessons\%1_*.cpp) do set SRC=%%f
if "%SRC%"=="" (
    echo [error] no lessons\%1_*.cpp found
    exit /b 1
)

set U=D:\Scoop\apps\msys2\current\ucrt64
set GXX=
set EXTRA=
if exist "%U%\bin\g++.exe" (
    set GXX=%U%\bin\g++.exe
    set EXTRA=-B%U%\bin\
) else (
    g++ --version >nul 2>nul
    if errorlevel 1 (
        echo [error] no g++ found - install the classroom kit or MSYS2
        exit /b 1
    )
    set GXX=g++
)

echo compiling %SRC% ...
"%GXX%" %EXTRA% -std=c++20 -O1 -Wall %SRC% -o %1.exe ^
    -lsfml-graphics -lsfml-audio -lsfml-window -lsfml-system
if errorlevel 1 (
    echo [error] build failed - check for Chinese punctuation / missing ;
    exit /b 1
)
echo OK: %1.exe - double-click it to play (stay in this folder)
endlocal
