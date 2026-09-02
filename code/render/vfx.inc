// =============================================================================
// render/vfx.inc  —  tracers/particles/marks/killfeed state, vfx_on_event,
// corpses, the scene batch (scene_push, scene_chunk_begin/end).
// Proof: budget, shot battery. One TU: included once by game.c, in
// definition order — moving a definition out is a semantic change.
// Contract: scene_chunk_begin/end tags a figure's vertex range with an
// analytic 2.0 m sphere for per-frustum culling — everything between chunks
// is always drawn, so an untagged builder can only be over-drawn, never
// dropped. fx_flash/fx_dust themselves are built in render/figure_draw.inc,
// not here — see task-4-report.md.
// =============================================================================
// ---------------------------------------------------------------------------
// VFX + dynamic scene batch. All lifetimes are TICKS, never wall-clock, so
// every effect is deterministic and capturable in headless screenshots (tracer visible
// at `wait 2`, gone by `wait 12`).
// ---------------------------------------------------------------------------

#define MAX_TRACERS   24
#define MAX_PARTICLES 1024
// The pool is SPLIT, not merely shared.
#define PARTS_SKID_LO 640
// The two numbers above are coupled and far apart in the file: if the split ever
// climbs past the pool, both skid loops become no-ops and the whole effect
// disappears silently, with nothing to see but an absent spark.
static_assert(PARTS_SKID_LO < MAX_PARTICLES, "skid region must be inside the pool");
#define MAX_MARKS     64
#define MAX_FLASHES   8
#define TRACER_TICKS  8
#define FLASH_TICKS   7   // flame in the first half, sparks + dust glow after
// 14 and 26, not 10 and 20 (117 ms / 217 ms).
#define HITMARK_TICKS 14
#define KILLMARK_TICKS 26
// THE MARKER IS ARMED ON A DELAY, and it is the same trick — for the same measured
// reason — that the audio layer already applies with wpn_def_t.sfx_hit_t.
#define HITMARK_ARM_MIN 2      // ticks: never zero, or a contact hit is swamped
#define PARTICLE_TICKS 26
// A round landing on a MAN throws a burst too, and it is a different material from a
// rock chip: fragments a body stopped come BACK along the round rather than off a
// surface normal, they are gone in a tenth of a second, and they are redder.
#define IMPACT_SPARKS  18      // per burst off WORLD geometry: a stone chip
#define HITSPARK_N     22      // ...and off a MAN. Nearly three times the stone
                               // burst, because a round stopping in armour and webbing
                               // is the most violent thing that happens in a firefight
                               // and the one the player is playing FOR — at 8 it read
                               // as a flicker at any range past a couple of metres.
#define HITSPARK_TICKS 20      // must OUTLIVE TRACER_TICKS (8): the streak's
                               // tail collapses onto this exact point and owns
                               // the first few ticks, so a shorter burst is
                               // buried inside the tracer that caused it
#define HITSPARK_BACK  4.4f    // m/s of back-spray toward the shooter, sampled
                               // 0.55-1.0 of it (the stone chip leaves at
                               // 1.5-4.5 along a surface normal; a body absorbs)
#define HITSPARK_SIDE  2.7f    // m/s of scatter in the frame perpendicular to
                               // the ROUND, not to world axes: comparable to
                               // HITSPARK_BACK or the burst is a needle
#define HITSPARK_IN    0.020f  // m ALONG the ray, INTO the box. The hitbox is
                               // 440 x 600 mm against a 341 mm-deep carrier, so
                               // its face stands well proud of the mesh: backing
                               // the spawn OFF the box would hang the burst in
                               // open air beside the man.
#define HITSPARK_HEAD_K 1.55f  // a headshot is the SAME burst, hotter and
                               // faster — a layer, exactly as the audio's head
                               // signature is a layer on whichever base ran
#define HITSPARK_NEAR  0.50f   // m from the CAMERA under which a body spark is
                               // not drawn.
#define HITSPARK_FADE  2.60f   // ...and where that ease is complete. A target's
                               // head at contact range is 0.6-1.5 m out, so the blowout
                               // is not confined to the player's own body and a step at
                               // 0.5 m cannot cover it.
static_assert(HITSPARK_TICKS > TRACER_TICKS,
              "the body spark must outlive the tracer that lands on it");
// A KILL IS THE EVENT THE WHOLE GAME IS PLAYED FOR, AND IN THE WORLD IT LOOKED EXACTLY
// LIKE A GRAZE. The hitmarker and the kill feed are both SCREEN SPACE; out in the arena
// a killing round pushed the same kind-4 body spark a 26-damage nick does, so the one
// moment worth celebrating had no read where the player is actually looking.
#define KILLBURST_N      44     // 2x the body spark's 22...
#define KILLBURST_TICKS  34     // ...living 1.7x as long: long enough for the arc
                                // to be a shape, short enough to be gone before
                                // the corpse has finished falling.
#define KILLBURST_SP     11.0f  // m/s peak, sampled 0.62-1.0 of it, against the
                                // body spark's 2.4-4.4.
#define KILLBURST_REF    8.0f   // m. Reference range, where the burst is exactly
                                // as authored — most duels happen here.
#define KILLBURST_K_LO   0.20f  // ...and the clamps. Below this a contact kill
#define KILLBURST_K_HI   2.10f  // blows out; above it a far kill is a firework.
#define KILLBURST_BACK   0.30f  // x speed, toward the shooter. Half a sphere
                                // points into the body and is occluded by it, so
                                // the centroid moves onto the shooter's side or
                                // half the fragments are never seen.
#define KILLBURST_UP     0.34f  // x speed, world up. Gravity is 22, so without
                                // this the burst is flat in a dozen ticks.
#define KILLBURST_DRAG   0.965f // per tick. This burst leaves 2-4x faster than
                                // any other spark, and drag is what makes it a fragment
                                // that SLOWS; at constant speed every streak is the
                                // same length for its whole life and it reads as a
                                // firework, not as debris.
#define KILLBURST_STREAK 0.040f // s of velocity drawn as the fragment's line,
                                // against 0.030 on the body spark
#define KILLBURST_SLOW   0.30f  // ...of peak speed is the SLOWEST fragment,
                                // against the body spark's 0.55.
static float fx_range_k(float d) {
  return f_clamp(d / KILLBURST_REF, KILLBURST_K_LO, KILLBURST_K_HI);
}

// BRASS. One discrete object leaving the weapon per trigger pull, which is the cheapest
// "the mechanism cycled" cue the genre has and the only one the player sees LEAVE the
// gun rather than happen to something else.
#define BRASS_TICKS  102     // 0.85 s: long enough to arc and land in frame
#define BRASS_OUT    1.35f    // m/s to the shooter's right — an AR ejects right
#define BRASS_UP     1.55f    // ...and up, so it arcs rather than drops
#define BRASS_FWD    0.35f   // a little forward, off the ejection port
#define BRASS_SPIN   17.0f   // rad/s of tumble. A casing that does not tumble is
                             // a stick; the glint IS the read, and it comes from
                             // the length changing as it turns.
#define BRASS_LEN    0.016f  // half-length of the drawn sliver, 32 mm case
#define BRASS_PORT_F 0.42f   // m in front of the eye: where the receiver renders
#define SCORE_POP_TICKS 24   // 0.2 s of pulse on the frag number
#define STREAK_TICKS   150   // 1.25 s of banner — shorter than the 3 s window
                             // it counts, so a triple replaces its own double
                             // rather than queueing behind it
#define KILLFEED_N     5
#define KILLFEED_TICKS 480   // 4 s on screen, the last 0.5 s fading
#define DMG_TICKS      36    // incoming-damage flash

typedef struct { v3 a, b; int life, src; } tracer_t;
// `fresh` is a render latch, not a lifetime: it marks a flash no frame has shown yet
// and is cleared by the first draw.
typedef struct { v3 pos, dir; int life, src, wpn; long t0; uint32_t seed; int fresh; } flash_t;
// kind: 0 = world impact spark, 1 = skid dust, 2 = skid spark, 3 = contact flare,
// 4 = body hitspark, 5 = kill burst, 6 = brass. `life0` is the spawn life, because the
// fade must normalise against the particle's OWN lifetime once lifetimes vary — a hero
// skid spark outlives a cheap one 3:1, and a shared divisor would leave it clipped
// white for two thirds of its life.
//
// `y0` IS OVERLOADED BY KIND, so read the kind before reading it. Kind 2, the only
// reader: the plane the fragment was struck FROM, so a slide across a platform skips
// off the platform and not off world zero — sampled at spawn, so fragments in flight
// when the slider leaves a raised surface keep the plane they were struck from. Kinds
// 1 and 3 store that plane too and nothing reads it. Kinds 0/4/5: the ANGULAR range
// scale from `fx_range_k`, which sizes the streak geometry only, never `sz` (which is
// heat). Kind 6: the tumble PHASE.
typedef struct { v3 pos, vel; float y0, sz; int life, life0, kind; } particle_t;
// AN IMPACT MARK IS A CRATER IN A SURFACE, NOT A STICKER ON IT, and it used to be both
// wrong things at once: an 84 mm disc of flat near-black — measured 4.6 stops under a
// shadowed crate and 5.8 under a sunlit wall, and that SPREAD is the defect a host-
// relative albedo fixes, when a full cast shadow here is 2.94 and the whole AO ramp is
// 0.41 — drawn with no idea which face it belonged to, so a round landing near a corner
// left a black disc hanging in open air past the edge.
typedef struct {
  v3       pos, nrm;        // lifted centre, face normal
  v3       surf;            // the host face's albedo — the crater is relative
  float    rad;             // this mark's own radius
  float    u0, u1, v0, v1;  // clip window in axis_frame(nrm)'s (t1,t2), and
                            // u0 <= 0 <= u1, v0 <= 0 <= v1 by construction:
                            // that is what makes the radial clamp correct
  float    ao;              // the host face's own RENDERED occlusion, flat —
                            // NOT ao_at(y). mesh_quad emits only the four face CORNERS,
                            // so the world pass interpolates AO linearly across a whole
                            // face while ao_at is a ramp that saturates at 0.55 m: on a
                            // 2.75 m wall the two disagree by 0.33, and a mark drawn on
                            // the ramp renders ~50% brighter in linear than the wall it
                            // is in.
  float    rough;           // ...and the host's finish, for the same reason
  uint32_t seed;            // size / rotation / rim wobble, no RNG stream
  // THE CRATER HAD NO BIRTH: written at its final size and value and drawn identically
  // from the first frame to the last, with no dust, no spall flash and nothing
  // directional — the one permanent record a shot leaves, arriving without an event.
  long     t0;              // the tick it was stamped
  int      used;
} mark_t;
#define MARK_R        0.030f  // 60 mm across, against 84. A 5.56 round in
                              // concrete leaves a 10-25 mm entry crater inside
                              // a 40-70 mm spall zone.
