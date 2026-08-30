// =============================================================================
// ui/home.inc  —  HOME direction: HOME_SHOT/HOME_BEAT, home_stage,
// home_uncover.
// Proof: home on; appframe N; uistat. One TU: included once by game.c, in
// definition order — moving a definition out is a semantic change.
// Contract: the director is a pure function of one clock (g_ui_time -
// g_home_t0) — appframe is the command that advances g_ui_time, wait does
// not. Only the player is immune to ent_damage; bots fight and kill each
// other.
// =============================================================================
// ---------------------------------------------------------------------------
// HOME — the entry screen, and the only place this game gets to sell itself.
// It is a DIRECTOR, not a menu with a picture behind it.
//
// It is reached at boot AND on the way back from an online match (leave_match
// re-enters it), so "the boot screen" is where it is first seen, not the only
// place: home_enter re-stages the player and restarts the reel's clock either
// way, and nothing here may assume it has run exactly once.
// ---------------------------------------------------------------------------

// A shot is four numbers eased from A to B, and which four depends on the lens.
typedef struct {
  float len;         // seconds
  int   cu;          // 0 = the soldier, in the world; 1 = the weapon, close;
                     // 2 = a BOT — the camera rides it, see home_camera
  float a[4], b[4];  // hero: camera right / forward / up, and the look height,
                     //       all in metres in the SOLDIER'S own frame
                     // cu:   orbit yaw, orbit pitch (deg), radius (m), and the
                     //       station along the bore the lens is pointed at
  float fov;         // cu only: a real lens, in degrees
  float rate;        // world speed under this shot (0 = 1.0). Slow motion is
                     // an ANIMATION effect, so it scales the sim, not the lens
  float rise;        // cu only: how far the pivot sits ABOVE the bore axis —
                     // an optic is 9 cm up and a shot aimed at the barrel under
                     // it photographs the barrel
  int   calm;        // 1 = the bots hold still under this shot
  const char *cap;   // what the camera is looking at, for the caption card
} home_shot_t;

