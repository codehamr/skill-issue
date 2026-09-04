// =============================================================================
// render/render.inc  —  GL programs, VBOs, the shadow map, rig_upload, the
// "HARDLINE" SDF-typeface bake (font_bake).
// Proof: budget, shot battery. One TU: included once by game.c, in
// definition order — moving a definition out is a semantic change.
// Contract: rig_upload is the ONE writer that hands the sun/light rig to
// BOTH shader programs; shadow_update is the ONE builder of the sun shadow
// map, and shadow_tex_size the ONE answer to how wide it is.
// A world/scene vertex is VERT_FLOATS (12) —
// deliberately not widened for the surface/material work.
// =============================================================================
// ---------------------------------------------------------------------------
// "HARDLINE" — the UI typeface, procedural like everything else in this
// game.
// ---------------------------------------------------------------------------
static const int8_t HF_SIMPLEX[95][40] = {
  /*  32 ' ' */ {0,14},
  /*  33 '!' */ {5,10,5,21,5,7,-1,-1,5,1,5,1},
  /*  34 '"' */ {5,14,4,21,4,15,-1,-1,10,21,10,15},
  /*  35 '#' */ {11,19,6,21,6,0,-1,-1,11,21,11,0,-1,-1,2,14,15,14,-1,-1,2,7,15,7},
  /*  36 '$' */ {15,20,15,17,11,21,5,21,2,18,2,15,5,12,12,10,15,7,15,3,12,0,5,0,2,3,-1,-1,8,24,8,-3},
  /*  37 '%' */ {14,20,2,0,15,21,-1,-1,2,21,6,21,6,16,2,16,2,21,-1,-1,11,5,15,5,15,0,11,0,11,5},
  /*  38 '&' */ {13,21,15,0,5,10,3,12,3,18,6,21,9,21,12,18,12,15,3,7,3,3,6,0,10,0,15,5},
  /*  39 ''' */ {2,8,4,21,4,15},
  /*  40 '(' */ {4,12,10,25,6,20,6,-2,10,-7},
  /*  41 ')' */ {4,12,2,25,6,20,6,-2,2,-7},
  /*  42 '*' */ {8,16,8,20,8,12,-1,-1,4,18,12,14,-1,-1,4,14,12,18},
  /*  43 '+' */ {5,18,8,4,8,16,-1,-1,2,10,14,10},
  /*  44 ',' */ {2,10,5,2,3,-3},
  /*  45 '-' */ {2,16,3,10,13,10},
  /*  46 '.' */ {2,10,5,1,5,1},
  /*  47 '/' */ {2,16,3,-3,13,24},
  /*  48 '0' */ {12,20,2,4,2,17,6,21,11,21,15,17,15,4,11,0,6,0,2,4,-1,-1,7,6,10,15},
  /*  49 '1' */ {6,20,4,17,8,21,8,0,-1,-1,4,0,12,0},
  /*  50 '2' */ {7,20,2,17,6,21,11,21,15,17,15,12,2,0,15,0},
  /*  51 '3' */ {14,20,2,17,6,21,11,21,15,17,15,14,12,11,7,11,-1,-1,12,11,15,8,15,4,11,0,6,0,2,4},
  /*  52 '4' */ {4,20,12,0,12,21,3,6,16,6},
  /*  53 '5' */ {9,20,14,21,3,21,3,13,10,13,14,9,14,4,10,0,5,0,2,3},
  /*  54 '6' */ {11,20,13,21,7,21,2,16,2,4,6,0,11,0,15,4,15,7,11,11,6,11,2,7},
  /*  55 '7' */ {6,20,2,21,15,21,6,0,-1,-1,6,11,13,11},
  /*  56 '8' */ {18,20,6,11,3,14,3,18,6,21,11,21,14,18,14,14,11,11,6,11,-1,-1,6,11,2,7,2,3,5,0,12,0,15,3,15,7,11,11},
  /*  57 '9' */ {12,20,3,4,7,0,11,0,15,4,15,17,11,21,6,21,2,17,2,14,6,10,11,10,15,14},
  /*  58 ':' */ {5,10,5,2,5,2,-1,-1,5,13,5,13},
  /*  59 ';' */ {5,10,5,13,5,13,-1,-1,5,2,3,-3},
  /*  60 '<' */ {3,18,14,17,3,10,14,3},
  /*  61 '=' */ {5,18,2,13,14,13,-1,-1,2,7,14,7},
  /*  62 '>' */ {3,18,3,17,14,10,3,3},
  /*  63 '?' */ {10,18,2,18,5,21,10,21,13,18,13,14,9,10,9,7,-1,-1,9,1,9,1},
  /*  64 '@' */ {16,24,15,4,11,0,6,0,2,4,2,17,6,21,12,21,16,17,16,9,13,6,11,8,11,13,9,15,7,13,7,9,9,7},
  /*  65 'A' */ {7,20,2,0,6,21,11,21,15,0,-1,-1,4,7,13,7},
  /*  66 'B' */ {12,20,2,0,2,21,10,21,13,18,13,14,10,11,2,11,11,11,15,7,15,3,12,0,2,0},
  /*  67 'C' */ {8,20,15,17,11,21,6,21,2,17,2,4,6,0,11,0,15,4},
  /*  68 'D' */ {7,20,2,0,2,21,10,21,15,16,15,5,10,0,2,0},
  /*  69 'E' */ {7,19,15,0,2,0,2,21,15,21,-1,-1,2,11,12,11},
  /*  70 'F' */ {6,18,2,0,2,21,15,21,-1,-1,2,11,12,11},
  /*  71 'G' */ {10,21,15,17,11,21,6,21,2,17,2,4,6,0,11,0,15,4,15,9,9,9},
  /*  72 'H' */ {8,21,2,0,2,21,-1,-1,15,0,15,21,-1,-1,2,11,15,11},
  /*  73 'I' */ {2,9,4,0,4,21},
  /*  74 'J' */ {5,16,12,21,12,3,9,0,5,0,2,3},
  /*  75 'K' */ {8,20,2,0,2,21,-1,-1,15,21,2,9,-1,-1,6,12,15,0},
  /*  76 'L' */ {3,17,2,21,2,0,14,0},
  /*  77 'M' */ {5,24,2,0,2,21,10,7,18,21,18,0},
  /*  78 'N' */ {4,21,2,0,2,21,15,0,15,21},
  /*  79 'O' */ {9,21,2,4,2,17,6,21,11,21,15,17,15,4,11,0,6,0,2,4},
  /*  80 'P' */ {7,19,2,0,2,21,11,21,14,18,14,13,11,10,2,10},
  /*  81 'Q' */ {12,21,2,4,2,17,6,21,11,21,15,17,15,4,11,0,6,0,2,4,-1,-1,9,5,16,-2},
  /*  82 'R' */ {10,20,2,0,2,21,11,21,14,18,14,13,11,10,2,10,-1,-1,8,10,15,0},
  /*  83 'S' */ {12,20,15,17,11,21,5,21,2,18,2,15,5,12,12,10,15,7,15,3,12,0,5,0,2,3},
  /*  84 'T' */ {5,19,2,21,15,21,-1,-1,8,21,8,0},
  /*  85 'U' */ {6,21,2,21,2,4,6,0,11,0,15,4,15,21},
  /*  86 'V' */ {4,20,2,21,7,0,10,0,15,21},
  /*  87 'W' */ {5,24,2,21,6,0,10,13,14,0,18,21},
  /*  88 'X' */ {5,20,2,21,15,0,-1,-1,15,21,2,0},
  /*  89 'Y' */ {6,20,2,21,8,13,8,0,-1,-1,15,21,8,13},
  /*  90 'Z' */ {4,19,2,21,15,21,2,0,15,0},
  /*  91 '[' */ {4,12,9,25,4,25,4,-7,9,-7},
  /*  92 bslash */ {2,16,3,24,13,-3},
  /*  93 ']' */ {4,12,3,25,8,25,8,-7,3,-7},
  /*  94 '^' */ {3,16,4,15,8,21,12,15},
  /*  95 '_' */ {2,18,0,-4,16,-4},
  /*  96 '`' */ {2,8,3,22,6,18},
  /*  97 'a' */ {0,16},  // small caps: glyph_index maps to upper
  /*  98 'b' */ {0,16},  // small caps: glyph_index maps to upper
  /*  99 'c' */ {0,16},  // small caps: glyph_index maps to upper
  /* 100 'd' */ {0,16},  // small caps: glyph_index maps to upper
  /* 101 'e' */ {0,16},  // small caps: glyph_index maps to upper
  /* 102 'f' */ {0,16},  // small caps: glyph_index maps to upper
  /* 103 'g' */ {0,16},  // small caps: glyph_index maps to upper
  /* 104 'h' */ {0,16},  // small caps: glyph_index maps to upper
  /* 105 'i' */ {0,16},  // small caps: glyph_index maps to upper
  /* 106 'j' */ {0,16},  // small caps: glyph_index maps to upper
  /* 107 'k' */ {0,16},  // small caps: glyph_index maps to upper
  /* 108 'l' */ {0,16},  // small caps: glyph_index maps to upper
  /* 109 'm' */ {0,16},  // small caps: glyph_index maps to upper
  /* 110 'n' */ {0,16},  // small caps: glyph_index maps to upper
  /* 111 'o' */ {0,16},  // small caps: glyph_index maps to upper
  /* 112 'p' */ {0,16},  // small caps: glyph_index maps to upper
  /* 113 'q' */ {0,16},  // small caps: glyph_index maps to upper
  /* 114 'r' */ {0,16},  // small caps: glyph_index maps to upper
  /* 115 's' */ {0,16},  // small caps: glyph_index maps to upper
  /* 116 't' */ {0,16},  // small caps: glyph_index maps to upper
  /* 117 'u' */ {0,16},  // small caps: glyph_index maps to upper
  /* 118 'v' */ {0,16},  // small caps: glyph_index maps to upper
  /* 119 'w' */ {0,16},  // small caps: glyph_index maps to upper
  /* 120 'x' */ {0,16},  // small caps: glyph_index maps to upper
  /* 121 'y' */ {0,16},  // small caps: glyph_index maps to upper
  /* 122 'z' */ {0,16},  // small caps: glyph_index maps to upper
  /* 123 '{' */ {7,12,10,25,7,23,7,12,4,9,7,6,7,-5,10,-7},
  /* 124 '|' */ {2,8,4,25,4,-7},
  /* 125 '}' */ {7,12,2,25,5,23,5,12,8,9,5,6,5,-5,2,-7},
  /* 126 '~' */ {4,18,2,8,6,12,10,8,14,12},
};

