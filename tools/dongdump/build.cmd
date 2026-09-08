@echo off
rem  Build DONGDUMP.EXE -- MSVC x86, no C runtime, XP-compatible.
rem
rem  /NODEFAULTLIB and our own entry point because the modern CRT needs Vista and the
rem  machines with working parallel ports are XP.  /Gs9999999 turns off stack probes,
rem  which would otherwise call _chkstk; /GS- turns off the stack cookie, which would
rem  otherwise call __security_init_cookie.
rem
rem  Verify the result is XP-compatible: machine 014C, magic 010B, subsystem 2 (GUI),
rem  subsystem version 5.01.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul
cl /nologo /c /O1 /GS- /Gs9999999 /W3 dongdump.c || exit /b 1
link /nologo /NODEFAULTLIB /ENTRY:start /SUBSYSTEM:WINDOWS,5.01 /FIXED:NO ^
     /OUT:DONGDUMP.EXE dongdump.obj kernel32.lib user32.lib gdi32.lib shell32.lib || exit /b 1
echo Built DONGDUMP.EXE
