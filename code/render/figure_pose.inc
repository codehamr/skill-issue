// =============================================================================
// render/figure_pose.inc  —  fig_death_pose, fig_torso, kit attachments.
// Proof: figcheck, figv, ragx. One TU: included once by game.c, in
// definition order — moving a definition out is a semantic change.
// Contract: everything mounted on the carrier follows the carrier's own
// twist (offsets AND node roll per part), or it shears off the plate
// exactly when the figure is at the ready, which is always.
// =============================================================================
// (death_t is declared with pose_t, above the sim: the corpse pool hands one
// over from bot_tick and the renderer consumes it, so it cannot live down here.)

// A FIGURE BUILDER IS A SOURCE DECOMPOSITION AND MUST NOT BE ANYTHING ELSE, and under
// -O3 -ffast-math that is not free.
#define FIG_INLINE static inline __attribute__((always_inline))

// Phase 1 of the corpse solve: the staggered slackening — legs ease at dth->slack
// x2.2, arms+head+the carried gun at x4.5. Simultaneous reads as collapsing,
// staggered (grip and gaze die first, the frame follows) reads as dying. Mutates P
// in place; hands back the leg-side slack factor `c` and the local trunk frame
// (pr/pf/up) fig_death_drape needs to keep working in the same frame.
FIG_INLINE void fig_death_slacken(pose_t *P, const death_t *dth,
                                  float *out_c, v3 *out_pr, v3 *out_pf,
                                  v3 *out_up) {
  float c = f_clamp(dth->slack * 2.2f, 0.0f, 1.0f);        // legs
  float ca = f_clamp(dth->slack * 4.5f, 0.0f, 1.0f);        // arms + head:
  // staggered slackening — simultaneous reads as collapsing, staggered
  // (grip and gaze die first, the frame follows) reads as dying.
  v3 up = {{0, 1, 0}};
  v3 sp = v3_norm(v3_sub(P->chest, P->pelvis));
  v3 pr = v3_norm(v3_sub(P->hip[1], P->hip[0]));
  v3 pf = v3_norm(v3_cross(sp, pr));
  float reach = (ANIM_THIGH + ANIM_SHIN) * 0.80f;
  for (int i = 0; i < 2; i++) {
    float sd = i ? 1.0f : -1.0f;
    v3 d = v3_norm(v3_add(v3_scale(up, -1.0f),
                   v3_add(v3_scale(pr, sd * 0.30f), v3_scale(pf, 0.34f))));
    v3 ank = v3_lerp(P->ankle[i], v3_add(P->hip[i], v3_scale(d, reach)), c);
    v3 toe = v3_add(ank, v3_sub(P->toe[i], P->ankle[i]));
    v3 pole = v3_norm(v3_add(v3_scale(pf, 1.0f), v3_scale(up, 0.45f)));
    P->knee[i] = ik_solve(P->hip[i], ank, ANIM_THIGH, ANIM_SHIN, pole);
    P->ankle[i] = ank;
    P->toe[i] = toe;
  }
  for (int i = 0; i < 2; i++) {
    float sd = i ? 1.0f : -1.0f;
    v3 slack = v3_add(P->chest, v3_add(v3_scale(pr, sd * 0.34f),
               v3_add(v3_scale(up, -0.40f), v3_scale(pf, 0.06f))));
    v3 wri = v3_lerp(P->wri[i], slack, ca);
    v3 pole = v3_norm(v3_add(v3_scale(pr, sd * 0.8f), v3_scale(up, -0.6f)));
    P->elb[i] = ik_solve(P->sho[i], wri, ANIM_UARM, ANIM_FARM, pole);
    P->wri[i] = wri;
  }
  P->gun_o = v3_lerp(P->gun_o, v3_add(P->wri[1], v3_scale(P->gun_f, 0.07f)), ca);
  // The head drops off-axis in the FIRST fifth of the fall — a chin that
  // stays proudly level through the whole topple is the statue giveaway.
  P->head = v3_lerp(P->head, v3_add(P->neck, v3_add(v3_scale(pr, 0.055f),
                    v3_scale(up, 0.075f))), ca);
  // A wall slam crumples the body INTO itself — joints collapse toward the
  // pelvis. That compression is what reads as mass arriving somewhere.
  if (dth->slam > 0.01f) {
    float k = 0.26f * dth->slam;
    for (int i = 0; i < 2; i++) {
      P->knee[i] = v3_lerp(P->knee[i], P->pelvis, k * 0.5f);
      P->ankle[i] = v3_lerp(P->ankle[i], P->pelvis, k);
      P->toe[i] = v3_lerp(P->toe[i], P->pelvis, k);
      P->elb[i] = v3_lerp(P->elb[i], P->chest, k * 0.6f);
      P->wri[i] = v3_lerp(P->wri[i], P->chest, k);
    }
    P->head = v3_lerp(P->head, P->chest, k * 0.45f);
    P->neck = v3_lerp(P->neck, P->chest, k * 0.45f);
  }
  *out_c = c; *out_pr = pr; *out_pf = pf; *out_up = up;
}

