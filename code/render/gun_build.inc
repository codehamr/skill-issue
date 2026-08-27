// =============================================================================
// render/gun_build.inc  —  gun constants (BOLT_*, SRTRIG_Z, …), gun_sight_y/
// z, gun_build.
// Proof: figcheck, vmcheck, figbury, vmbore shots. One TU: included once by
// game.c, in definition order — moving a definition out is a semantic
// change.
// Contract: gun_build is ONE function for both rifles and every view, and
// uses NO RNG — both weapons are byte-identical across map seeds.
// =============================================================================
// --- the rifle: ONE model, built in gun-local space (x right, y up, z forward, origin
// at the receiver rear) and placed by whoever is holding it. ----------- The viewmodel
// and every third-person figure call THIS. The bot's rifle used to be seven boxes with
// their own dimensions — a second model of the same weapon, free to drift from the one
// the player stares at all day, and it did: the thing in a bot's hands was a different
// length, a different profile and had no optic worth the name.

// Sight line height above the gun origin — the ADS pose puts exactly this on
// the camera axis, so the HUD dot/reticle lands in the aperture's center.
#define VM_SIGHT_AR 0.062f
#define VM_SIGHT_SR 0.085f

// Where the trigger hand grips, in gun-local space: what a third-person figure
// anchors the model to, so the hands land on the gun instead of near it.
static const float GUN_GRIP[2][3] = {{0.0f, -0.034f, 0.006f},    // AR
                                     {0.0f, -0.038f, -0.036f}};  // SR

// Barrel tip in gun-local space, one row per weapon. gun_build places its muzzle
// geometry at this point and the tracer/flash spawn reads the same row through
// pose_muzzle, so a streak's origin cannot drift from the barrel the renderer actually
// draws — the drift it replaces was 1.6 m of "in front of the camera", which read as
// shooting from the eye.
#define GUN_GIRTH_3P 1.05f
static const float GUN_MUZ[2][3] = {{0.0f, 0.0225f, 0.494f},     // AR
                                    // 0.782, not 0.498: the bolt gun is a CLASS longer
                                    // than the carbine now (1.44x against 1.04x), and
                                    // this row is where the tracer and the flash learn
                                    // that. See the note at the top of the SR branch.
                                    {0.0f, 0.0160f, 0.7820f}};   // SR

// HOW FAR THE MODEL HANGS BELOW ITS OWN ORIGIN, in model units, per weapon.
static float g_gun_low[2];

// The rifle model's frame from a solved pose: exactly the anchor scene_draw_figure uses
// to place gun_build so the trigger hand lands on the pistol grip.
static float gun_sight_y(int cur);   // defined with the weapon builder below
static void pose_gun_frame(const pose_t *P, int wpn,
                           v3 *org, v3 *rt, v3 *up, v3 *fw) {
  *fw = v3_norm(P->gun_f);
  *up = v3_norm(P->gun_u);
  *rt = v3_norm(v3_cross(*fw, *up));
  const float *gp = GUN_GRIP[wpn];
  // WHERE THE RIFLE HANGS OFF THE POSE'S GUN ANCHOR, and it is not a taste number: it
  // decides whether the third-person hand is ON the grip at all.
  v3 gw = v3_add(P->gun_o, v3_add(v3_scale(*fw, 0.005f),
                                  v3_scale(*up, -0.098f)));
  // TWO WEAPON-AWARE SEATS, and this is the only place that can apply them:
  // `skel_solve` does not know which rifle it is holding, and the two rifles differ in
  // exactly the two ways a cheek weld cares about.
  static const float GUN_BUTT_Z[WPN_COUNT] = {-0.2220f, -0.2480f};  // figbury-butt
  #define GUN_SEAT_(w, gz) (GUN_BUTT_Z[w] * GUN_GIRTH_3P - (gz))
  float seat = GUN_SEAT_(WPN_AR, GUN_GRIP[WPN_AR][2]) - GUN_SEAT_(wpn, gp[2]);
  #undef GUN_SEAT_
  float rise = -(gun_sight_y(wpn) - VM_SIGHT_AR) * GUN_GIRTH_3P;
  gw = v3_add(gw, v3_add(v3_scale(*fw, seat), v3_scale(*up, rise)));
  *org = v3_sub(gw, v3_add(v3_scale(*rt, gp[0]),
               v3_add(v3_scale(*up, gp[1]), v3_scale(*fw, gp[2]))));
}

// HOW FAR THE HEAD IS CANTED, in radians, positive = crown toward the strong
// side. It lives on the ANIM rather than in `pose_t` because pose_t is asserted
// to be a flat array of v3 (the death topple rotates the whole struct as one),
// and a scalar cannot ride in there — and it is a function of the blend states
// anyway, so the draw and the proof can each ask for it rather than store it.
//
// A shooter lays his CHEEK on the stock; he does not hold his head plumb over a
// weapon that is 30 degrees off his shoulder line. Half the blade angle is the
// honest number: the shoulder line turns 30 degrees under `blade` and the head
// follows about half of that, which is what puts the eye over the sight instead
// of beside it. Scaled by ads_s so a walking figure keeps his head level.
static float anim_head_cant(const anim_t *A) {
  return 0.26f * A->ads_s;
}

// THE HEAD'S OWN FRAME, and the EYE in it — one copy, because the renderer builds this
// basis to draw the skull and the weld proof has to measure the same one or it is
// measuring a different head. `anim_head_cant` supplies the cant; everything else here
// is the draw's own construction.
static void pose_head_frame(const pose_t *P, float cant,
                            v3 *eye, v3 *hr, v3 *hu, v3 *hf) {
  v3 sp = v3_sub(P->chest, P->pelvis);
  sp = v3_dot(sp, sp) > 1e-8f ? v3_norm(sp) : (v3){{0, 1, 0}};
  v3 f = v3_norm(P->head_f);
  v3 r = v3_cross(f, sp);
  r = v3_dot(r, r) > 1e-6f ? v3_norm(r) : v3_norm(P->right);
  if (fabsf(cant) > 1e-4f) r = v3_norm(v3_rot_axis(r, f, cant));
  v3 u = v3_cross(r, f);
  if (hr) *hr = r;
  if (hu) *hu = u;
  if (hf) *hf = f;
  if (eye) *eye = v3_add(P->head, v3_add(v3_scale(u, 0.018f), v3_scale(f, 0.062f)));
}

static v3 pose_muzzle(const pose_t *P, int wpn) {
  v3 org, rt, up, fw;
  pose_gun_frame(P, wpn, &org, &rt, &up, &fw);
  const float *m = GUN_MUZ[wpn];
  // The third-person rifle draws at GUN_GIRTH_3P, so the flash/tracer anchor
  // has to sit on the SCALED barrel tip or bursts detach from the muzzle.
  return v3_add(org, v3_add(v3_scale(rt, m[0] * GUN_GIRTH_3P),
               v3_add(v3_scale(up, m[1] * GUN_GIRTH_3P),
                      v3_scale(fw, m[2] * GUN_GIRTH_3P))));
}

// The two CYLINDERS a pair of hands closes around, in gun-local space: {cx, cy, cz,
// radius, ax, ay, az}. `fig_hand` builds a hand as arcs about a cylinder, so the weapon
// has to say where its grip surfaces are — and it has to say it ONCE, or the viewmodel
// and the third-person figure grip two different rifles, which is the same defect the
// single `gun_build` was written to kill.
#define GH_GRIP 0
#define GH_FORE 1
#define GH_RELD 2   // where the support hand goes mid-reload
// ...and the THIRD-PERSON support station, which is a different number for a reason
// that is not stylistic: the figure has ARMS and the viewmodel does not.
#define GH_FORE3 3
// The radius is the surface a finger can actually touch, which on a 12-gon is the FLATS
// — cos(pi/12) = 0.966 of the authored half-extent — not the circumradius, and it is
// the smaller of the two half-extents at that station.
static const float GUN_HOLD[2][4][7] = {
  // THE PUBLISHED RADIUS IS THE ONE THE HAND CLOSES ON, so it has to be the radius the
  // grip ACTUALLY HAS along the row the fingers occupy — not an independent number.
  { { 0.0f, -0.0500f,  -0.0078f, 0.0130f,  0.0f, 0.921f,  0.386f },   // AR grip
    { 0.0f,  0.0190f,   0.3050f, 0.0123f,  0.0f, 0.0f,    1.0f   },   // AR fore-end
    // The magazine. A hand wraps its NARROW axis, so the radius is the small half-
    // extent of the mag chain at that station, not its 19 mm depth.
    { 0.0f, -0.0640f,   0.1010f, 0.0124f,  0.0f, 0.959f,  0.283f },   // AR mag
    // Third person: the REAR of the same handguard, just ahead of the delta
    // ring (the guard body spans z 0.196-0.330), which is 100 mm closer to the
    // butt than GH_FORE and exactly what buys the elbow its bend.
    { 0.0f,  0.0190f,   0.1780f, 0.0135f,  0.0f, 0.0f,    1.0f   } }, // AR fore, 3p
  // 10.4-14.1 mm measured the same way; 12.6 is the mean over the finger row.
  { { 0.0f, -0.0510f,  -0.0444f, 0.0126f,  0.0f, 0.900f,  0.436f },   // SR grip
    // 0.0139, not 0.0121: the fore-end is a k=8 chassis now, and on that profile the
    // touchable radius is min(rx, rz) * cos(pi/8) at the finger row — 0.0150 * 0.924.
    { 0.0f,  0.0130f,   0.2080f, 0.0139f,  0.0f, 0.0f,    1.0f   },   // SR fore-end
    // The sniper changes a magazine too now.
    { 0.0f, -0.0480f,   0.0640f, 0.0129f,  0.0f, 0.9997f, -0.0256f },   // SR mag
    // The sniper's fore-end is already 100 mm nearer its butt than the AR's, so
    // the third-person station moves less; it still moves, because the sniper's
    // butt is further back again.
    { 0.0f,  0.0130f,   0.1480f, 0.0152f,  0.0f, 0.0f,    1.0f   } }, // SR fore, 3p
};
// The magazine's reload travel (down, camera-side) per weapon, published ONCE beside
// the grip surfaces for the same reason they are: the model (gun_build), the
// viewmodel's hand-follow (vm_build) and the third-person hand-follow all move a hand
// with this magazine, and the third copy had already drifted to a weapon-independent
// 0.058/0.033 — the exact hand/magazine decoupling the hand-follow was written to fix.
static const float GUN_MAG_TRAVEL[2][2] = {
  [WPN_AR] = {0.060f, 0.034f},
  [WPN_SR] = {0.056f, 0.032f},
};
// Third-person rifle stylisation: at 20 m the true-scale barrel is 1-2 px and the
// class-defining prop vanishes; the genre oversizes weapons x1.2-1.5.
static float g_gun_girth = 1.0f;   // declared above fig_hand, which reads it

static void gun_hold(int cur, int which, v3 org, v3 gx, v3 gy, v3 gz,
                     v3 *c, v3 *a, float *r) {
  const float *h = GUN_HOLD[cur][which];
  float gg = g_gun_girth;
  *c = v3_add(org, v3_add(v3_scale(gx, h[0] * gg),
       v3_add(v3_scale(gy, h[1] * gg), v3_scale(gz, h[2] * gg))));
  *a = v3_norm(v3_add(v3_scale(gx, h[4]),
       v3_add(v3_scale(gy, h[5]), v3_scale(gz, h[6]))));
  *r = h[3] * gg;
}

static float gun_sight_y(int cur) { return cur == WPN_SR ? VM_SIGHT_SR : VM_SIGHT_AR; }
// ...and WHERE along the bore the shooter's eye is supposed to sit behind.
#define AR_APERTURE_Z 0.2150f
static float gun_sight_z(int cur) { return cur == WPN_SR ? SCOPE_OCU_Z : AR_APERTURE_Z; }

