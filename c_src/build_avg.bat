@echo off
rem Build the Battlezone AVG walker test tool (M0v, Gate V), VS2022 cl /W4 /std:c11.
rem Modelled on C:\Source2026\Tempest Dissasembly\c_src\build_avg.bat.  Kept apart
rem from build_all.bat (owned by the scaffold milestone) - avg.c/avgshapes.c have
rem no dependency on state.h/progrom.c/vecrom.c, so this stands alone.
rem
rem   tests\avgshapes.exe   walks every ROM picture/character, writes tests\avg_out\avg_shapes.txt
rem   tests\avgtime.exe     cycle-true draw time (pictures / synth / frames modes)
rem
rem Gate V (segments vs ../disasm/vrender.py) is tools\avg_gate_v.py; Gate T
rem (cycles vs the real AVG state PROM) is tools\avg_prom_sim.py.  Both
rem generate tests\avg_shapes_index.txt, run the .exe above, and compare.
rem   python tools\avg_gate_v.py
rem   python tools\avg_prom_sim.py
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64 -no_logo
cd /d "%~dp0"

if not exist obj\avg mkdir obj\avg
if not exist tests\avg_out mkdir tests\avg_out

set FLAGS=/nologo /O2 /W4 /std:c11 /D_CRT_SECURE_NO_WARNINGS /I.

echo === tests\avgshapes.exe
cl %FLAGS% /Foobj\avg\ avg.c tests\avgshapes.c /Fe:tests\avgshapes.exe || exit /b 1

echo === tests\avgtime.exe
cl %FLAGS% /Foobj\avg\ avg.c tests\avgtime.c /Fe:tests\avgtime.exe || exit /b 1

echo AVG BUILD OK
