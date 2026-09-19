;Battlezone (Atari, 1980) - vector ROM $3000-$3FFF, byte-exact listing.
;
;Source: BZMTNS.MAC's own AVG display lists and 3D/lookup tables, with
;VGMC.MAC's vector macros (.INCLUDEd; VGMC.MAC's own header: 'PROGRAMMER:
;ED LOGG') and VGAN.MAC's alphanumeric character set (.INCLUDEd partway
;through; found via macsrc.py's ALT_DIRS in ../red-baron-main; VGAN.MAC's
;own header also says 'PROGRAMMER: ED LOGG'). BZMTNS.MAC's own header
;carries no programmer credit, so the display lists/tables below are shown
;unattributed beyond what BZMTNS.MAC itself states. The vector ROM holds
;three kinds of thing:
;  1. AVG display lists: mountains/horizon (HORIZN, MTNS/MTN0-7), the
;     tank-view crosshair (XCROSS, XCROS1), radar ring (RDRING), tank radar
;     symbol (TANKS/TSYMBL), screen-crack pieces (CRACK0-7/CRACKS), the
;     explosion picture (EXPIC), the self-test/diagnostic screen (HATCH)
;     and the full VGAN alphanumeric character set (CHAR.*).
;  2. 3D object point tables (label: .BYTE offset-to-next-table, then one
;     .NWORD/.MWORD X,Y,Z per point - see build/vector_tables.json) and the
;     OBJPNT pointer array indexing them by object #.
;  3. A few small lookup tables read directly by the 6502 (ARCTAN sine/cos
;     helper, BONTBL/MISLVL difficulty tables, PTBLO1/LOGOBJ).
;No 6502 code executes out of this range: every byte here is either read by
;the AVG chip as an instruction stream, or read by the 6502 as plain table
;data (DRAW/PNTPUT in BZONE.MAC) - vromsrc.py requires every statement's
;mnemonic to be an AVG op or .byte/.word/.NWORD/.MWORD, with no exceptions.
;
;AVG opcodes (see avg.py, derived from VGMC.MAC): VCTR dx,dy,z = long
;vector (4 bytes); SVEC = short vector (2 bytes, VGMC's VCTR macro picks it
;automatically for even |dx|,|dy| <= 30) - both decoded straight from the
;ROM bits, independent of which macro name (VCTR/GVCTR) the source used.
;z 0-7 = beam intensity (0 = blank/move only), straight from VGMC.MAC's own
;comment - Battlezone's AVG is monochrome, no colour RAM. STAT z[,hi,in] =
;$6xxx: z loads the intensity latch; with hi/in given it also sets up a
;clip-window corner (hi = upper-right vs lower-left, in = inside/outside);
;bare 'STAT z' sets the 'not a window' bit ($0400). SCAL b,$ll = binary /
;linear scale. CNTR/RTSL/HALT take no operand (VGMC's macros supply none,
;so the ROM only ever holds their fixed word). JSRL/JMPL operand = the
;target label ((target-$2000)/2 is what the word actually encodes; VG
;address space $2000-$3FFF covers both vector RAM and this ROM).
;
;Labels are Atari's names, mangled '.' -> '_', '$' -> 'S_', local 'NN$' ->
;'SCOPE_NN'. Two Atari names occasionally share one address (an 'end of the
;previous table' bare marker immediately followed by the next table's own
;label, or a later 'CHAR.0 = CHAR.O' alias); the listing shows both but
;names tables/targets after the one build/srclines.json recorded as that
;statement's own label.
;
;Verify with: python disasm/verify_vrom.py
;Re-generate with: python disasm/emit_vrom.py

.org $3000

;----------------------------[ AVG display lists / data tables ]----------------------------

HORIZN:           ;Horizon line (single long VCTR)
VGROM:            ;same address
V3000:  VCTR   -1536, 0, 3            ;0000 7A00
V3004:  RTSL                          ;C000

MTNS:             ;Mountain range picture - calls all 8 MTN0-MTN7 skyline segments
V3006:  JSRL   MTN0                   ;A80B
V3008:  JSRL   MTN1                   ;A865
V300A:  JSRL   MTN2                   ;A882
V300C:  JSRL   MTN3                   ;A8C1
V300E:  JSRL   MTN4                   ;A8E2
V3010:  JSRL   MTN5                   ;A8F9
V3012:  JSRL   MTN6                   ;A917
V3014:  JSRL   MTN7                   ;A934

MTN0:             ;Mountain/skyline segment 0 (includes the moon); 1 ref
V3016:  VCTR   0, 64, 0               ;0040 0000
V301A:  VCTR   32, -32, 3             ;1FE0 6020
V301E:  SVEC   -16, -8, 0             ;5C18
V3020:  VCTR   80, 40, 3              ;0028 6050
V3024:  VCTR   32, 0, 3               ;0000 6020
V3028:  VCTR   32, -32, 3             ;1FE0 6020
V302C:  VCTR   -64, 32, 3             ;0020 7FC0
V3030:  VCTR   0, -64, 0              ;1FC0 0000
V3034:  VCTR   128, 64, 3             ;0040 6080
V3038:  VCTR   64, -64, 3             ;1FC0 6040
V303C:  VCTR   32, 0, 0               ;0000 0020
V3040:  VCTR   -64, 32, 3             ;0020 7FC0
V3044:  VCTR   -32, 32, 0             ;0020 1FE0
V3048:  VCTR   64, -32, 3             ;1FE0 6040
V304C:  VCTR   64, -16, 3             ;1FF0 6040
V3050:  VCTR   96, -16, 3             ;1FF0 6060
V3054:  VCTR   48, 160, 0             ;00A0 0030
V3058:  VCTR   5, -12, 7              ;1FF4 E005  MOON
V305C:  SVEC   0, -12, 7              ;5AE0
V305E:  SVEC   -8, -12, 7             ;5AFC
V3060:  SVEC   -12, -12, 7            ;5AFA
V3062:  VCTR   -12, -3, 7             ;1FFD FFF4
V3066:  VCTR   -12, 3, 7              ;0003 FFF4
V306A:  VCTR   12, -9, 7              ;1FF7 E00C
V306E:  VCTR   12, -3, 7              ;1FFD E00C
V3072:  VCTR   12, 3, 7               ;0003 E00C
V3076:  VCTR   12, 9, 7               ;0009 E00C
V307A:  SVEC   6, 12, 7               ;46E3
V307C:  SVEC   0, 12, 7               ;46E0
V307E:  SVEC   -4, 12, 7              ;46FE
V3080:  VCTR   -11, 12, 7             ;000C FFF5  END OF MOON PART A
V3084:  VCTR   -13, 3, 2              ;0003 5FF3
V3088:  VCTR   -15, -4, 2             ;1FFC 5FF1
V308C:  SVEC   -10, -8, 2             ;5C5B
V308E:  VCTR   -5, -11, 2             ;1FF5 5FFB
V3092:  VCTR   -2, -13, 2             ;1FF3 5FFE
V3096:  VCTR   6, -15, 2              ;1FF1 4006  END OF MOON PART B
V309A:  VCTR   27, 3, 0               ;0003 001B
V309E:  SVEC   0, -4, 5               ;5EA0
V30A0:  VCTR   6, -1, 5               ;1FFF A006
V30A4:  VCTR   6, 11, 5               ;000B A006
V30A8:  VCTR   -4, -1, 5              ;1FFF BFFC
V30AC:  VCTR   -1, 2, 5               ;0002 BFFF
V30B0:  SVEC   18, 20, 0              ;4A09
V30B2:  VCTR   -3, -3, 5              ;1FFD BFFD
V30B6:  VCTR   1, 3, 5                ;0003 A001
V30BA:  VCTR   -3, 1, 5               ;0001 BFFD
V30BE:  SVEC   0, 6, 5                ;43A0
V30C0:  VCTR   1, 1, 2                ;0001 4001  END OF MOON
V30C4:  VCTR   7, -150, 0             ;1F6A 0007
V30C8:  RTSL                          ;C000

MTN1:             ;Mountain/skyline segment 1; 1 ref
V30CA:  VCTR   32, 0, 0               ;0000 0020
V30CE:  VCTR   64, 48, 3              ;0030 6040
V30D2:  VCTR   32, -48, 3             ;1FD0 6020
V30D6:  VCTR   -32, 48, 0             ;0030 1FE0
V30DA:  VCTR   32, -16, 3             ;1FF0 6020
V30DE:  VCTR   64, 32, 3              ;0020 6040
V30E2:  VCTR   160, -64, 3            ;1FC0 60A0
V30E6:  VCTR   -128, 32, 3            ;0020 7F80
V30EA:  VCTR   -32, 32, 3             ;0020 7FE0
V30EE:  VCTR   -64, -32, 0            ;1FE0 1FC0
V30F2:  VCTR   96, -32, 3             ;1FE0 6060
V30F6:  VCTR   128, 0, 0              ;0000 0080
V30FA:  VCTR   160, 32, 3             ;0020 60A0
V30FE:  VCTR   0, -32, 0              ;1FE0 0000
V3102:  RTSL                          ;C000

MTN2:             ;Mountain/skyline segment 2; 1 ref
V3104:  VCTR   0, 32, 0               ;0020 0000
V3108:  VCTR   64, 0, 3               ;0000 6040
V310C:  VCTR   0, -32, 0              ;1FE0 0000
V3110:  VCTR   -64, 32, 3             ;0020 7FC0
V3114:  VCTR   64, 0, 0               ;0000 0040
V3118:  VCTR   64, 32, 3              ;0020 6040
V311C:  VCTR   -64, -32, 0            ;1FE0 1FC0
V3120:  VCTR   96, -32, 3             ;1FE0 6060
V3124:  VCTR   32, 32, 3              ;0020 6020
V3128:  VCTR   -64, 32, 3             ;0020 7FC0
V312C:  VCTR   32, 0, 3               ;0000 6020
V3130:  VCTR   32, -32, 3             ;1FE0 6020
V3134:  VCTR   32, 16, 3              ;0010 6020
V3138:  VCTR   32, -16, 3             ;1FF0 6020
V313C:  VCTR   -96, -32, 3            ;1FE0 7FA0
V3140:  VCTR   96, 32, 0              ;0020 0060
V3144:  VCTR   64, 32, 3              ;0020 6040
V3148:  VCTR   64, -32, 3             ;1FE0 6040
V314C:  VCTR   -96, 16, 0             ;0010 1FA0
V3150:  VCTR   32, -16, 3             ;1FF0 6020
V3154:  VCTR   32, 16, 3              ;0010 6020
V3158:  VCTR   0, -48, 0              ;1FD0 0000
V315C:  VCTR   96, 8, 3               ;0008 6060
V3160:  VCTR   -64, 24, 3             ;0018 7FC0
V3164:  VCTR   32, 16, 3              ;0010 6020
V3168:  VCTR   64, -16, 3             ;1FF0 6040
V316C:  VCTR   -32, -24, 3            ;1FE8 7FE0
V3170:  VCTR   -32, 40, 3             ;0028 7FE0
V3174:  VCTR   64, -16, 0             ;1FF0 0040
V3178:  VCTR   32, 0, 3               ;0000 6020
V317C:  VCTR   0, -32, 0              ;1FE0 0000
V3180:  RTSL                          ;C000

MTN3:             ;Mountain/skyline segment 3; 1 ref
V3182:  VCTR   0, 32, 0               ;0020 0000
V3186:  VCTR   64, -32, 3             ;1FE0 6040
V318A:  VCTR   -64, 32, 0             ;0020 1FC0
V318E:  VCTR   128, -32, 3            ;1FE0 6080
V3192:  VCTR   160, 0, 0              ;0000 00A0
V3196:  VCTR   96, 32, 3              ;0020 6060
V319A:  VCTR   32, -32, 3             ;1FE0 6020
V319E:  VCTR   -32, 32, 0             ;0020 1FE0
V31A2:  VCTR   32, 32, 3              ;0020 6020
V31A6:  VCTR   32, -64, 3             ;1FC0 6020
V31AA:  VCTR   32, 0, 0               ;0000 0020
V31AE:  VCTR   -64, 64, 3             ;0040 7FC0
V31B2:  VCTR   32, -32, 0             ;1FE0 0020
V31B6:  VCTR   32, 32, 3              ;0020 6020
V31BA:  VCTR   32, -32, 3             ;1FE0 6020
V31BE:  VCTR   0, -32, 0              ;1FE0 0000
V31C2:  RTSL                          ;C000

