@echo off
setlocal
set U=D:\Scoop\apps\msys2\current\ucrt64
set G=%U%\lib\gcc\x86_64-w64-mingw32\16.2.0
set PATH=%U%\bin;C:\Windows\System32;C:\Windows

rem PATH-based DLL search is broken on this machine for child processes.
rem exe-dir search always works, so copy required DLLs beside cc1/cc1plus.
for %%D in (libgcc_s_seh-1.dll libgmp-10.dll libisl-23.dll libmpc-3.dll libmpfr-6.dll libwinpthread-1.dll zlib1.dll libzstd.dll) do copy /y "%U%\bin\%%D" "%G%\" >nul
echo DLLS_COPIED=%ERRORLEVEL%

echo --- hello world C++ test ---
cd /d D:\Berton\Tank-Battle\tank-battle-cpp
echo #include ^<iostream^> > hello.cpp
echo int main(){std::cout ^<< "Hello from g++ " ^<< __VERSION__ ^<< std::endl;} >> hello.cpp
g++ -std=c++20 hello.cpp -o hello.exe
echo CXX_EXIT=%ERRORLEVEL%
if exist hello.exe hello.exe
echo RUN_EXIT=%ERRORLEVEL%
del hello.cpp hello.exe 2>nul
endlocal
