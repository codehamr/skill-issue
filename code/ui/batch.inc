// =============================================================================
// ui/batch.inc  —  the one HUD batch: primitives (ui_push/quad/rect/line/
// disk/capsule/panel), the corner language (ui_cham_path/fill/edge), SDF
// text draw over the atlas font_bake bakes (render/render.inc).
// Proof: uiframe, shot battery. One TU: included once by game.c, in
// definition order — moving a definition out is a semantic change.
// Contract: ui_cham_path/fill/edge is the ONE copy of the corner language —
// tuning the clamp in one place used to move the wash off its own outline.
// =============================================================================
// ---------------------------------------------------------------------------
// UI batch — all HUD/menu drawing accumulates into one vertex buffer and is
// drawn with a single call. Coordinates are "virtual pixels": a 360-unit-tall space
// scaled by an INTEGER factor at or above that height (the SDF typeface and the
// hairlines are authored for integer steps), and fractionally below it — see ui_scale.
// ---------------------------------------------------------------------------

#define UI_MAX_VERTS 24576  // raised for the GRATICULE UI (rounded panels,
// glow sprites, SDF text).
static float g_ui_vb[UI_MAX_VERTS * 9];
static int   g_ui_nverts;
static int   g_ui_peak;
static long  g_ui_drops;
static float g_uis = 1;      // virtual->real pixel scale
static float g_ui_vw = 640;  // virtual screen size in those units...
static float g_ui_vh = 360;  // ...and the height is NOT always 360
static float g_ui_mode = UIM_RAW;  // batch state, set by every primitive

// Presentation-side motion clock. dt is the RENDERED frame's dt (clamped by
// app_frame); the harness never writes it, so uiframe/shot advance a fixed
// 1/60 step and screenshots stay deterministic. Sim state never reads this.
static float g_ui_dt = 1.0f / 60.0f;
static float g_ui_time;
static float ui_smooth(float cur, float target, float speed) {
  return target + (cur - target) * expf(-speed * g_ui_dt);
}
static float ui_easeout(float t) {
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  float it = 1.0f - t;
  return 1.0f - it * it * it;
}

// The HUD scale, in ONE place — integer at or above the design height, fractional
// below it (see the clamp note in the body). ui_begin caches it for the UI layer;
// the scope-disc placement in vm_build recomputes it from R.height (it runs
// before ui_begin each frame, and the harness's vmsight/vmorbit call it
// directly, so the cached g_uis can be one frame stale across a resize).
static float ui_scale(void) {
  // BELOW THE 360-UNIT DESIGN HEIGHT THE SCALE GOES FRACTIONAL. Clamping to 1x there
  // means the virtual space is SHORTER than the layout authored into it, and the
  // panel's own height clamp then pulls its footer up into the row list — measured at
  // 700x300, where the hint chips drew on top of two bind rows and BACK fell off the
  // bottom.
  if (R.height > 0 && R.height < 360) return (float)R.height / 360.0f;
  int s = R.height / 360;
  return s < 1 ? 1.0f : (float)s;
}

static void ui_begin(void) {
  g_uis = ui_scale();
  // The scale is an INTEGER, so a 360-unit-tall design space only comes out at heights
  // that are a multiple of 360.
  g_ui_vw = (float)R.width / g_uis;
  g_ui_vh = (float)R.height / g_uis;
  g_ui_nverts = 0;
}

static void ui_push(float x, float y, float u, float v, const float c[4]) {
  if (g_ui_nverts >= UI_MAX_VERTS) { g_ui_drops++; return; }
  float *p = g_ui_vb + (size_t)g_ui_nverts++ * 9;
  p[0] = x * g_uis; p[1] = y * g_uis;
  p[2] = u; p[3] = v;
  p[4] = c[0]; p[5] = c[1]; p[6] = c[2]; p[7] = c[3];
  p[8] = g_ui_mode;
}

static void ui_quad(float x, float y, float w, float h,
                    float u0, float v0, float u1, float v1, const float c[4]) {
  ui_push(x, y, u0, v0, c);     ui_push(x + w, y, u1, v0, c); ui_push(x + w, y + h, u1, v1, c);
  ui_push(x, y, u0, v0, c);     ui_push(x + w, y + h, u1, v1, c); ui_push(x, y + h, u0, v1, c);
}