#define MARK_R_VAR    0.12f   // +/-12% per mark: 30 marks on one wall must not
                              // read as one stamp repeated
#define MARK_R1       0.50f   // core plateau edge, x MARK_R: a 30 mm hole. 0.36
                              // put a 21.6 mm core inside a 60 mm crater, which is sub-
                              // pixel past ~10 m: measured against a shot-free baseline
                              // of the same frame, two marks at 20 m moved five pixels
                              // in total and two of them were BRIGHTER than the wall,
                              // i.e. the halo survived and the hole did not.
#define MARK_R2       0.66f   // spall halo peak — a RING, not an outline
#define MARK_K        8       // fan segments: the authored-facet family, and the
                              // rim is deliberately irregular anyway TWO COPLANAR
                              // DECALS ARE A DEPTH TIE, AND A DEPTH TIE IS A FLICKER.
                              // Every mark used to sit at exactly MARK_LIFT along its
                              // face's normal, so any two that overlapped fought for
                              // the same pixels and the pair rendered as a torn blob
                              // whose seams moved with the camera — measured, a 0.1 mm
                              // camera nudge over four stacked holes changed 30425
                              // pixels.
#define MARK_MERGE    0.66f   // x (r1+r2): merge inside the SPALL radii, i.e.
                              // before the two haloes can overlap at all
#define MARK_PLANES   4       // distinct depth planes the ring cycles through
#define MARK_PLANE_DZ 0.0005f // 0.5 mm apart — above the 0.29 mm depth quantum
                              // at 24 m, and the mark is a pixel wide by the
                              // range where the quantum grows past it
#define MARK_LIFT     0.003f  // normal offset. The 24-bit depth quantum at
                              // ZNEAR 0.12 is d^2*4.96e-7 m = 1.14 mm at the arena's 48
                              // m diagonal, so this keeps 2.6x margin at the worst
                              // sightline in the game.
#define MARK_CORE_K   0.47f   // crater albedo x the host's: 2.0 stops under
#define MARK_HALO_K   1.12f   // fresh fractured material: +0.43 stop. Cannot
                              // overflow — the brightest palette entry is 0.83
                              // and col_tint's widest is 1.07: 0.995.
#define MARK_JIT_LO   0.86f   // rim wobble on the two outer rings only, +/-14%:
#define MARK_JIT_SPAN 0.28f   // a perfect circle is the strongest decal cue
#define MARK_ROUGH    0.95f   // the crater itself: scorched, no highlight left
#define MARK_BLOOM_K  3.1f    // x MARK_R at birth...
#define MARK_BLOOM_T  30      // ...collapsing to 1.0 over a quarter second
#define MARK_BLOOM_C  1.30f   // ...and the spall rings that much brighter with it
typedef struct { int src, dst, hs, life; } feed_t;

static tracer_t   g_tracers[MAX_TRACERS];
static flash_t    g_flashes[MAX_FLASHES];      // world-pass muzzle-flash stars
static particle_t g_parts[MAX_PARTICLES];
static mark_t     g_marks[MAX_MARKS];
static int        g_mark_head;
static int        g_muzzle_t;                  // viewmodel muzzle flash ticks left
static int        g_muzzle_fresh;              // render latch, see flash_t.fresh
static long       g_muzzle_fire_tick;          // its 12-tick staleness bound
// THE SCREEN NOW CARRIES THE SAME TWO AXES THE AUDIO DOES. `kind` is CONSEQUENCE (hit /
// kill) in bit 1 and PRECISION (body / head) in bit 0, which is the confirmation matrix
// exactly as THE CONFIRMATION MATRIX states it, and the head half is drawn as an
// additive LAYER on whichever base ran rather than as a fourth marker — the same reason
// sfx_head_sig is a layer.
enum { HM_BODY = 0, HM_HEAD = 1, HM_KILL = 2, HM_KILL_HEAD = 3 };
static int        g_hitmark_t, g_hitmark_kind;  // ticks left + which of the four
static int        g_hitmark_arm, g_hitmark_pend; // the per-weapon arming delay
static float      g_ch_kick;   // crosshair gap kick, in virtual px, decaying
// HIT-STOP. The player's own KILL only, and bounded twice over.
#define HITSTOP_KILL 5       // ticks (42 ms) of slow motion on a body kill...
#define HITSTOP_HEAD 7       // ...and on a headshot, which is the rarer event
#define HITSTOP_SCALE 0.18f  // ...at this fraction of real time
static int        g_hitstop;
// The world's muzzle light.
static float      g_world_muz_c;
static v3         g_world_muz_p;
// THE SCORE EXISTED ONLY WHILE TAB WAS HELD. A match is a 30-step progress bar
// (sp_fraglimit) and the only way to see where you were in it was to stop
// aiming and hold the scoreboard — so every kill was a transient row in a feed
// that fades in four seconds and nothing else. `g_score_pop` is a pulse on the
// player's own kill; the numbers themselves all live in g_match already.
// `g_streak_t` is the multi-kill banner, on the SAME 3 s window the audio's
// pitch escalation uses, so the two cannot disagree about what a double is.
static int        g_score_pop;   // ticks of scale pulse on the frag readout
static int        g_streak_n;    // 2 = double, 3 = triple, ... 0 = nothing
static int        g_streak_t;    // ticks the banner has left
static long       g_streak_last_t = -100000;  // VFX-side window latch, see EV_KILL
static feed_t     g_feed[KILLFEED_N];          // kill feed, newest first
static int        g_dmg_t;                     // incoming-damage flash ticks
// FOUR DIRECTIONS, NOT ONE. `g_dmg_from` was a single vector overwritten by every hit
// and `g_dmg_t` a single shared timer, so in a 20-bot free-for-all two attackers on
// opposite sides produced ONE tick that flipped between them and the second hit reset
// the first's fade — i.e. the cue that is supposed to say WHERE said "somewhere" the
// moment more than one person was shooting you.
#define DMG_SLOTS 4
static struct { v3 from; int life; } g_dmg[DMG_SLOTS];
static uint64_t   g_rng_vfx = 0x5EEDu;         // particle scatter only
// EV_KILL carries no position — match_on_kill pushes src/dst/k and nothing else — so
// the kill burst takes the wound from the EV_HIT of the SAME TICK. Exact rather than
// lucky: ent_damage pushes the hit event and then calls match_on_kill, and ev_push
// preserves order.
static struct { v3 at, dir; float dist; long tick; } g_kill_wound = {.tick = -1};
// THE SAME WOUND, BUT FOR ANY VICTIM. g_kill_wound above answers "where did MY killing
// round land" and is filled only for g_local_ent's own hits, which is all the killburst
// ever needed.
static struct { v3 at, from; long tick; int dmg, hs; } g_wound[MAX_ENTS];
// Alive state as of the previous tick, so a HUMAN's online death is an EDGE rather than
// a level — a level fires the launch (and the corpse handover) on every tick the body
// is down.
static unsigned char g_cl_alive_prev[MAX_ENTS];
// THE MARKER'S ARMING DELAY NEEDS ITS OWN LATCH. The audio layer's `g_last_wpn` looks
// like the obvious source and is wrong for this consumer: it is assigned in
// audio_on_event, which runs in the frame drain AFTER the ticks, while vfx_on_event
// runs at PUSH time inside the tick — so a marker armed from it reads the weapon of a
// shot drained in a PREVIOUS frame, and the first hit after a swap arms on the other
// weapon's offset.
static int g_vfx_wpn;

// The world-space barrel tip of a posed figure's rifle — defined with the gun
// tables below, declared here because the tracer spawn needs it.
static v3 pose_muzzle(const pose_t *P, int wpn);
// Defined with the FX primitives below, declared here for the same reason: the skid
// emitter needs deterministic variety and must NOT take it from g_rng_vfx.
static uint32_t fx_hash(uint32_t x);
// Perpendicular frame for a mark or a tracer whose only anchor is one
// direction — defined with the FX primitives below, declared here because
// mark_stamp resolves its clip window in exactly that frame.
static void axis_frame(v3 n, v3 *t1, v3 *t2);
// Hashed scatter, declared here because the body-hit burst wants the same
// deterministic variety the skid emitter uses and for the same reason.
static float skid_h(uint32_t *h);
static float skid_r(uint32_t *h, float lo, float hi);

// One kind-3 contact flare, in the reserved region, so a burst is anchored to the point
// it came off.
static void fx_flare(v3 at, float sz, int life) {
  for (int i = 0; i < MAX_PARTICLES; i++)
    if (g_parts[i].life <= 0) {
      g_parts[i] = (particle_t){.pos = at, .vel = (v3){{0, 0, 0}}, .y0 = 0.0f,
                                .sz = sz, .life = life, .life0 = life, .kind = 3};
      return;
    }
}

static void impact_free_slot(int want) {
  int free_n = 0;
  for (int i = 0; i < MAX_PARTICLES && free_n < want; i++)
    if (g_parts[i].life <= 0) free_n++;
  while (free_n < want) {
    int victim = -1, low = 1 << 30;
    for (int i = PARTS_SKID_LO; i < MAX_PARTICLES; i++)
      if (g_parts[i].life > 0 && g_parts[i].life < low) {
        low = g_parts[i].life;
        victim = i;
      }
    if (victim < 0) break;          // nothing but reserved traffic left
    g_parts[victim].life = 0;
    free_n++;
  }
}

