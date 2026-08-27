// =============================================================================
// world/decor.inc  —  scat_emit, mesh_decor, dec_twig/dec_stick, drifts,
// mesh_cast_extra, and fig_joint (the one joint-mass builder).
// Proof: scat, mapcheck, shot battery. One TU: included once by game.c, in
// definition order — moving a definition out is a semantic change.
// Contract: decor lives in the world mesh ONLY, never in g_solids — it does
// not collide, occlude a bot or cost a tick. fig_joint builds every flexing
// joint's mass from the two bone directions (not a sphere) and is the ONE
// copy every render figure file calls.
// =============================================================================
// ===========================================================================
// DECOR — the world mesh only. Never a solid, never a tick, never a bullet.
// ===========================================================================
// Everything here is ankle height ON PURPOSE: a knee-high visual rock a round
// passes through is a lie you can die to, and a free-standing 300-800 mm mass reads as
// a crouched player at range whether it is one or not.
static v3 dec_rot(v3 p, float sg) { return (v3){{p.x * sg, p.y, p.z * sg}}; }

// A tumbled stone. The TILT is the whole point: everything else in this arena is an
// axis-aligned box, so a few degrees off vertical is the single cue that separates "a
// thing that fell here" from "a thing that was built here".
// chamk is the decor chamfer ladder — 0.30 for tumbled dry stone, 0.45 for the
// wet theme's water-worn cobbles — a global language constant per theme, never
// a per-seed draw.
static void dec_stone(v3 at, float rx, float ry, float rz, float yaw,
                      float tilt, v3 col, float sg, float chamk) {
  // A STONE HAS NO ARRIS, and g_cham is already the primitive that says so — the same
  // flag the viewmodel turns on for its 0.7 mm broken edges, here as a fraction of the
  // minor half-extent because a weathered stone is nearly round at its corners.
  float mn3 = rx < ry ? (rx < rz ? rx : rz) : (ry < rz ? ry : rz);
  // 0.30 of the minor half-extent, not 0.18: at the smaller number the bevel was a
  // machined arris and every stone still read as a small crate. The size floor
  // is 0.032 (was 0.072, tuned when the smallest stone was twice this size):
  // above it every stone gets its bevel — an unbevelled 5 cm cube on the floor
  // reads as a DIE, which is the review note that moved it.
  float cham = mn3 > 0.032f ? mn3 * chamk : 0.0f;
  float save_cham = g_cham;
  g_cham = cham;
  float cy = cosf(yaw), sy = sinf(yaw), ct = cosf(tilt), st = sinf(tilt);
  v3 X = {{cy, 0.0f, -sy}}, Y = {{0.0f, 1.0f, 0.0f}}, Z = {{sy, 0.0f, cy}};
  v3 Xt = v3_add(v3_scale(X, ct), v3_scale(Y, st));
  v3 Yt = v3_sub(v3_scale(Y, ct), v3_scale(X, st));
  scene_box(dec_rot(at, sg), v3_scale(dec_rot(Xt, sg), rx),
            v3_scale(Yt, ry), v3_scale(dec_rot(Z, sg), rz), col);
  g_cham = save_cham;
}

// A dry grass tuft: tapered blades from one root. The caller owns the
// randomness (a per-site hash stream, re-seeded per twin so both hold the
// same tuft) and pre-rotates the WIND for the mirrored copy — dec_rot maps a
// downwind lean onto an upwind one, which is exactly the half-arena tell the
// old signature shipped.
static void dec_tuft(v3 at, float h, v3 wind, float lean, v3 col, float sg,
                     uint64_t *st, int nb) {
  for (int b = 0; b < nb; b++) {
    float a = (float)b * (6.2832f / (float)nb) + rng_rangef(st, -0.44f, 0.44f);
    float bh = h * rng_rangef(st, 0.62f, 1.0f);
    float bw = rng_rangef(st, 0.010f, 0.017f);
    v3 dir = {{cosf(a), 0.0f, sinf(a)}};
    v3 perp = {{-sinf(a), 0.0f, cosf(a)}};
    // Splay out from the root, then lean downwind. The splay is what gives the
    // tuft a silhouette; the lean is what gives the field a direction.
    v3 root = v3_add(at, v3_scale(dir, rng_rangef(st, 0.0f, 0.022f)));
    v3 tip = v3_add(root, (v3){{0.0f, bh, 0.0f}});
    tip = v3_add(tip, v3_scale(dir, bh * rng_rangef(st, 0.16f, 0.42f)));
    tip = v3_add(tip, v3_scale(wind, bh * lean));
    v3 b0 = v3_add(root, v3_scale(perp, bw));
    v3 b1 = v3_sub(root, v3_scale(perp, bw));
    // Up-and-outward, so the blade catches the sky the way a standing leaf does
    // rather than going flat-dark like a card lying on the ground.
    v3 n = v3_norm(v3_add((v3){{0.0f, 1.0f, 0.0f}}, v3_scale(perp, 0.35f)));
    v3 tint = v3_scale(col, rng_rangef(st, 0.88f, 1.12f));
    v3 r0 = dec_rot(b0, sg), r1 = dec_rot(b1, sg), rt = dec_rot(tip, sg);
    v3 rn = dec_rot(n, sg);
    scene_poly3(r0, r1, rt, rn, tint);
    scene_poly3(r1, r0, rt, rn, tint);
  }
}

// ONE TWIG — a tapered double-sided blade, the tuft blade's woody sibling:
// what the shrub and the tumbleweed are made of. Six verts, because at
// 6-10 mm across a full prism is thirty spent below one pixel.
static void dec_twig(v3 root, v3 dir, float len, float w0, v3 col, float sg) {
  v3 tip = v3_add(root, v3_scale(dir, len));
  v3 up = fabsf(dir.y) > 0.9f ? (v3){{1, 0, 0}} : (v3){{0, 1, 0}};
  v3 perp = v3_scale(v3_norm(v3_cross(dir, up)), w0);
  v3 n = v3_norm(v3_add((v3){{0.0f, 1.0f, 0.0f}}, v3_scale(perp, 0.3f)));
  v3 b0 = dec_rot(v3_add(root, perp), sg), b1 = dec_rot(v3_sub(root, perp), sg);
  v3 rt = dec_rot(tip, sg), rn = dec_rot(n, sg);
  scene_poly3(b0, b1, rt, rn, col);
  scene_poly3(b1, b0, rt, rn, col);
}

// An ANGULAR rock — the chamfered box's sibling for stone that broke rather
// than tumbled. Eight box corners, some pulled toward the centroid, every
// face split into two flat triangles: a "worn corner" is one vertex moved, the
// mesh stays closed by construction, and flat shading is the house facet
// language. `keel` is the SAND theme's ventifact cue: the two top corners on
// the local -X side (the caller aligns +X downwind) pulled hard down, which is
// the wind-carved slope every desert photo of one shows.
static void dec_rock(v3 at, float rx, float ry, float rz, float yaw,
                     float tilt, uint32_t cuts, float cutf, int keel,
                     v3 col, float sg) {
  float cy = cosf(yaw), sy = sinf(yaw), ct = cosf(tilt), tst = sinf(tilt);
  v3 X = {{cy, 0.0f, -sy}}, Y = {{0.0f, 1.0f, 0.0f}}, Z = {{sy, 0.0f, cy}};
  v3 Xt = v3_add(v3_scale(X, ct), v3_scale(Y, tst));
  v3 Yt = v3_sub(v3_scale(Y, ct), v3_scale(X, tst));
  v3 ex = v3_scale(Xt, rx), ey = v3_scale(Yt, ry), ez = v3_scale(Z, rz);
  v3 p[8];
  for (int i = 0; i < 8; i++) {
    v3 q = at;
    q = v3_add(q, v3_scale(ex, i & 1 ? 1.0f : -1.0f));
    q = v3_add(q, v3_scale(ey, i & 2 ? 1.0f : -1.0f));
    q = v3_add(q, v3_scale(ez, i & 4 ? 1.0f : -1.0f));
    p[i] = q;
  }
  if (keel) {   // -x (bit 0 clear) top (bit 1 set) corners: 2 and 6
    p[2] = v3_lerp(p[2], at, 0.44f);
    p[6] = v3_lerp(p[6], at, 0.44f);
  }
  for (int i = 0; i < 8; i++)
    if ((cuts >> i) & 1u)
      p[i] = v3_lerp(p[i], at, cutf * (0.75f + 0.5f * (float)((cuts >> (i + 8)) & 1u)));
  // scene_box's own face table, CCW from outside; the split diagonal is a-c.
  static const int F[6][4] = {{1, 3, 7, 5}, {4, 6, 2, 0}, {2, 6, 7, 3},
                              {4, 0, 1, 5}, {4, 5, 7, 6}, {0, 2, 3, 1}};
  for (int f = 0; f < 6; f++) {
    v3 a = p[F[f][0]], b = p[F[f][1]], c = p[F[f][2]], d = p[F[f][3]];
    for (int half = 0; half < 2; half++) {
      v3 t0 = a, t1 = half ? c : b, t2 = half ? d : c;
      v3 n = v3_cross(v3_sub(t1, t0), v3_sub(t2, t0));
      float l = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
      if (l < 1e-9f) continue;   // a cut can degenerate one half-face
      n = v3_scale(n, 1.0f / l);
      v3 mid = v3_scale(v3_add(v3_add(t0, t1), t2), 1.0f / 3.0f);
      // The face table is authored CCW-outside, but a deep cut can turn a
      // half-face over — culling is on, so a flip must swap the WINDING with
      // the normal or the face simply vanishes.
      if (v3_dot(n, v3_sub(mid, at)) < 0.0f) {
        v3 tmp = t1; t1 = t2; t2 = tmp;
        n = v3_scale(n, -1.0f);
      }
      scene_poly3(dec_rot(t0, sg), dec_rot(t1, sg), dec_rot(t2, sg),
                  dec_rot(n, sg), col);
    }
  }
}

