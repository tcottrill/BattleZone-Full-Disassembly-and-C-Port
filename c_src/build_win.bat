@echo off
rem Build the playable Battlezone C port with the Windows backend (x64, VS2022 cl).
rem Modelled on the Tempest port's build_win.bat: the vendored framework files
rem compile at /W3 (harvested copies, not ours to rewrite); the game, the seam
rem and the Battlezone backend stay /W4 /std:c11.
rem
rem Target:
rem   bz_win.exe   (c_src\, next to bz_win.ini / bz_win.log): app_loop.c +
rem                bzstst.c + every translated module + avg.c, mathbox.c, the
rem                ROM images, c012294.c + platform\windows\* (NOTES_platform.md)
rem
rem Objects go to obj\win\ so they never collide with build_all.bat's (obj\).
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64 -no_logo
cd /d "%~dp0"

for %%f in (state_defs.h progrom.c vecrom.c mbprom.c app_loop.c bzstst.c) do if not exist %%f (echo MISSING %%f & exit /b 1)
if not exist obj mkdir obj
if not exist obj\win mkdir obj\win

rem ---- vendored framework (their own warning level) --------------------------
cl /nologo /O2 /W3 /MD /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE /c ^
   platform\windows\sys_gl.c platform\windows\glew.c ^
   platform\windows\log.c platform\windows\vector_draw.c ^
   platform\windows\mat4.c platform\windows\rawinput.c ^
   platform\windows\mixer.c platform\windows\fileio.c ^
   platform\windows\miniz.c platform\windows\ini.c ^
   platform\windows\joystick.c ^
   /Foobj\win\ || exit /b 1
cl /nologo /O2 /W3 /std:c11 /MD /D_CRT_SECURE_NO_WARNINGS /I. /c c012294.c /Foobj\win\ || exit /b 1

rem ---- the game ---------------------------------------------------------------
set MODULES=state.c vgut.c bzsoun.c coin65.c bzone_nmi.c bzone_math.c bzone_game.c bzone_disp.c bzone_robot.c bzone_enemy.c
set HW=mathbox.c mbprom.c progrom.c vecrom.c avg.c

echo === bz_win.exe
cl /nologo /O2 /W4 /std:c11 /MD /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE /I. /Foobj\win\ ^
   app_loop.c bzstst.c bz_sounds.c %MODULES% %HW% ^
   platform\windows\plat_win.c ^
   obj\win\c012294.obj ^
   obj\win\sys_gl.obj obj\win\glew.obj obj\win\log.obj ^
   obj\win\vector_draw.obj obj\win\mat4.obj obj\win\rawinput.obj ^
   obj\win\mixer.obj obj\win\fileio.obj obj\win\miniz.obj ^
   obj\win\ini.obj obj\win\joystick.obj ^
   /Fe:bz_win.exe ^
   /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib winmm.lib ole32.lib || exit /b 1

echo WIN BUILD OK
