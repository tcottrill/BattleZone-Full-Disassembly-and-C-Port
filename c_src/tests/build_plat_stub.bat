@echo off
rem Build tests\plat_stub.exe: the Windows backend (platform\windows\plat_win.c)
rem driven by the stub core tests\plat_stub.c, for platform verification before
rem app_loop.c exists / builds (M7, NOTES_platform.md).  Modelled on
rem build_win.bat: vendored framework files at /W3, our own code /W4 /std:c11.
rem
rem Objects go to obj\stub\ so they never collide with build_all.bat's (obj\)
rem or build_win.bat's (obj\win\).
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64 -no_logo
cd /d "%~dp0.."

if not exist obj mkdir obj
if not exist obj\stub mkdir obj\stub

rem ---- vendored framework (their own warning level) -------------------------
cl /nologo /O2 /W3 /MD /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE /c ^
   platform\windows\sys_gl.c platform\windows\glew.c ^
   platform\windows\log.c platform\windows\vector_draw.c ^
   platform\windows\mat4.c platform\windows\rawinput.c ^
   platform\windows\mixer.c platform\windows\fileio.c ^
   platform\windows\miniz.c platform\windows\ini.c ^
   platform\windows\joystick.c ^
   /Foobj\stub\ || exit /b 1

rem ---- our code: the Windows backend + the stub core -------------------------
echo === tests\plat_stub.exe
cl /nologo /O2 /W4 /std:c11 /MD /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE /I. /Foobj\stub\ ^
   tests\plat_stub.c platform\windows\plat_win.c ^
   obj\stub\sys_gl.obj obj\stub\glew.obj obj\stub\log.obj ^
   obj\stub\vector_draw.obj obj\stub\mat4.obj obj\stub\rawinput.obj ^
   obj\stub\mixer.obj obj\stub\fileio.obj obj\stub\miniz.obj ^
   obj\stub\ini.obj obj\stub\joystick.obj ^
   /Fe:tests\plat_stub.exe ^
   /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib winmm.lib ole32.lib || exit /b 1

echo STUB BUILD OK