// Stamp one crater on the face the round entered.
static void mark_stamp(v3 at, v3 n, int solid) {
  v3 t1, t2;
  axis_frame(n, &t1, &t2);
  v3 mn, mx, surf;
  float aom, rough;
  if (solid >= g_num_solids) solid = -1;   // EV_IMPACT.i rides the wire:
                                           // a hostile index reads as "no host
                                           // solid", never as g_solids[32767]
  if (solid >= 0) {
    mn = g_solids[solid].min; mx = g_solids[solid].max;
    surf = g_solids[solid].color;
    rough = WORLD_SOLID_R;
    // The host's RENDERED occlusion at this point: mesh_build sets the ramp,
    // mesh_quad emits the four face corners and the rasteriser interpolates
    // between them, so a +y face is uniformly ao_at(mx.y), a -y face is
    // ao_at(mn.y), and a side face is a straight lerp up its own height.
    float lo = ao_ramp(mn.y, 0.0f, WORLD_AO_RATE, WORLD_AO_MIN);
    float hi = ao_ramp(mx.y, 0.0f, WORLD_AO_RATE, WORLD_AO_MIN);
    float h = mx.y - mn.y;
    aom = n.y > 0.5f ? hi : n.y < -0.5f ? lo
        : h > 1e-4f ? lo + (hi - lo) * f_clamp((at.y - mn.y) / h, 0.0f, 1.0f)
        : lo;
  } else {                                  // the arena floor is its own plane
    mn = (v3){{-ARENA_HALF, 0.0f, -ARENA_HALF}};
    mx = (v3){{ ARENA_HALF, 0.0f,  ARENA_HALF}};
    // The floor is no longer ao_off: mesh_ground_vert bakes a contact skirt into
    // it, and a crater stamped at a flat 1.0 sits BRIGHTER than the ground it is
    // in wherever that skirt bites. One function, two readers.
    surf = g_theme.ground; aom = ground_skirt_ao(at.x, at.z);
    rough = WORLD_GROUND_R;
  }
  // MERGE FIRST: if this round landed on top of an existing hole on the same plane,
  // take THAT slot rather than a fresh one.
  int slot = g_mark_head, merged = 0;
  for (int i = 0; i < MAX_MARKS; i++) {
    const mark_t *m = &g_marks[i];
    if (!m->used || v3_dot(m->nrm, n) < 0.99f) continue;
    v3 d = v3_sub(at, m->pos);
    // `at` is the raw impact point but m->pos is the LIFTED centre, so the along-normal
    // distance for a genuine same-face overlap is exactly the old mark's own lift:
    // MARK_LIFT + up to (MARK_PLANES-1)*MARK_PLANE_DZ. The threshold must clear the
    // DEEPEST lift — at 0.004 every plane-3 mark could never be merged onto and plane-2
    // sat at zero margin, i.e. the documented PRIMARY mechanism silently never fired
    // for 25-50% of the ring. 2 mm of slack on top still cannot reach the far side of
    // the thinnest wall or a neighbouring corner face.
    if (fabsf(v3_dot(d, n)) >
        MARK_LIFT + (float)(MARK_PLANES - 1) * MARK_PLANE_DZ + 0.002f)
      continue;                                      // a different plane
    float lat = sqrtf(fmaxf(0.0f, v3_dot(d, d) - v3_dot(d, n) * v3_dot(d, n)));
    if (lat < (MARK_R + m->rad) * MARK_MERGE) { slot = i; merged = 1; break; }
  }
  // The lift is purely along n and t1.n == t2.n == 0, so the window below is the same
  // whether it is measured from the impact point or the lifted centre.
  float lift = MARK_LIFT + (float)(slot % MARK_PLANES) * MARK_PLANE_DZ;
  const float lift_max = MARK_LIFT + (MARK_PLANES - 1) * MARK_PLANE_DZ;
  v3 p = v3_add(at, v3_scale(n, lift));
  float u0 = 0, u1 = 0, v0 = 0, v1 = 0;
  for (int k = 0; k < 3; k++) {
    float lo = mn.e[k] - p.e[k], hi = mx.e[k] - p.e[k];
    float a = t1.e[k] * lo, b = t1.e[k] * hi;
    u0 += a < b ? a : b;  u1 += a < b ? b : a;
    a = t2.e[k] * lo;     b = t2.e[k] * hi;
    v0 += a < b ? a : b;  v1 += a < b ? b : a;
  }
  uint32_t h = fx_hash((uint32_t)g_tick * 2654435761u + (uint32_t)slot);
  g_marks[slot] = (mark_t){
    .pos = p, .nrm = n, .surf = surf, .ao = aom, .rough = rough,
    .seed = h, .t0 = g_tick, .used = 1,
    .rad = MARK_R * (1.0f - MARK_R_VAR +
                     2.0f * MARK_R_VAR * (float)((h >> 8) & 255) / 255.0f),
    // Inset by the lift, then clamped at the centre.
    .u0 = f_clamp(u0 + lift_max, u0, 0.0f), .u1 = f_clamp(u1 - lift_max, 0.0f, u1),
    .v0 = f_clamp(v0 + lift_max, v0, 0.0f), .v1 = f_clamp(v1 - lift_max, 0.0f, v1)};
  // A merge reuses a slot, so the ring's age order — and therefore which hole
  // the next 64 rounds evict — is only advanced when a NEW one was taken.
  if (!merged) g_mark_head = (g_mark_head + 1) % MAX_MARKS;
}

static void vfx_ev_fire(const event_t *e) {
    if (e->src == g_local_ent) g_vfx_wpn = e->wpn;   // the hitmarker's arming delay
    // THE ROUND STARTS AT THE EYE, THE PICTURE STARTS AT THE BARREL. `a - b*f` inverts
    // the ballistic segment back to the shooter's EYE, so the rifle's tip has to come
    // from the pose the shot was anchored on — ev_push runs ent_on_event, which
    // re-solves that anchor for a foreign shot, BEFORE this handler. Where no anchor
    // belongs to this shot the eye stands in: a tracer from the collar reads as a
    // tracer, one from nowhere does not. Brass, streak and flash then share the one
    // barrel frame, as they must.
    v3 md = v3_norm(e->b);
    v3 muz = (g_fire_anchor_src == e->src && g_fire_anchor_tick == g_tick)
                 ? pose_muzzle(&g_fire_anchor_pose, e->wpn)
                 : v3_sub(e->a, v3_scale(md, e->f));
    if (e->src == g_local_ent) {
      // `right` is the shot direction crossed with world up, which is where a port
      // faces on a level rifle and is close enough on a pitched one that nobody
      // could name the error.
      v3 rt = v3_cross(md, (v3){{0, 1, 0}});
      float rl = v3_len(rt);
      rt = rl > 1e-4f ? v3_scale(rt, 1.0f / rl) : (v3){{1, 0, 0}};
      uint32_t bh = fx_hash((uint32_t)g_tick * 2654435761u + 0x42524153u);
      for (int i = 0; i < MAX_PARTICLES; i++)
        if (g_parts[i].life <= 0) {
          v3 v = v3_add(v3_scale(rt, BRASS_OUT * skid_r(&bh, 0.80f, 1.25f)),
                v3_add(v3_scale((v3){{0, 1, 0}}, BRASS_UP * skid_r(&bh, 0.75f, 1.30f)),
                       v3_scale(md, BRASS_FWD * skid_r(&bh, -0.4f, 1.0f))));
          // Move back from the barrel tip to the receiver port. Muzzle, case and
          // tracer share the one physical barrel frame.
          g_parts[i] = (particle_t){
              .pos = v3_add(muz, v3_add(v3_scale(rt, 0.135f),
                            v3_add(v3_scale(md, BRASS_PORT_F),
                                   (v3){{0, -0.155f, 0}}))),
              .vel = v, .y0 = skid_r(&bh, 0.0f, F_TAU),   // tumble phase
              .sz = skid_r(&bh, 0.85f, 1.15f),
              .life = BRASS_TICKS, .life0 = BRASS_TICKS, .kind = 6};
          break;
        }
    }
    // The streak ENDS on the ballistic segment's own endpoint (`a`) and starts on the
    // barrel above, so it reads as the round's path while still leaving the weapon
    // the viewer can see — even after a remote pose has advanced.
    if (e->src < 0) return;
    // The pool cannot overflow at the default tune (fire cooldown 10 ticks >
    // the 8-tick life caps concurrency at one per shooter), but a DEV rpm
    // near the 1000 cap makes consecutive shots overlap — and the PLAYER's
    // own streak must never be the one that silently vanishes, so when no
    // slot is free it takes the oldest (the audio layer's lead-voice rule).
    {
      int slot = -1, oldest = -1, low = 1 << 30;
      for (int i = 0; i < MAX_TRACERS; i++) {
        if (g_tracers[i].life <= 0) { slot = i; break; }
        if (g_tracers[i].life < low) { low = g_tracers[i].life; oldest = i; }
      }
      if (slot < 0 && e->src == g_local_ent) slot = oldest;
      if (slot >= 0)
        g_tracers[slot] = (tracer_t){muz, e->a, TRACER_TICKS, e->src};
    }
    if (e->src == g_local_ent) {
      g_muzzle_t = FLASH_TICKS;
      g_muzzle_fresh = 1;             // one guaranteed bright frame, any fps
      g_muzzle_fire_tick = g_tick;
    }
    // Every shooter gets a world-pass star; whether it is DRAWN is the
    // renderer's per-camera call (fx_build skips the player's while the
    // viewmodel pass owns the flash) — gating the spawn on the third-person
    // CONFIG here left a free-cam film of the player firing with no flash.
    for (int i = 0; i < MAX_FLASHES; i++)
      if (g_flashes[i].life <= 0) {
        g_flashes[i] = (flash_t){muz, e->b, FLASH_TICKS, e->src, e->wpn, g_tick,
                                 (uint32_t)(e->i * 61 + e->src * 7), 1};
        break;
      }
}

