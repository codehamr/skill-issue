// =============================================================================
// render/viewmodel.inc  —  fx_build, vm_build, cam_override_t.
// Proof: vmcheck, vmsight, vmtrig, vmframe. One TU: included once by game.c, in
// definition order — moving a definition out is a semantic change.
// Contract: the FP arms are whole arms — shoulder->elbow->wrist via the one
// ik_solve, the same ANIM_UARM/ANIM_FARM skel_solve reads. vmorbit moves the
// EYE (since 2026-08-20); any view-dependent shading term through it before
// that date measured the wrong viewpoint.
// =============================================================================
typedef struct {
  v3 muzzle;
  long tick, publish_tick;
  unsigned serial, order;
  int live;
} fx_build_witness_t;
static fx_build_witness_t g_fx_build_witness;
typedef struct {
  v3 muzzle, mesh_muzzle;
  long tick;
  unsigned serial, order;
} vm_emit_witness_t;
static vm_emit_witness_t g_vm_emit_witness;
static unsigned g_vm_render_order;
static long g_vm_muz_tick = -1000; // exact publish tick; stale values are rejected

static void fx_build(v3 eye) {
  // The real consumer-side witness for vmframe.  Recording at fx_build entry—not in
  // the publisher—means a future pass-order regression cannot keep the proof green by
  // merely leaving the publish helper correct while moving its call below this one.
  g_fx_build_witness.muzzle = g_fx_muzzle;
  g_fx_build_witness.tick = g_tick;
  g_fx_build_witness.publish_tick = g_vm_muz_tick;
  g_fx_build_witness.live = g_fx_muz_live;
  g_fx_build_witness.serial++;
  g_fx_build_witness.order = ++g_vm_render_order;
  scene_reset();

  for (int i = 0; i < MAX_TRACERS; i++)
    if (g_tracers[i].life > 0) {
      float f = (float)g_tracers[i].life / TRACER_TICKS;
      v3 a = g_tracers[i].a, b = g_tracers[i].b;
      if (g_tracers[i].src == g_local_ent && g_fx_muz_live) a = g_fx_muzzle;
      v3 d = v3_sub(b, a);
      float len = v3_len(d);
      if (len < 0.6f) continue;
      // The tail holds AT the barrel for the first half of the life — those
      // are the ticks in which the eye checks where the shot came from, and
      // any recede there detaches the streak from the muzzle immediately on
      // a 17 m shot (a fraction of the LENGTH, not a speed) — then lets go
      // and collapses into the impact as it dies, which reads as arrival.
      float age = 1.0f - f;
      float t0 = smooth01(f_clamp((age - 0.45f) / 0.55f, 0.0f, 1.0f)) * 0.92f;
      const int N = 8;
      for (int sgi = 0; sgi < N; sgi++) {
        float ua = t0 + (1.0f - t0) * (float)sgi / N;
        float ub = t0 + (1.0f - t0) * (float)(sgi + 1) / N;
        v3 pa = v3_add(a, v3_scale(d, ua)), pb = v3_add(a, v3_scale(d, ub));
        // Colour runs on absolute distance, not on the segment index: a hot
        // white-amber birth at the barrel cooling to a deep tracer orange by
        // ~14 m, whatever the shot's length.
        float ka = f_clamp(ua * len * (1.0f / 14.0f), 0.0f, 1.0f);
        float kb = f_clamp(ub * len * (1.0f / 14.0f), 0.0f, 1.0f);
        v3 ca = v3_lerp((v3){{1.65f, 1.36f, 0.92f}}, (v3){{1.10f, 0.50f, 0.14f}}, ka);
        v3 cb = v3_lerp((v3){{1.65f, 1.36f, 0.92f}}, (v3){{1.10f, 0.50f, 0.14f}}, kb);
        v3 ha = v3_lerp((v3){{0.46f, 0.28f, 0.11f}}, (v3){{0.22f, 0.09f, 0.03f}}, ka);
        v3 hb = v3_lerp((v3){{0.46f, 0.28f, 0.11f}}, (v3){{0.22f, 0.09f, 0.03f}}, kb);
        // Width is an envelope over absolute METRES, not over the fraction of the shot:
        // a thin filament at the bore blooming to full glow by ~2.5 m, tapering
        // downrange.
        float ra = f_clamp(ua * len * (1.0f / 2.5f), 0.0f, 1.0f);
        float rb = f_clamp(ub * len * (1.0f / 2.5f), 0.0f, 1.0f);
        float wha = (0.010f + 0.045f * ra) * (1.0f - 0.5f * ka);
        float whb = (0.010f + 0.045f * rb) * (1.0f - 0.5f * kb);
        float wca = (0.0040f + 0.0050f * ra) * (1.0f - 0.45f * ka);
        float wcb = (0.0040f + 0.0050f * rb) * (1.0f - 0.45f * kb);
        fx_seg(pa, pb, eye, wha, whb, v3_scale(ha, f * f), v3_scale(hb, f * f));
        fx_seg(pa, pb, eye, wca, wcb, v3_scale(ca, f), v3_scale(cb, f));
      }
    }
  for (int i = 0; i < MAX_FLASHES; i++) {
    flash_t *fl = &g_flashes[i];
    if (fl->src == g_local_ent && g_fx_vm_flash) continue;  // the vm star owns it
    int fresh = fl->fresh && g_tick - fl->t0 <= 12;
    if (fl->life <= 0 && !fresh) continue;
    float f = fresh ? 1.0f : (float)fl->life / FLASH_TICKS;
    fl->fresh = 0;
    v3 t1, t2;
    axis_frame(fl->dir, &t1, &t2);
    uint32_t h = fx_hash(fl->seed ^ 0x9E37u);
    float s = (0.22f + 0.08f * (float)((h >> 8) & 255) / 255.0f) *
              (0.45f + 0.55f * f) * (fl->wpn == WPN_SR ? 1.45f : 1.0f);
    fx_flash(fl->pos, fl->dir, t1, t2, eye, s, f, fl->seed, fl->wpn);
  }
  for (int i = 0; i < MAX_PARTICLES; i++)
    if (g_parts[i].life > 0) {
      const particle_t *q = &g_parts[i];
      float f = (float)q->life / (float)q->life0;   // 1 at birth -> 0 at death
      if (q->kind == 3) {
        // Contact flare: the glowing POINT where the knuckle plate is being dragged.
        float nf3;
        if (!fx_near_ease(q->pos, eye, &nf3)) continue;
        v3 vd = v3_norm(v3_sub(eye, q->pos));
        v3 t1, t2;
        axis_frame(vd, &t1, &t2);
        v3 col = v3_scale((v3){{1.75f, 0.80f, 0.17f}}, f * f * nf3);
        v3 tipc = v3_scale(col, 0.10f);
        float w = q->sz * 0.34f;
        for (int k = 0; k < 3; k++) {
          float ang = 0.5235988f + (float)k * 1.0471976f;   // 30 + k*60 deg
          v3 d = v3_add(v3_scale(t1, cosf(ang) * q->sz),
                        v3_scale(t2, sinf(ang) * q->sz));
          fx_seg(v3_sub(q->pos, d), q->pos, eye, w * 0.06f, w, tipc, col);
          fx_seg(q->pos, v3_add(q->pos, d), eye, w, w * 0.06f, col, tipc);
        }
        continue;
      }
      if (q->kind == 1) {
        // Slide dust: a warm sand mote that grows and thins as it dies — additive, so
        // it reads as a sunlit puff over the arena floor.
        float fd = (float)q->life / PARTICLE_TICKS;
        float g2 = 1.0f - f_clamp(fd, 0.0f, 1.0f);
        v3 tail = v3_add(q->pos, v3_scale(q->vel, -0.040f));
        v3 col = v3_scale((v3){{0.105f, 0.093f, 0.075f}}, fd * fd * 0.55f);
        fx_ribbon(q->pos, tail, eye, 0.006f + 0.010f * g2, col);
        continue;
      }
      if (q->kind == 5) {
        // KILL BURST. Same near-camera ease as the body spark — see the note
        // at KILLBURST_STREAK for why it is shared rather than widened, and
        // what the blowout actually responded to.
        float nf;
        if (!fx_near_ease(q->pos, eye, &nf)) continue;
        // The same two-lerp cooling law as the skid ember and the body spark — green
        // falls first and red last, because a straight white->red lerp passes through
        // pink and nothing hot is ever pink.
        float a1 = 1.0f - f;
        v3 col = fx_cool(a1, 0.30f, (v3){{4.30f, 3.45f, 1.90f}},
                         (v3){{2.60f, 0.86f, 0.10f}}, (v3){{0.80f, 0.05f, 0.01f}});
        col = v3_scale(col, f_clamp(f * 3.0f, 0.0f, 1.0f) * q->sz * nf);
        v3 tail = v3_add(q->pos, v3_scale(q->vel, -KILLBURST_STREAK));
        // Widths take the range scale with the rest of the geometry — `y0` on this
        // kind, see the spawn.
        float wc = 0.0080f * q->y0, wh = 0.0230f * q->y0;
        fx_teardrop(q->pos, tail, eye, wc, wh, col, fx_warm_halo(col));
        continue;
      }
      if (q->kind == 4) {
        // Body impact spark.
        float nf;
        if (!fx_near_ease(q->pos, eye, &nf)) continue;
        // Two lerps like the skid ember and for the same reason: a straight white->red
        // lerp passes through pink, so green has to fall first and red last.
        float a1 = 1.0f - f;
        v3 col = fx_cool(a1, 0.22f, (v3){{3.10f, 2.05f, 0.78f}},
                         (v3){{2.20f, 0.52f, 0.06f}}, (v3){{0.72f, 0.04f, 0.01f}});
        col = v3_scale(col, f_clamp(f * 3.0f, 0.0f, 1.0f) * q->sz * nf);
        // The streak's length is a time constant times the SPEED, so the faster spray
        // also draws longer lines — which is most of why the burst reads as an
        // explosion of fragments rather than a puff of dots. ...and the same ANGULAR
        // range scale kind 5 carries, out of `y0`, which this kind never used (it does
        // not bounce, so it has no floor).
        v3 tail = v3_add(q->pos, v3_scale(q->vel, -0.030f * q->y0));
        float wc = 0.0068f * q->y0, wh = 0.0185f * q->y0;
        fx_teardrop(q->pos, tail, eye, wc, wh, col, fx_warm_halo(col));
        continue;
      }
      if (q->kind == 6) {
        // BRASS. Not a spark: it is a lit OBJECT, so it is drawn at a flat brass colour
        // that does not cool, and what makes it read is the TUMBLE — the sliver's
        // projected length swings between full and nearly nothing as it turns, which is
        // exactly the glint a case throws.
        v3 dqb = v3_sub(q->pos, eye);
        float dqbl = v3_dot(dqb, dqb);
        if (dqbl < 0.10f * 0.10f) continue;
        v3 vd = v3_norm(v3_sub(eye, q->pos));
        v3 t1, t2;
        axis_frame(vd, &t1, &t2);
        float ph = q->y0 + (float)(q->life0 - q->life) * TICK_DT * BRASS_SPIN;
        // The second cosine is the FORESHORTENING: a case seen end-on is a dot.
        float ln = BRASS_LEN * q->sz * (0.28f + 0.72f * fabsf(cosf(ph * 0.61f)));
        v3 ax = v3_add(v3_scale(t1, cosf(ph) * ln), v3_scale(t2, sinf(ph) * ln));
        float fo = f_clamp((float)q->life / (0.25f * (float)q->life0), 0.0f, 1.0f);
        v3 col = v3_scale((v3){{1.15f, 0.74f, 0.26f}}, fo);
        fx_seg(v3_sub(q->pos, ax), q->pos, eye, 0.0016f, 0.0030f,
               v3_scale(col, 0.45f), col);
        fx_seg(q->pos, v3_add(q->pos, ax), eye, 0.0030f, 0.0016f,
               col, v3_scale(col, 0.45f));
        continue;
      }
      if (q->kind == 2) {
        // A skid spark is a COOLING FRAGMENT. Two lerps and not one, because a
        // straight white->red lerp passes through pink: green has to fall
        // first and red last, which is what a black body actually does.
        float a1 = 1.0f - f;
        v3 col = fx_cool(a1, 0.15f, (v3){{2.70f, 1.72f, 0.62f}},
                         (v3){{1.95f, 0.56f, 0.05f}}, (v3){{0.78f, 0.06f, 0.01f}});
        // Fade the last third out: an additive streak that simply stops existing pops,
        // and at 120 Hz that pop is one whole frame of a bright line vanishing.
        col = v3_scale(col, f_clamp(f * 3.0f, 0.0f, 1.0f) * (0.55f + 0.45f * q->sz));
        // Velocity-aligned STREAK: the length is a time constant times the
        // speed, so a fragment thrown hard draws a long line and one that has
        // nearly stopped draws a dot. That one term is most of the read.
        v3 tail = v3_add(q->pos, v3_scale(q->vel, -0.032f * q->sz));
        // A hero is LONGER much more than it is fatter: scaling both by `sz`
        // gave it the same 15:1 aspect as the crowd, so it read as a bigger
        // shard rather than as a spark that flew further.
        float wk = 0.55f + 0.45f * q->sz;
        float wc = 0.0021f * wk, wh = 0.0060f * wk;
        // The skid ember's halo is deliberately HOTTER than fx_warm_halo's shared cut:
        // measured against the display-encoded floor at 0.78, 0.075 green holds the sum
        // at G 0.87 / B 0.69 round a core that is still allowed to clip — an ember at
        // arm's length.
        v3 halo = {{col.x * 0.34f, col.y * 0.075f, col.z * 0.030f}};
        fx_teardrop(q->pos, tail, eye, wc, wh, col, halo);
        continue;
      }
      // KIND 0 — the WORLD impact spark, and the effect the player triggers most often,
      // on every round that misses.
      float nf;
      if (!fx_near_ease(q->pos, eye, &nf)) continue;
      float a1 = 1.0f - f;
      v3 col = fx_cool(a1, 0.18f, (v3){{2.60f, 1.90f, 0.90f}},
                       (v3){{1.85f, 0.62f, 0.10f}}, (v3){{0.70f, 0.07f, 0.01f}});
      col = v3_scale(col, f_clamp(f * 3.0f, 0.0f, 1.0f) * nf);
      // `y0` is the ANGULAR range scale — see the spawn.
      v3 tail = v3_add(q->pos, v3_scale(q->vel, -0.026f * q->y0));
      // Wider than the body spark's 0.0068/0.0185, deliberately: a round into
      // concrete throws a bigger, cooler, dustier chip than a round into
      // webbing, and this is the effect the player sees on every missed shot —
      // it has to READ, and measured at 8 m the old 12 mm bar was 0.96 px wide.
      float wc = 0.0092f * q->y0, wh = 0.0320f * q->y0;
      fx_teardrop(q->pos, tail, eye, wc, wh, col, fx_warm_halo(col));
    }
}

