// =============================================================================
// sim/locomotion.inc  —  mv_*: the one set of movement primitives (walk,
// steer, air, slide, jump) for player, bots and puppet alike.
// Proof: parity (24 rows through shared code), trace. One TU: included once
// by game.c, in definition order — moving a definition out is a semantic
// change.
// Contract: difficulty carries no movement numbers. Any second copy of a
// speed constant is a bug by definition. mv_locomote, the single dispatcher
// over these primitives, lives in sim/combat.inc — see task-4-report.md.
// =============================================================================
// ---------------------------------------------------------------------------
// Locomotion, shared by the player and by every bot. There is exactly ONE set of
// movement numbers in this game and these helpers are it.

// Ground move: one response time for both taking and releasing intent.
static void mv_walk(v3 *vel, float wx, float wz, float wlen, float max_speed) {
  float alpha = mv_alpha(g_sim_mv[MV_RESPONSE_MS]);
  vel->x += (wx * max_speed - vel->x) * alpha;
  vel->z += (wz * max_speed - vel->z) * alpha;
  if (wlen < 1e-5f && vel->x * vel->x + vel->z * vel->z < 1e-4f)
    vel->x = vel->z = 0;  // full stop: no perpetual micro-drift
}

// Take-off speed. The half-tick term compensates the discrete Euler integration
// so the apex lands on JUMP HEIGHT instead of ~3% short.
static float mv_jump_speed(void) {
  return sqrtf(2.0f * GRAVITY * g_sim_mv[MV_JUMP_H]) + 0.5f * GRAVITY * TICK_DT;
}

// Slide launch speed: SLIDE BOOST is a multiplier on run speed, so top speed and
// boost stay orthogonal knobs.
static float mv_slide_boost(void) {
  return g_sim_mv[MV_SLIDE_BOOST] * g_sim_mv[MV_RUN];
}

// The slide's settle threshold, and the exit predicate built on it.
static float mv_slide_end_speed(void) {
  return g_sim_mv[MV_CROUCH_SP] * SLIDE_END_SPEED;
}
static int mv_slide_should_end(int grounded, int hold, float sp2d) {
  return !hold || !grounded || sp2d < mv_slide_end_speed();
}

// Rotate the horizontal velocity toward the intent at CONSTANT speed — the
// slide's carve and the above-run-speed air redirect are the same operation, and
// were two copies of it. `rate_k` scales the turn rate (1 for a slide, AIR
// CONTROL's fraction in the air), which is the only thing that ever differed.
static void mv_steer(v3 *vel, float sp, float wx, float wz, float rate_k) {
  float t = f_clamp(rate_k * SLIDE_STEER_RATE * TICK_DT, 0, 1);
  float dx = vel->x / sp * (1 - t) + wx * t;
  float dz = vel->z / sp * (1 - t) + wz * t;
  float dl = sqrtf(dx * dx + dz * dz);
  if (dl > 1e-5f) {
    vel->x = dx / dl * sp;
    vel->z = dz / dl * sp;
  }
}

// Slide launch: take the shared boost, keeping any speed already above it (a slide may
// not SLOW you down).
static float mv_slide_launch(v3 *vel, float sp2d) {
  float boost = mv_slide_boost();
  float boosted = boost > sp2d ? boost : sp2d;
  vel->x *= boosted / sp2d;
  vel->z *= boosted / sp2d;
  return boosted;
}

// One tick of a slide: decay sized so a boosted slide carries SLIDE LENGTH metres
// before settling at the crouch-speed end threshold (an exponential glide from v0 down
// to ve at rate k covers (v0-ve)/k metres).
static float mv_slide_step(v3 *vel, float sp, float wx, float wz, float wlen) {
  const float *mv = g_sim_mv;
  float v0 = mv_slide_boost();
  float ve = mv_slide_end_speed();
  float k = (v0 - ve > 0.1f * v0 ? v0 - ve : 0.1f * v0) / mv[MV_SLIDE_LEN];
  float drop = sp * k * TICK_DT;
  float ns = sp - drop > 0 ? sp - drop : 0;
  if (sp > 1e-5f) {
    vel->x *= ns / sp;
    vel->z *= ns / sp;
  }
  if (wlen > 1e-5f && ns > 0.01f) mv_steer(vel, ns, wx, wz, 1.0f);
  return ns;
}

// AIR CONTROL is the fraction of ground authority kept while airborne.
static void mv_air(v3 *vel, float wx, float wz, float wlen, float sp2d) {
  const float *mv = g_sim_mv;
  if (wlen <= 1e-5f || mv[MV_AIR_CTL] <= 0) return;
  float ctl = mv[MV_AIR_CTL] * 0.01f;
  if (sp2d <= mv[MV_RUN]) {
    float alpha = mv_alpha(mv[MV_RESPONSE_MS]) * ctl;
    vel->x += (wx * mv[MV_RUN] - vel->x) * alpha;
    vel->z += (wz * mv[MV_RUN] - vel->z) * alpha;
  } else {
    mv_steer(vel, sp2d, wx, wz, ctl);
  }
}