MTN4:             ;Mountain/skyline segment 4; 1 ref
V31C4:  VCTR   0, 32, 0               ;0020 0000
V31C8:  VCTR   64, 32, 3              ;0020 6040
V31CC:  VCTR   32, -64, 3             ;1FC0 6020
V31D0:  VCTR   32, 0, 0               ;0000 0020
V31D4:  VCTR   -64, 64, 3             ;0040 7FC0
V31D8:  VCTR   32, -32, 0             ;1FE0 0020
V31DC:  VCTR   32, 16, 3              ;0010 6020
V31E0:  VCTR   96, -48, 3             ;1FD0 6060
V31E4:  VCTR   128, 0, 0              ;0000 0080
V31E8:  VCTR   160, 32, 3             ;0020 60A0
V31EC:  VCTR   0, -32, 0              ;1FE0 0000
V31F0:  RTSL                          ;C000

MTN5:             ;Mountain/skyline segment 5; 1 ref
V31F2:  VCTR   0, 32, 0               ;0020 0000
V31F6:  VCTR   64, 0, 3               ;0000 6040
V31FA:  VCTR   224, -32, 3            ;1FE0 60E0
V31FE:  VCTR   -32, 48, 3             ;0030 7FE0
V3202:  VCTR   -64, -32, 3            ;1FE0 7FC0
V3206:  VCTR   96, -16, 0             ;1FF0 0060
V320A:  VCTR   96, 64, 3              ;0040 6060
V320E:  VCTR   64, -40, 3             ;1FD8 6040
V3212:  SVEC   -16, -24, 0            ;5418
V3214:  VCTR   64, 96, 3              ;0060 6040
V3218:  VCTR   3, -7, 3               ;1FF9 6003
V321C:  VCTR   5, 5, 3                ;0005 6005
V3220:  VCTR   3, -6, 3               ;1FFA 6003
V3224:  VCTR   5, 8, 3                ;0008 6005
V3228:  VCTR   0, -96, 0              ;1FA0 0000
V322C:  RTSL                          ;C000

MTN6:             ;Mountain/skyline segment 6; 1 ref
V322E:  VCTR   0, 96, 0               ;0060 0000
V3232:  VCTR   64, -96, 3             ;1FA0 6040
V3236:  VCTR   96, 64, 3              ;0040 6060
V323A:  VCTR   64, -64, 3             ;1FC0 6040
V323E:  VCTR   64, 0, 0               ;0000 0040
V3242:  VCTR   -128, 64, 3            ;0040 7F80
V3246:  VCTR   64, -32, 0             ;1FE0 0040
V324A:  VCTR   64, 32, 3              ;0020 6040
V324E:  VCTR   128, -64, 3            ;1FC0 6080
V3252:  VCTR   -64, 32, 0             ;0020 1FC0
V3256:  VCTR   64, 16, 3              ;0010 6040
V325A:  VCTR   32, -48, 3             ;1FD0 6020
V325E:  VCTR   -32, 48, 0             ;0030 1FE0
V3262:  VCTR   96, -48, 3             ;1FD0 6060
V3266:  RTSL                          ;C000

MTN7:             ;Mountain/skyline segment 7; 1 ref
V3268:  VCTR   192, 0, 0              ;0000 00C0
V326C:  VCTR   224, 32, 3             ;0020 60E0
V3270:  VCTR   64, -32, 3             ;1FE0 6040
V3274:  VCTR   -32, 16, 0             ;0010 1FE0
V3278:  VCTR   64, 48, 3              ;0030 6040
V327C:  VCTR   0, -64, 0              ;1FC0 0000
V3280:  RTSL                          ;C000

CHAR_A:           ;1 ref
V3282:  SVEC   0, 16, 6               ;48C0
V3284:  SVEC   8, 8, 6                ;44C4
V3286:  SVEC   8, -8, 6               ;5CC4
V3288:  SVEC   0, -16, 6              ;58C0
V328A:  SVEC   -16, 8, 0              ;4418
V328C:  SVEC   16, 0, 6               ;40C8
V328E:  SVEC   8, -8, 0               ;5C04
V3290:  RTSL                          ;C000

CHAR_B:           ;1 ref
V3292:  SVEC   0, 24, 6               ;4CC0
V3294:  SVEC   12, 0, 6               ;40C6
V3296:  SVEC   4, -4, 6               ;5EC2  THESE VECTORS ARE BRIGHTER THAN THE OTHERS
V3298:  SVEC   0, -4, 6               ;5EC0
V329A:  SVEC   -4, -4, 6              ;5EDE
V329C:  SVEC   -12, 0, 6              ;40DA
V329E:  SVEC   12, 0, 0               ;4006
V32A0:  SVEC   4, -4, 6               ;5EC2
V32A2:  SVEC   0, -4, 6               ;5EC0
V32A4:  SVEC   -4, -4, 6              ;5EDE
V32A6:  SVEC   -12, 0, 6              ;40DA
V32A8:  JMPL   CHAR_                  ;E993

CHAR_C:           ;1 ref
V32AA:  SVEC   0, 24, 6               ;4CC0
V32AC:  SVEC   16, 0, 6               ;40C8
V32AE:  SVEC   -16, -24, 0            ;5418
V32B0:  JMPL   UNDERL                 ;E9D6

CHAR_D:           ;1 ref
V32B2:  SVEC   0, 24, 6               ;4CC0
V32B4:  SVEC   8, 0, 6                ;40C4
V32B6:  SVEC   8, -8, 6               ;5CC4
V32B8:  SVEC   0, -8, 6               ;5CC0
V32BA:  SVEC   -8, -8, 6              ;5CDC
V32BC:  SVEC   -8, 0, 6               ;40DC
V32BE:  JMPL   CHAR_                  ;E993

CHAR_E:           ;1 ref
V32C0:  SVEC   16, 0, 6               ;40C8
V32C2:  SVEC   -16, 0, 0              ;4018

CHAR_F:           ;1 ref
V32C4:  SVEC   0, 24, 6               ;4CC0
V32C6:  SVEC   16, 0, 6               ;40C8
V32C8:  SVEC   -4, -12, 0             ;5A1E
V32CA:  SVEC   -12, 0, 6              ;40DA

CHARF1:           ;1 ref
V32CC:  SVEC   24, -12, 0             ;5A0C
V32CE:  RTSL                          ;C000

CHAR_G:           ;1 ref
V32D0:  SVEC   0, 24, 6               ;4CC0
V32D2:  SVEC   16, 0, 6               ;40C8
V32D4:  SVEC   0, -8, 6               ;5CC0
V32D6:  SVEC   -8, -8, 0              ;5C1C
V32D8:  SVEC   8, 0, 6                ;40C4
V32DA:  SVEC   0, -8, 6               ;5CC0
V32DC:  JMPL   CHAR01                 ;E992

CHAR_H:           ;1 ref
V32DE:  SVEC   0, 24, 6               ;4CC0
V32E0:  SVEC   0, -12, 0              ;5A00
V32E2:  SVEC   16, 0, 6               ;40C8
V32E4:  SVEC   0, 12, 0               ;4600
V32E6:  JMPL   CHAR72                 ;E9EF

CHAR_I:           ;1 ref
V32E8:  SVEC   16, 0, 6               ;40C8
V32EA:  SVEC   -16, 24, 0             ;4C18

CHARI1:           ;1 ref
V32EC:  SVEC   16, 0, 6               ;40C8
V32EE:  SVEC   -8, 0, 0               ;401C

CHARI2:           ;1 ref
V32F0:  SVEC   0, -24, 6              ;54C0
V32F2:  SVEC   16, 0, 0               ;4008
V32F4:  RTSL                          ;C000

CHAR_J:           ;1 ref
V32F6:  SVEC   0, 8, 0                ;4400
V32F8:  SVEC   8, -8, 6               ;5CC4
V32FA:  SVEC   8, 0, 6                ;40C4
V32FC:  JMPL   CHARU1                 ;E9B4

CHAR_K:           ;1 ref
V32FE:  SVEC   0, 24, 6               ;4CC0
V3300:  SVEC   12, 0, 0               ;4006
V3302:  SVEC   -12, -12, 6            ;5ADA
V3304:  SVEC   12, -12, 6             ;5AC6
V3306:  SVEC   12, 0, 0               ;4006
V3308:  RTSL                          ;C000

CHAR_L:           ;1 ref
V330A:  SVEC   0, 24, 0               ;4C00
V330C:  SVEC   0, -24, 6              ;54C0
V330E:  JMPL   UNDERL                 ;E9D6

CHAR_M:           ;1 ref
V3310:  SVEC   0, 24, 6               ;4CC0
V3312:  SVEC   8, -8, 6               ;5CC4
V3314:  SVEC   8, 8, 6                ;44C4
V3316:  JMPL   CHAR72                 ;E9EF

CHAR_N:           ;1 ref
V3318:  SVEC   0, 24, 6               ;4CC0
V331A:  SVEC   16, -24, 6             ;54C8
V331C:  JMPL   CHARU1                 ;E9B4

CHAR_O:           ;2 refs
CHAR_0:           ;2 refs
V331E:  SVEC   0, 24, 6               ;4CC0
V3320:  SVEC   16, 0, 6               ;40C8
V3322:  SVEC   0, -24, 6              ;54C0

CHAR01:           ;1 ref
V3324:  SVEC   -16, 0, 6              ;40D8

CHAR_:            ;4 refs
V3326:  SVEC   24, 0, 0               ;400C  BLANK
V3328:  RTSL                          ;C000

CHAR_P:           ;1 ref
V332A:  SVEC   0, 24, 6               ;4CC0
V332C:  SVEC   16, 0, 6               ;40C8
V332E:  SVEC   0, -12, 6              ;5AC0
V3330:  SVEC   -16, 0, 6              ;40D8
V3332:  JMPL   CHARF1                 ;E966

CHAR_Q:           ;1 ref
V3334:  SVEC   0, 24, 6               ;4CC0
V3336:  SVEC   16, 0, 6               ;40C8
V3338:  SVEC   0, -16, 6              ;58C0
V333A:  SVEC   -8, -8, 6              ;5CDC
V333C:  SVEC   -8, 0, 6               ;40DC
V333E:  SVEC   8, 8, 0                ;4404
V3340:  SVEC   8, -8, 6               ;5CC4
V3342:  JMPL   CHAR21                 ;E9D7

CHAR_R:           ;1 ref
V3344:  SVEC   0, 24, 6               ;4CC0
V3346:  SVEC   16, 0, 6               ;40C8
V3348:  SVEC   0, -12, 6              ;5AC0
V334A:  SVEC   -16, 0, 6              ;40D8
V334C:  SVEC   4, 0, 0                ;4002
V334E:  SVEC   12, -12, 6             ;5AC6
V3350:  JMPL   CHAR21                 ;E9D7

CHAR_S:           ;2 refs
CHAR_5:           ;2 refs
V3352:  SVEC   16, 0, 6               ;40C8
V3354:  SVEC   0, 12, 6               ;46C0
V3356:  SVEC   -16, 0, 6              ;40D8
V3358:  SVEC   0, 12, 6               ;46C0
V335A:  SVEC   16, 0, 6               ;40C8
V335C:  JMPL   CHARU2                 ;E9B5

CHAR_T:           ;1 ref
V335E:  SVEC   0, 24, 0               ;4C00
V3360:  JMPL   CHARI1                 ;E976

CHAR_U:           ;1 ref
V3362:  SVEC   0, 24, 0               ;4C00
V3364:  SVEC   0, -24, 6              ;54C0
V3366:  SVEC   16, 0, 6               ;40C8