// HOME_NSHOT and the `len` column below are the authority on the reel's length (~51 s
// as it stands); the cut list is the argument: establish the man, then alternate macro
// passes on the thing he is holding with world shots, with the two beats that make it
// move (a shoulder, a swap) landing on WIDE shots where a whole body reads.
#define HOME_NSHOT 10
// ...and ONE MORE past the playlist, reachable only by substitution — see home_slot.
#define HOME_SUB_SLIDE HOME_NSHOT
// DESIGNATED for everything after the two point arrays. `fov`, `rate`, `rise` and
// `calm` are four adjacent scalars of which two are floats in overlapping ranges, and a
// swapped rate/rise pair once put the bore heights (0.010-0.090) into `rate` — three
// close-ups plus the sniper substitute crawling at 2-9% world speed, i.e. the one thing
// this screen may not be. A designator cannot be swapped, and a zero can simply be left
// out. The rule for `calm` is still exactly one line: THE BOTS HOLD UNDER A CLOSE-UP
// AND NOWHERE ELSE (calm == (cu == 1)).
static const home_shot_t HOME_SHOT[HOME_NSHOT + 1] = {
  { 5.5f, 0, { 1.42f, 2.05f, 0.52f, 1.18f}, { 1.02f, 1.58f, 0.34f, 1.24f}, .calm = 0},
  // 1 — the track down the bore on a long lens.
  { 5.0f, 1, {  96.f, 14.0f, 0.185f, 0.600f}, { 100.f, 18.0f, 0.195f, 0.300f},
    .fov = 26.0f, .rise = 0.010f, .calm = 1, .cap = "MUZZLE BRAKE"},
  // 2 — THE FIREFIGHT, at full speed, and SHORT. The camera leaves the player entirely
  // and rides a bot.
  { 3.4f, 2, { 2.60f, 1.45f, 2.40f, 1.10f}, { 1.70f, 1.15f, 3.10f, 1.05f}, .calm = 0},
  // 3 — THE SLIDE, in slow motion: he runs, drops into a slide along the wall, dust off
  // the boots and the fight cracking past behind him.
  { 6.5f, 0, { 2.20f, 1.50f, 0.90f, 1.00f}, { 1.85f, 0.05f, 0.40f, 0.42f}, .rate = 0.40f},
  // 4 — orbit on the strong side while the rifle comes to the shoulder.
  { 5.5f, 0, { 1.72f, 0.86f, 1.34f, 1.30f}, { 0.82f, 1.90f, 0.86f, 1.16f}, .calm = 0},
  // 5 — the receiver: the brushed flat, the port, the broken edges. Radii out from
  // 0.345/0.310 to 0.470/0.425.
  { 4.5f, 1, {  78.f, 38.0f, 0.520f, 0.020f}, {  84.f, 34.0f, 0.515f, 0.200f},
    .fov = 26.0f, .rise = 0.028f, .calm = 1, .cap = "UPPER RECEIVER"},
  // 6 — out of cover and straight down the lens. Full speed while the lean reads as a
  // movement, then eased into a light 0.45x for the automatic fire.
  { 6.5f, 0, { 0.55f, 3.05f, 1.05f, 1.42f}, { 0.20f, 2.30f, 1.28f, 1.46f}, .rate = 0.45f},
  // 7 — LOW AND TRAVELLING FORWARD along the handguard, on a wide lens and close in, so
  // the rail runs out of frame past the camera rather than sitting in it.
  { 4.5f, 1, { 124.f, -18.0f, 0.215f, 0.260f}, { 118.f, -12.0f, 0.220f, 0.520f},
    .fov = 30.0f, .rise = -0.012f, .calm = 1, .cap = "HANDGUARD"},
  // 8 — the optic, while the bolt gun comes to the eye.
  { 5.0f, 1, { 104.f, 22.0f, 0.450f, -0.020f}, {  98.f, 18.0f, 0.455f, 0.150f},
    .fov = 22.0f, .rise = 0.090f, .calm = 1, .cap = "OPTIC"},
  // 9 — down onto the ejection port from high on the strong side, falling to a rake.
  { 4.5f, 1, {  62.f, 44.0f, 0.500f, 0.010f}, {  68.f, 40.0f, 0.505f, 0.150f},
    .fov = 28.0f, .rise = 0.038f, .calm = 1, .cap = "EJECTION PORT"},
  // 10 — THE SUBSTITUTE, and it exists to delete one specific bad frame. The
  //     slide is a one-handed carry with the muzzle rotated 66 degrees up, which
  //     is right for a carbine and absurd for a bolt gun: a 700 mm heavy barrel
  //     with a scope on it, stood on end beside a crouching man, is the one
  //     composition in the reel that got laughed at. So sniper loops do not
  //     play the slide at all — they play a long travel back down that barrel
  //     instead, which is the thing the sniper has and the carbine does not.
  //     Same length as shot 3 by construction, so the loop's total is unchanged
  //     and home_loop can still be computed before the playlist.
  //     Opening station 0.500, not 0.640: at 0.640 the lens was aimed 130 mm
  //     PAST the brake and the shot opened on two seconds of empty sky before
  //     the barrel drifted into frame. A travel has to start ON the thing it
  //     is travelling along.
  { 6.5f, 1, {  88.f, 10.0f, 0.200f, 0.540f}, {  94.f, 15.0f, 0.210f, 0.180f},
    .fov = 26.0f, .rise = 0.020f, .calm = 1, .cap = "HEAVY BARREL"},
};

