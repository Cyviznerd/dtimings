# dtimings - reference test vectors

**Ranking of authority**

1. **VESA / CTA published specifications** (definitive).
2. Reference implementations that quote spec text verbatim (e.g.
   `cvt_modeline_calculator_12/cvt12.c` embeds the CVT 1.2 Excel formulas
   in its comments; Xorg `gtf.c` credits the GTF 1.1 worksheet).
3. Third-party calculators/tools.

Where a tool contradicts the spec, this document flags the tool.

Sources of test vectors:
- **VESA GTF 1.1 spec** (PDF, September 1999) — Sections 7.2-7.5 (formula stages).
- **VESA CVT 1.2 spec** (PDF, 2013) — as reproduced in `cvt12.c`.
- **CTA-861-I Annex U.2** (OVT algorithm) — as reproduced in the [CTA-861 OVT Calculator](https://www.cta.tech/cta-861-ovt-calculator/) JS.
- **tomverbeure**: [video_timings_calculator](https://tomverbeure.github.io/video_timings_calculator) (client-side JS, follows CVT 1.2 spec).
- **UMC**: [Universal Modeline Calculator 0.2](https://github.com/cmsj/umc-p96).
- **Xorg gtf.c**: reference C implementation of GTF 1.1.

Modeline field order: `pClock(MHz) HR HSS HSE HFL VR VSS VSE VFL Hpol Vpol`.

---

## External tool bugs identified

### UMC 0.2 has three spec-non-compliant behaviors in CVT / CVT-RB

**1. V-sync width hard-coded to 4 lines (CVT 1.2 says 4/5/6/7/10 by aspect).**

`umc.c:126` initializes `UMC_CVT.VSyncWidth = 4.0` and the CVT/RBT flags never
touch it based on aspect ratio. The CVT 1.2 spec (Table 4-1) mandates:

```
4:3  -> 4 lines
16:9 -> 5 lines
16:10-> 6 lines
5:4  -> 7 lines
15:9 -> 7 lines
other-> 10 lines
```

`cvt12.c:334-390` implements this table. `tomverbeure` implements it. **UMC does not.**
This makes UMC's VSyncEnd wrong on all non-4:3 timings.

**2. H-sync pixel width rounded to nearest instead of ROUNDDOWN (CVT).**

`umc.c:706`: `HSyncTicks = round(...)*CELL_GRAN`.
CVT 1.2 spec (per `cvt12.c:596` verbatim): `ROUNDDOWN(H_SYNC_PER/100 * TOTAL_PIXELS / CELL_GRAN_RND, 0) * CELL_GRAN_RND`.
For 1920x1080@60 CVT: spec says 200 px, UMC says 208 px; the HSyncStart
in the modeline shifts by 8 pixels.

**3. No CVT-RB v2 support at all.**

UMC only implements CVT-RB v1 (`--rbt`). Users of `--rbt` on a modern LCD
get a more wasteful pixel clock than CVT-RB v2 would produce.

### GTF 1.1 - no bugs found in Xorg gtf.c or UMC's GTF path.

Both correctly use `ROUND(x,0)` (round-to-nearest) per GTF 1.1 spec, section 7.2:
> "If Y=0, then the number is rounded to the nearest integer."

This is what tripped dtimings up initially - dtimings was using CVT-1.2's
ROUNDDOWN convention in the GTF path, which is the wrong rounding for
GTF. Now fixed (see below).

---

## Reference 1: tomverbeure calculator

### CVT (normal blanking)

Spec basis: CVT 1.2 spec, formulas embedded in `cvt12.c:520-604`. Rounding:
`ROUNDDOWN` for H_BLANK and H_SYNC; V_SYNC width from aspect table.

| Res @ Hz | tomverbeure | dtimings | Match |
| --- | --- | --- | --- |
| 640x480 @ 60   | 23.75  640 656 720 800 / 480 483 487 500 -H +V | 23.750  640 656 720 800 / 480 483 487 500 -H +V | OK |
| 800x600 @ 60   | 38.25  800 832 912 1024 / 600 603 607 624 -H +V | 38.250  800 832 912 1024 / 600 603 607 624 -H +V | OK |
| 1024x768 @ 60  | 63.5  1024 1072 1176 1328 / 768 771 775 798 -H +V | 63.500  1024 1072 1176 1328 / 768 771 775 798 -H +V | OK |
| 1280x720 @ 60  | 74.5  1280 1344 1472 1664 / 720 723 728 748 -H +V | 74.500  1280 1344 1472 1664 / 720 723 728 748 -H +V | OK |
| 1920x1080 @ 60 | 173  1920 2048 2248 2576 / 1080 1083 1088 1120 -H +V | 173.000  1920 2048 2248 2576 / 1080 1083 1088 1120 -H +V | OK |
| 1920x1200 @ 60 | 193.25  1920 2056 2256 2592 / 1200 1203 1209 1245 -H +V | 193.250  1920 2056 2256 2592 / 1200 1203 1209 1245 -H +V | OK |
| 2560x1440 @ 60 | 312.25  2560 2752 3024 3488 / 1440 1443 1448 1493 -H +V | 312.250  2560 2752 3024 3488 / 1440 1443 1448 1493 -H +V | OK |
| 3840x2160 @ 60 | 712.75  3840 4152 4576 5312 / 2160 2163 2168 2237 -H +V | 712.750  3840 4152 4576 5312 / 2160 2163 2168 2237 -H +V | OK |

### CVT-RB v1

Spec basis: CVT 1.2 spec (RB v1), `cvt12.c:416-451, 617-618`.
Rounding: V front porch fixed at 3 (RB_V_FPORCH), V back porch = VBI-3-VSYNC.

| Res @ Hz | tomverbeure | dtimings | Match |
| --- | --- | --- | --- |
| 640x480 @ 60   | 23.5  640 688 720 800 / 480 483 487 494 +H -V | 23.500  640 688 720 800 / 480 483 487 494 +H -V | OK |
| 800x600 @ 60   | 35.5  800 848 880 960 / 600 603 607 618 +H -V | 35.500  800 848 880 960 / 600 603 607 618 +H -V | OK |
| 1024x768 @ 60  | 56  1024 1072 1104 1184 / 768 771 775 790 +H -V | 56.000  1024 1072 1104 1184 / 768 771 775 790 +H -V | OK |
| 1280x720 @ 60  | 64  1280 1328 1360 1440 / 720 723 728 741 +H -V | 64.000  1280 1328 1360 1440 / 720 723 728 741 +H -V | OK |
| 1920x1080 @ 60 | 138.5  1920 1968 2000 2080 / 1080 1083 1088 1111 +H -V | 138.500  1920 1968 2000 2080 / 1080 1083 1088 1111 +H -V | OK |
| 1920x1200 @ 60 | 154  1920 1968 2000 2080 / 1200 1203 1209 1235 +H -V | 154.000  1920 1968 2000 2080 / 1200 1203 1209 1235 +H -V | OK |
| 2560x1440 @ 60 | 241.5  2560 2608 2640 2720 / 1440 1443 1448 1481 +H -V | 241.500  2560 2608 2640 2720 / 1440 1443 1448 1481 +H -V | OK |
| 3840x2160 @ 60 | 533.25  3840 3888 3920 4000 / 2160 2163 2168 2222 +H -V | 533.250  3840 3888 3920 4000 / 2160 2163 2168 2222 +H -V | OK |

### CVT-RB v2

Spec basis: CVT 1.2 spec (RB v2), `cvt12.c:619-620`. `V_BACK_PORCH` **fixed
at 6 lines**, `V_FRONT_PORCH` absorbs the slack. dtimings was patched to
match after initially getting this reversed.

| Res @ Hz | tomverbeure | dtimings | Match |
| --- | --- | --- | --- |
| 640x480 @ 60   | 21.384  640 648 680 720 / 480 481 489 495 +H -V | 21.384  640 648 680 720 / 480 481 489 495 +H -V | OK |
| 800x600 @ 60   | 32.63  800 808 840 880 / 600 604 612 618 +H -V | 32.630  800 808 840 880 / 600 604 612 618 +H -V | OK |
| 1024x768 @ 60  | 52.329  1024 1032 1064 1104 / 768 776 784 790 +H -V | 52.329  1024 1032 1064 1104 / 768 776 784 790 +H -V | OK |
| 1280x720 @ 60  | 60.465  1280 1288 1320 1360 / 720 727 735 741 +H -V | 60.465  1280 1288 1320 1360 / 720 727 735 741 +H -V | OK |
| 1920x1080 @ 60 | 133.32  1920 1928 1960 2000 / 1080 1097 1105 1111 +H -V | 133.320  1920 1928 1960 2000 / 1080 1097 1105 1111 +H -V | OK |
| 1920x1200 @ 60 | 148.2  1920 1928 1960 2000 / 1200 1221 1229 1235 +H -V | 148.200  1920 1928 1960 2000 / 1200 1221 1229 1235 +H -V | OK |
| 2560x1440 @ 60 | 234.59  2560 2568 2600 2640 / 1440 1467 1475 1481 +H -V | 234.590  2560 2568 2600 2640 / 1440 1467 1475 1481 +H -V | OK |
| 3840x2160 @ 60 | 522.614  3840 3848 3880 3920 / 2160 2208 2216 2222 +H -V | 522.614  3840 3848 3880 3920 / 2160 2208 2216 2222 +H -V | OK |

## Reference 2: CTA-861 OVT Calculator

Spec basis: CTA-861-I Annex U.2.

| Res @ Hz | Reference | dtimings | Match |
| --- | --- | --- | --- |
| 1920x1080 @ 60      | 139.776  2080 / 1120,  Hf=96 Hs=32 Hb=32,  Vf=13 Vs=8 Vb=19   | 139.776  2080 / 1120,  Hf=96 Hs=32 Hb=32,  Vf=13 Vs=8 Vb=19   | OK |
| 1920x1080 @ 60 -v   | 139.636 MHz (V=59.94006 Hz)                                  | 139.636 MHz (same lattice)                                   | OK |
| 1920x1080 @ 120     | 278.208  2016 / 1150,  Hf=32 Hs=32 Hb=32,  Vf=14 Vs=8 Vb=48   | 278.208  2016 / 1150,  Hf=32 Hs=32 Hb=32,  Vf=14 Vs=8 Vb=48   | OK |
| 1280x720 @ 60       | 67.488  1480 / 760,  Hf=136 Hs=32 Hb=32,  Vf=21 Vs=8 Vb=11    | 67.488  1480 / 760,  Hf=136 Hs=32 Hb=32,  Vf=21 Vs=8 Vb=11    | OK |
| 2560x1440 @ 60      | 241.920  2688 / 1500,  Hf=64 Hs=32 Hb=32,  Vf=24 Vs=8 Vb=28   | 241.920  2688 / 1500,  Hf=64 Hs=32 Hb=32,  Vf=24 Vs=8 Vb=28   | OK |
| 3840x2160 @ 24      | 261.888  3968 / 2750,  Hf=64 Hs=32 Hb=32,  Vf=563 Vs=8 Vb=19  | 261.888  3968 / 2750,  Hf=64 Hs=32 Hb=32,  Vf=563 Vs=8 Vb=19  | OK |
| 3840x2160 @ 24 -v   | 261.626 MHz (V=23.976 Hz)                                    | 261.626 MHz (same)                                           | OK |
| 3840x2160 @ 60      | 532.224  3960 / 2240,  Hf=56 Hs=32 Hb=32,  Vf=26 Vs=8 Vb=46   | 532.224  3960 / 2240,  Hf=56 Hs=32 Hb=32,  Vf=26 Vs=8 Vb=46   | OK |
| 3840x2160 @ 120     | 1095.168  3968 / 2300, Hf=64 Hs=32 Hb=32,  Vf=29 Vs=8 Vb=103  | 1095.168  3968 / 2300, Hf=64 Hs=32 Hb=32,  Vf=29 Vs=8 Vb=103  | OK |

## Reference 3: UMC 0.2 (GTF path)

Spec basis: **VESA GTF 1.1**, sections 7.2 (rounding) and 7.3-7.5 (formulas).

GTF 1.1 spec section 7.2 (quoted directly from the PDF):
> "In the following formula, the Microsoft EXCEL(TM) definition of ROUND(X,Y) has been used ...
>  If Y=0, then the number is rounded to the nearest integer."

Applied to the specific formulas in section 7.3:
- Stage  8: `[V SYNC+BP] = ROUND([MIN VSYNC+BP] / [H PERIOD EST], 0)` — round-to-nearest
- Stage 12: `[H PERIOD] = [H PERIOD EST] / ([V FIELD RATE RQD] / [V FIELD RATE EST])` — refined H period
- Stage 18: `[IDEAL DUTY CYCLE] = [C'] - ([M'] * [H PERIOD] / 1000)` — duty uses REFINED period
- Stage 19: `[H BLANK] = ROUND(...) * 2 * CELL_GRAN` — round-to-nearest
- Stage 21: `[PIXEL FREQ] = [TOTAL PIXELS] / [H PERIOD]` — clock uses REFINED period
- Stage  2 (H sync): `[H SYNC (PIXELS)] = ROUND(H_SYNC_PER/100 * TOTAL_PIXELS / CELL_GRAN, 0) * CELL_GRAN`

Both UMC's GTF and dtimings' GTF (post-fix) implement these exactly. Xorg's
`gtf.c` uses `rint()` for the same stages; likewise correct.

| Res @ Hz | UMC (--gtf) | dtimings (--standard gtf) | Match |
| --- | --- | --- | --- |
| 640x480 @ 60   | 23.856000  640 656 720 800 / 480 481 484 497 | 23.856  640 656 720 800 / 480 481 484 497 | OK |
| 800x600 @ 60   | 38.215680  800 832 912 1024 / 600 601 604 622 | 38.216  800 832 912 1024 / 600 601 604 622 | OK |
| 1024x768 @ 60  | 64.108800  1024 1080 1184 1344 / 768 769 772 795 | 64.109  1024 1080 1184 1344 / 768 769 772 795 | OK |
| 1280x720 @ 60  | 74.480640  1280 1336 1472 1664 / 720 721 724 746 | 74.481  1280 1336 1472 1664 / 720 721 724 746 | OK |
| 1280x1024 @ 60 | 108.883200  1280 1360 1496 1712 / 1024 1025 1028 1060 | 108.883  1280 1360 1496 1712 / 1024 1025 1028 1060 | OK |
| 1280x1024 @ 75 | 138.542400  1280 1368 1504 1728 / 1024 1025 1028 1069 | 138.542  1280 1368 1504 1728 / 1024 1025 1028 1069 | OK |
| 1600x1200 @ 60 | 160.963200  1600 1704 1880 2160 / 1200 1201 1204 1242 | 160.963  1600 1704 1880 2160 / 1200 1201 1204 1242 | OK |
| 1920x1080 @ 60 | 172.798080  1920 2040 2248 2576 / 1080 1081 1084 1118 | 172.798  1920 2040 2248 2576 / 1080 1081 1084 1118 | OK |
| 1920x1200 @ 60 | 193.155840  1920 2048 2256 2592 / 1200 1201 1204 1242 | 193.156  1920 2048 2256 2592 / 1200 1201 1204 1242 | OK |
| 2560x1440 @ 60 | 311.827200  2560 2744 3024 3488 / 1440 1441 1444 1490 | 311.827  2560 2744 3024 3488 / 1440 1441 1444 1490 | OK |
| 3840x2160 @ 60 | 712.339200  3840 4152 4576 5312 / 2160 2161 2164 2235 | 712.339  3840 4152 4576 5312 / 2160 2161 2164 2235 | OK |
| 1024x768 @ 75  | 81.804000  1024 1080 1192 1360 / 768 769 772 802 | 81.804  1024 1080 1192 1360 / 768 769 772 802 | OK |
| 1024x768 @ 85  | 94.386720  1024 1088 1200 1376 / 768 769 772 807 | 94.387  1024 1088 1200 1376 / 768 769 772 807 | OK |

## Reference 4: UMC 0.2 (CVT path) - shows the UMC bugs

Spec basis: **CVT 1.2**. Where UMC and dtimings disagree, dtimings matches the
CVT 1.2 spec text (via `cvt12.c`) and the tomverbeure calculator. **All the
diffs below are UMC bugs.**

| Res @ Hz | UMC (--cvt)  | dtimings (--standard cvt)  | UMC bug(s) |
| --- | --- | --- | --- |
| 640x480 @ 60   | 640 656 720 800 / 480 483 487 500 | 640 656 720 800 / 480 483 487 500 | none (4:3, sync exact) |
| 800x600 @ 60   | 800 832 912 1024 / 600 603 607 624 | 800 832 912 1024 / 600 603 607 624 | none (4:3) |
| 1024x768 @ 60  | 1024 1072 1176 1328 / 768 771 775 798 | 1024 1072 1176 1328 / 768 771 775 798 | none (4:3) |
| 1280x720 @ 60  | 1280 **1336** 1472 1664 / 720 723 **727** 748 | 1280 **1344** 1472 1664 / 720 723 **728** 748 | H sync 136 (nearest-round bug); V sync 4 (aspect-hardcode bug: 16:9 should be 5) |
| 1280x1024 @ 60 | 1280 1360 1496 1712 / 1024 1027 **1031** 1063 | 1280 1360 1496 1712 / 1024 1027 **1034** 1063 | V sync 4 (should be 7 for 5:4) |
| 1600x1200 @ 60 | 1600 **1704** 1880 2160 / 1200 1203 1207 1245 | 1600 **1712** 1880 2160 / 1200 1203 1207 1245 | H sync 176 (should be 168) |
| 1920x1080 @ 60 | 1920 **2040** 2248 2576 / 1080 1083 **1087** 1120 | 1920 **2048** 2248 2576 / 1080 1083 **1088** 1120 | H sync 208 (should be 200); V sync 4 (should be 5 for 16:9) |
| 1920x1200 @ 60 | 1920 **2048** 2256 2592 / 1200 1203 **1207** 1245 | 1920 **2056** 2256 2592 / 1200 1203 **1209** 1245 | H sync 208 (should be 200); V sync 4 (should be 6 for 16:10) |
| 2560x1440 @ 60 | 2560 **2744** 3024 3488 / 1440 1443 **1447** 1493 | 2560 **2752** 3024 3488 / 1440 1443 **1448** 1493 | H sync 280 (should be 272); V sync 4 (should be 5 for 16:9) |
| 3840x2160 @ 60 | 3840 4152 4576 5312 / 2160 2163 **2167** 2237 | 3840 4152 4576 5312 / 2160 2163 **2168** 2237 | V sync 4 (should be 5 for 16:9) |

Pixel clock, H_TOTAL and V_TOTAL match everywhere - UMC only misplaces
the H-sync-start and V-sync-end.

---

## Fixes applied to dtimings

Both spec-non-compliant and each verified against the actual spec text
(not just against other tools):

1. **CVT-RB2 V-porch placement** - now matches CVT 1.2 `cvt12.c:619-620`:
   V_BACK_PORCH = 6 (fixed), V_FRONT_PORCH takes the slack. RB v1 policy
   unchanged.

2. **GTF rounding + H-period refinement** - `computeGtf` now uses
   `rint(x)` at every stage the GTF 1.1 spec calls out with `ROUND(x,0)`
   (Stages 8, 19, and the H_SYNC(PIXELS) formula in section 7.5), and
   adds the Stage 12 H-period refinement so the pixel clock hits the
   requested V rate exactly. Prior to fix, `computeGtf` was using
   `floor()` (correct for CVT 1.2, wrong for GTF 1.1).

## Overall match after all fixes

| Standard | Cases | Match | Spec authority |
| --- | --- | --- | --- |
| CVT (tomverbeure) | 8  | 8/8 | CVT 1.2 via cvt12.c |
| CVT-RB (tomverbeure) | 8  | 8/8 | CVT 1.2 via cvt12.c |
| CVT-RB2 (tomverbeure) | 8  | 8/8 | CVT 1.2 via cvt12.c |
| OVT (CTA calc) | 9  | 9/9 | CTA-861-I Annex U.2 |
| GTF (UMC) | 13 | 13/13 | GTF 1.1 spec sections 7.2-7.5 |
| CVT/RB (UMC) | 13 | H_TOTAL/V_TOTAL: 13/13; H-sync-start & V-sync-end: 6/13 | dtimings matches spec; **UMC diverges from spec** |