// Icon strokes in the same format, addressed as string bytes \x01..\x05: chevron (list
// marker), AR and SR (killfeed weapon glyphs), headshot, and a middle dot.
static const int8_t HF_EXTRA[5][32] = {
  /* \x01 chevron  */ {3,14, 4,17,11,10,4,3},
  /* \x02 AR       */ {12,30, 1,7,4,10,28,10,-1,-1,9,10,8,5,-1,-1,14,10,13,4,-1,-1,21,10,21,14},
  /* \x03 SR       */ {15,31, 1,7,4,10,29,10,-1,-1,9,10,8,5,-1,-1,12,13,19,13,-1,-1,13,13,13,10,-1,-1,18,13,18,10},
  /* \x04 headshot */ {14,16,5,10,3,12,3,17,5,19,9,19,11,17,11,12,9,10,5,10,-1,-1,2,4,5,7,11,7,14,4},
  /* \x05 mid dot  */ {2,10, 5,9,5,9},
};

#define HF_ASCII   95
#define HF_ICONS   5
#define HF_GLYPHS  (HF_ASCII + HF_ICONS)   // stroke glyphs
#define HF_SOLID   HF_GLYPHS               // all-255 cell: solid fills
#define HF_SOFT    (HF_GLYPHS + 1)         // radial falloff cell: glow/shadow
#define HF_CELLS   (HF_GLYPHS + 2)
#define HF_COLS    16
#define HF_ROWS    ((HF_CELLS + HF_COLS - 1) / HF_COLS)
#define HF_CELL    72                      // atlas cell size, px
#define HF_TEX_W   (HF_COLS * HF_CELL)
#define HF_TEX_H   (HF_ROWS * HF_CELL)
#define HF_PX      1.6f                    // atlas px per font unit
#define HF_DMAX    10.0f                   // encoded distance range, atlas px
#define HF_BASE    50.0f                   // baseline row inside a cell
#define HF_MARG    10.0f                   // ink origin inset inside a cell
#define HF_CAP     21.0f                   // cap height in font units

// Text weights are SDF thresholds: 1 - stroke_radius_px/HF_DMAX. Lower =
// bolder. UIM_RAW bypasses the field (solid slot, soft glow slot).
#define TXT_REG    0.76f
#define TXT_BOLD   0.66f
#define TXT_BLACK  0.56f
#define UIM_RAW    2.0f