// The director's finger: (shot, action, start, hold) in shot-local seconds.
typedef struct { int shot, act; float t0, hold; } home_beat_t;
static const home_beat_t HOME_BEAT[] = {
  // THE MAGAZINE CHANGE IS NOT A SCENE. It has to happen — the firing beats below spend
  // rounds and there is no reserve pool — but it belongs where the camera cannot see
  // it.
  {0, IN_RIGHT,  2.30f, 1.70f},
  {4, IN_CROUCH, 3.30f, 2.20f},
  {1, IN_RELOAD, 0.10f, 0.20f},
  {1, IN_FIRE,   3.20f, 0.16f},
  {1, IN_FIRE,   4.40f, 0.13f},
  // THE SLIDE. A slide needs RUN SPEED before the crouch edge can start it (mv_slide's
  // own entry gate), so the run comes first and the crouch lands on top of it; crouch
  // is then held, because releasing it is one of the three things that ends a slide.
  {3, IN_FWD,    0.15f, 1.05f},
  {3, IN_CROUCH, 0.95f, 2.60f},
  {4, IN_ADS,    1.20f, 4.00f},   // up early, held through the orbit's middle
  // Shot 6, and it is a SNAP: out and shooting.
  {6, IN_LEAN_R, 0.20f, 6.60f},
  {6, IN_ADS,    0.30f, 6.40f},
  {6, IN_FIRE,   0.95f, 1.60f},
  {6, IN_FIRE,   3.10f, 1.40f},
  {6, IN_FIRE,   5.00f, 1.20f},
  // (The weapon swap is NOT in this table — see home_input. It is one press per
  //  LOOP, on the establish shot, and which weapon it picks is the loop's own.)
};// How fast the WORLD runs under the current shot. Slow motion is not a camera
// trick — the thing being slowed is the animation, so it belongs on the sim's
// accumulator and nowhere else.
static float home_timescale(void);

static float g_home_t0;    // g_ui_time when the screen opened
static int   g_home_orb;   // HOME, not the harness, owns the turntable

// The left third of the screen is the tile rail, so the subject does not belong
// in the middle of the frame — it belongs in the middle of what is LEFT. This
// is a look-direction offset and not a camera move: the viewpoint, and
// therefore every highlight on the weapon, is exactly where the shot put it.
#define HOME_COMPOSE 0.235f

// Where the boot screen stands the player.
static float home_rnd(unsigned loop, unsigned salt);
static unsigned home_loop(void);