// A pebble: a squashed six-vertex octahedron, eight flat faces, 24 verts —
// the smallest closed thing this mesh can afford, for the tier where any more
// would be spent below one pixel.
static void dec_pebble(v3 at, float r, uint64_t *st, v3 col, float sg) {
  float rx = r * rng_rangef(st, 0.85f, 1.30f);
  float rz = r * rng_rangef(st, 0.85f, 1.30f);
  float ry = r * rng_rangef(st, 0.55f, 0.80f);
  float yaw = rng_rangef(st, 0.0f, 6.283f);
  float sink = rng_rangef(st, 0.15f, 0.40f);
  at.y = ry * (1.0f - 2.0f * sink);
  float cy = cosf(yaw), sy = sinf(yaw);
  v3 X = {{cy, 0.0f, -sy}}, Z = {{sy, 0.0f, cy}};
  v3 e[4];   // the equator, each spoke its own length
  e[0] = v3_add(at, v3_scale(X, rx * rng_rangef(st, 0.85f, 1.15f)));
  e[1] = v3_add(at, v3_scale(Z, rz * rng_rangef(st, 0.85f, 1.15f)));
  e[2] = v3_sub(at, v3_scale(X, rx * rng_rangef(st, 0.85f, 1.15f)));
  e[3] = v3_sub(at, v3_scale(Z, rz * rng_rangef(st, 0.85f, 1.15f)));
  v3 top = v3_add(at, (v3){{0.0f, ry, 0.0f}});
  v3 bot = v3_sub(at, (v3){{0.0f, ry, 0.0f}});
  // Each face's winding is derived from its own cross product, so the normal
  // and the vertex order can never disagree (culling is on).
  for (int i = 0; i < 4; i++) {
    v3 a = e[i], b = e[(i + 1) & 3];
    v3 apex[2] = {top, bot};
    for (int hb = 0; hb < 2; hb++) {
      v3 t0 = apex[hb], t1 = hb ? a : b, t2 = hb ? b : a;
      v3 n = v3_cross(v3_sub(t1, t0), v3_sub(t2, t0));
      float l = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
      if (l < 1e-9f) continue;
      n = v3_scale(n, 1.0f / l);
      v3 mid = v3_scale(v3_add(v3_add(t0, t1), t2), 1.0f / 3.0f);
      if (v3_dot(n, v3_sub(mid, at)) < 0.0f) {
        v3 tmp = t1; t1 = t2; t2 = tmp;
        n = v3_scale(n, -1.0f);
      }
      scene_poly3(dec_rot(t0, sg), dec_rot(t1, sg), dec_rot(t2, sg),
                  dec_rot(n, sg), col);
    }
  }
}

// A sand drift banked against a face. IT IS A LENS, NOT A SLAB. The first
// version was one quad from the wall out to a straight line at a fixed
// distance, closed with two vertical end triangles — which from any angle is a
// pale, hard-edged, axis-aligned RECTANGLE lying on the ground, and at the
// wall/floor line it put a 90-degree step into the join as if the wall were
// sunk into a ledge. That is a modelling error, not weather. Both the reach and
// the height now taper to zero at each end on a sine, so the thing has a plan
// outline a wind could have made and no edge anywhere that a straight line
// could be drawn along. Ankle height, so it is scenery a player walks through
// and never scenery a player trusts.
static void dec_drift(v3 a0, v3 a1, v3 o, float len, float hgt, v3 col,
                      float sg) {
  const int N = 7;
  v3 pt = {{0, 0, 0}}, pb = {{0, 0, 0}};
  for (int i = 0; i <= N; i++) {
    float u = (float)i / (float)N;
    float bell = powf(sinf(u * F_PI), 0.62f);
    v3 base = v3_lerp(a0, a1, u);
    v3 top = v3_add(base, (v3){{0.0f, hgt * bell, 0.0f}});
    v3 out = v3_add(base, v3_scale(o, len * bell));
    v3 rt = dec_rot(top, sg), rb = dec_rot(out, sg);
    if (i > 0) {
      // The END segments are degenerate by construction (the bell starts at
      // zero, so the previous top and out coincide) and v3_norm of a zero
      // cross is NaN under -ffast-math — a TRUE-BLACK triangle at every
      // drift's tip. At 20 cm it hid for months; on a long face it was a
      // black wedge lying in the open.
      v3 cr = v3_cross(v3_sub(rb, pt), v3_sub(pb, pt));
      float cl = sqrtf(cr.x * cr.x + cr.y * cr.y + cr.z * cr.z);
      v3 n = cl > 1e-8f ? v3_scale(cr, 1.0f / cl) : (v3){{0.0f, 1.0f, 0.0f}};
      if (n.y < 0.0f) n = v3_scale(n, -1.0f);
      scene_poly4(pt, rt, rb, pb, n, col);
    }
    pt = rt; pb = rb;
  }
}

// The stone tint — ONE copy (it was dec_clump's, and dec_clump is gone): a
// stone is the GROUND it broke off, not the cover it sits beside — at 0.42
// toward the cover tone it came out a saturated red-brown slab on beige sand,
// which is the loudest thing a 6 cm object can be. Pulled a third of the way
// toward its host wall, then desaturated hard.
static v3 dec_stone_col(v3 host, uint64_t *st) {
  const theme_t *th = &g_theme;
  // Anchored on the FLOOR, not the walls: a desert rock is dust-coated and
  // sits close to its own ground. The first recipe pulled 0.34 toward the
  // host wall and then darkened twice — on sand the rocks came out PINK (the
  // wall family's hue) with shadow sides near black, i.e. exactly the value a
  // distant player presents, which is a readability tax and not a look.
  v3 stone = v3_lerp(th->ground, host, 0.14f);
  float sl = stone.x * 0.30f + stone.y * 0.59f + stone.z * 0.11f;
  stone = v3_lerp(stone, (v3){{sl, sl, sl}}, 0.30f);
  // 0.86-1.00 of the floor, not 0.74-0.90: the visible side of a rock is
  // mostly its SHADED side, so the lighting already supplies the value step —
  // an albedo step on top of it read as a black blob at twenty metres.
  return v3_scale(stone, rng_rangef(st, 0.86f, 1.00f));
}

// EMIT — mint both 180-degree twins of every planned site. The per-site hash
// stream is re-seeded for each twin so both hold the SAME object; the wind is
// pre-rotated for the mirrored copy (see the symmetry contract at scat_plan);
// a face-attached site's twin goes through scat_reflect first so the rotation
// lands it on the twin solid's own lee face.
static void scat_emit_rock(const scat_t *sc, v3 at, float sg, v3 w2, v3 host, int sand, int wet, float chamk, uint64_t st) {
        float ry = sc->s, rx, rz, yaw;
        if (sand) {
          // A ventifact: elongated ALONG the wind (an axis cue, safe under
          // the rotation) with the keel facet carved on its windward end.
          yaw = atan2f(-w2.z, w2.x) + rng_rangef(&st, -0.22f, 0.22f);
          rx = ry * rng_rangef(&st, 1.35f, 1.75f);
          rz = ry * rng_rangef(&st, 0.85f, 1.10f);
        } else {
          yaw = rng_rangef(&st, 0.0f, 6.283f);
          rx = ry * rng_rangef(&st, 0.90f, 1.40f);
          rz = ry * rng_rangef(&st, 0.80f, 1.20f);
        }
        float tilt = rng_rangef(&st, -0.14f, 0.14f);
        float sink = rng_rangef(&st, 0.25f, 0.30f);
        // The two-class cover rule as arithmetic: whatever the tilt and
        // aspect drew, the proud top stays strictly under 0.30 m — scaled
        // uniformly so the shape survives the clamp.
        float ct = cosf(tilt), stt = fabsf(sinf(tilt));
        float top = ry * (1.0f - 2.0f * sink) + ct * ry + stt * rx;
        float k = top > 0.295f ? 0.295f / top : 1.0f;
        rx *= k; ry *= k; rz *= k; top *= k;
        at.y = ry * (1.0f - 2.0f * sink);
        if (top > g_scat_top) g_scat_top = top;
        v3 col = dec_stone_col(host, &st);
        // MOSSTONE: one cobble in three takes a films-of-moss lerp — the
        // growth a mineral theme is allowed.
        if (wet && g_mood_preset == 2 && (sc->h % 3u) == 0u)
          col = v3_lerp(col, (v3){{0.30f, 0.36f, 0.20f}}, 0.20f);
        if (wet)   // a cobble tumbled round, not broken sharp
          dec_stone(at, rx, ry, rz, yaw, tilt, col, sg, chamk);
        else
          dec_rock(at, rx, ry, rz, yaw, tilt,
                   (sc->h & 0x2Du) | (sc->h & 0xFF00u),
                   rng_rangef(&st, 0.16f, 0.28f), sand, col, sg);
}

static void scat_emit_stone(const scat_t *sc, v3 at, float sg, v3 host, int wet, float chamk, uint64_t st) {
        float r = sc->s;
        float rx = r * rng_rangef(&st, 0.85f, 1.35f);
        float ry = r * rng_rangef(&st, 0.72f, 1.05f);
        float rz = r * rng_rangef(&st, 0.85f, 1.25f);
        float sink = rng_rangef(&st, 0.12f, 0.30f);
        v3 col = v3_scale(dec_stone_col(host, &st),
                          rng_rangef(&st, 0.92f, 1.08f));
        if (wet && g_mood_preset == 2 && (sc->h % 3u) == 0u)
          col = v3_lerp(col, (v3){{0.30f, 0.36f, 0.20f}}, 0.20f);
        at.y = ry * (1.0f - 2.0f * sink);
        float top = at.y + ry + 0.2f * rx;
        if (top > g_scat_top) g_scat_top = top;
        // The satellites speak the SAME language as their hero: on the dry
        // themes that is the cut-corner rock — a chamfered cube at this size
        // reads as a die beside an angular parent (P6, from inside the
        // family). Wet keeps the tumbled cobble.
        if (wet)
          dec_stone(at, rx, ry, rz, rng_rangef(&st, 0.0f, 6.283f),
                    rng_rangef(&st, -0.20f, 0.20f), col, sg, chamk);
        else
          dec_rock(at, rx, ry, rz, rng_rangef(&st, 0.0f, 6.283f),
                   rng_rangef(&st, -0.20f, 0.20f),
                   (sc->h & 0x2Du) | (sc->h & 0xFF00u),
                   rng_rangef(&st, 0.14f, 0.26f), 0, col, sg);
}

static void scat_emit_pebble(const scat_t *sc, v3 at, float sg, v3 host,
                             const theme_t *th, uint64_t st) {
        // A pebble only needs to exist inside 3 m — at range a dark speck on
        // a lit floor reads as a bullet hole. Most of the way back toward the
        // floor's own colour.
        v3 col = v3_lerp(th->ground, dec_stone_col(host, &st), 0.60f);
        dec_pebble(at, sc->s, &st, col, sg);
}