// --- viewmodel: the AR, built in gun-local space (x right, y up, z forward,
// origin at the receiver rear) and placed by the pose + spring offsets. -----

// Rotate a onto b's plane by ang (small-rotation compose for the gun basis).
static void rot2(v3 *a, v3 *b, float ang) {
  float c = cosf(ang), s = sinf(ang);
  v3 na = v3_add(v3_scale(*a, c), v3_scale(*b, s));
  *b = v3_add(v3_scale(*b, c), v3_scale(*a, -s));
  *a = na;
}

// The viewmodel pass has its own projection, deliberately wider than the world FOV: at
// 50 degrees a weapon close enough to read is mostly outside the frame.
#define VM_FOV_DEG 62.0f
static v3 g_vm_muzzle;  // world-space barrel tip of the most recently emitted mesh
static v3 g_vm_mesh_muzzle; // gun_build return; vmframe checks it against the plan
// The weapon's own basis in world space, published by the same vm_build call that
// builds the mesh.
static v3 g_vm_mdl_o, g_vm_mdl_x, g_vm_mdl_y, g_vm_mdl_z;
static v3 g_vm_muz_cam; // current pre-glow plan tip in CAMERA space
                        // (right/up/forward), FOV-remapped for the world tracer

// TURNTABLE for the viewmodel (harness only).
static struct { int on, which, piv_on;
                float yaw, pit, dist, fov, bore, rise, comp; } g_vmorb;