// Phase 2 of the corpse solve: rigid tip about the shot's own axis, then the
// drape and settle, all measured against rag_support() rather than one whole-body
// ground plane — the settle, the drapes and the floor guard all measure each joint
// against the surface under ITSELF. Takes the slacken phase's own outputs (`c`,
// the local trunk frame `pr`/`pf`/`up`) to keep working in the same frame.
FIG_INLINE void fig_death_drape(pose_t *P, const death_t *dth, v3 pos,
                                float yaw, int wpn, float c, v3 pr, v3 pf,
                                v3 up) {
  // Tilt is a PHYSICAL angle now, integrated from the shot's spin, about the
  // axis the bullet set — not a fixed 86 degrees about the victim's own facing.
  float th = dth->tilt;
  v3 axis = dth->axis;
  if (v3_dot(axis, axis) < 1e-6f) axis = (v3){{cosf(yaw), 0, sinf(yaw)}};
  // Spin about the victim's OWN vertical, applied BEFORE the topple with the topple
  // axis turned by the same angle — which is exactly the same thing as spinning the
  // already-fallen body about the WORLD vertical, because R_y * R_a == R_(R_y a) * R_y.
  if (dth->yaw != 0.0f) {
    float cy = cosf(dth->yaw), sy = sinf(dth->yaw);
    #define DYAW(P, OX, OZ) do { float _x = (P).x - (OX), _z = (P).z - (OZ); \
      (P).x = (OX) + _x * cy + _z * sy;                                      \
      (P).z = (OZ) - _x * sy + _z * cy; } while (0)
    axis = (v3){{axis.x * cy + axis.z * sy, 0.0f, -axis.x * sy + axis.z * cy}};
    v3 *y = (v3 *)P;
    for (int k = 0; k < POSE_NPOS; k++) DYAW(y[k], pos.x, pos.z);
    for (int k = POSE_NPOS; k < (int)(sizeof(pose_t) / sizeof(v3)); k++)
      DYAW(y[k], 0.0f, 0.0f);
    #undef DYAW
  }
  v3 *j = (v3 *)P;
  for (int k = 0; k < POSE_NPOS; k++)
    j[k] = v3_add(pos, v3_rot_axis(v3_sub(j[k], pos), axis, th));
  for (int k = POSE_NPOS; k < (int)(sizeof(pose_t) / sizeof(v3)); k++)
    j[k] = v3_rot_axis(j[k], axis, th);
  // THE LIMBS TRAIL THE TRUNK. Everything above rotates the solved pose as one rigid
  // body, which is what a spitted mannequin does; a thrown body's free ends lag behind
  // its trunk by whatever it is turning at. `swing` is that lag in radians and the
  // displacement is its arc — cross(axis, r) is the tangential direction at each joint,
  // so this is literally "where this joint WAS `swing` radians ago", and it goes to
  // zero on its own the moment the rotation stops.
  v3 kpole[2] = {pf, pf};
  v3 epole[2] = {v3_add((v3){{0, -0.5f, 0}}, v3_scale(pr, -0.8f)),
                 v3_add((v3){{0, -0.5f, 0}}, v3_scale(pr, 0.8f))};
  if (fabsf(dth->swing) > 0.001f) {
    #define LAGJ(P, G) do { \
      v3 _r = v3_sub((P), pos); \
      (P) = v3_add((P), v3_scale(v3_cross(axis, _r), -dth->swing * DEATH_LAG_K * (G))); \
    } while (0)
    LAGJ(P->wri[0], 1.00f); LAGJ(P->wri[1], 0.72f);
    LAGJ(P->ankle[0], 0.80f); LAGJ(P->toe[0], 0.80f);
    LAGJ(P->ankle[1], 0.55f); LAGJ(P->toe[1], 0.55f);
    LAGJ(P->head, 0.45f);
    #undef LAGJ
    for (int i = 0; i < 2; i++) {
      P->elb[i] = ik_solve(P->sho[i], P->wri[i], ANIM_UARM, ANIM_FARM, epole[i]);
      P->knee[i] = ik_solve(P->hip[i], P->ankle[i], ANIM_THIGH, ANIM_SHIN, kpole[i]);
    }
  }
  // SETTLE FIRST, THEN DRAPE — the order is the whole difference between a corpse and a
  // gymnast.
  float sfrac = f_clamp(dth->slack * 1.6f, 0.0f, 1.0f);
  {
    // Against each joint's OWN surface, not one plane under the pelvis: a 1.8 m
    // body spans more ground than one ray can describe, and dropping all of it
    // onto the height that one ray found is how a corpse ends up lying flat in
    // the air past a platform lip.
    float lo0 = 1e9f;
    for (int k = 0; k < POSE_NPOS; k++)
      lo0 = fminf(lo0, j[k].y - POSE_HALF[k] - rag_support(j[k]));
    float s0 = (lo0 - 0.004f) * sfrac;
    for (int k = 0; k < POSE_NPOS; k++) j[k].y -= s0;
  }
  // Gravity drape.
  float dr = f_clamp((th - 0.60f) / 0.70f, 0.0f, 1.0f) * c *
             (1.0f - 0.85f * f_clamp(fabsf(dth->swing) / 0.35f, 0.0f, 1.0f));
  if (dr > 0.001f) {
    // Which leg ends up straight follows the side the body rolls toward, so
    // corpses do not all settle into the same silhouette.
    int lead = v3_dot(pr, axis) > 0.0f;
    // THE TRUNK HAS TO LIE DOWN TOO, and this is the half the drape never did.
    // It laid the four limbs on the ground and left pelvis, chest, neck, head,
    // hips and shoulders wherever the rigid tip about the FEET had put them —
    // and a tip about the feet is not a fall, it is a pivot, so the trunk ends
    // up over the pivot rather than beside it. Measured with `ragx`'s joint dump
    // on a settled corpse: toes 0.06, ankles 0.12, chest 0.64, shoulder 0.84.
    // That is a man kneeling with his boots on the floor, which is exactly what
    // it rendered as (span 1.33 m against a 1.79 m man, hgt 0.93 m).
    //
    // Each trunk joint eases to its OWN mesh radius above the surface UNDER
    // ITSELF — not onto one plane — so a body that comes to rest across a crate
    // drapes over it instead of being flattened onto the floor beside it. That
    // is also the answer to the single-`gy` failure: `gy` is one ray under the
    // pelvis, and a 1.8 m body spans more ground than one ray can describe.
    static const int TRUNK[6] = {0, 1, 8, 9, 15, 17};   // hips, shoulders,
    P->pelvis.y += (rag_support(P->pelvis) + POSE_HALF[14] - P->pelvis.y) * dr;
    P->neck.y   += (rag_support(P->neck)   + POSE_HALF[16] - P->neck.y)   * dr;
    for (int q = 0; q < 6; q++) {                       // chest and head
      v3 *t = &j[TRUNK[q]];
      t->y += (rag_support(*t) + POSE_HALF[TRUNK[q]] - t->y) * dr;
    }
    // ...and it runs BEFORE the limbs, not after: the arm drape clamps its wrist
    // target to shoulder reach, so with the shoulder still 0.84 m in the air the
    // hands were pulled back up to 0.47 instead of onto the ground.
    for (int i = 0; i < 2; i++) {
      float sd = i ? 1.0f : -1.0f;
      v3 out = v3_norm(v3_cross(axis, (v3){{0, 1, 0}}));
      v3 h = P->hip[i];
      // Each foot onto the surface under ITSELF. The last single-`gy` use in
      // the solve: a body settling across a platform lip had the leg that hung
      // over the void placed at the PLATFORM's height, i.e. a boot resting on
      // nothing 0.55 m up, which `ragx`'s `hang` column reads directly.
      float fy = rag_support(P->ankle[i]) + 0.085f;
      // Reach budget, the same one the gait uses: with the hip this low, the heel can
      // only be so far away horizontally.
      float want = (ANIM_THIGH + ANIM_SHIN) * (i == lead ? 0.94f : 0.62f);
      float dy = h.y - fy;
      float hmax = sqrtf(fmaxf(0.010f, want * want - dy * dy));
      v3 hd = v3_sub(P->ankle[i], h);
      hd.y = 0.0f;
      hd = v3_add(hd, v3_scale(out, sd * 0.22f));  // heels roll apart
      if (v3_dot(hd, hd) < 1e-6f) hd = out;
      hd = v3_norm(hd);
      v3 lt = {{h.x + hd.x * hmax, fy, h.z + hd.z * hmax}};
      v3 nk = v3_lerp(P->ankle[i], lt, dr);
      v3 tv = v3_sub(P->toe[i], P->ankle[i]);
      kpole[i] = v3_add((v3){{0, 0.30f, 0}}, v3_scale(out, sd * 1.25f));
      P->knee[i] = ik_solve(h, nk, ANIM_THIGH, ANIM_SHIN, kpole[i]);
      P->ankle[i] = nk;
      P->toe[i] = v3_add(nk, tv);
      v3 s = P->sho[i];
      v3 wt = P->wri[i];
      wt.y = rag_support(P->wri[i]) + 0.065f;   // ...and each hand, likewise
      wt = v3_add(wt, v3_scale(out, sd * 0.26f));  // arms fall out to the sides
      // The lead-side arm ends past the head, the other across the belly —
      // symmetric arms are the "sassy corpse" read.
      if (i == lead) wt = v3_add(wt, v3_scale(axis, 0.44f));  // thrown out wide
      else wt = v3_lerp(wt, v3_add(P->pelvis, v3_scale(up, 0.04f)), 0.45f);
      v3 dv = v3_sub(wt, s);
      float dl = v3_len(dv);
      float lim = (ANIM_UARM + ANIM_FARM) * 0.94f;
      if (dl > lim) wt = v3_add(s, v3_scale(dv, lim / dl));
      v3 nw = v3_lerp(P->wri[i], wt, dr);
      epole[i] = v3_add((v3){{0, 0.4f, 0}}, v3_scale(out, sd * 1.0f));
      P->elb[i] = ik_solve(s, nw, ANIM_UARM, ANIM_FARM, epole[i]);
      P->wri[i] = nw;
    }
  }
  // Settle: rotating rigidly about the entity origin swings every horizontally offset
  // joint UPWARD, so a folded corpse ends up sitting in mid-air with its boots a hand's
  // width clear of its own shadow.
  for (int k = 0; k < POSE_NPOS; k++)
    j[k] = foot_clamp_m(pos, j[k], POSE_RAD[k] + 0.004f * (float)(k % 5));
  // THE RAY CLAMP ONLY PROTECTS ALONG ONE LINE. It casts from the body's own origin, so
  // it cannot see a wall the body is lying ALONGSIDE, and it cannot see a part that
  // stands off its joint in a third direction — a helmet sits 120 mm past the head node
  // along the body's own spine, which is HORIZONTAL once the body is on its side, so a
  // head correctly stopped 119 mm short of a face still had 99 mm of shell inside it
  // (measured with ragx: 1421 vertices in a solid on one tick of a body sailing past
  // the end of a cover wall).
  for (int i = 0; i < 2; i++) {
    P->elb[i] = ik_solve(P->sho[i], P->wri[i], ANIM_UARM, ANIM_FARM, epole[i]);
    P->knee[i] = ik_solve(P->hip[i], P->ankle[i], ANIM_THIGH, ANIM_SHIN, kpole[i]);
  }
  // Two passes, because pushing a joint out of one solid can push it into the
  // one abutting it — cover in this arena is built from touching boxes.
  for (int pass = 0; pass < 2; pass++)
  for (int k = 0; k < POSE_NPOS; k++) {
    float rad = POSE_RAD[k];
    for (int i = 0; i < g_num_solids; i++) {
      const solid_t *sd = &g_solids[i];
      v3 q = j[k];
      if (q.x <= sd->min.x - rad || q.x >= sd->max.x + rad ||
          q.y <= sd->min.y - rad || q.y >= sd->max.y + rad ||
          q.z <= sd->min.z - rad || q.z >= sd->max.z + rad) continue;
      // Above the top face is RESTING, not trapped.
      if (q.y >= sd->max.y && q.x > sd->min.x && q.x < sd->max.x &&
          q.z > sd->min.z && q.z < sd->max.z) {
        j[k].y = sd->max.y + rad;
        continue;
      }
      float dxl = q.x - (sd->min.x - rad), dxh = (sd->max.x + rad) - q.x;
      float dzl = q.z - (sd->min.z - rad), dzh = (sd->max.z + rad) - q.z;
      float dyh = (sd->max.y + rad) - q.y;   // never DOWN: that buries the body
      float m = fminf(fminf(dxl, dxh), fminf(fminf(dzl, dzh), dyh));
      if (m == dyh)      j[k].y = sd->max.y + rad;
      else if (m == dxl) j[k].x = sd->min.x - rad;
      else if (m == dxh) j[k].x = sd->max.x + rad;
      else if (m == dzl) j[k].z = sd->min.z - rad;
      else               j[k].z = sd->max.z + rad;
    }
  }
  // ...and a floor guard afterwards, because the drape and the wall clamp both move
  // joints and either can leave one under the surface.
  float lo = 1e9f;
  for (int k = 0; k < POSE_NPOS; k++)
    lo = fminf(lo, j[k].y - POSE_HALF[k] - rag_support(j[k]));
  float set = (lo - 0.004f) * sfrac;
  if (set < 0.0f)
    for (int k = 0; k < POSE_NPOS; k++) j[k].y -= set;
  // The rifle is DROPPED, not welded to the chest: rotated rigidly with the body it
  // keeps its body-relative orientation and ends up standing straight up out of a
  // supine ribcage, held by nothing.
  v3 gfl = P->gun_f;
  gfl.y = 0.0f;
  if (v3_dot(gfl, gfl) > 1e-6f)
    P->gun_f = v3_norm(v3_lerp(P->gun_f, v3_norm(gfl), c));
  P->gun_u = v3_lerp(P->gun_u, (v3){{0, 1, 0}}, c);
  P->gun_u = v3_sub(P->gun_u, v3_scale(P->gun_f, v3_dot(P->gun_u, P->gun_f)));
  P->gun_u = v3_dot(P->gun_u, P->gun_u) > 1e-6f ? v3_norm(P->gun_u) : (v3){{0, 1, 0}};
  // A DROPPED RIFLE RESTS ON THE GROUND — on its magazine, which is what a rifle set
  // down actually stands on. `gy + 0.085` put the RECEIVER 85 mm up and everything
  // under it below the floor. g_gun_low is measured off the model, so this cannot drift
  // from the magazine it is clearing.
  if (g_gun_low[wpn] > 0.0f) {
    // Against the surface under the RIFLE, the same per-joint reference the body's own
    // guard uses.
    v3 away = v3_sub(P->wri[1], P->chest);
    away.y = 0.0f;
    float al = sqrtf(away.x * away.x + away.z * away.z);
    v3 lay = {{P->wri[1].x, P->wri[1].y, P->wri[1].z}};
    if (al > 1e-4f) {
      lay.x += away.x / al * 0.16f;
      lay.z += away.z / al * 0.16f;
    }
    lay.y = rag_support(lay) + g_gun_low[wpn] * GUN_GIRTH_3P + 0.006f;
    P->gun_o = v3_lerp(P->gun_o, lay, c);
  }
}