static void home_stage(player_t *p) {
  if (!p) return;
  // SUN_DIR points TO the light; the yaw convention is fwd = (sin y, 0, -cos y).
  float syaw = atan2f(g_sun.dir.x, -g_sun.dir.z) - 0.55f;
  p->yaw = syaw;
  // ...AND IT STANDS HIM AT A CORNER, ON THE SUN SIDE OF IT. One shot leans out of
  // cover and fires down the lens, and a man leaning in open ground is a man with a
  // limp — the cover has to be THERE. The arena is procedural, so the corner is found
  // rather than authored: the nearest vertical edge of a solid tall enough to hide
  // behind, at a distance the camera can still frame him from. `pen == 0` is the arena
  // SHELL (the edge of the world, not cover).
  v3 sunh = v3_norm((v3){{g_sun.dir.x, 0.0f, g_sun.dir.z}});
  float best = -1e9f;
  v3 stand = p->pos;
  float syaw2 = syaw;
  // TWO PASSES. Run-up used to be scored rather than required, on the reasoning that a
  // procedural arena owes nobody a corner that is both sun-facing and has room in front
  // of it — and then the per-loop random tiebreak started winning with corners that had
  // none, and the slide shot became a man crouching against a wall with his rifle up.
  for (int pass = 0; pass < 2 && best <= -1e8f; pass++) {
  float need_run = pass ? 0.0f : 7.0f;
  for (int i = 0; i < g_num_solids; i++) {
    const solid_t *sd = &g_solids[i];
    if (!sd->pen) continue;
    if (sd->max.y - sd->min.y < 1.30f) continue;
    float ex = sd->max.x - sd->min.x, ez = sd->max.z - sd->min.z;
    int along_x = ex > ez;                       // the wall's long axis
    for (int c = 0; c < 4; c++) {
      v3 cor = {{(c & 1) ? sd->max.x : sd->min.x, 0.0f,
                 (c & 2) ? sd->max.z : sd->min.z}};
      float dx = cor.x - p->pos.x, dz = cor.z - p->pos.z;
      float d = sqrtf(dx * dx + dz * dz);
      if (d < 2.0f || d > 16.0f) continue;
      // Stand BESIDE the wall's long face and a little back from its end, so
      // the corner is forward and to the strong side: leaning right is then
      // what brings the shoulder past it.
      float sgn_l = along_x ? ((c & 1) ? -1.0f : 1.0f) : ((c & 2) ? -1.0f : 1.0f);
      float sgn_o = along_x ? ((c & 2) ? 1.0f : -1.0f) : ((c & 1) ? 1.0f : -1.0f);
      v3 back = along_x ? (v3){{sgn_l, 0, 0}} : (v3){{0, 0, sgn_l}};
      v3 out  = along_x ? (v3){{0, 0, sgn_o}} : (v3){{sgn_o, 0, 0}};
      // THE WALL GOES ON HIS LEFT, ALWAYS. Every world shot frames him from the
      // open side and one leans him past the corner; if the cover can land
      // on either flank then half the seeds put a camera inside it, and the
      // collision pull-in then delivers a close-up of a shoulder instead of the
      // shot that was framed. `back` points from the corner to where he stands,
      // so he faces -back, and his right is (back.z, 0, -back.x).
      v3 rgt_ = {{back.z, 0.0f, -back.x}};
      if (v3_dot(rgt_, out) < 0.0f) out = v3_scale(out, -1.0f);
      // ...AND THERE HAS TO BE RUN-UP. One shot sprints and slides along this wall, and
      // a slide needs run speed before its entry gate will fire — measured, he reached
      // 7 m/s and hit the arena shell at 10 m with the crouch edge landing on a
      // velocity of zero, so the slide never happened and the shot was a man crouching
      // against a wall.
      v3 fwd_ = v3_scale(back, -1.0f);
      float run = 1e9f;
      for (int q = 0; q < g_num_solids; q++) {
        v3 nq;
        float t = ray_aabb(v3_add(cor, (v3){{0, 1.0f, 0}}), fwd_,
                           g_solids[q].min, g_solids[q].max, &nq);
        if (t > 0.0f && t < run) run = t;
      }
      // GATED AS WELL AS SCORED — the required run-up on pass 0, free on the fallback pass
      // (see TWO PASSES above).
      if (run < need_run) continue;
      if (run > 14.0f) run = 14.0f;
      // A RANDOM TIEBREAK, per loop.
      float score = v3_dot(out, sunh) * 3.0f + run * 0.22f - d * 0.06f
                  + home_rnd(home_loop(), (unsigned)(i * 4 + c) + 4096u) * 2.4f;
      if (score <= best) continue;
      v3 q = v3_add(cor, v3_add(v3_scale(back, 0.70f),
                                v3_scale(out, PLAYER_RADIUS + 0.30f)));
      q.y = p->pos.y;
      if (!aabb_fits(q, PLAYER_RADIUS, HEIGHT_STAND)) continue;
      best = score;
      stand = q;
      syaw2 = atan2f(-back.x, back.z);
    }
  }
  }
  if (best > -1e8f) {
    p->pos = p->prev_pos = stand;
    p->vel = (v3){{0, 0, 0}};
    p->yaw = syaw2;
  }
  // AND THE ANIMATION ROOT GOES WITH HIM. This is a teleport, and every joint in pose_t
  // is in WORLD space — solved from the anim's own root, not rebuilt from the position
  // passed to scene_draw_figure.
  {
    int h = (int)(p - g_players);
    if (h >= 0 && h < MAX_HUMANS)
      anim_reset(&g_player_anim[h], p->pos, p->yaw, p->wp.cur);
  }
}

static void home_enter(player_t *p) {
  g_home = 1;
  g_home_t0 = g_ui_time;
  home_stage(p);
}