CHARU1:           ;3 refs
V3368:  SVEC   0, 24, 6               ;4CC0

CHARU2:           ;2 refs
V336A:  SVEC   8, -24, 0              ;5404
V336C:  RTSL                          ;C000

CHAR_V:           ;1 ref
V336E:  SVEC   0, 24, 0               ;4C00
V3370:  SVEC   8, -24, 6              ;54C4
V3372:  SVEC   8, 24, 6               ;4CC4
V3374:  JMPL   CHARU2                 ;E9B5

CHAR_W:           ;1 ref
V3376:  SVEC   0, 24, 0               ;4C00
V3378:  SVEC   0, -24, 6              ;54C0
V337A:  SVEC   8, 8, 6                ;44C4
V337C:  SVEC   8, -8, 6               ;5CC4
V337E:  JMPL   CHARU1                 ;E9B4

CHAR_X:           ;1 ref
V3380:  SVEC   16, 24, 6              ;4CC8
V3382:  SVEC   -16, 0, 0              ;4018
V3384:  SVEC   16, -24, 6             ;54C8
V3386:  JMPL   CHAR73                 ;E9F0

CHAR_Y:           ;1 ref
V3388:  SVEC   8, 0, 0                ;4004
V338A:  SVEC   0, 16, 6               ;48C0
V338C:  SVEC   -8, 8, 6               ;44DC
V338E:  SVEC   16, 0, 0               ;4008
V3390:  SVEC   -8, -8, 6              ;5CDC
V3392:  SVEC   16, -16, 0             ;5808
V3394:  RTSL                          ;C000

CHAR_Z:           ;1 ref
V3396:  SVEC   0, 24, 0               ;4C00
V3398:  SVEC   16, 0, 6               ;40C8
V339A:  SVEC   -16, -24, 6            ;54D8
V339C:  JMPL   UNDERL                 ;E9D6

CHAR_1:           ;1 ref
V339E:  SVEC   8, 24, 0               ;4C04
V33A0:  JMPL   CHARI2                 ;E978

CHAR_2:           ;1 ref
V33A2:  SVEC   0, 24, 0               ;4C00
V33A4:  SVEC   16, 0, 6               ;40C8
V33A6:  SVEC   0, -12, 6              ;5AC0
V33A8:  SVEC   -16, 0, 6              ;40D8
V33AA:  SVEC   0, -12, 6              ;5AC0

UNDERL:           ;4 refs
V33AC:  SVEC   16, 0, 6               ;40C8

CHAR21:           ;2 refs
V33AE:  SVEC   8, 0, 0                ;4004
V33B0:  RTSL                          ;C000

CHAR_3:           ;1 ref
V33B2:  SVEC   0, 24, 0               ;4C00

CHAR31:           ;1 ref
V33B4:  SVEC   16, 0, 6               ;40C8
V33B6:  SVEC   0, -24, 6              ;54C0
V33B8:  SVEC   -16, 0, 6              ;40D8
V33BA:  SVEC   0, 12, 0               ;4600
V33BC:  SVEC   16, 0, 6               ;40C8
V33BE:  SVEC   8, -12, 0              ;5A04
V33C0:  RTSL                          ;C000

CHAR_4:           ;1 ref
V33C2:  SVEC   0, 24, 0               ;4C00
V33C4:  SVEC   0, -12, 6              ;5AC0
V33C6:  SVEC   16, 0, 6               ;40C8
V33C8:  SVEC   0, 12, 0               ;4600
V33CA:  JMPL   CHAR72                 ;E9EF

CHAR_6:           ;1 ref
V33CC:  SVEC   0, 12, 0               ;4600
V33CE:  SVEC   16, 0, 6               ;40C8
V33D0:  SVEC   0, -12, 6              ;5AC0
V33D2:  SVEC   -16, 0, 6              ;40D8
V33D4:  SVEC   0, 24, 6               ;4CC0
V33D6:  SVEC   24, -24, 0             ;540C
V33D8:  RTSL                          ;C000

CHAR_7:           ;1 ref
V33DA:  SVEC   0, 24, 0               ;4C00

CHAR71:           ;1 ref
V33DC:  SVEC   16, 0, 6               ;40C8

CHAR72:           ;3 refs
V33DE:  SVEC   0, -24, 6              ;54C0

CHAR73:           ;1 ref
V33E0:  SVEC   8, 0, 0                ;4004
V33E2:  RTSL                          ;C000

CHAR_8:           ;1 ref
V33E4:  SVEC   0, 24, 6               ;4CC0
V33E6:  JMPL   CHAR31                 ;E9DA

CHAR_9:           ;1 ref
V33E8:  SVEC   16, 12, 0              ;4608
V33EA:  SVEC   -16, 0, 6              ;40D8
V33EC:  SVEC   0, 12, 6               ;46C0
V33EE:  JMPL   CHAR71                 ;E9EE

VGMSGA:          
V33F0:  JSRL   CHAR_                  ;A993  ADDRESS OF LETTER ROUTINES
V33F2:  JSRL   CHAR_O                 ;A98F
V33F4:  JSRL   CHAR_1                 ;A9CF
V33F6:  JSRL   CHAR_2                 ;A9D1
V33F8:  JSRL   CHAR_3                 ;A9D9
V33FA:  JSRL   CHAR_4                 ;A9E1
V33FC:  JSRL   CHAR_S                 ;A9A9
V33FE:  JSRL   CHAR_6                 ;A9E6
V3400:  JSRL   CHAR_7                 ;A9ED
V3402:  JSRL   CHAR_8                 ;A9F2
V3404:  JSRL   CHAR_9                 ;A9F4
V3406:  JSRL   CHAR_A                 ;A941
V3408:  JSRL   CHAR_B                 ;A949
V340A:  JSRL   CHAR_C                 ;A955
V340C:  JSRL   CHAR_D                 ;A959
V340E:  JSRL   CHAR_E                 ;A960
V3410:  JSRL   CHAR_F                 ;A962
V3412:  JSRL   CHAR_G                 ;A968
V3414:  JSRL   CHAR_H                 ;A96F
V3416:  JSRL   CHAR_I                 ;A974
V3418:  JSRL   CHAR_J                 ;A97B
V341A:  JSRL   CHAR_K                 ;A97F
V341C:  JSRL   CHAR_L                 ;A985
V341E:  JSRL   CHAR_M                 ;A988
V3420:  JSRL   CHAR_N                 ;A98C
V3422:  JSRL   CHAR_O                 ;A98F
V3424:  JSRL   CHAR_P                 ;A995
V3426:  JSRL   CHAR_Q                 ;A99A
V3428:  JSRL   CHAR_R                 ;A9A2
V342A:  JSRL   CHAR_S                 ;A9A9
V342C:  JSRL   CHAR_T                 ;A9AF
V342E:  JSRL   CHAR_U                 ;A9B1
V3430:  JSRL   CHAR_V                 ;A9B7
V3432:  JSRL   CHAR_W                 ;A9BB
V3434:  JSRL   CHAR_X                 ;A9C0
V3436:  JSRL   CHAR_Y                 ;A9C4
V3438:  JSRL   CHAR_Z                 ;A9CB
V343A:  JSRL   CHAR_                  ;A993
V343C:  JSRL   UNDERL                 ;A9D6
V343E:  JSRL   CPYRIT                 ;AA2A
V3440:  JSRL   PNTNT                  ;AA33
V3442:  RTSL                          ;C000

CIRCLE:           ;Small circle (bullet point glyph, used by CPYRIT/PNTNT); 2 refs
V3444:  SVEC   0, 4, 0                ;4200
V3446:  SVEC   0, 16, 6               ;48C0
V3448:  SVEC   8, 4, 6                ;42C4
V344A:  SVEC   8, -4, 6               ;5EC4
V344C:  SVEC   0, -16, 6              ;58C0
V344E:  SVEC   -8, -4, 6              ;5EDC
V3450:  SVEC   -8, 4, 6               ;42DC
V3452:  RTSL                          ;C000

CPYRIT:           ;Copyright symbol (c) + circle; 1 ref
V3454:  JSRL   CIRCLE                 ;AA22
V3456:  VCTR   11, 1, 0               ;0001 000B
V345A:  SVEC   -6, 0, 6               ;40DD
V345C:  SVEC   0, 14, 6               ;47C0
V345E:  SVEC   6, 0, 6                ;40C3
V3460:  VCTR   13, -19, 0             ;1FED 000D
V3464:  RTSL                          ;C000

PNTNT:            ;'Patent pending'-style circled glyph; 1 ref
V3466:  JSRL   CIRCLE                 ;AA22
V3468:  VCTR   5, 1, 0                ;0001 0005
V346C:  SVEC   0, 14, 6               ;47C0
V346E:  SVEC   6, 0, 6                ;40C3
V3470:  SVEC   0, -6, 6               ;5DC0
V3472:  SVEC   -6, 0, 6               ;40DD
V3474:  VCTR   19, -13, 0             ;1FF3 0013
V3478:  RTSL                          ;C000

EXPIC:            ;Explosion picture, 10 point flashes (GVCTR-scaled x4)
V347A:  VCTR   -64, 0, 0              ;0000 1FC0  16/16 PICTURE OF EXPLOSION  (src: GVCTR -16,0,0)
V347E:  VCTR   0, 0, 7                ;0000 E000  (src: GVCTR 0,0,.BRITE)
V3482:  VCTR   -64, -64, 0            ;1FC0 1FC0  (src: GVCTR -16,-16,0)
V3486:  VCTR   0, 0, 7                ;0000 E000  (src: GVCTR 0,-0,.BRITE)
V348A:  VCTR   64, -64, 0             ;1FC0 0040  (src: GVCTR 16,-16,0)
V348E:  VCTR   0, 0, 7                ;0000 E000  (src: GVCTR 0,0,.BRITE)
V3492:  VCTR   96, 32, 0              ;0020 0060  (src: GVCTR 24,8,0)
V3496:  VCTR   0, 0, 7                ;0000 E000  (src: GVCTR -0,0,.BRITE)
V349A:  VCTR   64, -32, 0             ;1FE0 0040  (src: GVCTR 16,-8,0)
V349E:  VCTR   0, 0, 7                ;0000 E000  (src: GVCTR 0,0,.BRITE)
V34A2:  VCTR   0, 64, 0               ;0040 0000  (src: GVCTR 0,16,0)
V34A6:  VCTR   0, 0, 7                ;0000 E000  (src: GVCTR 0,-0,.BRITE)
V34AA:  VCTR   32, 96, 0              ;0060 0020  (src: GVCTR 8,24,0)
V34AE:  VCTR   0, 0, 7                ;0000 E000  (src: GVCTR 0,0,.BRITE)
V34B2:  VCTR   -32, 96, 0             ;0060 1FE0  (src: GVCTR -8,24,0)
V34B6:  VCTR   0, 0, 7                ;0000 E000  (src: GVCTR -0,0,.BRITE)
V34BA:  VCTR   -128, -32, 0           ;1FE0 1F80  (src: GVCTR -32,-8,0)
V34BE:  VCTR   0, 0, 7                ;0000 E000  (src: GVCTR 0,0,.BRITE)
V34C2:  VCTR   -96, 32, 0             ;0020 1FA0  (src: GVCTR -24,8,0)
V34C6:  VCTR   0, 0, 7                ;0000 E000  (src: GVCTR 0,-0,.BRITE)
V34CA:  RTSL                          ;C000

XCROSS:           ;Gunsight / crosshair (tank view)
V34CC:  CNTR                          ;8040
V34CE:  VCTR   0, -175, 0             ;1F51 0000
V34D2:  VCTR   0, 100, 3              ;0064 6000
V34D6:  VCTR   -75, 25, 0             ;0019 1FB5
V34DA:  VCTR   0, -25, 3              ;1FE7 6000
V34DE:  VCTR   150, 0, 3              ;0000 6096
V34E2:  VCTR   0, 25, 3               ;0019 6000
V34E6:  VCTR   0, 100, 0              ;0064 0000
V34EA:  VCTR   0, 25, 3               ;0019 6000
V34EE:  VCTR   -150, 0, 3             ;0000 7F6A
V34F2:  VCTR   0, -25, 3              ;1FE7 6000
V34F6:  VCTR   75, 25, 0              ;0019 004B
V34FA:  VCTR   0, 100, 3              ;0064 6000
V34FE:  RTSL                          ;C000