static void scat_emit_tuft(const scat_t *sc, v3 at, float sg, v3 w2, v3 dry, v3 green, uint64_t st) {
        // Log-uniform biased small: many short tufts, a few tall ones.
        float u = rng_rangef(&st, 0.0f, 1.0f);
        float h = 0.085f * powf(0.235f / 0.085f, u * u) * sc->s;
        v3 col = (sc->var & 2) ? green : dry;
        float lean = rng_rangef(&st, 0.16f, 0.34f);
        // Band tufts (face-attached) get the 7-blade build — they are what a
        // player walks past at arm's length; field tufts keep 5.
        dec_tuft(at, h, w2, lean, v3_scale(col, rng_rangef(&st, 0.90f, 1.10f)),
                 sg, &st, sc->host >= 0 ? 7 : 5);
}

static void scat_emit_pave(const scat_t *sc, v3 at, float sg, v3 host, uint64_t st) {
        // Desert pavement: ~40 flat plates a centimetre proud, noise-ragged
        // boundary — a mid-value texture zone from range that resolves into
        // stones up close. The bake darkens the ground beneath it.
        float prad = sc->s;
        v3 col = v3_scale(dec_stone_col(host, &st), 0.96f);
        int np2 = 52 + (int)(rng_next(&st) % 13u);
        for (int k = 0; k < np2; k++) {
          float rr = prad * sqrtf(rng_rangef(&st, 0.0f, 1.0f));
          float ang = rng_rangef(&st, 0.0f, 6.283f);
          v3 p = v3_add(at, (v3){{cosf(ang) * rr, 0.0f, sinf(ang) * rr}});
          uint32_t ph = (uint32_t)rng_next(&st);
          float ry = rng_rangef(&st, 0.012f, 0.018f);
          float rx = rng_rangef(&st, 0.060f, 0.100f);
          float rz = rx * rng_rangef(&st, 0.7f, 1.1f);
          float yaw = rng_rangef(&st, 0.0f, 6.283f);
          if (fnoise2(p.x * 0.9f + 3.0f, p.z * 0.9f - 8.0f) < 0.22f) continue;
          p.y = ry * 0.30f;
          dec_rock(p, rx, ry, rz, yaw, rng_rangef(&st, -0.06f, 0.06f),
                   ph & 0xFFFFu, 0.22f, 0, v3_scale(col, rng_rangef(&st, 0.92f, 1.06f)),
                   sg);
        }
}

static void scat_emit_shrub(const scat_t *sc, v3 at, float sg, v3 w2, uint64_t st) {
        // A dry shrub: openwork twigs up-and-out from one root, leaning a
        // little downwind. It never exceeds 0.35 m and you can see through
        // it — which is what keeps it honestly outside the cover language.
        int ns2 = 7 + (int)(rng_next(&st) % 4u);
        v3 wcol2 = (v3){{0.24f, 0.21f, 0.17f}};
        for (int k = 0; k < ns2; k++) {
          float ya = rng_rangef(&st, 0.0f, 6.283f);
          float el = rng_rangef(&st, 0.35f, 1.15f);
          v3 dir = {{cosf(ya) * cosf(el), sinf(el), sinf(ya) * cosf(el)}};
          dir = v3_norm(v3_add(dir, v3_scale(w2, 0.18f)));
          float len = sc->s * rng_rangef(&st, 0.75f, 1.35f);
          v3 root = v3_add(at, (v3){{rng_rangef(&st, -0.02f, 0.02f), 0.0f,
                                     rng_rangef(&st, -0.02f, 0.02f)}});
          // tips read lighter: sun-bleached dead wood
          v3 c2 = v3_scale(wcol2, rng_rangef(&st, 0.88f, 1.06f) +
                                      0.4f * len / sc->s * 0.15f);
          dec_twig(root, dir, len, rng_rangef(&st, 0.005f, 0.009f), c2, sg);
        }
}

static void scat_emit_cactus(const scat_t *sc, v3 at, float sg, uint64_t st) {
        // Sage over the desert floor — desaturated enough to stay out of the
        // soldier's olive band, audited at both sun extremes.
        v3 sage = v3_scale((v3){{0.34f, 0.38f, 0.27f}},
                           rng_rangef(&st, 0.92f, 1.06f));
        if (sc->var & 1) {
          // barrel: a squat all-corners-cut lump, sunk like a stone
          float ry = rng_rangef(&st, 0.11f, 0.14f);
          float rx = rng_rangef(&st, 0.09f, 0.12f);
          float rz = rx * rng_rangef(&st, 0.9f, 1.1f);
          at.y = ry * 0.70f;
          float top = at.y + ry;
          if (top > g_scat_top) g_scat_top = top;
          dec_rock(at, rx, ry, rz, rng_rangef(&st, 0.0f, 6.283f), 0.0f,
                   0xFFFFu, 0.30f, 0, sage, sg);
        } else {
          // prickly pear: 3-5 flat pads fanned low from the root
          int np3 = 3 + (int)(rng_next(&st) % 3u);
          for (int k = 0; k < np3; k++) {
            float ya = rng_rangef(&st, 0.0f, 6.283f);
            v3 d = {{cosf(ya), 0.0f, sinf(ya)}};
            float rx = rng_rangef(&st, 0.06f, 0.09f);
            float ry = rng_rangef(&st, 0.07f, 0.10f);
            v3 c3 = v3_add(at, (v3){{d.x * rng_rangef(&st, 0.04f, 0.08f),
                                     rng_rangef(&st, 0.07f, 0.12f),
                                     d.z * rng_rangef(&st, 0.04f, 0.08f)}});
            v3 yy = v3_norm(v3_add((v3){{0, 1, 0}},
                                   v3_scale(d, rng_rangef(&st, 0.25f, 0.6f))));
            v3 zz = v3_norm(v3_cross(d, yy));
            v3 xx = v3_cross(yy, zz);
            float top = c3.y + ry;
            if (top > g_scat_top) g_scat_top = top;
            scene_box(dec_rot(c3, sg), v3_scale(dec_rot(xx, sg), rx),
                      v3_scale(dec_rot(yy, sg), ry),
                      v3_scale(dec_rot(zz, sg), 0.014f),
                      v3_scale(sage, rng_rangef(&st, 0.94f, 1.05f)));
          }
        }
}

static void scat_emit_tumble(const scat_t *sc, v3 at, float sg, v3 dry, uint64_t st) {
        // A tumbleweed at rest: pale twigs on a sphere, parked in the lee
        // corner the plan found for it. It does not roll — a resting one is
        // 90 % of the read at 2 % of the cost.
        float r = sc->s;
        at.y = r * 0.92f;
        v3 pale = v3_lerp(dry, (v3){{0.55f, 0.50f, 0.36f}}, 0.55f);
        int ns3 = 14 + (int)(rng_next(&st) % 5u);
        for (int k = 0; k < ns3; k++) {
          float ya = rng_rangef(&st, 0.0f, 6.283f);
          float cy = rng_rangef(&st, -1.0f, 1.0f);
          float sy = sqrtf(fmaxf(1.0f - cy * cy, 0.0f));
          v3 dir = {{sy * cosf(ya), cy, sy * sinf(ya)}};
          dec_twig(v3_sub(at, v3_scale(dir, r * 0.2f)), dir, r * 1.2f,
                   rng_rangef(&st, 0.004f, 0.006f),
                   v3_scale(pale, rng_rangef(&st, 0.9f, 1.1f)), sg);
        }
}

static void scat_emit_brick(const scat_t *sc, v3 at, float sg, v3 host, uint64_t st) {
        // The house brick module, 0.30 x 0.14 x 0.10, in the host wall's own
        // cast: rotated on the ledges, spilled and part-buried at the feet.
        int kind = sc->var & 0xF, bax = (sc->var >> 4) & 1;
        float s = sc->s;
        float bx = 0.15f * s, by = 0.05f * s, bz = 0.07f * s;
        float yaw = (bax ? 0.0f : 1.5708f) + rng_rangef(&st, -0.6f, 0.6f);
        if (rng_rangef(&st, 0.0f, 1.0f) < 0.18f)
          yaw += rng_rangef(&st, 0.8f, 1.6f);   // the outlier that sells it
        float tilt = kind == ME_FOOT ? rng_rangef(&st, -0.30f, 0.30f)
                                     : rng_rangef(&st, 0.08f, 0.45f);
        v3 bcol = v3_scale(host, rng_rangef(&st, 0.84f, 0.96f));
        if (kind == ME_FOOT) {
          float sink = rng_rangef(&st, 0.20f, 0.40f);
          at.y = by * (1.0f - 2.0f * sink);
        } else {
          at.y = sc->pos.y + by * 0.75f;
        }
        dec_rock(at, bx, by, bz, yaw, tilt, 0u, 0.0f, 0, bcol, sg);
}

static void scat_emit(void) {
  const theme_t *th = &g_theme;
  g_scat_top = 0.0f;   // emit owns this number: the tallest proud decor point
  v3 w = wind_dir();
  int sand = th->surf == SURF_SAND, wet = th->surf == SURF_WET;
  // The decor chamfer ladder: 0.30 tumbled dry, 0.45 water-worn — a language
  // constant per theme, never a per-seed draw.
  float chamk = wet ? 0.45f : 0.30f;
  // Dead dry grass, and it must stay UNDER the floor in value: a tuft brighter
  // than the sand it stands in is the brightest thing in the lower third of
  // the frame, which is exactly where a player's eye is looking for a prone
  // figure. The green variant exists only beside rocks (the plan gates it).
  v3 dry = v3_lerp(th->ground, (v3){{0.34f, 0.31f, 0.17f}}, 0.62f);
  // STEPPE lifts the straw 8 % — its identity IS the tuft bands, and they
  // must read from the play camera, not only in a crop.
  if (th->surf == SURF_GRIT && g_mood_preset == 2) dry = v3_scale(dry, 1.08f);
  v3 green = v3_lerp(dry, (v3){{0.30f, 0.34f, 0.14f}}, 0.55f);
  for (int n = 0; n < g_scat_n; n++) {
    const scat_t *sc = &g_scat[n];
    // The host tint once per PAIR, off the canonical position — a stone takes
    // its wall's cast only when it plausibly broke off one.
    v3 host = th->cover;
    int wi;
    if (scat_wall(sc->pos.x, sc->pos.z, &wi, NULL) < 3.5f && wi >= 0)
      host = g_solids[wi].color;
    for (int t = 0; t < 2; t++) {
      float sg = t ? -1.0f : 1.0f;
      v3 at = sc->pos;
      if (t && sc->host >= 0)
        at = scat_reflect(at, &g_solids[sc->host], sc->var & 1);
      v3 w2 = dec_rot(w, sg);
      uint64_t st = (uint64_t)sc->h * 2654435761u + 1;
      switch (sc->fam) {
      case SCAT_ROCK:
        scat_emit_rock(sc, at, sg, w2, host, sand, wet, chamk, st);
        break;
      case SCAT_STONE:
        scat_emit_stone(sc, at, sg, host, wet, chamk, st);
        break;
      case SCAT_PEBBLE:
        scat_emit_pebble(sc, at, sg, host, th, st);
        break;
      case SCAT_TUFT:
        scat_emit_tuft(sc, at, sg, w2, dry, green, st);
        break;
      case SCAT_PAVE:
        scat_emit_pave(sc, at, sg, host, st);
        break;
      case SCAT_SHRUB:
        scat_emit_shrub(sc, at, sg, w2, st);
        break;
      case SCAT_CACTUS:
        scat_emit_cactus(sc, at, sg, st);
        break;
      case SCAT_TUMBLE:
        scat_emit_tumble(sc, at, sg, dry, st);
        break;
      case SCAT_BRICK:
        scat_emit_brick(sc, at, sg, host, st);
        break;
      }
    }
  }
}