// EVERY PASS IS A DIFFERENT REEL. A loop that plays the same shots in the same
// order with the same framing is a video file, and a player who leaves the menu open
// for two minutes has watched it twice.
static float home_rnd(unsigned loop, unsigned salt) {
  unsigned h = (loop * 2654435761u) ^ ((salt + 0x9e3779b9u) * 2246822519u);
  h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
  return (float)(h & 0xffffffu) * (1.0f / 16777216.0f);
}

// The playlist.
static int home_sniper_loop(unsigned loop) {
  return home_rnd(loop, 909u) > 0.55f;
}

static int home_slot(unsigned loop, int slot) {
  // Two piles, shuffled separately and then DEALT ALTERNATELY, rather than one shuffle
  // of everything.
  int world[HOME_NSHOT], macro[HOME_NSHOT], nw = 0, nm = 0;
  for (int i = 1; i < HOME_NSHOT; i++) {
    // The substitution FIRST, because it changes which pile the shot is in: on a sniper
    // loop the slide becomes a close-up of a heavy barrel.
    int si = (i == 3 && home_sniper_loop(loop)) ? HOME_SUB_SLIDE : i;
    if (HOME_SHOT[si].cu == 1) macro[nm++] = si;
    else                       world[nw++] = si;
  }
  for (int i = nm - 1; i > 0; i--) {
    int j = (int)(home_rnd(loop, (unsigned)i + 16u) * (float)(i + 1));
    if (j > i) j = i;
    int t = macro[i]; macro[i] = macro[j]; macro[j] = t;
  }
  for (int i = nw - 1; i > 0; i--) {
    int j = (int)(home_rnd(loop, (unsigned)i + 48u) * (float)(i + 1));
    if (j > i) j = i;
    int t = world[i]; world[i] = world[j]; world[j] = t;
  }
  int ord[HOME_NSHOT], n = 0, wi = 0, mi = 0, last = 0, run = 0;
  ord[n++] = 0;                       // the establish, a world shot
  while (wi < nw || mi < nm) {
    // Take from the pile that is FURTHEST BEHIND, so a 6-and-3 split spreads as MMW MMW
    // MMW rather than dealing the surplus at the end; break a tie away from whatever
    // was just dealt; and never deal three of a kind in a row whatever the counts say.
    int rw = nw - wi, rm = nm - mi;
    int take_m = rm > rw ? 1 : rm < rw ? 0 : (last != 1);
    if (run >= 2 && take_m == last) take_m = !take_m;
    if (take_m && mi >= nm) take_m = 0;
    if (!take_m && wi >= nw) take_m = 1;
    ord[n++] = take_m ? macro[mi++] : world[wi++];
    run = (take_m == last) ? run + 1 : 1;
    last = take_m;
  }
  return ord[slot < 0 ? 0 : slot >= HOME_NSHOT ? HOME_NSHOT - 1 : slot];
}

static unsigned home_loop(void) {
  float total = 0;
  for (int i = 0; i < HOME_NSHOT; i++) total += HOME_SHOT[i].len;
  float t = fmaxf(g_ui_time - g_home_t0, 0.0f);
  // BOUNDED BEFORE THE CAST: a float outside unsigned's range is UB, and `home T` takes
  // T straight from a script.
  float q = t / total;
  return q >= 4.0e9f ? 0u : (unsigned)q;
}

static int home_where(float *u) {
  float total = 0;
  for (int i = 0; i < HOME_NSHOT; i++) total += HOME_SHOT[i].len;
  unsigned loop = home_loop();
  float t = fmodf(fmaxf(g_ui_time - g_home_t0, 0.0f), total);
  for (int i = 0; i < HOME_NSHOT; i++) {
    int si = home_slot(loop, i);
    if (t < HOME_SHOT[si].len) { *u = t / HOME_SHOT[si].len; return si; }
    t -= HOME_SHOT[si].len;
  }
  *u = 0.0f;
  return 0;
}