// The bolt cycle, as two scalars: how far the handle is LIFTED and how far it is DRAWN.
// Three beats inside one 0..1 — lift, draw and return, drop — with sines at every seam
// so nothing has a velocity step through it.
static void gun_bolt_phase(float bolt01, float *lift, float *draw) {
  float b = f_clamp(bolt01, 0.0f, 1.0f);
  *lift = b < 0.25f ? smooth01(b / 0.25f)
        : b < 0.80f ? 1.0f : 1.0f - smooth01((b - 0.80f) / 0.20f);
  *draw = b < 0.25f ? 0.0f
        : b < 0.80f ? sinf((b - 0.25f) / 0.55f * F_PI) : 0.0f;
}
// ...and the knob's world position for that phase. The rotation is about the weapon's
// own right axis through the bolt's centreline at y = 0.014.
#define BOLT_LIFT_A  -1.22f   // rad of bolt lift at full phase (~70 degrees)
// 0.016, not 0.014: the bore, the barrel chain and the bolt axis are ONE line, and
// they disagreed by 2 mm — so the handle rotated about an axis the bolt is not on.
#define BOLT_PIVOT_Y  0.016f  // the bolt axis the handle rotates about
#define BOLT_KNOB_U   0.0310f // knob station along the receiver's right
#define BOLT_KNOB_Z  -0.037f  // ...and along the bore
// 0.044: a .338 bolt travels further than a .308 one. Capped by the cheek piece — the
// shroud's rear at full draw is -0.152 against the riser's front face at -0.156.
#define BOLT_DRAW_M   0.044f  // rearward throw of a full draw
static v3 gun_bolt_knob(float bolt01, v3 org, v3 gx, v3 gy, v3 gz, float gg) {
  float lift, draw;
  gun_bolt_phase(bolt01, &lift, &draw);
  float ba = lift * BOLT_LIFT_A, u = BOLT_KNOB_U,
        vy = -0.0055f - BOLT_PIVOT_Y,  // -0.0055 = the knob BALL's VY midpoint
        wz = BOLT_KNOB_Z - draw * BOLT_DRAW_M;
  return v3_add(org, v3_add(v3_scale(gx, (u * cosf(ba) - vy * sinf(ba)) * gg),
                v3_add(v3_scale(gy, (u * sinf(ba) + vy * cosf(ba) + BOLT_PIVOT_Y) * gg),
                       v3_scale(gz, wz * gg))));
}

// Where the player's eye looks THROUGH the weapon, published by gun_build so the
// clearance proof below reads the same numbers the geometry does.
static v3    g_gun_sight_p;   // world-space centre of the aperture
static float g_gun_sight_r;   // its smaller half-extent (the tighter constraint)

// `md01` is the MAGAZINE's own 0..1 travel, separate from `rl`. Driven off the pose arc
// the magazine was 61 mm out of the well at the exact tick the seat sound played and
// did not reach home until 300 ms after the game said the mag was in — a hard A/V
// desync on the mechanic's defining moment — and 75 mm of travel never produced an
// empty-magwell frame at all.
// WHERE EACH TRIGGER GROUP SITS ALONG THE BORE. Both were solved against the rewritten
// `vmtrig`, and both are a compromise between two neighbours: too far back and the
// guard's rear upright runs through the palm wrapped round the pistol grip, too far
// forward and the blade leaves the finger's reach.
#define SRTRIG_Z 0.0180f
#define ARTRIG_Z 0.0400f

  #define GUNP(u, vy, wz) v3_add(org, v3_add(v3_scale(gx, (u) * gg), \
                          v3_add(v3_scale(gy, (vy) * gg), v3_scale(gz, (wz) * gg))))
  #define GBOX(u, vy, wz, hx, hy, hz, col) \
    scene_box(GUNP(u, vy, wz), v3_scale(gx, (hx) * gg), v3_scale(gy, (hy) * gg), \
              v3_scale(gz, (hz) * gg), col)
  // Placement discipline: neighboring parts INTERPENETRATE by >=1 mm with
  // different cross-sections (hidden faces, no coplanar pairs) — nothing
  // floats, nothing shares a plane.

  // Round parts are CHAINS — the same watertight swept prism the body is built from
  // (closed, correctly bevelled corners, no coplanar pairs by construction).
  #define GCH(k, c0, c1, hint, ...) do {                                      \
    fnode_t nd_[] = __VA_ARGS__;                                              \
    int n_ = (int)(sizeof nd_ / sizeof *nd_);                                  \
    for (int i_ = 0; i_ < n_; i_++)                                            \
      nd_[i_].p = GUNP(nd_[i_].p.x, nd_[i_].p.y, nd_[i_].p.z);                 \
    fig_chain(nd_, n_, k, c0, c1, hint);                                       \
  } while (0)
  #define GN(u, vy, wz, r, rr, c) \
    (fnode_t){.p = {{u, vy, wz}}, .col = c, .bcol = c, \
              .rx = (r) * gg, .rz = (rr) * gg}
  // ...and GCH_PUB is GCH that also PUBLISHES the finished polyline into g_gun_guard,
  // which vmtrig traces the trigger hand against. Spelling the four node positions out
  // a second time as the collision path is how the two silently drift: one weapon's
  // guard moves in the picture and the proof keeps testing where it used to be.
  #define GCH_PUB(k, c0, c1, hint, guard_r, ...) do {                         \
    fnode_t nd_[] = __VA_ARGS__;                                              \
    int n_ = (int)(sizeof nd_ / sizeof *nd_);                                 \
    for (int i_ = 0; i_ < n_; i_++)                                           \
      nd_[i_].p = GUNP(nd_[i_].p.x, nd_[i_].p.y, nd_[i_].p.z);                \
    fig_chain(nd_, n_, k, c0, c1, hint);                                      \
    for (int i_ = 0; i_ < 4; i_++) g_gun_guard[i_] = nd_[i_].p;               \
    g_gun_guard_r = (guard_r) * gg;                                           \
  } while (0)

static void gun_ar_receiver(v3 org, v3 gx, v3 gy, v3 gz, float gg,
                            v3 alloy, v3 alloy2, v3 upper, v3 steel,
                            v3 black, float bolt01) {
    // ---- receiver group (flat-sided: boxes) ----
    mat_set(MAT_ALLOY_R, MAT_ALLOY_M);
    // THE UPPER RECEIVER IS A SECTION, NOT A BOX. It was one 31 x 29 x 210 mm cuboid,
    // so its flank was a single unbroken plane 21 cm long and took one unbroken
    // gradient — the largest flat on the weapon and most of why it read as a slab.
    // The k=8 profile is the chamfered square, i.e. flat sides, a flat top under the
    // rail, a flat bottom onto the lower, and 45-degree SHOULDERS between them: that
    // shoulder is the single most characteristic line an AR upper has, and a cuboid
    // has none of it. Flat-shaded, because the facet IS the look on a machined part.
    // The taper is real too — an upper is deepest at the charging-handle raceway and
    // thins toward the barrel nut — and it is what stops the receiver and the
    // handguard reading as one continuous bar.
    GCH(8, 1, 1, gy, {
      GN(0, 0.0210f, -0.0195f, 0.0155f, 0.0150f, alloy),  // rear, into the tube
      GN(0, 0.0210f,  0.0300f, 0.0158f, 0.0152f, alloy),  // the raceway: deepest
      GN(0, 0.0208f,  0.1250f, 0.0151f, 0.0145f, alloy),
      GN(0, 0.0206f,  0.1900f, 0.0145f, 0.0138f, alloy),  // barrel-nut shoulder
    });
    // ONE CONTINUOUS TOP RAIL, receiver rear to handguard end — the monolithic rail
    // line that makes an HK416 read as one machine (ref: commons HK416N).
    // A PICATINNY RAIL IS THE RECEIVER'S OWN MATERIAL, not a black rubber strip: it
    // is machined into, or bolted to, the same anodized alloy. At `black` under
    // metalness 0.90 the most visible 27 cm on the weapon reflected 1.6%.
    //
    // AND IT IS A DOVETAIL, WHICH IS THE WHOLE REASON A RAIL READS AS A RAIL. One bar
    // has no section; two — a wider base with a narrower crown on it — gives the
    // trapezoid its 45-degree flanks, and the arris between them runs the entire
    // length of the weapon catching light. Two boxes for the most-looked-at line on
    // the gun.
    // THE THREE WIDTHS ARE A LADDER AND EACH RUNG IS 1.5 mm. A real rail's slots are
    // cut across a bar of constant width, so a tooth's flank is CONTINUOUS with the
    // bar's — which is the one thing figcheck cannot tell apart from a z-fight. At a
    // tooth 0.2 mm proud of the crown, 24 teeth put 285 parallel-within-1-mm pairs into
    // vmcheck's near tier. Base 10.2, crown 7.2, tooth 8.7: every neighbouring pair is
    // 1.5 mm apart, which is outside the near band on both sides.
    GBOX(0,  0.0356f, 0.155f, 0.0102f, 0.0018f, 0.169f, alloy);  // rail base
    // ...and the crown is INSET 2.5 mm at both ends for the same reason the sight's
    // hood bridge is: two boxes that share a z plane and overlap in projection are a
    // z-fight wherever they meet, and a rail sitting on a rail shares both ends.
    // The crown is the SLOT FLOOR, and a recess that sees less sky than the teeth
    // standing on it is 16% darker. Free: ao_off() is on for the whole weapon, so a
    // machined step gets no contact darkening unless it is authored.
    GBOX(0,  0.0384f, 0.155f, 0.0072f, 0.0021f, 0.1665f,
         v3_scale(alloy, 0.84f));                                // rail crown
    mat_set(MAT_ALLOY2_R, MAT_ALLOY_M);
    GBOX(0, -0.005f,  0.058f, 0.0145f, 0.0135f, 0.072f, alloy2); // lower receiver
    mat_set(MAT_ALLOY_R, MAT_ALLOY_M);
    // Every small control is CENTRED on the face it sits on and at least 2.6 mm thick,
    // so both of its own side faces clear that face by more than the depth buffer can
    // lose at across-the-arena range.
    // A PORT IS A HOLE. Authored `alloy` on an `alloy` receiver at the same roughness
    // and metalness, this rendered as literally nothing — the one gun-like feature on
    // the flank, gone. With no CSG a cut is a proud FRAME with a dark plate inside it,
    // which is also what a dust cover looks like when it is closed.
    GBOX(0.0158f, 0.020f, 0.055f, 0.0032f, 0.0079f, 0.0264f, alloy);  // port frame
    GBOX(0.0176f, 0.020f, 0.055f, 0.0026f, 0.0056f, 0.0224f, black);  // ...and its cut
    // ...AND THE SAME GROUP ON THE SIDE THE PLAYER CAN ACTUALLY SEE. Measured with per-
    // part recolour masks on the hip frame, the AR's ejection port, its bolt catch and
    // its four +x M-LOK boxes render EXACTLY 0 px, while their -x twins render 164-384
    // px: the first-person camera sits on the weapon's LEFT (the file states this
    // itself where the charging handle is placed), so the single most gun-like feature
    // on a rifle is invisible for the whole match.
    GBOX(-0.0158f, 0.0195f, 0.0550f, 0.0038f, 0.0072f, 0.0244f, alloy);  // port frame
    GBOX(-0.0176f, 0.0195f, 0.0550f, 0.0026f, 0.0052f, 0.0210f, black);  // ...and its cut
    // MIL-STD-1913 TEETH, AT THE PITCH THE STANDARD ACTUALLY SPECIFIES. Six teeth on
    // 21 mm centres across 338 mm of rail is half the real density, and a rhythm at
    // half rate does not read as a rail, it reads as six blocks. The standard is a
    // 10.008 mm slot pitch; at this weapon's scale that is 33 teeth, so the pitch here
    // is 14 mm — 24 teeth — which keeps the RHYTHM fine enough to read as machining
    // while staying inside a sane vertex count.
    //
    // HERO TIER, and that is the trade this flag exists for: 24 chamfered teeth are
    // 3168 verts in the one view that can resolve a 4.7 mm land, and ZERO in the view
    // that put them 2.6 mm proud purely to stop them z-fighting at 28 m. The dovetail
    // bar above carries the rail's silhouette at every other range.
    if (g_gun_hero)
      for (int r = 0; r < 24; r++)
        GBOX(0, 0.0400f, -0.0090f + 0.0140f * (float)r,
             0.0087f, 0.0026f, 0.0033f, alloy);
    // The controls are phosphated STEEL parts in an alloy receiver, which is a real
    // material step and the only thing that makes them read as separate pieces.
    mat_lathe(MAT_PARK_R, MAT_PARK_M);
    if (gun_tier()) {
      GBOX(0.0145f, -0.004f, 0.040f, 0.0030f, 0.0044f, 0.0050f, upper); // bolt catch
    GBOX(0.0168f, 0.0210f, 0.0275f, 0.0028f, 0.0042f, 0.0062f, alloy);  // brass deflector
      GBOX(-0.0145f, -0.006f, 0.078f, 0.0030f, 0.0038f, 0.0042f, upper); // mag release
      GBOX(-0.0155f, 0.028f, 0.056f, 0.0030f, 0.0040f, 0.0060f, upper);  // forward assist
    }
    // THE SELECTOR IS THE MOST RECOGNISABLE CONTROL ON AN AR'S LEFT FLANK, and the
    // first-person camera sits on the weapon's left — so it is the one small part with
    // a guaranteed audience, and the model had none. A boss and a lever.
    GCH(12, 1, 1, gx, {
      GN(-0.0130f, -0.0040f, 0.0270f, 0.0062f, 0.0062f, upper),
      GN(-0.0168f, -0.0040f, 0.0270f, 0.0058f, 0.0058f, upper),
    });
    if (gun_tier())
      GBOX(-0.0186f, -0.0072f, 0.0234f, 0.0022f, 0.0038f, 0.0090f, upper); // lever
    if (g_gun_hero) {
      // ...and the two takedown pins, which is how an AR comes apart. On an alloy face
      // it is the MATERIAL step that makes them read, not the 1 mm of relief.
      GCH(12, 1, 1, gx, {
        GN(-0.0132f, -0.0040f, 0.0100f, 0.0050f, 0.0050f, upper),
        GN(-0.0170f, -0.0040f, 0.0100f, 0.0046f, 0.0046f, upper),
      });
      GCH(12, 1, 1, gx, {
        GN(-0.0132f, -0.0040f, 0.1280f, 0.0050f, 0.0050f, upper),
        GN(-0.0170f, -0.0040f, 0.1280f, 0.0046f, 0.0046f, upper),
      });
    }
    mat_set(MAT_ALLOY_R, MAT_ALLOY_M);
    mat_lathe(MAT_STEEL_R, MAT_STEEL_M);
    // THE CHARGING HANDLE MOVES. The AR's reload went visually dead from the magazine
    // seating to the end — measured, 45 changed pixels of 921600 between prog 0.921 and
    // the standing idle frame — and the audio layer had already patched the same hole
    // from its own side with a sixth event, "hand to the handle", whose comment says
    // "in exactly the window where the ANIMATION also goes static".
    {
      float ch = f_clamp(bolt01, 0.0f, 1.0f);
      float dr = ch < 0.18f ? smooth01(ch / 0.18f)
               : ch < 0.62f ? 1.0f - 0.05f * (ch - 0.18f) / 0.44f
               : 1.0f - 0.95f * smooth01((ch - 0.62f) / 0.38f);
      mat_set(MAT_ALLOY_R, MAT_ALLOY_M);   // the handle itself is aluminium
      GBOX(-0.0155f, 0.026f, 0.020f - dr * 0.030f, 0.0040f, 0.0035f, 0.010f, alloy);
      mat_lathe(MAT_STEEL_R, MAT_STEEL_M);
      // The latch paddle, which is the part a hand actually hooks: it rides
      // with the handle and is what makes the pull read at all from the eye.
      GBOX(-0.0205f, 0.026f, 0.026f - dr * 0.030f, 0.0026f, 0.0032f, 0.0052f, steel);
    }
    mat_set(MAT_ALLOY2_R, MAT_ALLOY_M);
    // 23.2 mm of half-length, not 29.2: the well was 58 mm long feeding a 39 mm
    // magazine, so it presented a 9.4 mm ledge behind the mag and an 11.4 mm one in
    // front. A real fence stands about 3.5 mm proud of the body it wraps.
    GBOX(0, -0.020f, 0.1070f, 0.0146f, 0.011f, 0.0232f, alloy2); // magwell
    // Flared feed collar, proud of the well on every side — the aggressive
    // competition-magwell silhouette, and a real thickness step (>2.6 mm
    // through the well's top face) so nothing grazes.
    GBOX(0, -0.0122f, 0.1070f, 0.0158f, 0.0036f, 0.0250f, alloy2); // magwell flare
    mat_set(MAT_ALLOY_R, MAT_ALLOY_M);
}