XCROS1:           ;Gunsight / crosshair, alternate (with extra tick marks)
V3500:  CNTR                          ;8040
V3502:  VCTR   0, -175, 0             ;1F51 0000
V3506:  VCTR   0, 100, 7              ;0064 E000
V350A:  VCTR   0, 40, 3               ;0028 6000
V350E:  VCTR   -35, 0, 0              ;0000 1FDD
V3512:  VCTR   -40, -40, 7            ;1FD8 FFD8
V3516:  VCTR   150, 0, 7              ;0000 E096
V351A:  VCTR   -40, 40, 7             ;0028 FFD8
V351E:  VCTR   0, 70, 0               ;0046 0000
V3522:  VCTR   40, 40, 7              ;0028 E028
V3526:  VCTR   -150, 0, 7             ;0000 FF6A
V352A:  VCTR   40, -40, 7             ;1FD8 E028
V352E:  VCTR   35, 0, 0               ;0000 0023
V3532:  VCTR   0, 40, 3               ;0028 6000
V3536:  VCTR   0, 100, 7              ;0064 E000
V353A:  RTSL                          ;C000

RDRING:           ;Radar ring
V353C:  CNTR                          ;8040
V353E:  VCTR   68, 316, 0             ;013C 0044
V3542:  SVEC   -8, 0, 7               ;40FC
V3544:  VCTR   -60, -60, 0            ;1FC4 1FC4
V3548:  SVEC   0, -8, 7               ;5CE0
V354A:  VCTR   -60, 68, 0             ;0044 1FC4
V354E:  SVEC   -8, 0, 7               ;40FC
V3550:  VCTR   68, 0, 0               ;0000 0044
V3554:  VCTR   -36, 52, 5             ;0034 BFDC
V3558:  VCTR   36, 8, 0               ;0008 0024
V355C:  SVEC   0, 8, 7                ;44E0
V355E:  VCTR   36, -16, 0             ;1FF0 0024
V3562:  VCTR   -36, -52, 5            ;1FCC BFDC
V3566:  RTSL                          ;C000

TANKS:            ;Tank symbol (radar blip glyph); 1 ref
V3568:  SVEC   -6, 6, 6               ;43DD
V356A:  VCTR   9, 3, 6                ;0003 C009
V356E:  VCTR   3, 6, 6                ;0006 C003
V3572:  VCTR   36, -9, 6              ;1FF7 C024
V3576:  SVEC   -6, -6, 6              ;5DDD
V3578:  VCTR   -36, 0, 6              ;0000 DFDC
V357C:  SVEC   18, 12, 0              ;4609
V357E:  VCTR   21, 0, 6               ;0000 C015
V3582:  VCTR   0, -3, 6               ;1FFD C000
V3586:  VCTR   -9, 0, 6               ;0000 DFF7
V358A:  VCTR   27, -9, 0              ;1FF7 001B
V358E:  RTSL                          ;C000

TSYMBL:           ;Tank symbol entry point (calls TANKS)
V3590:  JSRL   TANKS                  ;AAB4

CRACK0:           ;Screen-crack piece 0 (collision effect); 1 ref
V3592:  CNTR                          ;8040
V3594:  VCTR   -100, 50, 0            ;0032 1F9C
V3598:  VCTR   -75, 0, 6              ;0000 DFB5
V359C:  VCTR   35, -100, 0            ;1F9C 0023
V35A0:  VCTR   40, 100, 6             ;0064 C028
V35A4:  VCTR   -100, 100, 6           ;0064 DF9C
V35A8:  VCTR   250, 25, 0             ;0019 00FA
V35AC:  VCTR   -150, -125, 6          ;1F83 DF6A
V35B0:  VCTR   100, -50, 6            ;1FCE C064
V35B4:  RTSL                          ;C000

CRACK1:           ;Screen-crack piece 1; 1 ref
V35B6:  VCTR   80, 0, 6               ;0000 C050
V35BA:  VCTR   -220, -50, 0           ;1FCE 1F24
V35BE:  VCTR   65, -50, 6             ;1FCE C041
V35C2:  VCTR   -100, 150, 0           ;0096 1F9C
V35C6:  VCTR   0, -45, 6              ;1FD3 C000
V35CA:  VCTR   -25, 145, 0            ;0091 1FE7
V35CE:  VCTR   -150, 45, 6            ;002D DF6A
V35D2:  VCTR   225, 30, 0             ;001E 00E1
V35D6:  VCTR   -75, -75, 6            ;1FB5 DFB5
V35DA:  VCTR   250, 25, 0             ;0019 00FA
V35DE:  VCTR   10, 95, 6              ;005F C00A
V35E2:  VCTR   -10, -95, 0            ;1FA1 1FF6
V35E6:  VCTR   100, 60, 6             ;003C C064
V35EA:  RTSL                          ;C000

CRACK2:           ;Screen-crack piece 2; 1 ref
V35EC:  VCTR   100, 60, 6             ;003C C064
V35F0:  VCTR   -100, 45, 0            ;002D 1F9C
V35F4:  VCTR   0, -105, 6             ;1F97 C000
V35F8:  VCTR   -275, -10, 0           ;1FF6 1EED
V35FC:  VCTR   -105, 95, 6            ;005F DF97
V3600:  VCTR   -120, -125, 0          ;1F83 1F88
V3604:  VCTR   -50, 10, 6             ;000A DFCE
V3608:  VCTR   50, -10, 0             ;1FF6 0032
V360C:  VCTR   -130, -120, 6          ;1F88 DF7E
V3610:  VCTR   305, -70, 0            ;1FBA 0131
V3614:  VCTR   -90, -5, 6             ;1FFB DFA6
V3618:  VCTR   190, -100, 0           ;1F9C 00BE
V361C:  VCTR   45, -140, 6            ;1F74 C02D
V3620:  VCTR   110, 240, 0            ;00F0 006E
V3624:  VCTR   0, -40, 6              ;1FD8 C000
V3628:  RTSL                          ;C000

CRACK3:           ;Screen-crack piece 3; 1 ref
V362A:  VCTR   105, 0, 6              ;0000 C069
V362E:  VCTR   -215, -200, 0          ;1F38 1F29
V3632:  VCTR   -80, -150, 6           ;1F6A DFB0
V3636:  VCTR   -155, 390, 0           ;0186 1F65
V363A:  VCTR   0, -50, 6              ;1FCE C000
V363E:  VCTR   0, 50, 0               ;0032 0000
V3642:  VCTR   -85, 25, 6             ;0019 DFAB
V3646:  VCTR   120, 295, 0            ;0127 0078
V364A:  VCTR   -120, 115, 6           ;0073 DF88
V364E:  VCTR   175, 15, 0             ;000F 00AF
V3652:  VCTR   -55, -130, 6           ;1F7E DFC9
V3656:  VCTR   480, -25, 0            ;1FE7 01E0
V365A:  VCTR   70, 0, 6               ;0000 C046
V365E:  VCTR   -195, 105, 0           ;0069 1F3D
V3662:  VCTR   25, -60, 6             ;1FC4 C019
V3666:  VCTR   75, 85, 6              ;0055 C04B
V366A:  RTSL                          ;C000

CRACK4:           ;Screen-crack piece 4; 1 ref
V366C:  VCTR   -15, 65, 6             ;0041 DFF1
V3670:  VCTR   50, -40, 0             ;1FD8 0032
V3674:  VCTR   -35, -25, 6            ;1FE7 DFDD
V3678:  VCTR   -40, -465, 0           ;1E2F 1FD8
V367C:  VCTR   105, 100, 6            ;0064 C069
V3680:  VCTR   -80, -160, 0           ;1F60 1FB0
V3684:  VCTR   -25, 60, 6             ;003C DFE7
V3688:  VCTR   -10, -60, 6            ;1FC4 DFF6
V368C:  VCTR   -250, -350, 0          ;1EA2 1F06
V3690:  VCTR   -35, 60, 6             ;003C DFDD
V3694:  VCTR   -40, -60, 6            ;1FC4 DFD8
V3698:  VCTR   -115, 400, 0           ;0190 1F8D
V369C:  VCTR   -35, -30, 6            ;1FE2 DFDD
V36A0:  RTSL                          ;C000

CRACK5:           ;Screen-crack piece 5; 1 ref
V36A2:  VCTR   50, -70, 6             ;1FBA C032
V36A6:  VCTR   -100, 30, 0            ;001E 1F9C
V36AA:  VCTR   50, 40, 6              ;0028 C032
V36AE:  VCTR   475, -20, 0            ;1FEC 01DB
V36B2:  VCTR   -50, -50, 6            ;1FCE DFCE
V36B6:  VCTR   135, 600, 0            ;0258 0087
V36BA:  VCTR   15, -50, 6             ;1FCE C00F
V36BE:  VCTR   15, -340, 0            ;1EAC 000F
V36C2:  VCTR   95, -110, 6            ;1F92 C05F
V36C6:  RTSL                          ;C000

CRACK6:           ;Screen-crack piece 6; 1 ref
V36C8:  VCTR   115, 75, 6             ;004B C073
V36CC:  VCTR   -90, -115, 0           ;1F8D 1FA6
V36D0:  VCTR   -25, 40, 6             ;0028 DFE7
V36D4:  VCTR   -735, -70, 0           ;1FBA 1D21
V36D8:  VCTR   -65, -15, 6            ;1FF1 DFBF
V36DC:  RTSL                          ;C000

CRACK7:           ;Screen-crack piece 7; 1 ref
V36DE:  VCTR   -85, 45, 6             ;002D DFAB
V36E2:  VCTR   50, -95, 0             ;1FA1 0032
V36E6:  VCTR   35, 50, 6              ;0032 C023
V36EA:  VCTR   855, -15, 0            ;1FF1 0357
V36EE:  VCTR   -30, 60, 6             ;003C DFE2
V36F2:  VCTR   80, 30, 6              ;001E C050
V36F6:  RTSL                          ;C000

CRACKS:           ;Screen-crack picture - calls all 8 CRACK0-CRACK7 pieces
V36F8:  JSRL   CRACK0                 ;AAC9
V36FA:  JSRL   CRACK1                 ;AADB
V36FC:  JSRL   CRACK2                 ;AAF6
V36FE:  JSRL   CRACK3                 ;AB15
V3700:  JSRL   CRACK4                 ;AB36
V3702:  JSRL   CRACK5                 ;AB51
V3704:  JSRL   CRACK6                 ;AB64
V3706:  JSRL   CRACK7                 ;AB6F
V3708:  RTSL                          ;C000