static void vfx_ev_impact(const event_t *e) {
    // The range the round was taken at, for the angular scale.
    float rk = fx_range_k(e->f > 0.0f ? e->f : KILLBURST_REF);
    impact_free_slot(IMPACT_SPARKS + 1);
    for (int n = 0, i = 0; i < MAX_PARTICLES && n < IMPACT_SPARKS; i++)
      if (g_parts[i].life <= 0) {  // sparks: fast, short, mostly along the normal
        v3 v = {{rng_rangef(&g_rng_vfx, -3.4f, 3.4f),
                 rng_rangef(&g_rng_vfx, -0.8f, 4.2f),
                 rng_rangef(&g_rng_vfx, -3.4f, 3.4f)}};
        g_parts[i] = (particle_t){
            .pos  = v3_add(e->a, v3_scale(e->b, 0.02f)),
            .vel  = v3_add(v, v3_scale(e->b, rng_rangef(&g_rng_vfx, 1.5f, 4.5f))),
            .y0 = rk, .sz = 1.0f,
            .life = PARTICLE_TICKS, .life0 = PARTICLE_TICKS, .kind = 0};
        n++;
      }
    // THE CONTACT FLARE, which impacts never had. vfx_skid's own comment states the
    // reason it exists, verbatim: "a fragment is gone from the contact within two ticks
    // and nothing marks where it came from" — and measured on a ground shot at 3.4 m
    // the spark cloud's bounding box had separated ~90 px from the 10x5 px hole by t+20
    // with nothing at all at the contact point.
    fx_flare(v3_add(e->a, v3_scale(e->b, 0.02f)), 0.115f * rk, 9);
    mark_stamp(e->a, e->b, e->i);
}

// EV_HIT and EV_HIT_HEAD share this body (only `hot`/`g_hitmark_pend` read
// `e->type`) — the two used to be one switch case for exactly that reason.
static void vfx_ev_hit(const event_t *e) {
    // A BODY UNDER FIRE SPARKS AT THE POINT THE ROUND LANDED ON. `a` is that point and
    // `b` the shooter's origin, so the round's direction is a subtract and nothing re-
    // derives the hitscan.
    v3 d = v3_sub(e->a, e->b);
    float dl = v3_len(d);
    if (dl > 1e-4f) {
      d = v3_scale(d, 1.0f / dl);
      v3 t1, t2;
      axis_frame(d, &t1, &t2);
      float hot = e->type == EV_HIT_HEAD ? HITSPARK_HEAD_K : 1.0f;
      uint32_t base = (uint32_t)g_tick * 2654435761u +
                      (uint32_t)(e->dst + 1) * 2246822519u +
                      (uint32_t)(e->src + 1) * 3266489917u;
      impact_free_slot(HITSPARK_N + 1);   // ...+ the contact flare
      int n = 0;
      for (int i = 0; i < MAX_PARTICLES && n < HITSPARK_N; i++)
        if (g_parts[i].life <= 0) {
          uint32_t h = base + (uint32_t)n * 40503u;
          float sp = HITSPARK_BACK * hot * skid_r(&h, 0.55f, 1.0f);
          v3 v = v3_add(v3_scale(d, -sp),
                v3_add(v3_scale(t1, skid_r(&h, -HITSPARK_SIDE, HITSPARK_SIDE)),
                       v3_scale(t2, skid_r(&h, -HITSPARK_SIDE, HITSPARK_SIDE))));
          v.y += skid_r(&h, 0.15f, 1.30f);
          g_parts[i] = (particle_t){
              .pos = v3_add(e->a, v3_scale(d, HITSPARK_IN)),
              .vel = v, .y0 = fx_range_k(dl), .sz = hot,
              .life = HITSPARK_TICKS, .life0 = HITSPARK_TICKS, .kind = 4};
          n++;
        }
      fx_flare(v3_add(e->a, v3_scale(d, HITSPARK_IN)),
               0.052f * hot * fx_range_k(dl), 6);
      // ...and if this round turns out to have been the killing one, the
      // EV_KILL two pushes later needs the wound it landed on. See g_kill_wound.
      if (e->src == g_local_ent) g_kill_wound = (typeof(g_kill_wound)){e->a, d, dl, g_tick};
      // ...and for the VICTIM, whoever shot him: the client's ragdoll launch
      // reads this. Bounds-checked because dst is wire data on a client.
      if (e->dst >= 0 && e->dst < MAX_ENTS)
        g_wound[e->dst] = (typeof(g_wound[0])){e->a, e->b, g_tick, e->i, 0};
    }
    if (e->src == g_local_ent) {          // hitmarker: only the player's own hits
      // Armed on the weapon's OWN confirmation offset, so the marker lands in the hole
      // the impact spark leaves rather than on top of its peak.
      int arm = (int)(WPN_DEF[g_vfx_wpn].sfx_hit_t * (float)TICK_HZ + 0.5f);
      g_hitmark_arm = arm > HITMARK_ARM_MIN ? arm : HITMARK_ARM_MIN;
      g_hitmark_pend = e->type == EV_HIT_HEAD ? HM_HEAD : HM_BODY;
    } else if (e->dst == g_local_ent) {   // taking damage: flash + direction memory
      g_dmg_t = DMG_TICKS;
      // Refresh the slot already pointing that way (same shooter keeping fire on
      // you is ONE arc that stays lit, not four stacked ones), otherwise take
      // the most faded slot.
      {
        int best = 0, bl = 1 << 30;
        for (int i = 0; i < DMG_SLOTS; i++) {
          v3 d0 = v3_sub(g_dmg[i].from, e->b);
          if (g_dmg[i].life > 0 && v3_dot(d0, d0) < 9.0f) { best = i; bl = -1; break; }
          if (g_dmg[i].life < bl) { bl = g_dmg[i].life; best = i; }
        }
        g_dmg[best].from = e->b;
        g_dmg[best].life = DMG_TICKS;
      }
    }
}

static void vfx_ev_kill(const event_t *e) {
    if (e->src == g_local_ent) {
      // The kill REPLACES whatever hit was still waiting to be armed — the two are
      // pushed on the same tick by ent_damage, and a smeared pair of confirmations is
      // exactly what the audio layer's EV_HIT suppression exists to prevent.
      int arm = (int)(WPN_DEF[g_vfx_wpn].sfx_hit_t * (float)TICK_HZ + 0.5f);
      g_hitmark_arm = arm > HITMARK_ARM_MIN ? arm : HITMARK_ARM_MIN;
      g_hitmark_pend = e->k ? HM_KILL_HEAD : HM_KILL;
      g_hitmark_t = 0;
      // SET, not accumulated — see HITSTOP_KILL. A second kill inside the window
      // re-arms it to the same length rather than extending it.
      g_hitstop = e->k ? HITSTOP_HEAD : HITSTOP_KILL;
      g_score_pop = SCORE_POP_TICKS;
      // The banner counts the SAME 3 s window the kill pitch does, but off its OWN
      // latch: g_multi_t is written in the audio DRAIN, which runs after the ticks —
      // and in the harness not at all unless the audio route is on — so reading it here
      // left the banner permanently dead in every screenshot script and one frame stale
      // in the window.
      if (g_tick - g_streak_last_t > MULTI_WIN) g_streak_n = 0;
      g_streak_last_t = g_tick;
      if (++g_streak_n >= 2) g_streak_t = STREAK_TICKS;
      // THE KILL BURST. Same tick as the wound or nothing at all.
      if (g_kill_wound.tick == g_tick) {
        v3 d = g_kill_wound.dir;   // the round's own direction of travel
        float hot = e->k ? HITSPARK_HEAD_K : 1.0f;   // headshot, same layer
                                                     // idiom as the body spark
                                                     // and the audio's head
                                                     // signature
        // Hashed off (tick, victim, headshot) and not drawn from g_rng_vfx, for
        // the reason spelled out at skid_h: out of the shared stream the
        // scatter of every later world impact would depend on whether somebody
        // across the map happened to die at the same moment.
        uint32_t base = (uint32_t)g_tick * 2654435761u +
                        (uint32_t)(e->dst + 1) * 2246822519u + 0x4B494C4Cu;
        // Angular sizing — see KILLBURST_REF. It scales the GEOMETRY only, and
        // it rides in `y0` rather than in `sz` because those are two different
        // questions: `sz` is the burst's HEAT and drives the colour, and folding
        // a range factor into it would make a 20 m kill 1.7x brighter as well
        // as bigger, pushing the halo past the display headroom that the
        // ONE/ONE blend leaves and clipping the whole thing to white.
        float rk = fx_range_k(g_kill_wound.dist);
        impact_free_slot(KILLBURST_N);
        int n = 0;
        for (int i = 0; i < MAX_PARTICLES && n < KILLBURST_N; i++)
          if (g_parts[i].life <= 0) {
            uint32_t h = base + (uint32_t)n * 40503u;
            // A GENUINE SPHERE, not a disc in the frame perpendicular to the round.
            float cz = skid_r(&h, -1.0f, 1.0f);
            float cr = sqrtf(f_clamp(1.0f - cz * cz, 0.0f, 1.0f));
            float ca = skid_r(&h, 0.0f, F_TAU);
            float sp = KILLBURST_SP * hot * rk *
                       skid_r(&h, KILLBURST_SLOW, 1.0f);
            v3 v = {{cr * cosf(ca) * sp, cz * sp, cr * sinf(ca) * sp}};
            v = v3_add(v, v3_scale(d, -sp * KILLBURST_BACK));
            v.y += sp * KILLBURST_UP;
            g_parts[i] = (particle_t){
                .pos = v3_add(g_kill_wound.at, v3_scale(d, HITSPARK_IN)),
                .vel = v, .y0 = rk, .sz = hot,
                .life = KILLBURST_TICKS, .life0 = KILLBURST_TICKS, .kind = 5};
            n++;
          }
      }
    }
    memmove(g_feed + 1, g_feed, sizeof g_feed - sizeof g_feed[0]);
    g_feed[0] = (feed_t){e->src, e->dst, e->k, KILLFEED_TICKS};
}

static void vfx_on_event(const event_t *e) {
  switch (e->type) {
  case EV_FIRE:   vfx_ev_fire(e);   break;
  case EV_IMPACT: vfx_ev_impact(e); break;
  case EV_HIT:
  case EV_HIT_HEAD: vfx_ev_hit(e);  break;
  case EV_KILL:   vfx_ev_kill(e);   break;
  // Listed, not defaulted — see audio_on_event. A new event type that wants a
  // visual has to be answered here or the build fails.
  case EV_DRYFIRE:
  case EV_RELOAD:
  case EV_RELOAD_CANCEL:
  case EV_RESPAWN:
  case EV_STEP:
  case EV_LAND:
  case EV_END:
    break;
  }
}

