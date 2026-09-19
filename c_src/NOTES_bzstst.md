# NOTES_bzstst.md - BZSTST.MAC self test (M8)

## Routine status
- `pwron()` - done (earlier session): PWRON's game path + dispatch.
- `selftest_boot()` - RAMTST..STEST3 ($7BCB-$7D3A). RAMTST/STEST1 (moving-
  inversion tests) always pass here (bzstst.h) - bad-memory legs unreachable,
  not translated; only the "good" exit's RAM/VRAM side effects are modelled.
  STEST3's ROM checksums are computed for real via cpu_rd().
- `selftest_pass()` = mbdiag_test()+stest4()+stest7(), static helpers in ROM
  order (mbdiag_test, stest6, stest4, diagdb, stest7). One call = one
  MBDIAG/STEST4/(STEST6)/STEST7 trip (bzstst.h's contract) - every ROM "JMP
  MBDIAG" re-entry and the diag-mode wait loop is "return 0, call me again",
  never an internal C loop (input can only be repolled between calls).

## Judgment calls
- STEST7's `STA GOADD`: real A there is STEST4/STEST6's leftover append
  value ($20 HATCH path, $40 = DIATST's first byte on the diag-step path).
  Since BZSTST always targets VGLIST (= VECRAM), this port passes
  `VGLIST_AT(1)` to hw_goadd() instead of that incidental value.
- STEST1/STEST2 bad-RAM legs, RAMTST's lockup, DIATST's self-check JMPL/
  JSRL/RTSL entries: unreachable given always-good RAM, not translated.

## Seam (app_loop.c)
- `in_selftest` (static), set from pwron()'s return; selftest_pass()
  replaces main_pass() while set. hw_in0() services any due NMI when
  in_selftest (BZSTST's busy-waits have no frame-wait of their own, unlike
  hw_wait_frame()). `machine_wait_cyc()` paces real time to mach_cyc after
  each selftest_pass() call (machine_tick()'s clock anchor, no NMI grid).

## Keys (plat_win.c)
- F2: self-test switch, latching toggle (WM_KEYDOWN edge, no auto-repeat).
  `[main] test_switch` ini key / `--test` flag set it at startup; logged and
  shown in the title. F1: diag step (held). F3: slam (held, was unmapped).
- Headless backends: `diag` defaults 0 (zero-init plat_inputs); bz_smoke.c
  gained `--test`.

## Open items
- Diagnostic-step mode translated but only smoke-tested via the plain path.
- BZSTST not registered with tests\lockstep.c (out of scope).