HATCH:            ;Self-test / diagnostic screen (grid, scale test, brightness bars)
V370A:  CNTR                          ;8040
V370C:  SCAL   1, $00                 ;7100
V370E:  VCTR   520, 520, 0            ;0208 0208
V3712:  STAT   1, 1, 0                ;6210
V3714:  CNTR                          ;8040
V3716:  VCTR   -520, -520, 0          ;1DF8 1DF8
V371A:  STAT   2, 0, 0                ;6020
V371C:  VCTR   8, 136, 0              ;0088 0008
V3720:  VCTR   1023, 0, 7             ;0000 E3FF  EDGE OF SCREEN
V3724:  VCTR   0, 767, 7              ;02FF E000
V3728:  VCTR   -1023, 0, 7            ;0000 FC01
V372C:  VCTR   0, -767, 7             ;1D01 E000
V3730:  VCTR   767, 767, 7            ;02FF E2FF  DRAW 45 DEGREE
V3734:  VCTR   256, -256, 7           ;1F00 E100
V3738:  VCTR   -511, -511, 7          ;1E01 FE01
V373C:  VCTR   -512, 512, 7           ;0200 FE00
V3740:  VCTR   256, 255, 7            ;00FF E100
V3744:  VCTR   767, -767, 7           ;1D01 E2FF
V3748:  VCTR   0, 767, 0              ;02FF 0000  1023,895
V374C:  VCTR   -767, -767, 7          ;1D01 FD01
V3750:  VCTR   -256, 255, 7           ;00FF FF00
V3754:  VCTR   512, 512, 7            ;0200 E200
V3758:  VCTR   511, -511, 7           ;1E01 E1FF
V375C:  VCTR   -256, -256, 7          ;1F00 FF00
V3760:  VCTR   -767, 767, 7           ;02FF FD01
V3764:  CNTR                          ;8040
V3766:  SVEC   -16, -8, 0             ;5C18
V3768:  SVEC   30, 0, 7               ;40EF
V376A:  SVEC   -2, 4, 0               ;421F
V376C:  SVEC   -26, 0, 6              ;40D3
V376E:  SVEC   2, 4, 0                ;4201
V3770:  SVEC   22, 0, 5               ;40AB
V3772:  SVEC   -2, 4, 0               ;421F
V3774:  SVEC   -18, 0, 4              ;4097
V3776:  SVEC   2, 4, 0                ;4201
V3778:  SVEC   14, 0, 3               ;4067
V377A:  SVEC   -2, 4, 0               ;421F
V377C:  SVEC   -10, 0, 2              ;405B
V377E:  SVEC   2, 4, 0                ;4201
V3780:  SVEC   6, 0, 1                ;4023
V3782:  RTSL                          ;C000

CHKS0:           
V3784:  .byte  $92                    ;.BYTE 92

ARCTAN:          
V3785:  .byte  $00, $00, $00, $00, $01, $01, $01, $01, $01, $01, $02, $02, $02, $02, $02, $02 ;.BYTE 000,000,000,000,001,001,001,001,001,001,002,002,002,002,002,002
V3795:  .byte  $03, $03, $03, $03, $03, $03, $03, $04, $04, $04, $04, $04, $04, $05, $05, $05 ;.BYTE 003,003,003,003,003,003,003,004,004,004,004,004,004,005,005,005
V37A5:  .byte  $05, $05, $05, $06, $06, $06, $06, $06, $06, $06, $07, $07, $07, $07, $07, $07 ;.BYTE 005,005,005,006,006,006,006,006,006,006,007,007,007,007,007,007
V37B5:  .byte  $08, $08, $08, $08, $08, $08, $08, $09, $09, $09, $09, $09, $09, $0A, $0A, $0A ;.BYTE 008,008,008,008,008,008,008,009,009,009,009,009,009,00A,00A,00A
V37C5:  .byte  $0A, $0A, $0A, $0A, $0B, $0B, $0B, $0B, $0B, $0B, $0B, $0C, $0C, $0C, $0C, $0C ;.BYTE 00A,00A,00A,00A,00B,00B,00B,00B,00B,00B,00B,00C,00C,00C,00C,00C
V37D5:  .byte  $0C, $0C, $0D, $0D, $0D, $0D, $0D, $0D, $0D, $0E, $0E, $0E, $0E, $0E, $0E, $0E ;.BYTE 00C,00C,00D,00D,00D,00D,00D,00D,00D,00E,00E,00E,00E,00E,00E,00E
V37E5:  .byte  $0F, $0F, $0F, $0F, $0F, $0F, $0F, $10, $10, $10, $10, $10, $10, $10, $11, $11 ;.BYTE 00F,00F,00F,00F,00F,00F,00F,010,010,010,010,010,010,010,011,011
V37F5:  .byte  $11, $11, $11, $11, $11, $11, $12, $12, $12, $12, $12, $12, $12, $13, $13, $13 ;.BYTE 011,011,011,011,011,011,012,012,012,012,012,012,012,013,013,013
V3805:  .byte  $13, $13, $13, $13, $13, $14, $14, $14, $14, $14, $14, $14, $14, $15, $15, $15 ;.BYTE 013,013,013,013,013,014,014,014,014,014,014,014,014,015,015,015
V3815:  .byte  $15, $15, $15, $15, $15, $15, $16, $16, $16, $16, $16, $16, $16, $16, $17, $17 ;.BYTE 015,015,015,015,015,015,016,016,016,016,016,016,016,016,017,017
V3825:  .byte  $17, $17, $17, $17, $17, $17, $17, $18, $18, $18, $18, $18, $18, $18, $18, $18 ;.BYTE 017,017,017,017,017,017,017,018,018,018,018,018,018,018,018,018
V3835:  .byte  $19, $19, $19, $19, $19, $19, $19, $19, $19, $19, $1A, $1A, $1A, $1A, $1A, $1A ;.BYTE 019,019,019,019,019,019,019,019,019,019,01A,01A,01A,01A,01A,01A
V3845:  .byte  $1A, $1A, $1A, $1B, $1B, $1B, $1B, $1B, $1B, $1B, $1B, $1B, $1B, $1C, $1C, $1C ;.BYTE 01A,01A,01A,01B,01B,01B,01B,01B,01B,01B,01B,01B,01B,01C,01C,01C
V3855:  .byte  $1C, $1C, $1C, $1C, $1C, $1C, $1C, $1C, $1D, $1D, $1D, $1D, $1D, $1D, $1D, $1D ;.BYTE 01C,01C,01C,01C,01C,01C,01C,01C,01D,01D,01D,01D,01D,01D,01D,01D
V3865:  .byte  $1D, $1D, $1D, $1E, $1E, $1E, $1E, $1E, $1E, $1E, $1E, $1E, $1E, $1E, $1F, $1F ;.BYTE 01D,01D,01D,01E,01E,01E,01E,01E,01E,01E,01E,01E,01E,01E,01F,01F
V3875:  .byte  $1F, $1F, $1F, $1F, $1F, $1F, $1F, $1F, $1F, $1F, $20, $20, $20, $20, $20, $20 ;.BYTE 01F,01F,01F,01F,01F,01F,01F,01F,01F,01F,020,020,020,020,020,020

CHKS1:           
V3885:  .byte  $D4                    ;.BYTE 0D4

BONTBL:          
V3886:  .byte  $00, $14, $24, $49     ;.BYTE 0,14,24,49

MISLVL:          
V388A:  .byte  $05, $10, $20, $30     ;.BYTE 5,10,20,30

OBJPNT:          
V388E:  .word  $38E6, $3905, $3955, $3936 ;.WORD PYRTBL,CUBTBL,TNKTBL,SHLTBL
V3896:  .word  $3A55, $3A30, $3A0B, $39E6 ;.WORD TREAD7,TREAD6,TREAD5,TREAD4  REVERSED AFTER 1 TRIAL & 1 ERROR
V389E:  .word  $3A7A, $3A9F, $3AC4, $3AE9 ;.WORD TREAD8,TREAD9,TREADA,TREADB
V38A6:  .word  $3C3B, $3B0E, $3B3F, $3C5A ;.WORD ROCK1,RDRTBL,EXPTBL,ROCK2
V38AE:  .word  $3B9B, $3BC0, $3B46, $3B0E ;.WORD EX1TBL,EX2TBL,EX0TBL,RDRTBL
V38B6:  .word  $3BC0, $3B9B, $3C8B, $76D5 ;.WORD EX2TBL,EX1TBL,R2D3TB,BATBL
V38BE:  .word  $3BC0, $3C0A, $3B9B, $3BF1 ;.WORD EX2TBL,EX4TBL,EX1TBL,EX3TBL
V38C6:  .word  $3B9B, $3C0A, $774E, $77CD ;.WORD EX1TBL,EX4TBL,TLETBL,ZONTBL
V38CE:  .word  $3EB0, $3F17, $0000, $0000 ;.WORD SAUTBL,TR7TBL,0,0
V38D6:  .word  $3D28, $3D59, $3D8A, $3DBB ;.WORD REX0,REX1,REX2,REX3
V38DE:  .word  $3DEC, $3E1D, $3E4E, $3E7F ;.WORD REX4,REX5,REX6,REX7

PYRTBL:           ;object #0 points table
V38E6:  .byte  $1F                    ;.BYTE CUBTBL-PYRTBL
V38E7:  .word  $FE00, $FE00, $FEC0    ;.NWORD -128,-128,-80
V38ED:  .word  $FE00, $0200, $FEC0    ;.NWORD -128,128,-80
V38F3:  .word  $0200, $0200, $FEC0    ;.NWORD 128,128,-80
V38F9:  .word  $0200, $FE00, $FEC0    ;.NWORD 128,-128,-80
V38FF:  .word  $0000, $0000, $0140    ;.NWORD 0,0,80

CUBTBL:           ;object #1 points table
V3905:  .byte  $31                    ;.BYTE SHLTBL-CUBTBL
V3906:  .word  $FE00, $FE00, $FEC0    ;.NWORD -128,-128,-80
V390C:  .word  $FE00, $0200, $FEC0    ;.NWORD -128,128,-80
V3912:  .word  $0200, $0200, $FEC0    ;.NWORD 128,128,-80
V3918:  .word  $0200, $FE00, $FEC0    ;.NWORD 128,-128,-80
V391E:  .word  $FE00, $FE00, $0140    ;.NWORD -128,-128,80
V3924:  .word  $FE00, $0200, $0140    ;.NWORD -128,128,80
V392A:  .word  $0200, $0200, $0140    ;.NWORD 128,128,80
V3930:  .word  $0200, $FE00, $0140    ;.NWORD 128,-128,80

SHLTBL:           ;object #3 points table
V3936:  .byte  $1F                    ;.BYTE TNKTBL-SHLTBL
V3937:  .word  $FFD8, $FFD8, $FFD0    ;.NWORD -10,-10,-12
V393D:  .word  $FFD8, $FFD8, $FFF8    ;.NWORD -10,-10,-2
V3943:  .word  $FFD8, $0028, $FFF8    ;.NWORD -10,10,-2
V3949:  .word  $FFD8, $0028, $FFD0    ;.NWORD -10,10,-12
V394F:  .word  $0050, $0000, $FFE4    ;.NWORD 20,0,-7

TNKTBL:           ;object #2 points table
V3955:  .byte  $91                    ;.BYTE ETKTBL-TNKTBL
V3956:  .word  $FD20, $FE00, $FEC0    ;.NWORD -184,-128,-80
V395C:  .word  $FD20, $0200, $FEC0    ;.NWORD -184,128,-80
V3962:  .word  $03C8, $0200, $FEC0    ;.NWORD 242,128,-80
V3968:  .word  $03C8, $FE00, $FEC0    ;.NWORD 242,-128,-80
V396E:  .word  $FC00, $FDC8, $FF30    ;.NWORD -256,-142,-52
V3974:  .word  $FC00, $0238, $FF30    ;.NWORD -256,142,-52
V397A:  .word  $04E0, $0238, $FF30    ;.NWORD 312,142,-52
V3980:  .word  $04E0, $FDC8, $FF30    ;.NWORD 312,-142,-52
V3986:  .word  $FD58, $FEA8, $FF88    ;.NWORD -170,-86,-30
V398C:  .word  $FD58, $0158, $FF88    ;.NWORD -170,86,-30
V3992:  .word  $02A8, $0158, $FF88    ;.NWORD 170,86,-30
V3998:  .word  $02A8, $FEA8, $FF88    ;.NWORD 170,-86,-30
V399E:  .word  $FE00, $FF58, $0030    ;.NWORD -128,-42,12
V39A4:  .word  $FE00, $00A8, $0030    ;.NWORD -128,42,12
V39AA:  .word  $FF80, $FFD8, $FFF8    ;.NWORD -32,-10,-2
V39B0:  .word  $FF80, $0028, $FFF8    ;.NWORD -32,10,-2
V39B6:  .word  $0080, $0028, $FFD0    ;.NWORD 32,10,-12
V39BC:  .word  $0080, $FFD8, $FFD0    ;.NWORD 32,-10,-12
V39C2:  .word  $0460, $0028, $FFF8    ;.NWORD 280,10,-2
V39C8:  .word  $0460, $0028, $FFD0    ;.NWORD 280,10,-12
V39CE:  .word  $0460, $FFD8, $FFF8    ;.NWORD 280,-10,-2
V39D4:  .word  $0460, $FFD8, $FFD0    ;.NWORD 280,-10,-12
V39DA:  .word  $FE00, $0000, $0030    ;.NWORD -128,0,12
V39E0:  .word  $FE00, $0000, $0050    ;.NWORD -128,0,20