// A slide has TWO ground contacts and they are two different physical events, so two
// particle classes rather than one with a colour swap: a rubber sole over grit lifts a
// mote that HANGS, while a glove's knuckle plate at 20 m/s strikes hot fragments that
// ARC, SKIP and cool from white through orange to red.
#define SKID_CONTACT_DY 0.15f  // fist clearance over the foot that is planted
#define SKID_V_LO       3.4f   // m/s: barely dragging — a few lazy sparks
#define SKID_V_HI      21.5f   // m/s: a full-speed entry — peak violence
// SKID_V_HI is not a taste number, it is the ENTRY SPEED, and getting it wrong takes
// the speed scaling out of the effect entirely: every slide launches at the same speed
// (mv_slide_launch takes max(SLIDE BOOST, current)), so the only thing that ever varies
// is the DECAY. Too low a ceiling pins `hot` at 1.0 for the first third of the slide —
// constant rate, constant burst, constant flare — and a deliberately slow 4.5 m/s entry
// then rendered within 13% of a full-speed one.
#define SKID_ATTEMPTS   5      // spawn tries per contact tick; see `rate`
#define SKID_VY_MAX     5.30f  // m/s of lift — the E-SPORTS CEILING on this
// effect, and the reason it is a clamp rather than a range end: `rise` is scaled by
// heat and multiplied again on the strike tick, and three multipliers with nothing at
// the end of them is how an effect gets into the crosshair.

// Variety from a HASH of (tick, slot) rather than from g_rng_vfx, and here that is
// load-bearing rather than tidy: this emitter wants six numbers per spark and up to
// four sparks a tick for EVERY sliding entity — 2880 draws a second per fast slider.
static short g_skid_t_all[SRV_LOBBIES][MAX_ENTS];
#define g_skid_t (g_skid_t_all[g_lobby])

// --- corpses ---------------------------------------------------------------
// A BODY MUST OUTLIVE THE ENTITY THAT WAS IT. A dead bot IS its own corpse: bot_tick's
// dead branch integrates the ragdoll, and when respawn_t hits zero bot_spawn teleports
// the same entity to a fresh point, so the body does not fall away, it BLINKS OUT — and
// the only lever on how long it lies there is the respawn delay, which is match pacing
// and must not move.
#define MAX_CORPSES     6      // sized against the vertex budget, not taste:
                               // see CORPSE_K
#define CORPSE_K        8      // the faceted profile tier. A body on the floor
                               // is scenery and does not need the 16-gon the
                               // living wear; measured, six corpses at their own
                               // distance tier plus a 20-bot hard brawl overran
                               // SCENE_MAX_VERTS by 60k and dropped triangles,
                               // which is holes in whatever is drawn last.
#define CORPSE_TICKS  540      // 4.5 s on the ground before it starts to go —
                               // long enough to walk past your own work, short
                               // enough that a 20-bot brawl is not a graveyard
#define CORPSE_DISS    18      // ...and 0.15 s to be GONE. This has now been
                               // both slower (84, with a travelling front) and in
                               // between (54): a body that takes a fifth of a second to
                               // leave is an event, one that takes most of a second is
                               // a cutscene you cannot skip, and in a firefight you are
                               // looking somewhere else by then anyway.
#define CORPSE_EMBERS  36      // fragments in the ONE-SHOT burst. Not a rate:
                               // the whole body leaves in a single spray on the first
                               // dissolve tick, which is the "puff".
typedef struct {
  anim_t   anim;    // the skeleton, frozen in shape but not in PLACE
  rag_t    rag;     // ...and still integrating on the SHARED integrator, so a
                    // body handed over mid-flight goes on flying, tumbling,
                    // bouncing and slamming exactly as it did as an entity
  gunanim_t gs;     // the weapon's own state, so a body killed mid-reload does
                    // not snap its trigger and bolt closed on handover
  v3       pos, prev_pos;
  float    yaw;
  float    slack, prev_slack;  // the limb-collapse blend, ramped HERE as well as
                    // on the entity: a bot hands over a second after it died
                    // and is already fully slack, but the player's body enters
                    // the pool on the killing tick and would otherwise snap
                    // straight from a firing stance into a sprawl in one frame
  uint32_t seed;    // its own dissolve pattern
  int      born;    // the tick it was handed over. bot_tick steps the ENTITY
                    // and then hands the already-stepped body to the pool, and
                    // `vfx_tick` runs later in the SAME player_tick — so without this
                    // the handover tick integrated gravity, drag, friction and the
                    // topple twice (measured: 2.1x the tilt step and 2.3x the position
                    // step on that one tick), which is a one-frame pop at exactly the
                    // seam this whole change exists to remove.
  int      wpn, life, used;
  int      owner;   // entity id the body was (0 = player, 1.. = bots): the
                    // `rag`/`ragx` proofs follow ONE body across the handover,
                    // and without ownership any other kill landing in their
                    // observation window silently re-targeted the follow
} corpse_t;
static corpse_t g_corpses_all[SRV_LOBBIES][MAX_CORPSES];
#define g_corpses (g_corpses_all[g_lobby])

// Hand a body over to the pool. Called from the sim at the respawn, exactly like
// ev_push: presentation state written by the tick, never read back.
static void corpse_add(const anim_t *A, v3 pos, v3 prev_pos, rag_t rag,
                       float yaw, float slack, float prev_slack, gunanim_t gs,
                       int wpn, int owner) {
  int slot = -1, oldest = 1 << 30;
  for (int i = 0; i < MAX_CORPSES; i++) {
    if (!g_corpses[i].used) { slot = i; break; }
    if (g_corpses[i].life < oldest) { oldest = g_corpses[i].life; slot = i; }
  }
  g_corpses[slot] = (corpse_t){
    .anim = *A,
    .rag = rag,
    .gs = gs,
    .pos = pos,
    .prev_pos = prev_pos,
    .yaw = yaw,
    .slack = slack,
    .prev_slack = prev_slack,
    .seed = fx_hash((uint32_t)g_tick * 2654435761u +
                    (uint32_t)slot * 40503u),
    .born = (int)g_tick,
    .wpn = wpn,
    .life = CORPSE_TICKS + CORPSE_DISS,
    .used = 1,
    .owner = owner,
  };
}

// Live particles at the worst tick, and how many of them were in the RESERVED
// region below PARTS_SKID_LO. The body burst is nearly three times the stone
// one now, so the reserve's headroom stopped being something to reason about
// and became something to print: `budget` reports both.
static int g_part_peak, g_part_res_peak;

static float skid_h(uint32_t *h) {
  *h = fx_hash(*h);
  return (float)(*h >> 8) * (1.0f / 16777216.0f);
}
static float skid_r(uint32_t *h, float lo, float hi) {
  return lo + (hi - lo) * skid_h(h);
}

// Ground sparks off a DEAD body that is still moving.
#define RAG_SPARK_V   2.20f    // m/s of grind below which concrete just scuffs
static void vfx_rag_sparks(const rag_t *r, v3 pos) {
  if (!r->grounded) return;
  float sp = sqrtf(r->vel.x * r->vel.x + r->vel.z * r->vel.z);
  if (sp < RAG_SPARK_V) return;
  if ((g_tick & 1) == 0) return;   // every other tick: one sliding body costs
                                   // the scenery region what one sliding PLAYER does,
                                   // and chatter reads better than a continuous hose
  float hot = f_clamp((sp - RAG_SPARK_V) / 4.0f, 0.0f, 1.0f);
  int n = 1 + (int)(hot * 2.0f);
  uint32_t h = (uint32_t)g_tick * 2246822519u ^ (uint32_t)(int)(pos.x * 977.0f);
  float inv = 1.0f / sp;
  for (int e = 0; e < n; e++) {
    int slot = -1;
    for (int i = PARTS_SKID_LO; i < MAX_PARTICLES; i++)
      if (g_parts[i].life <= 0) { slot = i; break; }
    if (slot < 0) return;
    // Thrown BACKWARD along the drag and splayed up: the direction a grain
    // torn off a moving contact actually leaves in, same as the boot skid.
    float back = sp * skid_r(&h, 0.18f, 0.48f);
    v3 v = {{-r->vel.x * inv * back + skid_r(&h, -1.1f, 1.1f),
             skid_r(&h, 1.0f, 2.9f),
             -r->vel.z * inv * back + skid_r(&h, -1.1f, 1.1f)}};
    float y = pos.y + 0.05f;
    g_parts[slot] = (particle_t){
        .pos = {{pos.x + skid_r(&h, -0.25f, 0.25f), y,
                 pos.z + skid_r(&h, -0.25f, 0.25f)}},
        .vel = v, .y0 = pos.y, .sz = 0.7f + 0.5f * hot,
        .life = 18, .life0 = 18, .kind = 2};
  }
}