// Hand/wrist diagnostics, published by vm_build for the `vmhand` command.
static struct { float bend_deg, bore_deg, palm_mm, off_mm; } g_vm_hdbg[2];
static v3 g_vm_hand_p[2];   // trigger wrist, support wrist — published by vm_build

// Per-frame animation inputs computed once by vm_stage_pose and shared by
// vm_stage_springs (the pose's own "every spring offset on top") and
// vm_stage_hands, and read directly by vm_build itself for the gun_build
// call and the scope-disc block that follow.
typedef struct {
  int cur, srcyc, bore_aligned;
  float ads, ready, sight_y, hip, lean, lean_d, sw;
  float kick_back, kick_lift, kick_side, dip;
  float gunwall;   // eased wall tuck: FP twin of the 3P carriage override
  gunanim_t ga;
} vm_pose_t;

// The weapon's own basis for this frame, as computed by vm_stage_springs.
typedef struct { v3 org, gx, gy, gz; } vm_frame_t;

// A complete, mesh-free viewmodel frame.  The render pipeline needs the current
// barrel tip before the glow pass (the opaque viewmodel itself is deliberately drawn
// later), while the harness needs to build the identical pose without publishing it.
// Keeping pose, basis and muzzle in this value lets both callers use one pure solve;
// only vm_build_prepared below emits geometry or changes the published diagnostics.
typedef struct {
  vm_pose_t pose;
  vm_frame_t frame;
  v3 muzzle;
} vm_build_plan_t;

#define VLERP(f) (v->prev_##f + (v->f - v->prev_##f) * alpha)

static vm_pose_t vm_stage_pose(const player_t *p, const vm_t *v, const weapon_t *w,
                               float alpha, float ready) {
  vm_pose_t ps;
  float ads = w->prev_ads_s + (w->ads_s - w->prev_ads_s) * alpha;
  float kick_back = VLERP(kick_back);
  float kick_lift = VLERP(kick_lift);
  float kick_side = VLERP(kick_side);
  float dip = p->prev_land_dip + (p->land_dip - p->prev_land_dip) * alpha;

  int cur = w->cur;
  float sight_y = cur == WPN_SR ? VM_SIGHT_SR : VM_SIGHT_AR;

  // Reload, trigger and bolt all come from ONE solver shared with the
  // third-person figure (`gun_anim`) — see the note there. `rl` is the body
  // arc, `rk` the two beat impulses, `rch` the charging-handle / bolt lobe,
  // `mdrop` the magazine's own travel and `mgv` the support hand's.
  gunanim_t ga_ = gun_anim(w);
  // Swap: the incoming weapon rises from below, muzzle tilted down.
  float sw = smooth01(w->prev_switch +
                      ((float)w->switch_t / GUN_SWITCH_TICKS - w->prev_switch) * alpha);

  // Pose: hip vs sight-aligned, then every spring offset on top.
  // FRAMING. The hip pose used to sit 155 mm below the eye and 300 mm forward
  // with the gun's own axis parallel to the view, so the camera looked DOWN onto
  // the weapon: the top rail and the barrel filled the corner and the receiver,
  // the magwell, the magazine and the stock were all outside the frustum. What
  // the player stared at all match was a black ladder. A first-person weapon is
  // a composition, not a placement — it is raised until its SIDE faces the
  // camera, canted so the right of the receiver turns into view, and pulled back
  // until the magazine is in frame. Every extra term is scaled by `hip` and is
  // therefore exactly zero in ADS, where the sight has to stay on the axis.
  float hip = 1.0f - ads;
  // Lean: how far the gun is behind the body's lean right now (0 once settled), and the
  // settled lean itself.
  float lean = p->prev_lean + (p->lean - p->prev_lean) * alpha;
  float lean_d = (v->prev_lean_lag + (v->lean_lag - v->prev_lean_lag) * alpha) - lean;
  // THE RELOAD DIP USED TO PUT THE WHOLE WEAPON BELOW THE FRAME. At 0.30 m in front of
  // the eye a 62-degree pass has 0.18 m of half-height, and the hip pose already sits
  // 0.120 m down; another 0.100 m plus 0.25 rad of muzzle-down pitch took everything
  // but the top rail off screen for the entire 1.8 s.
  int srcyc = cur == WPN_SR;

  ps.ads = ads; ps.ready = f_clamp(ready, 0.0f, 1.0f);
  float gw = VLERP(gunwall);
  ps.gunwall = gw;
  // Reload and swap cannot fire, and neither can a walled rifle (gun_shoot's wall
  // gate). In every other state the rendered barrel is the gameplay barrel;
  // composition may translate and roll around it, never invent a second pitch/yaw
  // axis. The wall tuck may: its whole point is showing a barrel that cannot fire.
  // The tuck terms are all hip-scaled, so in ADS a residual tuck moves nothing and
  // must NOT unhook the bore lock — the (1 - ads) mirrors the terms it guards.
  ps.bore_aligned = w->reload_t == 0 && w->switch_t == 0 &&
                    gw * (1.0f - ads) < 0.001f;
  ps.kick_back = kick_back; ps.kick_lift = kick_lift;
  ps.kick_side = kick_side; ps.dip = dip; ps.cur = cur; ps.sight_y = sight_y;
  ps.ga = ga_; ps.sw = sw; ps.hip = hip; ps.lean = lean; ps.lean_d = lean_d;
  ps.srcyc = srcyc;
  return ps;
}

