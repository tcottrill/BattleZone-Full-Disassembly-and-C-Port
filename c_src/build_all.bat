@echo off
rem Build the Battlezone C port (x64, VS2022 cl), modelled on the Tempest
rem port's build_all.bat: our code /W4 /std:c11, shared/harvested cores at /W3.
rem
rem Targets:
rem   tests\refrun.exe      reference harness (M0): the real ROM on the shared
rem                         ref6502 core, per-pass RAM / vector-RAM dumps,
rem                         --mbtest (MBDIAG.MAC's table against mathbox.c)
rem   tests\lockstep.exe    lockstep verifier (M1+): the real ROM vs the
rem                         translated C modules (vgut.c, bzsoun.c/coin65.c/
rem                         bzone_nmi.c, bzone_math.c so far), call by call -
rem                         see tests\lockstep.c, NOTES_m1.md, NOTES_integration.md
rem
rem Generated inputs (checked in; rerun after regenerating ..\disasm):
rem   python tools\gen_state.py     python tools\gen_roms.py
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64 -no_logo
cd /d "%~dp0"

rem the oracle's 6502 core: the copy in tests\ref6502 (from the shared
rem ref6502 project), so the tree builds on its own
set REF6502=tests\ref6502
if not exist "%REF6502%\ref6502.c" (echo MISSING %REF6502%\ref6502.c & exit /b 1)
for %%f in (state_defs.h progrom.c vecrom.c mbprom.c) do if not exist %%f (echo MISSING %%f - run the generators & exit /b 1)
if not exist obj mkdir obj
if not exist tests\ref mkdir tests\ref

rem ---- shared / harvested chip cores (verbatim; their own warning level) ----
cl /nologo /O2 /W3 /std:c11 /c "%REF6502%\ref6502.c" /Foobj\ || exit /b 1
cl /nologo /O2 /W3 /std:c11 /D_CRT_SECURE_NO_WARNINGS /I. /c c012294.c /Foobj\ || exit /b 1

rem avg.c is in HW (not just harvested chip cores) because both refrun.c
rem (PLAN.md M1 item 3: avg_walk() for AVG draw-time) and lockstep.c link it.
set HW=mathbox.c mbprom.c progrom.c vecrom.c avg.c
set FLAGS=/nologo /O2 /W4 /std:c11 /D_CRT_SECURE_NO_WARNINGS /I.

echo === tests\refrun.exe
cl %FLAGS% /Foobj\ /I"%REF6502%" tests\refrun.c %HW% obj\ref6502.obj obj\c012294.obj /Fe:tests\refrun.exe || exit /b 1

rem MODULES: translated modules linked into lockstep.exe as their own module
rem hands over (NOTES_<module>.md's HANDOVER line) - see NOTES_integration.md
rem for the current roster. Every routine of BZONE.MAC/BZSOUN/COIN65/VGUT is
rem here now - only BZSTST remains (planned last).
set MODULES=vgut.c bzsoun.c coin65.c bzone_nmi.c bzone_math.c bzone_game.c bzone_disp.c bzone_robot.c bzone_enemy.c

echo === tests\lockstep.exe
cl %FLAGS% /Foobj\ /I"%REF6502%" tests\lockstep.c %HW% state.c %MODULES% obj\ref6502.obj obj\c012294.obj /Fe:tests\lockstep.exe || exit /b 1

rem M7: the playable build's core (app_loop.c + bzstst.c's PWRON) on a
rem headless test backend - see tests\bz_smoke.c.  bz_win.exe itself is
rem build_win.bat.
echo === tests\bz_smoke.exe
cl %FLAGS% /Foobj\ tests\bz_smoke.c app_loop.c bzstst.c bz_sounds.c %HW% state.c %MODULES% obj\c012294.obj /Fe:tests\bz_smoke.exe || exit /b 1

echo ALL BUILDS OK
