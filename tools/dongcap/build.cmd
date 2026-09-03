@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul
cl /nologo /c /O1 /GS- /Gs9999999 /W3 dongcap.c
link /nologo /NODEFAULTLIB /ENTRY:start /SUBSYSTEM:CONSOLE,5.01 /FIXED:NO /OUT:DONGCAP.EXE dongcap.obj kernel32.lib