static vm_frame_t vm_stage_springs(const vm_pose_t *ps, const vm_t *v, float alpha,
                                    v3 eye, v3 rgt, v3 upv, v3 fwd, v3 bore) {
  int cur = ps->cur, srcyc = ps->srcyc;
  const gun_pose_desc_t *erg = gun_pose_desc(cur);
  float sight_y = ps->sight_y, ads = ps->ads, dip = ps->dip;
  float kick_back = ps->kick_back, kick_lift = ps->kick_lift;
  float kick_side = ps->kick_side;
  float hip = ps->hip, lean = ps->lean, lean_d = ps->lean_d, sw = ps->sw;
  float rl = ps->ga.arc, rk = ps->ga.punch, rch = ps->ga.cyc;
  // The reload's micro-motion: the ring-down after each beat and the magazine's
  // own mass in the hands — see gunanim_t. Small numbers on purpose: these are
  // the difference between a pose that snaps between keyframes and a mechanism
  // with weight in it, and past ~2x these values they read as the shooter
  // losing the weapon rather than working it.
  float jl = ps->ga.jolt, hf = ps->ga.heft;
  // The wall tuck: pull the rifle in and down off the wall, muzzle dipped, a touch
  // of roll — the FP reading of the 3P lowered carriage. Scaled by hip: ADS is being
  // denied while walled, so during the ramp-out overlap the sight stays on axis and
  // the tuck arrives only as the hip pose returns.
  float gw = ps->gunwall * hip;

  // BREATHING: two incommensurate frequencies (0.30 Hz and its 1.57x partner)
  // so the idle never visibly loops, walking with the SIM CLOCK — a breath on
  // the render clock is the caret-blink trap, and every harness shot of an
  // idle weapon would depend on how many frames preceded it. Mostly bled out
  // in ADS: what little survives is the hold, not a wander — the HUD dot is
  // the honest point of aim and a sway that moves the picture but not the
  // shot would split them.
  float bt = ((float)g_tick + alpha) * TICK_DT;
  float swc = vm_motion_scale(cur);
  float br_k = swc * (0.22f + 0.78f * hip);
  float br_p = (sinf(bt * F_TAU * 0.30f) * 0.7f +
                sinf(bt * F_TAU * 0.47f) * 0.3f) * 0.0032f * br_k;
  float br_y = sinf(bt * F_TAU * 0.30f) * 0.0016f * br_k;
  // The slide pose: tucked in, canted, muzzle up — the one-handed carry's
  // reading at first-person scale. All of it rides `hip`, so ADS mid-slide
  // keeps the sight welded to the axis.
  float sl = VLERP(slide_s);
  // The FP view consumes the SAME eased carry state as the body. Only the SR has a
  // relative low-ready composition; hip keeps it out of ADS and busy states raise the
  // shared blend before this render. These are small view-relative deltas, never the
  // 20/15-degree world carry angles.
  float low = cur == WPN_SR ? (1.0f - ps->ready) * hip : 0.0f;
  float px = 0.158f * hip + VLERP(bob_x) + VLERP(lag_x) - rl * 0.072f
           + rk * 0.011f + jl * 0.0016f + rch * (srcyc ? -0.062f : 0.026f)
           + kick_side
           + (lean_d * 0.16f - lean * 0.028f - sl * 0.048f) * hip
           + erg->fp_low_pos.x * low;
  // DOWN a little and NOT toward the eye.
  float py = -0.120f * hip - sight_y * ads + VLERP(bob_y) + VLERP(lag_y)
           // sw * 0.125, not 0.22: THE SWAP PUT THE WHOLE WEAPON BELOW THE FRAME, which
           // is the identical failure the reload dip above was already fixed for.
           - dip * 0.5f + rl * 0.084f + rch * (srcyc ? 0.044f : 0.020f)
           - rk * 0.020f + jl * 0.0030f + hf * 0.0045f
           - sw * 0.125f + br_y - sl * 0.020f * hip
           + erg->fp_low_pos.y * low - 0.045f * gw;
  // The sniper pulls closer to the eye in ADS so the scope window opens up.
  float pz = 0.30f
           - 0.022f * hip - (cur == WPN_SR ? 0.152f : 0.06f) * ads - kick_back
           + VLERP(lag_z) - sw * 0.03f - rl * 0.020f
           - rch * (srcyc ? 0.062f : 0.030f) + rk * 0.016f
           - sl * 0.040f * hip + erg->fp_low_pos.z * low - 0.110f * gw;

  v3 gx = rgt, gy = upv, gz = fwd;
  // sw * 0.42, not 0.7: 0.7 rad of muzzle-down put the barrel tip a further 0.32 m
  // below an already-off-frame origin — see the note on py above.
  // The vertical-inertia channel also TILTS: a body that rises or drops under
  // the gun leaves the muzzle a beat behind in angle as well as height — the
  // jump float, the fall lead-down and the crouch settle are all this term.
  rot2(&gz, &gy, -(VLERP(sway_p) + rl * 0.055f - rch * 0.045f
                 - rk * 0.075f - jl * 0.020f - hf * 0.012f
                 + sw * 0.42f) + 0.045f * hip
                 + kick_lift
                 + br_p + VLERP(lag_y) * 0.9f * (0.15f + 0.85f * hip)
                 + sl * 0.20f * hip
                 + erg->fp_low_ang.x * low - 0.30f * gw);                  // pitch
  rot2(&gx, &gz, VLERP(sway_y) + rl * 0.10f + jl * 0.016f
                 - rch * (srcyc ? 0.13f : 0.0f)
                 + (-0.075f + lean_d * 0.22f + sl * 0.06f) * hip
                 + kick_side + erg->fp_low_ang.y * low);                    // yaw
  // THE CANT HAS A SIGN, and it decides whether the reload is about the magazine or
  // about the top rail. The SR's was 0.86, which swept the scope's OCULAR
  // across the camera at the bolt beat — a black disc dead-centre, the rifle
  // apparently pointed into the shooter's own face. 0.55 plus the yaw term
  // above keeps the bolt raceway in view and the glass off-axis.
  float cyroll = cur == WPN_SR ? 0.40f : 0.34f;
  rot2(&gx, &gy, VLERP(roll) + rl * -0.46f + rch * cyroll - rk * 0.16f
                 - jl * 0.055f - hf * 0.040f +
                 (0.090f - lean * 0.10f + lean_d * 0.30f + sl * 0.10f) * hip
                 - kick_side * 1.4f + erg->fp_low_ang.z * low
                 + 0.22f * gw);                                             // roll
  if (ps->bore_aligned) {
    // Preserve the authored roll/cant by projecting its right axis onto the plane
    // normal to the authoritative bore. This is a pure twist: translations and stock
    // compression remain visible, while the mesh axis becomes exactly the gameplay
    // axis supplied by weapon_t — not the camera's separate head line.
    bore = v3_norm(bore);
    gx = v3_sub(gx, v3_scale(bore, v3_dot(gx, bore)));
    if (v3_dot(gx, gx) <= 1e-8f)
      gx = v3_sub(rgt, v3_scale(bore, v3_dot(rgt, bore)));
    gx = v3_norm(gx);
    gz = bore;
    gy = v3_norm(v3_cross(gx, gz));
  }
  v3 org = v3_add(eye, v3_add(v3_scale(rgt, px),
           v3_add(v3_scale(upv, py), v3_scale(fwd, pz))));

  vm_frame_t fr;
  fr.org = org; fr.gx = gx; fr.gy = gy; fr.gz = gz;
  return fr;
}
#undef VLERP

