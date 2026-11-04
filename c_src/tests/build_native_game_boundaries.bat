@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64 -no_logo
cd /d "%~dp0.."
if not exist obj\native_boundaries mkdir obj\native_boundaries
set FLAGS=/nologo /O2 /W4 /std:c11 /D_CRT_SECURE_NO_WARNINGS /I. /Itests\ref6502 /Foobj\native_boundaries\
cl %FLAGS% tests\native_game_boundaries.c tests\ref6502\ref6502.c c012294.c mathbox.c mbprom.c progrom.c vecrom.c avg.c state.c vgut.c bzsoun.c coin65.c bzone_nmi.c bzone_math.c bzone_game.c bzone_disp.c bzone_robot.c bzone_enemy.c /Fe:tests\native_game_boundaries.exe || exit /b 1
tests\native_game_boundaries.exe
exit /b %ERRORLEVEL%