static void gun_ar_stock(v3 org, v3 gx, v3 gy, v3 gz, float gg,
                         v3 body, v3 alloy, v3 black) {
    mat_set(MAT_ALLOY_R, MAT_ALLOY_M);  // state carried in from gun_ar_receiver's last call (task-10 split)
    // ---- stock group ----
    // THE BUFFER TUBE DID NOT COME OUT OF THE MIDDLE OF THE RECEIVER, and that
    // is a y offset the whole stock group carries. The tube's axis sat at 0.014
    // against the upper receiver's 0.021 and the bore's 0.0225: 7 mm under the
    // box it exits and 8.5 mm under the bore, on a rifle whose entire design
    // premise is that the receiver extension IS the bore line. What that looks
    // like from the first-person camera — above the weapon and on its left — is
    // 8.9 mm of bare receiver face left standing above the tube while the tube
    // hangs 5.1 mm out under the box and slides away toward the lower corner:
    // the stock reads as bolted on off-centre, and from a raking angle it reads
    // as bolted on to one SIDE, which is the shape of the complaint. Measured on
    // the reported frame, the tube's apex is 5 px off the rear face's centre in
    // a 215 px face — the horizontal was never the defect, the vertical was, and
    // perspective turned one into the other.
    // The whole group moves as ONE rigid +7 mm so nothing inside it is
    // re-authored: every burial, every proud edge and every interpenetration
    // between tube, body, nose wedge, cheek line, toe spur and pad is a
    // difference of two stations here, and a uniform translation preserves all
    // of them. POSE_BUTT_UP is the one number outside this block that measures
    // it, and it follows.
    GCH(16, 1, 1, gz, {  // buffer tube, both ends capped and buried
      // -0.178, not -0.155: THE BUTT PAD WAS NOT ATTACHED TO THE RIFLE. The tube's rear
      // cap sat at z = -0.155 and the pad spans [-0.194, -0.172], so 17 mm of open air
      // separated them and the pad rendered as a black slab floating behind the stock —
      // from either side under `vmorbit`, and in the plain hip frame, where the tube's
      // dome ends in mid-air and the ground shows through the gap 20 px before the pad
      // starts. `vmcheck` cannot see this: it tests the mesh against ITSELF for
      // manifoldness and coplanarity, and two parts that never touch are a perfectly
      // valid mesh.
      GN(0, 0.021f, -0.198f, 0.0114f, 0.0119f, alloy),
      // ...and the front cap goes INSIDE the receiver rather than ON its rear plane.
      GN(0, 0.021f, -0.0155f, 0.0126f, 0.0121f, alloy),
    });
    mat_set(MAT_POLY_R, 0.0f);
    // THE STOCK WAS FOUR NESTED SLABS AND IT READ AS LUGGAGE. Body, nose wedge, cheek
    // line and toe spur were four axis-aligned cuboids stacked on one another, so every
    // join was a hard step between two parallel planes and the whole assembly had one
    // value and no draft anywhere. A stock is a SWEPT shape: it grows out of the buffer
    // tube at the nose and flares into the pad, and a chain gives that for free —
    // fig_chain fillets every node geometrically, which is the difference between a
    // transition and a seam.
    GCH(8, 1, 1, gy, {   // stock body: tube collar -> flared butt
      // ...AND IT IS 41 mm WIDE AT THE BUTT, NOT 28.6. On a k=8 profile only 0.42 of
      // the half-extent is flat flank before the chamfers start, so a 28.6 mm body is
      // 10 mm of flat between two big bevels — which is the "thermos flask" read. The
      // rz column is what fixes it; rx is unchanged, so the comb and the pad still
      // meet the same top line.
      GN(0, 0.0210f, -0.1160f, 0.0128f, 0.0130f, body),  // rides the tube
      GN(0, 0.0205f, -0.1380f, 0.0168f, 0.0155f, body),
      GN(0, 0.0195f, -0.1720f, 0.0212f, 0.0185f, body),
      GN(0, 0.0185f, -0.1975f, 0.0248f, 0.0205f, body),  // into the pad
    });
    // The COMB is its own chain riding the body's top line, rising toward the butt the
    // way a cheek weld does. Buried in the body at every node.
    // ...and BOTH of these start INSIDE the body. A chain's end cap is a flat disc, so
    // a comb whose first node sits on the body's own surface shows that disc as a step
    // and the part reads as a plate glued on — which is exactly what the four slabs did
    // and exactly what this rebuild exists to stop. Buried, the chain emerges out of
    // the body instead of landing on it.
    GCH(8, 1, 1, gy, {   // comb: rises toward the butt, the way a cheek weld does
      GN(0, 0.0250f, -0.1180f, 0.0048f, 0.0078f, body),   // interior
      GN(0, 0.0350f, -0.1450f, 0.0060f, 0.0090f, body),
      GN(0, 0.0405f, -0.1750f, 0.0068f, 0.0094f, body),
      GN(0, 0.0415f, -0.1960f, 0.0064f, 0.0090f, body),
    });
    GCH(8, 1, 1, gy, {   // toe: what stops the butt reading as a rectangle from the side
      GN(0, 0.0120f, -0.1320f, 0.0055f, 0.0088f, body),   // interior
      GN(0, -0.0040f, -0.1700f, 0.0070f, 0.0098f, body),
      GN(0, -0.0105f, -0.1950f, 0.0076f, 0.0100f, body),
    });
    // The sling bar and the adjustment lever — the two things a hand touches on a
    // carbine stock, and both hero tier: at 25 m a 4 mm bar is sub-pixel.
    if (g_gun_hero) {
      // BOTH OF THESE WERE FLOATING, and each in its own way. The lever's top face sat
      // at -11.8 while the toe chain's underside at that station is -9.2 rising to -2.7
      // — its forward two thirds hung in open air. The sling bar engaged the toe by
      // 0.19 mm at its front face, against this function's own 1 mm minimum, and stood
      // 4.1 mm proud of the toe on each side, which reads as a battery pack. Both are
      // pulled rearward under the toe's deepest run and cut to the toe's own width.
      GBOX(0, -0.0048f, -0.1560f, 0.0092f, 0.0044f, 0.0058f, black);  // sling bar
      GBOX(0, -0.0118f, -0.1730f, 0.0058f, 0.0044f, 0.0115f, black);  // adjust lever
    }
    mat_set(MAT_POLY_R * 1.7f, 0.0f);   // rubber recoil pad: dead matte
    // THE PAD IS A CHAIN TOO, so its face is DISHED rather than flat — a butt pad is
    // moulded to a shoulder, and a flat slab across the back of a stock is the one
    // shape that says "box". 14.2 mm of half-width and up: the buffer tube ends INSIDE
    // this pad (see the node above) and a 11.9 mm ring inside a 12.8 mm box leaves
    // 0.9 mm of clearance, which is inside figcheck's 1 mm `near` tier.
    GCH(8, 1, 1, gy, {
      // ...and the PAD COVERS THE TOE. At y 0.0175 the toe's underside (-17.5) poked
      // 7.5 mm below the pad and ended in a flat disc hanging in mid-air 23 mm ahead of
      // the butt. Dropped and grown, the pad spans -19.0 to +44.0 and buries both the
      // toe and the body's top line by more than a millimetre.
      GN(0, 0.0125f, -0.1930f, 0.0315f, 0.0195f, black),
      GN(0, 0.0125f, -0.2085f, 0.0330f, 0.0205f, black),
      // -0.2220 and not -0.2185: GUN_BUTT_Z[AR] is the seat pose_gun_frame lands BOTH
      // weapons' pads on, and figbury-butt is what measures whether the model agrees
      // with it. The old pad was a box plus a rib plate ending at -0.2220; this chain
      // replaced both, and at -0.2185 the declared seat and the drawn pad were 3.5 mm
      // apart — a length contract quietly broken, not a look.
      GN(0, 0.0125f, -0.2220f, 0.0300f, 0.0185f, black),  // the dish rolls off
    });
}