// ONE STICK — a thin oriented prism from a to b. The dead tree's branches,
// the shrub's stems and the tumbleweed all speak this one construction (P6:
// a second stick vocabulary would be a second plant kingdom).
static void dec_stick(v3 a, v3 b, float r, v3 col, float sg) {
  v3 d = v3_sub(b, a);
  float l = sqrtf(v3_dot(d, d));
  if (l < 1e-5f) return;
  d = v3_scale(d, 1.0f / l);
  v3 up = fabsf(d.y) > 0.9f ? (v3){{1, 0, 0}} : (v3){{0, 1, 0}};
  v3 s1 = v3_norm(v3_cross(d, up)), s2 = v3_cross(d, s1);
  v3 c = v3_scale(v3_add(a, b), 0.5f);
  scene_box(dec_rot(c, sg), v3_scale(dec_rot(d, sg), l * 0.5f),
            v3_scale(dec_rot(s1, sg), r), v3_scale(dec_rot(s2, sg), r), col);
}

// The CASTING specials, emitted into the pre-latch region of the world mesh:
// the boulder pair's chamfered render (its collision stays the AABB — the
// 0.04 m bevel is under the wire quantization players already live with) and
// the dead trees' crowns. The crown is the deliberate THIRD class — casts,
// does not collide — because a branch two metres overhead can never be
// mistaken for cover, and a tree whose long shadow is a bare post's is the
// headless-shadow defect wearing bark. Branches are 40+ mm across, over the
// shadow map's own texel floor; the sub-texel twigs live in the decor pass.
static void mesh_cast_extra(void) {
  if (g_meta.bould[0] < 0 && g_meta.tree[0] < 0) return;
  int save_floats = g_scene_floats, save_want = g_scene_want;
  int save_peak = g_scene_peak;
  long save_drops = g_scene_drops;
  g_scene_floats = 0;
  g_ao_mul = 1.0f;
  mat_set(WORLD_SOLID_R, 0.0f);
  ao_set(0.0f, WORLD_AO_RATE, WORLD_AO_MIN);
  for (int b = 0; b < 2; b++)
    if (g_meta.bould[b] >= 0) {
      const solid_t *s = &g_solids[g_meta.bould[b]];
      v3 c = v3_scale(v3_add(s->min, s->max), 0.5f);
      float save_cham = g_cham;
      g_cham = 0.04f;
      scene_box(c, (v3){{(s->max.x - s->min.x) * 0.5f, 0, 0}},
                (v3){{0, (s->max.y - s->min.y) * 0.5f, 0}},
                (v3){{0, 0, (s->max.z - s->min.z) * 0.5f}}, s->color);
      g_cham = save_cham;
    }
  if (g_meta.tree[0] >= 0) {
    const solid_t *s = &g_solids[g_meta.tree[0]];
    v3 base = {{(s->min.x + s->max.x) * 0.5f, s->max.y,
                (s->min.z + s->max.z) * 0.5f}};
    v3 w = wind_dir();
    v3 wood = {{0.30f, 0.26f, 0.21f}};
    ao_off();
    mat_set(0.86f, 0.0f);   // weathered dead wood: matte, near the floor's R
    for (int t = 0; t < 2; t++) {
      float sg = t ? -1.0f : 1.0f;
      v3 w2 = dec_rot(w, sg);
      // KRUMMHOLZ: the crown leans DOWNWIND — a dead tree in this arena's
      // one climate wears the same wind the drifts, tufts and clouds do.
      float wyaw = atan2f(w2.z, w2.x);
      int np = 3;
      uint64_t st = (uint64_t)scat_h(0x72EEu, 1, 2, 3) * 2654435761u + 1;
      // One low dead stub halfway up the trunk: the gnarl that keeps a
      // straight sixteen-centimetre post from reading as a telephone pole.
      {
        float sya = wyaw + rng_rangef(&st, 1.8f, 4.4f);
        float sel = rng_rangef(&st, 0.10f, 0.35f);
        v3 sd = {{cosf(sya) * cosf(sel), sinf(sel), sinf(sya) * cosf(sel)}};
        v3 sa = {{base.x, s->max.y * rng_rangef(&st, 0.45f, 0.62f), base.z}};
        dec_stick(sa, v3_add(sa, v3_scale(sd, rng_rangef(&st, 0.35f, 0.60f))),
                  0.040f, v3_scale(wood, 0.92f), sg);
      }
      for (int p = 0; p < np; p++) {
        float ya = wyaw + ((float)p - (float)(np - 1) * 0.5f) * 1.15f +
                   rng_rangef(&st, -0.35f, 0.35f);
        float el = rng_rangef(&st, 0.62f, 1.05f);
        float ln = rng_rangef(&st, 0.85f, 1.35f);
        v3 dir = {{cosf(ya) * cosf(el), sinf(el), sinf(ya) * cosf(el)}};
        v3 a = v3_add(base, (v3){{0.0f, rng_rangef(&st, -0.28f, -0.05f), 0.0f}});
        v3 b2 = v3_add(a, v3_scale(dir, ln));
        dec_stick(a, b2, 0.055f, wood, sg);
        int nsec = 2 + (int)(rng_next(&st) % 2u);
        for (int q = 0; q < nsec; q++) {
          float ya2 = ya + rng_rangef(&st, -0.9f, 0.9f);
          float el2 = el - rng_rangef(&st, 0.45f, 0.85f);   // dead wood sags
          float ln2 = ln * rng_rangef(&st, 0.55f, 0.75f);
          v3 d2 = {{cosf(ya2) * cosf(el2), sinf(el2), sinf(ya2) * cosf(el2)}};
          dec_stick(b2, v3_add(b2, v3_scale(d2, ln2)), 0.040f,
                    v3_scale(wood, 0.94f), sg);
        }
      }
    }
  }
  ao_off();
  int n = g_scene_floats;
  int cap = (int)(sizeof g_mesh / sizeof g_mesh[0]);
  if (g_mesh_floats + n > cap) n = cap - g_mesh_floats;
  if (n < g_scene_floats && !g_mesh_drops++)
    fprintf(stderr, "mesh: world vertex budget exhausted — geometry dropped\n");
  memcpy(g_mesh + g_mesh_floats, g_scene, (size_t)n * sizeof(float));
  g_mesh_floats += n;
  g_scene_floats = save_floats; g_scene_want = save_want;
  g_scene_peak = save_peak; g_scene_drops = save_drops;
}