// The same pure transform gun_build and pose_muzzle consume, applied to the already-
// solved FP frame at its required unit scale. render_frame may therefore publish it
// before any mesh is built, with no second gun build or hidden draw.
static v3 vm_plan_muzzle(const vm_pose_t *ps, const vm_frame_t *fr) {
  return gun_muzzle_point(ps->cur, fr->org, fr->gx, fr->gy, fr->gz, 1.0f);
}

static vm_build_plan_t vm_plan_make(const player_t *p, float alpha,
                                    v3 eye, v3 rgt, v3 upv, v3 fwd,
                                    v3 bore, float ready) {
  vm_build_plan_t plan;
  plan.pose = vm_stage_pose(p, &p->vm, &p->wp, alpha, ready);
  plan.frame = vm_stage_springs(&plan.pose, &p->vm, alpha, eye, rgt, upv, fwd,
                                bore);
  plan.muzzle = vm_plan_muzzle(&plan.pose, &plan.frame);
  return plan;
}

// Hands and forearms. Without them the first-person view is a rifle floating in space
// — the one thing the player looks at for a whole match, held by nobody.
static void vm_stage_hands(const vm_pose_t *ps, v3 org, v3 gx, v3 gy, v3 gz,
                            v3 eye, v3 rgt, v3 upv, v3 fwd) {
  int cur = ps->cur;
  float ads = ps->ads;
  const gun_pose_desc_t *erg = gun_pose_desc(cur);
  const float pose_scale = 1.0f;  // first-person weapon and GH contacts are unit scale
  gunanim_t ga_ = ps->ga;
  float mdrop = ga_.mag, mgv = ga_.hand;
  {
    // FROM THE FIGURE'S ONE KIT, not restated. The one thing that may
    // legitimately differ is a LIGHTING compensation.
    // The viewmodel has its own hero rig and its own depth-cleared pass; the
    // world sun and the shadow map do not reach it, and at equal albedo it
    // measures a few percent darker than the same cloth on the body (measured:
    // rendered mean luminance of the cloth pixels, 95 on the body against 75
    // on the viewmodel before this was compensated at all). That factor lives
    // here, once, instead of in a second set of colours that can drift.
    kit_t vkt = fig_kit(0.0f, (v3){{1, 1, 1}});
    v3 sleeve = v3_scale(vkt.sleeve, 1.055f);   // shared uniform, VM-lit
    v3 glove  = v3_scale(vkt.glove, 1.055f);    // ...and his own gloves
    // The cuff belongs to the GLOVE, not to the sleeve.
    v3 gnt    = v3_scale(glove, 0.88f);
    // BOTH hands are placed on the weapon's OWN published grip cylinders (`gun_hold`)
    // rather than on hand-tuned offsets.
    probe("vm_hands");
    struct { v3 wrist, gc, ga; float gr, side; } hd[2];
    // THE RELOAD IS THE ONE MOMENT OF A VIEWMODEL THAT IS ALL HAND, and it was the one
    // moment nothing happened: the gun dipped and rolled while both hands stayed welded
    // where they were, so a magazine change read as "the rifle tilts a bit".
    fig_hand_diag_reset();
    for (int h = 0; h < 2; h++) {
      v3 gc, ga; float gr;
      gun_hold(cur, h == 0 ? GH_GRIP : GH_FORE, org, gx, gy, gz, &gc, &ga, &gr);
      if (h == 1 && mgv > 0.5f) {
        v3 rc, ra; float rr;
        gun_hold(cur, GH_RELD, org, gx, gy, gz, &rc, &ra, &rr);
        // The magazine drops away on the same curve the model uses, so the hand has
        // to travel with it or it grips where the mag no longer is. `cur` IS the table
        // index — spelling both arms out was two copies of one expression, and only
        // the table said they differed.
        rc = v3_add(rc, v3_add(v3_scale(gy, -mdrop * GUN_MAG_TRAVEL[cur][0]),
                               v3_scale(gx, -mdrop * GUN_MAG_TRAVEL[cur][1])));
        gc = v3_lerp(gc, rc, mgv);
        ga = v3_norm(v3_lerp(ga, ra, mgv));
        gr = gr + (rr - gr) * mgv;
        gc = v3_add(gc, gun_reload_route_offset(erg, gx, gy, gz, mgv, 1.0f));
      }
      // THE RIGHT HAND WORKS THE BOLT. This is the other half of a bolt gun's reload
      // and the half a player recognises instantly — a magazine change alone is an AR
      // reload with a different magazine in it.
      if (h == 0 && ga_.bhand > 0.0f && cur == WPN_SR) {
        // A bolt knob is a small ball gripped across, not a cylinder wrapped
        // along: the axis is the weapon's own right, and the radius is the knob's.
        v3 kc = gun_bolt_knob(ga_.bolt, org, gx, gy, gz, 1.0f), ka = gx;
        float kr = BOLT_HAND_R;
        gc = v3_lerp(gc, kc, ga_.bhand);
        ga = v3_norm(v3_lerp(ga, ka, ga_.bhand));
        gr = gr + (kr - gr) * ga_.bhand;
      }
      // Wrist bearing, authored as a raw direction and then projected onto the plane
      // perpendicular to the grip axis, so it stays a legal bearing however the axis is
      // raked.
      const gun_hand_pose_t *eh = &erg->fp_hand[h];
      v3 br_local = v3_lerp(eh->bearing_hip, eh->bearing_ads, ads);
      // On the magazine the arm comes from the left and FORWARD instead of from
      // below, which is what turns the travel into a reach rather than a slide.
      if (h == 1 && mgv > 0.5f)
        br_local = v3_lerp(br_local, eh->bearing_reload, mgv);
      if (h == 0 && ga_.bhand > 0.0f)
        br_local = v3_lerp(br_local, eh->bearing_bolt, ga_.bhand);
      v3 br = gun_pose_offset(gx, gy, gz, br_local, pose_scale);
      br = v3_sub(br, v3_scale(ga, v3_dot(br, ga)));
      br = v3_norm(br);
      // How far the wrist stands off the surface, and how far back along the knuckle
      // row it sits.
      float lift = (eh->lift_hip + (eh->lift_ads - eh->lift_hip) * ads) * pose_scale;
      // How far DOWN the grip axis the wrist sits.
      float axo = (eh->axial_hip + (eh->axial_ads - eh->axial_hip) * ads) * pose_scale;
      lift += (eh->reload_lift * mgv + eh->bolt_lift * ga_.bhand) * pose_scale;
      axo += (eh->reload_axial * mgv + eh->bolt_axial * ga_.bhand) * pose_scale;
      hd[h].gc = gc; hd[h].ga = ga; hd[h].gr = gr;
      hd[h].side = h == 0 ? 1.0f : -1.0f;       // right hand on the grip
      hd[h].wrist = v3_add(gc, v3_add(v3_scale(ga, axo),
                    v3_scale(br, gr + lift)));
    }
    g_vm_hand_p[0] = hd[0].wrist;
    g_vm_hand_p[1] = hd[1].wrist;
    for (int h = 0; h < 2; h++) {
      v3 wv = v3_sub(hd[h].wrist, hd[h].gc);
      v3 r = v3_sub(wv, v3_scale(hd[h].ga, v3_dot(wv, hd[h].ga)));
      g_vm_hdbg[h].off_mm = (v3_len(r) - hd[h].gr) * 1000.0f;
    }
    for (int h = 0; h < 2; h++) {
      const gun_hand_pose_t *eh = &erg->fp_hand[h];
      mat_set(MAT_GLOVE_R, 0.0f);
      v3 pd;
      // A hand that has left the grip for the bolt is not on the trigger, and routing
      // its index at a blade 60 mm away would stretch the digit across the receiver.
      int trigger_contact = ga_.arc + ga_.cyc + ga_.hand + ga_.bhand <= 0.001f;
      int style = h == 0 ? (ga_.bhand > 0.35f ? 0 : 1)
                         : (mgv > 0.5f ? 0 : 2);
      int hand_slot = h == 0 ? FIG_HAND_FIRE : FIG_HAND_SUPPORT;
      fig_hand_contact_t contact =
        fig_hand_contact_pose(hand_slot, mgv, ga_.bhand, 0);
      fig_hand(hd[h].wrist, hd[h].gc, hd[h].ga, hd[h].gr, hd[h].side, glove,
               style, h == 0 && trigger_contact ? &g_gun_trig_p : NULL,
               hand_slot, 0, contact, gx, &pd);
      g_vm_hdbg[h].palm_mm = g_fig_hand_diag[h].palm_mm;
      mat_set(MAT_CLOTH_R, 0.0f);
      // THE FOREARM'S DIRECTION IS DERIVED FROM THE PALM'S, and that is the whole
      // wrist. Authored independently — an elbow offset in gun-local space chosen so
      // the arm left the frame nicely — the two disagreed by 39 degrees (support) and
      // 53 (trigger), fig_chain straddled that corner with a bevel pair, and the arm
      // read as a hand stuck on the end of a pipe at right angles.
      //
      // THE WRIST BEND IS AN ANGLE, NOT A LERP. Easing `-pd` toward a straight-down
      // vector buys however many degrees happen to separate those two directions at
      // that hold, so one constant meant 3.4 degrees on one wrist and 16.4 on the
      // other — saturated on the hand the player actually looks at, whose forearm then
      // inherited the palm's own sweep and came at the rifle from directly below. A
      // real support arm sits 40-55 degrees off the bore.
      //
      // Two axes, because a wrist has two: EXTENSION about the palm's breadth axis and
      // DEVIATION about the palm normal — radial on the support hand, ulnar on the
      // trigger hand. Expressed this way the two hands' numbers are comparable
      // quantities instead of accidents of how far `down` happened to be from `pd`.
      v3 bx = v3_sub(hd[h].ga, v3_scale(pd, v3_dot(hd[h].ga, pd)));
      bx = v3_dot(bx, bx) > 1e-8f ? v3_norm(bx) : v3_norm(v3_cross(pd, gx));
      v3 bn = v3_norm(v3_cross(pd, bx));            // out of the palm
      float ext = eh->ext_hip + (eh->ext_ads - eh->ext_hip) * ads;
      float dev = eh->dev_hip + (eh->dev_ads - eh->dev_hip) * ads;
      v3 back = v3_rot_axis(v3_scale(pd, -1.0f), bx, hd[h].side * ext);
      back = v3_norm(v3_rot_axis(back, bn, hd[h].side * dev));
      // AND THEN LEAN IT BACK. Getting the wrist angle right left the support forearm
      // hanging plumb under the rifle: measured on the hip frame its screen axis went
      // 19.4 degrees off vertical to 7.4 — twelve degrees the WRONG way, straight back
      // toward the perpendicular approach the wrist work was supposed to cure.
      float bias = eh->bias_hip + (eh->bias_ads - eh->bias_hip) * ads;
      // A magazine hand moves to a different grip axis but remains a live support
      // forearm, not the explicit SR-bolt exception. Blend a separate boreward bias
      // with the production reload lobe so every intermediate station stays inside
      // the same 35..65-degree ergonomics contract as Hip/ADS.
      bias += eh->bias_reload * mgv;
      back = v3_norm(v3_add(back, v3_scale(gz, bias)));
      // THE ARM IS WHOLE NOW. The forearm used to be a 268 mm stub aimed by the
      // authored wrist angles above and cut off by the frame edge; the arm now
      // runs from a real camera-space SHOULDER through a SOLVED ELBOW to the
      // same wrist — the same ik_solve, the same ANIM_UARM/ANIM_FARM and the
      // same chain family the third-person figure wears, so first and third
      // person are one man. The shoulder hangs off the EYE (the camera IS the
      // figure's eye) and the shoulder line is bladed 0.52 rad exactly as
      // skel_solve blades it: the trigger shoulder back, the support forward.
      // In ADS the shoulders ride 70 mm up relative to the eye — the 3P weld
      // drops the HEAD onto the stock, and a camera that may not drop reads
      // that same motion as the body rising around it.
      float bside = hd[h].side;
      v3 sho = v3_add(eye, v3_add(
          v3_scale(rgt, bside * ANIM_SHO_HALF * cosf(ANIM_BLADE)),
          v3_add(v3_scale(fwd, -bside * ANIM_SHO_HALF * sinf(ANIM_BLADE)),
                 v3_scale(upv, -(0.190f - 0.045f * ads)))));
      // The wrist is welded to the published hold, so settle the off-screen shoulder
      // at the nearest point that lets the descriptor's preferred forearm be an exact
      // two-bone solution. This keeps both anatomical lengths and makes the drawn arm,
      // not merely a diagnostic vector, obey the authored wrist angles.
      v3 want_elb = v3_add(hd[h].wrist, v3_scale(back, ANIM_FARM));
      v3 se = v3_sub(sho, want_elb);
      se = v3_dot(se, se) > 1e-8f ? v3_norm(se) : v3_scale(back, -1.0f);
      sho = v3_add(want_elb, v3_scale(se, ANIM_UARM));
      // Keep the shoulder on the camera-facing side of the weapon. The exact
      // length solve above can pull it through the torso at ADS, hiding the
      // upper arm even though the wrist is correctly welded to the gun.
      sho = v3_add(sho, v3_scale(rgt, bside * 0.018f));
      // `ik_solve` projects the pole perpendicular to shoulder->wrist; the preferred
      // elbow's vector from the settled shoulder selects the correct side of its circle.
      v3 pole = v3_sub(want_elb, sho);
      v3 elb = ik_solve(sho, hd[h].wrist, ANIM_UARM, ANIM_FARM, pole);
      // Upper arm: the deltoid IS the chain's own fat top, exactly as on the
      // figure (fig_arms' stations verbatim) — buried toward the chest so the
      // seam can never show, k=12 like the FP forearm: at arm's length from
      // the lens a facet is a wall.
      v3 ua = v3_sub(elb, sho);
      v3 inb = v3_norm(v3_sub(v3_add(eye, v3_scale(upv, -0.34f)), sho));
      // The upper arm RAMPS DOWN toward the elbow exactly as the forearm ramps
      // down toward it from the other side (slv4 is 0.58 of the sleeve): the
      // fold is where the two chains and the joint ball meet at first-person
      // range, and a full-value upper arm against a 0.58 forearm end read as
      // two different garments stitched at the elbow.
      v3 usl1 = v3_scale(sleeve, 0.80f), usl2 = v3_scale(sleeve, 0.64f);
      v3 usl3 = v3_scale(sleeve, 0.54f);
      fig_upper_arm(sho, elb, inb, upv, sleeve, sleeve, usl1,
                    usl2, usl2, usl3, 12);
      // The elbow ball survives only as the cap-swallower between the two arm
      // chains, and it wears the VALUE THE TWO CHAINS ARRIVE WITH (0.55), not
      // the shoulder's. 12 % larger than fig_arms' numbers: at first-person
      // range a chamfered cap rim poking one millimetre out of the ball is a
      // bright torn edge across the fold, where at 3P range it is sub-pixel.
      fig_joint(elb, ua, v3_sub(hd[h].wrist, elb), 0.0565f, 0.0640f, 0.05f, 12,
                v3_scale(sleeve, 0.55f));
      // The diagnostics report the arm that is DRAWN: the solved elbow's
      // direction, not the authored one it replaced.
      v3 fdir = v3_sub(elb, hd[h].wrist);
      fdir = v3_dot(fdir, fdir) > 1e-8f ? v3_norm(fdir) : back;
      g_vm_hdbg[h].bend_deg = acosf(f_clamp(v3_dot(v3_scale(pd, -1.0f), fdir),
                                            -1.0f, 1.0f)) / DEG2RAD;
      g_vm_hdbg[h].bore_deg = acosf(f_clamp(fabsf(v3_dot(fdir, gz)),
                                            0.0f, 1.0f)) / DEG2RAD;
      // A WRIST, and this is the change that made the hands exist at all. The
      // real elbow rescales the chain onto the solved segment.
      fig_forearm(hd[h].wrist, back, pd, gx, hd[h].side, sleeve, gnt, &elb, 12);
    }
    mat_set(MAT_PARK_R, MAT_PARK_M);
    probe(NULL);
  }
}

