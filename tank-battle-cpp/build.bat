@echo off
rem ============================================================
rem  Tank Battle - build script (MSYS2 UCRT64 toolchain)
rem
rem  NOTE for this machine: child-process PATH lookup (for DLLs
rem  and for as/ld) is blocked by some local security software,
rem  so we (1) pass -B with the absolute toolchain dir,
rem  (2) rely on DLLs copied beside cc1plus (one-shot fix:
rem  tools\fix_gcc_dlls.cmd - re-run it if gcc is reinstalled), and
rem  (3) deploy the full DLL closure next to the exe.
rem ============================================================
setlocal
set U=D:\Scoop\apps\msys2\current\ucrt64
if not exist "%U%\bin\g++.exe" (
    echo [error] g++.exe not found under %U%
    echo         install with: pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-sfml
    exit /b 1
)
rem 追加而非替换 PATH: g++ 子进程靠 -B 和 exe 旁 DLL 绕开 PATH 问题,
rem 而 python(用于 DLL 部署)在原 PATH 里
set PATH=%U%\bin;%PATH%
cd /d %~dp0

echo [1/4] compiling...
rem exe 图标资源(绝对路径调用: 本机 cmd 的 PATH 搜索同被安全软件干扰)
"%U%\bin\windres.exe" -I res res\app_icon.rc -O coff -o app_icon.res.o
if errorlevel 1 (
    echo [error] windres failed
    exit /b 1
)
rem voice control helper (C# / System.Speech). Build failure is a warning only.
rem NOTE: on this machine System.Speech.dll lives under the WPF subdirectory
rem of the .NET framework dir, hence the full-path reference below.
set CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe
if not exist "%CSC%" set CSC=%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe
set SSP=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\WPF\System.Speech.dll
"%CSC%" /nologo /codepage:65001 /target:exe /r:"%SSP%" /out:tools\voice_control.exe tools\voice_control.cs
if errorlevel 1 (
    echo [warn] voice_control build failed - game runs without voice control
)
g++ -B%U%\bin\ -std=c++20 -O2 -Wall -Wextra -Wshadow -Wconversion ^
    src/main.cpp src/Assets.cpp src/Game.cpp app_icon.res.o ^
    -o tank-battle.exe ^
    -lsfml-graphics -lsfml-audio -lsfml-window -lsfml-system -lsfml-network
if errorlevel 1 (
    echo [error] build failed
    exit /b 1
)

echo [2/4] unit tests...
g++ -B%U%\bin\ -std=c++20 -O1 -Wall -Wextra -Wshadow -Wconversion tests/unit_tests.cpp ^
    -o unit_tests.exe -lsfml-system
if errorlevel 1 (
    echo [error] unit tests failed to build
    exit /b 1
)
unit_tests.exe
if errorlevel 1 (
    echo [error] unit tests FAILED
    exit /b 1
)

echo [3/4] asset manifest check + runtime DLLs (dependency closure)...
python tools\check_manifest.py
if errorlevel 1 (
    echo [error] manifest check failed - re-run tools\extract_assets.py or sync src\Assets.cpp
    exit /b 1
)
python tools\deploy_dlls.py
if errorlevel 1 (
    echo [error] DLL deployment failed
    exit /b 1
)

echo [4/4] done.
echo   play : tank-battle.exe   ^(run from this directory so assets\ is found^)
echo   again: build.bat
endlocal