static void vfx_skid(const anim_t *A, int ent) {
  short *clk = &g_skid_t[ent];
  float sp = sqrtf(A->vel_s.x * A->vel_s.x + A->vel_s.z * A->vel_s.z);
  // A body in the air scrapes nothing, and only a real skid marks a floor.
  if (A->slide_s < 0.55f || A->air_s > 0.35f || sp < 3.0f) { *clk = -1; return; }
  float hot = f_clamp((sp - SKID_V_LO) / (SKID_V_HI - SKID_V_LO), 0.0f, 1.0f);

  // --- boot: dust ----------------------------------------------------------
  // The old emitter, unchanged in rate, velocity, lifetime and level: a boot skidding
  // raises dust and a knuckle on concrete throws sparks, and only the second one needed
  // rewriting.
  v3 ank = A->pose.ankle[0].y < A->pose.ankle[1].y ? A->pose.ankle[0]
                                                  : A->pose.ankle[1];
  if ((g_tick % 3) == 1)
    for (int i = 0; i < MAX_PARTICLES; i++)
      if (g_parts[i].life <= 0) {
        v3 v = {{-A->vel_s.x * 0.22f + rng_rangef(&g_rng_vfx, -0.5f, 0.5f),
                 rng_rangef(&g_rng_vfx, 0.5f, 1.4f),
                 -A->vel_s.z * 0.22f + rng_rangef(&g_rng_vfx, -0.5f, 0.5f)}};
        float y = fmaxf(ank.y - 0.04f, 0.02f);   // just under the sole
        g_parts[i] = (particle_t){.pos = {{ank.x, y, ank.z}}, .vel = v,
                                  .y0 = y, .sz = 1.0f,
                                  .life = PARTICLE_TICKS + 6,
                                  .life0 = PARTICLE_TICKS + 6, .kind = 1};
        break;
      }

  // --- knuckle: sparks -----------------------------------------------------
  v3 fist = A->pose.wri[0];                     // the dragging brace glove
  // The contact gate is a SCHMITT TRIGGER, the same idiom as the pad's 0.30/0.20 analog
  // trigger and for the same reason: `strike` fires on every 0->1 edge, so a fist that
  // chatters across one threshold — which it does at the end of every slide (measured:
  // fist clearance -0.015 m at t=265, +0.018 at t=271) and over any step in the floor —
  // would slap the ground twice.
  if (fist.y > ank.y + (*clk < 0 ? SKID_CONTACT_DY : SKID_CONTACT_DY + 0.05f)) {
    *clk = -1;
    return;
  }
  int strike = *clk < 0;                        // the tick the knuckle lands
  *clk = (short)(*clk < 0 || *clk > 30000 ? 0 : *clk + 1);
  // Rate is CONTINUOUS, not a period/burst pair.
  float rate = hot * (0.30f + 0.70f * hot);     // 0.30 at a crawl, 1.0 flat out
  int attempts = SKID_ATTEMPTS;
  if (strike) { rate = 1.0f; attempts = SKID_ATTEMPTS * 2; }   // the slap
  float sy = fmaxf(fist.y - 0.03f, 0.015f);     // the struck plane
  v3 fwd = {{A->vel_s.x / sp, 0.0f, A->vel_s.z / sp}};    // the fan's axis
  v3 lat = {{-fwd.z, 0.0f, fwd.x}};                       // across it
  for (int n = 0; n < attempts; n++) {
    uint32_t h = (uint32_t)g_tick * 2654435761u + (uint32_t)ent * 2246822519u +
                 (uint32_t)n * 3266489917u;
    if (skid_h(&h) > rate) continue;
    int slot = -1;
    for (int i = PARTS_SKID_LO; i < MAX_PARTICLES; i++)
      if (g_parts[i].life <= 0) { slot = i; break; }
    if (slot < 0) break;   // pool pressure thins the spray; it never breaks it
    // A HERO is bigger, brighter and three times as long-lived, so a handful of arcs
    // carry across the frame while the cheap majority stays a tight bright spray at the
    // contact.
    int hero = skid_h(&h) < 0.14f;
    // ALONG THE TRAVEL: what the fragment KEPT of the skid.
    float keep = hero ? skid_r(&h, -0.10f, 0.82f) : skid_r(&h, -0.06f, 0.50f);
    float side = skid_r(&h, -0.115f, 0.115f) * (strike ? 1.6f : 1.0f);
    // LIFT is the primary spread, not an afterthought.
    float u    = skid_h(&h);
    float rise = (0.85f + 4.60f * u * u) * (0.34f + 0.66f * hot) *
                 (strike ? 1.25f : 1.0f);
    int   lf  = hero ? 30 + (int)skid_r(&h, 0.0f, 16.0f)
                     : 10 + (int)skid_r(&h, 0.0f, 11.0f);
    // sz is size AND heat AND streak length in one number, so folding `hot`
    // into it is what makes a dying skid throw a few small dull embers instead
    // of the same spark less often.
    float sz  = (hero ? skid_r(&h, 1.50f, 2.10f) : skid_r(&h, 0.70f, 1.05f)) *
                (0.62f + 0.38f * hot) * (strike ? 1.22f : 1.0f);
    v3 v = v3_add(v3_scale(fwd, sp * keep), v3_scale(lat, sp * side));
    v.y = fminf(rise, SKID_VY_MAX);
    // Scatter the ORIGIN a few centimetres too.
    v3 o = v3_add(v3_add((v3){{fist.x, sy, fist.z}},
                         v3_scale(lat, skid_r(&h, -0.032f, 0.032f))),
                  v3_scale(fwd, skid_r(&h, -0.020f, 0.020f)));
    g_parts[slot] = (particle_t){.pos = o, .vel = v,
                                 .y0 = sy, .sz = sz,
                                 .life = lf, .life0 = lf, .kind = 2};
  }
  // The flare anchors the spray to a POINT. Without it the sparks read as spawning in
  // mid-air a hand's width off the body — a fragment is gone from the contact within
  // two ticks and nothing marks where it came from.
  for (int i = PARTS_SKID_LO; i < MAX_PARTICLES; i++)
    if (g_parts[i].life <= 0) {
      float k = strike ? 1.55f : 1.0f;
      g_parts[i] = (particle_t){
          .pos = {{fist.x, sy + 0.022f, fist.z}},
          .vel = {{0.0f, 0.0f, 0.0f}},                   // unused for kind 3
          .y0 = sy, .sz = (0.032f + 0.030f * hot) * k,   // arm half-length
          .life = 4, .life0 = 4, .kind = 3};
      break;
    }
}

// The particle census. Sampled TWICE per tick — see vfx_tick's own comment for
// why the top sample is not redundant with the bottom one.
static void part_census(void) {
  int live = 0, res = 0;
  for (int i = 0; i < MAX_PARTICLES; i++)
    if (g_parts[i].life > 0) { live++; if (i < PARTS_SKID_LO) res++; }
  if (live > g_part_peak) g_part_peak = live;
  if (res > g_part_res_peak) g_part_res_peak = res;
}

static void vfx_tick(void) {
  // TWO samples per tick, both feeding the same maxima.
  part_census();
  // The player's emitter is gated on ALIVE for the same reason a bot's is, and it is
  // not symmetry for its own sake: player_tick stops calling anim_tick the moment the
  // player dies (it only copies prev_pose), so g_player_anim FREEZES with whatever
  // slide_s and vel_s it held on the death tick.
  for (int h = 0; h < MAX_HUMANS; h++)   // every human skids, own id
    if (g_humans_on[h] && g_tick - g_player_anim[h].live_t <= 1)
      vfx_skid(&g_player_anim[h], ent_of_human(h));
  for (int b = 0; b < g_match.nbots; b++)
    if (g_bots[b].active && g_bots[b].alive) vfx_skid(&g_bots[b].anim, b + 1);
  for (int i = 0; i < MAX_TRACERS; i++)
    if (g_tracers[i].life > 0) g_tracers[i].life--;
  for (int i = 0; i < MAX_FLASHES; i++)
    if (g_flashes[i].life > 0) g_flashes[i].life--;
  for (int i = 0; i < MAX_PARTICLES; i++)
    if (g_parts[i].life > 0) {
      particle_t *q = &g_parts[i];
      if (q->kind == 3) { q->life--; continue; }   // flare: welded to the contact
      if (q->kind == 1) {          // dust hangs — drag beats gravity on a mote
        q->vel.y -= GRAVITY * 0.10f * TICK_DT;
        q->vel = v3_scale(q->vel, 0.94f);
      } else {                     // sparks arc, and hard
        q->vel.y -= GRAVITY * 1.2f * TICK_DT;
        if (q->kind == 2) q->vel = v3_scale(q->vel, 0.982f);  // air on an ember
        else if (q->kind == 5) q->vel = v3_scale(q->vel, KILLBURST_DRAG);
      }
      q->pos = v3_add(q->pos, v3_scale(q->vel, TICK_DT));
      // SKIP. A fragment that reaches the floor still moving bounces, and that second
      // lower arc is most of what separates "sparks" from "orange confetti".
      if (q->kind == 6 && q->vel.y < 0.0f) {
        float fl = rag_support(q->pos) + 0.012f;
        if (q->pos.y < fl) {
          q->pos.y = fl;
          q->vel.y *= -0.34f;
          q->vel.x *= 0.55f;
          q->vel.z *= 0.55f;
          if (q->vel.y < 0.4f) q->vel = (v3){{0, 0, 0}};   // settled: stop tumbling
        }
      }
      if (q->kind == 2 && q->pos.y < q->y0 && q->vel.y < 0.0f) {
        q->pos.y = q->y0;
        q->vel.y *= -0.38f;
        q->vel.x *= 0.72f;
        q->vel.z *= 0.72f;
      }
      q->life--;
    }
  // Corpses: count down, and throw a few embers while they come apart — the same kind-4
  // body spark the hits use, so the dissolve is made of the material the body was
  // already shedding.
  {
    int used = 0, oldest = -1, low = 1 << 30;
    for (int c = 0; c < MAX_CORPSES; c++) {
      if (!g_corpses[c].used) continue;
      used++;
      if (g_corpses[c].life < low) { low = g_corpses[c].life; oldest = c; }
    }
    if (used == MAX_CORPSES && oldest >= 0 && g_corpses[oldest].life > CORPSE_DISS)
      g_corpses[oldest].life = CORPSE_DISS;
  }
  for (int c = 0; c < MAX_CORPSES; c++) {
    corpse_t *cp = &g_corpses[c];
    if (!cp->used) continue;
    if (cp->born == (int)g_tick) continue;   // handed over this tick — already
                                             // stepped by whoever owned it
    if (--cp->life <= 0) { cp->used = 0; continue; }
    // FINISH THE FLIGHT THE ENTITY STARTED, on the same integrator it started on.
    cp->prev_pos = cp->pos;
    cp->prev_slack = cp->slack;
    rag_advance(&cp->rag, &cp->pos, cp->prev_pos, &cp->slack, &cp->anim);
    if (cp->life != CORPSE_DISS) continue;   // ONE tick, ONE spray
    // THE PUFF — the whole body leaves in a single spray on the first dissolve tick.
    uint32_t h = (uint32_t)g_tick * 2654435761u + (uint32_t)c * 40503u;
    v3 mid = v3_add(cp->pos, (v3){{0.0f, 0.22f, 0.0f}});
    for (int e = 0; e < CORPSE_EMBERS; e++) {
      int slot = -1;
      for (int i = PARTS_SKID_LO; i < MAX_PARTICLES; i++)
        if (g_parts[i].life <= 0) { slot = i; break; }
      if (slot < 0) break;
      // A point in the lying body's box: wide and flat, which is the shape a
      // body on the ground actually has.
      v3 pj = {{mid.x + skid_r(&h, -0.55f, 0.55f),
                cp->pos.y + skid_r(&h, 0.04f, 0.44f),
                mid.z + skid_r(&h, -0.55f, 0.55f)}};
      v3 d = v3_sub(pj, mid);
      float dl = sqrtf(v3_dot(d, d));
      d = dl > 0.05f ? v3_scale(d, 1.0f / dl)
                     : (v3){{skid_r(&h, -1.0f, 1.0f), 0.4f,
                             skid_r(&h, -1.0f, 1.0f)}};
      // SIZED AGAINST THE BODY, NOT AGAINST TASTE: 16 ticks is 0.13 s, so a spray that
      // should be about as wide as the thing that vanished needs ~9 m/s.
      float sp = skid_r(&h, 6.0f, 13.0f);
      v3 v = {{d.x * sp + skid_r(&h, -1.4f, 1.4f),
               d.y * sp * 0.45f + skid_r(&h, 0.9f, 2.4f),
               d.z * sp + skid_r(&h, -1.4f, 1.4f)}};
      g_parts[slot] = (particle_t){.pos = pj, .vel = v, .y0 = cp->pos.y,
                                   .sz = skid_r(&h, 1.10f, 1.90f),
                                   .life = 16, .life0 = 16, .kind = 2};
    }
  }
  if (g_muzzle_t > 0) g_muzzle_t--;
  if (g_hitmark_arm > 0 && --g_hitmark_arm == 0) {
    g_hitmark_kind = g_hitmark_pend;
    g_hitmark_t = g_hitmark_kind >= HM_KILL ? KILLMARK_TICKS : HITMARK_TICKS;
    // A hit KICKS the crosshair open and it springs back — the reaction lands on the
    // one element the player is already staring at, and it costs one float.
    g_ch_kick += g_hitmark_kind >= HM_KILL ? 3.0f : 1.2f;
  }
  if (g_hitmark_t > 0) g_hitmark_t--;
  g_ch_kick *= 0.87f;   // ~8 ticks to a third: a snap, not a breath
  if (g_dmg_t > 0) g_dmg_t--;
  for (int i = 0; i < KILLFEED_N; i++)
    if (g_feed[i].life > 0) g_feed[i].life--;
  if (g_score_pop > 0) g_score_pop--;
  for (int i = 0; i < DMG_SLOTS; i++)
    if (g_dmg[i].life > 0) g_dmg[i].life--;
  // The banner DISPLAY fades at STREAK_TICKS but the COUNT lives for the whole
  // MULTI_WIN (the EV_KILL window check above resets it): zeroing the count
  // with the display made a kill 1.25-3 s after the second one a TRIPLE to the
  // audio's pitch ladder and a fresh single to the banner.
  if (g_streak_t > 0) g_streak_t--;
  // The END sample — this function's own spawns; see the TOP sample's comment.
  part_census();
}