TREAD4:           ;object #7 points table
ETKTBL:           ;same address
V39E6:  .byte  $25                    ;.BYTE TREAD5-TREAD4
V39E7:  .word  $FC00, $FDC8, $FF30    ;.NWORD -256,-142,-52
V39ED:  .word  $FC00, $0238, $FF30    ;.NWORD -256,142,-52
V39F3:  .word  $FC68, $FDDC, $FF08    ;.NWORD -230,-137,-62
V39F9:  .word  $FC68, $0224, $FF08    ;.NWORD -230,137,-62
V39FF:  .word  $FCD0, $FDEC, $FEE0    ;.NWORD -204,-133,-72
V3A05:  .word  $FCD0, $0214, $FEE0    ;.NWORD -204,133,-72

TREAD5:           ;object #6 points table
V3A0B:  .byte  $25                    ;.BYTE TREAD6-TREAD5
V3A0C:  .word  $FC18, $FDCC, $FF28    ;.NWORD -250,-141,-54
V3A12:  .word  $FC18, $0234, $FF28    ;.NWORD -250,141,-54
V3A18:  .word  $FC80, $FDE0, $FF00    ;.NWORD -224,-136,-64
V3A1E:  .word  $FC80, $0220, $FF00    ;.NWORD -224,136,-64
V3A24:  .word  $FCE8, $FDF0, $FED8    ;.NWORD -198,-132,-74
V3A2A:  .word  $FCE8, $0210, $FED8    ;.NWORD -198,132,-74

TREAD6:           ;object #5 points table
V3A30:  .byte  $25                    ;.BYTE TREAD7-TREAD6
V3A31:  .word  $FC34, $FDD4, $FF1C    ;.NWORD -243,-139,-57
V3A37:  .word  $FC34, $022C, $FF1C    ;.NWORD -243,139,-57
V3A3D:  .word  $FC9C, $FDE4, $FEF4    ;.NWORD -217,-135,-67
V3A43:  .word  $FC9C, $021C, $FEF4    ;.NWORD -217,135,-67
V3A49:  .word  $FD04, $FDF8, $FECC    ;.NWORD -191,-130,-77
V3A4F:  .word  $FD04, $0208, $FECC    ;.NWORD -191,130,-77

TREAD7:           ;object #4 points table
V3A55:  .byte  $25                    ;.BYTE TREAD8-TREAD7
V3A56:  .word  $FC4C, $FDD8, $FF14    ;.NWORD -237,-138,-59
V3A5C:  .word  $FC4C, $0228, $FF14    ;.NWORD -237,138,-59
V3A62:  .word  $FCB4, $FDE8, $FEEC    ;.NWORD -211,-134,-69
V3A68:  .word  $FCB4, $0218, $FEEC    ;.NWORD -211,134,-69
V3A6E:  .word  $FD20, $FDFC, $FEC4    ;.NWORD -184,-129,-79
V3A74:  .word  $FD20, $0204, $FEC4    ;.NWORD -184,129,-79

TREAD8:           ;object #8 points table
V3A7A:  .byte  $25                    ;.BYTE TREAD9-TREAD8
V3A7B:  .word  $04E0, $FDC8, $FF30    ;.NWORD 312,-142,-52
V3A81:  .word  $04E0, $0238, $FF30    ;.NWORD 312,142,-52
V3A87:  .word  $0480, $FDDC, $FF08    ;.NWORD 288,-137,-62
V3A8D:  .word  $0480, $0224, $FF08    ;.NWORD 288,137,-62
V3A93:  .word  $0420, $FDEC, $FEE0    ;.NWORD 264,-133,-72
V3A99:  .word  $0420, $0214, $FEE0    ;.NWORD 264,133,-72

TREAD9:           ;object #9 points table
V3A9F:  .byte  $25                    ;.BYTE TREADA-TREAD9
V3AA0:  .word  $04C8, $FDCC, $FF28    ;.NWORD 306,-141,-54
V3AA6:  .word  $04C8, $0234, $FF28    ;.NWORD 306,141,-54
V3AAC:  .word  $0468, $FDE0, $FF00    ;.NWORD 282,-136,-64
V3AB2:  .word  $0468, $0220, $FF00    ;.NWORD 282,136,-64
V3AB8:  .word  $0408, $FDF0, $FED8    ;.NWORD 258,-132,-74
V3ABE:  .word  $0408, $0210, $FED8    ;.NWORD 258,132,-74

TREADA:           ;object #10 points table
V3AC4:  .byte  $25                    ;.BYTE TREADB-TREADA
V3AC5:  .word  $04B0, $FDD4, $FF1C    ;.NWORD 300,-139,-57
V3ACB:  .word  $04B0, $022C, $FF1C    ;.NWORD 300,139,-57
V3AD1:  .word  $0450, $FDE4, $FEF4    ;.NWORD 276,-135,-67
V3AD7:  .word  $0450, $021C, $FEF4    ;.NWORD 276,135,-67
V3ADD:  .word  $03F0, $FDF8, $FECC    ;.NWORD 252,-130,-77
V3AE3:  .word  $03F0, $0208, $FECC    ;.NWORD 252,130,-77

TREADB:           ;object #11 points table
V3AE9:  .byte  $25                    ;.BYTE ETREAD-TREADB
V3AEA:  .word  $0498, $FDD8, $FF14    ;.NWORD 294,-138,-59
V3AF0:  .word  $0498, $0228, $FF14    ;.NWORD 294,138,-59
V3AF6:  .word  $0438, $FDE8, $FEEC    ;.NWORD 270,-134,-69
V3AFC:  .word  $0438, $0218, $FEEC    ;.NWORD 270,134,-69
V3B02:  .word  $03D8, $FDFC, $FEC4    ;.NWORD 246,-129,-79
V3B08:  .word  $03D8, $0204, $FEC4    ;.NWORD 246,129,-79

RDRTBL:           ;object #13 points table; object #19 points table
ETREAD:           ;same address
V3B0E:  .byte  $31                    ;.BYTE ERADAR-RDRTBL
V3B0F:  .word  $0000, $FFB0, $0050    ;.NWORD 0,-20,20
V3B15:  .word  $0050, $FF60, $0064    ;.NWORD 20,-40,25
V3B1B:  .word  $0050, $FF60, $0078    ;.NWORD 20,-40,30
V3B21:  .word  $0000, $FFB0, $008C    ;.NWORD 0,-20,35
V3B27:  .word  $0000, $0050, $0050    ;.NWORD 0,20,20
V3B2D:  .word  $0050, $00A0, $0064    ;.NWORD 20,40,25
V3B33:  .word  $0050, $00A0, $0078    ;.NWORD 20,40,30
V3B39:  .word  $0000, $0050, $008C    ;.NWORD 0,20,35

EXPTBL:           ;object #14 points table
ERADAR:           ;same address
V3B3F:  .byte  $07                    ;.BYTE EEXP-EXPTBL
V3B40:  .word  $0000, $0000, $0000    ;.NWORD 0,0,0

EX0TBL:           ;object #18 points table
EEXP:             ;same address
V3B46:  .byte  $55                    ;.BYTE EX1TBL-EX0TBL
V3B47:  .word  $FDB4, $FEA8, $FF6C    ;.NWORD -147,-86,-37
V3B4D:  .word  $FDB4, $0158, $FF6C    ;.NWORD -147,86,-37
V3B53:  .word  $024C, $0158, $FE18    ;.NWORD 147,86,-122
V3B59:  .word  $024C, $FEA8, $FE18    ;.NWORD 147,-86,-122
V3B5F:  .word  $FEF0, $FF58, $FFD0    ;.NWORD -68,-42,-12
V3B65:  .word  $FEF0, $00A8, $FFD0    ;.NWORD -68,42,-12
V3B6B:  .word  $0000, $FFD8, $FF44    ;.NWORD 0,-10,-47
V3B71:  .word  $0000, $0028, $FF44    ;.NWORD 0,10,-47
V3B77:  .word  $00B4, $0028, $FEE0    ;.NWORD 45,10,-72
V3B7D:  .word  $00B4, $FFD8, $FEE0    ;.NWORD 45,-10,-72
V3B83:  .word  $0438, $0028, $FE0C    ;.NWORD 270,10,-125
V3B89:  .word  $0410, $0028, $FDE8    ;.NWORD 260,10,-134
V3B8F:  .word  $0438, $FFD8, $FE0C    ;.NWORD 270,-10,-125
V3B95:  .word  $0410, $FFD8, $FDE8    ;.NWORD 260,-10,-134

EX1TBL:           ;object #16 points table; object #21 points table; object #26 points table; object #28 points table
V3B9B:  .byte  $25                    ;.BYTE EX2TBL-EX1TBL
V3B9C:  .word  $00DC, $0000, $FEF0    ;.NWORD 55,0,-68
V3BA2:  .word  $FEC0, $FFB0, $FF44    ;.NWORD -80,-20,-47
V3BA8:  .word  $0154, $0050, $FFA0    ;.NWORD 85,20,-24
V3BAE:  .word  $FF48, $0000, $FE9C    ;.NWORD -46,0,-89
V3BB4:  .word  $FF84, $FFB0, $FF00    ;.NWORD -31,-20,-64
V3BBA:  .word  $FF8C, $0050, $FF30    ;.NWORD -29,20,-52

EX2TBL:           ;object #17 points table; object #20 points table; object #24 points table
V3BC0:  .byte  $31                    ;.BYTE EEX2-EX2TBL
V3BC1:  .word  $FF10, $FF88, $FEC0    ;.NWORD -60,-30,-80
V3BC7:  .word  $FE88, $0040, $FEE8    ;.NWORD -94,16,-70
V3BCD:  .word  $02D0, $00A0, $FE80    ;.NWORD 180,40,-96
V3BD3:  .word  $0280, $FF88, $FEC0    ;.NWORD 160,-30,-80
V3BD9:  .word  $FFD8, $FFC0, $FFB0    ;.NWORD -10,-16,-20
V3BDF:  .word  $0000, $0020, $FFC4    ;.NWORD 0,8,-15
V3BE5:  .word  $0038, $FF60, $FF38    ;.NWORD 14,-40,-50
V3BEB:  .word  $0078, $00C8, $FF10    ;.NWORD 30,50,-60

EX3TBL:           ;object #27 points table
EEX2:             ;same address
V3BF1:  .byte  $19                    ;.BYTE EX4TBL-EX3TBL
V3BF2:  .word  $FFB0, $FFF4, $FEE0    ;.NWORD -20,-3,-72
V3BF8:  .word  $01D8, $0070, $FE50    ;.NWORD 118,28,-108
V3BFE:  .word  $0320, $FFD4, $000C    ;.NWORD 200,-11,3
V3C04:  .word  $0058, $FFF0, $FEF4    ;.NWORD 22,-4,-67

EX4TBL:           ;object #25 points table; object #29 points table
V3C0A:  .byte  $31                    ;.BYTE ROCK1-EX4TBL
V3C0B:  .word  $FED4, $FFB8, $FF48    ;.NWORD -75,-18,-46
V3C11:  .word  $FF18, $FF58, $FF48    ;.NWORD -58,-42,-46
V3C17:  .word  $FF18, $FEF0, $FF14    ;.NWORD -58,-68,-59
V3C1D:  .word  $FED4, $FEF0, $FEE4    ;.NWORD -75,-68,-71
V3C23:  .word  $FFA0, $00A8, $FF34    ;.NWORD -24,42,-51
V3C29:  .word  $0028, $000C, $FF40    ;.NWORD 10,3,-48
V3C2F:  .word  $0028, $FEFC, $FEBC    ;.NWORD 10,-65,-81
V3C35:  .word  $FFA0, $FF18, $FE6C    ;.NWORD -24,-58,-101

