// dtimings.cpp
// Video monitor timing calculator.
// Implements VESA GTF (1.1), VESA CVT (1.2) with normal blanking,
// CVT-RB v1 (reduced blanking), CVT-RB v2 (CVT 1.2), and
// CTA-861-I OVT (Optimized Video Timings).
//
// Build:
//   MSVC     : cl /EHsc /O2 dtimings.cpp
//   MinGW    : g++ -O2 -std=c++17 -static -static-libgcc -static-libstdc++ \
//              dtimings.cpp -o dtimings
//   The -static* flags produce a self-contained exe with no runtime DLL
//   dependencies (matters on Windows: without them the exe imports
//   libgcc_s_seh-1.dll and libstdc++-6.dll, which adds loader latency
//   and breaks portability).
//
// Running dtimings with no arguments prints usage documentation.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>

namespace {

// -----------------------------------------------------------------------------
// GTF / CVT common blanking-curve constants (identical across the two specs)
// -----------------------------------------------------------------------------
constexpr double GTF_M = 600.0;                     // blanking gradient
constexpr double GTF_C = 40.0;                      // blanking offset
constexpr double GTF_K = 128.0;                     // scaling
constexpr double GTF_J = 20.0;                      // scaling
constexpr double C_PRIME = ((GTF_C - GTF_J) * GTF_K / 256.0) + GTF_J;   // 30
constexpr double M_PRIME = (GTF_K / 256.0) * GTF_M;                     // 300

// -----------------------------------------------------------------------------
// GTF constants (VESA GTF 1.1)
// -----------------------------------------------------------------------------
constexpr double GTF_MARGIN_PERCENT   = 1.8;   // % of active vertical
constexpr int    GTF_CELL_GRAN        = 8;     // px
constexpr int    GTF_MIN_PORCH        = 1;     // lines / pixels
constexpr int    GTF_V_SYNC_RQD       = 3;     // lines
constexpr double GTF_H_SYNC_PERCENT   = 8.0;   // % of total line
constexpr double GTF_MIN_VSYNC_BP_US  = 550.0; // vsync + back porch, us

// -----------------------------------------------------------------------------
// CVT common constants (VESA CVT 1.2)
// -----------------------------------------------------------------------------
constexpr double CVT_MARGIN_PERCENT   = 1.8;
constexpr int    CVT_CELL_GRAN        = 8;
constexpr int    CVT_MIN_V_PORCH_RND  = 3;
constexpr int    CVT_MIN_V_BPORCH     = 6;
constexpr double CVT_MIN_VSYNC_BP_US  = 550.0;
constexpr double CVT_H_SYNC_PER       = 8.0;
constexpr double CVT_CLOCK_STEP_MHZ   = 0.25;

// CVT-RB v1 (2003)
constexpr int    RB1_H_BLANK          = 160;
constexpr int    RB1_H_SYNC           = 32;
constexpr int    RB1_V_FPORCH         = 3;
constexpr double RB1_MIN_V_BLANK_US   = 460.0;
constexpr int    RB1_MIN_V_BPORCH     = 6;

// CVT-RB v2 (CVT 1.2, 2013)
constexpr int    RB2_H_BLANK          = 80;
constexpr int    RB2_H_SYNC           = 32;
constexpr int    RB2_V_FPORCH         = 1;
constexpr double RB2_MIN_V_BLANK_US   = 460.0;
constexpr int    RB2_MIN_V_BPORCH     = 6;
constexpr int    RB2_V_SYNC_LINES     = 8;
constexpr int    RB2_CELL_GRAN        = 1;
constexpr double RB2_CLOCK_STEP_MHZ   = 0.001;

// -----------------------------------------------------------------------------
// CTA-861-I OVT constants (Annex U.2 defaults, from CTA OVT calculator)
// -----------------------------------------------------------------------------
constexpr double OVT_MIN_V_BLANK_US        = 460.0;
constexpr int    OVT_MIN_V_BLANK_LINES     = 20;
constexpr double OVT_MIN_V_SYNC_LE_US      = 400.0;
constexpr int    OVT_MIN_V_SYNC_LE_LINES   = 14;
constexpr int    OVT_MIN_CLOCK_420_MHZ     = 590;
constexpr int    OVT_PIX_420               = 2;
constexpr int    OVT_MIN_H_BLANK_444       = 80;
constexpr int    OVT_MIN_H_BLANK_420       = 128;
constexpr int    OVT_P_GRAN_HZ             = 1000;
constexpr int    OVT_MIN_H_GRAN            = 8;
constexpr int    OVT_AP_RATE_KHZ           = 195;
constexpr int    OVT_AP_SIZE               = 32;
constexpr int    OVT_LINE_OVERHEAD         = 32;
constexpr int    OVT_MAX_CHUNK_MHZ         = 650;
constexpr int    OVT_H_SYNC                = 32;
constexpr int    OVT_H_BACK                = 32;
constexpr int    OVT_V_SYNC                = 8;

// -----------------------------------------------------------------------------
// Types
// -----------------------------------------------------------------------------
enum class Standard { CVT, CVT_RB, CVT_RB2, GTF, OVT };

struct Options {
    int    hPixels        = 0;
    int    vLines         = 0;
    double refreshHz      = 0.0;

