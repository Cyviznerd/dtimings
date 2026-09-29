# dtimings

Command-line calculator for VESA video monitor timings.

Supports:

| Standard | `--standard` value | Notes                                             |
| -------- | ------------------ | ------------------------------------------------- |
| CVT      | `cvt` (default)    | VESA CVT 1.2, normal blanking. GTF-style curve.   |
| CVT-RB   | `rb`               | Reduced Blanking v1 (2003). H_blank fixed at 160. |
| CVT-RB2  | `rb2`              | Reduced Blanking v2 (CVT 1.2). H_blank fixed at 80.|
| GTF      | `gtf`              | Legacy VESA GTF 1.1, continuous pixel clock.      |
| OVT      | `ovt`              | CTA-861-I Optimized Video Timings (HDMI/DTV).     |

## Build

Recommended (self-contained, no runtime DLL dependencies):

```
g++ -O2 -std=c++17 -static -static-libgcc -static-libstdc++ dtimings.cpp -o dtimings
```

The `-static*` flags bundle the MinGW C++ runtime into the exe. Without
them the resulting exe imports `libgcc_s_seh-1.dll` and `libstdc++-6.dll`
from PATH; that adds DLL-loader latency to every invocation and breaks
the exe on machines that don't have MinGW installed.

Minimal build (dynamic, requires MinGW DLLs on PATH):

```
g++ -O2 -std=c++17 dtimings.cpp -o dtimings
```

MSVC (already self-contained by default):

```
cl /nologo /EHsc /O2 dtimings.cpp
```

## Usage

Run with no arguments to see the full usage text.

```
dtimings <hpixels> <vlines> <refresh_hz> [options]
```

### Required arguments

- `hpixels`  -- active horizontal pixels (e.g. `1920`)
- `vlines`   -- active vertical lines (e.g. `1080`)
- `refresh_hz` -- requested refresh rate in Hz (e.g. `60`)

### Common options

- `-s, --standard NAME`   `cvt` (default), `rb`, `rb2`, `gtf`, `ovt`
- `-i, --interlaced`      produce interlaced timing (not for OVT)
- `-m, --margins`         add VESA 1.8 % image margins (CVT / GTF)
- `-v, --video-optimized` apply 1000/1001 factor (RB2 or OVT -> 59.94 Hz etc.)
- `--aspect W:H`          override aspect for V-sync width lookup
- `--vsync N`             override V-sync width (lines)
- `--hpol +|-`, `--vpol +|-`  override sync polarity
- `-d, --detailed`        show the full timing table (default is a single-line Modeline)

## Examples

```
dtimings 1920 1080 60
dtimings 1920 1080 60 --standard rb2 --detailed
dtimings 1280 1024 75 --standard gtf
dtimings 3840 2160 60 --standard rb2 -v
dtimings 1920 1080 60 --standard ovt          # CTA-861-I OVT
dtimings 3840 2160 24 --standard ovt -v       # 23.976 Hz fractional
```

## Formulas at a glance

All standards share the GTF blanking-curve constants:

```
M = 600, C = 40, K = 128, J = 20
C' = ((C - J) * K/256) + J   = 30
M' = (K/256) * M             = 300

IDEAL_DUTY (%) = C' - (M' * H_PERIOD_us / 1000)
H_BLANK = round_down(ACTIVE * DUTY / (100 - DUTY) / (2*CELL_GRAN)) * (2*CELL_GRAN)
```

Key spec-specific constants:

| Constant           | GTF      | CVT       | CVT-RB v1 | CVT-RB v2 | OVT       |
| ------------------ | -------- | --------- | --------- | --------- | --------- |
| Cell granularity   | 8 px     | 8 px      | 8 px      | 1 px      | 8 px      |
| Pixel-clock step   | -        | 0.25 MHz  | 0.25 MHz  | 0.001 MHz | 1 kHz     |
| H_SYNC             | 8 %      | 8 %       | 32 px     | 32 px     | 32 px     |
| H_BLANK            | curve    | curve     | 160 px    | 80 px     | >=80, tuned|
| Min V-sync + BP    | 550 us   | 550 us    | -         | -         | -         |
| Min V-blank        | -        | -         | 460 us    | 460 us    | 460 us / 20 lines |
| V-sync width       | 3 lines  | 4-10 by AR| 4-10 by AR| 8 lines   | 8 lines   |
| V-front porch      | 1 line   | 3 lines   | 3 lines   | 1 line    | derived   |

OVT extras: audio-packet aware H-blank (LineOverhead 32 + 32.packets/line at
195 kHz), 4:2:0 threshold at 590 MHz (H_BLANK bumped to >=128 px), frame-rate
groups {24/25/30, 48/50/60, 100/120, ...} with V-total granularity, MaxChunkRate
650 MHz.

Default sync polarity: **CVT / GTF -> H-, V+** ; **CVT-RB / RB2 -> H+, V-** ;
**OVT -> H+, V+**.

## Output

Default output is a single-line Xorg-style Modeline, ready to paste into
`xrandr --newmode`:

```
Modeline "1920x1080_60.00" 139.776 1920 2016 2048 2080 1080 1093 1101 1120 +hsync +vsync
```

Modeline fields:
`"name" pClock  HR HSS HSE HFL  VR VSS VSE VFL  +/-hsync +/-vsync [interlace]`

Add `--detailed` (`-d`) for the full breakdown:

```
Standard      : CVT (normal blanking)
Resolution    : 1920 x 1080 @ 59.963 Hz
Pixel clock   : 173.0000 MHz
H frequency   : 67.158 kHz
V frequency   : 59.9628 Hz

Horizontal (px)    Active  FrontP  Sync   BackP  Blank  Total
                     1920     128    200    328    656   2576
Vertical (lines)     1080       3      5     32     40   1120

Sync polarity : H- V+

Modeline "1920x1080_59.96" 173.000 1920 2048 2248 2576 1080 1083 1088 1120 -hsync +vsync
```

## References

- VESA CVT 1.2 Standard
- VESA GTF 1.1 Standard
- CTA-861-I, Annex U.2 (OVT algorithm)
- Xorg `gtf.c` reference implementation
- `cvt_modeline_calculator_12` reference implementation for CVT-RB2
- CTA-861 OVT calculator (cta.tech)