// The corpse solve, run in place on the INTERPOLATED pose: gravity drape, rigid tip
// about the shot's own axis, wall clamp and settle. Two phases: fig_death_slacken
// (the staggered slackening) hands its own local trunk frame and slack factor
// straight to fig_death_drape (the topple/drape/settle against rag_support()).
FIG_INLINE void fig_death_pose(pose_t *P, const death_t *dth, v3 pos,
                               float yaw, int wpn) {
  float c; v3 pr, pf, up;
  fig_death_slacken(P, dth, &c, &pr, &pf, &up);
  fig_death_drape(P, dth, pos, yaw, wpn, c, pr, pf, up);
}

// Every band colour the figure builders below draw with, solved once per figure.
typedef struct {
  v3 uniform, boot, helm, web, rub, gm2;       // base materials
  v3 gear, belt, pad, pants, dust1;            // carrier, webbing, rubber, trouser
  v3 vest, sleeve, glove, bala;                // derived / balaclava
} kit_t;

static kit_t fig_kit(float flash01, v3 flash_col) {
  // One Cool-Arid kit for every figure and every camera. Large Stone-Sage
  // cloth fields sit between a darker Dust-Olive trouser and the lighter
  // Coyote/Stone carrier and shell; role is never encoded in uniform hue.
  v3 uniform = {{0.378f, 0.385f, 0.354f}};  // stone-sage ripstop
  v3 dark    = {{0.296f, 0.300f, 0.273f}};  // dust-olive trouser
  v3 boot    = {{0.160f, 0.153f, 0.147f}};  // dark brown leather
  v3 helm    = {{0.442f, 0.449f, 0.406f}};  // sand-sage shell, the value peak
  // The carrier is the cool LIGHT block at the chest line: about one value
  // step above the uniform, with low enough chroma to stay quiet in SAND.
  v3 nyl   = {{0.447f, 0.426f, 0.374f}};   // muted coyote-stone carrier
  v3 web   = {{0.229f, 0.218f, 0.198f}};   // umber webbing / belt
  // Pads remain in the trouser family instead of becoming articulation dots.
  v3 rub   = {{0.205f, 0.210f, 0.204f}};   // worn graphite rubber
  v3 gm2   = {{0.078f, 0.082f, 0.098f}};   // lens glass, dark pupil
  // The covered face stays charcoal and never becomes the value peak.
  v3 bala  = {{0.253f, 0.253f, 0.246f}};
  if (flash01 > 0) {  // hit flash: every surface washes toward the flash color
    uniform = v3_lerp(uniform, flash_col, flash01);
    dark  = v3_lerp(dark, flash_col, flash01);
    boot  = v3_lerp(boot, flash_col, flash01);
    helm  = v3_lerp(helm, flash_col, flash01);
    nyl   = v3_lerp(nyl, flash_col, flash01);
    web   = v3_lerp(web, flash_col, flash01);
    rub   = v3_lerp(rub, flash_col, flash01);
  }

  // Band colors.
  v3 gear = nyl;                      // plate carrier + pouches
  v3 belt = web;
  v3 pad = rub;                       // knee / elbow pads
  v3 pants = dark;
  // Everything below the knee has been walking through the arena, so it carries the
  // arena's colour.
  v3 dust1 = v3_lerp(dark, g_theme.ground, 0.20f);   // shin (the boot upper
                                                     // derives its own tint at
                                                     // the draw site)
  // The yoke band rides ABOVE the uniform value (it sits at the chest line,
  // where the ramp peaks), not below it.
  v3 vest = v3_scale(uniform, 1.04f), sleeve = v3_scale(uniform, 0.97f);
  // Dark brown leather in the same neutral family as the boot. Its material
  // highlight and the viewmodel's lighting correction separate it from both
  // the cloth sleeve and the graphite weapon without creating a role colour.
  v3 glove = {{0.158f, 0.151f, 0.145f}};
  if (flash01 > 0) glove = v3_lerp(glove, flash_col, flash01);
  kit_t kt;
  kt.uniform = uniform;
  kt.boot = boot;
  kt.helm = helm;
  kt.web = web;
  kt.rub = rub;
  kt.gm2 = gm2;
  kt.gear = gear;
  kt.belt = belt;
  kt.pad = pad;
  kt.pants = pants;
  kt.dust1 = dust1;
  kt.vest = vest;
  kt.sleeve = sleeve;
  kt.glove = glove;
  kt.bala = flash01 > 0 ? v3_lerp(bala, flash_col, flash01) : bala;
  return kt;
}