static void vm_build_prepared(v3 eye, v3 rgt, v3 upv, v3 fwd,
                              const vm_build_plan_t *plan) {
  scene_reset();

  vm_pose_t ps = plan->pose;
  int cur = ps.cur;
  float sight_y = ps.sight_y;
  gunanim_t ga_ = ps.ga;
  float mdrop = ga_.mag;

  vm_frame_t fr = plan->frame;
  v3 org = fr.org, gx = fr.gx, gy = fr.gy, gz = fr.gz;

  ao_off();                               // a gun at arm's length sees open sky
  g_fig_k = FIG_KMAX;                     // ...and always at full resolution
  if (g_fig_kcap && g_fig_k > g_fig_kcap)
    g_fig_k = g_fig_kcap;
  probe("vm_gun");
  g_vm_mdl_o = org; g_vm_mdl_x = gx; g_vm_mdl_y = gy; g_vm_mdl_z = gz;
  // BROKEN EDGES, FIRST PERSON ONLY, and that is an LOD decision of exactly the same
  // kind as g_fig_k above it rather than a second weapon model.
  g_cham = 0.0007f;
  g_cham_polish = 1;   // ...and this arris was broken by a machine — see cham_arris
  g_gun_hero = 1;      // ...and this is the one view that can resolve a 4.7 mm land
  g_vm_mesh_muzzle = gun_build(cur, org, gx, gy, gz, mdrop, ga_.bolt);
  // The return is retained as an executable contract with gun_build.  Both values are
  // derived from the single GUN_MUZ row and therefore agree bit-for-bit today; using
  // the plan's value here makes the pre-glow publish and the later emitted pass consume
  // the exact same production result. vmframe gates any future builder drift.
  g_vm_muzzle = plan->muzzle;
  g_vm_emit_witness.muzzle = g_vm_muzzle;
  g_vm_emit_witness.mesh_muzzle = g_vm_mesh_muzzle;
  g_vm_emit_witness.tick = g_tick;
  g_vm_emit_witness.serial++;
  g_vm_emit_witness.order = ++g_vm_render_order;
  g_gun_hero = 0;
  g_cham_polish = 0;
  g_cham = 0.0f;
  // The tracer re-anchor publish does NOT happen here: vm_build is also the
  // engine of the harness probes (vmcheck/vmsight/vmhand), which build forced
  // poses on a synthetic camera basis — a probe must stay a side-effect-free
  // observer, and publishing from inside it re-anchored the next live tracer
  // onto a pose the render never draws. render_frame's mesh-free pre-glow
  // plan, the one caller whose basis IS the camera, owns the publish.

  vm_stage_hands(&ps, org, gx, gy, gz, eye, rgt, upv, fwd);

  if (cur == WPN_SR) {
    // The scope disc is DERIVED from the ocular, not tuned: project the ocular's
    // glass radius at its own distance through the viewmodel's own projection, so
    // the picture fills the lens exactly in every pose and travels with the scope
    // as it comes up instead of fading in at a fixed screen spot.
    v3 ocu = v3_add(org, v3_add(v3_scale(gy, sight_y),
                                v3_scale(gz, SCOPE_OCU_Z)));
    g_gun_sight_p = ocu;
    g_gun_sight_r = SCOPE_OCU_R;
    v3 rel = v3_sub(ocu, eye);
    float oz = v3_dot(rel, fwd);
    if (oz > 0.02f) {
      // Work in the HUD's own virtual units (ui_scale, the same integer scale
      // ui_begin lays the HUD out with), derived from the real viewport rather
      // than assumed to be 360 tall: a disc sized for 360 lands above the lens
      // and half its size at any other height.
      float sc = ui_scale();
      float vh = (float)R.height / sc, vw = (float)R.width / sc;
      float f = (vh * 0.5f) / tanf(VM_FOV_DEG * 0.5f * DEG2RAD);
      g_scope_r = (SCOPE_OCU_R - 0.0016f) / oz * f;
      g_scope_cx = 0.5f * vw + v3_dot(rel, rgt) / oz * f;
      g_scope_cy = 0.5f * vh - v3_dot(rel, upv) / oz * f;
    } else {
      g_scope_r = 0.0f;
    }
  }
}