typedef struct {                 // per-glyph metrics, filled by font_bake
  float adv;                     // advance, font units (digits: tabular)
  float qx0, qy0, qx1, qy1;      // quad extents, font units (ink + spill)
  float u0, v0, u1, v1;          // atlas UVs of exactly that quad
} hf_gm_t;
static hf_gm_t g_hfm[HF_GLYPHS];
static float g_hf_solid_u, g_hf_solid_v;   // centre texel of the solid cell
static float g_hf_soft[4];                 // u0 v0 u1 v1 of the soft cell

static const int8_t *hf_glyph_data(int gi) {
  return gi < HF_ASCII ? HF_SIMPLEX[gi] : HF_EXTRA[gi - HF_ASCII];
}

// SQUARE-capped stroke distance: distance to the segment measured in its own frame with
// the cap planes cutting flat at the ends.
static float hf_seg_dist(float px, float py, float ax, float ay,
                         float bx, float by) {
  float dx = bx - ax, dy = by - ay;
  float l = sqrtf(dx * dx + dy * dy);
  if (l < 1e-6f) {  // a point stroke becomes a square dot
    float ex = fabsf(px - ax), ey = fabsf(py - ay);
    return fmaxf(ex, ey);
  }
  float ux = dx / l, uy = dy / l;
  float lx = (px - ax) * ux + (py - ay) * uy;   // along the stroke
  float ly = (px - ax) * -uy + (py - ay) * ux;  // across it
  float ox = fmaxf(fmaxf(-lx, lx - l), 0.0f);   // outside the cap planes
  // EUCLIDEAN distance with flat cap planes (a Chebyshev max() here warps
  // the field and the bold weights fray): end faces cut flat, only the four
  // cap corners keep a stroke-radius round — machined, not bullnosed.
  return sqrtf(ox * ox + ly * ly);
}

static void font_bake(uint8_t *atlas) {
  memset(atlas, 0, (size_t)HF_TEX_W * HF_TEX_H);
  const float spill = HF_DMAX / HF_PX;   // how far the field reaches, font units
  for (int gi = 0; gi < HF_GLYPHS; gi++) {
    const int8_t *d = hf_glyph_data(gi);
    int n = d[0];
    int cx = (gi % HF_COLS) * HF_CELL, cy = (gi / HF_COLS) * HF_CELL;
    hf_gm_t *m = &g_hfm[gi];
    memset(m, 0, sizeof *m);
    m->adv = (float)d[1];
    if (gi >= 16 && gi <= 25) m->adv = 22.0f;   // tabular digits '0'..'9'
    if (n == 0) continue;                       // space: advance only
    float ix0 = 127, iy0 = 127, ix1 = -127, iy1 = -127;
    for (int i = 0; i < n; i++) {
      float px = d[2 + 2 * i], py = d[3 + 2 * i];
      if (px == -1 && py == -1) continue;
      if (px < ix0) ix0 = px;
      if (px > ix1) ix1 = px;
      if (py < iy0) iy0 = py;
      if (py > iy1) iy1 = py;
    }
    m->qx0 = ix0 - spill; m->qx1 = ix1 + spill;
    m->qy0 = iy0 - spill; m->qy1 = iy1 + spill;
    int ax0 = cx + (int)floorf(HF_MARG + m->qx0 * HF_PX);
    int ax1 = cx + (int)ceilf(HF_MARG + m->qx1 * HF_PX);
    int ay0 = cy + (int)floorf(HF_BASE - m->qy1 * HF_PX);
    int ay1 = cy + (int)ceilf(HF_BASE - m->qy0 * HF_PX);
    if (ax0 < cx) ax0 = cx;
    if (ay0 < cy) ay0 = cy;
    if (ax1 > cx + HF_CELL) ax1 = cx + HF_CELL;
    if (ay1 > cy + HF_CELL) ay1 = cy + HF_CELL;
    for (int y = ay0; y < ay1; y++)
      for (int x = ax0; x < ax1; x++) {
        float fx = ((float)x + 0.5f - (float)cx - HF_MARG) / HF_PX;
        float fy = (HF_BASE - ((float)y + 0.5f - (float)cy)) / HF_PX;
        float dmin = 1e9f;
        float lx = 0, ly = 0;
        int pen = 0;
        for (int i = 0; i < n; i++) {
          float sx = d[2 + 2 * i], sy = d[3 + 2 * i];
          if (sx == -1 && sy == -1) { pen = 0; continue; }
          if (pen) {
            float sd = hf_seg_dist(fx, fy, lx, ly, sx, sy);
            if (sd < dmin) dmin = sd;
          }
          lx = sx; ly = sy; pen = 1;
        }
        float v = 1.0f - dmin * HF_PX / HF_DMAX;
        if (v > 0) atlas[y * HF_TEX_W + x] = (uint8_t)(v * 255.0f + 0.5f);
      }
    m->u0 = ((float)cx + HF_MARG + m->qx0 * HF_PX) / (float)HF_TEX_W;
    m->u1 = ((float)cx + HF_MARG + m->qx1 * HF_PX) / (float)HF_TEX_W;
    m->v0 = ((float)cy + HF_BASE - m->qy1 * HF_PX) / (float)HF_TEX_H;
    m->v1 = ((float)cy + HF_BASE - m->qy0 * HF_PX) / (float)HF_TEX_H;
  }
  {  // solid cell: every UI rect/line/disk samples its centre texel
    int cx = (HF_SOLID % HF_COLS) * HF_CELL, cy = (HF_SOLID / HF_COLS) * HF_CELL;
    for (int y = 0; y < HF_CELL; y++)
      for (int x = 0; x < HF_CELL; x++)
        atlas[(cy + y) * HF_TEX_W + cx + x] = 255;
    g_hf_solid_u = ((float)cx + HF_CELL * 0.5f) / (float)HF_TEX_W;
    g_hf_solid_v = ((float)cy + HF_CELL * 0.5f) / (float)HF_TEX_H;
  }
  {  // soft cell: quadratic radial falloff — one quad = one glow/shadow sprite
    int cx = (HF_SOFT % HF_COLS) * HF_CELL, cy = (HF_SOFT / HF_COLS) * HF_CELL;
    float half = HF_CELL * 0.5f, rmax = half - 1.0f;
    for (int y = 0; y < HF_CELL; y++)
      for (int x = 0; x < HF_CELL; x++) {
        float dx = (float)x + 0.5f - half, dy = (float)y + 0.5f - half;
        float v = 1.0f - sqrtf(dx * dx + dy * dy) / rmax;
        if (v < 0) v = 0;
        atlas[(cy + y) * HF_TEX_W + cx + x] = (uint8_t)(v * v * 255.0f + 0.5f);
      }
    g_hf_soft[0] = ((float)cx + 1.0f) / (float)HF_TEX_W;
    g_hf_soft[1] = ((float)cy + 1.0f) / (float)HF_TEX_H;
    g_hf_soft[2] = ((float)cx + HF_CELL - 1.0f) / (float)HF_TEX_W;
    g_hf_soft[3] = ((float)cy + HF_CELL - 1.0f) / (float)HF_TEX_H;
  }
}