// Do the bots hold under the shot on screen? Read from inside the sim tick,
// which is a presentation clock reaching into it — deliberate, and bounded the
// same way `g_bots_frozen` is: it can only ever be true while the boot screen
// is up, and the boot screen is unreachable from the proof battery.
static int home_calm(void) {
  if (!g_home) return 0;
  float u;
  return HOME_SHOT[home_where(&u)].calm;
}

// How fast the world runs under the current shot; see the forward declaration.
static float home_timescale(void) {
  if (!g_home) return 1.0f;
  float u;
  float r = HOME_SHOT[home_where(&u)].rate;
  if (r <= 0.01f) return 1.0f;
  // THE RAMP IS THE POINT. A shot that opens in slow motion has nothing to slow DOWN
  // from, and the movement this one opens with — a man leaning out of cover — reads as
  // a stumble at half speed.
  float in_ = smooth01(f_clamp((u - 0.09f) / 0.09f, 0.0f, 1.0f));
  float out = smooth01(f_clamp((0.97f - u) / 0.10f, 0.0f, 1.0f));
  float k = in_ * out;
  return 1.0f + (r - 1.0f) * k;
}

// Read by frame_input_keys, which is where every other key is read.
static int home_input(int act) {
  if (!g_home) return 0;
  float u;
  int sh = home_where(&u);
  float t = u * HOME_SHOT[sh].len;
  // ONE WEAPON PER LOOP, AND THE SWAP NEVER HAPPENS UNDER A LENS. A weapon
  // materialising in a man's hands is the one thing on this screen that cannot read as
  // anything but a glitch, and the dip only hides it if the shot on either side is not
  // a MACRO — 0.3 m from a receiver, a rifle becoming a different rifle is the whole
  // frame changing.
  if (sh == 0 && t >= 0.01f && t < 0.16f) {
    int sr = home_sniper_loop(home_loop());
    if (act == (sr ? IN_SLOT2 : IN_SLOT1)) return 1;
  }
  for (unsigned i = 0; i < sizeof HOME_BEAT / sizeof *HOME_BEAT; i++)
    if (HOME_BEAT[i].shot == sh && HOME_BEAT[i].act == act &&
        t >= HOME_BEAT[i].t0 && t < HOME_BEAT[i].t0 + HOME_BEAT[i].hold)
      return 1;
  return 0;
}

// What the camera is looking at, for the card the layout draws. The fade is the shot's
// own envelope, so a cut never lands a hard label on a frame that has already moved on.
static float home_dip(void) {
  float u;
  const home_shot_t *sh = &HOME_SHOT[home_where(&u)];
  // 0.30, not 0.20: the cut has to cover a WEAPON SWAP, whose raise animation is 0.30 s
  // (GUN_SWITCH_TICKS), and a dip shorter than the thing it is hiding hides nothing.
  float t = u * sh->len, in = t / 0.30f, out = (sh->len - t) / 0.30f;
  float e = in < out ? in : out;
  return e >= 1.0f ? 0.0f : 1.0f - smooth01(f_clamp(e, 0.0f, 1.0f));
}

static const char *home_caption(float *fade) {
  float u;
  const home_shot_t *sh = &HOME_SHOT[home_where(&u)];
  if (!sh->cap) return NULL;
  *fade = smooth01(f_clamp(u * 6.0f, 0.0f, 1.0f)) *
          smooth01(f_clamp((1.0f - u) * 6.0f, 0.0f, 1.0f));
  return sh->cap;
}

