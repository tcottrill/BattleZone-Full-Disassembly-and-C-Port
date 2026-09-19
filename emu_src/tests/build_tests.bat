@echo off
rem bz_emu: the two one-off checks of DESIGN.md "Verification" 1 and 2
rem (x64, VS2022 cl).  Console programs; they open no window.
rem
rem   obj\klaus_test.exe  Klaus Dormann's 6502 functional test on the VENDORED
rem                       core (emu_src\cpu).  The runner is the Klaus tree's
rem                       own test_6502.cpp, unmodified; it is copied to
rem                       obj\klaus\ first because #include "cpu_6502.h" looks
rem                       beside the including file before /I, and the Klaus
rem                       tree holds an older copy of the core.
rem                       Run:  obj\klaus_test.exe ^
rem                         C:\Source2026\6502_Klaus_tests_c6502\6502_65C02_functional_tests-master\bin_files\6502_functional_test.bin
rem   obj\romcheck.exe    emu_roms.c's images of roms\bzonea.zip against
rem                       the C port's generated progrom.c / vecrom.c /
rem                       mbprom.c (copies in tests\cport_ref\).
rem                       Run:  obj\romcheck.exe [ZIP]   (default ..\roms\bzonea.zip)
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64 -no_logo
cd /d "%~dp0.."

rem KLAUS: the tree holding test_6502.cpp and Klaus's bin_files (set it to override)
if not defined KLAUS set KLAUS=C:\Source2026\6502_Klaus_tests_c6502
if not exist "%KLAUS%\test_6502.cpp" (echo MISSING %KLAUS%\test_6502.cpp & exit /b 1)
if not exist obj mkdir obj
if not exist obj\klaus mkdir obj\klaus
if not exist obj\romcheck mkdir obj\romcheck

echo === obj\klaus_test.exe
copy /y "%KLAUS%\test_6502.cpp" obj\klaus\test_6502.cpp >nul || exit /b 1
cl /nologo /O2 /W3 /EHsc /std:c++17 /MD /D_CRT_SECURE_NO_WARNINGS /Icpu /Foobj\klaus\ ^
   obj\klaus\test_6502.cpp cpu\cpu_6502.cpp cpu\sys_log_shim.cpp ^
   /Fe:obj\klaus_test.exe || exit /b 1

echo === obj\romcheck.exe
cl /nologo /O2 /W3 /MD /D_CRT_SECURE_NO_WARNINGS /c platform\windows\miniz.c /Foobj\romcheck\ || exit /b 1
rem emu_roms.c with its four image symbols renamed, so that the C port's generated
rem arrays of the same names can be linked beside them
cl /nologo /O2 /W4 /std:c11 /MD /D_CRT_SECURE_NO_WARNINGS /DEMU_ROMS_NO_BACKEND ^
   /Dprogrom=emu_progrom /Dvecrom=emu_vecrom /Dmb_map=emu_mb_map /Dmb_ucode=emu_mb_ucode ^
   /I. /c emu_roms.c /Foobj\romcheck\ || exit /b 1
cl /nologo /O2 /W4 /std:c11 /MD /D_CRT_SECURE_NO_WARNINGS /I. /Foobj\romcheck\ ^
   tests\romcheck.c ^
   tests\cport_ref\progrom.c tests\cport_ref\vecrom.c tests\cport_ref\mbprom.c ^
   obj\romcheck\emu_roms.obj obj\romcheck\miniz.obj ^
   /Fe:obj\romcheck.exe || exit /b 1

echo TESTS BUILD OK