static float g_scope_r, g_scope_cx, g_scope_cy;  // scope disc, derived per frame
static int g_hud_visible = 1;  // harness capture switch; menus remain visible
// The harness's debug picture: 0 = the frame, 1 = the AO buffer (`dbgview`).
static int g_dbg_view = 0;
// One map over the whole 48 m arena: 4096 px gives ~1.3 cm texels, which is fine
// enough that a figure self-shadows its own arms. HIGH is that map; LOW halves the
// side, so a quarter of the fill and a texel a figure's fingers no longer resolve
// on — a real quality step, which is why SHADOWS is a three-state cycle and not a
// flag. EVERY reader asks this function: the viewport, the texel the PCSS penumbra
// is measured in and the texture itself are one number, and a second copy would be
// a shadow filtered for a map that is not the one on the card.
#define SHADOW_TEX_HIGH 4096
#define SHADOW_TEX_LOW  2048
static int shadow_tex_size(void) {
  return g_cfg.shadows == 2 ? SHADOW_TEX_HIGH : SHADOW_TEX_LOW;
}
// The light-space box is no longer a constant: it is fitted to the elevation by
// sun_set (g_sun.sh_half / sh_depth / sh_centre), because a fixed 31 m half was
// the tight fit for ONE sun angle and this arena no longer has one.
// World depth range.
#define ZNEAR         0.12f
#define ZFAR          140.0f
#define SCOPE_OCU_Z   -0.052f // gun-space z of the ocular glass
#define SCOPE_OCU_R    0.0205f // ocular bell radius
#define SCOPE_TUBE_R   0.0142f // main tube radius
#define SCOPE_SEGS     96   // the disc rim must out-resolve the ocular's own
#define SCOPE_FOV_DEG 9.0f  // ~6x against a 55 deg hip view
// Where on the ADS ramp the scope picture starts to open.
#define SCOPE_ADS_OPEN 0.55f

// The PiP aperture is a presentation contract shared by the renderer and its
// optical proof. Keep alignment, ADS threshold and easing in one expression.
static float scope_pip_radius(float scope, float vw, float vh) {
  float off = sqrtf((g_scope_cx - vw * 0.5f) * (g_scope_cx - vw * 0.5f) +
                    (g_scope_cy - vh * 0.5f) * (g_scope_cy - vh * 0.5f));
  float aligned = 1.0f - f_clamp((off - g_scope_r * 0.30f) /
                                 fmaxf(1.0f, g_scope_r * 0.55f), 0.0f, 1.0f);
  return g_scope_r *
         smooth01(f_clamp((scope - SCOPE_ADS_OPEN) / 0.42f, 0.0f, 1.0f)) *
         aligned;
}

// Every uniform GLSL_ENV declares, for one program. One struct, so adding a
// light to the rig cannot be half-wired.
typedef struct {
  GLint sun, suncol, skytop, skyhor, skygnd, expo, disc, atmo, haze, hazes;
  GLint ambd, ambu, bounce, vmfill, vmrim, ambs, sunh, look, bounces;
} lightloc_t;

// THE LOOK, three of the four components of the vec4 every tonemapping program
// reads: curve (0 ACES, 1 AgX), contrast, saturation. The fourth is BRIGHTNESS
// and it is NOT mirrored here — rig_upload reads g_cfg.brightness straight into
// uLook.w, so the config key stays the one copy of the player's exposure and a
// graded A/B frame still renders at the exposure that is configured. The harness
// `grade` command overrides these three; they are the shipped values.
// ACES stays the curve: AgX was measured flatter on this palette (the sunset
// went grey, the landmark reds dulled) even with the same grade on top. The
// grade is a touch of contrast about mid-grey and a touch of saturation —
// what the old vignette's darkening had been standing in for.
static float g_look[3] = {0.0f, 1.03f, 1.10f};