static void gun_ar_gripmag(v3 org, v3 gx, v3 gy, v3 gz, float gg,
                           v3 body, v3 alloy2, v3 steel, v3 black, v3 mag,
                           float md01, float trig01) {
    // ---- grip, trigger, magazine ----
    mat_set(MAT_POLY_R, 0.0f);
    // A PISTOL GRIP HAS TO BE AS LONG AS THE HAND ON IT. This was 62 mm from the
    // receiver to the butt against a hand breadth of 90 mm (ANSUR 88, x1.13 here), so
    // the palm overhung both ends: its index-side corner ran up inside the receiver and
    // its little-finger corner hung 7 mm off the bottom in open air.
    GCH(12, 1, 1, gz, {  // pistol grip, raked back and tapering into a swell
      GN(0, -0.014f, 0.008f, 0.0121f, 0.0116f, body),
      GN(0, -0.052f, -0.0100f, 0.0114f, 0.0126f, body),
      GN(0, -0.090f, -0.0239f, 0.0095f, 0.0105f, black),
    });
    // THE BEAVERTAIL is the only part of a grip that is NEVER under the hand and always
    // in the first-person frame — the camera sits above and to the left of it. Highest
    // return per box on the whole grip, and it changes no hold station.
    scene_box(GUNP(0, -0.0215f, -0.0055f), v3_scale(gx, 0.0100f * gg),
              v3_scale(gy, 0.0092f * gg), v3_scale(gz, 0.0044f * gg), body);
    if (g_gun_hero) {
      // Texture panels as a VALUE STEP, not as holes: the magazine already taught this
      // function that `black` against the body reads as a window punched through it.
      v3 pan = v3_scale(body, 0.80f);
      for (int gp = 0; gp < 3; gp++) {
        float t = 0.24f + 0.26f * (float)gp;
        scene_box(GUNP(0, -0.014f - 0.076f * t, 0.008f - 0.0319f * t),
                  v3_scale(gx, 0.0124f * gg), v3_scale(gy, 0.0052f * gg),
                  v3_scale(gz, 0.0040f * gg), pan);
      }
    }
    // THE TRIGGER GROUP. A FINGER HAS TO FIT THROUGH THE GUARD, and this one's opening
    // was 25 x 10 mm — a curled index finger's distal phalanx is 17-19 mm thick, so
    // there was no pose in which the digit could be inside the bow rather than through
    // it.
    mat_set(MAT_ALLOY2_R, MAT_ALLOY_M);  // a trigger guard is part of the LOWER
    // A TRAPEZOID, AND A STRAP RATHER THAN A WIRE. Four right angles on a k=6 near-round
    // section is a coat hanger, and that is what it read as: fig_chain's bevel needs an
    // angle to ease, and at 90 degrees it has nothing to work with. Raking the front
    // upright forward opens both front corners to about 120 degrees, so the bow becomes
    // an arc; and a real guard is a flat strap, WIDER ACROSS than it is deep.
    // The front node also lands inside the shortened magwell instead of stopping 12.8 mm
    // short of it in open air.
    // FOUR NODES, and that is a contract: GCH_PUB copies exactly nd_[0..3] into
    // g_gun_guard, so a fifth node would leave vmtrig testing half the bow.
    GCH_PUB(6, 1, 1, gy, 0.0042f, {   // ...and publishes g_gun_guard for vmtrig
      GN(0, -0.0170f, 0.0140f, 0.0034f, 0.0034f, alloy2),  // buried in the receiver
      GN(0, -0.0505f, 0.0255f, 0.0026f, 0.0042f, alloy2),
      GN(0, -0.0510f, 0.0530f, 0.0026f, 0.0042f, alloy2),
      GN(0, -0.0180f, 0.0790f, 0.0034f, 0.0034f, alloy2),  // buried in the magwell
    });
    mat_set(MAT_ALLOY_R, MAT_ALLOY_M);
    // The blade: a real curved trigger on a pivot inside the receiver, not a
    // 5 mm box. `trig01` rotates it about that pivot — 7.2 degrees, which is
    // 4.4 mm of travel at the toe, the distance a finger actually moves.
    mat_lathe(MAT_STEEL_R, MAT_STEEL_M);
    {
      // 0.0375 and not 0.0330: the blade used to sit hard against the REAR of its own
      // bow with 2.67 mm of clearance and 30 mm of empty bow in FRONT of it, so once
      // the toe travels the right way it ends 2.06 mm INSIDE the guard tube — which
      // figcheck cannot see, because two closed meshes that intersect are nowhere
      // coplanar.
      v3 tpv = GUNP(0, -0.0168f, ARTRIG_Z);       // pivot, inside the receiver
      // POSITIVE. This basis is (right, up, FORWARD), i.e.
      float ta = trig01 * 0.210f;                 // rad, toward the shooter
      // Rotating about gx (the weapon's right) takes the toe REARWARD, which is -gz,
      // and slightly up.
      #define TRIGP(VY, WZ) v3_add(tpv, v3_add(                                \
          v3_scale(gy, ((VY) * cosf(ta) - (WZ) * sinf(ta)) * gg),               \
          v3_scale(gz, ((VY) * sinf(ta) + (WZ) * cosf(ta)) * gg)))
      // Radii carry the girth scale exactly as the positions do (every other
      // chain routes through GN, which applies it): raw, the third-person
      // blade drew 18% thinner than designed and the published radius below
      // no longer matched "the chain's own widest rz".
      fnode_t tb[3] = {
        {TRIGP(0.0016f, -0.0016f), steel, steel, 0.0026f * gg, 0.0042f * gg, 0, 0.0f},
        {TRIGP(-0.0122f, -0.0034f), steel, steel, 0.0026f * gg, 0.0040f * gg, 0, 0.0f},
        {TRIGP(-0.0226f, -0.0068f), steel, steel, 0.0024f * gg, 0.0034f * gg, 0, 0.0f}};
      fig_chain(tb, 3, 8, 1, 1, gz);
      for (int q = 0; q < 3; q++) g_gun_blade[q] = tb[q].p;
      g_gun_blade_r = 0.0042f * gg;   // the chain's own widest rz
      // THE PAD IS BEHIND THE BLADE, NOT IN FRONT OF IT. This used to publish the
      // blade's MUZZLE-side face with a forward normal, on the reasoning that "a
      // trigger finger hooks PAST the blade and pulls it back toward the palm" — which
      // is not how a rifle trigger is pulled.
      g_gun_trig_p = TRIGP(-0.0165f, -0.0073f);
      g_gun_trig_n = v3_norm(v3_sub(TRIGP(-0.0165f, -0.0110f),
                                    TRIGP(-0.0165f, -0.0073f)));
      #undef TRIGP
      // The no-go box for the trigger finger: the receiver FORWARD of the grip.
      g_gun_body_c   = GUNP(0, 0.0085f, 0.0700f);
      g_gun_body_e[0] = v3_scale(gx, 0.0155f * gg);
      g_gun_body_e[1] = v3_scale(gy, 0.0270f * gg);
      g_gun_body_e[2] = v3_scale(gz, 0.0600f * gg);
    }
    mat_set(MAT_POLY_R, 0.0f);
    {  // The magazine: a BOX, not a tube. It was a 12-gon — the smooth family,
      // A STANAG magazine is a flat-sided polymer box with broken edges, which is
      // exactly what the k=8 chamfered profile is for; built on the round profile it
      // rendered as a rounded extrusion. Its cross-section was also TRANSPOSED, the
      // same class of bug this file records on the palm: `fig_ring` lays `rx` along the
      // hint, so rx was front-to-back and rz the width — a magazine 38 mm wide and 24
      // deep, inside a magwell 29 wide and 58 deep. A real 5.56 mag is ~24 by ~39.
      // 60 mm of drop, not 150: the magazine only has to CLEAR the well to read as out,
      // and the viewmodel frustum has 18 cm of half-height at the weapon's distance, so
      // at 150 the subject of the animation spent it off screen.
      // AND SIDEWAYS, NOT ONLY DOWN. Straight-down travel moves the magazine along the
      // bottom edge of the frame, so it got LESS visible at exactly the moment it was
      // the subject (5025 px in the gun against 3209 px at prog 0.40). The camera sits
      // on the weapon's LEFT, so a component along -gx swings it out of the well INTO
      // frame — which is also what stripping one looks like from the shooter's own eye.
      float md = md01 * GUN_MAG_TRAVEL[WPN_AR][0],
            mdx = md01 * -GUN_MAG_TRAVEL[WPN_AR][1];
      mat_set(MAT_POLY_R, 0.0f);
      // ONE VALUE STEP LIGHTER THAN THE RECEIVER. At `body` the magazine was the same
      // albedo as the lower receiver and the magwell it hangs out of, so the one object
      // a reload is about had no silhouette against the part it separates from —
      // invisible for a different reason than being off screen, and just as complete.
      GCH(8, 1, 1, gz, {
        GN(mdx, -0.030f - md, 0.107f, 0.0196f, 0.0120f, mag),
        GN(mdx, -0.090f - md, 0.089f, 0.0190f, 0.0117f, mag),
        GN(mdx, -0.140f - md, 0.074f, 0.0184f, 0.0114f, mag),
      });
      // GRIP RIBS. A polymer magazine is moulded with a texture panel down each flank —
      // it is the one surface on the weapon that exists to be held with a wet hand, and
      // it was a bare 110 mm plane. Four shallow bands, on the magazine's OWN rake
      // (20.6 degrees forward), because axis-aligned ribs on a raked body walk off its
      // face — the same lesson the floor plate below is written up for.
      if (g_gun_hero) {
        v3 rup = v3_norm(v3_add(v3_scale(gy, 0.9580f), v3_scale(gz, 0.2870f)));
        v3 rfw = v3_norm(v3_add(v3_scale(gy, -0.2870f), v3_scale(gz, 0.9580f)));
        // A VALUE STEP, NOT A HOLE. At `black` against `mag` these read as four
        // rectangular windows punched through the magazine; a moulded grip panel is
        // one step down from the body it is moulded into, not four stops down.
        v3 rib = v3_scale(mag, 0.74f);
        for (int rb = 0; rb < 5; rb++) {
          v3 rc = GUNP(mdx, -0.046f - md - 0.0192f * (float)rb,
                       0.1020f - 0.0058f * (float)rb);
          scene_box(rc, v3_scale(gx, 0.0123f * gg), v3_scale(rup, 0.0030f * gg),
                    v3_scale(rfw, 0.0128f * gg), rib);
        }
      }
      // Floor plate: proud of the body on every side, which is what gives a magazine
      // its one hard horizontal line and the thing a hand actually grips when it pulls
      // one out.
      mat_set(MAT_POLY_R * 1.4f, 0.0f);
      // ON THE MAGAZINE'S OWN AXIS, NOT ON THE GUN'S. The magazine is raked 20.6
      // degrees forward (z 0.107 -> 0.060 over y -0.030 -> -0.190), so the chain's end
      // cap is TILTED, while these two boxes were axis-aligned in GUN space.
      v3 mup = v3_norm(v3_add(v3_scale(gy, 0.9580f), v3_scale(gz, 0.2870f)));
      v3 mfw = v3_norm(v3_add(v3_scale(gy, -0.2870f), v3_scale(gz, 0.9580f)));
      v3 mbt = GUNP(mdx, -0.140f - md, 0.074f);   // the chain's last node
      scene_box(v3_add(mbt, v3_scale(mup, -0.0028f * gg)),
                v3_scale(gx, 0.0134f * gg), v3_scale(mup, 0.0058f * gg),
                v3_scale(mfw, 0.0206f * gg), black);
      scene_box(v3_add(mbt, v3_scale(mup, -0.0108f * gg)),
                v3_scale(gx, 0.0122f * gg), v3_scale(mup, 0.0034f * gg),
                v3_scale(mfw, 0.0192f * gg), black);
      mat_set(MAT_POLY_R, 0.0f);
    }
}