// A CAMERA MAY NOT BE INSIDE A WALL, and the boot screen's is now standing next to one
// on purpose.
static v3 home_uncover(v3 look, v3 pos) {
  v3 d = v3_sub(pos, look);
  float dl = v3_len(d);
  if (dl < 1e-3f) return pos;
  v3 dir = v3_scale(d, 1.0f / dl);
  // FIVE CANDIDATE DIRECTIONS, not one.
  static const float TRY[5] = {0.0f, 0.62f, -0.62f, 1.25f, -1.25f};
  v3 up = {{0, 1, 0}};
  v3 bestp = pos;
  float bestr = -1.0f;
  for (int c = 0; c < 5; c++) {
    v3 dc = c ? v3_norm(v3_rot_axis(dir, up, TRY[c])) : dir;
    float lim = dl;
    for (int i = 0; i < g_num_solids; i++) {
      v3 n;
      float t = ray_aabb(look, dc, g_solids[i].min, g_solids[i].max, &n);
      if (t > 0.0f && t - 0.28f < lim) lim = t - 0.28f;
    }
    if (lim > bestr) { bestr = lim; bestp = v3_add(look, v3_scale(dc, lim)); }
    if (lim >= dl - 0.01f) break;
  }
  // ...and if all five are boxed in, go UP. An arena of open-topped solids
  // always leaves that one, and a high three-quarter of a man in a corner is a
  // shot; 0.45 m of wall is not.
  if (bestr < 0.60f) {
    v3 hi = v3_add(look, v3_add(v3_scale(dir, dl * 0.45f),
                                v3_scale(up, dl * 0.80f + 0.9f)));
    return hi;
  }
  return bestp;
}