static void ui_rect(float x, float y, float w, float h,
                    float r, float g, float b, float a) {
  g_ui_mode = UIM_RAW;
  float u = g_hf_solid_u, v = g_hf_solid_v;
  ui_quad(x, y, w, h, u, v, u, v, (float[4]){r, g, b, a});
}

// A panel whose height depends on its own content cannot be drawn last — immediate mode
// paints in insertion order and it would cover what it frames.

// Crisp line at any angle: an oriented quad (MSAA smooths the edges) — for HUD marks
// that must not look like stepped pixel art, and the chamfered border's building block.
static void ui_line(float x0, float y0, float x1, float y1, float hw,
                    float r, float g, float b, float a) {
  g_ui_mode = UIM_RAW;
  float u = g_hf_solid_u, v = g_hf_solid_v;
  float dx = x1 - x0, dy = y1 - y0;
  float l = sqrtf(dx * dx + dy * dy);
  if (l < 1e-4f) return;
  float px = -dy / l * hw, py = dx / l * hw;
  const float c[4] = {r, g, b, a};
  ui_push(x0 + px, y0 + py, u, v, c);
  ui_push(x1 + px, y1 + py, u, v, c);
  ui_push(x1 - px, y1 - py, u, v, c);
  ui_push(x0 + px, y0 + py, u, v, c);
  ui_push(x1 - px, y1 - py, u, v, c);
  ui_push(x0 - px, y0 - py, u, v, c);
}

static void ui_diskn(float cx, float cy, float rad, int segs, const float c[4]) {
  g_ui_mode = UIM_RAW;
  float u = g_hf_solid_u, v = g_hf_solid_v;
  for (int i = 0; i < segs; i++) {
    float a0 = (float)i * F_TAU / (float)segs,
          a1 = (float)(i + 1) * F_TAU / (float)segs;
    ui_push(cx, cy, u, v, c);
    ui_push(cx + cosf(a0) * rad, cy + sinf(a0) * rad, u, v, c);
    ui_push(cx + cosf(a1) * rad, cy + sinf(a1) * rad, u, v, c);
  }
}

// Filled circle — the round red dot, which sits dead centre of the ADS view and
// is therefore the one HUD element whose facets a player can count. 32 segments.
#define UI_DISK_SEGS 32
static void ui_disk(float cx, float cy, float rad,
                    float r, float g, float b, float a) {
  ui_diskn(cx, cy, rad, UI_DISK_SEGS, (float[4]){r, g, b, a});
}

// Thick stroke with round caps — the GRATICULE mark language (crosshair arms,
// hitmarker, damage wedges) is capsules, not boxes.
static void ui_capsule(float x0, float y0, float x1, float y1,
                       float hw, float r, float g, float b, float a) {
  // MACHINED, not bullnosed: the ends extend by hw and cut flat (the old 8-segment end
  // disks were the last rounded marks on screen — crosshair, hitmarker and cursor all
  // speak the chamfer language now).
  float dx = x1 - x0, dy = y1 - y0;
  float l = sqrtf(dx * dx + dy * dy);
  if (l > 1e-4f) {
    float ex = dx / l * hw, ey = dy / l * hw;
    x0 -= ex; y0 -= ey; x1 += ex; y1 += ey;
  }
  ui_line(x0, y0, x1, y1, hw, r, g, b, a);
}

static void ui_arc_seg(float cx, float cy, float rad, float a0, float a1,
                       float hw, const float c[4]) {
  int segs = (int)(fabsf(a1 - a0) * 9.0f) + 2;
  if (segs > 48) segs = 48;
  g_ui_mode = UIM_RAW;
  float u = g_hf_solid_u, v = g_hf_solid_v;
  float pa = a0;
  for (int i = 1; i <= segs; i++) {
    float na = a0 + (a1 - a0) * (float)i / (float)segs;
    float p0x = cx + cosf(pa) * (rad - hw), p0y = cy + sinf(pa) * (rad - hw);
    float p1x = cx + cosf(pa) * (rad + hw), p1y = cy + sinf(pa) * (rad + hw);
    float n0x = cx + cosf(na) * (rad - hw), n0y = cy + sinf(na) * (rad - hw);
    float n1x = cx + cosf(na) * (rad + hw), n1y = cy + sinf(na) * (rad + hw);
    ui_push(p0x, p0y, u, v, c); ui_push(p1x, p1y, u, v, c); ui_push(n1x, n1y, u, v, c);
    ui_push(p0x, p0y, u, v, c); ui_push(n1x, n1y, u, v, c); ui_push(n0x, n0y, u, v, c);
    pa = na;
  }
}