    Standard standard     = Standard::CVT;
    bool   interlaced     = false;
    bool   margins        = false;
    bool   videoOptimized = false;      // 1000/1001 for RB2
    char   hSyncPolarity  = 0;
    char   vSyncPolarity  = 0;
    std::optional<double> aspectOverride;
    std::optional<int>    vSyncOverride;
    bool   detailed       = false;   // full tabular output (default: compact)
};

struct Timing {
    int hActive       = 0;
    int hFrontPorch   = 0;
    int hSyncWidth    = 0;
    int hBackPorch    = 0;
    int hBlank        = 0;
    int hTotal        = 0;
    int vActive       = 0;
    int vFrontPorch   = 0;
    int vSyncWidth    = 0;
    int vBackPorch    = 0;
    int vBlank        = 0;
    int vTotal        = 0;
    double pixelClockMHz = 0.0;
    double hFreqKHz      = 0.0;
    double vFreqHz       = 0.0;
    char hSyncPolarity = '-';
    char vSyncPolarity = '+';
    bool interlaced   = false;
    std::string label;
};

int roundDown(double x, int gran) {
    return static_cast<int>(std::floor(x / gran)) * gran;
}

long long gcdLL(long long a, long long b) {
    while (b) { long long t = a % b; a = b; b = t; }
    return a < 0 ? -a : a;
}

// Greatest power of two that divides n (n > 0).
int gpow2d(long long n) {
    int p = 1;
    while ((n & 1) == 0 && n > 0) { p <<= 1; n >>= 1; }
    return p;
}

// Smallest power of two >= x (x >= 0).
int pow2ceiling(double x) {
    int p = 1;
    while (double(p) < x) p <<= 1;
    return p;
}

// Aspect-ratio -> default V-sync width (CVT normal / CVT-RB v1)
int cvtVSyncLines(double aspect) {
    auto near = [&](double t){ return std::fabs(aspect - t) < 1e-3; };
    if (near(4.0/3.0))     return 4;
    if (near(16.0/9.0))    return 5;
    if (near(16.0/10.0))   return 6;
    if (near(5.0/4.0))     return 7;
    if (near(15.0/9.0))    return 7;
    return 10;
}

// -----------------------------------------------------------------------------
// CVT -- normal blanking
// -----------------------------------------------------------------------------
Timing computeCvtNormal(const Options& o) {
    const int    cellGran   = CVT_CELL_GRAN;
    const double vRate      = o.refreshHz;
    const int    hPixRnd    = roundDown(o.hPixels, cellGran);
    const int    interlace  = o.interlaced ? 1 : 0;

    const double aspect     = o.aspectOverride.value_or(
                                  double(hPixRnd) / o.vLines);
    const int    vSync      = o.vSyncOverride.value_or(cvtVSyncLines(aspect));
    const int    vLinesRnd  = o.interlaced ? (o.vLines / 2) : o.vLines;

    int hMargin = 0, vMargin = 0;
    if (o.margins) {
        vMargin = roundDown(CVT_MARGIN_PERCENT / 100.0 * vLinesRnd, 1);
        hMargin = roundDown(CVT_MARGIN_PERCENT / 100.0 * hPixRnd / cellGran,
                            1) * cellGran;
    }
    const int activePix = hPixRnd + 2 * hMargin;

    const double hPeriodEst =
        (1.0e6 / vRate - CVT_MIN_VSYNC_BP_US) /
        (vLinesRnd + 2 * vMargin + CVT_MIN_V_PORCH_RND + interlace * 0.5);

    int vSyncBp = int(std::floor(CVT_MIN_VSYNC_BP_US / hPeriodEst)) + 1;
    vSyncBp = std::max(vSyncBp, vSync + CVT_MIN_V_BPORCH);

    const int totalV =
        vLinesRnd + 2 * vMargin + vSyncBp + interlace + CVT_MIN_V_PORCH_RND;

    double duty = C_PRIME - (M_PRIME * hPeriodEst / 1000.0);
    if (duty < 20.0) duty = 20.0;

    const int hBlank = int(std::floor(
        activePix * duty / (100.0 - duty) / (2.0 * cellGran))) * (2 * cellGran);

    const int totalPix = activePix + hBlank;

    const double pxUnq = totalPix / hPeriodEst;
    const double pxClk = std::floor(pxUnq / CVT_CLOCK_STEP_MHZ) *
                         CVT_CLOCK_STEP_MHZ;

    const int hSync  = roundDown(CVT_H_SYNC_PER / 100.0 * totalPix, cellGran);
    const int hBack  = hBlank / 2;
    const int hFront = hBlank - hSync - hBack;
    const int vFront = CVT_MIN_V_PORCH_RND;
    const int vBack  = vSyncBp - vSync;

    Timing t;
    t.hActive     = hPixRnd;
    t.hFrontPorch = hFront;
    t.hSyncWidth  = hSync;
    t.hBackPorch  = hBack;
    t.hBlank      = hBlank;
    t.hTotal      = totalPix;
    t.vActive     = o.vLines;
    t.vFrontPorch = vFront;
    t.vSyncWidth  = vSync;
    t.vBackPorch  = vBack;
    t.vBlank      = vFront + vSync + vBack + (o.interlaced ? 1 : 0);
    t.vTotal      = totalV;
    t.pixelClockMHz = pxClk;
    t.hFreqKHz    = pxClk * 1000.0 / totalPix;
    t.vFreqHz     = t.hFreqKHz * 1000.0 / totalV;
    t.hSyncPolarity = o.hSyncPolarity ? o.hSyncPolarity : '-';
    t.vSyncPolarity = o.vSyncPolarity ? o.vSyncPolarity : '+';
    t.interlaced  = o.interlaced;
    t.label       = "CVT (normal blanking)";
    return t;
}

// -----------------------------------------------------------------------------
// CVT -- reduced blanking (v1 or v2)
// -----------------------------------------------------------------------------
Timing computeCvtReduced(const Options& o, bool v2) {
    const int    cellGran     = v2 ? RB2_CELL_GRAN : CVT_CELL_GRAN;
    const int    hBlankFixed  = v2 ? RB2_H_BLANK   : RB1_H_BLANK;
    const int    hSyncFixed   = v2 ? RB2_H_SYNC    : RB1_H_SYNC;
    const int    vFrontFixed  = v2 ? RB2_V_FPORCH  : RB1_V_FPORCH;
    const double minVBlankUs  = v2 ? RB2_MIN_V_BLANK_US : RB1_MIN_V_BLANK_US;
    const int    minVBpFixed  = v2 ? RB2_MIN_V_BPORCH   : RB1_MIN_V_BPORCH;
    const double clockStep    = v2 ? RB2_CLOCK_STEP_MHZ : CVT_CLOCK_STEP_MHZ;

    double vRate = o.refreshHz;
    if (v2 && o.videoOptimized) vRate = vRate * 1000.0 / 1001.0;

    const int hPixRnd = roundDown(o.hPixels, cellGran);
    const int interlace = o.interlaced ? 1 : 0;

    const double aspect = o.aspectOverride.value_or(
                              double(hPixRnd) / o.vLines);
    const int vSync = o.vSyncOverride.value_or(
                          v2 ? RB2_V_SYNC_LINES : cvtVSyncLines(aspect));
    const int vLinesRnd = o.interlaced ? (o.vLines / 2) : o.vLines;

    int hMargin = 0, vMargin = 0;
    if (o.margins) {
        vMargin = roundDown(CVT_MARGIN_PERCENT / 100.0 * vLinesRnd, 1);
        hMargin = roundDown(CVT_MARGIN_PERCENT / 100.0 * hPixRnd / cellGran,
                            1) * cellGran;
    }
    const int activePix = hPixRnd + 2 * hMargin;

    const double hPeriodEst =
        (1.0e6 / vRate - minVBlankUs) /
        (vLinesRnd + 2 * vMargin);

    int vbiLines = int(std::floor(minVBlankUs / hPeriodEst)) + 1;
    vbiLines = std::max(vbiLines, vFrontFixed + vSync + minVBpFixed);

    const int totalV   = vbiLines + vLinesRnd + 2 * vMargin + interlace;
    const int totalPix = activePix + hBlankFixed;

    const double pxUnq = vRate * totalV * totalPix / 1.0e6;
    const double pxClk = std::floor(pxUnq / clockStep) * clockStep;

    const int hBackPorch  = hBlankFixed / 2;
    const int hFrontPorch = hBlankFixed - hSyncFixed - hBackPorch;

    // V-porch placement per CVT 1.2:
    //   RB v1  -> V_FRONT_PORCH fixed at RB_V_FPORCH (3), V_BACK_PORCH takes slack
    //   RB v2  -> V_BACK_PORCH  fixed at RB_MIN_V_BPORCH (6), V_FRONT_PORCH takes slack
    int vFrontOut, vBackOut;
    if (v2) {
        vBackOut  = minVBpFixed;
        vFrontOut = vbiLines - vBackOut - vSync;
    } else {
        vFrontOut = vFrontFixed;
        vBackOut  = vbiLines - vFrontOut - vSync;
    }

    Timing t;
    t.hActive     = hPixRnd;
    t.hFrontPorch = hFrontPorch;
    t.hSyncWidth  = hSyncFixed;
    t.hBackPorch  = hBackPorch;
    t.hBlank      = hBlankFixed;
    t.hTotal      = totalPix;
    t.vActive     = o.vLines;
    t.vFrontPorch = vFrontOut;
    t.vSyncWidth  = vSync;
    t.vBackPorch  = vBackOut;
    t.vBlank      = vbiLines + (o.interlaced ? 1 : 0);
    t.vTotal      = totalV;
    t.pixelClockMHz = pxClk;
    t.hFreqKHz    = pxClk * 1000.0 / totalPix;
    t.vFreqHz     = t.hFreqKHz * 1000.0 / totalV;
    t.hSyncPolarity = o.hSyncPolarity ? o.hSyncPolarity : '+';
    t.vSyncPolarity = o.vSyncPolarity ? o.vSyncPolarity : '-';
    t.interlaced  = o.interlaced;
    t.label       = v2 ? "CVT-RB v2 (reduced blanking, CVT 1.2)"
                       : "CVT-RB v1 (reduced blanking)";
    return t;
}

// -----------------------------------------------------------------------------
// GTF (VESA GTF 1.1) -- analog-CRT-oriented, continuous pixel clock
// -----------------------------------------------------------------------------
Timing computeGtf(const Options& o) {
    const int    cellGran = GTF_CELL_GRAN;
    const int    hPixRnd  = roundDown(o.hPixels, cellGran);
    const int    interlace = o.interlaced ? 1 : 0;

    const int    vLinesRnd = o.interlaced ? (o.vLines / 2) : o.vLines;
    const double vRate     = o.refreshHz;

    int vMargin = 0, hMargin = 0;
    if (o.margins) {
        vMargin = roundDown(GTF_MARGIN_PERCENT / 100.0 * vLinesRnd, 1);
        hMargin = roundDown(GTF_MARGIN_PERCENT / 100.0 * hPixRnd / cellGran,
                            1) * cellGran;
    }

    const double hPeriodEst =
        (1.0e6 / vRate - GTF_MIN_VSYNC_BP_US) /
        (vLinesRnd + 2 * vMargin + GTF_MIN_PORCH + interlace * 0.5);

    // GTF 1.1 spec (per Xorg gtf.c reference) rounds these three quantities
    // to the NEAREST integer, not down. That is what makes the difference
    // vs the CVT/RB path (which uses ROUNDDOWN per CVT 1.2 spec).
    const int vSyncBp    = int(std::rint(GTF_MIN_VSYNC_BP_US / hPeriodEst));
    const int vBackPorch = vSyncBp - GTF_V_SYNC_RQD;

    const int totalV = vLinesRnd + 2 * vMargin + vSyncBp
                     + GTF_MIN_PORCH + interlace;

    // Per Xorg gtf.c: refine H_PERIOD so the FINAL field rate exactly hits
    // v_field_rate_rqd (given the just-rounded totalV).
    //   v_field_rate_est = 1 / (H_PERIOD_EST * totalV)
    //   H_PERIOD         = H_PERIOD_EST / (v_field_rate_rqd / v_field_rate_est)
    // Equivalently, h_freq = v_rate * totalV.
    const double vFieldRateEst = 1.0e6 / (hPeriodEst * totalV);   // Hz
    const double hPeriod = hPeriodEst * vFieldRateEst / vRate;    // us

    const int activePix = hPixRnd + 2 * hMargin;

    // Duty cycle uses the REFINED H period, matching the spec / gtf.c.
    double duty = C_PRIME - (M_PRIME * hPeriod / 1000.0);
    if (duty < 20.0) duty = 20.0;

    const int hBlank = int(std::rint(
        activePix * duty / (100.0 - duty) / (2.0 * cellGran))) * (2 * cellGran);
    const int totalPix = activePix + hBlank;

    const double pxClk = totalPix / hPeriod;                      // MHz

    const int hSync  = int(std::rint(
        GTF_H_SYNC_PERCENT / 100.0 * totalPix / cellGran)) * cellGran;
    const int hBack  = hBlank / 2;
    const int hFront = hBlank - hSync - hBack;

    Timing t;
    t.hActive     = hPixRnd;
    t.hFrontPorch = hFront;
    t.hSyncWidth  = hSync;
    t.hBackPorch  = hBack;
    t.hBlank      = hBlank;
    t.hTotal      = totalPix;
    t.vActive     = o.vLines;
    t.vFrontPorch = GTF_MIN_PORCH;
    t.vSyncWidth  = GTF_V_SYNC_RQD;
    t.vBackPorch  = vBackPorch;
    t.vBlank      = GTF_MIN_PORCH + GTF_V_SYNC_RQD + vBackPorch
                    + (o.interlaced ? 1 : 0);
    t.vTotal      = totalV;
    t.pixelClockMHz = pxClk;
    t.hFreqKHz    = pxClk * 1000.0 / totalPix;
    t.vFreqHz     = t.hFreqKHz * 1000.0 / totalV;
    t.hSyncPolarity = o.hSyncPolarity ? o.hSyncPolarity : '-';
    t.vSyncPolarity = o.vSyncPolarity ? o.vSyncPolarity : '+';
    t.interlaced  = o.interlaced;
    t.label       = "GTF (Generalized Timing Formula 1.1)";
    return t;
}

// -----------------------------------------------------------------------------
// CTA-861-I OVT -- Optimized Video Timings
// -----------------------------------------------------------------------------
// Reference: CTA-861-I Annex U.2. Progressive scan only. NTSC/1000-1001
// fractional rate is selected with --video-optimized.
Timing computeOvt(const Options& o) {
    int hActive = o.hPixels;
    if (hActive % 8 != 0) hActive += 8 - (hActive % 8);   // spec: multiple of 8

    const int vActive = o.vLines;
    const int vrate   = int(std::round(o.refreshHz));     // integer rate

    // Frame-rate group -> (maxVrate, V-total granularity).
    int maxVrate, vgran;
    switch (vrate) {
        case 24: case 25: case 30:   maxVrate = 30;  vgran = 20; break;
        case 48: case 50: case 60:   maxVrate = 60;  vgran = 20; break;
        case 100: case 120:          maxVrate = 120; vgran = 5;  break;
        case 200: case 240:          maxVrate = 240; vgran = 5;  break;
        case 300: case 360:          maxVrate = 360; vgran = 5;  break;
        case 400: case 480:          maxVrate = 480; vgran = 5;  break;
        default:                     maxVrate = vrate; vgran = 1; break;
    }

    // Step 1 -- MinVtotal.
    const double maxActiveTimeUs = 1.0e6 / maxVrate - OVT_MIN_V_BLANK_US;
    const double minLineTimeUs   = maxActiveTimeUs / vActive;
    int minVblank = std::max(OVT_MIN_V_BLANK_LINES,
                             int(std::ceil(OVT_MIN_V_BLANK_US / minLineTimeUs)));
    int minVtot = vActive + minVblank;
    if (minVtot % vgran) minVtot += vgran - (minVtot % vgran);

    // Step 2 -- audio-packet requirements.
    const long long minLineRate = (long long)maxVrate * minVtot;
    const int maxAPline = int(std::ceil(
        double(OVT_AP_RATE_KHZ) * 1000.0 / double(minLineRate)));

    // Step 3 -- initial MinHtotal.
    const int minHblankAudio = OVT_LINE_OVERHEAD + OVT_AP_SIZE * maxAPline;
    int minHtot = hActive + std::max(OVT_MIN_H_BLANK_444, minHblankAudio);
    const long long initPrate = (long long)maxVrate * minHtot * minVtot;
    const int hgranChunk = pow2ceiling(
        double(initPrate) / (double(OVT_MAX_CHUNK_MHZ) * 1.0e6));
    const int hgran = std::max(OVT_MIN_H_GRAN, hgranChunk);
    if (minHtot % hgran) minHtot += hgran - (minHtot % hgran);

    // Step 4 -- OVT loop and 4:2:0 retry.
    int htot = 0, vtot = 0;
    long long prateHz = 0;
    for (int retry = 0; retry < 8; ++retry) {
        const long long pgran = OVT_P_GRAN_HZ;
        const long long rgran = pgran / gcdLL(pgran, maxVrate);
        long long rmin = 0;
        int v = minVtot;
        htot = minHtot; vtot = v;
        for (int yStep = 0; yStep < 5000; ++yStep) {
            int h = minHtot;
            long long r = (long long)h * v;
            if (rmin > 0 && r > rmin) break;
            for (int xStep = 0; xStep < 5000; ++xStep) {
                const long long chunk =
                    (long long)maxVrate * r / gpow2d(h);
                if ((r % rgran) == 0 &&
                    chunk <= (long long)OVT_MAX_CHUNK_MHZ * 1000000)
                    break;
                h += hgran;
                r = (long long)h * v;
            }
            if (rmin == 0 || r < rmin) {
                htot = h; vtot = v; rmin = r;
            }
            v += vgran;
        }
        prateHz = (long long)maxVrate * htot * vtot;

        // 4:2:0 adjustment: if pixel clock crosses the 4:2:0 threshold and
        // current H-total does not satisfy 4:2:0 blanking, retry with a
        // bigger MinHtotal.
        const int minHblank420Audio =
            OVT_PIX_420 * (OVT_LINE_OVERHEAD + OVT_AP_SIZE * maxAPline);
        int minHtot420 = hActive +
                         std::max(OVT_MIN_H_BLANK_420, minHblank420Audio);
        if (minHtot420 % hgran) minHtot420 += hgran - (minHtot420 % hgran);

        if (prateHz >= (long long)OVT_MIN_CLOCK_420_MHZ * 1000000 &&
            htot < minHtot420) {
            minHtot = minHtot420;
            continue;
        }
        break;
    }

    // Step 5 -- scale V-total to requested rate (from max-of-group).
    if (vrate != maxVrate) vtot = vtot * maxVrate / vrate;

    // Step 6 -- vertical sync placement.
    const int vBlank    = vtot - vActive;
    const int vSyncPos  = std::max(
        OVT_MIN_V_SYNC_LE_LINES,
        int(std::ceil(OVT_MIN_V_SYNC_LE_US * double(prateHz)
                      / (double(htot) * 1.0e6))));
    const int vFront    = vBlank - vSyncPos;
    const int vBack     = vBlank - vFront - OVT_V_SYNC;

    // Step 7 -- NTSC fractional rate.
    double prateOut = double(prateHz);
    if (o.videoOptimized) prateOut /= 1.001;

    // Step 8 -- horizontal sync layout (fixed).
    const int hBlank  = htot - hActive;
    const int hFront  = hBlank - OVT_H_SYNC - OVT_H_BACK;

    Timing t;
    t.hActive     = hActive;
    t.hFrontPorch = hFront;
    t.hSyncWidth  = OVT_H_SYNC;
    t.hBackPorch  = OVT_H_BACK;
    t.hBlank      = hBlank;
    t.hTotal      = htot;
    t.vActive     = vActive;
    t.vFrontPorch = vFront;
    t.vSyncWidth  = OVT_V_SYNC;
    t.vBackPorch  = vBack;
    t.vBlank      = vBlank;
    t.vTotal      = vtot;
    t.pixelClockMHz = prateOut / 1.0e6;
    t.hFreqKHz    = prateOut / double(htot) / 1000.0;
    t.vFreqHz     = t.hFreqKHz * 1000.0 / double(vtot);
    t.hSyncPolarity = o.hSyncPolarity ? o.hSyncPolarity : '+';
    t.vSyncPolarity = o.vSyncPolarity ? o.vSyncPolarity : '+';
    t.interlaced  = false;
    t.label       = o.videoOptimized
        ? "CTA-861-I OVT (Optimized Video Timings, 1000/1001)"
        : "CTA-861-I OVT (Optimized Video Timings)";
    return t;
}

// -----------------------------------------------------------------------------
// Output
// -----------------------------------------------------------------------------
void printModeline(const Timing& t) {
    const int hss = t.hActive + t.hFrontPorch;
    const int hse = hss + t.hSyncWidth;
    const int hfl = t.hTotal;
    const int vss = t.vActive + t.vFrontPorch;
    const int vse = vss + t.vSyncWidth;
    const int vfl = t.vTotal;
    std::printf(
        "Modeline \"%dx%d_%.2f\" %.3f %d %d %d %d %d %d %d %d %chsync %cvsync%s\n",
        t.hActive, t.vActive, t.vFreqHz,
        t.pixelClockMHz,
        t.hActive, hss, hse, hfl,
        t.vActive, vss, vse, vfl,
        t.hSyncPolarity, t.vSyncPolarity,
        t.interlaced ? " interlace" : "");
}

void printTiming(const Timing& t, const Options& o) {
    // Default: single-line Xorg-style modeline (compact).
    if (!o.detailed) {
        printModeline(t);
        return;
    }

    // --detailed: full tabular form plus the modeline at the bottom.
    std::printf("Standard      : %s%s\n",
                t.label.c_str(), t.interlaced ? " [interlaced]" : "");
    std::printf("Resolution    : %d x %d @ %.3f Hz\n",
                t.hActive, t.vActive, t.vFreqHz);
    std::printf("Pixel clock   : %.4f MHz\n", t.pixelClockMHz);
    std::printf("H frequency   : %.3f kHz\n", t.hFreqKHz);
    std::printf("V frequency   : %.4f Hz\n",  t.vFreqHz);
    std::printf("\n");
    std::printf("Horizontal (px)    Active  FrontP  Sync   BackP  Blank  Total\n");
    std::printf("                   %6d  %6d  %5d  %5d  %5d  %5d\n",
                t.hActive, t.hFrontPorch, t.hSyncWidth,
                t.hBackPorch, t.hBlank, t.hTotal);
    std::printf("Vertical (lines)   %6d  %6d  %5d  %5d  %5d  %5d\n",
                t.vActive, t.vFrontPorch, t.vSyncWidth,
                t.vBackPorch, t.vBlank, t.vTotal);
    std::printf("\n");
    std::printf("Sync polarity : H%c V%c\n\n", t.hSyncPolarity, t.vSyncPolarity);
    printModeline(t);
}

void printHelp() {
    std::printf(
      "dtimings -- compute video timings (VESA GTF / CVT / CVT-RB / CVT-RB2, CTA-861 OVT)\n"
      "\n"
      "Usage:\n"
      "  dtimings <hpixels> <vlines> <refresh_hz> [options]\n"
      "\n"
      "Required positional arguments:\n"
      "  hpixels        Active horizontal pixels (e.g. 1920)\n"
      "  vlines         Active vertical lines   (e.g. 1080)\n"
      "  refresh_hz     Requested vertical refresh in Hz (e.g. 60)\n"
      "\n"
      "Optional arguments:\n"
      "  -s, --standard NAME     cvt (default) | rb | rb2 | gtf | ovt\n"
      "  -i, --interlaced        Produce interlaced timing (not for ovt)\n"
      "  -m, --margins           Add VESA image margins (1.8%%) (cvt/gtf only)\n"
      "  -v, --video-optimized   Apply 1000/1001 factor (rb2 / ovt)\n"
      "      --aspect W:H        Force aspect ratio for V-sync width lookup\n"
      "      --vsync N           Override V-sync width (lines)\n"
      "      --hpol +|-          Override H sync polarity\n"
      "      --vpol +|-          Override V sync polarity\n"
      "  -d, --detailed          Show full timing table (default: compact Modeline)\n"
      "  -h, --help              Show this help\n"
      "\n"
      "Examples:\n"
      "  dtimings 1920 1080 60                     # compact modeline\n"
      "  dtimings 1920 1080 60 --detailed          # full timing table\n"
      "  dtimings 3840 2160 60 --standard rb2\n"
      "  dtimings 1280 1024 75 --standard gtf\n"
      "  dtimings 1920 1080 60 --standard rb2 -v   # 59.94 Hz video-optimized\n"
      "  dtimings 1920 1080 60 --standard ovt      # CTA-861-I OVT\n"
      "\n"
      "Defaults:\n"
      "  standard = CVT (normal blanking), non-interlaced, no margins,\n"
      "  V-sync width picked from aspect ratio (CVT/RB), sync polarity from spec:\n"
      "    CVT / GTF    : H-, V+\n"
      "    CVT-RB / RB2 : H+, V-\n"
      "    OVT          : H+, V+\n");
}

bool parseAspect(const char* s, double& out) {
    int w = 0, h = 0;
    if (std::sscanf(s, "%d:%d", &w, &h) == 2 && w > 0 && h > 0) {
        out = double(w) / h;
        return true;
    }
    char* end = nullptr;
    double d = std::strtod(s, &end);
    if (end != s && d > 0.0) { out = d; return true; }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    // No arguments -> print usage documentation.
    if (argc < 2) {
        printHelp();
        return 0;
    }
    if (std::strcmp(argv[1], "-h") == 0 || std::strcmp(argv[1], "--help") == 0) {
        printHelp();
        return 0;
    }
    if (argc < 4) {
        std::fprintf(stderr,
            "error: need at least <hpixels> <vlines> <refresh_hz>\n"
            "run 'dtimings' with no arguments to see usage\n");
        return 2;
    }

    Options o;
    o.hPixels   = std::atoi(argv[1]);
    o.vLines    = std::atoi(argv[2]);
    o.refreshHz = std::atof(argv[3]);

    if (o.hPixels <= 0 || o.vLines <= 0 || o.refreshHz <= 0) {
        std::fprintf(stderr, "error: hpixels, vlines and refresh must be > 0\n");
        return 2;
    }

    for (int i = 4; i < argc; ++i) {
        std::string a = argv[i];
        auto needArg = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: %s requires a value\n", name);
                std::exit(2);
            }
            return argv[++i];
        };