// The trunk: torso prism, plate carrier, shoulder straps, assault pack and antenna, and
// the two belt mag pouches.
FIG_INLINE void fig_torso(const pose_t *P, const kit_t *kt, v3 Rc, v3 spine,
                          v3 prt) {
  // A figure lit like the world it stands in: the same height ramp the cover boxes get,
  // so boots and knees sit in their own shadow while shoulders and helmet catch the
  // sky.
  ao_set(g_fig_y, 0.55f, 0.84f);
  mat_set(MAT_CLOTH_R, 0.0f);

  // Torso: ONE swept prism from crotch to shoulder yoke.
  float tw = atan2f(v3_dot(v3_cross(prt, Rc), spine), v3_dot(prt, Rc));
  // FACETED, k=8: the redesign's stated art direction is a chunky flat-faceted operator
  // that rhymes with the arena's chamfered crates, and the torso is where that decision
  // is most visible.
  v3 cfw0 = v3_norm(v3_cross(spine, Rc));   // chest forward (the facing)
  #define TN(T, F, RX, RZ, COL, TW_, AO_) \
    {v3_add(P->pelvis, v3_add(v3_scale(spine, (T)), v3_scale(cfw0, (F)))), \
     COL, COL, (RX), (RZ), (TW_) * tw, (AO_)}
  fnode_t t[8] = {
    TN(-0.120f, -0.008f, 0.112f, 0.096f, kt->pants, 0.00f, 0.70f),  // crotch
    TN(-0.026f, -0.020f, 0.180f, 0.142f, kt->pants, 0.10f, 0.0f),   // glutes: mass BACK
    TN( 0.062f, -0.004f, 0.158f, 0.114f, kt->belt,  0.28f, 0.0f),   // belt line
    TN( 0.128f,  0.004f, 0.146f, 0.106f, kt->uniform, 0.45f, 0.0f), // waist, pulled in
    TN( 0.205f,  0.010f, 0.158f, 0.122f, kt->uniform, 0.66f, 0.0f), // lower ribcage
    TN( 0.285f,  0.016f, 0.172f, 0.136f, kt->uniform, 0.86f, 0.0f), // chest, fwd-up
    TN( 0.352f,  0.018f, 0.184f, 0.134f, kt->uniform, 0.94f, 0.0f), // upper chest
    TN( 0.472f,  0.006f, 0.156f, 0.108f, kt->vest,  1.00f, 0.0f)};  // shoulder yoke
  #undef TN
  probe("torso");
  // Damped jitter: full crate-jitter put a 1.35x light hoop mid-garment (adjacent bands
  // of ONE cloth suit), which reads as chain construction, not as designed planes.
  g_fig_jit = 0.40f;
  // First person needs the pelvis and abdomen that join the visible thighs, not a
  // second chest wrapped around the camera. Capping the shared trunk at the lower
  // ribcage keeps a closed surface below the near plane; looking down can no longer
  // expose the inside of the shoulder-yoke cap or the carrier and pack behind it.
  fig_chain(t, g_fig_fp ? 5 : 8, 8, 1, 1, prt);
  g_fig_jit = 1.0f;
  if (g_fig_fp) return;
  mat_set(MAT_GEAR_R, 0.0f);
  // Plate carrier: STANDOFF, not paint.
  #define CN(T, F, RX, RZ, TW_, AO_) \
    {v3_add(P->pelvis, v3_add(v3_scale(spine, (T)), v3_scale(cfw0, (F)))), \
     kt->gear, kt->gear, (RX), (RZ), (TW_) * tw, (AO_)}
  fnode_t pc[4] = {
    CN(0.190f, 0.010f, 0.172f, 0.152f, 0.62f, 0.72f),
    CN(0.278f, 0.016f, 0.196f, 0.170f, 0.84f, 0.0f),
    CN(0.352f, 0.018f, 0.204f, 0.170f, 0.94f, 0.0f),
    CN(0.415f, 0.012f, 0.184f, 0.148f, 0.99f, 0.0f)};
  #undef CN
  probe("carrier");
  g_fig_jit = 0.45f;
  fig_chain(pc, 4, 8, 1, 1, prt);
  g_fig_jit = 1.0f;
  // Shoulder straps: the carrier is otherwise one black slab across the chest with a
  // hard top edge and nothing holding it up.
  {
    v3 cf2 = v3_norm(v3_cross(spine, Rc));
    for (int e = 0; e < 2; e++) {
      float es = e ? 1.0f : -1.0f;
      // FIVE nodes: the old four turned 65 degrees at the shoulder's crest and
      // fig_chain's bevel folded back through the ring behind it — the same
      // fold the helmet brim's note records, and it read as a torn notch in
      // the strap from dead ahead. Splitting the crest into two ~35 degree
      // turns unfolds it. Pulled 5 mm inboard as well: the outer edge sat
      // 1 mm off the (now flat-shaded) deltoid's inner facet.
      fnode_t st[5] = {
        {v3_add(P->chest, v3_add(v3_scale(Rc, es * 0.060f),
           v3_add(v3_scale(spine, 0.016f), v3_scale(cf2, 0.112f)))),
         kt->gear, kt->gear, 0.034f, 0.019f, 0, 0.0f},
        // OUTBOARD, over the shoulder rather than over the throat.
        {v3_add(P->chest, v3_add(v3_scale(Rc, es * 0.101f),
           v3_add(v3_scale(spine, 0.082f), v3_scale(cf2, 0.086f)))),
         kt->gear, kt->gear, 0.033f, 0.021f, 0, 0.0f},
        {v3_add(P->chest, v3_add(v3_scale(Rc, es * 0.106f),
           v3_add(v3_scale(spine, 0.093f), v3_scale(cf2, 0.040f)))),
         kt->gear, kt->gear, 0.033f, 0.021f, 0, 0.0f},
        {v3_add(P->chest, v3_add(v3_scale(Rc, es * 0.107f),
           v3_add(v3_scale(spine, 0.090f), v3_scale(cf2, -0.012f)))),
         kt->gear, kt->gear, 0.033f, 0.021f, 0, 0.0f},
        {v3_add(P->chest, v3_add(v3_scale(Rc, es * 0.072f),
           v3_add(v3_scale(spine, 0.030f), v3_scale(cf2, -0.104f)))),
         kt->gear, kt->gear, 0.034f, 0.019f, 0, 0.0f}};
      probe("strap");
      fig_chain(st, 5, 8, 1, 1, spine);
    }
  }
  // Trapezius wedges: the head used to float on a long bare neck over a flat shoulder
  // line.
  for (int e = 0; e < 2; e++) {
    float es = e ? 1.0f : -1.0f;
    fnode_t tp[3] = {
      {v3_add(P->chest, v3_add(v3_scale(spine, 0.085f), v3_scale(Rc, es * 0.040f))),
       kt->uniform, kt->uniform, 0.040f, 0.052f, 0, 0.0f},
      {v3_add(P->chest, v3_add(v3_scale(spine, 0.058f), v3_scale(Rc, es * 0.115f))),
       kt->uniform, kt->uniform, 0.036f, 0.048f, 0, 0.0f},
      {v3_add(P->chest, v3_add(v3_scale(spine, 0.042f), v3_scale(Rc, es * 0.172f))),
       kt->uniform, kt->uniform, 0.026f, 0.036f, 0, 0.0f}};
    probe("trap");
    fig_chain(tp, 3, 8, 1, 1, spine);
  }
  // Utility pouch on the carrier's back — the rear silhouette is otherwise one flat
  // plate, which is the view you get of a fleeing enemy.
  v3 cfw = v3_norm(v3_cross(spine, Rc));
  {
    // Swept UP the spine, not backward.
    v3 pb = v3_add(P->chest, v3_add(v3_scale(spine, -0.060f),
                                   v3_scale(cfw, -0.178f)));
    // An assault pack rather than a pouch.
    fnode_t up[6] = {
      {v3_add(pb, v3_scale(spine, -0.104f)), kt->gear, kt->gear, 0.088f, 0.048f, 0, 0.55f},
      {v3_add(pb, v3_scale(spine, -0.052f)), kt->gear, kt->gear, 0.112f, 0.070f, 0, 0.0f},
      {v3_add(pb, v3_scale(spine,  0.020f)), kt->gear, kt->gear, 0.118f, 0.076f, 0, 0.0f},
      {v3_add(pb, v3_scale(spine,  0.052f)), kt->gear, kt->gear, 0.112f, 0.070f, 0, 0.0f},
      {v3_add(pb, v3_scale(spine,  0.070f)), kt->gear, kt->gear, 0.121f, 0.079f, 0, 0.0f},  // flap lip
      {v3_add(pb, v3_scale(spine,  0.108f)), kt->gear, kt->gear, 0.086f, 0.052f, 0, 0.0f}};
    probe("pack");
    g_fig_jit = 0.45f;
    fig_chain(up, 6, 8, 1, 1, Rc);
    g_fig_jit = 1.0f;
    // Two vertical compression straps down the pack's rear face: the only
    // landmarks the biggest rear mass has, and they say "webbing", not "tank".
    mat_set(MAT_GEAR_R, 0.0f);
    for (int cs2 = 0; cs2 < 2; cs2++) {
      float ss = cs2 ? 1.0f : -1.0f;
      fnode_t vb[3] = {
        {v3_add(pb, v3_add(v3_scale(spine, -0.088f),
           v3_add(v3_scale(Rc, ss * 0.042f), v3_scale(cfw, -0.062f)))),
         kt->web, kt->web, 0.017f, 0.021f, 0, 0.0f},
        {v3_add(pb, v3_add(v3_scale(spine,  0.016f),
           v3_add(v3_scale(Rc, ss * 0.042f), v3_scale(cfw, -0.082f)))),
         kt->web, kt->web, 0.017f, 0.020f, 0, 0.0f},
        {v3_add(pb, v3_add(v3_scale(spine,  0.096f),
           v3_add(v3_scale(Rc, ss * 0.042f), v3_scale(cfw, -0.058f)))),
         kt->web, kt->web, 0.015f, 0.018f, 0, 0.0f}};
      probe("packstrap");
      fig_chain(vb, 3, 8, 1, 1, spine);
    }
    // Whip antenna off the pack's strong-side corner: a clean vertical mast
    // BEHIND the shoulder, against the sky. It was moved onto a chest radio for
    // one pass and moved back the same day — based at the chest its whip stood
    // in front of the neck and crossed the FACE in every side view, and the
    // blade twist made its base read as floating beside the arm.
    // ...but not on the FP body: the mast is at the camera's own shoulder
    // there, and looking down it reads as a bent black prism jammed into the
    // chest — unidentifiable debris in the one view the FP body exists for.
    if (!g_fig_fp) {
      v3 ab = v3_add(pb, v3_add(v3_scale(Rc, 0.072f), v3_scale(spine, 0.070f)));
      fnode_t an[3] = {
        {ab, kt->rub, kt->rub, 0.0105f, 0.0072f, 0, 0.62f},
        {v3_add(v3_add(ab, v3_add(v3_scale(spine, 0.150f), v3_scale(cfw, -0.020f))),
         v3_scale(P->ant_sway, 0.40f)),
         kt->rub, kt->rub, 0.0068f, 0.0044f, 0, 0.0f},
        {v3_add(v3_add(ab, v3_add(v3_scale(spine, 0.286f), v3_scale(cfw, -0.076f))),
         P->ant_sway),
         kt->rub, kt->rub, 0.0036f, 0.0024f, 0, 0.0f}};
      fig_chain(an, 3, 6, 1, 1, Rc);
    }
  }
  // FRONT MAG SHINGLE — the operator's #1 mass (the MW read is chest-heavy, not
  // back-heavy): a clustered triple pouch block at STERNUM height on the plate,
  // 55 mm proud, so the side view gains its forward step where the plate is.
  // ONE mass with two divider straps, never three floating pouches — six small
  // items read as noise, one stepped block reads as a loaded carrier.
  //
  // EVERYTHING MOUNTED ON THE CARRIER FOLLOWS THE CARRIER'S OWN TWIST. The
  // torso chain rolls its cross-sections by (TW)*tw to blade the shoulders, so
  // the plate's FRONT at chest height is cfw rotated ~0.8*tw about the spine —
  // a pouch authored square to the pelvis shears off the plate exactly when the
  // figure is at the ready, which is always. Offsets AND node roll carry it.
  {
    mat_set(MAT_GEAR_R, 0.0f);
    // 0.80 (and the radio's 0.86 below) approximate the carrier's own authored
    // roll ladder (0.62/0.84/0.94 * tw) at the mount heights.
    float thw = tw * 0.80f;
    v3 fw2 = v3_rot_axis(cfw, spine, thw);
    v3 rt2 = v3_rot_axis(prt, spine, thw);
    // A small value step UNDER the carrier it hangs on, so the block reads as
    // kit ON the plate rather than as a second plate.
    v3 shc = v3_scale(kt->gear, 0.94f);
    v3 flp = v3_scale(kt->gear, 1.03f);
    // Buried into the carrier at both ends (the carrier's front face runs
    // ~0.165..0.185 of cfw over this span), 55 mm proud at the belly.
    fnode_t sg[5] = {
      {v3_add(P->pelvis, v3_add(v3_scale(spine, 0.215f), v3_scale(fw2, 0.132f))),
       shc, shc, 0.100f, 0.028f, thw, 0.58f},
      {v3_add(P->pelvis, v3_add(v3_scale(spine, 0.242f), v3_scale(fw2, 0.172f))),
       shc, shc, 0.114f, 0.058f, thw, 0.0f},
      {v3_add(P->pelvis, v3_add(v3_scale(spine, 0.292f), v3_scale(fw2, 0.176f))),
       shc, shc, 0.116f, 0.062f, thw, 0.0f},
      {v3_add(P->pelvis, v3_add(v3_scale(spine, 0.328f), v3_scale(fw2, 0.168f))),
       flp, flp, 0.108f, 0.050f, thw, 0.0f},   // flap lip
      {v3_add(P->pelvis, v3_add(v3_scale(spine, 0.360f), v3_scale(fw2, 0.142f))),
       flp, flp, 0.088f, 0.026f, thw, 0.0f}};
    probe("shingle");
    g_fig_jit = 0.45f;
    fig_chain(sg, 5, 8, 1, 1, rt2);
    g_fig_jit = 1.0f;
    // Two divider straps split the block into three mags. Hero/near tiers only
    // (the rail-teeth precedent): the strap CREST stands ~18 mm proud of the
    // shingle face — ~1.4 px at 12 m.
    if (g_fig_k >= 12) {
      for (int dv = 0; dv < 2; dv++) {
        float ss = dv ? 1.0f : -1.0f;
        fnode_t dvs[3] = {
          {v3_add(P->pelvis, v3_add(v3_scale(spine, 0.247f),
             v3_add(v3_scale(rt2, ss * 0.039f), v3_scale(fw2, 0.194f)))),
           kt->web, kt->web, 0.014f, 0.017f, thw, 0.0f},
          {v3_add(P->pelvis, v3_add(v3_scale(spine, 0.288f),
             v3_add(v3_scale(rt2, ss * 0.039f), v3_scale(fw2, 0.240f)))),
           kt->web, kt->web, 0.014f, 0.016f, thw, 0.0f},
          {v3_add(P->pelvis, v3_add(v3_scale(spine, 0.328f),
             v3_add(v3_scale(rt2, ss * 0.039f), v3_scale(fw2, 0.194f)))),
           kt->web, kt->web, 0.012f, 0.015f, thw, 0.0f}};
        probe("shingle_strap");
        fig_chain(dvs, 3, 8, 1, 1, rt2);
      }
    }
    // Radio brick on the weak-side CUMMERBUND — the flank, not the chest front.
    // On the chest it sat under the support arm in every ready pose and read as
    // a prop strapped to the arm; the flank is what the side view shows, and
    // the side view is where a radio lives on a real carrier. No antenna here —
    // the whip stays on the pack's corner, behind the shoulder (see the pack).
    mat_set(MAT_RUBBER_R, 0.0f);
    {
      float thw2 = tw * 0.86f;
      v3 rt3 = v3_rot_axis(prt, spine, thw2);
      v3 fw3 = v3_rot_axis(cfw, spine, thw2);
      v3 rdc = v3_scale(kt->rub, 0.92f);
      // Low on the flank (top station 0.344, not 0.368) and pulled 8 mm
      // inboard: at -0.186 across with its top at armpit height the brick
      // crossed the deltoid, the shoulder ball AND the upper arm (figv: 22
      // pairs over the movement sweep) — a radio worn where the arm swings is
      // a radio the arm passes through.
      fnode_t rd[4] = {
        {v3_add(P->pelvis, v3_add(v3_scale(spine, 0.248f),
           v3_add(v3_scale(rt3, -0.150f), v3_scale(fw3, 0.052f)))),
         rdc, rdc, 0.026f, 0.024f, thw2, 0.55f},
        {v3_add(P->pelvis, v3_add(v3_scale(spine, 0.272f),
           v3_add(v3_scale(rt3, -0.178f), v3_scale(fw3, 0.056f)))),
         rdc, rdc, 0.030f, 0.036f, thw2, 0.0f},
        {v3_add(P->pelvis, v3_add(v3_scale(spine, 0.344f),
           v3_add(v3_scale(rt3, -0.178f), v3_scale(fw3, 0.052f)))),
         rdc, rdc, 0.030f, 0.036f, thw2, 0.0f},
        {v3_add(P->pelvis, v3_add(v3_scale(spine, 0.368f),
           v3_add(v3_scale(rt3, -0.152f), v3_scale(fw3, 0.048f)))),
         rdc, rdc, 0.024f, 0.028f, thw2, 0.0f}};
      probe("radio");
      fig_chain(rd, 4, 8, 1, 1, fw3);
    }
    mat_set(MAT_GEAR_R, 0.0f);
  }
  // ONE mag pouch on the strong-side belt, pushed outboard. The old pair sat
  // at +-0.072 — close enough to the midline that the two merged into a single
  // dark blob at the crotch from every front view — and a mirrored pair is the
  // action-figure symmetry the whole belt-kit exists to break (the weak side
  // carries the dump pouch, down on the thigh).
  v3 pfw = v3_norm(v3_cross(spine, prt));
  {
    v3 hipc = v3_add(P->pelvis, v3_add(v3_scale(prt, 0.118f),
                                       v3_scale(spine, 0.030f)));
    fnode_t mp[3] = {
      {v3_add(hipc, v3_scale(pfw, 0.030f)), kt->belt, kt->belt, 0.050f, 0.033f, 0, 0.0f},
      {v3_add(hipc, v3_scale(pfw, 0.068f)), kt->belt, kt->belt, 0.054f, 0.037f, 0, 0.0f},
      {v3_add(hipc, v3_scale(pfw, 0.096f)), kt->belt, kt->belt, 0.040f, 0.026f, 0, 0.0f}};
    probe("magpouch");
    fig_chain(mp, 3, 12, 1, 1, spine);
  }
}