static void gun_ar_barrel(v3 org, v3 gx, v3 gy, v3 gz, float gg,
                          v3 alloy, v3 upper, v3 steel, v3 black) {
    // ---- handguard, barrel, muzzle ----
    mat_set(MAT_POLY_R, 0.0f);
    mat_set(MAT_ALLOY_R, MAT_ALLOY_M);
    // QUAD-RAIL HANDGUARD: a chunky faceted block (k=8 — the facet IS the look),
    // SHORTER than the old tube so real barrel shows ahead of it (ref: the HK416's
    // exposed muzzle end), entered through a delta-ring taper at the receiver.
    GCH(8, 1, 1, gy, {
      // THE RAIL WAS NOT TOUCHING THE HANDGUARD. The guard's top sat at 32.8 mm and
      // the rail base's underside at 33.8 — a millimetre of open sky along the whole
      // 134 mm of guard, widening to 2.4 once both parts' 0.7 mm arrises were cut. It
      // is the gas-block sliver defect on a part fourteen times as long. The fix is the
      // guard's `rx` ALONE (0.0138 -> 0.0158), because the hand stations GH_FORE and
      // GH_FORE3 are pinned to y 0.0190 and may not move: the top rises 2 mm into the
      // rail and the bottom drops 2 mm, which the 6 o'clock stub already covers.
      GN(0, 0.019f, 0.178f, 0.0180f, 0.0154f, alloy),  // delta ring
      GN(0, 0.019f, 0.196f, 0.0158f, 0.0148f, alloy),
      GN(0, 0.019f, 0.300f, 0.0156f, 0.0144f, alloy),
      GN(0, 0.019f, 0.330f, 0.0152f, 0.0140f, alloy),
    });
    // M-LOK, NOT THREE SLABS. The guard used to carry one 112 mm bar per side and one
    // underneath — three long unbroken strips on the one part whose whole job is to be
    // GRIPPED, so the hand's own surface was the smoothest thing on the weapon. A
    // modern free-float rail is a slotted tube, and what a slot row reads as at any
    // range is a RHYTHM of dark lands: five short bars beat one long one for the same
    // vertex count, and the gaps between them do the work the slabs could not.
    mat_set(MAT_POLY_R, 0.0f);
    for (int m = 0; gun_tier() && m < 5; m++) {
      float mz = 0.2140f + 0.0225f * (float)m;
      GBOX( 0.0152f, 0.019f, mz, 0.0026f, 0.0038f, 0.0082f, black);
      GBOX(-0.0152f, 0.019f, mz, 0.0026f, 0.0038f, 0.0082f, black);
    }
    mat_set(MAT_ALLOY_R, MAT_ALLOY_M);
    // The 6 o'clock position keeps a REAL rail stub, because that is where a grip or a
    // light goes and it is the one place on the guard a bolt-on section belongs.
    GBOX(0, 0.0034f, 0.2620f, 0.0048f, 0.0034f, 0.0330f, alloy);
    if (g_gun_hero) {
      for (int r = 0; r < 5; r++)
        GBOX(0, 0.0006f, 0.2340f + 0.0140f * (float)r,
             0.0034f, 0.0026f, 0.0033f, alloy);
      // The QD sling cup at the guard's rear, on the side the camera can see: the
      // first-person camera sits on the weapon's LEFT, so furniture on +x is furniture
      // nobody renders.
      mat_lathe(MAT_PARK_R, MAT_PARK_M);
      GBOX(-0.0141f, 0.0100f, 0.2080f, 0.0026f, 0.0044f, 0.0044f, upper);
      mat_set(MAT_ALLOY_R, MAT_ALLOY_M);
    }
    // Gas tube running from the low block back under the rail into the guard.
    GCH(8, 1, 1, gy, {
      GN(0, 0.0306f, 0.186f, 0.0019f, 0.0019f, steel),
      GN(0, 0.0306f, 0.368f, 0.0019f, 0.0019f, steel),
    });
    // AN EXPOSED BARREL IS NITRIDED OR PHOSPHATED, NOT BARE. At MAT_BARE_C (0.740) and
    // MAT_STEEL_R this was brighter and smoother than the receiver, the optic and the
    // brake put together, and it sits dead centre-right of the hip frame — the value
    // PEAK of the weapon was 121 mm of undifferentiated tube. `steel` is now kept for
    // the parts that really are in the white: the crown land, the charging-handle latch
    // and the bolt.
    mat_lathe(MAT_PARK_R, MAT_PARK_M);
    GCH(16, 1, 1, gy, {  // barrel: real exposure from the guard mouth out,
      GN(0, 0.0225f, 0.334f, 0.0086f, 0.0084f, upper),  // heavy at the chamber
      GN(0, 0.0225f, 0.368f, 0.0077f, 0.0075f, upper),
      GN(0, 0.0225f, 0.404f, 0.0069f, 0.0067f, upper),
      GN(0, 0.0225f, 0.432f, 0.0062f, 0.0061f, upper),
      GN(0, 0.0225f, 0.450f, 0.0058f, 0.0057f, upper),
      GN(0, 0.0225f, 0.462f, 0.0055f, 0.0054f, upper),
    });
    // THE GAS BLOCK HAS TO CLAMP THE BARREL, and it was hovering over it. At
    // y 0.036 with 6.2 mm of half-height its underside sat at 0.0298 while the
    // barrel's top at that station is 0.0302 — 0.4 mm of overlap, which the
    // 0.7 mm arris chamfer then ate: from the side there is a sliver of SKY
    // between a gas block and the barrel it is supposed to be pinned to. The
    // house rule for this whole function is that neighbours interpenetrate by
    // at least a millimetre; this one was written before the chamfer existed
    // and never re-checked against it. Now 5.7 mm of overlap, and it grew a
    // little because a block that straddles a barrel is wider than one perched
    // on it.
    mat_lathe(MAT_PARK_R, MAT_PARK_M);
    GBOX(0, 0.0325f, 0.368f, 0.0074f, 0.0080f, 0.011f, upper);   // gas block
    // ...and a low front sight base ON it. Filled black, this weapon's silhouette was a
    // straight serrated bar 1100 px long with one block on it — a rail with a gun
    // attached. One 6.4 mm incident 170 mm ahead of the optic is what breaks the line,
    // and it lands 44 mm forward of the rail's end so it collides with nothing.
    GBOX(0, 0.0430f, 0.368f, 0.0038f, 0.0032f, 0.0080f, upper);  // front sight base
    // Sling loop at the guard's mouth — the quad-rail block carries the rail
    // furniture now, so the loop is the guard's one remaining jewel.
    mat_lathe(MAT_STEEL_R, MAT_STEEL_M);
    fig_tube(GUNP(-0.0130f, 0.0090f, 0.196f), GUNP(-0.0182f, 0.0090f, 0.196f),
             0.0068f * gg, 0.0068f * gg, 0.0040f * gg, 0.0040f * gg, 12, steel, gy);   // front sling loop
    mat_lathe(MAT_PARK_R, MAT_PARK_M);
    // A THREE-CHAMBER BRAKE, AND THE CHAMBERS ARE WHAT MAKE IT ONE. The old head was a
    // smooth cone with two 5 mm fins stuck on its flanks: fins point OUT of a brake and
    // a brake's ports are cut IN. Stepping the radius three times gives three real
    // baffle walls with a groove between each pair — that is the silhouette, and every
    // groove is a place the arris highlight lands.
    GCH(12, 1, 1, gy, {  // muzzle: stepped, so the baffles read
      GN(0, 0.0225f, 0.4550f, 0.0092f, 0.0089f, upper),
      GN(0, 0.0225f, 0.4610f, 0.0104f, 0.0101f, upper),   // baffle 1
      GN(0, 0.0225f, 0.4655f, 0.0088f, 0.0085f, black),  // groove: the port
      GN(0, 0.0225f, 0.4720f, 0.0104f, 0.0101f, upper),   // baffle 2
      GN(0, 0.0225f, 0.4765f, 0.0088f, 0.0085f, black),  // groove: the port
      GN(0, 0.0225f, 0.4840f, 0.0104f, 0.0101f, upper),   // baffle 3
      GN(0, 0.0225f, 0.4890f, 0.0084f, 0.0082f, steel),   // the crown's land
    });
    // ...AND THE PORTS ARE THE GROOVES THEMSELVES, drawn dark. Four proud boxes on the
    // brake's crown were tried first and they are the same defect the old side fins
    // were: a port is a HOLE, and a box standing on a hole is a fin. Worse, a 12-gon
    // has a facet centred exactly on the weapon's up axis, so a port box's own top face
    // landed 0.06 mm under it — parallel, overlapping, and figcheck's zfight tier went
    // 0 to 16 on that one pair. The groove nodes carry `black` instead: a band a shade
    // darker than the baffles either side of it reads as an opening, costs no geometry
    // at all, and cannot z-fight with anything.
    // THE BORE WAS READING INSIDE OUT, and the fix is a SOLID plug rather than a ring.
    // The brake caps its front with a bright 16.8 mm disc, and the old bore was an
    // ANNULUS standing 3.5 mm proud of it — so what the player saw down the muzzle was
    // a dark ring with the bright cap showing THROUGH it, which is the exact inverse of
    // a hole. Leaving the chain's cap off instead makes a watertight join with the ring
    // impossible (figcheck: open=12), so the bore is a dead-matte solid cylinder whose
    // face stands 1.2 mm past the crown: from the front, a 7.6 mm dark disc in a
    // 16.8 mm land; from the side, nothing. This is the one detail the player looks
    // straight at every time the gun comes up.
    mat_set(MAT_POLY_R * 1.7f, 0.0f);   // no sheen down a barrel
    GCH(12, 1, 1, gy, {
      GN(0, 0.0225f, 0.4830f, 0.0040f, 0.0040f, black),   // buried in the brake
      GN(0, 0.0225f, 0.4902f, 0.0038f, 0.0038f, black),
    });
    mat_lathe(MAT_PARK_R, MAT_PARK_M);
}

static void gun_ar_sight(int cur, v3 org, v3 gx, v3 gy, v3 gz, float gg,
                         v3 alloy, v3 steel, v3 black, v3 lens, float sight_y) {
    // ---- red dot: a MICRO dot, low over the rail, forward on the barrel ------
    // REBUILT 2026-08-25 (third pass). The previous body was a holographic-sight
    // chassis with a 31.6 mm hood standing over the window: at hip range the tallest
    // thing on the weapon was its own sight hood, and the group read as a lump. A
    // micro red dot is the opposite statement — a thin square frame riding LOW on a
    // short clamp, an emitter pod poking into the window's lower edge, and NOTHING
    // above the glass. The hood, the chassis and the two posts are deleted outright;
    // what survives of "a sight is a body with a window in it" is the pod and the
    // clamp, which is exactly the body a micro dot has.
    //
    // THE OPEN APERTURE IS GAMEPLAY AND IT DID NOT SHRINK: inner height stays 21.0 mm
    // (the ADS frame's own window), inner width 36.0 against the old 37.3. What got
    // thin is the RIM: 1.05 mm walls against 1.6, a 3.0 mm pane against 3.4.
    //
    // FORWARD: the aperture moved 0.1835 -> 0.2150 (AR_APERTURE_Z, one copy — figm's
    // relief reads the real station, so the eye relief grew 311 -> ~343 mm with it).
    // The clamp is CENTRED ON A RAIL SLOT: its z faces land mid-gap between teeth
    // r15/r16 and r16/r17, 3.7 mm from every tooth face — a clamp is bolted ACROSS a
    // slot, and mid-gap is also where the coplanar ladder has the most room.
    mat_set(MAT_ALLOY_R, MAT_ALLOY_M);
    // hx 0.0122 = rail base 0.0102 + 2.0 mm: the next rung of the rail's own width
    // ladder (teeth 0.0087 / base 0.0102), so no clamp face runs within 1.5 mm of a
    // rail face it overlaps. y is the OLD clamp's exactly — its bottom face against
    // the rail base's top is already in the near baseline and stays neutral.
    GBOX(0, 0.0424f, AR_APERTURE_Z, 0.0122f, 0.0060f, 0.0070f, alloy);  // clamp
    // THE EMITTER POD CARRIES THE FRAME. The frame's bottom wall passes through the
    // pod (crossings at 90 degrees, which the cross tier does not score) and the pod
    // is buried 1.4 mm in the clamp, so nothing here floats — and the pod's top ends
    // 1.5 mm ABOVE the window's lower edge, in open air inside the frame, the same
    // idiom the old posts used: a face buried just under a 1 mm wall is a near pair,
    // a nub standing proud inside the aperture is a real emitter where a real one
    // sits. 11 mm wide against a 36 mm window; the point of aim passes 9 mm over it.
    mat_set(MAT_POLY_R, 0.0f);
    GBOX(0, 0.0500f, AR_APERTURE_Z, 0.0055f, 0.0030f, 0.0050f, black);  // emitter pod
    // One brightness button on the clamp's LEFT flank — the side the camera sees.
    // Embedded 1.4 mm (the gas-block lesson: 1 mm is the floor, not the target).
    if (gun_tier())
      GBOX(-0.0132f, 0.0424f, AR_APERTURE_Z, 0.0024f, 0.0034f, 0.0030f, black);
    // The frame: a thin square tube, 1.05 mm of wall, 3.0 mm deep. Steel, not alloy —
    // the bright machined rim over a black pod is most of the sight's read.
    mat_lathe(MAT_STEEL_R, MAT_STEEL_M);
    fig_tube(GUNP(0, sight_y, AR_APERTURE_Z - 0.0015f),
             GUNP(0, sight_y, AR_APERTURE_Z + 0.0015f),
             0.01155f * gg, 0.01905f * gg, 0.0105f * gg, 0.0180f * gg, 4, steel, gy);
    // THE COATING, AS AN ANNULUS IN THE FRAME'S OWN WALL — construction unchanged
    // from the second pass, scaled to the thinner wall: outer edges 0.3 mm inside the
    // frame's, inner edges 0.5 / 0.8 mm proud into the aperture (which is what makes
    // the glass, not the steel, the tighter constraint), z faces 1.1 mm inside the
    // frame's so no pair of the four ring faces is parallel within a millimetre.
    mat_set(MAT_GLASS_R, 0.0f);
    fig_tube(GUNP(0, sight_y, AR_APERTURE_Z - 0.0004f),
             GUNP(0, sight_y, AR_APERTURE_Z + 0.0004f),
             0.01125f * gg, 0.0188f * gg, 0.0100f * gg, 0.0172f * gg, 4, lens, gy);
    g_gun_sight_p = GUNP(0, sight_y, gun_sight_z(cur));   // one copy; see figm weld
    // ...and the tighter constraint is the GLASS's inner edge, not the frame's:
    // the coating annulus narrows the open aperture from 21.0 to 20.0 mm.
    g_gun_sight_r = 0.0100f;
    mat_set(MAT_POLY_R, 0.0f);
}