// Probe-facing compatibility entry point.  It solves once and emits once, but never
// publishes a live tracer anchor; only frame_render's real camera path owns that side
// effect.
#ifndef _WIN32
static void vm_build(const player_t *p, float alpha, v3 eye, v3 rgt, v3 upv, v3 fwd,
                     float ready) {
  vm_build_plan_t plan = vm_plan_make(p, alpha, eye, rgt, upv, fwd, fwd, ready);
  vm_build_prepared(eye, rgt, upv, fwd, &plan);
}
#endif

// ONE upload per rebuild, not one per pass. g_scene is filled by scene_build and then
// drawn from up to three times — the shadow depth pass, the main view and the scope FBO
// — with nothing writing the buffer in between; fx_build and vm_build refill it for
// their own passes and flag it again.
static void scene_bind(void) {
  glBindVertexArray(R.vao_scene);
  glBindBuffer(GL_ARRAY_BUFFER, R.vbo_scene);
  if (g_scene_dirty) {
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(g_scene_floats * sizeof(float)),
                 g_scene, GL_STREAM_DRAW);
    g_scene_dirty = 0;
  }
}

static void scene_draw(void) {
  if (!g_scene_floats) return;
  scene_bind();
  glDrawArrays(GL_TRIANGLES, 0, g_scene_floats / VERT_FLOATS);
}