static void mesh_decor(void) {
  int save_floats = g_scene_floats, save_want = g_scene_want;
  int save_peak = g_scene_peak;
  long save_drops = g_scene_drops;
  g_scene_floats = 0;

  const theme_t *th = &g_theme;
  int drifts = th->surf == SURF_SAND;
  v3 wind = wind_dir();
  mat_set(WORLD_GROUND_R, 0.0f);
  // scene_push multiplies its AO by g_ao_mul, which is FIGURE state: a global the
  // corpse bands write per band, and world_upload runs at the top of a frame with
  // whatever the PREVIOUS frame's last band left in it.
  g_ao_mul = 1.0f;
  // A six-centimetre stone does not self-occlude the way a wall does.
  ao_set(0.0f, 1.0f / 0.16f, 0.78f);

  // Sand drifts stay face-attached (a drift IS its wall's wind shadow), but
  // the walk is canonical-face + reflected-twin now: the old form rotated the
  // planned drift verbatim, which put every mirrored drift on its wall's
  // WINDWARD side — half the arena's loudest wind cue pointing the wrong way.
  if (drifts)
    for (int i = 0; i < g_num_solids; i++) {
      const solid_t *s = &g_solids[i];
      if (!scat_host_ok(s)) continue;
      // THE SHELL BREAKS THE TWIN-REFLECTION RULE: wall 0's twin is wall 1,
      // and the "same-normal face of the twin" is then the OUTER face — the
      // reflected drift landed outside the arena or as a mirror-wound black
      // sliver at the inner base (found as exactly that). The boundary gets
      // the honest treatment instead: its INNER face banks a drift when the
      // wind says so, emitted once, wind-true — the edge of the world may be
      // asymmetric because it is not cover.
      int shell = s->pen == 0;
      for (int f = 0; f < 4; f++) {
        v3 o = {{f == 0 ? 1.0f : f == 1 ? -1.0f : 0.0f, 0.0f,
                 f == 2 ? 1.0f : f == 3 ? -1.0f : 0.0f}};
        float base = f == 0 ? s->max.x : f == 1 ? s->min.x
                   : f == 2 ? s->max.z : s->min.z;
        float lo = (f < 2) ? s->min.z : s->min.x;
        float hi = (f < 2) ? s->max.z : s->max.x;
        if (hi - lo < 0.9f) continue;
        float cmx = (f < 2) ? base : (lo + hi) * 0.5f;
        float cmz = (f < 2) ? (lo + hi) * 0.5f : base;
        if (shell) {
          // inner face only: outward normal points at the arena centre
          if (o.x * cmx + o.z * cmz > 0.0f) continue;
        } else if (cmz < -0.001f || (fabsf(cmz) <= 0.001f && cmx <= 0.0f))
          continue;
        // The lee face gets the drift. One per face at most: a wall banked on
        // all four sides has no wind in it. BURIED is the drift mood: bigger
        // lenses, and faces that would not bank elsewhere bank here — the
        // "half-sunk" read is drifts doing the swallowing.
        int buried = th->surf == SURF_SAND && g_mood_preset == 0;
        float lee = -(o.x * wind.x + o.z * wind.z);
        if (lee <= (buried ? 0.22f : 0.35f)) continue;
        uint64_t st = (uint64_t)scat_h(0xD81Fu, (uint32_t)i, (uint32_t)f, 0) *
                      2654435761u + 1;
        float dl = rng_rangef(&st, 0.30f, 0.62f) * (buried ? 1.7f : 1.0f);
        float dh = rng_rangef(&st, 0.055f, 0.135f) * lee * (buried ? 1.5f : 1.0f);
        float c0 = lo + rng_rangef(&st, 0.05f, 0.35f) * (hi - lo);
        float c1 = hi - rng_rangef(&st, 0.05f, 0.35f) * (hi - lo);
        if (c1 - c0 <= 0.5f) continue;
        // A drift is a LENS, not a levee: on the shell's 48 m faces the
        // fractional draw made twenty-metre monsters. Cap the run and let
        // the window land where the draw says.
        if (c1 - c0 > 4.5f) {
          float m0 = rng_rangef(&st, c0, c1 - 4.5f);
          c0 = m0; c1 = m0 + 4.5f;
        }
        v3 a0 = (f < 2) ? (v3){{base, 0.0f, c0}} : (v3){{c0, 0.0f, base}};
        v3 a1 = (f < 2) ? (v3){{base, 0.0f, c1}} : (v3){{c1, 0.0f, base}};
        v3 col = v3_scale(th->ground, 1.06f);
        dec_drift(a0, a1, o, dl, dh, col, 1.0f);
        if (shell) continue;   // the boundary emits once, wind-true
        // A host centred on the origin is its own twin: the reflection maps
        // this face onto ITSELF, so a second emit would overlap the first —
        // one drift on the one lee face is already the whole pair.
        if (fabsf(s->min.x + s->max.x) < 0.01f &&
            fabsf(s->min.z + s->max.z) < 0.01f)
          continue;
        // The twin: reflect through the host's centre along the face axis and
        // NEGATE the outward direction — after the rotation the drift then
        // extends off the twin solid's same-normal face, its true lee. The
        // endpoints are REVERSED because reflect-then-rotate is an improper
        // transform: without the swap every twin drift is mirror-wound, and
        // with culling on that is a black hole in the ground, not a drift.
        int axis = f < 2 ? 0 : 1;
        v3 q0 = scat_reflect(a0, s, axis), q1 = scat_reflect(a1, s, axis);
        dec_drift(q1, q0, v3_scale(o, -1.0f), dl, dh, col, -1.0f);
      }
    }
  scat_emit();

  // BEYOND THE SHELL — two to four mesa-scale silhouette prisms, 60-100 m
  // out and 8 m or taller, never in g_solids: render-only mass for the
  // horizon third of every eye-level frame, sold for free by the aerial
  // perspective's height falloff. Emitted as symmetric PAIRS so both
  // players' horizons are equivalent; well inside ZFAR from any eye.
  {
    ao_off();
    // ONE pair, deep in the haze: a second pair doubled the fragment bill of
    // every sky-heavy frame (the world shader is not cheap, and a prism's
    // pixels are exactly the sky pixels that used to cost one gradient) —
    // measured +9.5 % on a one-thread llvmpipe sky frame with two pairs,
    // which over-reports fragment cost but points the right way.
    int npr = 1;
    for (int k = 0; k < npr; k++) {
      uint64_t st = (uint64_t)scat_h(0xE110u, (uint32_t)k, 1, 2) *
                    2654435761u + 1;
      float ang = rng_rangef(&st, 0.0f, 2.0f * F_PI);
      float r = rng_rangef(&st, 74.0f, 100.0f);
      float hgt = rng_rangef(&st, 9.0f, 14.0f);
      float wx = rng_rangef(&st, 4.0f, 9.0f), wz = rng_rangef(&st, 2.5f, 5.0f);
      // The prisms run 20 m BELOW the floor plane: there is no terrain out
      // there, so a base sitting at y = 0 showed a base LINE against the
      // sky's below-horizon gradient from any raised camera — levitating
      // scenery. With the base buried, every camera reads them as standing
      // behind the shell. A CRANE camera (harness/media, 8 m+) can still see
      // a soft seam where the prism's fogged lower half meets the darker
      // below-horizon sky — priced and accepted: a PLAY camera never can,
      // because the eye (max ~3.1 m on a crate) never clears the 3.2 m
      // shell, which occludes everything below its own top line out there.
      v3 c = {{cosf(ang) * r, (hgt - 20.0f) * 0.5f, sinf(ang) * r}};
      v3 mesa = v3_scale(th->wall, rng_rangef(&st, 0.82f, 0.94f));
      float sh = hgt * rng_rangef(&st, 0.45f, 0.65f);   // the lower shoulder
      float sx = wx * rng_rangef(&st, 0.5f, 0.8f);
      float sof = wx + sx * 0.9f;
      for (int t = 0; t < 2; t++) {
        float sg = t ? -1.0f : 1.0f;
        scene_box(dec_rot(c, sg), (v3){{wx * sg, 0, 0}},
                  (v3){{0, (hgt + 20.0f) * 0.5f, 0}}, (v3){{0, 0, wz * sg}}, mesa);
        v3 c2 = {{c.x + sof, (sh - 20.0f) * 0.5f, c.z}};
        scene_box(dec_rot(c2, sg), (v3){{sx * sg, 0, 0}},
                  (v3){{0, (sh + 20.0f) * 0.5f, 0}},
                  (v3){{0, 0, wz * 0.8f * sg}}, v3_scale(mesa, 0.96f));
      }
    }
  }

  // What a dead tree sheds: a few fallen sticks at each trunk's base — the
  // ground story that ties the crown to the floor without any aerial twigs.
  if (g_meta.tree[0] >= 0) {
    const solid_t *ts = &g_solids[g_meta.tree[0]];
    v3 base = {{(ts->min.x + ts->max.x) * 0.5f, 0.0f,
                (ts->min.z + ts->max.z) * 0.5f}};
    v3 wood = {{0.28f, 0.24f, 0.19f}};
    for (int t = 0; t < 2; t++) {
      float sg = t ? -1.0f : 1.0f;
      uint64_t st = (uint64_t)scat_h(0x716Cu, 4, 5, 6) * 2654435761u + 1;
      int ntw = 3 + (int)(rng_next(&st) % 3u);
      for (int k = 0; k < ntw; k++) {
        float ang = rng_rangef(&st, 0.0f, 6.283f);
        float d = rng_rangef(&st, 0.25f, 0.90f);
        v3 a = v3_add(base, (v3){{cosf(ang) * d, 0.012f, sinf(ang) * d}});
        float ya2 = rng_rangef(&st, 0.0f, 6.283f);
        v3 dir = {{cosf(ya2), 0.05f, sinf(ya2)}};
        dec_stick(a, v3_add(a, v3_scale(dir, rng_rangef(&st, 0.18f, 0.42f))),
                  0.014f, v3_scale(wood, rng_rangef(&st, 0.9f, 1.05f)), sg);
      }
    }
  }
  ao_off();

  int n = g_scene_floats;
  int cap = (int)(sizeof g_mesh / sizeof g_mesh[0]);
  if (n > DECOR_MAX_VERTS * VERT_FLOATS) n = DECOR_MAX_VERTS * VERT_FLOATS;
  if (g_mesh_floats + n > cap) n = cap - g_mesh_floats;
  if (n < g_scene_floats && !g_mesh_drops++)
    fprintf(stderr, "mesh: decor budget exhausted — geometry dropped\n");
  memcpy(g_mesh + g_mesh_floats, g_scene, (size_t)n * sizeof(float));
  g_mesh_floats += n;

  g_scene_floats = save_floats; g_scene_want = save_want;
  g_scene_peak = save_peak; g_scene_drops = save_drops;
  g_scene_dirty = 1;
}

// --- figure skinning: swept prisms wrapped around the solved pose ----------
// The pose is a stick figure; the body is a handful of "chains" — polylines of nodes,
// each carrying a cross-section, a roll reference and a colour — swept into closed low-
// poly prisms.

#define FIG_KMAX 20   // profile vertices
#define FIG_NMAX 10   // nodes per chain

typedef struct {
  v3    p;          // joint position
  v3    col;        // color of the band ARRIVING here (node 0: its end cap)
  v3    bcol;       // color of the bevel facet AT this node (knee/elbow pad)
  float rx, rz;     // cross-section half extents (rx along the chain's roll ref)
  float roll;       // extra roll of the cross-section here (radians) — the
                    // torso uses it to twist the mesh with the spine
  float ao;         // 0 = just the figure's height ramp; >0 scales it for the
                    // band arriving here — armpits, under the helmet brim and under the
                    // carrier are cavities, and no height ramp knows that
} fnode_t;

typedef struct { v3 c, ny, v[FIG_KMAX]; int k; } fring_t;

// Unit cross-sections, traversed counter-clockwise around the sweep axis (the
// band/cap winding below depends on that orientation).
//
// Two families, and the family decides how the band SHADES:
//   4 / 6 / 8  authored facets — a chamfered box. Flat-shaded, because the
//              facet IS the look: crates, receivers, butt pads.
//   12/16/20   the unit circle. Smooth-shaded (see fig_band), which is what
//              turns a 16-gon into a barrel instead of a visible 16-gon. The
//              circle is generated rather than typed out, so adding a
//              resolution is one entry in FIG_KS.
static const float FIG_FACET[3][8][2] = {
  {{1, 1}, {-1, 1}, {-1, -1}, {1, -1}},
  {{1, 0.52f}, {0, 1}, {-1, 0.52f}, {-1, -0.52f}, {0, -1}, {1, -0.52f}},
  {{1, 0.42f}, {0.42f, 1}, {-0.42f, 1}, {-1, 0.42f},
   {-1, -0.42f}, {-0.42f, -1}, {0.42f, -1}, {1, -0.42f}},
};
static const int FIG_KS[6] = {4, 6, 8, 12, 16, 20};
static float g_prof[6][FIG_KMAX][2];
static int   g_prof_ready;

// Half-step offset (i+0.5): it puts EDGE MIDPOINTS on the cross-section axes
// instead of vertices, so a 12-gon barrel presents a face to the light where a
// vertex would otherwise present a hard edge.
static void fig_prof_init(void) {
  for (int s = 0; s < 3; s++)
    for (int i = 0; i < FIG_KS[s]; i++) {
      g_prof[s][i][0] = FIG_FACET[s][i][0];
      g_prof[s][i][1] = FIG_FACET[s][i][1];
    }
  for (int s = 3; s < 6; s++) {
    int k = FIG_KS[s];
    for (int i = 0; i < k; i++) {
      float a = ((float)i + 0.5f) * F_TAU / (float)k;
      g_prof[s][i][0] = cosf(a);
      g_prof[s][i][1] = sinf(a);
    }
  }
  g_prof_ready = 1;
}