// Vertical gradient fill — per-vertex color makes this free.
static void ui_vgrad(float x, float y, float w, float h,
                     const float ct[4], const float cb[4]) {
  g_ui_mode = UIM_RAW;
  float u = g_hf_solid_u, v = g_hf_solid_v;
  ui_push(x, y, u, v, ct);     ui_push(x + w, y, u, v, ct);     ui_push(x + w, y + h, u, v, cb);
  ui_push(x, y, u, v, ct);     ui_push(x + w, y + h, u, v, cb); ui_push(x, y + h, u, v, cb);
}

// Horizontal gradient fill — the side edges of vignettes need it.
static void ui_hgrad(float x, float y, float w, float h,
                     const float cl[4], const float cr[4]) {
  g_ui_mode = UIM_RAW;
  float u = g_hf_solid_u, v = g_hf_solid_v;
  ui_push(x, y, u, v, cl);     ui_push(x + w, y, u, v, cr);     ui_push(x + w, y + h, u, v, cr);
  ui_push(x, y, u, v, cl);     ui_push(x + w, y + h, u, v, cr); ui_push(x, y + h, u, v, cl);
}

static void ui_rr_col(float y0, float yy, float h, const float ct[4],
                      const float cb[4], float out[4]) {
  float t = h > 0 ? (yy - y0) / h : 0;
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  for (int i = 0; i < 4; i++) out[i] = ct[i] + (cb[i] - ct[i]) * t;
}

#define UI_RR_SEGS 5
static void ui_rr_corner(float ccx, float ccy, float rad, float base, float y0,
                         float h, const float ct[4], const float cb[4]) {
  float u = g_hf_solid_u, v = g_hf_solid_v;
  float cc[4];
  ui_rr_col(y0, ccy, h, ct, cb, cc);
  for (int i = 0; i < UI_RR_SEGS; i++) {
    float a0 = base + F_PI_2 * (float)i / UI_RR_SEGS;
    float a1 = base + F_PI_2 * (float)(i + 1) / UI_RR_SEGS;
    float p0x = ccx + cosf(a0) * rad, p0y = ccy + sinf(a0) * rad;
    float p1x = ccx + cosf(a1) * rad, p1y = ccy + sinf(a1) * rad;
    float c0[4], c1[4];
    ui_rr_col(y0, p0y, h, ct, cb, c0);
    ui_rr_col(y0, p1y, h, ct, cb, c1);
    ui_push(ccx, ccy, u, v, cc);
    ui_push(p0x, p0y, u, v, c0);
    ui_push(p1x, p1y, u, v, c1);
  }
}

// Rounded rectangle with an optional vertical gradient: three slabs + four
// corner fans. The GRATICULE panel/chip/row shape.
static void ui_rrect_c(float x, float y, float w, float h, float rad,
                       const float ct[4], const float cb[4]) {
  g_ui_mode = UIM_RAW;
  float u = g_hf_solid_u, v = g_hf_solid_v;
  if (rad > w * 0.5f) rad = w * 0.5f;
  if (rad > h * 0.5f) rad = h * 0.5f;
  if (rad < 0.5f) {
    ui_push(x, y, u, v, ct);     ui_push(x + w, y, u, v, ct);     ui_push(x + w, y + h, u, v, cb);
    ui_push(x, y, u, v, ct);     ui_push(x + w, y + h, u, v, cb); ui_push(x, y + h, u, v, cb);
    return;
  }
  float ca[4], cz[4];
  ui_rr_col(y, y + rad, h, ct, cb, ca);
  ui_rr_col(y, y + h - rad, h, ct, cb, cz);
  // middle slab, full width
  ui_push(x, y + rad, u, v, ca);         ui_push(x + w, y + rad, u, v, ca);
  ui_push(x + w, y + h - rad, u, v, cz);
  ui_push(x, y + rad, u, v, ca);         ui_push(x + w, y + h - rad, u, v, cz);
  ui_push(x, y + h - rad, u, v, cz);
  // top and bottom slabs between the corners
  ui_push(x + rad, y, u, v, ct);         ui_push(x + w - rad, y, u, v, ct);
  ui_push(x + w - rad, y + rad, u, v, ca);
  ui_push(x + rad, y, u, v, ct);         ui_push(x + w - rad, y + rad, u, v, ca);
  ui_push(x + rad, y + rad, u, v, ca);
  ui_push(x + rad, y + h - rad, u, v, cz); ui_push(x + w - rad, y + h - rad, u, v, cz);
  ui_push(x + w - rad, y + h, u, v, cb);
  ui_push(x + rad, y + h - rad, u, v, cz); ui_push(x + w - rad, y + h, u, v, cb);
  ui_push(x + rad, y + h, u, v, cb);
  ui_rr_corner(x + rad, y + rad, rad, F_PI, y, h, ct, cb);            // TL
  ui_rr_corner(x + w - rad, y + rad, rad, F_3PI_2, y, h, ct, cb);      // TR
  ui_rr_corner(x + w - rad, y + h - rad, rad, 0.0f, y, h, ct, cb);     // BR
  ui_rr_corner(x + rad, y + h - rad, rad, F_PI_2, y, h, ct, cb);       // BL
}