typedef struct {
  int    width, height;
  GLuint prog_world, prog_hud, prog_sky, prog_depth;
  GLint  u_vp, u_eye, u_detail, u_emis, u_screen, u_tex, u_img;
  GLint  u_lightvp, u_smtexel, u_shadow, u_smstep, u_sm, u_dvp;
  GLint  u_vm, u_fill, u_rim, u_muzc, u_muzl, u_muzp;
  GLint  u_mdlo, u_mdlx, u_mdlz;           // the weapon's own frame
  GLint  u_surf;                           // the floor's theme treatment
  // TWO PROGRAMS READ THE SAME LIGHT. GLSL_ENV is shared source, so every one of
  // these is declared once, but a uniform location belongs to a PROGRAM: the
  // world and the sky each need their own handle to the same value, and a single
  // one used for both is a sky standing in different weather from the arena
  // under it. rig_locs fills one of these per program and rig_upload is the ONE
  // writer, so the two cannot drift however many uniforms the rig grows.
  lightloc_t rig_world, rig_sky;
  GLuint sm_fbo, sm_tex;             // sun shadow map (depth only)
  int    sm_built;                   // side of the map on the card, 0 = none yet
  mat4   light_vp;
  GLuint scope_fbo, scope_tex, scope_depth;  // picture-in-picture scope target
  int    scope_size;
  GLint  u_sky_fwd, u_sky_right, u_sky_up, u_sky_tan, u_sky_wind, u_sky_cov;
  GLint  u_sky_sunxz, u_sky_ridge;
  GLuint vao_world, vbo_world, vao_hud, vbo_hud, vao_sky, font_tex;
  GLuint vao_scene, vbo_scene;   // per-frame dynamic 3D batch (bot, VFX, gun)
  int    world_verts;
  // THE SHADOW PASS DRAWS A PREFIX, NOT THE WHOLE BUFFER. The solids come first and the
  // decor after, and only the solids cast: a shadow map is 4096 over 62 m, i.e. 15 mm a
  // texel, and a 22 mm grass blade at that resolution is not a shadow, it is one texel
  // of noise flickering under the figure standing on it.
  int    world_cast_verts, world_decor_verts;
  GLuint present_fbo;               // frame destination: 0 on Windows, harness FBO on Linux
  GLuint msaa_fbo, msaa_rb[2];      // multisampled scene target, msaa_fbo 0 = MSAA off
  int    frame_resolved;            // this frame's resolve has happened; see msaa_resolve
  int    msaa_cfg, msaa_w, msaa_h;  // setting/size the MSAA target was built for
  GLuint blur_fbo[2], blur_tex[2];  // quarter-res ping-pong: the frosted menu
  GLuint prog_blur;                 // dual-Kawase tap (see ui_menu_backdrop)
  GLint  u_blur_tex, u_blur_px;
  GLuint prog_glare;                // bloom + light shafts (see glare_pass)
  GLint  u_gl_tex, u_gl_sun, u_gl_knee, u_gl_gain, u_gl_mode, u_gl_grade;
  GLint  u_gl_scene;                // the composite's read of the scene target
  // Quarter-resolution shadow-map volume integration. The output reuses the
  // glare ping-pong pair and therefore owns no render target of its own.
  GLuint prog_volume;
  GLint  u_vol_depth, u_vol_shadow, u_vol_invvp, u_vol_lightvp;
  GLint  u_vol_eye, u_vol_sundir, u_vol_suncol, u_vol_atmo, u_vol_p;
  int    blur_w, blur_h;            // size the blur chain was built for
  // THE SCENE TARGET: where the picture is built when MSAA is off, and where the
  // multisample target resolves to when it is on. Its colour is what the
  // composite reads; its depth is the PREPASS's (pre_fbo shares the texture),
  // so the two FBOs are one depth seen twice. scene_fbo == 0 never happens
  // past renderer_init — an RGBA8 + depth24 target is universal on GL 3.3.
  GLuint scene_fbo, scene_tex, depth_tex;
  GLuint pre_fbo, nrm_tex;          // geometry prepass: world-space normals + depth
  GLuint prog_pre;                  // the prepass program (PRE_VS/PRE_FS)
  GLint  u_pre_vp;
  // Screen-space ambient occlusion, half resolution, ping-pong for the blur.
  GLuint ao_fbo[2], ao_tex[2];
  int    ao_w, ao_h, scene_w, scene_h;
  GLuint prog_ssao, prog_aoblur;
  GLint  u_ao_depth, u_ao_nrm, u_ao_invvp, u_ao_eye, u_ao_scr, u_ao_z, u_ao_i;
  GLint  u_aob_tex, u_aob_px;
  GLint  u_ao, u_aop;               // the world pass's read of the AO texture
  GLint  u_predepth, u_skycut;      // the dust pass's sky test
  GLint  u_gl_aa;                   // the composite's FXAA switch + texel
  // The shadow map seen WITHOUT the compare (unit 2, a sampler object over the
  // same texture) for the PCSS blocker search, and the penumbra constants.
  GLuint sm_raw_sampler;
  GLint  u_smraw, u_smpcss;
  // The bloom chain: four levels from a quarter of the frame down.
  GLuint bloom_fbo[4], bloom_tex[4];
  int    bloom_w[4], bloom_h[4];
  GLuint prog_bdown, prog_bup;
  GLint  u_bd_tex, u_bd_px, u_bd_first, u_bu_tex, u_bu_px, u_gl_bloom;
  // Screen-space reflections for the standing water: the opaque picture
  // copied out (pic_tex), the march's result (ssr_tex), and the two programs.
  GLuint pic_fbo, pic_tex, ssr_fbo, ssr_tex;
  GLuint prog_ssr, prog_ssr_apply;
  GLint  u_pre_pool;
  GLint  u_ssr_depth, u_ssr_nrm, u_ssr_pic, u_ssr_invvp, u_ssr_vp, u_ssr_eye, u_ssr_z;
  GLint  u_sa_ssr, u_sa_nrm, u_sa_depth, u_sa_invvp, u_sa_eye, u_sa_px;
} renderer_t;

static renderer_t R;