// The camera. Returns 1 when HOME owns it; `taken` says the harness already
// does, in which case the director still has to let go of the turntable.
static int home_camera(cam_override_t *ov, const player_t *p, float alpha,
                       int taken) {
  if (!g_home || taken) {
    if (g_home_orb) { g_vmorb.on = 0; g_home_orb = 0; }
    return 0;
  }
  float u;
  int si = home_where(&u);
  const home_shot_t *sh = &HOME_SHOT[si];
  // RE-SET THE STAGE AT EVERY CUT, not only at the loop's wrap. The slide shot
  // leaves him eight metres from where he started, and every shot after it in
  // that pass was then framed against whatever geometry he happened to end up
  // in — measured, one of them put the camera inside his own chest, because
  // home_uncover found a wall between the subject and where the shot wanted to
  // be and pulled in to its floor. Every shot BEGINS under the dip (it is at its
  // peak on the boundary), so a placement there is invisible, and the corner it
  // picks is re-rolled per loop rather than per shot — a reel that teleports
  // between takes is one thing, a reel that teleports to a different arena
  // corner mid-take is another.
  {
    static int last_shot = -1;
    if (si != last_shot && last_shot >= 0) home_stage(local_player());
    last_shot = si;
  }
  float e = smooth01(u), k[4];
  // ...AND THE FRAMING IS JITTERED PER LOOP. Eight percent either way on every
  // number the shot is built from: enough that the same shot never composes
  // twice, small enough that a shot tuned to clear a wall still clears it.
  unsigned lp = home_loop();
  for (int i = 0; i < 4; i++) {
    float j = 0.92f + 0.16f * home_rnd(lp, (unsigned)(si * 8 + i) + 64u);
    k[i] = (sh->a[i] + (sh->b[i] - sh->a[i]) * e) * j;
  }
  // A hand-held float on two incommensurate periods, so nothing in the frame is ever
  // perfectly still and the drift never repeats inside one shot.
  float wob  = sinf(g_ui_time * 0.71f) * 0.013f + sinf(g_ui_time * 1.33f) * 0.007f;
  float wob2 = sinf(g_ui_time * 0.53f) * 0.010f + sinf(g_ui_time * 1.07f) * 0.005f;
  // ...AND THE WHOLE SHOT SWINGS ROUND THE SUBJECT, per loop.
  float az = (home_rnd(lp, (unsigned)si + 321u) - 0.5f) * 0.80f;
  float caz = cosf(az), saz = sinf(az);
  float kr = k[0] * caz - k[1] * saz, kf = k[0] * saz + k[1] * caz;

  v3 feet = v3_lerp(p->prev_pos, p->pos, alpha);
  float yw = p->yaw;
  v3 fwd = {{sinf(yw), 0, -cosf(yw)}};
  v3 rgt = {{cosf(yw), 0, sinf(yw)}};

  ov->active = 1; ov->bot = 0; ov->orbit = 0;
  ov->boff = (v3){{0, 0, 0}}; ov->blook = 0;

  if (sh->cu == 2) {
    // THE CAMERA RIDES A BOT — a subject that is moving and shooting rather than
    // standing still, which is the whole reason this shot is worth its seconds.
    if (g_home_orb) { g_vmorb.on = 0; g_home_orb = 0; }
    int nb = sp_bot_count();
    int bi = (int)(home_rnd(lp, 777u) * (float)nb);
    if (bi < 0) bi = 0;
    if (bi >= nb) bi = nb - 1;
    if (!g_bots[bi].alive) bi = 0;
    v3 bp = v3_lerp(g_bots[bi].prev_pos, g_bots[bi].pos, alpha);
    v3 blook = v3_add(bp, (v3){{0, k[3], 0}});
    // The bot cam's four numbers are WORLD x / height / z / look height, not the
    // subject-frame right/forward/up the hero shots use, so its swing is on the x-z
    // pair.
    v3 bpos = home_uncover(blook,
                v3_add(bp, (v3){{k[0] * caz - k[2] * saz, k[1] + wob,
                                 k[0] * saz + k[2] * caz}}));
    v3 bd = v3_sub(blook, bpos);
    ov->active = 1;
    ov->bot = 0;
    ov->eye = bpos;
    ov->yaw = atan2f(bd.x, -bd.z);
    ov->pitch = f_clamp(atan2f(bd.y, sqrtf(bd.x * bd.x + bd.z * bd.z)),
                        -PITCH_MAX, PITCH_MAX);
    return 1;
  }
  if (!sh->cu) {
    if (g_home_orb) { g_vmorb.on = 0; g_home_orb = 0; }
    v3 look = v3_add(feet, (v3){{0, k[3], 0}});
    v3 pos = v3_add(feet, v3_add(v3_scale(rgt, kr),
                    v3_add(v3_scale(fwd, kf), (v3){{0, k[2] + wob, 0}})));
    pos = home_uncover(look, pos);
    v3 d = v3_sub(look, pos);
    ov->eye = pos;
    ov->yaw = atan2f(d.x, -d.z) - HOME_COMPOSE + wob2 * 0.25f;
    ov->pitch = f_clamp(atan2f(d.y, sqrtf(d.x * d.x + d.z * d.z)),
                        -PITCH_MAX, PITCH_MAX);
    return 1;
  }
  // A close-up draws the weapon in the VIEWMODEL pass, which has its own projection and
  // clears depth — so what the world camera decides here is only the backdrop behind
  // it.
  ov->eye = home_uncover(v3_add(feet, (v3){{0, 1.45f, 0}}),
                         v3_add(feet, v3_add(v3_scale(fwd, 3.2f),
                                             (v3){{0, 1.52f + wob, 0}})));
  ov->yaw = yw + 0.26f * sinf(g_ui_time * 0.19f);
  // Tilted a hair UP rather than down: the horizon then crosses the frame
  // around its middle, so a black rifle has sky behind its top half and ground
  // behind its bottom half instead of sitting as one silhouette on one tone.
  ov->pitch = 0.03f + wob2 * 0.4f;
  g_vmorb.on = 1;
  g_home_orb = 1;
  g_vmorb.which = 2;
  g_vmorb.piv_on = 1;
  g_vmorb.yaw = k[0] * DEG2RAD;
  g_vmorb.pit = k[1] * DEG2RAD;
  g_vmorb.dist = k[2];
  g_vmorb.bore = k[3];
  g_vmorb.rise = sh->rise;
  g_vmorb.fov = sh->fov;
  // 0.16 of the frame: the subject sits at two thirds of the width, which is
  // the middle of what the tile rail leaves.
  g_vmorb.comp = 0.16f;
  return 1;
}