// Hairline border along a rounded-rect path (stroke centred on the path).
static void ui_rrect_line(float x, float y, float w, float h,
                          float rad, float hw,
                          float r, float g, float b, float a) {
  if (rad > w * 0.5f) rad = w * 0.5f;
  if (rad > h * 0.5f) rad = h * 0.5f;
  const float c[4] = {r, g, b, a};
  ui_rect(x + rad, y - hw, w - 2 * rad, 2 * hw, r, g, b, a);
  ui_rect(x + rad, y + h - hw, w - 2 * rad, 2 * hw, r, g, b, a);
  ui_rect(x - hw, y + rad, 2 * hw, h - 2 * rad, r, g, b, a);
  ui_rect(x + w - hw, y + rad, 2 * hw, h - 2 * rad, r, g, b, a);
  if (rad > 0.5f) {
    ui_arc_seg(x + rad, y + rad, rad, F_PI, F_3PI_2, hw, c);
    ui_arc_seg(x + w - rad, y + rad, rad, F_3PI_2, F_TAU, hw, c);
    ui_arc_seg(x + w - rad, y + h - rad, rad, 0.0f, F_PI_2, hw, c);
    ui_arc_seg(x + rad, y + h - rad, rad, F_PI_2, F_PI, hw, c);
  }
}

// THE CORNER LANGUAGE, AND IT REALLY IS IN ONE PLACE NOW: a 45-degree chamfer on two
// opposing corners (top-right, bottom-left), matching HARDLINE's octagonal bowls.
static float ui_cham_path(float x, float y, float w, float h,
                          float qx[6], float qy[6]) {
  float c = h * 0.30f;
  if (c > 9.0f) c = 9.0f;
  if (c < 2.5f) c = 2.5f;
  if (c > w * 0.25f) c = w * 0.25f;
  qx[0] = x;  qx[1] = x + w - c;  qx[2] = x + w;
  qx[3] = x + w;  qx[4] = x + c;  qx[5] = x;
  qy[0] = y;  qy[1] = y;          qy[2] = y + c;
  qy[3] = y + h;  qy[4] = y + h;  qy[5] = y + h - c;
  return c;
}
// The path as a triangle fan off vertex 0, in the raw (untextured) mode.
static void ui_cham_fill(const float qx[6], const float qy[6],
                         const float col[4]) {
  g_ui_mode = UIM_RAW;
  float u = g_hf_solid_u, v = g_hf_solid_v;
  for (int i = 1; i < 5; i++) {
    ui_push(qx[0], qy[0], u, v, col);
    ui_push(qx[i], qy[i], u, v, col);
    ui_push(qx[i + 1], qy[i + 1], u, v, col);
  }
}
// The same path as a closed hairline — what makes a wash a SURFACE.
static void ui_cham_edge(const float qx[6], const float qy[6], float hw,
                         float r, float g, float b, float a) {
  for (int i = 0; i < 6; i++) {
    int j = (i + 1) % 6;
    ui_line(qx[i], qy[i], qx[j], qy[j], hw, r, g, b, a);
  }
}