// How many profile vertices the figure being built right now may use.
static int g_fig_k = FIG_KMAX;
// A CEILING on the tier the next figure may use, in the same batch-state idiom as
// mat_set/ao_set/g_fig_diss. 0 = whatever the distance says.
static int g_fig_kcap;
// BUILDING THE LOCAL BODY FOR THE FIRST-PERSON CAMERA: the figure minus its
// head (the camera is inside it), minus its arms and rifle (the viewmodel pass
// owns both, at hero tier, welded to the camera). What remains — torso, kit,
// legs, boots — is what a player looking down at himself actually has.
static int g_fig_fp;
// The FP body is slid BACK along the aim so the camera sits over the collar
// instead of inside the chest: the pose is world-space and the camera is the
// figure's own eye, so drawn in place the near plane lands mid-torso and a
// look downward photographs the inside of the plate carrier.
static v3 g_fig_fp_off;

// ...AND THE TIER ITSELF, because "hero or not" is two states and a weapon needs three.
// 2 = the first-person view, which can resolve a 3 mm pin. 1 = a third-person figure
// inside about twelve metres, where a control still covers a pixel. 0 = past that,
// where every small part is sub-pixel and its only remaining cost is the vertex and the
// shadow-map triangle. It is derived from g_fig_k rather than carrying its own distance,
// so it can never disagree with the profile tier the same figure is being built at.
//
// THIS IS THE LOD THE THIRD-PERSON WEAPON NEVER HAD. A figure is 7260 tris at k=16 and
// the gun was 1384 of them — built from faceted boxes, which the profile tier does not
// touch — so at 25 m it was 23 % of a body forty pixels tall. Both weapons roughly
// doubled their part count in this pass; without a tier that would have been +8 % of
// scene geometry at 20 bots, which is exactly the wrong place to spend it.
static int gun_tier(void) { return g_gun_hero ? 2 : (g_fig_k >= 12 ? 1 : 0); }

static int fig_pi(int k) {
  return k <= 4 ? 0 : k <= 6 ? 1 : k <= 8 ? 2 : k <= 12 ? 3 : k <= 16 ? 4 : 5;
}
// Round profiles shade smooth; authored facets shade flat. One rule, so a part
// changes its whole look by changing one number.
static int fig_round(int k) { return k >= 12; }

// One ring in an explicit frame: `ny` is the sweep direction, `nx` the profile's
// first axis (already perpendicular to ny).
static fring_t fig_ring(v3 c, v3 ny, v3 nx, float rx, float rz, int k) {
  if (!g_prof_ready) fig_prof_init();
  if (k > g_fig_k) k = g_fig_k < 4 ? 4 : g_fig_k;
  fring_t r;
  r.k = k;
  r.c = c;
  r.ny = ny;
  v3 nz = v3_cross(nx, ny);
  const float(*pr)[2] = g_prof[fig_pi(k)];
  for (int i = 0; i < k; i++)
    r.v[i] = v3_add(c, v3_add(v3_scale(nx, pr[i][0] * rx),
                              v3_scale(nz, pr[i][1] * rz)));
  return r;
}

// Circumradius of a profile: the bevel has to be sized off the FURTHEST
// profile vertex, not off rx/rz, or the corners of the cross-section fold
// through each other on the inside of a sharp bend.
static float fig_rmax(int k, float rx, float rz) {
  if (!g_prof_ready) fig_prof_init();
  const float(*pr)[2] = g_prof[fig_pi(k)];
  float m = 0;
  for (int i = 0; i < k; i++) {
    float x = pr[i][0] * rx, z = pr[i][1] * rz;
    m = fmaxf(m, x * x + z * z);   // sqrt commutes with max: one root suffices
  }
  return sqrtf(m);
}

static float g_fig_y;  // ground height of the figure currently being built
static v3    g_cam_eye;      // where the frame is being viewed from

// Quad band a -> b (b is the later ring along the sweep).
static void fig_ring_nrm(const fring_t *r, v3 sweep_from, v3 sweep_to, v3 *out) {
  int k = r->k;
  v3 ts = v3_sub(sweep_to, sweep_from);
  for (int i = 0; i < k; i++) {
    v3 x = v3_cross(ts, v3_sub(r->v[(i + 1) % k], r->v[(i + k - 1) % k]));
    out[i] = v3_dot(x, x) > 1e-16f ? v3_norm(x) : r->ny;
  }
}

// A FIGURE THAT IS DISSOLVING LOSES ITS SURFACE IN CHUNKS. This pass has no textures
// and no alpha, so a fade is not available; what IS available is dropping geometry, and
// dropping it on a SPATIAL hash rather than per triangle is what makes the body break
// into pixel-sized pieces instead of shimmering.
static float g_fig_diss;
// ...quantised RELATIVE to the body's own origin and hashed with its own seed:
// a world-locked grid makes two overlapping corpses erode in exactly the same
// holes, which reads as one shared hole rather than two bodies.
static v3       g_fig_diss_o;
static uint32_t g_fig_diss_seed;
static int fig_diss_cut(v3 p) {
  if (g_fig_diss <= 0.0f) return 0;
  p = v3_sub(p, g_fig_diss_o);
  // 12.5 cells/m = 80 mm.
  uint32_t h = fx_hash(g_fig_diss_seed ^
                       (uint32_t)(int)(p.x * 12.5f) * 73856093u ^
                       (uint32_t)(int)(p.y * 12.5f) * 19349663u ^
                       (uint32_t)(int)(p.z * 12.5f) * 83492791u);
  // ONE THRESHOLD, AND IT MOVES FAST. A travelling front was tried and cut: an edge the
  // eye can follow only earns its keep if it is given time to travel, and time is
  // exactly what a body vanishing should NOT take — it turns a punctuation mark into a
  // scene.
  return (float)(h >> 8) * (1.0f / 16777216.0f) < g_fig_diss;
}

// Quad band a -> b with EXPLICIT per-vertex normals (round profiles).
static void fig_band_smooth(const fring_t *a, const fring_t *b,
                            const v3 *na, const v3 *nb, v3 col) {
  for (int i = 0; i < a->k; i++) {
    int j = (i + 1) % a->k;
    if (fig_diss_cut(a->v[i])) continue;
    scene_push(a->v[i], na[i], col);
    scene_push(b->v[i], nb[i], col);
    scene_push(b->v[j], nb[j], col);
    scene_push(a->v[i], na[i], col);
    scene_push(b->v[j], nb[j], col);
    scene_push(a->v[j], na[j], col);
  }
}

// Flat band: one normal per quad, from the real quad diagonals, so a tapered or sheared
// band still shades correctly.
static void fig_band(const fring_t *a, const fring_t *b, v3 col) {
  for (int i = 0; i < a->k; i++) {
    int j = (i + 1) % a->k;
    if (fig_diss_cut(a->v[i])) continue;
    v3 n = v3_cross(v3_sub(b->v[j], a->v[i]), v3_sub(a->v[j], b->v[i]));
    if (v3_dot(n, n) < 1e-12f) n = v3_cross(a->ny, v3_sub(a->v[j], a->v[i]));
    if (v3_dot(n, n) < 1e-12f) n = a->ny;
    scene_quad(a->v[i], b->v[i], b->v[j], a->v[j], v3_norm(n), col);
  }
}

// Flat end cap; sign +1 closes the ring facing along its own sweep direction. ...and
// the SAME broken edge on a round part.
static void fig_cap(const fring_t *r, float sign, v3 col_in) {
  v3 n = v3_scale(r->ny, sign);
  v3 col = col_in;
  fring_t in;
  const fring_t *f = r;
  if (g_cham > 0.0f) {
    // Mean radius from the profile itself rather than from rx/rz, which this
    // function does not have: the inset is a scale about the ring's centre, so
    // it is uniform on a circle and proportional on an ellipse, and a ring
    // smaller than twice the chamfer keeps its flat cap instead of collapsing
    // into a cone point (2 degenerate triangles per vertex — figcheck's degen
    // tier, and the reason the ratio is checked rather than assumed).
    float rm = 0.0f;
    for (int i = 0; i < r->k; i++) rm += v3_len(v3_sub(r->v[i], r->c));
    rm /= (float)r->k;
    if (rm > g_cham * 2.2f) {
      float sc = 1.0f - g_cham / rm;
      in = *r;
      in.c = v3_add(r->c, v3_scale(n, g_cham));
      for (int i = 0; i < r->k; i++)
        in.v[i] = v3_add(v3_add(r->c, v3_scale(v3_sub(r->v[i], r->c), sc)),
                         v3_scale(n, g_cham));
      // The bevel band, through the SAME cham_arris the box's edges use — spelling
      // the wear argument out a second time here is how the two silently drift.
      float rsave = g_mat_rough;
      v3 ecol = cham_arris(col_in, &g_mat_rough);
      for (int i = 0; i < r->k; i++) {
        int j = (i + 1) % r->k;
        if (fig_diss_cut(r->v[i])) continue;
        // Outward radial at the band's mid-edge, averaged with the axial normal: a
        // 45-degree bevel points exactly between the side it came from and the face it
        // leads to.
        v3 rad = v3_sub(v3_scale(v3_add(r->v[i], r->v[j]), 0.5f), r->c);
        v3 bn = v3_norm(v3_add(v3_norm(rad), n));
        scene_poly4(r->v[i], r->v[j], in.v[j], in.v[i], bn, ecol);
      }
      g_mat_rough = rsave;
      f = &in;
    }
  }
  for (int i = 0; i < f->k; i++) {
    int j = (i + 1) % f->k;
    if (fig_diss_cut(f->v[i])) continue;
    if (sign > 0) { scene_push(f->c, n, col); scene_push(f->v[j], n, col); scene_push(f->v[i], n, col); }
    else          { scene_push(f->c, n, col); scene_push(f->v[i], n, col); scene_push(f->v[j], n, col); }
  }
}

// Rotate v by the minimal rotation that maps unit a onto unit b. Antiparallel
// has no minimal rotation; keeping v is still a valid frame vector there.
static v3 v3_rot_align(v3 v, v3 a, v3 b) {
  v3 ax = v3_cross(a, b);
  float s = v3_len(ax);
  if (s < 1e-5f) return v;
  return v3_rot_axis(v, v3_scale(ax, 1.0f / s), atan2f(s, v3_dot(a, b)));
}