// Both arms, and the gloves closed round the grip cylinders the rifle publishes.
FIG_INLINE void fig_arms(const pose_t *P, const kit_t *kt, v3 Rc, v3 spine,
                         v3 g_org, v3 g_rt, v3 g_up, v3 g_fw, int wpn,
                         float brace_k, v3 brace_dir, gunanim_t gs, int relaxed) {
  ao_set(g_fig_y, 0.55f, 0.84f);
  mat_set(MAT_CLOTH_R, 0.0f);
  // Arms: the chain reaches into the chest first, so the deltoid bevel closes
  // the shoulder instead of leaving a floating tube end at the seam.
  fig_hand_diag_reset();
  for (int i = 0; i < 2; i++) {
    float s = i ? 1.0f : -1.0f;
    // Six rings: a cylinder from shoulder to wrist is the single most obvious "this is
    // a mannequin" tell.
    v3 ua = v3_sub(P->elb[i], P->sho[i]);
    v3 fa = v3_sub(P->wri[i], P->elb[i]);
    // Split at the elbow for the same reason as the knee: a single chain through a
    // joint that flexes past 90 degrees has to pay for the fillet out of bone
    // length it does not have, and fig_chain pays by thinning the cross-section.
    // THE SHOULDER IS ONE MUSCLE, NOT THREE SHELLS. The old build stacked a
    // deltoid BALL, an armour pad chain and the arm tube at the same joint —
    // three concentric bulges, which is exactly the ball-jointed robot read.
    // Now the deltoid IS the arm chain's own fat top: the chain starts BURIED
    // in the trunk (so there is no end cap at the shoulder for a ball to
    // swallow, and the ball is deleted, not shrunk), turns at the shoulder —
    // fig_chain's bevel is the roundover — and carries the teardrop down the
    // arm through measured stations: deltoid cap, deltoid belly at 24%, the
    // insertion waist at 50%, the bicep belly at 68%, and a decisive narrowing
    // into the elbow. A limb reads as a limb through its radius CHANGES at
    // anatomical stations; both a straight cone and a stack of shells read as
    // machine parts.
    v3 inb = v3_norm(v3_sub(P->chest, P->sho[i]));   // into the trunk
    // First bone runs chest-outward, so `spine` is the stable roll reference.
    // k=8 faceted: the sleeve family joins the crate language of the torso.
    probe("uarm");
    fig_upper_arm(P->sho[i], P->elb[i], inb, spine,
                  kt->sleeve, kt->sleeve, kt->sleeve,
                  kt->sleeve, kt->sleeve, kt->sleeve, 8);
    // The FOREARM is built after the hand, further down, because it needs the palm's
    // own axis — see fig_forearm.
    {  // The elbow ball survives only as the cap-swallower between the two arm
      // chains, and it wears the SLEEVE, near-host value: the old 0.68x dark
      // rubber made every elbow the darkest accent on the arm — an
      // articulation dot. SWELL IS NEAR-ZERO (0.05) ON PURPOSE, third try:
      // fig_joint's swell stretches the ellipsoid along OUT while the fold
      // SHORTENS it 30% along the bisector, and on the k=8 profile the out
      // extreme is a facet EDGE — so any meaningful swell turns a bent elbow
      // into a beak (0.34 was a spur, 0.18 still a point). A bent elbow's
      // corner is as blunt as the arm itself; fig_joint's built-in 0.22*bend
      // outward shift alone marks the olecranon side.
      v3 hinge = v3_cross(ua, fa);
      if (v3_dot(hinge, hinge) < 1e-8f) hinge = spine;
      probe("elbow");
      fig_joint(P->elb[i], ua, v3_sub(P->wri[i], P->elb[i]),
                0.0505f, 0.0570f, 0.05f, 8, v3_scale(kt->sleeve, 0.96f));
    }
    // Hand: the SAME wrap the viewmodel uses, closed round the SAME published grip
    // cylinder of the SAME rifle.
    mat_set(MAT_GLOVE_R, 0.0f);
    probe("hand");
    {
      v3 pd;
      if (relaxed) {
        fig_hand_relaxed(P->wri[i], P->elb[i], s, Rc, kt->glove, &pd);
      } else {
      v3 hc, ha; float hr;
      gun_hold(wpn, i ? GH_GRIP : GH_FORE3, g_org, g_rt, g_up, g_fw, &hc, &ha, &hr);
      if (i == 0 && gs.hand > 0.5f) {
        const gun_pose_desc_t *erg = gun_pose_desc(wpn);
        v3 rc, ra; float rr;
        gun_hold(wpn, GH_RELD, g_org, g_rt, g_up, g_fw, &rc, &ra, &rr);
        rc = v3_add(rc, v3_add(
            v3_scale(g_up, -gs.mag * GUN_MAG_TRAVEL[wpn][0] * GUN_GIRTH_3P),
            v3_scale(g_rt, -gs.mag * GUN_MAG_TRAVEL[wpn][1] * GUN_GIRTH_3P)));
        hc = v3_lerp(hc, rc, gs.hand);
        ha = v3_norm(v3_lerp(ha, ra, gs.hand));
        hr += (rr - hr) * gs.hand;
        hc = v3_add(hc, gun_reload_route_offset(
            erg, g_rt, g_up, g_fw, gs.hand, GUN_GIRTH_3P));
      }
      if (i == 1 && wpn == WPN_SR && gs.bhand > 0.001f) {
        v3 kc = gun_bolt_knob(gs.bolt, g_org, g_rt, g_up, g_fw, GUN_GIRTH_3P);
        hc = v3_lerp(hc, kc, gs.bhand);
        ha = v3_norm(v3_lerp(ha, g_rt, gs.bhand));
        hr += (BOLT_HAND_R * GUN_GIRTH_3P - hr) * gs.bhand;
      }
      if (i == 0 && brace_k > 0.5f) {
        // Slide brace: a KNUCKLE-DRAG FIST. The fat-cylinder palm-flat attempt wrapped
        // the fingers ~90 deg around a deeply buried axis — measured, the digits
        // reached 0.16 m below the wrist and sank through the floor.
        v3 lat = v3_cross(brace_dir, (v3){{0, 1, 0}});
        ha = v3_dot(lat, lat) > 1e-6f ? v3_norm(lat) : Rc;
        hr = 0.013f;
        hc = v3_add(P->wri[i], v3_scale((v3){{0, 1, 0}}, -0.040f));
      }
      // The trigger hand gets the weapon's published blade, exactly as the viewmodel
      // does — the third-person body is the SAME man holding the SAME rifle, and a
      // finger that only finds the trigger in first person is the divergence this whole
      // pass exists to remove.
      int trigger_contact = gs.arc + gs.cyc + gs.hand + gs.bhand <= 0.001f;
      int style = i == 1 ? (gs.bhand > 0.35f ? 0 : 1)
                         : (gs.hand > 0.5f ? 0 : 2);
      int hand_slot = i == 1 ? FIG_HAND_FIRE : FIG_HAND_SUPPORT;
      fig_hand_contact_t contact =
        fig_hand_contact_pose(hand_slot, gs.hand, gs.bhand,
                              i == 0 && brace_k > 0.5f);
      const v3 *tp = i == 1 && trigger_contact ? &g_gun_trig_p : NULL;
      fig_hand(P->wri[i], hc, ha, hr, s, kt->glove, style, tp,
               hand_slot, brace_k > 0.5f, contact, g_rt, &pd);
      }
      // ...and the SAME forearm the viewmodel builds, off the same palm axis.
      mat_set(MAT_CLOTH_R, 0.0f);
      probe("farm");
      {
        v3 back = v3_sub(P->elb[i], P->wri[i]);
        back = v3_dot(back, back) > 1e-8f ? v3_norm(back) : v3_scale(pd, -1.0f);
        v3 lat = v3_cross(back, spine);
        lat = v3_dot(lat, lat) > 1e-8f ? v3_norm(lat) : Rc;
        // k=8, the FACETED family: at k=12 the forearm was the one smooth
        // polished tube on an otherwise flat-faceted body — the single
        // loudest "robot arm" tell. The viewmodel's own call keeps 12: at
        // arm's length from the lens a facet is a wall.
        fig_forearm(P->wri[i], back, pd, lat, s, kt->sleeve,
                    v3_scale(kt->glove, 0.88f), &P->elb[i], 8);
      }
    }
    mat_set(MAT_CLOTH_R, 0.0f);
  }
}