// The GRATICULE surface: a near-flat cold slab with a crisp hairline over that
// chamfered path. `rad` is legacy and ignored: the surface is machined, never turned.
static void ui_panel_wash(float x, float y, float w, float h,
                          float r, float g, float b, float a) {
  float qx[6], qy[6];
  (void)ui_cham_path(x, y, w, h, qx, qy);
  ui_cham_fill(qx, qy, (const float[4]){r, g, b, a});
}

static void ui_panel(float x, float y, float w, float h,
                     float rad, float a) {
  (void)rad;
  float qx[6], qy[6];
  (void)ui_cham_path(x, y, w, h, qx, qy);
  ui_cham_fill(qx, qy, (const float[4]){0.030f, 0.036f, 0.046f, 0.92f * a});
  ui_cham_edge(qx, qy, 0.5f, 0.941f, 0.957f, 0.973f, 0.12f * a);
}

// One quad = one soft radial sprite (the atlas soft cell, raw alpha):
// glow behind accents, shadows under panels.
static void ui_glow(float cx, float cy, float rad,
                    float r, float g, float b, float a) {
  g_ui_mode = UIM_RAW;
  ui_quad(cx - rad, cy - rad, 2 * rad, 2 * rad,
          g_hf_soft[0], g_hf_soft[1], g_hf_soft[2], g_hf_soft[3],
          (float[4]){r, g, b, a});
}

// HARDLINE text. y is the CAP TOP (the old 5x7 convention); scale 1 sets a 7-unit cap
// height.
static void ui_text_ex(const char *t, float x, float y, float scale,
                       float thr, float track, const float c[4]) {
  float f = scale * (7.0f / HF_CAP);
  g_ui_mode = thr;
  for (; *t; t++) {
    int gi = glyph_index(*t);
    if (gi >= 0 && gi < HF_GLYPHS) {
      const hf_gm_t *m = &g_hfm[gi];
      if (m->qx1 > m->qx0) {
        float ox = (m->adv - (float)hf_glyph_data(gi)[1]) * 0.5f;
        ui_quad(x + (ox + m->qx0) * f, y + (HF_CAP - m->qy1) * f,
                (m->qx1 - m->qx0) * f, (m->qy1 - m->qy0) * f,
                m->u0, m->v0, m->u1, m->v1, c);
      }
      x += m->adv * f + track;
    } else {
      x += 16.0f * f + track;
    }
  }
  g_ui_mode = UIM_RAW;
}

static void ui_text(const char *t, float x, float y, float scale,
                    float r, float g, float b, float a) {
  ui_text_ex(t, x, y, scale, TXT_REG, 0, (float[4]){r, g, b, a});
}

static void ui_textb(const char *t, float x, float y, float scale,
                     float r, float g, float b, float a) {
  ui_text_ex(t, x, y, scale, TXT_BOLD, 0, (float[4]){r, g, b, a});
}

// HUD text over the WORLD: an ink shadow first, then the text.
static void ui_text_sh(const char *t, float x, float y, float scale,
                       float thr, float track, const float c[4]) {
  ui_text_ex(t, x + 0.8f, y + 0.9f, scale, thr, track,
             (float[4]){0.02f, 0.03f, 0.04f, 0.55f * c[3]});
  ui_text_ex(t, x, y, scale, thr, track, c);
}

static float ui_text_width_tr(const char *t, float scale, float track) {
  float f = scale * (7.0f / HF_CAP), w = 0;
  int n = 0;
  for (; *t; t++, n++) {
    int gi = glyph_index(*t);
    w += (gi >= 0 && gi < HF_GLYPHS ? g_hfm[gi].adv : 16.0f) * f;
  }
  return n ? w + (float)(n - 1) * track : 0;
}

static float ui_text_width(const char *t, float scale) {
  return ui_text_width_tr(t, scale, 0);
}

static void ui_flush(void) {
  if (g_ui_nverts > g_ui_peak) g_ui_peak = g_ui_nverts;
  if (!g_ui_nverts) return;
  glBindBuffer(GL_ARRAY_BUFFER, R.vbo_hud);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(g_ui_nverts * 9 * sizeof(float)),
               g_ui_vb, GL_STREAM_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, g_ui_nverts);
}