// Per-band value jitter SCALE, batch state in the same sense as mat_set/ao_set: it
// belongs to a PART, not to a vertex or a call argument. 1 = the authored amount, 0 =
// off.
static float g_fig_jit = 1.0f;

// Sweep a whole chain.
static v3 fig_perp(v3 ax, v3 hint) {
  v3 nx = v3_sub(hint, v3_scale(ax, v3_dot(hint, ax)));
  if (v3_dot(nx, nx) < 1e-8f) nx = v3_cross(ax, (v3){{0, 1, 0}});
  if (v3_dot(nx, nx) < 1e-8f) nx = v3_cross(ax, (v3){{1, 0, 0}});
  return v3_norm(nx);
}

static void fig_chain(const fnode_t *src, int nsrc, int k, int cap0, int cap1, v3 hint) {
  fnode_t nd[FIG_NMAX];
  int n = 0;
  for (int i = 0; i < nsrc && n < FIG_NMAX; i++)  // drop coincident nodes:
    if (!n || v3_len(v3_sub(src[i].p, nd[n - 1].p)) > 1e-4f) nd[n++] = src[i];
  if (n < 2) return;
  // THE LOD CLAMP BELONGS HERE, NOT NEXT TO THE RING LOOP: the bevel below is sized off
  // fig_rmax(k, ...), i.e. off the circumradius of the profile the rings will ACTUALLY
  // be built with.
  if (k > g_fig_k) k = g_fig_k < 4 ? 4 : g_fig_k;
  struct { v3 c, ny, col; float rx, rz, roll, ao; int hard; } rr[FIG_NMAX * 2];
  int nr = 0;
  for (int i = 0; i < n; i++) {
    v3 dp = i > 0 ? v3_sub(nd[i].p, nd[i - 1].p) : (v3){{0, 0, 0}};
    v3 dn = i + 1 < n ? v3_sub(nd[i + 1].p, nd[i].p) : (v3){{0, 0, 0}};
    float lp = v3_len(dp), ln = v3_len(dn);
    if (lp > 1e-5f) dp = v3_scale(dp, 1.0f / lp);
    if (ln > 1e-5f) dn = v3_scale(dn, 1.0f / ln);
#define FIG_PUSH(CEN, AXIS, COL) \
  rr[nr].c = (CEN); rr[nr].ny = (AXIS); rr[nr].col = (COL); rr[nr].hard = 1; \
  rr[nr].rx = nd[i].rx; rr[nr].rz = nd[i].rz; rr[nr].roll = nd[i].roll; \
  rr[nr].ao = nd[i].ao > 0.0f ? nd[i].ao : 1.0f; nr++
    if (i == 0) {
      FIG_PUSH(nd[i].p, dn, nd[i].col);
    } else if (i == n - 1) {
      FIG_PUSH(nd[i].p, dp, nd[i].col);
    } else {
      float th = acosf(f_clamp(v3_dot(dp, dn), -1.0f, 1.0f));
      float rm = fig_rmax(k, nd[i].rx, nd[i].rz);
      float e = rm * tanf(fminf(th, 2.55f) * 0.5f) * 1.09f;  // margin over exact
      float lim = 0.46f * fminf(lp, ln);  // a bevel may never eat a whole bone
      float sh = 1.0f;
      if (e > lim) { sh = lim / e; e = lim; }
      // A joint can only be as fat as its bones are long: when the bevel had to be
      // clipped, THIN the cross-section by the same factor instead of letting the inner
      // faces cross.
      if (e < 0.004f) {  // straight enough: one ring, no facet, shade across it
        FIG_PUSH(nd[i].p, v3_norm(v3_add(dp, dn)), nd[i].col);
        rr[nr - 1].hard = 0;
      } else {
        // The bevel pair below is GEOMETRY — it has to exist or the inner faces fold
        // through each other.
        int soft = th < 0.60f;
        FIG_PUSH(v3_add(nd[i].p, v3_scale(dp, -e)), dp, nd[i].col);
        rr[nr - 1].rx *= sh; rr[nr - 1].rz *= sh;
        rr[nr - 1].hard = !soft;
        FIG_PUSH(v3_add(nd[i].p, v3_scale(dn, e)), dn, nd[i].bcol);  // the facet
        rr[nr - 1].rx *= sh; rr[nr - 1].rz *= sh;
        rr[nr - 1].hard = !soft;
      }
    }
#undef FIG_PUSH
  }
  if (nr < 2) return;
  fring_t rings[FIG_NMAX * 2];
  v3 nx = fig_perp(rr[0].ny, hint);
  for (int i = 0; i < nr; i++) {
    if (i) {
      nx = v3_rot_align(nx, rr[i - 1].ny, rr[i].ny);
      nx = v3_sub(nx, v3_scale(rr[i].ny, v3_dot(nx, rr[i].ny)));  // re-orthogonalize
      nx = v3_dot(nx, nx) > 1e-8f ? v3_norm(nx) : v3_norm(v3_cross(rr[i].ny, nx));
    }
    v3 rx = rr[i].roll != 0.0f ? v3_rot_axis(nx, rr[i].ny, rr[i].roll) : nx;
    rings[i] = fig_ring(rr[i].c, rr[i].ny, rx, rr[i].rx, rr[i].rz, k);
  }
  // Per-band variation.
  for (int i = 0; i < nr; i++) {
    unsigned hh = ((unsigned)i * 2654435761u) ^ 0x9e3779b9u;
    hh ^= hh >> 13;
    // Amplitude by PROFILE FAMILY. 4/6/8 are authored facets where the band IS
    // the look and a few percent of value variation separates a crate from an
    // extrusion. 12/16/20 are meant to read as one continuous curved surface,
    // and the same +/-3.8% there defeats the whole point of smooth-shading them:
    // a helmet, a skull and a limb each came out as a stack of visibly separate
    // rings, which is the single most "generated" thing left in the shading.
    float amp = (fig_round(k) ? 0.020f : 0.076f) * g_fig_jit;
    float jit = (1.0f - amp * 0.5f) + amp * (float)(hh & 255u) * (1.0f / 255.0f);
    float ed = i == 1 ? (fig_round(k) ? 0.94f : 0.82f)
             : (i == nr - 1 && nr > 2) ? (fig_round(k) ? 0.96f : 0.90f) : 1.0f;
    jit *= 1.0f - (1.0f - ed) * g_fig_jit;
    rr[i].col = v3_scale(rr[i].col, jit);
  }
  g_ao_mul = rr[0].ao;
  if (cap0) fig_cap(&rings[0], -1.0f, rr[0].col);
  if (!fig_round(k)) {
    for (int i = 1; i < nr; i++) {
      g_ao_mul = rr[i].ao;
      fig_band(&rings[i - 1], &rings[i], rr[i].col);
    }
  } else {
    // Round profiles shade as the surface of revolution they approximate, and that must
    // include the sweep DIRECTION: a head or a tapering limb changes radius ring to
    // ring, so two bands meeting at a shared ring compute different normals and leave a
    // horizontal seam at every node.
    v3 NA[FIG_NMAX * 2][FIG_KMAX], NB[FIG_NMAX * 2][FIG_KMAX];
    for (int i = 1; i < nr; i++) {
      fig_ring_nrm(&rings[i - 1], rings[i - 1].c, rings[i].c, NA[i]);
      fig_ring_nrm(&rings[i], rings[i - 1].c, rings[i].c, NB[i]);
    }
    for (int i = 1; i < nr - 1; i++)
      if (!rr[i].hard || !rr[i + 1].hard)
        for (int j = 0; j < k; j++) {
          v3 av = v3_add(NB[i][j], NA[i + 1][j]);
          if (v3_dot(av, av) > 1e-12f) {
            av = v3_norm(av);
            NB[i][j] = av;
            NA[i + 1][j] = av;
          }
        }
    for (int i = 1; i < nr; i++) {
      g_ao_mul = rr[i].ao;
      fig_band_smooth(&rings[i - 1], &rings[i], NA[i], NB[i], rr[i].col);
    }
  }
  g_ao_mul = rr[nr - 1].ao;
  if (cap1) fig_cap(&rings[nr - 1], 1.0f, rr[nr - 1].col);
  g_ao_mul = 1.0f;
}