static void gun_sr_action(v3 org, v3 gx, v3 gy, v3 gz, float gg,
                          v3 alloy, v3 upper, v3 black, v3 chas) {
    // ---- bolt-action precision rifle ----
    // THE TWO WEAPONS HAVE TO BE DIFFERENT CLASSES, AND LENGTH IS HOW A RIFLE SAYS SO.
    // This was 746 mm against the carbine's 716 — a ratio of 1.04 — where an AI AWM is
    // 1200 mm against an M4's 840, i.e. 1.43. At 1.04 the bolt gun was a carbine with a
    // scope on it: the one cue that reads at 25 m, in silhouette, through smoke and past
    // the aerial perspective was simply absent. The model is built at 0.852 of real
    // scale (716/840), so the AWM lands at 1030 mm and the muzzle at z 0.782.
    //
    // Everything BEHIND z 0.150 is pinned: the butt (GUN_BUTT_Z), the grip, the trigger
    // (SRTRIG_Z), the magazine and both fore-end hold stations are the contract with the
    // pose solver and the hand builder. All 284 mm of the growth is barrel.
    mat_lathe(MAT_PARK_R, MAT_PARK_M);
    // THE ACTION IS A SECTION, NOT A BOX — the same argument the AR's upper carries.
    // One 27.6 x 30.4 x 230 mm cuboid has no shoulder line anywhere, and an AI action is
    // famously FLAT-sided, so k=8 (the chamfered square) is the right family rather than
    // a round tube.
    GCH(8, 1, 1, gy, {
      GN(0, 0.0150f, -0.0880f, 0.0158f, 0.0140f, upper),  // rear: the shroud aperture
      GN(0, 0.0152f, -0.0300f, 0.0162f, 0.0144f, upper),  // deepest: lug abutments
      GN(0, 0.0152f,  0.0600f, 0.0160f, 0.0142f, upper),
      GN(0, 0.0154f,  0.1180f, 0.0152f, 0.0138f, upper),
      GN(0, 0.0156f,  0.1500f, 0.0140f, 0.0132f, upper),  // front ring
    });
    // The ejection port and the bolt raceway, let into the RIGHT flank — this is the one
    // face a bolt gun shows that a semi-auto does not, and the flank was blank.
    if (gun_tier()) {
      GBOX( 0.0134f, 0.0184f, -0.0060f, 0.0022f, 0.0072f, 0.0290f, black);  // port
      GBOX( 0.0132f, 0.0068f, -0.0330f, 0.0022f, 0.0038f, 0.0230f, black);  // raceway
    }
    // A ONE-PIECE 20-MOA RAIL, in the AR's three-rung width ladder (base / tooth /
    // crown, 1.6 mm a rung) so no two of its faces can land inside figcheck's near band.
    mat_set(MAT_ALLOY_R, MAT_ALLOY_M);
    GBOX(0, 0.0322f, 0.0350f, 0.0094f, 0.0026f, 0.1050f, alloy);  // rail base
    GBOX(0, 0.0364f, 0.0350f, 0.0062f, 0.0026f, 0.1030f,
         v3_scale(alloy, 0.84f));                                 // ...and its slot floor
    if (g_gun_hero)
      for (int r = 0; r < 5; r++)   // only the run BEHIND the mount is ever seen
        GBOX(0, 0.0384f, -0.0620f + 0.0140f * (float)r,
             0.0078f, 0.0026f, 0.0033f, alloy);
    // THE CHASSIS THE ACTION IS BEDDED IN, and the model had nothing for it: the
    // receiver hung in open air over the trigger group. Its side plates stand 2.4 mm
    // proud of the action, which is the AWM's signature line and the reason the class
    // reads as a chassis rifle rather than as a tube in a stock.
    mat_set(MAT_POLY_R, 0.0f);
    GCH(8, 1, 1, gy, {
      GN(0, 0.0000f, -0.0720f, 0.0132f, 0.0158f, chas),  // rear tang -> the spines
      GN(0, 0.0010f, -0.0200f, 0.0140f, 0.0164f, chas),
      GN(0, 0.0016f,  0.0300f, 0.0144f, 0.0166f, chas),
      GN(0, 0.0020f,  0.1000f, 0.0146f, 0.0166f, chas),
      GN(0, 0.0060f,  0.1450f, 0.0160f, 0.0170f, chas),  // into the fore-end
    });
}

static void gun_sr_magwell(v3 org, v3 gx, v3 gy, v3 gz, float gg,
                           v3 body, v3 upper, v3 steel, v3 black, v3 mag,
                           v3 chas, float md01, float trig01, float bolt01) {
    mat_set(MAT_POLY_R, 0.0f);  // state carried in from gun_sr_action's last call (task-10 split)
    // ---- magwell and a magazine that is its own class's length ----------------
    // 0.0150 and 0.0186, not 0.0166 and 0.0174: at 0.0166 the well's side faces landed
    // EXACTLY on the chassis's own k=8 side flat (rz 0.0166 over the same z run) —
    // parallel, overlapping, gap 0.000, i.e. vmcheck's zfight tier in every one of the
    // sniper's 77 poses. A well steps IN under the chassis and its fence steps back OUT
    // past it, which is also what the part does.
    GBOX(0, -0.0090f, 0.0640f, 0.0150f, 0.0158f, 0.0340f, chas);   // magwell
    GBOX(0, -0.0224f, 0.0640f, 0.0186f, 0.0034f, 0.0352f, chas);   // magwell flare
    {
      // Same two-component travel as the AR's: down AND toward the camera, so the
      // magazine gets BIGGER in frame as it leaves rather than sliding off the bottom
      // edge. See the note on the AR's `mdx`.
      float md = md01 * GUN_MAG_TRAVEL[WPN_SR][0],
            mdx = md01 * -GUN_MAG_TRAVEL[WPN_SR][1];
      mat_set(MAT_POLY_R, 0.0f);
      // 70 mm FORE-AFT, not 41. A .338 AICS magazine is ~100 mm front to back; this one
      // was less than half its class's length, which is why it read as a pistol mag.
      // rx is the fore-aft half-extent here and rz the width — the chain runs down y
      // with a gz hint, so the two swap against the AR's convention.
      GCH(8, 1, 1, gz, {
        GN(mdx, -0.0180f - md, 0.0640f, 0.0350f, 0.0140f, mag),
        GN(mdx, -0.0460f - md, 0.0638f, 0.0344f, 0.0138f, mag),
        GN(mdx, -0.0720f - md, 0.0634f, 0.0336f, 0.0135f, mag),
        GN(mdx, -0.0940f - md, 0.0630f, 0.0326f, 0.0132f, mag),
      });
      if (g_gun_hero) {   // witness slots, in the magazine's own frame
        v3 rib = v3_scale(mag, 0.74f);
        for (int rb = 0; rb < 4; rb++)
          scene_box(GUNP(mdx, -0.0300f - md - 0.0200f * (float)rb, 0.0636f),
                    v3_scale(gx, 0.0141f * gg), v3_scale(gy, 0.0030f * gg),
                    v3_scale(gz, 0.0250f * gg), rib);
      }
      // Floor plate, on the magazine's OWN axis: this one is nearly vertical (76 mm of
      // drop over 1 mm of z), so `mup` is almost gy — but it is DERIVED rather than
      // assumed, because the AR learned that lesson as a 10 mm gap between an
      // axis-aligned plate and a tilted end cap.
      mat_set(MAT_POLY_R * 1.4f, 0.0f);
      v3 mup = v3_norm(v3_add(v3_scale(gy, 0.99989f), v3_scale(gz, -0.01316f)));
      v3 mfw = v3_norm(v3_add(v3_scale(gy, 0.01316f), v3_scale(gz, 0.99989f)));
      v3 mbt = GUNP(mdx, -0.0940f - md, 0.0630f);
      scene_box(v3_add(mbt, v3_scale(mup, -0.0028f * gg)),
                v3_scale(gx, 0.0146f * gg), v3_scale(mup, 0.0058f * gg),
                v3_scale(mfw, 0.0348f * gg), black);
      // The finger hook a shooter actually pulls a stuck AICS mag out by.
      mat_set(MAT_POLY_R, 0.0f);
      GBOX(mdx, -0.0820f - md, 0.1000f, 0.0110f, 0.0116f, 0.0032f, black);
    }
    mat_lathe(MAT_STEEL_R, MAT_STEEL_M);
    // The BOLT, and it cycles. `bolt01` lifts the handle, draws it back and returns it —
    // one rotation about the receiver's axis plus one translation, so the knob traces
    // the L a real bolt handle traces.
    {
      float lift, draw;
      gun_bolt_phase(bolt01, &lift, &draw);
      float ba = lift * BOLT_LIFT_A;      // 70 degrees of bolt lift
      float bz = draw * -BOLT_DRAW_M;
      #define BOLTP(U, VY, WZ) GUNP((U) * cosf(ba) - ((VY) - BOLT_PIVOT_Y) * sinf(ba), \
                                    (U) * sinf(ba) + ((VY) - BOLT_PIVOT_Y) * cosf(ba) + BOLT_PIVOT_Y, \
                                    (WZ) + bz)
      // THE BOLT SHROUD IS THE SINGLE LOUDEST 30 cm BOLT-GUN CUE and the model had
      // none: the rear of the action is the only place a bolt gun differs from a
      // semi-auto at zero range, and it was a flat wall. It rides BOLTP, so it lifts,
      // draws and returns with the handle.
      fnode_t bs[4] = {
        {BOLTP(0, 0.0160f, -0.1080f), steel, steel, 0.0116f*gg, 0.0116f*gg, 0, 0.0f},
        {BOLTP(0, 0.0160f, -0.0980f), steel, steel, 0.0124f*gg, 0.0124f*gg, 0, 0.0f},
        {BOLTP(0, 0.0160f, -0.0900f), steel, steel, 0.0112f*gg, 0.0112f*gg, 0, 0.0f},
        {BOLTP(0, 0.0160f, -0.0760f), steel, steel, 0.0118f*gg, 0.0118f*gg, 0, 0.0f}};
      fig_chain(bs, 4, 12, 1, 1, gy);
      // The cocking indicator out its rear face — the one part of a rifle that tells
      // you, without touching it, whether it is ready.
      if (gun_tier())
        scene_box(BOLTP(0, 0.0160f, -0.1128f), v3_scale(gx, 0.0022f * gg),
                  v3_scale(gy, 0.0022f * gg), v3_scale(gz, 0.0042f * gg), black);
      fnode_t bh[2] = {   // handle root
        {BOLTP(0.0110f, 0.0160f, -0.0300f), steel, steel, 0.0048f*gg, 0.0046f*gg, 0, 0.0f},
        {BOLTP(0.0268f, 0.0052f, -0.0370f), steel, steel, 0.0042f*gg, 0.0042f*gg, 0, 0.0f}};
      fig_chain(bh, 2, 16, 1, 1, gy);
      // ...and a BALL on the handle's own axis, not a stub on the gun's. A real AI knob
      // is 22 mm across; this was 13.8 and stood vertically, so it read as a nub.
      mat_lathe(MAT_PARK_R, MAT_PARK_M);
      fnode_t bk[4] = {
        {BOLTP(0.0250f, -0.0018f, -0.0370f), upper, upper, 0.0056f*gg, 0.0056f*gg, 0, 0.0f},
        {BOLTP(0.0288f, -0.0042f, -0.0370f), upper, upper, 0.0094f*gg, 0.0094f*gg, 0, 0.0f},
        {BOLTP(0.0330f, -0.0068f, -0.0370f), upper, upper, 0.0094f*gg, 0.0094f*gg, 0, 0.0f},
        {BOLTP(0.0362f, -0.0088f, -0.0370f), upper, upper, 0.0058f*gg, 0.0058f*gg, 0, 0.0f}};
      fig_chain(bk, 4, 12, 1, 1, gz);
      #undef BOLTP
    }
    mat_set(MAT_POLY_R, 0.0f);
    // Same two fixes as the AR's grip (see there): the top ring floated 8.7 mm under the
    // receiver it is supposed to hang from, and the bottom ended 6.5 mm above the
    // little-finger edge of the hand that holds it.
    GCH(12, 1, 1, gz, {  // grip
      GN(0, -0.0072f, -0.0233f, 0.0117f, 0.0113f, chas),
      GN(0, -0.050f, -0.046f, 0.0108f, 0.0122f, chas),
      GN(0, -0.0920f, -0.0640f, 0.0092f, 0.0102f, black),
    });
    // Trigger group — same construction as the AR's, same reason (see there): the guard
    // has to have a finger-sized hole in it.
    mat_set(MAT_POLY_R, 0.0f);
    // THE REAR UPRIGHT WAS UNDER THE GRIP. Its bottom-rear corner sat 25 mm BEHIND the
    // receiver origin and directly below the pistol grip's own top node, so the wire ran
    // through the palm wrapped round that grip — 7.2 mm inside the hand's mesh by the
    // rewritten vmtrig, and no amount of clearing the DIGITS could reach it because the
    // offender was the palm.
    GCH_PUB(6, 1, 1, gy, 0.0042f, {   // ...and publishes g_gun_guard for vmtrig
      GN(0, -0.0008f, SRTRIG_Z - 0.0290f, 0.0034f, 0.0034f, body),  // in the chassis
      GN(0, -0.0505f, SRTRIG_Z - 0.0350f, 0.0026f, 0.0042f, body),
      GN(0, -0.0530f,  0.0272f, 0.0026f, 0.0042f, body),
      GN(0, -0.0006f,  0.0372f, 0.0034f, 0.0034f, body),   // buried in the magwell
    });
    mat_lathe(MAT_STEEL_R, MAT_STEEL_M);
    {
      v3 tpv = GUNP(0, -0.0010f, SRTRIG_Z);
      float ta = trig01 * 0.150f;    // a match trigger breaks shorter. Sign:
                                     // see the AR's note — this basis is
                                     // left-handed and -0.150 ran forward.
      #define TRIGP(VY, WZ) v3_add(tpv, v3_add(                                \
          v3_scale(gy, ((VY) * cosf(ta) - (WZ) * sinf(ta)) * gg),               \
          v3_scale(gz, ((VY) * sinf(ta) + (WZ) * cosf(ta)) * gg)))
      // Girth-scaled radii — same fix as the AR's blade above.
      fnode_t tb[3] = {
        {TRIGP(0.0014f, 0.0004f), steel, steel, 0.0028f * gg, 0.0038f * gg, 0, 0.0f},
        {TRIGP(-0.0126f, -0.0008f), steel, steel, 0.0028f * gg, 0.0036f * gg, 0, 0.0f},
        {TRIGP(-0.0232f, -0.0026f), steel, steel, 0.0026f * gg, 0.0032f * gg, 0, 0.0f}};
      fig_chain(tb, 3, 8, 1, 1, gz);
      for (int q = 0; q < 3; q++) g_gun_blade[q] = tb[q].p;
      g_gun_blade_r = 0.0038f * gg;   // the chain's own widest rz
      // The SHOOTER-side face, exactly as on the AR — see the note there. The
      // centreline is at WZ -0.0015 here with a 0.0027 half-thickness.
      g_gun_trig_p = TRIGP(-0.0165f, -0.0042f);
      g_gun_trig_n = v3_norm(v3_sub(TRIGP(-0.0165f, -0.0080f),
                                    TRIGP(-0.0165f, -0.0042f)));
      #undef TRIGP
      // See the AR's box: the receiver forward of the grip only.
      g_gun_body_c   = GUNP(0, 0.0130f, 0.0575f);
      g_gun_body_e[0] = v3_scale(gx, 0.0138f * gg);
      g_gun_body_e[1] = v3_scale(gy, 0.0152f * gg);
      g_gun_body_e[2] = v3_scale(gz, 0.0875f * gg);
    }
    mat_set(MAT_POLY_R, 0.0f);
}