// THE SUN SHADOW MAP, built here and nowhere else — the renderer's init calls this
// for the first build and the frame calls it again, so the SHADOWS setting takes
// effect on the next frame with no platform hook (the msaa_update idiom: remember
// the size that was built, delete and recreate when it moves).
// The map is kept even with shadows OFF, at the low size: units 0 and 2 belong to
// it for the life of the program, and a unit holding no texture under a declared
// sampler2DShadow is undefined. Shadows off skips the PASS and the READ (uShadow),
// never the binding — so only the texture's FORMAT matters there, never its
// contents: sun_shadow returns before it samples, and clearing a map nothing
// reads would be a full-size fill bought for nothing.
static void shadow_update(void) {
  int want = shadow_tex_size();
  if (R.sm_built == want) return;
  if (R.sm_fbo) { glDeleteFramebuffers(1, &R.sm_fbo); R.sm_fbo = 0; }
  if (R.sm_tex) { glDeleteTextures(1, &R.sm_tex); R.sm_tex = 0; }
  R.sm_built = want;
  glGenTextures(1, &R.sm_tex);
  glBindTexture(GL_TEXTURE_2D, R.sm_tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, want, want, 0,
               GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
  {  // border = "nothing in front of the light", so outside the map is fully lit
    const float lit[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, lit);
  }
  glGenFramebuffers(1, &R.sm_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, R.sm_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, R.sm_tex, 0);
  glDrawBuffer(GL_NONE);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    R.sm_fbo = 0;   // unshadowed game rather than no game
  glBindFramebuffer(GL_FRAMEBUFFER, R.present_fbo);
  // The same map on unit 2 through a sampler object with the compare OFF: the
  // PCSS blocker search wants depths, not verdicts. A sampler object's state
  // overrides the texture's own, so one texture serves both readers. Unit 2
  // belongs to this binding for the life of the program; nothing else uses it.
  // The sampler outlives every rebuild — it is state of the UNIT — but the
  // texture under it is new each time, so the bind is repeated here.
  if (!R.sm_raw_sampler) {
    glGenSamplers(1, &R.sm_raw_sampler);
    glSamplerParameteri(R.sm_raw_sampler, GL_TEXTURE_COMPARE_MODE, 0 /* GL_NONE */);
    glSamplerParameteri(R.sm_raw_sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glSamplerParameteri(R.sm_raw_sampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glSamplerParameteri(R.sm_raw_sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glSamplerParameteri(R.sm_raw_sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
  }
  gl_active_texture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, R.sm_tex);
  glBindSampler(2, R.sm_raw_sampler);
  gl_active_texture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, R.sm_tex);   // unit 0's lit-pass binding
}

static GLuint gl_compile(GLenum type, const char *src) {
  GLuint sh = glCreateShader(type);
  glShaderSource(sh, 1, &src, NULL);
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetShaderInfoLog(sh, sizeof log, NULL, log);
    fatal(log);
  }
  return sh;
}

static lightloc_t rig_locs(GLuint p) {
  return (lightloc_t){
    .sun    = glGetUniformLocation(p, "SUN_DIR"),
    .suncol = glGetUniformLocation(p, "SUN_COL"),
    .skytop = glGetUniformLocation(p, "SKY_TOP"),
    .skyhor = glGetUniformLocation(p, "SKY_HOR"),
    .skygnd = glGetUniformLocation(p, "SKY_GND"),
    .expo   = glGetUniformLocation(p, "EXPOSURE"),
    .disc   = glGetUniformLocation(p, "SUN_DISC"),
    .atmo   = glGetUniformLocation(p, "uAtmo"),
    .haze   = glGetUniformLocation(p, "uHaze"),
    .hazes  = glGetUniformLocation(p, "uHazeS"),
    .ambd   = glGetUniformLocation(p, "uAmbD"),
    .ambu   = glGetUniformLocation(p, "uAmbU"),
    .bounce = glGetUniformLocation(p, "uBounce"),
    .vmfill = glGetUniformLocation(p, "VM_FILL"),
    .vmrim  = glGetUniformLocation(p, "VM_RIM"),
    .ambs   = glGetUniformLocation(p, "uAmbS"),
    .sunh   = glGetUniformLocation(p, "uSunH"),
    .look   = glGetUniformLocation(p, "uLook"),
    .bounces = glGetUniformLocation(p, "uBounceS"),
  };
}

// Luminance, the one weighting. Used to re-normalize the hero rig below.
static float v3_luma(v3 c) {
  return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
}

// THE ONE WRITER. Call it with the program already bound; every consumer of the
// light gets the whole rig or none of it, which is what stops a sky pass from
// running one frame behind the world's sun.
static void rig_upload(const lightloc_t *L) {
  const sun_t *S = &g_sun;
  glUniform3f(L->sun, S->dir.x, S->dir.y, S->dir.z);
  glUniform3f(L->suncol, S->col.x, S->col.y, S->col.z);
  glUniform3f(L->skytop, S->sky_top.x, S->sky_top.y, S->sky_top.z);
  glUniform3f(L->skyhor, S->sky_hor.x, S->sky_hor.y, S->sky_hor.z);
  glUniform3f(L->skygnd, S->sky_gnd.x, S->sky_gnd.y, S->sky_gnd.z);
  glUniform1f(L->expo, S->exposure);
  glUniform1f(L->disc, S->disc);
  v3 at = atmo_tint();
  glUniform4f(L->atmo, at.x, at.y, at.z, atmo_haze());
  // The two halves of haze_rad that are constant for the whole draw, folded
  // here so the fragment shader does not rebuild them three times a pixel.
  glUniform3f(L->haze, at.x * (S->col.x * 0.055f + S->sky_hor.x * 0.30f),
                       at.y * (S->col.y * 0.055f + S->sky_hor.y * 0.30f),
                       at.z * (S->col.z * 0.055f + S->sky_hor.z * 0.30f));
  glUniform3f(L->hazes, at.x * S->col.x * 0.95f, at.y * S->col.y * 0.95f,
                        at.z * S->col.z * 0.95f);
  // Diffuse irradiance, both ends of the hemisphere lobe. Sun-facing AO is only
  // partly applied at the call site — a crease is dark in ambient light, not in
  // a direct beam.
  glUniform3f(L->ambd, S->sky_gnd.x * 0.44f, S->sky_gnd.y * 0.44f,
                       S->sky_gnd.z * 0.44f);
  { v3 up = v3_lerp(S->sky_hor, S->sky_top, 0.45f);
    glUniform3f(L->ambu, up.x * 0.40f, up.y * 0.40f, up.z * 0.40f); }
  glUniform3f(L->bounce, S->sky_gnd.x * 1.35f, S->sky_gnd.y * 1.35f,
                         S->sky_gnd.z * 1.35f);
  // The sun-side skylight: the sun through the dust, a twentieth of it, on
  // faces turned toward its azimuth (see the ambient in WORLD_FS).
  { v3 sh = v3_norm((v3){{S->dir.x, 0.0f, S->dir.z}});
    glUniform3f(L->sunh, sh.x, sh.y, sh.z);
    glUniform3f(L->ambs, at.x * S->col.x * 0.05f, at.y * S->col.y * 0.05f,
                         at.z * S->col.z * 0.05f); }
  // w = the exposure multiplier, applied before the curve in tonemap(). Read
  // from the config at the upload, not threaded: the key is the only copy.
  glUniform4f(L->look, g_look[0], g_look[1], g_look[2], g_cfg.brightness);
  // The sun off the floor: the floor's linear albedo times the sun's
  // irradiance on it (sin of the elevation), a third of the half-hemisphere
  // a wall would see if every metre of that floor were lit.
  { v3 g = g_theme.ground;
    float k = 0.30f * fmaxf(S->dir.y, 0.0f);
    glUniform3f(L->bounces, g.x * g.x * S->col.x * k, g.y * g.y * S->col.y * k,
                            g.z * g.z * S->col.z * k); }
  // The viewmodel's fill and rim: sky hue and sun hue, both re-normalized to a
  // fixed DISPLAY strength (see VM_FILL's comment). The 1.22 / 1.35 reproduce
  // the two constants this replaced to within 3% at a midday sun, so a weapon
  // photographed at noon is the weapon that was tuned.
  { v3 sky = v3_lerp(S->sky_hor, S->sky_top, 0.35f);
    // THE FILL KEEPS ONLY HALF THE SKY'S CHROMA, taken out on the luminance
    // axis. Physically the fill IS the sky, and at dusk the sky is a saturated
    // violet — which lands on the one blue object in the frame, the player's
    // own sleeve, and multiplies. Measured on the sleeve patch: 0.37
    // saturation with the sky's full chroma against 0.29 in the arena's own
    // light. The RIM keeps all of its colour, because the rim is the sun and
    // the sun's warmth is the half of this rig that is supposed to read.
    float sl0 = v3_luma(sky);
    sky = v3_lerp((v3){{sl0, sl0, sl0}}, sky, 0.5f);
    // `luma(col) * exposure` is the arena's light AFTER eye adaptation, i.e.
    // what the player actually lost; 2.556 * 0.40 is that same quantity for the
    // midday rig these constants were tuned under. The 0.7 power hands back
    // about half of the loss.
    float world = v3_luma(S->col) * S->exposure / (2.556f * 0.40f);
    float k = 0.40f * powf(f_clamp(world, 0.05f, 4.0f), 0.70f)
            / fmaxf(S->exposure, 0.05f);
    float fs = 1.22f * k / fmaxf(v3_luma(sky), 0.02f);
    float rs = 1.35f * k / fmaxf(v3_luma(S->col), 0.02f);
    glUniform3f(L->vmfill, sky.x * fs, sky.y * fs, sky.z * fs);
    glUniform3f(L->vmrim, S->col.x * rs, S->col.y * rs, S->col.z * rs); }
}

static GLuint gl_program(const char *vs, const char *fs) {
  GLuint p = glCreateProgram();
  GLuint v = gl_compile(GL_VERTEX_SHADER, vs), f = gl_compile(GL_FRAGMENT_SHADER, fs);
  glAttachShader(p, v);
  glAttachShader(p, f);
  glLinkProgram(p);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetProgramInfoLog(p, sizeof log, NULL, log);
    fatal(log);
  }
  glDeleteShader(v);
  glDeleteShader(f);
  return p;
}

// World mesh: 36 verts per solid + 6 for the ground quad, 12 floats each
// (position, normal, albedo, material).
#define VERT_FLOATS 12

// --- surface state: what the next vertices are MADE OF ---------------------
// Roughness and metalness belong to a PART, not to a vertex, and threading them through
// every scene_box/scene_quad/fig_chain call would touch a hundred call sites to say
// "still steel".
static float g_mat_rough = 0.72f, g_mat_metal = 0.0f;
static float g_ao_base = 0.0f, g_ao_rate = 0.0f, g_ao_min = 1.0f;

static void mat_set(float rough, float metal) {
  g_mat_rough = rough;
  g_mat_metal = metal;
}
// THE TOOL DIRECTION RIDES IN THE SIGN OF THE METALNESS, and that is a free channel
// rather than a trick. A machined surface's highlight STRETCHES, and which way it
// stretches is decided by which way the tool ran — a turned barrel throws its streak
// along the bore, a broached receiver flat throws one across it — so the shader needs
// exactly one bit per surface. The vertex is 12 floats and was deliberately not
// widened; every writer in this file authors metalness in [0,1], so the sign is unused.
//
// ROUGHNESS CANNOT CARRY THIS BIT, and that is not a preference: cham_arris scales a
// part's roughness by MAT_ARRIS_R for its bevel bands, which walks a broached
// receiver's own arris straight across any threshold that separated it from the turned
// family — so the two opposite lobe directions would be decided, per pixel, by the
// interpolator's last bit on the most visible edge of the weapon.
static void mat_lathe(float rough, float metal) { mat_set(rough, -metal); }
static void ao_set(float base, float rate, float min_v) {
  g_ao_base = base;
  g_ao_rate = rate;
  g_ao_min = min_v;
}
static void ao_off(void) { g_ao_min = 1.0f; g_ao_rate = 0.0f; }

// The world's own occlusion ramp for a solid: concentrated in the bottom half metre,
// because spread over 1.7 m it is an 8% wash with nothing at the contact, which is the
// one place occlusion shows.
#define WORLD_AO_RATE (1.0f / 0.55f)
#define WORLD_AO_MIN  0.55f
// ...and the two finishes the world pass gives its own surfaces, for the same
// reason: a decal whose roughness does not match its host has a silhouette at
// GRAZING angles, which is exactly where a wall you are running past is seen.
#define WORLD_SOLID_R  0.72f   // cover, crates, the shell

// Cavity term, set per band by fig_chain from fnode_t.ao.
static float g_ao_mul = 1.0f;

// Named finishes, so "gun metal" means the same thing on the bot's rifle, on the
// viewmodel and on the corpse's dropped weapon.
#define MAT_CLOTH_R    0.88f   // cotton ripstop: no highlight, only sheen
#define MAT_GEAR_R     0.72f   // coyote nylon: carrier, pouches, webbing
#define MAT_HELM_R     0.42f   // composite shell: broad semi-gloss
#define MAT_RUBBER_R   0.90f   // knee / elbow pads
#define MAT_LEATHER_R  0.50f   // boots
// A GLOVE is not a boot.
#define MAT_GLOVE_R    0.72f
// Glass-filled nylon furniture is not semi-gloss.
#define MAT_POLY_R     0.62f   // weapon furniture: grips, handguards, stocks
// A METAL HAS NO DIFFUSE, so its roughness is not a taste setting, it is the only thing
// that decides whether the form is modelled at all.
#define MAT_PARK_R     0.44f   // parkerized receiver: matte metal
#define MAT_PARK_M     0.90f
// The other side of the same argument: a turned barrel is NOT a mirror, but it
// is the bright family, and 0.17 put it close enough to one that the glossy
// environment lobe collapsed to a point nothing could catch. 0.24 keeps the
// glint tight enough to read as a different material from the receiver beside
// it and wide enough that a turning mark has something to modulate.
#define MAT_STEEL_R    0.24f   // barrel, bolt, turrets: bright metal
#define MAT_STEEL_M    1.00f
#define MAT_GLASS_R    0.05f
// Hard-anodized 7075: the receiver/handguard family. Between the two above, and it
// has to BE between them — an AR is mostly aluminium, and while the receivers were
// authored in the polymer family the four biggest parts on the weapon shared one
// value with the grip.
// 0.40, not 0.34: at 0.34 the receiver's raking macro clipped 27.8% of its own crop to
// white once the F0 correction landed. A Type III hard anodize is a porous oxide and
// every published gloss figure for a service finish is in the flat band; 0.34 was a
// semi-gloss number on the one part with the most sky above it.
#define MAT_ALLOY_R    0.40f
#define MAT_ALLOY_M    0.92f
#define MAT_ALLOY2_R   0.425f   // the second run, one step matter again
// AN ARRIS IS A MATERIAL, NOT A SHADE OF THE ONE IT JOINS. A broken edge is what every
// hand and every doorway has burnished, so it is SMOOTHER than either face it joins —
// and on a metal a conversion coating is thinnest there, often gone, so what shows is
// the substrate at three times the reflectance. Both halves are authored into vertices
// that ALREADY EXIST (scene_box_cham's bevel bands, fig_cap's), so this is the one
// surface change in the whole pass that costs exactly zero fragment instructions.
#define MAT_ARRIS_R    0.55f   // ...of the part's own roughness
#define MAT_ARRIS_MIX  0.26f   // ...of the way to the bare substrate, on a METAL
// Bare steel's colour, as ONE constant with two readers: gun_build authors the barrel
// and the bolt with it, and scene_box_cham/fig_cap wear a metal's edges toward it.
// Written out rather than shared through a v3 because it has to be usable as an
// initialiser in both places.
#define MAT_BARE_C ((v3){{0.740f, 0.726f, 0.700f}})

// The ramp itself, as a pure function.
static float ao_ramp(float y, float base, float rate, float min_v) {
  if (min_v >= 1.0f) return 1.0f;
  float t = (y - base) * rate;
  t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
  return min_v + (1.0f - min_v) * t;
}

static float ao_at(float y) {
  return ao_ramp(y, g_ao_base, g_ao_rate, g_ao_min);
}

// --- THE FLOOR IS THE LARGEST SURFACE IN THE FRAME, and it was one quad ----
// A fragment shader cannot fix that, because the variation that makes a floor read as
// GROUND is a function of where the WALLS are — sand piles in the lee of an obstacle,
// traffic packs the open lanes, and the metre nearest a wall is dirtier and darker than
// the middle of a lane.
// 48, not 32: at 1.5 m cells a brick-foot stain was one smeared vertex; at
// 1.0 m the stamps read. 13824 static verts, zero fragment cost, never casts
// — and the "4608 triangles" note at the cast latch is exact again.
#define GROUND_CELLS 48
#define GROUND_STEP  (2.0f * ARENA_HALF / (float)GROUND_CELLS)
#define GROUND_VERTS (GROUND_CELLS * GROUND_CELLS * 6)

// What the floor is MADE OF at one point, as three independent scalars.
typedef struct {
  float drift;    // wind-blown sand banked against an obstacle
  float compact;  // open ground, packed by traffic: darker, less loose
  float skirt;    // the metre nearest a wall: contact darkening
} gsurf_t;

// Smooth value noise in C, matching the shader's own construction so a term baked here
// and a term added there cannot disagree about what a metre is.
static float fhash2(float x, float z) {
  uint32_t h = (uint32_t)(int32_t)x * 374761393u +
               (uint32_t)(int32_t)z * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return (float)((h ^ (h >> 16)) >> 8) * (1.0f / 16777216.0f);
}
static float fnoise2(float x, float z) {
  float ix = floorf(x), iz = floorf(z), fx = x - ix, fz = z - iz;
  fx = fx * fx * (3.0f - 2.0f * fx);
  fz = fz * fz * (3.0f - 2.0f * fz);
  float a = fhash2(ix, iz),         b = fhash2(ix + 1.0f, iz);
  float c = fhash2(ix, iz + 1.0f),  d = fhash2(ix + 1.0f, iz + 1.0f);
  return (a + (b - a) * fx) + ((c - a) + (a - b + d - c) * fx) * fz;
}

// How far UPWIND of this point the given footprint is, or -1 if the wind does not pass
// over it on its way here.
static float wind_shadow(const solid_t *s, float x, float z, v3 w, float sgn) {
  float dx = w.x * sgn, dz = w.z * sgn;
  float t0 = -1e9f, t1 = 1e9f;
  if (fabsf(dx) < 1e-6f) {
    if (x < s->min.x || x > s->max.x) return -1.0f;
  } else {
    float a = (s->min.x - x) / dx, b = (s->max.x - x) / dx;
    if (a > b) { float t = a; a = b; b = t; }
    if (a > t0) t0 = a;
    if (b < t1) t1 = b;
  }
  if (fabsf(dz) < 1e-6f) {
    if (z < s->min.z || z > s->max.z) return -1.0f;
  } else {
    float a = (s->min.z - z) / dz, b = (s->max.z - z) / dz;
    if (a > b) { float t = a; a = b; b = t; }
    if (a > t0) t0 = a;
    if (b < t1) t1 = b;
  }
  if (t1 < t0 || t1 < 0.0f) return -1.0f;
  return t0 > 0.0f ? t0 : 0.0f;
}

// One point of floor against every solid.
static gsurf_t ground_sample(float x, float z) {
  gsurf_t g = {0.0f, 0.0f, 0.0f};
  v3 w = wind_dir();
  float near_d = 1e9f;
  for (int i = 0; i < g_num_solids; i++) {
    const solid_t *s = &g_solids[i];
    if (s->max.y < 0.30f) continue;         // a step crate banks nothing
    float dx = fmaxf(fmaxf(s->min.x - x, x - s->max.x), 0.0f);
    float dz = fmaxf(fmaxf(s->min.z - z, z - s->max.z), 0.0f);
    float d = sqrtf(dx * dx + dz * dz);
    if (d < near_d) near_d = d;
    if (d > 9.0f) continue;
    // A DRIFT IS A SHADOW, NOT A HALO. The first version scored the distance to the
    // footprint and weighted it by which face the point sat off, and the result was an
    // isotropic ring around every solid in the arena — every wall banked sand on all
    // four sides equally, which says nothing about wind and reads as a glow.
    float lee = wind_shadow(s, x, z, w, -1.0f);
    if (lee >= 0.0f) {
      float amt = expf(-lee / 3.4f);
      if (amt > g.drift) g.drift = amt;
    }
    float wnd = wind_shadow(s, x, z, w, 1.0f);
    if (wnd >= 0.0f) {
      float amt = expf(-wnd / 1.15f) * 0.45f;
      if (amt > g.drift) g.drift = amt;
    }
  }
  // Traffic packs LANES, not acreage.
  float open01 = (near_d - 1.6f) * (1.0f / 3.4f);
  open01 = open01 < 0.0f ? 0.0f : open01 > 1.0f ? 1.0f : open01;
  float lane = fnoise2(x * 0.088f, z * 0.088f) * 0.7f +
               fnoise2(x * 0.21f, z * 0.21f) * 0.3f;
  lane = (lane - 0.44f) * 3.6f;
  lane = lane < 0.0f ? 0.0f : lane > 1.0f ? 1.0f : lane;
  g.compact = open01 * lane;
  g.skirt = expf(-near_d / 1.30f);
  // Drift and traffic are the same sand arguing over the same ground: a lane that is
  // walked flat cannot also be a dune.
  g.drift *= 1.0f - 0.75f * g.compact;
  return g;
}

// The floor's own occlusion, asked for by NAME because mark_stamp has to stamp an
// impact crater into ground whose occlusion it did not build.
static float ground_skirt_ao(float x, float z) {
  return 1.0f - 0.34f * ground_sample(x, z).skirt;
}