// A match restart must not carry stale feedback into the fresh match — and
// the arena is regenerated, so world-anchored VFX (impact marks, tracers,
// sparks) would float in mid-air on the new map.
static void vfx_match_reset(void) {
  memset(g_feed, 0, sizeof g_feed);
  memset(g_tracers, 0, sizeof g_tracers);
  memset(g_flashes, 0, sizeof g_flashes);
  g_muzzle_fresh = 0;
  memset(g_parts, 0, sizeof g_parts);
  memset(g_skid_t, 0xFF, sizeof g_skid_t);   // 0xFFFF = -1 = nobody is scraping
  memset(g_marks, 0, sizeof g_marks);
  memset(g_corpses, 0, sizeof g_corpses);
  memset(g_player_rag, 0, sizeof g_player_rag);  // no leak is constructible
  memset(g_player_corpse_q, 0, sizeof g_player_corpse_q);
                               // (the handover runs at the end of the same world tick
                               // that can set it) but it is cross-tick state and this
                               // is where the rest of the pool's state is owned
  g_mark_head = 0;
  g_dmg_t = 0;
  memset(g_dmg, 0, sizeof g_dmg);
  // ALL FOUR halves of the marker, not just the visible one: a marker still
  // waiting out its per-weapon arming delay when the match restarts would arm
  // itself into the new one, and the crosshair kick would ride across with it.
  g_hitmark_t = g_hitmark_arm = g_hitmark_pend = g_hitmark_kind = 0;
  g_ch_kick = 0.0f;
  g_multi_n = 0;               // a new match does not start on a double kill
  g_hitstop = 0;
  g_score_pop = g_streak_n = g_streak_t = 0;
  g_streak_last_t = -100000;
}

// --- scene batch: CPU-built oriented boxes, world-shader vertex format -----

// 21 skinned figures (20 bots + the player's body) plus the 64-slot impact-mark ring
// have to fit, and up to six corpses on top of them: bodies outlive the entities that
// were them.
#define SCENE_MAX_VERTS 524288
static float g_scene[SCENE_MAX_VERTS * VERT_FLOATS];
static long g_scene_drops;
static int   g_scene_floats;
static int   g_scene_want;   // floats ASKED for this frame, including any dropped

static int g_scene_peak;   // most verts any one frame has needed
static int g_scene_dirty;  // buffer changed since it was last handed to the GL

// Kept OUT of scene_push: this is the once-per-session diagnostic, and an fprintf plus
// a string literal in the body is what pushed the hot function over gcc's inline budget
// at its ~50 call sites.
static void scene_drop_note(void) {
  if (!g_scene_drops++)
    fprintf(stderr, "scene: SCENE_MAX_VERTS exhausted — geometry dropped\n");
}

static inline void scene_push(v3 p, v3 n, v3 c) {
  // The peak tracks DEMAND, not what fitted.
  g_scene_want += VERT_FLOATS;
  if (g_scene_want > g_scene_peak) g_scene_peak = g_scene_want;
  if (g_scene_floats + VERT_FLOATS > SCENE_MAX_VERTS * VERT_FLOATS) {
    scene_drop_note();
    return;
  }
  float *v = g_scene + g_scene_floats;
  v[0] = p.x; v[1] = p.y; v[2] = p.z;
  v[3] = n.x; v[4] = n.y; v[5] = n.z;
  v[6] = c.x; v[7] = c.y; v[8] = c.z;
  v[9] = g_mat_rough; v[10] = g_mat_metal; v[11] = ao_at(p.y) * g_ao_mul;
  g_scene_floats += VERT_FLOATS;
}

// Every refill goes through here so the upload flag cannot be forgotten by a
// new builder: scene_draw re-sends the buffer only when this has been called
// since the last hand-over.
static inline void scene_reset(void);

// --- scene chunks: what a pass is allowed to SKIP ---------------------------
// One buffer feeds four passes with three frusta — the shadow map (ortho, whole arena),
// the main view, and the sniper's scope at a NINE degree lens.
#define SCENE_MAX_CHUNKS (MAX_ENTS + MAX_CORPSES + 4)
#define SCENE_CHUNK_R    2.0f
typedef struct { int f0, f1; v3 c; float r; } schunk_t;
static schunk_t g_schunk[SCENE_MAX_CHUNKS];
static int g_nschunk, g_schunk_open = -1;

static void scene_chunk_begin(v3 c, float r) {
  if (g_nschunk >= SCENE_MAX_CHUNKS) return;   // tail geometry: never culled
  g_schunk_open = g_nschunk++;
  g_schunk[g_schunk_open] = (schunk_t){g_scene_floats, g_scene_floats, c, r};
}
static void scene_chunk_end(void) {
  if (g_schunk_open >= 0) g_schunk[g_schunk_open].f1 = g_scene_floats;
  g_schunk_open = -1;
}

// Six clip planes out of a view-projection (Gribb/Hartmann), in the column-major
// layout m4_mul builds: row r is m[0*4+r], m[1*4+r], m[2*4+r], m[3*4+r].
typedef struct { float p[6][4]; } frustum_t;
static frustum_t frustum_of(const mat4 *m) {
  frustum_t f;
  for (int i = 0; i < 6; i++) {
    int a = i >> 1, sgn = (i & 1) ? -1 : 1;
    for (int c = 0; c < 4; c++)
      f.p[i][c] = m->m[c * 4 + 3] + (float)sgn * m->m[c * 4 + a];
    float l = sqrtf(f.p[i][0] * f.p[i][0] + f.p[i][1] * f.p[i][1] +
                    f.p[i][2] * f.p[i][2]);
    if (l > 1e-9f) for (int c = 0; c < 4; c++) f.p[i][c] /= l;
  }
  return f;
}
static int frustum_sees(const frustum_t *f, v3 c, float r) {
  for (int i = 0; i < 6; i++)
    if (f->p[i][0] * c.x + f->p[i][1] * c.y + f->p[i][2] * c.z + f->p[i][3] < -r)
      return 0;
  return 1;
}
// The same test for a swept sphere.
static int frustum_sees_cap(const frustum_t *f, v3 a, v3 b, float r) {
  for (int i = 0; i < 6; i++) {
    float da = f->p[i][0] * a.x + f->p[i][1] * a.y + f->p[i][2] * a.z + f->p[i][3];
    float db = f->p[i][0] * b.x + f->p[i][1] * b.y + f->p[i][2] * b.z + f->p[i][3];
    if (da < -r && db < -r) return 0;
  }
  return 1;
}

// Where this body's shadow can possibly land.
#define SHADOW_CAST_MAX 42.0f
static v3 shadow_foot(v3 c) {
  // THE CAP HAS TO FOLLOW THE SUN DOWN. At 38 degrees a body's shadow is under
  // three metres long and a 24 m ceiling was slack; at 8 it is twenty, and a cap
  // shorter than the real shadow culls a figure whose shadow is still on screen.
  float t = (c.y + 0.5f) / fmaxf(g_sun.dir.y, 0.05f);
  if (t < 0.0f) t = 0.0f;
  if (t > SHADOW_CAST_MAX) t = SHADOW_CAST_MAX;
  return v3_add(c, v3_scale(v3_scale(g_sun.dir, -1.0f), t));
}

// The camera frustum of the frame being built, published for scene_build.
static frustum_t g_cam_frustum;
static int       g_cam_cull;

// Floats that CAST a shadow: everything scene_build emits before the local
// first-person body, which is appended last and casts nothing (the local
// player has never cast a shadow in first person, and a headless, armless,
// gunless shadow would be worse than none). -1 = the whole buffer, which is
// what every other filler of this buffer (vm pass, FX, probes) wants.
static int g_scene_cast_floats;

static inline void scene_reset(void) {
  g_scene_floats = 0; g_scene_want = 0; g_scene_dirty = 1;
  g_nschunk = 0; g_schunk_open = -1;
  g_scene_cast_floats = -1;
}