static void gun_sr_stock(v3 org, v3 gx, v3 gy, v3 gz, float gg,
                         v3 black, v3 chas) {
    mat_set(MAT_POLY_R, 0.0f);  // state carried in from gun_sr_magwell's last call (task-10 split)
    // ---- the skeleton stock -------------------------------------------------
    // AWM THUMBHOLE CHASSIS: an upper and a lower spine run from the action and the
    // grip heel into a butt block, and the OPENING between them IS the thumbhole — no
    // CSG needed. The opening was 142 x 43 mm, which is 2.6 times a real thumbhole and
    // why the stock read as a LADDER; a web closes its rear half and brings it to
    // 80 x 52. Every bar is buried at both ends: a chain cap that lands ON a block face
    // is the coplanar-cap z-fight class.
    GCH(8, 1, 1, gy, {   // comb / upper spine, action -> butt block
      GN(0, 0.0125f, -0.0640f, 0.0104f, 0.0128f, chas),
      GN(0, 0.0150f, -0.1400f, 0.0112f, 0.0132f, chas),   // the thumbhole's rear edge
      GN(0, 0.0140f, -0.2120f, 0.0116f, 0.0130f, chas),
    });
    GCH(8, 1, 1, gy, {   // toe line / lower spine, grip heel -> web
      // ...and it exits the grip's REAR at mid-height, well above the palm heel:
      // started at the heel it ran surface-on-surface under the trigger hand's own
      // up-facing palm chord (vmcheck z-fight at the one spot both are horizontal).
      GN(0, -0.0560f, -0.0500f, 0.0072f, 0.0090f, chas),
      GN(0, -0.0480f, -0.1000f, 0.0074f, 0.0092f, chas),
      GN(0, -0.0400f, -0.1520f, 0.0086f, 0.0100f, chas),
    });
    GBOX(0, -0.0180f, -0.1760f, 0.0118f, 0.0230f, 0.0380f, chas);  // the web
    GBOX(0, -0.0060f, -0.2200f, 0.0128f, 0.0330f, 0.0175f, chas);  // butt block
    // The BUTT HOOK — the shooter's support hand goes here, and it is the one shape on
    // the weapon that says "fired from a bipod" rather than "fired standing".
    GBOX(0, -0.0400f, -0.2140f, 0.0098f, 0.0190f, 0.0098f, chas);
    if (gun_tier())
      GBOX(0, -0.0600f, -0.2140f, 0.0044f, 0.0056f, 0.0044f, black);  // monopod stud
    // The CHEEK PIECE, on a shaped riser rather than on two 6 mm posts — the posts were
    // the "plank on stilts" read, and a riser is what actually carries an adjustable comb.
    GCH(8, 1, 1, gy, {
      GN(0, 0.0300f, -0.1560f, 0.0072f, 0.0126f, chas),
      GN(0, 0.0316f, -0.1900f, 0.0078f, 0.0132f, chas),
      GN(0, 0.0306f, -0.2160f, 0.0074f, 0.0126f, chas),
    });
    if (g_gun_hero) {   // the locking wheel, on the side the camera can see
      mat_lathe(MAT_PARK_R, MAT_PARK_M);
      fnode_t cw[2] = {
        {GUNP(-0.0126f, 0.0270f, -0.2020f), black, black, 0.0052f*gg, 0.0052f*gg, 0, 0.0f},
        {GUNP(-0.0172f, 0.0270f, -0.2020f), black, black, 0.0048f*gg, 0.0048f*gg, 0, 0.0f}};
      fig_chain(cw, 2, 12, 1, 1, gy);
    }
    mat_set(MAT_POLY_R, 0.0f);
    if (gun_tier())
      GBOX(0, -0.0040f, -0.2338f, 0.0134f, 0.0296f, 0.0034f, chas);  // LOP spacer
    mat_set(MAT_POLY_R * 1.7f, 0.0f);
    // (top face 1.6 mm below the butt block's own — a shared top plane z-fights)
    GBOX(0, -0.0040f, -0.2415f, 0.0124f, 0.0294f, 0.0065f, black); // butt pad
    mat_set(MAT_POLY_R, 0.0f);
}

static void gun_sr_foreend(v3 org, v3 gx, v3 gy, v3 gz, float gg,
                           v3 black, v3 chas) {
    mat_set(MAT_POLY_R, 0.0f);  // state carried in from gun_sr_stock's last call (task-10 split)
    // ---- fore-end -----------------------------------------------------------
    // FLAT-SIDED (k=8), NOT A ROUND TUBE. A k=12 ellipse is a hair-dryer; a chassis
    // fore-end is a machined box with an under-rail, slotted flanks and a barrel channel
    // you can see daylight through. rx is the vertical half-extent here, rz the
    // horizontal — the chain runs along z with a gy hint.
    GCH(8, 1, 1, gy, {
      GN(0, 0.0116f, 0.1360f, 0.0176f, 0.0170f, chas),  // shoulder, into the chassis
      GN(0, 0.0124f, 0.1620f, 0.0166f, 0.0158f, chas),  // <- GH_FORE3 station
      GN(0, 0.0132f, 0.2080f, 0.0156f, 0.0150f, chas),  // <- GH_FORE station
      GN(0, 0.0142f, 0.2640f, 0.0158f, 0.0162f, chas),
      GN(0, 0.0152f, 0.3200f, 0.0162f, 0.0176f, chas),  // the bipod bay, widest
      GN(0, 0.0160f, 0.3600f, 0.0166f, 0.0150f, chas),  // nose, concentric with the bore
    });
    // The under-rail, full length, in the same three-rung width ladder as the AR's.
    GBOX(0, -0.0034f, 0.2560f, 0.0089f, 0.0032f, 0.0900f, chas);
    GBOX(0, -0.0086f, 0.2560f, 0.0057f, 0.0030f, 0.0880f,
         v3_scale(chas, 0.84f));
    if (g_gun_hero)
      for (int r = 0; r < 11; r++)
        GBOX(0, -0.0108f, 0.1780f + 0.0150f * (float)r,
             0.0073f, 0.0026f, 0.0034f, chas);
    // M-LOK flanks: a rhythm of dark lands, the same construction the AR's guard uses.
    for (int m = 0; gun_tier() && m < 5; m++) {
      float mz = 0.1980f + 0.0270f * (float)m;
      GBOX( 0.0162f, 0.0138f, mz, 0.0028f, 0.0044f, 0.0094f, black);
      GBOX(-0.0162f, 0.0138f, mz, 0.0028f, 0.0044f, 0.0094f, black);
    }
}

static void gun_sr_barrel(v3 org, v3 gx, v3 gy, v3 gz, float gg,
                          v3 upper, v3 steel, v3 chas) {
    // ---- barrel -------------------------------------------------------------
    // A HEAVY MATCH BARREL READS BY *NOT* TAPERING. The AR's visibly does (8.6 -> 5.5 mm
    // of radius over 128 mm); this one holds 22 mm across 340 mm of exposed length, and
    // that constancy plus the free-float gap at the nose is the whole "precision" cue.
    // The chamber swell lives only where the action covers it: a true 34 mm breech does
    // not fit inside a fore-end the hand contract can also close around.
    mat_lathe(MAT_PARK_R, MAT_PARK_M);
    GCH(16, 1, 1, gy, {
      GN(0, 0.0160f, 0.1000f, 0.0126f, 0.0126f, upper),  // tenon, inside the action
      GN(0, 0.0160f, 0.1440f, 0.0120f, 0.0120f, upper),  // breech face
      GN(0, 0.0160f, 0.1560f, 0.0112f, 0.0112f, upper),  // chamber step
      GN(0, 0.0160f, 0.4000f, 0.0110f, 0.0110f, upper),
      GN(0, 0.0160f, 0.6400f, 0.0107f, 0.0107f, upper),
      GN(0, 0.0160f, 0.6880f, 0.0106f, 0.0106f, upper),
      GN(0, 0.0160f, 0.7040f, 0.0090f, 0.0090f, steel),  // the bare thread shank
    });
    // Free-float counterbore: 2.4 mm of daylight around the barrel at the nose, which is
    // the cheapest one-primitive statement of "this barrel touches nothing".
    if (gun_tier())
      fig_tube(GUNP(0, 0.0160f, 0.3550f), GUNP(0, 0.0160f, 0.3665f),
               0.0172f * gg, 0.0158f * gg, 0.0134f * gg, 0.0134f * gg, 12, chas, gy);
}

static void gun_sr_bipod(v3 org, v3 gx, v3 gy, v3 gz, float gg, v3 black) {
    // ---- bipod: a mount AND legs. Only the stud existed, and at 25 m the bipod is the
    // third strongest thing in this weapon's silhouette.
    mat_lathe(MAT_PARK_R, MAT_PARK_M);
    GBOX(0, -0.0060f, 0.3460f, 0.0076f, 0.0096f, 0.0130f, black);  // stud
    GBOX(0, -0.0140f, 0.3460f, 0.0110f, 0.0044f, 0.0060f, black);  // yoke
    for (int lg = 0; lg < 2; lg++) {
      float u = lg ? -1.0f : 1.0f;
      GCH(8, 1, 1, gy, {   // folded back along the fore-end, as a bipod is carried
        GN(u * 0.0100f, -0.0150f, 0.3400f, 0.0038f, 0.0040f, black),
        GN(u * 0.0132f, -0.0128f, 0.2400f, 0.0030f, 0.0032f, black),
        GN(u * 0.0148f, -0.0112f, 0.1900f, 0.0026f, 0.0028f, black),
      });
      if (gun_tier())
        GBOX(u * 0.0150f, -0.0106f, 0.1830f, 0.0034f, 0.0038f, 0.0044f, black);  // foot
    }
}

static void gun_sr_brake(v3 org, v3 gx, v3 gy, v3 gz, float gg,
                         v3 upper, v3 steel, v3 black) {
    mat_lathe(MAT_PARK_R, MAT_PARK_M);  // state carried in from gun_sr_bipod's last call (task-10 split)
    // ---- muzzle brake -------------------------------------------------------
    GCH(12, 1, 1, gy, {
      GN(0, 0.0160f, 0.6960f, 0.0112f, 0.0110f, upper),  // collar over the thread
      GN(0, 0.0160f, 0.7120f, 0.0128f, 0.0126f, upper),
      GN(0, 0.0160f, 0.7380f, 0.0112f, 0.0110f, black),  // groove: a port
      GN(0, 0.0160f, 0.7500f, 0.0128f, 0.0126f, upper),
      GN(0, 0.0160f, 0.7640f, 0.0112f, 0.0110f, black),  // groove: a port
      GN(0, 0.0160f, 0.7740f, 0.0124f, 0.0122f, upper),
      GN(0, 0.0160f, 0.7800f, 0.0098f, 0.0096f, steel),  // the crown's land
    });
    mat_set(MAT_POLY_R * 1.7f, 0.0f);   // the bore: no sheen down a barrel
    GCH(12, 1, 1, gy, {
      GN(0, 0.0160f, 0.7740f, 0.0046f, 0.0046f, black),
      GN(0, 0.0160f, 0.7812f, 0.0044f, 0.0044f, black),
    });
}