ROCK1:            ;object #12 points table
V3C3B:  .byte  $1F                    ;.BYTE ROCK2-ROCK1
V3C3C:  .word  $FCE0, $FCE0, $FEC0    ;.NWORD -200,-200,-80
V3C42:  .word  $FCE0, $0320, $FEC0    ;.NWORD -200,200,-80
V3C48:  .word  $0320, $0320, $FEC0    ;.NWORD 200,200,-80
V3C4E:  .word  $0320, $FCE0, $FEC0    ;.NWORD 200,-200,-80
V3C54:  .word  $0000, $0000, $0190    ;.NWORD 0,0,100

ROCK2:            ;object #15 points table
V3C5A:  .byte  $31                    ;.BYTE EROCK-ROCK2
V3C5B:  .word  $FD80, $FD80, $FEC0    ;.NWORD -160,-160,-80
V3C61:  .word  $FD80, $0280, $FEC0    ;.NWORD -160,160,-80
V3C67:  .word  $0280, $0280, $FEC0    ;.NWORD 160,160,-80
V3C6D:  .word  $0280, $FD80, $FEC0    ;.NWORD 160,-160,-80
V3C73:  .word  $FD80, $FD80, $FFD8    ;.NWORD -160,-160,-10
V3C79:  .word  $FD80, $0280, $FFD8    ;.NWORD -160,160,-10
V3C7F:  .word  $0280, $0280, $FFD8    ;.NWORD 160,160,-10
V3C85:  .word  $0280, $FD80, $FFD8    ;.NWORD 160,-160,-10

R2D3TB:           ;object #22 points table
EROCK:            ;same address
V3C8B:  .byte  $9D                    ;.BYTE REX0-R2D3TB
V3C8C:  .word  $FE80, $0090, $0000    ;.NWORD -96,36,0
V3C92:  .word  $FE80, $0048, $0030    ;.NWORD -96,18,12
V3C98:  .word  $FE80, $FFB8, $0030    ;.NWORD -96,-18,12
V3C9E:  .word  $FE80, $FF70, $0000    ;.NWORD -96,-36,0
V3CA4:  .word  $FE80, $FFB8, $FFD0    ;.NWORD -96,-18,-12
V3CAA:  .word  $FE80, $0048, $FFD0    ;.NWORD -96,18,-12
V3CB0:  .word  $FFA0, $0120, $0000    ;.NWORD -24,72,0
V3CB6:  .word  $FFA0, $00C0, $0060    ;.NWORD -24,48,24
V3CBC:  .word  $FFA0, $FF40, $0060    ;.NWORD -24,-48,24
V3CC2:  .word  $FFA0, $FEE0, $0000    ;.NWORD -24,-72,0
V3CC8:  .word  $FFA0, $FF40, $FFA0    ;.NWORD -24,-48,-24
V3CCE:  .word  $FFA0, $00C0, $FFA0    ;.NWORD -24,48,-24
V3CD4:  .word  $0480, $0000, $0000    ;.NWORD 288,0,0
V3CDA:  .word  $0570, $0000, $0000    ;.NWORD 348,0,0
V3CE0:  .word  $FF70, $FF70, $FF58    ;.NWORD -36,-36,-42
V3CE6:  .word  $FF70, $0090, $FF58    ;.NWORD -36,36,-42
V3CEC:  .word  $0090, $0090, $FF58    ;.NWORD 36,36,-42
V3CF2:  .word  $0090, $FF70, $FF58    ;.NWORD 36,-36,-42
V3CF8:  .word  $FFD0, $FFD0, $FFA4    ;.NWORD -12,-12,-23
V3CFE:  .word  $FFD0, $0030, $FFA4    ;.NWORD -12,12,-23
V3D04:  .word  $0030, $0030, $FFAC    ;.NWORD 12,12,-21
V3D0A:  .word  $0030, $FFD0, $FFAC    ;.NWORD 12,-12,-21
V3D10:  .word  $FFA0, $0000, $0060    ;.NWORD -24,0,24
V3D16:  .word  $0210, $0048, $0030    ;.NWORD 132,18,12
V3D1C:  .word  $0210, $FFB8, $0030    ;.NWORD 132,-18,12
V3D22:  .word  $0030, $0000, $0090    ;.NWORD 12,0,36

REX0:             ;object #36 points table
V3D28:  .byte  $31                    ;.BYTE REX1-REX0
V3D29:  .word  $0000, $0034, $FF4C    ;.NWORD 0,13,-45
V3D2F:  .word  $0024, $0024, $FF4C    ;.NWORD 9,9,-45
V3D35:  .word  $0034, $0000, $FF4C    ;.NWORD 13,0,-45
V3D3B:  .word  $0024, $FFDC, $FF4C    ;.NWORD 9,-9,-45
V3D41:  .word  $0000, $FFCC, $FF4C    ;.NWORD 0,-13,-45
V3D47:  .word  $FFDC, $FFDC, $FF4C    ;.NWORD -9,-9,-45
V3D4D:  .word  $FFCC, $0000, $FF4C    ;.NWORD -13,0,-45
V3D53:  .word  $FFDC, $0024, $FF4C    ;.NWORD -9,9,-45

REX1:             ;object #37 points table
V3D59:  .byte  $31                    ;.BYTE REX2-REX1
V3D5A:  .word  $0000, $0064, $FF38    ;.NWORD 0,25,-50
V3D60:  .word  $0048, $0048, $FF38    ;.NWORD 18,18,-50
V3D66:  .word  $0064, $0000, $FF38    ;.NWORD 25,0,-50
V3D6C:  .word  $0048, $FFB8, $FF38    ;.NWORD 18,-18,-50
V3D72:  .word  $0000, $FF9C, $FF38    ;.NWORD 0,-25,-50
V3D78:  .word  $FFB8, $FFB8, $FF38    ;.NWORD -18,-18,-50
V3D7E:  .word  $FF9C, $0000, $FF38    ;.NWORD -25,0,-50
V3D84:  .word  $FFB8, $0048, $FF38    ;.NWORD -18,18,-50

REX2:             ;object #38 points table
V3D8A:  .byte  $31                    ;.BYTE REX3-REX2
V3D8B:  .word  $0000, $0098, $FF24    ;.NWORD 0,38,-55
V3D91:  .word  $006C, $006C, $FF24    ;.NWORD 27,27,-55
V3D97:  .word  $0098, $0000, $FF24    ;.NWORD 38,0,-55
V3D9D:  .word  $006C, $FF94, $FF24    ;.NWORD 27,-27,-55
V3DA3:  .word  $0000, $FF68, $FF24    ;.NWORD 0,-38,-55
V3DA9:  .word  $FF94, $FF94, $FF24    ;.NWORD -27,-27,-55
V3DAF:  .word  $FF68, $0000, $FF24    ;.NWORD -38,0,-55
V3DB5:  .word  $FF94, $006C, $FF24    ;.NWORD -27,27,-55

REX3:             ;object #39 points table
V3DBB:  .byte  $31                    ;.BYTE REX4-REX3
V3DBC:  .word  $0000, $00C8, $FF10    ;.NWORD 0,50,-60
V3DC2:  .word  $0090, $0090, $FF10    ;.NWORD 36,36,-60
V3DC8:  .word  $00C8, $0000, $FF10    ;.NWORD 50,0,-60
V3DCE:  .word  $0090, $FF70, $FF10    ;.NWORD 36,-36,-60
V3DD4:  .word  $0000, $FF38, $FF10    ;.NWORD 0,-50,-60
V3DDA:  .word  $FF70, $FF70, $FF10    ;.NWORD -36,-36,-60
V3DE0:  .word  $FF38, $0000, $FF10    ;.NWORD -50,0,-60
V3DE6:  .word  $FF70, $0090, $FF10    ;.NWORD -36,36,-60

REX4:             ;object #40 points table
V3DEC:  .byte  $31                    ;.BYTE REX5-REX4
V3DED:  .word  $0000, $00FC, $FEFC    ;.NWORD 0,63,-65
V3DF3:  .word  $00B0, $00B0, $FEFC    ;.NWORD 44,44,-65
V3DF9:  .word  $00FC, $0000, $FEFC    ;.NWORD 63,0,-65
V3DFF:  .word  $00B0, $FF50, $FEFC    ;.NWORD 44,-44,-65
V3E05:  .word  $0000, $FF04, $FEFC    ;.NWORD 0,-63,-65
V3E0B:  .word  $FF50, $FF50, $FEFC    ;.NWORD -44,-44,-65
V3E11:  .word  $FF04, $0000, $FEFC    ;.NWORD -63,0,-65
V3E17:  .word  $FF50, $00B0, $FEFC    ;.NWORD -44,44,-65

REX5:             ;object #41 points table
V3E1D:  .byte  $31                    ;.BYTE REX6-REX5
V3E1E:  .word  $0000, $012C, $FEE8    ;.NWORD 0,75,-70
V3E24:  .word  $00D4, $00D4, $FEE8    ;.NWORD 53,53,-70
V3E2A:  .word  $012C, $0000, $FEE8    ;.NWORD 75,0,-70
V3E30:  .word  $00D4, $FF2C, $FEE8    ;.NWORD 53,-53,-70
V3E36:  .word  $0000, $FED4, $FEE8    ;.NWORD 0,-75,-70
V3E3C:  .word  $FF2C, $FF2C, $FEE8    ;.NWORD -53,-53,-70
V3E42:  .word  $FED4, $0000, $FEE8    ;.NWORD -75,0,-70
V3E48:  .word  $FF2C, $00D4, $FEE8    ;.NWORD -53,53,-70

REX6:             ;object #42 points table
V3E4E:  .byte  $31                    ;.BYTE REX7-REX6
V3E4F:  .word  $0000, $0160, $FED4    ;.NWORD 0,88,-75
V3E55:  .word  $0108, $0108, $FED4    ;.NWORD 66,66,-75
V3E5B:  .word  $0160, $0000, $FED4    ;.NWORD 88,0,-75
V3E61:  .word  $0108, $FEF8, $FED4    ;.NWORD 66,-66,-75
V3E67:  .word  $0000, $FEA0, $FED4    ;.NWORD 0,-88,-75
V3E6D:  .word  $FEF8, $FEF8, $FED4    ;.NWORD -66,-66,-75
V3E73:  .word  $FEA0, $0000, $FED4    ;.NWORD -88,0,-75
V3E79:  .word  $FEF8, $0108, $FED4    ;.NWORD -66,66,-75

REX7:             ;object #43 points table
V3E7F:  .byte  $31                    ;.BYTE REXEND-REX7
V3E80:  .word  $0000, $0190, $FEC0    ;.NWORD 0,100,-80
V3E86:  .word  $011C, $011C, $FEC0    ;.NWORD 71,71,-80
V3E8C:  .word  $0190, $0000, $FEC0    ;.NWORD 100,0,-80
V3E92:  .word  $011C, $FEE4, $FEC0    ;.NWORD 71,-71,-80
V3E98:  .word  $0000, $FE70, $FEC0    ;.NWORD 0,-100,-80
V3E9E:  .word  $FEE4, $FEE4, $FEC0    ;.NWORD -71,-71,-80
V3EA4:  .word  $FE70, $0000, $FEC0    ;.NWORD -100,0,-80
V3EAA:  .word  $FEE4, $011C, $FEC0    ;.NWORD -71,71,-80

