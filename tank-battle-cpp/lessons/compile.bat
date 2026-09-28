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

rem No argument = interactive menu (so double-clicking just works);
rem with an argument (compile.bat L03) it builds that lesson directly.
set LESSON=%1
if not "%LESSON%"=="" goto have_lesson
echo ============================================
echo   Tank Battle C++ - lesson builder
echo   lessons: 1 2 3 4 5 6 7 8
echo ============================================
set /p NUM=which lesson 1-8 : 
set LESSON=L0%NUM%
:have_lesson
if "%LESSON%"=="" (
    echo [error] no lesson chosen
    exit /b 1
)

set SRC=
for %%f in (lessons\%LESSON%_*.cpp) do set SRC=%%f
if "%SRC%"=="" (
    echo [error] no lessons\%LESSON%_*.cpp found
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
"%GXX%" %EXTRA% -std=c++20 -O1 -Wall %SRC% -o %LESSON%.exe ^
    -lsfml-graphics -lsfml-audio -lsfml-window -lsfml-system
if errorlevel 1 (
    echo [error] build failed - check for Chinese punctuation / missing ;
    exit /b 1
)
echo OK: %LESSON%.exe - double-click it to play (stay in this folder)
endlocal
