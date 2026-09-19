@echo off
rem Build bz_emu.exe: the real Battlezone ROM on AAE's cpu_6502 core behind
rem the Battlezone Windows backend (x64, VS2022 cl).  Self-contained: every
rem source file is in emu_src\ (the chip models, the AVG walker, the sounds,
rem the state and platform\ were copied from the C port, ..\c_src, on
rem 2026-09-18 and now belong to this project).
rem
rem Targets:
rem   bz_emu.exe                (emu_src\, next to bz_win.ini / bz_win.log -
rem                             the backend's fixed file names):
rem                             emu_main.cpp (the seam) + emu_roms.c (ROMs
rem                             from a MAME zip) + cpu\ (the vendored core)
rem                             + state.c avg.c mathbox.c c012294.c
rem                             bz_sounds.c + platform\windows\*
rem   tests\bz_emu_smoke.exe    the same seam on a headless backend, driven by
rem                             tests\bz_smoke.c (the C port's smoke driver)
rem   No generated progrom.c / vecrom.c / mbprom.c: emu_roms.c defines those
rem   symbols and fills them from the zip.
rem
rem Objects go to obj\ (tests\build_tests.bat uses obj\klaus\, obj\romcheck\).
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64 -no_logo
cd /d "%~dp0"

set WIN=platform\windows
for %%f in (state.c state_defs.h avg.c mathbox.c c012294.c bz_sounds.c %WIN%\plat_win.c tests\bz_smoke.c cpu\cpu_6502.cpp) do if not exist %%f (echo MISSING %%f & exit /b 1)
if not exist obj mkdir obj
if not exist obj\hl mkdir obj\hl

rem ---- vendored framework + chip model (their own warning level) ----------
cl /nologo /O2 /W3 /MD /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE /c ^
   %WIN%\sys_gl.c %WIN%\glew.c ^
   %WIN%\log.c %WIN%\vector_draw.c ^
   %WIN%\mat4.c %WIN%\rawinput.c ^
   %WIN%\mixer.c %WIN%\fileio.c ^
   %WIN%\miniz.c %WIN%\ini.c ^
   %WIN%\joystick.c ^
   /Foobj\ || exit /b 1
cl /nologo /O2 /W3 /std:c11 /MD /D_CRT_SECURE_NO_WARNINGS /I. /c c012294.c /Foobj\ || exit /b 1

rem ---- the vendored 6502 core + its log shim -------------------------------
cl /nologo /O2 /W3 /EHsc /std:c++17 /MD /D_CRT_SECURE_NO_WARNINGS /DEMU_LOG_TO_BACKEND /I. /c ^
   cpu\cpu_6502.cpp cpu\sys_log_shim.cpp /Foobj\ || exit /b 1
cl /nologo /O2 /W3 /EHsc /std:c++17 /MD /D_CRT_SECURE_NO_WARNINGS /I. /c ^
   cpu\sys_log_shim.cpp /Foobj\hl\ || exit /b 1

rem ---- machine files, the backend, the ROM loader (/W4) --------------------
cl /nologo /O2 /W4 /std:c11 /MD /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE /I. /c ^
   state.c avg.c mathbox.c bz_sounds.c ^
   %WIN%\plat_win.c ^
   emu_roms.c ^
   /Foobj\ || exit /b 1

rem ---- the seam, and the link ---------------------------------------------
echo === bz_emu.exe
cl /nologo /O2 /W4 /EHsc /std:c++17 /MD /D_CRT_SECURE_NO_WARNINGS /I. /Foobj\ ^
   emu_main.cpp ^
   obj\emu_roms.obj obj\cpu_6502.obj obj\sys_log_shim.obj ^
   obj\state.obj obj\avg.obj obj\mathbox.obj obj\bz_sounds.obj ^
   obj\plat_win.obj obj\c012294.obj ^
   obj\sys_gl.obj obj\glew.obj obj\log.obj ^
   obj\vector_draw.obj obj\mat4.obj obj\rawinput.obj ^
   obj\mixer.obj obj\fileio.obj obj\miniz.obj ^
   obj\ini.obj obj\joystick.obj ^
   /Fe:bz_emu.exe ^
   /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib winmm.lib ole32.lib || exit /b 1

rem ---- the headless smoke test: the same seam, bz_smoke.c's driver ----------
echo === tests\bz_emu_smoke.exe
cl /nologo /O2 /W4 /std:c11 /MD /D_CRT_SECURE_NO_WARNINGS /DEMU_HEADLESS /I. /c emu_roms.c /Foobj\hl\ || exit /b 1
cl /nologo /O2 /W4 /EHsc /std:c++17 /MD /D_CRT_SECURE_NO_WARNINGS /DEMU_HEADLESS /I. /c emu_main.cpp /Foobj\hl\ || exit /b 1
cl /nologo /O2 /W3 /std:c11 /MD /D_CRT_SECURE_NO_WARNINGS /I. /Foobj\hl\ ^
   tests\bz_smoke.c ^
   obj\hl\emu_main.obj obj\hl\emu_roms.obj obj\hl\sys_log_shim.obj obj\cpu_6502.obj ^
   obj\state.obj obj\avg.obj obj\mathbox.obj obj\bz_sounds.obj obj\c012294.obj obj\miniz.obj ^
   /Fe:tests\bz_emu_smoke.exe || exit /b 1

echo EMU BUILD OK
