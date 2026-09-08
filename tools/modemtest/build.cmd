@echo off
rem Build and run the COM4 modem's AT-engine test.  Not part of the CMake build:
rem it stubs out the handful of 86Box symbols char_modem.c touches so the command
rem engine can be driven on its own, which is not something to link into a release.
setlocal
set PATH=C:\msys64\mingw64\bin;%PATH%
gcc -std=gnu11 -Wall -I..\..\src\include -o modemtest.exe modemtest.c ..\..\src\char\char_modem.c -lws2_32 || exit /b 1
modemtest.exe
