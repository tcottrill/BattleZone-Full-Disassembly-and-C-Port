;Battlezone - the alternate $5000 program-ROM chip (MAME set 'bzone', Revision 2,
;036414-02.e1, CRC32 13de36d5), disassembled only where it differs from the source-
;derived image this disassembly reproduces (MAME set 'bzonea', Revision 1, 036414-01.e1,
;CRC32 efbc3fa0 - see bzone_program_rom.asm and HARDWARE_NOTES.md's 'two $5000 ROM
;images' section).  149 bytes differ overall: 1 (CHKS2) plus 148 inside CKSCOR's
;high-score display, split below into two runs of 93 and 55 bytes around one byte at
;$5505 that happens to be identical in both images (part of a 'STA (VGLIST),Y' whose
;other byte differs) - the fragment right after the gap continues the very next
;instruction, byte-adjacent in both images; nothing in between is omitted.  Everything
;outside these spans is byte-identical to bzone_program_rom.asm and is not repeated here.
;Conventions as bzone_program_rom.asm: Lxxxx: address labels, Atari's own names from
;build/symbols.json wherever an operand matches one, hex otherwise; [note] marks text
;written for this disassembly (nothing in the alternate ROM has its own source to quote -
;it is not part of the battlezone-main archive).  Each fragment below is re-encoded and
;compared with bzone_alt_cpu64k.bin by this script itself; see the run summary.

;==============================================================================
; $5340-$5340 (1 bytes differ from bzone_program_rom.asm)
;   CHKS2, the self-test EOR checksum byte for the $5000-$57FF program-ROM chip
;   (BZONE.MAC:802, 'CHKS2: .BYTE 70'; followed by '.ASCII /EDR/', 3 unrelated
;   filler bytes). STEST3's checksum loop (BZSTST.MAC:345-370) never reads CHKS2
;   by name - it just EORs every byte of the chip and expects $FF, so CHKS2 is
;   picked to make that true; it necessarily changes whenever anything else on
;   this chip changes, which is exactly what happened here (see the $54A8 span
;   below, on the same chip).
;==============================================================================
;==============================================================================
; $54A8-$5504 (93 bytes differ from bzone_program_rom.asm)
;   Inside CKSCOR (BZONE.MAC:805-1020, .SBTTL HIGH SCORE STUFF), the part of the
;   high-score table display that runs once per entry after DIGT2S has drawn its
;   score digits. The source version: STY SAVX (remembers which entry this is),
;   LDX #$1E; STX YDISP (sets the per-entry row spacing this entry's caller will
;   use next), JSR VGVTR, then (if HSCTBL+1,X is nonzero) JSR DSPALP twice to
;   draw two extra vector characters looked up from VGMSGA via HSCTBL+1,X and a
;   small table at $3590 - a marker next to entries with a nonzero high score
;   byte. The alternate version instead: LDX #$1A (a different opening value, 26
;   not 30) with no corresponding STX YDISP anywhere in this span, draws the
;   same two DSPALP characters but drops the TAX before the VGMSGA lookup (see
;   the [note] there), and ends the span with a completely different tail: it
;   sets YCOMP to a fixed $D8FF Y delta and XCOMP to one of two fixed deltas
;   depending on whether any characters were drawn for this entry (TYA/BEQ),
;   sets brightness 0 and JSR VGVTR2 - an invisible move-only vector, i.e. a
;   hand-picked row-advance offset - then rejoins the loop 2 bytes earlier
;   ($5434, not $54B2) than the source version does. Put together: this looks
;   like an earlier or alternate row-spacing/new-entry-marker scheme for the
;   same high-score screen, not a bug fix - CHKS2 above had to change only
;   because these 148 bytes did.
;==============================================================================
;==============================================================================
; $5506-$553C (55 bytes differ from bzone_program_rom.asm)
;   Continues directly on from the $54A8 span above (see its description) after
;   the one coincidentally-identical byte at $5505.
;==============================================================================

L5340:  .byte $67

L54A8:  LDA #$E0
L54AA:  LDX #$1A
L54AC:  JSR  VGVTR
L54AF:  LDX #$00
L54B1:  STX  SAVX
L54B4:  LDX  SAVX
L54B7:  LDA  HSCTBL,X
L54BA:  STA  TEMP2
L54BC:  ORA  $0301,X
L54BF:  BEQ  L553D
L54C1:  LDA  $0301,X
L54C4:  STA  $0B
L54C6:  LDA #$0A
L54C8:  JSR  DIGT2S
L54CB:  LDX #$1C
L54CD:  JSR  MSGS
L54D0:  LDX  SAVX
L54D3:  LDA  INITLS,X
L54D6:  JSR  DSPALP
L54D9:  LDX  SAVX
L54DC:  LDA  $031F,X
L54DF:  JSR  DSPALP
L54E2:  LDX  SAVX
L54E5:  LDA  $0320,X
L54E8:  JSR  DSPALP
L54EB:  LDY #$00
L54ED:  LDX  SAVX
L54F0:  LDA  $0301,X
L54F3:  BEQ  L550F
L54F5:  LDA  VGMSGA
L54F8:  STA  (VGLIST),Y
L54FA:  INY
L54FB:  LDA  $33F1
L54FE:  STA  (VGLIST),Y
L5500:  INY
L5501:  LDA  TSYMBL
L5504:  STA  (VGLIST),Y

L5506:  INY
L5507:  LDA  $3591
L550A:  STA  (VGLIST),Y
L550C:  JSR  VGADD
L550F:  LDX  SAVX
L5512:  CPX #$1B
L5514:  BCS  L553D
L5516:  INX
L5517:  INX
L5518:  INX
L5519:  STX  SAVX
L551C:  LDA #$D8
L551E:  STA  YCOMP
L5520:  LDA #$FF
L5522:  STA  $07
L5524:  LDA #$FE
L5526:  STA  $05
L5528:  TYA
L5529:  BEQ  L552F
L552B:  LDA #$A3
L552D:  BNE  L5531
L552F:  LDA #$F4
L5531:  STA  XCOMP
L5533:  LDA #$00
L5535:  STA  VGBRIT
L5537:  JSR  VGVTR2
L553A:  JMP  L54B4

;==============================================================================
; end of the alternate-image diff (149 bytes total, both spans)
;==============================================================================