// A closed ellipsoid, for JOINTS. A swept tube turning by theta needs r*tan(theta/2) of
// bone each side to fillet the corner without self-crossing, and a 0.072 m knee at 146
// degrees needs 0.235 m where the shin has 0.14 — so both bones end AT the joint and a
// ball covers the junction.
#define FIG_BALL_LAT 6   // enough that a knee's outline reads round
static void fig_ball(v3 c, float rx, float ry, float rz, v3 ax, v3 hint,
                     int k, v3 col) {
  // The six joint balls are the most visible residue a dissolve can leave: they
  // are the only near-spherical masses on the figure, so three dark spheres
  // hanging in mid-air is what a chain-only cull looks like.
  if (fig_diss_cut(c)) return;
  if (!g_prof_ready) fig_prof_init();
  if (k > g_fig_k) k = g_fig_k < 4 ? 4 : g_fig_k;
  ax = v3_norm(ax);
  v3 nx = fig_perp(ax, hint);
  v3 nz = v3_cross(nx, ax);
  const float(*pr)[2] = g_prof[fig_pi(k)];
  v3 v[FIG_BALL_LAT][FIG_KMAX], n[FIG_BALL_LAT][FIG_KMAX];
  for (int i = 0; i < FIG_BALL_LAT; i++) {
    float lat = -F_PI_2 + F_PI * (float)(i + 1) / (FIG_BALL_LAT + 1);
    float cl = cosf(lat), sl = sinf(lat);
    v3 cen = v3_add(c, v3_scale(ax, ry * sl));
    for (int j = 0; j < k; j++) {
      float ox = pr[j][0] * rx * cl, oz = pr[j][1] * rz * cl;
      v[i][j] = v3_add(cen, v3_add(v3_scale(nx, ox), v3_scale(nz, oz)));
      v3 g = v3_add(v3_scale(nx, ox / (rx * rx)),
             v3_add(v3_scale(nz, oz / (rz * rz)), v3_scale(ax, ry * sl / (ry * ry))));
      n[i][j] = v3_norm(g);
    }
  }
  v3 sp = v3_sub(c, v3_scale(ax, ry)), np = v3_add(c, v3_scale(ax, ry));
  v3 sn = v3_scale(ax, -1.0f);
  // THE FACETED FAMILY SHADES FLAT, exactly as fig_chain's bands do. The
  // gradient normals below smooth-shade whatever k the profile has, so every
  // k=8 joint was a polished sphere on a flat-faceted body — which is most of
  // why the joints read as ball bearings. One normal per facet, from the
  // facet's own winding; the smooth family (k >= 12) keeps the gradient.
  if (!fig_round(k)) {
    for (int j = 0; j < k; j++) {
      int j2 = (j + 1) % k;
      v3 fn = v3_cross(v3_sub(v[0][j], sp), v3_sub(v[0][j2], sp));
      if (v3_dot(fn, fn) > 1e-14f) fn = v3_norm(fn); else fn = sn;
      scene_push(sp, fn, col); scene_push(v[0][j], fn, col);
      scene_push(v[0][j2], fn, col);
      for (int i = 0; i + 1 < FIG_BALL_LAT; i++) {
        v3 qn = v3_cross(v3_sub(v[i + 1][j2], v[i][j]),
                         v3_sub(v[i][j2], v[i + 1][j]));
        if (v3_dot(qn, qn) > 1e-14f) qn = v3_norm(qn); else qn = n[i][j];
        scene_push(v[i][j], qn, col);
        scene_push(v[i + 1][j], qn, col);
        scene_push(v[i + 1][j2], qn, col);
        scene_push(v[i][j], qn, col);
        scene_push(v[i + 1][j2], qn, col);
        scene_push(v[i][j2], qn, col);
      }
      int L = FIG_BALL_LAT - 1;
      v3 fn2 = v3_cross(v3_sub(v[L][j2], np), v3_sub(v[L][j], np));
      if (v3_dot(fn2, fn2) > 1e-14f) fn2 = v3_norm(fn2); else fn2 = ax;
      scene_push(np, fn2, col); scene_push(v[L][j2], fn2, col);
      scene_push(v[L][j], fn2, col);
    }
    return;
  }
  for (int j = 0; j < k; j++) {
    int j2 = (j + 1) % k;
    scene_push(sp, sn, col); scene_push(v[0][j], n[0][j], col);
    scene_push(v[0][j2], n[0][j2], col);
    for (int i = 0; i + 1 < FIG_BALL_LAT; i++) {
      scene_push(v[i][j], n[i][j], col);
      scene_push(v[i + 1][j], n[i + 1][j], col);
      scene_push(v[i + 1][j2], n[i + 1][j2], col);
      scene_push(v[i][j], n[i][j], col);
      scene_push(v[i + 1][j2], n[i + 1][j2], col);
      scene_push(v[i][j2], n[i][j2], col);
    }
    int L = FIG_BALL_LAT - 1;
    scene_push(np, ax, col); scene_push(v[L][j2], n[L][j2], col);
    scene_push(v[L][j], n[L][j], col);
  }
}

// A JOINT IS NOT A BEARING. Every flexing joint here was a near-round ball — the
// shoulder 132 x 124 x 136 mm at k=16, a smooth sphere on an 8-faceted body, which is
// why it read as an action figure.
static void fig_joint(v3 c, v3 da, v3 db, float rs, float rl, float swell,
                      int k, v3 col) {
  da = v3_norm(da);
  db = v3_norm(db);
  v3 bis = v3_add(da, db);
  if (v3_dot(bis, bis) < 1e-6f) bis = da;      // folded flat: keep the parent
  bis = v3_norm(bis);
  v3 flex = v3_cross(da, db);                  // the hinge axis, zero when straight
  if (v3_dot(flex, flex) < 1e-8f) {
    // Straight: any axis perpendicular to the limb will do, and it has to be
    // chosen deterministically or the ellipsoid's facets spin as the pose
    // passes through zero flex.
    flex = v3_cross(bis, (v3){{0, 1, 0}});
    if (v3_dot(flex, flex) < 1e-8f) flex = v3_cross(bis, (v3){{1, 0, 0}});
  }
  flex = v3_norm(flex);
  // 0 straight, 1 folded to a right angle and beyond. dot(da, db) is +1 straight
  // and 0 at 90 degrees, so this is the same number the bevel above measures.
  float bend = f_clamp(1.0f - v3_dot(da, db), 0.0f, 1.0f);
  v3 out = v3_norm(v3_sub(da, db));            // away from the fold
  fig_ball(v3_add(c, v3_scale(out, rs * 0.22f * bend)),
           rs, rl * (1.0f - 0.30f * bend), rs * (1.0f + swell * bend),
           bis, flex, k, col);
}

// One hand, CLOSED AROUND A CYLINDER. The figure and the viewmodel both call this,
// because a hand built twice ends up different in the two places the player sees it —
// and the viewmodel's is the closest geometry to the camera in the game.

// A point on the grip cylinder's frame: `ax` along the axis from `gc`, `ph` around it
// measured from e1, at radius `rad`.
static v3    g_gun_trig_p;    // world contact point on the blade's rear face
static v3    g_gun_trig_n;    // unit REARWARD normal there (the pull direction)
static float g_gun_trig_s;    // ...and the 0..1 press that put them there. The
                              // hand needs the PRESS and not just the position:
                              // the digit curls as it closes, and a fig_hand
                              // argument would be a second copy of a number the
                              // weapon already knows, at two call sites.
static v3    g_gun_guard[4];  // the guard bow's polyline, world space
static float g_gun_guard_r;   // ...and the tube radius round it
// The cross-section scale the rifle is being drawn at — 1.0 in first person,
// GUN_GIRTH_3P on a figure.
static float g_gun_girth;
static v3    g_gun_blade[3];  // the trigger blade's polyline, world space
static float g_gun_blade_r;   // ...and its tube radius. Published for the same
                              // reason the guard is: it is a THIN part in open air, and
                              // the proof has to be able to ask whether a fat finger
                              // has swallowed it.
static v3    g_gun_body_c, g_gun_body_e[3];   // receiver box: centre + 3 half-axes

static float g_fig_palm_mm;   // last hand's wrist-to-knuckle chord, for `vmhand`
static float g_fig_wrist_mm;  // ...its distance from the grip axis, and whether
static int   g_fig_clamped;   // the reach clamp had to move the weapon: `vmtrig`
                              // prints both, because "the third-person hand is
                              // not on the grip" was an invisible fact for as
                              // long as nothing printed it.
static v3 hand_pt(v3 gc, v3 ga, v3 e1, v3 e2, float ax, float ph, float rad) {
  return v3_add(gc, v3_add(v3_scale(ga, ax),
         v3_add(v3_scale(e1, cosf(ph) * rad), v3_scale(e2, sinf(ph) * rad))));
}

// Digit profile: the SMOOTH family in first person, the faceted one everywhere else.
#define FIG_DIGIT_K (g_fig_k >= FIG_KMAX ? 12 : 8)
// THE TRIGGER FINGER IS AIMED, NOT AUTHORED. Three hand-tuned numbers cannot be right
// from two angles at once, and were not: the digit went straight through the guard and
// the receiver. `g_gun_trig_p` is where the blade is; this routes the index along a
// quadratic Bezier from the metacarpal head to that point, with the control point
// pushed out along the grip's own surface so the finger BOWS the way a curled one does.
static v3 fig_trig_pt(v3 a, v3 c, v3 b, float t) {
  float u = 1.0f - t;
  return v3_add(v3_scale(a, u * u), v3_add(v3_scale(c, 2.0f * u * t),
                                           v3_scale(b, t * t)));
}

// The aimed tip's own cross-section, NAMED because the pad's stand-off from the blade
// is derived from it.
#define FTIP_RX 0.82f
#define FTIP_RZ 0.78f
// A pad PRESSES on a trigger, it does not pass through it: the surface stops on the
// steel and deforms about a millimetre and a half.
#define TRIG_PAD_SQUASH 0.0015f
// How far the press slides the index's interior nodes along its own Bezier.
// 0.35 moves the middle node from 0.500 to 0.588 of the curve — a visible
// closing of the digit that costs no change in the curve's shape and therefore
// none in fig_chain's corner budget.
#define TRIG_CURL 0.35f
// A TRIGGER PULL IS A HAND CLOSING, NOT ONE FINGER SLIDING. Three more things move with
// the press: the MCP flexes, which swings the WHOLE finger and is the only part with
// real screen area; the pad ROLLS on the shoe as the shoe rotates under it; and the
// other three digits tighten on the grip.
#define TRIG_MCP  0.150f   // rad of extra knuckle flexion at full press
#define TRIG_ROLL 0.220f   // rad the pad rolls on the shoe
#define TRIG_GRIP 0.055f   // rad the other three digits tighten by

// How much air a digit keeps from the guard's wire, before the girth scale. 6.0 mm and
// not 1.5, because the push moves NODES and the swept surface between two cleared nodes
// is a chord where the arc is not — a margin sized for the nodes leaves the BAND
// between them inside the tube.
#define HAND_GUARD_CLR 0.0090f

// Push a digit node out of the trigger guard's tube.
static v3 fig_body_clear(v3 p, float r) {
  v3 rel = v3_sub(p, g_gun_body_c);
  float best = 1e9f, sgn = 1.0f;
  int axis = -1;
  for (int k = 0; k < 3; k++) {
    float h = v3_len(g_gun_body_e[k]);
    if (h < 1e-6f) return p;
    float d = v3_dot(rel, v3_scale(g_gun_body_e[k], 1.0f / h));
    float out = h + r - fabsf(d);
    if (out <= 0.0f) return p;              // already clear on this axis
    if (out < best) { best = out; axis = k; sgn = d < 0.0f ? -1.0f : 1.0f; }
  }
  float h = v3_len(g_gun_body_e[axis]);
  return v3_add(p, v3_scale(g_gun_body_e[axis], sgn * best / h));
}

static v3 fig_guard_clear(v3 p, float r) {
  // The margin scales with the weapon's own girth for the same reason the
  // radii do: the third-person rifle draws at GUN_GIRTH_3P and its digits are
  // fatter in proportion, so a fixed millimetre margin covers less of the
  // chord sag between two cleared nodes there than it does in first person.
  float need = r + g_gun_guard_r + HAND_GUARD_CLR * g_gun_girth;
  for (int e = 0; e + 1 < 4; e++) {
    v3 a = g_gun_guard[e], ab = v3_sub(g_gun_guard[e + 1], a);
    float l2 = v3_dot(ab, ab);
    float u = l2 > 1e-9f ? f_clamp(v3_dot(v3_sub(p, a), ab) / l2, 0.0f, 1.0f) : 0.0f;
    v3 c = v3_add(a, v3_scale(ab, u)), d = v3_sub(p, c);
    float len = v3_len(d);
    if (len < 1e-5f || len >= need) continue;
    p = v3_add(c, v3_scale(d, need / len));
  }
  return p;
}