static void scene_quad(v3 a, v3 b, v3 c, v3 d, v3 n, v3 col) {
  scene_push(a, n, col); scene_push(b, n, col); scene_push(c, n, col);
  scene_push(a, n, col); scene_push(c, n, col); scene_push(d, n, col);
}

// --- figure measurement probe -----------------------------------------------
// The figure's proportions are the thing that decides whether it reads as a man or as a
// stack of tubes, and they were authored by eye against a FRONT view — which is why
// every width was roughly right and every length and depth was not.
#define PROBE_MAX 128
typedef struct { const char *name; int v0, v1; } probe_t;
static probe_t g_probe[PROBE_MAX];
static int g_nprobe, g_probe_on;

static void probe(const char *name) {
  if (!g_probe_on) return;
  if (g_nprobe) g_probe[g_nprobe - 1].v1 = g_scene_floats;
  if (!name || g_nprobe >= PROBE_MAX) return;
  g_probe[g_nprobe++] = (probe_t){name, g_scene_floats, g_scene_floats};
}

// Oriented box: center + three half-extent vectors (need not be unit; their directions
// give the face normals).
static int fig_diss_cut(v3 p);   // defined with the figure; see the dissolve

// --- broken edges ----------------------------------------------------------
// Every machined or moulded part has its arrises broken, so a renderer emitting perfect
// 90-degree corners models the one thing that is never there.
static float g_cham;
// ...and a SECOND flag, because dec_stone borrows the same primitive and a burnished
// arris on a tumbled rock is worse than a sharp one. `g_cham` says an edge is broken;
// this says the edge was broken by a MACHINE and has been handled since.
static int g_cham_polish;

// THE HERO TIER: the jewellery a weapon has at 30 cm and cannot show at 25 m. One flag,
// ONE writer (vm_build, beside g_cham), and it is an LOD decision of exactly the same
// kind as g_fig_k rather than a second weapon model — every part behind it is built
// from the same stations as the parts around it, so nothing can drift.
//
// It EARNS its cost twice over, because the third-person weapon had no tier at all: a
// figure is 7260 tris at k=16 and the gun was 1384 of them, built from faceted boxes
// that the profile tier never touched, so at 25 m it was 23 % of a body forty pixels
// tall. The rail teeth are the proof — they were made 2.6 mm proud ONLY to keep their
// top face out of the depth buffer's noise at 28 m, i.e. a part deformed to survive a
// range at which it cannot be read. They are hero-only now, and the bar they sit on
// carries the rail's whole silhouette past that range.
static int g_gun_hero;

// One quad and one triangle with the winding REPAIRED against a stated outward normal.
static void scene_poly4(v3 a, v3 b, v3 c, v3 d, v3 n, v3 col) {
  if (v3_dot(v3_cross(v3_sub(b, a), v3_sub(c, a)), n) < 0.0f)
    scene_quad(d, c, b, a, n, col);
  else
    scene_quad(a, b, c, d, n, col);
}
static void scene_poly3(v3 a, v3 b, v3 c, v3 n, v3 col) {
  if (v3_dot(v3_cross(v3_sub(b, a), v3_sub(c, a)), n) < 0.0f) {
    scene_push(c, n, col); scene_push(b, n, col); scene_push(a, n, col);
  } else {
    scene_push(a, n, col); scene_push(b, n, col); scene_push(c, n, col);
  }
}

// The arris's own surface, and the ONE copy of it: scene_box_cham and fig_cap both
// emit bevel bands and must not disagree about what a broken edge is made of. Returns
// the band's colour and writes its roughness through `rough`; restore the caller's own
// value after the band. `g_cham_polish` gates it, so decor keeps a plain sharp edge.
static v3 cham_arris(v3 col, float *rough) {
  if (!g_cham_polish) return v3_scale(col, 1.16f);
  *rough *= MAT_ARRIS_R;
  // The bevel keeps its part's TOOL DIRECTION (the sign of the metalness) untouched: an
  // arris is a cut on the same part by the same machine, and flipping the lobe there
  // is the defect mat_lathe's own note is about.
  if (fabsf(g_mat_metal) > 0.5f) {
    v3 bare = MAT_BARE_C;
    return v3_add(v3_scale(col, 1.0f - MAT_ARRIS_MIX),
                  v3_scale(bare, MAT_ARRIS_MIX));
  }
  return v3_scale(col, 1.16f);
}

// The chamfered box: six inset axis faces, twelve edge bevels, eight corner triangles.
static void scene_box_cham(v3 c, v3 nx, v3 ny, v3 nz,
                           float ax, float ay, float az, float t, v3 col) {
  // A chamfer may never eat more than a third of the smallest half-extent: the
  // weapon's smallest controls are 2 mm boxes, and a 0.7 mm bevel on a 2 mm
  // half-extent is not a broken edge, it is a different part.
  float mn = ax < ay ? (ax < az ? ax : az) : (ay < az ? ay : az);
  if (t > mn * 0.34f) t = mn * 0.34f;
  if (t <= 1e-6f) return;
  v3 px[8], py[8], pz[8];
  for (int i = 0; i < 8; i++) {
    float sx = i & 1 ? 1.0f : -1.0f, sy = i & 2 ? 1.0f : -1.0f,
          sz = i & 4 ? 1.0f : -1.0f;
    v3 ux = v3_scale(nx, sx), uy = v3_scale(ny, sy), uz = v3_scale(nz, sz);
    px[i] = v3_add(c, v3_add(v3_scale(ux, ax),
            v3_add(v3_scale(uy, ay - t), v3_scale(uz, az - t))));
    py[i] = v3_add(c, v3_add(v3_scale(ux, ax - t),
            v3_add(v3_scale(uy, ay), v3_scale(uz, az - t))));
    pz[i] = v3_add(c, v3_add(v3_scale(ux, ax - t),
            v3_add(v3_scale(uy, ay - t), v3_scale(uz, az))));
  }
  #define CIDX(I, J, K) (((I) > 0) | (((J) > 0) << 1) | (((K) > 0) << 2))
  // The bevels and the corners are the surfaces that WEAR — see MAT_ARRIS_R. The six
  // axis faces go out FIRST, under the part's own surface state, and everything after
  // this line is the arris.
  for (int s = -1; s <= 1; s += 2) {
    float f = (float)s;
    scene_poly4(px[CIDX(s, -1, -1)], px[CIDX(s, 1, -1)],
                px[CIDX(s, 1, 1)],   px[CIDX(s, -1, 1)], v3_scale(nx, f), col);
    scene_poly4(py[CIDX(-1, s, -1)], py[CIDX(1, s, -1)],
                py[CIDX(1, s, 1)],   py[CIDX(-1, s, 1)], v3_scale(ny, f), col);
    scene_poly4(pz[CIDX(-1, -1, s)], pz[CIDX(1, -1, s)],
                pz[CIDX(1, 1, s)],   pz[CIDX(-1, 1, s)], v3_scale(nz, f), col);
  }
  float rsave = g_mat_rough;
  v3 ecol = cham_arris(col, &g_mat_rough);
  for (int a = -1; a <= 1; a += 2)
    for (int b = -1; b <= 1; b += 2) {
      float fa = (float)a, fb = (float)b;
      v3 nab = v3_norm(v3_add(v3_scale(nx, fa), v3_scale(ny, fb)));
      scene_poly4(px[CIDX(a, b, -1)], px[CIDX(a, b, 1)],
                  py[CIDX(a, b, 1)],  py[CIDX(a, b, -1)], nab, ecol);
      v3 nac = v3_norm(v3_add(v3_scale(nx, fa), v3_scale(nz, fb)));
      scene_poly4(px[CIDX(a, -1, b)], px[CIDX(a, 1, b)],
                  pz[CIDX(a, 1, b)],  pz[CIDX(a, -1, b)], nac, ecol);
      v3 nbc = v3_norm(v3_add(v3_scale(ny, fa), v3_scale(nz, fb)));
      scene_poly4(py[CIDX(-1, a, b)], py[CIDX(1, a, b)],
                  pz[CIDX(1, a, b)],  pz[CIDX(-1, a, b)], nbc, ecol);
    }
  for (int i = 0; i < 8; i++) {
    float sx = i & 1 ? 1.0f : -1.0f, sy = i & 2 ? 1.0f : -1.0f,
          sz = i & 4 ? 1.0f : -1.0f;
    v3 n = v3_norm(v3_add(v3_scale(nx, sx),
           v3_add(v3_scale(ny, sy), v3_scale(nz, sz))));
    scene_poly3(px[i], py[i], pz[i], n, ecol);
  }
  g_mat_rough = rsave;
  #undef CIDX
}

static void scene_box(v3 c, v3 ex, v3 ey, v3 ez, v3 col) {
  // A dissolving figure loses its BOXES too.
  if (fig_diss_cut(c)) return;
  if (v3_dot(v3_cross(ex, ey), ez) < 0.0f) ez = v3_scale(ez, -1.0f);
  v3 nx = v3_norm(ex), ny = v3_norm(ey), nz = v3_norm(ez);
  if (g_cham > 0.0f) {
    scene_box_cham(c, nx, ny, nz, v3_len(ex), v3_len(ey), v3_len(ez),
                   g_cham, col);
    return;
  }
  v3 p[8];
  for (int i = 0; i < 8; i++) {
    v3 q = c;
    q = v3_add(q, v3_scale(ex, i & 1 ? 1.0f : -1.0f));
    q = v3_add(q, v3_scale(ey, i & 2 ? 1.0f : -1.0f));
    q = v3_add(q, v3_scale(ez, i & 4 ? 1.0f : -1.0f));
    p[i] = q;
  }
  scene_quad(p[1], p[3], p[7], p[5], nx, col);                       // +x
  scene_quad(p[4], p[6], p[2], p[0], v3_scale(nx, -1.0f), col);      // -x
  scene_quad(p[2], p[6], p[7], p[3], ny, col);                       // +y
  scene_quad(p[4], p[0], p[1], p[5], v3_scale(ny, -1.0f), col);      // -y
  scene_quad(p[4], p[5], p[7], p[6], nz, col);                       // +z
  scene_quad(p[0], p[2], p[3], p[1], v3_scale(nz, -1.0f), col);      // -z
}