        if      (a == "-s" || a == "--standard") {
            std::string v = needArg("--standard");
            if      (v == "cvt")                    o.standard = Standard::CVT;
            else if (v == "rb"  || v == "cvt-rb")   o.standard = Standard::CVT_RB;
            else if (v == "rb2" || v == "cvt-rb2")  o.standard = Standard::CVT_RB2;
            else if (v == "gtf")                    o.standard = Standard::GTF;
            else if (v == "ovt" || v == "cta-ovt")  o.standard = Standard::OVT;
            else { std::fprintf(stderr, "unknown standard '%s'\n", v.c_str()); return 2; }
        }
        else if (a == "-i" || a == "--interlaced")      o.interlaced = true;
        else if (a == "-m" || a == "--margins")         o.margins = true;
        else if (a == "-v" || a == "--video-optimized") o.videoOptimized = true;
        else if (a == "--aspect") {
            double asp;
            if (!parseAspect(needArg("--aspect"), asp)) {
                std::fprintf(stderr, "bad --aspect value\n"); return 2;
            }
            o.aspectOverride = asp;
        }
        else if (a == "--vsync")     o.vSyncOverride = std::atoi(needArg("--vsync"));
        else if (a == "--hpol")      o.hSyncPolarity = needArg("--hpol")[0];
        else if (a == "--vpol")      o.vSyncPolarity = needArg("--vpol")[0];
        else if (a == "-d" || a == "--detailed") o.detailed = true;
        else if (a == "-h" || a == "--help") { printHelp(); return 0; }
        else {
            std::fprintf(stderr, "unknown option: %s\n", a.c_str());
            return 2;
        }
    }

    Timing t;
    switch (o.standard) {
        case Standard::CVT:     t = computeCvtNormal(o);           break;
        case Standard::CVT_RB:  t = computeCvtReduced(o, false);   break;
        case Standard::CVT_RB2: t = computeCvtReduced(o, true);    break;
        case Standard::GTF:     t = computeGtf(o);                 break;
        case Standard::OVT:     t = computeOvt(o);                 break;
    }

    printTiming(t, o);
    return 0;
}