// The shadow pass's view of the same buffer: everything up to the local
// first-person body (see g_scene_cast_floats). Identical to scene_draw when
// nothing set the mark.
static void scene_draw_cast(void) {
  int fl = g_scene_cast_floats >= 0 ? g_scene_cast_floats : g_scene_floats;
  if (!fl) return;
  scene_bind();
  glDrawArrays(GL_TRIANGLES, 0, fl / VERT_FLOATS);
}

// The same buffer, minus the chunks this frustum cannot see.
static void scene_draw_vp(const mat4 *vp) {
  if (!g_scene_floats) return;
  scene_bind();
  frustum_t fr = frustum_of(vp);
  int run0 = 0;
  for (int i = 0; i < g_nschunk; i++) {
    if (frustum_sees(&fr, g_schunk[i].c, g_schunk[i].r)) continue;
    // The gap before this chunk stays in the run; the chunk itself does not.
    if (g_schunk[i].f0 > run0)
      glDrawArrays(GL_TRIANGLES, run0 / VERT_FLOATS,
                   (g_schunk[i].f0 - run0) / VERT_FLOATS);
    run0 = g_schunk[i].f1;
  }
  if (g_scene_floats > run0)
    glDrawArrays(GL_TRIANGLES, run0 / VERT_FLOATS,
                 (g_scene_floats - run0) / VERT_FLOATS);
}

// Sky AFTER the world at depth 1.0 (LEQUAL, so only background pixels shade), then hand
// the world program back with detail OFF for the moving geometry that follows — that
// epilogue is load-bearing and easy to lose.
static void sky_pass(float yaw, float pitch, float roll, float fovy, float aspect) {
  v3 sf, sr, su;
  cam_basis(yaw, pitch, roll, &sf, &sr, &su);
  float ty = tanf(fovy * 0.5f);
  glUseProgram(R.prog_sky);
  rig_upload(&R.rig_sky);
  glUniform2f(R.u_sky_wind, cosf(g_wind_ang), sinf(g_wind_ang));
  { v3 sxz = v3_norm((v3){{g_sun.dir.x, 0.0f, g_sun.dir.z}});
    glUniform2f(R.u_sky_sunxz, sxz.x, sxz.z); }
  glUniform1f(R.u_sky_cov, g_sun.cloud);
  { float dk; v3 rp = ridge_params(&dk);
    glUniform4f(R.u_sky_ridge, rp.x, rp.y, rp.z, dk); }
  glUniform3f(R.u_sky_fwd, sf.x, sf.y, sf.z);
  glUniform3f(R.u_sky_right, sr.x, sr.y, sr.z);
  glUniform3f(R.u_sky_up, su.x, su.y, su.z);
  glUniform2f(R.u_sky_tan, ty * aspect, ty);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_FALSE);
  glBindVertexArray(R.vao_sky);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glDepthMask(GL_TRUE);
  glDepthFunc(GL_LESS);
  glUseProgram(R.prog_world);
  glUniform1f(R.u_detail, 0.0f);  // moving geometry: no world-anchored detail
}

// Additive light pass: depth-tested, not depth-written, ONE/ONE, vertex colour straight
// to the screen.
static void fx_pass_begin(void) {
  glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE);
  glUniform1f(R.u_emis, 1.0f);   // glow: vertex color straight to the screen
}
static void fx_pass_end(void) {
  glUniform1f(R.u_emis, 0.0f);
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glDepthMask(GL_TRUE);
}

// Renders one frame: world from the interpolated player camera, then HUD.
// Camera can be overridden: fixed (debug shots) or bot-following (films the
// bot's walk/topple animations from a chase offset).
typedef struct {
  int active, bot, orbit;
  v3 eye, boff;
  float yaw, pitch, blook;
  float orbit_radius, orbit_height, orbit_rate, orbit_look, orbit_phase;
} cam_override_t;