SAUTBL:           ;object #32 points table
REXEND:           ;same address
V3EB0:  .byte  $67                    ;.BYTE ESAUCE-SAUTBL
V3EB1:  .word  $FF10, $0000, $FFD8    ;.NWORD -60,0,-10
V3EB7:  .word  $FF60, $00A0, $FFD8    ;.NWORD -40,40,-10
V3EBD:  .word  $0000, $00F0, $FFD8    ;.NWORD 0,60,-10
V3EC3:  .word  $00A0, $00A0, $FFD8    ;.NWORD 40,40,-10
V3EC9:  .word  $00F0, $0000, $FFD8    ;.NWORD 60,0,-10
V3ECF:  .word  $00A0, $FF60, $FFD8    ;.NWORD 40,-40,-10
V3ED5:  .word  $0000, $FF10, $FFD8    ;.NWORD 0,-60,-10
V3EDB:  .word  $FF60, $FF60, $FFD8    ;.NWORD -40,-40,-10
V3EE1:  .word  $FC40, $0000, $0050    ;.NWORD -240,0,20
V3EE7:  .word  $FD58, $02A8, $0050    ;.NWORD -170,170,20
V3EED:  .word  $0000, $03C0, $0050    ;.NWORD 0,240,20
V3EF3:  .word  $02A8, $02A8, $0050    ;.NWORD 170,170,20
V3EF9:  .word  $03C0, $0000, $0050    ;.NWORD 240,0,20
V3EFF:  .word  $02A8, $FD58, $0050    ;.NWORD 170,-170,20
V3F05:  .word  $0000, $FC40, $0050    ;.NWORD 0,-240,20
V3F0B:  .word  $FD58, $FD58, $0050    ;.NWORD -170,-170,20
V3F11:  .word  $0000, $0000, $0118    ;.NWORD 0,0,70

TR7TBL:           ;object #33 points table
ESAUCE:           ;same address
V3F17:  .byte  $97                    ;.BYTE TR7END-TR7TBL
V3F18:  .word  $05B0, $0170, $FEC0    ;.NWORD 364,92,-80
V3F1E:  .word  $FE38, $0228, $FEC0    ;.NWORD -114,138,-80
V3F24:  .word  $FE38, $FDD8, $FEC0    ;.NWORD -114,-138,-80
V3F2A:  .word  $05B0, $FE90, $FEC0    ;.NWORD 364,-92,-80
V3F30:  .word  $FE38, $01C8, $FFA4    ;.NWORD -114,114,-23
V3F36:  .word  $FE38, $FE38, $FFA4    ;.NWORD -114,-114,-23
V3F3C:  .word  $0448, $0000, $FEEC    ;.NWORD 274,0,-69
V3F42:  .word  $FEF0, $0110, $FF8C    ;.NWORD -68,68,-29
V3F48:  .word  $FE38, $0110, $FFA4    ;.NWORD -114,68,-23
V3F4E:  .word  $FE38, $FEF0, $FFA4    ;.NWORD -114,-68,-23
V3F54:  .word  $FEF0, $FEF0, $FF8C    ;.NWORD -68,-68,-29
V3F5A:  .word  $FEF0, $00B8, $002C    ;.NWORD -68,46,11
V3F60:  .word  $FE38, $00B8, $002C    ;.NWORD -114,46,11
V3F66:  .word  $FE38, $FF48, $002C    ;.NWORD -114,-46,11
V3F6C:  .word  $FEF0, $FF48, $002C    ;.NWORD -68,-46,11
V3F72:  .word  $0500, $0058, $FFD4    ;.NWORD 320,22,-11
V3F78:  .word  $0058, $0058, $FFD4    ;.NWORD 22,22,-11
V3F7E:  .word  $0058, $FFA8, $FFD4    ;.NWORD 22,-22,-11
V3F84:  .word  $0500, $FFA8, $FFD4    ;.NWORD 320,-22,-11
V3F8A:  .word  $0500, $0058, $0000    ;.NWORD 320,22,0
V3F90:  .word  $FFA8, $0058, $0000    ;.NWORD -22,22,0
V3F96:  .word  $FFA8, $FFA8, $0000    ;.NWORD -22,-22,0
V3F9C:  .word  $0500, $FFA8, $0000    ;.NWORD 320,-22,0
V3FA2:  .word  $FE38, $0000, $002C    ;.NWORD -114,0,11
V3FA8:  .word  $FE38, $0000, $0114    ;.NWORD -114,0,69

EXPTBX:          
TR7END:           ;same address
V3FAE:  .word  $FF88, $FF88, $0014, $00C8, $0000 ;.NWORD -30,-30,5,50,0
V3FB8:  .word  $FF60                  ;.NWORD -40

EXPTBY:          
V3FBA:  .word  $0078, $0000, $FFEC, $00C8, $FF60 ;.NWORD 30,0,-5,50,-40
V3FC4:  .word  $FF60                  ;.NWORD -40

IZVEL:           
V3FC6:  .byte  $37, $28, $46, $58, $28 ;.BYTE 55,40,70,88,40
V3FCB:  .byte  $42                    ;.BYTE 66

PTBLO1:          
V3FCC:  .byte  $0C, $00, $0F, $10, $0C, $20, $0F, $40 ;.BYTE 0C,0,0F,10,0C,20,0F,40
V3FD4:  .byte  $0C, $18, $00, $28, $01, $30, $00, $38 ;.BYTE 0C,18,0,28,1,30,0,38
V3FDC:  .byte  $01, $40, $0F, $48, $0C, $50, $00, $58 ;.BYTE 1,40,0F,48,0C,50,0,58
V3FE4:  .byte  $01, $60, $0F, $68, $0C, $70, $00, $78 ;.BYTE 1,60,0F,68,0C,70,0,78
V3FEC:  .byte  $01, $80, $0F, $88, $0C, $90, $00, $98 ;.BYTE 1,80,0F,88,0C,90,0,98
V3FF4:  .byte  $01, $A0, $FF          ;.BYTE 1,0A0,0FF  END OF TABLE ( 0FF )

LOGOBJ:          
V3FF7:  .byte  $17, $80, $1E, $80, $1F, $80 ;.BYTE 17,80,1E,80,1F,80

V3FFD:  .byte  $00, $00, $00          ;ROM fill - not emitted by any source statement (BZMTNS.MAC's LOGOBJ, the last table, ends at $3FFD)

;----------------------------[ cross reference: JSRL/JMPL into vector ROM ]----------------------------
;name              addr   referenced from
;MTN0              $3016  $3006 JSRL (BZMTNS.MAC)
;MTN1              $30CA  $3008 JSRL (BZMTNS.MAC)
;MTN2              $3104  $300A JSRL (BZMTNS.MAC)
;MTN3              $3182  $300C JSRL (BZMTNS.MAC)
;MTN4              $31C4  $300E JSRL (BZMTNS.MAC)
;MTN5              $31F2  $3010 JSRL (BZMTNS.MAC)
;MTN6              $322E  $3012 JSRL (BZMTNS.MAC)
;MTN7              $3268  $3014 JSRL (BZMTNS.MAC)
;CHAR_A            $3282  $3406 JSRL (VGAN.MAC)
;CHAR_B            $3292  $3408 JSRL (VGAN.MAC)
;CHAR_C            $32AA  $340A JSRL (VGAN.MAC)
;CHAR_D            $32B2  $340C JSRL (VGAN.MAC)
;CHAR_E            $32C0  $340E JSRL (VGAN.MAC)
;CHAR_F            $32C4  $3410 JSRL (VGAN.MAC)
;CHARF1            $32CC  $3332 JMPL (VGAN.MAC)
;CHAR_G            $32D0  $3412 JSRL (VGAN.MAC)
;CHAR_H            $32DE  $3414 JSRL (VGAN.MAC)
;CHAR_I            $32E8  $3416 JSRL (VGAN.MAC)
;CHARI1            $32EC  $3360 JMPL (VGAN.MAC)
;CHARI2            $32F0  $33A0 JMPL (VGAN.MAC)
;CHAR_J            $32F6  $3418 JSRL (VGAN.MAC)
;CHAR_K            $32FE  $341A JSRL (VGAN.MAC)
;CHAR_L            $330A  $341C JSRL (VGAN.MAC)
;CHAR_M            $3310  $341E JSRL (VGAN.MAC)
;CHAR_N            $3318  $3420 JSRL (VGAN.MAC)
;CHAR_O            $331E  $33F2 JSRL (VGAN.MAC), $3422 JSRL (VGAN.MAC)
;CHAR01            $3324  $32DC JMPL (VGAN.MAC)
;CHAR_             $3326  $32A8 JMPL (VGAN.MAC), $32BE JMPL (VGAN.MAC), $33F0 JSRL (VGAN.MAC), $343A JSRL (BZMTNS.MAC)
;CHAR_P            $332A  $3424 JSRL (VGAN.MAC)
;CHAR_Q            $3334  $3426 JSRL (VGAN.MAC)
;CHAR_R            $3344  $3428 JSRL (VGAN.MAC)
;CHAR_S            $3352  $33FC JSRL (VGAN.MAC), $342A JSRL (VGAN.MAC)
;CHAR_T            $335E  $342C JSRL (VGAN.MAC)
;CHAR_U            $3362  $342E JSRL (VGAN.MAC)
;CHARU1            $3368  $32FC JMPL (VGAN.MAC), $331C JMPL (VGAN.MAC), $337E JMPL (VGAN.MAC)
;CHARU2            $336A  $335C JMPL (VGAN.MAC), $3374 JMPL (VGAN.MAC)
;CHAR_V            $336E  $3430 JSRL (VGAN.MAC)
;CHAR_W            $3376  $3432 JSRL (VGAN.MAC)
;CHAR_X            $3380  $3434 JSRL (VGAN.MAC)
;CHAR_Y            $3388  $3436 JSRL (VGAN.MAC)
;CHAR_Z            $3396  $3438 JSRL (VGAN.MAC)
;CHAR_1            $339E  $33F4 JSRL (VGAN.MAC)
;CHAR_2            $33A2  $33F6 JSRL (VGAN.MAC)
;UNDERL            $33AC  $32B0 JMPL (VGAN.MAC), $330E JMPL (VGAN.MAC), $339C JMPL (VGAN.MAC), $343C JSRL (BZMTNS.MAC)
;CHAR21            $33AE  $3342 JMPL (VGAN.MAC), $3350 JMPL (VGAN.MAC)
;CHAR_3            $33B2  $33F8 JSRL (VGAN.MAC)
;CHAR31            $33B4  $33E6 JMPL (VGAN.MAC)
;CHAR_4            $33C2  $33FA JSRL (VGAN.MAC)
;CHAR_6            $33CC  $33FE JSRL (VGAN.MAC)
;CHAR_7            $33DA  $3400 JSRL (VGAN.MAC)
;CHAR71            $33DC  $33EE JMPL (VGAN.MAC)
;CHAR72            $33DE  $32E6 JMPL (VGAN.MAC), $3316 JMPL (VGAN.MAC), $33CA JMPL (VGAN.MAC)
;CHAR73            $33E0  $3386 JMPL (VGAN.MAC)
;CHAR_8            $33E4  $3402 JSRL (VGAN.MAC)
;CHAR_9            $33E8  $3404 JSRL (VGAN.MAC)
;CIRCLE            $3444  $3454 JSRL (BZMTNS.MAC), $3466 JSRL (BZMTNS.MAC)
;CPYRIT            $3454  $343E JSRL (BZMTNS.MAC)
;PNTNT             $3466  $3440 JSRL (BZMTNS.MAC)
;TANKS             $3568  $3590 JSRL (BZMTNS.MAC)
;CRACK0            $3592  $36F8 JSRL (BZMTNS.MAC)
;CRACK1            $35B6  $36FA JSRL (BZMTNS.MAC)
;CRACK2            $35EC  $36FC JSRL (BZMTNS.MAC)
;CRACK3            $362A  $36FE JSRL (BZMTNS.MAC)
;CRACK4            $366C  $3700 JSRL (BZMTNS.MAC)
;CRACK5            $36A2  $3702 JSRL (BZMTNS.MAC)
;CRACK6            $36C8  $3704 JSRL (BZMTNS.MAC)
;CRACK7            $36DE  $3706 JSRL (BZMTNS.MAC)