static void gun_sr_scope(v3 org, v3 gx, v3 gy, v3 gz, float gg,
                         v3 upper, v3 steel, v3 black, v3 glass, v3 lens,
                         float sight_y) {
    // ---- the scope, and it is the weapon's second silhouette ------------------
    // THE OBJECTIVE WAS SMALLER THAN THE OCULAR (18.8 mm against 20.5), which is
    // backwards on every telescope ever built — a scope's front bell is the whole reason
    // it is called an objective. It is 26.8 against 19.2 now, and the tube runs 366 mm
    // rather than 199, so the thing above the barrel reads as an optic instead of as a
    // pipe. ONE chain: eight of fig_chain's ten nodes, which is worth knowing before
    // anyone adds a ninth.
    mat_lathe(MAT_PARK_R * 0.8f, MAT_PARK_M);   // anodized alloy scope body
    GCH(20, 1, 1, gy, {
      GN(0, sight_y, SCOPE_OCU_Z, SCOPE_OCU_R, SCOPE_OCU_R, black),
      GN(0, sight_y, SCOPE_OCU_Z + 0.018f, 0.0197f, 0.0197f, black),
      GN(0, sight_y, SCOPE_OCU_Z + 0.036f, 0.0190f, 0.0190f, black),
      GN(0, sight_y, 0.0010f, SCOPE_TUBE_R, SCOPE_TUBE_R, upper),
      GN(0, sight_y, 0.1990f, SCOPE_TUBE_R + 0.0001f, SCOPE_TUBE_R + 0.0001f, upper),
      GN(0, sight_y, 0.2500f, 0.0268f, 0.0268f, black),
      GN(0, sight_y, 0.3060f, 0.0268f, 0.0268f, black),
      GN(0, sight_y, 0.3140f, 0.0250f, 0.0250f, black),
    });
    GBOX(0, sight_y, 0.0900f, 0.0234f, 0.0230f, 0.0300f, black);   // turret saddle
    if (gun_tier())
      GCH(12, 1, 1, gy, {   // magnification ring
        GN(0, sight_y, 0.0080f, 0.0158f, 0.0158f, black),
        GN(0, sight_y, 0.0380f, 0.0158f, 0.0158f, black),
      });
    if (g_gun_hero)   // the throw lever, on the side the camera can see
      GBOX(-0.0205f, sight_y - 0.0050f, 0.0230f, 0.0068f, 0.0040f, 0.0056f, black);
    GCH(16, 1, 1, gz, {   // ELEVATION turret, capped and knurled
      GN(0, sight_y + 0.0130f, 0.0900f, 0.0182f, 0.0182f, upper),
      GN(0, sight_y + 0.0270f, 0.0900f, 0.0179f, 0.0179f, upper),
      GN(0, sight_y + 0.0360f, 0.0900f, 0.0175f, 0.0175f, upper),
      GN(0, sight_y + 0.0382f, 0.0900f, 0.0190f, 0.0190f, black),
      GN(0, sight_y + 0.0409f, 0.0900f, 0.0172f, 0.0172f, black),
    });
    GCH(16, 1, 1, gy, {   // WINDAGE turret, right
      GN(0.0200f, sight_y, 0.0900f, 0.0162f, 0.0162f, upper),
      GN(0.0238f, sight_y, 0.0900f, 0.0158f, 0.0158f, upper),
      GN(0.0252f, sight_y, 0.0900f, 0.0168f, 0.0168f, black),
      GN(0.0270f, sight_y, 0.0900f, 0.0152f, 0.0152f, black),
    });
    if (gun_tier())
      GCH(16, 1, 1, gy, {   // PARALLAX turret — the third knob a tactical scope has
        GN(-0.0200f, sight_y, 0.0900f, 0.0170f, 0.0170f, upper),
        GN(-0.0246f, sight_y, 0.0900f, 0.0166f, 0.0166f, upper),
        GN(-0.0262f, sight_y, 0.0900f, 0.0150f, 0.0150f, black),
      });
    // Ocular glass, recessed inside its retaining ring.
    mat_lathe(MAT_STEEL_R, MAT_STEEL_M);
    fig_tube(GUNP(0, sight_y, SCOPE_OCU_Z - 0.0062f),
             GUNP(0, sight_y, SCOPE_OCU_Z + 0.0018f),
             0.0193f * gg, 0.0191f * gg, 0.0172f * gg, 0.0170f * gg, 20, steel, gy);
    mat_set(MAT_GLASS_R, 0.0f);
    GCH(20, 1, 1, gy, {
      GN(0, sight_y, SCOPE_OCU_Z - 0.0030f, 0.0168f, 0.0166f, glass),
      GN(0, sight_y, SCOPE_OCU_Z + 0.0024f, 0.0160f, 0.0158f, glass),
    });
    // ...and the objective, which is the one lens anyone else ever sees.
    mat_lathe(MAT_STEEL_R, MAT_STEEL_M);
    fig_tube(GUNP(0, sight_y, 0.3080f), GUNP(0, sight_y, 0.3160f),
             0.0256f * gg, 0.0254f * gg, 0.0212f * gg, 0.0210f * gg, 20, steel, gy);
    mat_set(MAT_GLASS_R, 0.0f);
    GCH(20, 1, 1, gy, {
      GN(0, sight_y, 0.3096f, 0.0202f, 0.0200f, lens),
      GN(0, sight_y, 0.3148f, 0.0194f, 0.0192f, lens),
    });
}

static void gun_sr_mount(v3 org, v3 gx, v3 gy, v3 gz, float gg,
                         v3 alloy, v3 steel, float sight_y) {
    // ---- the mount: ONE machined unit, not two wires --------------------------
    // Rail-top to tube-centre is 46 mm here, which is a night-vision-height mount; two
    // 45 mm posts under a scope tube read as stilts, and that is what they were. A bar,
    // two towers, two rings and two caps make the same height read as MASS.
    mat_set(MAT_ALLOY_R, MAT_ALLOY_M);
    GBOX(0, 0.0436f, 0.0780f, 0.0128f, 0.0064f, 0.0680f, alloy);  // rail-clamp bar
    GBOX(0, 0.0592f, 0.0320f, 0.0122f, 0.0110f, 0.0122f, alloy);  // rear tower
    GBOX(0, 0.0592f, 0.1360f, 0.0122f, 0.0110f, 0.0122f, alloy);  // front tower
    GCH(20, 1, 1, gy, {   // rear ring
      GN(0, sight_y, 0.0210f, 0.0186f, 0.0186f, alloy),
      GN(0, sight_y, 0.0430f, 0.0186f, 0.0186f, alloy),
    });
    GCH(20, 1, 1, gy, {   // front ring
      GN(0, sight_y, 0.1250f, 0.0186f, 0.0186f, alloy),
      GN(0, sight_y, 0.1470f, 0.0186f, 0.0186f, alloy),
    });
    GBOX(0, 0.1002f, 0.0320f, 0.0132f, 0.0048f, 0.0128f, alloy);  // rear ring cap
    GBOX(0, 0.1002f, 0.1360f, 0.0132f, 0.0048f, 0.0128f, alloy);  // front ring cap
    if (g_gun_hero) {   // the eight ring screws
      mat_lathe(MAT_STEEL_R, MAT_STEEL_M);
      for (int r = 0; r < 2; r++)
        for (int q = 0; q < 4; q++)
          GBOX((q & 1) ? 0.0092f : -0.0092f, 0.1046f,
               (r ? 0.1360f : 0.0320f) + ((q & 2) ? 0.0074f : -0.0074f),
               0.0024f, 0.0018f, 0.0024f, steel);
    }
}

static v3 gun_build(int cur, v3 org, v3 gx, v3 gy, v3 gz, float md01,
                    float trig01, float bolt01) {
  float sight_y = gun_sight_y(cur);
  v3 muzzle = org;
  // Shared palette: slim two-tone polymer/graphite with cool steel accents.
  // THE PALETTE IS A VALUE LADDER AND A MATERIAL LADDER AT THE SAME TIME, because at
  // 25 m hue is gone and value is not. Six steps, three material families:
  //   dielectric   black 0.115 -> body 0.180 -> mag 0.250 (-> chas 0.320 on the SR)
  //   METAL        alloy 0.400 -> park  0.470 -> steel 0.740
  // and the two metal coatings are told apart by ROUGHNESS as much as by value —
  // anodize is a smooth oxide, phosphate is a crystalline rough one — which is what
  // keeps a receiver and a gas block different objects when they are the same colour.
  v3 body  = {{0.180f, 0.192f, 0.206f}};  // glass-filled polymer (dielectric), COOL
  // ...AND FOR A METAL THE ALBEDO IS ITS SPECULAR REFLECTANCE, F0, not a paint colour.
  // TWO THIRDS OF THIS WEAPON WAS A METAL WITH NO REFLECTANCE. The top rail, the lower
  // receiver, the magwell, the ejection port, every control, the muzzle brake and the
  // gas block were all drawn under mat_set(MAT_PARK_M) — metalness 0.90 to 1.00 — while
  // carrying `body` (0.023 linear) or `black` (0.016 linear) as their albedo. A metal
  // has NO DIFFUSE at all, so those surfaces returned 1.6-2.3% of what struck them in
  // the one lobe they have and nothing anywhere else: not a dark metal, a light sink.
  // That, and not the geometry, is why the whole weapon read as one charcoal moulding.
  v3 alloy = {{0.400f, 0.404f, 0.412f}};  // hard-anodized 7075: upper, rail,
                                          // handguard, buffer tube. Deep and SMOOTH.
  // TWO ANODIZING RUNS NEVER MATCH, and that is the most famous cosmetic fact about a
  // real AR. It is also the fix for what the metal correction above COST: once the
  // receivers, the magwell, the handguard, the rail and the optic mount all became one
  // honest alloy, twenty-nine of the weapon's parts carried one albedo at one roughness
  // and the whole thing read as a single billet — the upper/lower line, which is the
  // strongest horizontal on a rifle and runs through the middle of the first-person
  // frame, disappeared. One palette entry, five call sites, ZERO vertices.
  v3 alloy2 = {{0.312f, 0.315f, 0.321f}};  // ...the lower's batch: darker, matter
  v3 upper = {{0.470f, 0.462f, 0.444f}};  // manganese phosphate over steel: barrel
                                          // furniture, gas block, brake. ROUGH, and a
                                          // touch warm — it is an etched crystal layer
  v3 steel = MAT_BARE_C;                  // bare steel: real iron's reflectance, and
                                          // the one copy of it (see MAT_BARE_C)
  v3 black = {{0.115f, 0.114f, 0.117f}};  // rubber / dead-black polymer — nothing real
                                          // sits at 0.005 linear
  // THE MAGAZINE IS POLYMER AND IT WAS BORROWING THE RECEIVER'S ALBEDO. It gets its own
  // tone, keeping the thing the borrowed one was for: ONE VALUE STEP lighter than the
  // magwell it hangs out of, so a reload has a silhouette.
  v3 mag   = {{0.250f, 0.264f, 0.282f}};  // magazine body: polymer, one step up
  v3 chas  = {{0.320f, 0.352f, 0.286f}};  // SR chassis polymer: the AWM's olive-drab —
                                          // the one colour that says "precision rifle"
  v3 glass = {{0.045f, 0.055f, 0.070f}};  // coated lens: dark pupil, bright rim
  v3 lens  = {{0.086f, 0.112f, 0.118f}};  // multi-coated objective: blue-green
  float gg = g_gun_girth;                 // cross-section chunk (1.0 in first person)
  g_gun_trig_s = trig01;                  // published once, at the top, so a
                                          // third weapon cannot forget it
  ao_off();                               // a gun at arm's length sees open sky
  mat_lathe(MAT_PARK_R, MAT_PARK_M);

  if (cur == WPN_AR) {
    gun_ar_receiver(org, gx, gy, gz, gg, alloy, alloy2, upper, steel, black, bolt01);
    gun_ar_stock(org, gx, gy, gz, gg, body, alloy, black);
    gun_ar_gripmag(org, gx, gy, gz, gg, body, alloy2, steel, black, mag, md01, trig01);
    gun_ar_barrel(org, gx, gy, gz, gg, alloy, upper, steel, black);
    gun_ar_sight(cur, org, gx, gy, gz, gg, alloy, steel, black, lens, sight_y);
  } else {
    gun_sr_action(org, gx, gy, gz, gg, alloy, upper, black, chas);
    gun_sr_magwell(org, gx, gy, gz, gg, body, upper, steel, black, mag, chas, md01, trig01, bolt01);
    gun_sr_stock(org, gx, gy, gz, gg, black, chas);
    gun_sr_foreend(org, gx, gy, gz, gg, black, chas);
    gun_sr_barrel(org, gx, gy, gz, gg, upper, steel, chas);
    gun_sr_bipod(org, gx, gy, gz, gg, black);
    gun_sr_brake(org, gx, gy, gz, gg, upper, steel, black);
    gun_sr_scope(org, gx, gy, gz, gg, upper, steel, black, glass, lens, sight_y);
    gun_sr_mount(org, gx, gy, gz, gg, alloy, steel, sight_y);
  }
  muzzle = GUNP(GUN_MUZ[cur][0], GUN_MUZ[cur][1], GUN_MUZ[cur][2]);
  return muzzle;
}

#undef GN
#undef GCH
#undef GCH_PUB
#undef GBOX
#undef GUNP
