// =============================================================================
// render/figure_draw.inc  —  scene_draw_figure, fig_arms, profile tiers
// (g_fig_kcap), fx_flash (muzzle ribbons), fx_dust (airborne dust field).
// Proof: figcheck, figbury, budget. One TU: included once by game.c, in
// definition order — moving a definition out is a semantic change.
// Contract: THE HITBOX STANDS IN THE BODY'S OWN FRAME — this figure's own
// extents are what ent_trace_box uses. g_fig_kcap is a CEILING on the
// profile tier that may never be raised past what the corpse pool lowered
// it to.
// =============================================================================
// Draw one skeleton figure from its anim state. Every role and camera uses the
// same Cool-Arid kit; wpn picks which rifle the hands are holding. flash01
// washes the clothes toward flash_col.

enum {
  FIG_HEAD_UNDERCHIN,
  FIG_HEAD_CHIN,
  FIG_HEAD_JAW,
  FIG_HEAD_MOUTH,
  FIG_HEAD_CHEEK,
  FIG_HEAD_BROW,
  FIG_HEAD_TOP,
  FIG_HEAD_BANDS
};
typedef struct {
  float up, forward, width, depth, ao;
} fig_head_band_t;
typedef struct {
  float chin_mouth_front, chin_jaw_front;
  float chin_jaw_width_ratio, lower_face_len;
} fig_head_metrics_t;

// The renderer and harness share this authored Hero/Near profile. `forward +
// depth` is the visible front plane; `width` is the across-head half-width.
static const fig_head_band_t FIG_HEAD_PROFILE[FIG_HEAD_BANDS] = {
  // Profile planes, not a stack of round bands: a recessed underside breaks cleanly
  // into the chin, the broad mandible pinches at the mouth, cheekbones carry the face
  // and the brow turns back under the helmet. The front values deliberately avoid the
  // old 10 mm underbite where the chin projected beyond the mouth.
  [FIG_HEAD_UNDERCHIN] = {-0.1040f,  0.0060f, 0.0480f, 0.0700f, 0.68f},
  [FIG_HEAD_CHIN]      = {-0.0840f,  0.0250f, 0.0620f, 0.0680f, 0.76f},
  [FIG_HEAD_JAW]       = {-0.0600f,  0.0060f, 0.0780f, 0.0870f, 0.82f},
  [FIG_HEAD_MOUTH]     = {-0.0340f, -0.0020f, 0.0760f, 0.0960f, 0.88f},
  [FIG_HEAD_CHEEK]     = { 0.0020f, -0.0060f, 0.0810f, 0.1010f, 0.94f},
  [FIG_HEAD_BROW]      = { 0.0390f, -0.0120f, 0.0790f, 0.0970f, 0.78f},
  [FIG_HEAD_TOP]       = { 0.1140f, -0.0250f, 0.0480f, 0.0610f, 0.00f},
};

// At Far and corpse tiers only the selected chin differs from the exact
// pre-change profile. Keep that one legacy band as the LOD override instead of
// duplicating the six unchanged authored bands.
static const fig_head_band_t FIG_HEAD_FAR_CHIN =
  {-0.0840f, 0.0220f, 0.0600f, 0.0680f, 0.76f};

static inline fig_head_metrics_t fig_head_metrics(void) {
  const fig_head_band_t *chin = &FIG_HEAD_PROFILE[FIG_HEAD_CHIN];
  const fig_head_band_t *jaw = &FIG_HEAD_PROFILE[FIG_HEAD_JAW];
  const fig_head_band_t *mouth = &FIG_HEAD_PROFILE[FIG_HEAD_MOUTH];
  float chin_front = chin->forward + chin->depth;
  return (fig_head_metrics_t){
    .chin_mouth_front = chin_front - (mouth->forward + mouth->depth),
    .chin_jaw_front = chin_front - (jaw->forward + jaw->depth),
    .chin_jaw_width_ratio = chin->width / jaw->width,
    .lower_face_len = mouth->up - chin->up,
  };
}

// Neck + head + helmet, all in the look frame.
static void sdf_head(const anim_t *A, pose_t P, kit_t kt, v3 Rc, v3 spine) {
    // The head's basis comes from `pose_head_frame` — ONE copy, shared with the weld
    // proof, because a proof that measures a different head from the one drawn measures
    // nothing.
    v3 hf, hr, hu;
    pose_head_frame(&P, anim_head_cant(A), NULL, &hr, &hu, &hf);
    // A point in the HEAD's own frame: P.head + hr*a (across) + hu*b (up) + hf*c
    // (forward).
    #define HP(a, b, c) v3_add(P.head, v3_add(v3_scale(hr, (a)), \
                        v3_add(v3_scale(hu, (b)), v3_scale(hf, (c)))))
    mat_set(MAT_CLOTH_R, 0.0f);   // balaclava: the whole head is cloth now
    // Neck, MEASURED: ANSUR II (n=4082) gives 433 mm base circumference and 395 at the
    // narrow point — 138 and 126 mm across.
    fnode_t nk[5] = {
      {v3_add(P.chest, v3_scale(spine, 0.042f)), kt.vest,  kt.vest,  0.076f, 0.079f, 0, 0.70f},
      {v3_add(P.chest, v3_scale(spine, 0.092f)), kt.vest,  kt.vest,  0.070f, 0.073f, 0, 0.72f},
      {v3_add(P.chest, v3_scale(spine, 0.126f)), kt.uniform, kt.uniform, 0.066f, 0.068f, 0, 0.76f},
      // Held BELOW the mandible.
      {v3_add(P.chest, v3_scale(spine, 0.133f)), kt.uniform, kt.uniform, 0.064f, 0.066f, 0, 0.80f},
      // The throat band is balaclava now — the whole head is covered. Slightly
      // darker than the face cloth: it lives in the helmet's shadow.
      {v3_add(P.head, v3_scale(hu, -0.056f)), v3_scale(kt.bala, 0.82f),
       v3_scale(kt.bala, 0.82f), 0.058f, 0.061f, 0, 0.62f}};
    probe("neck");
    fig_chain(nk, 5, 12, 1, 1, Rc);
    // Seven authored planes up the skull. The narrow under-chin and broad jaw are what
    // make the lower face readable; low polygon count comes from decisive silhouette
    // changes rather than deleting the nose or turning the head into an ellipsoid.
    const int far_head = g_fig_k <= 8;
    fnode_t hd[FIG_HEAD_BANDS];
    for (int i = 0; i < FIG_HEAD_BANDS; i++) {
      fig_head_band_t b = far_head && i == FIG_HEAD_CHIN
                        ? FIG_HEAD_FAR_CHIN : FIG_HEAD_PROFILE[i];
      hd[i] = (fnode_t){HP(0, b.up, b.forward), kt.bala, kt.bala,
                        b.width, b.depth, 0, b.ao};
    }
    probe("skull");
    fig_chain(hd, FIG_HEAD_BANDS, far_head ? 16 : 8, 1, 1, hr);
    // A compact diamond nose restores a readable facial centre plane in profile. Its
    // root is buried in the cheek/brow surface and its bridge remains under the goggle;
    // only the tip and underside alter the silhouette.
    {
      fnode_t ns[4] = {
        {HP(0,  0.018f, 0.072f), kt.bala, kt.bala, 0.017f, 0.018f, 0, 0.0f},
        {HP(0,  0.014f, 0.096f), kt.bala, kt.bala, 0.016f, 0.017f, 0, 0.0f},
        {HP(0, -0.004f, 0.113f), kt.bala, kt.bala, 0.013f, 0.014f, 0, 0.0f},
        {HP(0, -0.021f, 0.094f), kt.bala, kt.bala, 0.010f, 0.012f, 0, 0.0f}};
      probe("nose");
      fig_chain(ns, 4, 4, 1, 1, hu);
      for (int e = 0; e < 2; e++) {
        float es = e ? 1.0f : -1.0f;
        fnode_t er[2] = {
          {HP(es * 0.068f, -0.030f, 0), kt.bala, kt.bala, 0.014f, 0.020f, 0, 0.0f},
          {HP(es * 0.082f, -0.026f, 0), kt.bala, kt.bala, 0.011f, 0.017f, 0, 0.0f}};
        probe("ear");
        fig_chain(er, 2, 8, 1, 1, hu);
      }
    }
    mat_set(MAT_HELM_R, 0.0f);           // helmet: a hard shell, not cloth
    // THE BUCKET, diagnosed by reading the radii rather than the screenshot.
    g_fig_jit = 0.35f;
    fnode_t hm[9] = {
      // Nape pulled 8 mm further down and back and 8 mm wider: at (0.054,
      // 0.064) against the base ring's (0.088, 0.095) 30 mm away, the shell
      // flared 49 degrees in one step and fig_chain's own bevel folded back
      // through the ring behind it — three helm_shell/helm_shell crossings, a
      // surface intersecting ITSELF. 36 degrees does not fold.
      {HP(0, -0.076f, -0.066f), kt.helm, kt.helm, 0.0620f, 0.0720f, 0, 0.42f},  // nape flap
      // Base dropped 8 mm and the LIP pulled in 5 mm, for the same reason the
      // nape moved: the brim flared 31 mm of radius in 25 mm of run — 51
      // degrees — and fig_chain's bevel folded the flare back through the ring
      // behind it, so the shell intersected ITSELF for a 25 mm seam right on
      // the brow line. 39 degrees does not fold, and the brim still reads as a
      // brim because it is still the widest ring on the helmet by 12 mm.
      {HP(0, -0.058f, -0.044f), kt.helm, kt.helm, 0.0880f, 0.0950f, 0, 0.55f},  // base
      {HP(0, -0.032f, -0.020f), kt.helm, kt.helm, 0.1075f, 0.1215f, 0, 0.0f},   // THE LIP
      {HP(0, -0.016f, -0.014f), kt.helm, kt.helm, 0.1040f, 0.1200f, 0, 0.0f},
      {HP(0, 0.024f, -0.012f), kt.helm, kt.helm, 0.1050f, 0.1205f, 0, 0.0f},
      {HP(0, 0.066f, -0.011f), kt.helm, kt.helm, 0.1000f, 0.1140f, 0, 0.0f},
      {HP(0, 0.104f, -0.012f), kt.helm, kt.helm, 0.0885f, 0.1000f, 0, 0.0f},
      {HP(0, 0.136f, -0.014f), kt.helm, kt.helm, 0.0680f, 0.0765f, 0, 0.0f},
      {HP(0, 0.156f, -0.016f), kt.helm, kt.helm, 0.0330f, 0.0375f, 0, 0.0f}};
    probe("helm_shell");
    fig_chain(hm, 9, 8, 1, 1, hr);
    g_fig_jit = 1.0f;
    // (The brim is a node of the shell above — see the LIP comment.
    mat_set(MAT_HELM_R, 0.0f);
    // NVG SHROUD: the squarish mount block high on the brow, the single most
    // recognizable modern-helmet cue there is. It sits ABOVE the goggle band
    // (goggle top runs to hu 0.058 at the centre — the shroud's bottom edge at
    // 0.060 clears it), buried into the shell at its base and ~21 mm proud.
    {
      // WIDER THAN TALL and clearly DARKER than the shell: at 0.90x of the
      // shell's own colour the boss read as a hole in the dome in 3/4 views —
      // an accessory bolted ON the shell has to separate from it in value, and
      // a real shroud is a dark polymer plate on a painted shell.
      v3 shl = v3_scale(kt.helm, 0.60f);
      fnode_t nv[3] = {
        {HP(0, 0.077f, 0.088f), shl, shl, 0.017f, 0.042f, 0, 0.0f},
        {HP(0, 0.075f, 0.112f), shl, shl, 0.016f, 0.038f, 0, 0.0f},
        {HP(0, 0.073f, 0.124f), shl, shl, 0.012f, 0.028f, 0, 0.0f}};
      probe("nvg_shroud");
      fig_chain(nv, 3, 8, 1, 1, hu);
    }
    // SIDE RAILS: one long low bar per temple, brow to nape. Hero/near tiers
    // only: 9 mm of proudness is sub-pixel past 12 m.
    if (g_fig_k >= 12) {
      // The bar follows the shell's own ellipse (rx 0.105 across, rz 0.1205
      // fwd) — a straight bar at the side flat floats in air past |c| ~ 0.03.
      // 0.80x, not 0.88: at 0.88 the bar vanished into the shell's facet
      // shading and the helmet kept reading as a bare dome from the side.
      v3 rlc = v3_scale(kt.helm, 0.80f);
      for (int e = 0; e < 2; e++) {
        float es = e ? 1.0f : -1.0f;
        // b 0.038: the goggle band's temple slope tops out at ~0.019 here, and
        // a rail whose underside ran at 0.013 crossed it shallowly all along.
        fnode_t rl[4] = {
          {HP(es * 0.080f, 0.040f, 0.062f), rlc, rlc, 0.013f, 0.011f, 0, 0.0f},
          {HP(es * 0.100f, 0.037f, 0.024f), rlc, rlc, 0.013f, 0.011f, 0, 0.0f},
          {HP(es * 0.101f, 0.035f, -0.034f), rlc, rlc, 0.013f, 0.011f, 0, 0.0f},
          {HP(es * 0.082f, 0.033f, -0.066f), rlc, rlc, 0.012f, 0.010f, 0, 0.0f}};
        probe("helm_rail");
        fig_chain(rl, 4, 8, 1, 1, hu);
      }
    }
    // Visor: ONE box spanning exactly the skull's flat front facet, which is 2*0.42*rx
    // wide for the chamfered profile.
    mat_set(MAT_GLASS_R, 0.0f);          // ballistic lens: the one wet gleam
    {
      // Re-fitted to the deeper skull.
      // The temple ends bury directly under the helmet shell; their top and rear
      // corners land inside that solid, so the band closes without a floating cap.
      const float gz_[5] = {-0.084f, -0.050f, 0.000f, 0.050f, 0.084f};  // across
      const float gd_[5] = {-0.026f, 0.056f, 0.080f, 0.056f, -0.026f};  // forward
      const float gu_[5] = {-0.006f, 0.016f, 0.020f, 0.016f, -0.006f};  // up
      const float gr_[5] = {0.014f, 0.034f, 0.038f, 0.034f, 0.014f};    // height
      fnode_t gg[5];
      // The temple ends are a QUIET step, not a bright one.
      v3 gfr = v3_scale(kt.bala, 1.12f);   // frame rim: a step over the cloth,
      for (int q = 0; q < 5; q++)          // still far under the helmet
        gg[q] = (fnode_t){.p = HP(gz_[q], gu_[q], gd_[q]),
                          .col = (q == 0 || q == 4) ? gfr : kt.gm2,
                          .bcol = (q == 0 || q == 4) ? gfr : kt.gm2,
                          .rx = gr_[q], .rz = 0.024f};
      probe("goggle");
      fig_chain(gg, 5, 8, 1, 1, hu);
      // NO STRAP. It existed because "goggles that stop at the temples look like a
      // sleep mask" — true of the narrow band it was written for, and no longer true of
      // this one: the band spans the face and its ends bury under the shell.
    }
    // Chin strap: two thin runs from the helmet rim down under the jaw. Nothing
    // says "helmet" like the thing that holds it on.
    mat_set(MAT_GEAR_R, 0.0f);
    for (int e = 0; e < 2; e++) {
      float es = e ? 1.0f : -1.0f;
      fnode_t cs[3] = {
        // 0.078, not 0.089: the top node's outer edge stood at 99 mm where the shell
        // has already narrowed past it, so the strap emerged THROUGH the helmet for
        // five pairs.
        {HP(es * 0.084f, -0.028f, 0), kt.gear, kt.gear, 0.010f, 0.008f, 0, 0.0f},
        {HP(es * (far_head ? 0.066f : 0.068f),
            far_head ? -0.088f : -0.085f, far_head ? 0 : 0.006f),
         kt.gear, kt.gear, 0.009f, 0.007f, 0, 0.0f},
        {HP(es * (far_head ? 0.024f : 0.028f),
            far_head ? -0.098f : -0.100f, far_head ? -0.014f : 0.010f),
         kt.gear, kt.gear, 0.009f, 0.007f, 0, 0.0f}};
      probe("chinstrap");
      fig_chain(cs, 3, 8, 1, 1, hf);
    }
    #undef HP
}

  // Legs: the chain starts INSIDE the pelvis, so the thigh's end cap can never peek out
  // of the hip, and the knee bevel lands as a faceted pad.
static void sdf_legs(pose_t P, kit_t kt, v3 spine, v3 prt) {
  mat_set(MAT_CLOTH_R, 0.0f);
  for (int i = 0; i < 2; i++) {
    v3 td = v3_sub(P.knee[i], P.hip[i]);
    td = v3_dot(td, td) > 1e-6f ? v3_norm(td) : v3_scale(spine, -1.0f);
    v3 sd = v3_sub(P.ankle[i], P.knee[i]);
    sd = v3_dot(sd, sd) > 1e-6f ? v3_norm(sd) : v3_scale(spine, -1.0f);
    // TWO chains and a ball, not one chain through the knee.
    v3 thv = v3_sub(P.knee[i], P.hip[i]);
    v3 shv = v3_sub(P.ankle[i], P.knee[i]);
    fnode_t th[7];   // stub, hip, [2 strap nodes on the rig leg], 0.42, 0.76, knee
    int nth = 0;
    // The thigh reaches BACKWARD ALONG ITS OWN AXIS into the pelvis: the end
    // cap ends up buried whatever the leg does, and because the stub is
    // collinear with the thigh the hip contributes no bend either.
    // k=8 faceted with real quad mass: the legs join the crate language.
    th[nth++] = (fnode_t){v3_add(P.hip[i], v3_scale(td, -0.090f)),
                          kt.pants, kt.pants, 0.046f, 0.050f, 0, 0.0f};
    // Narrowed ACROSS more than in depth, which is both the anatomy and the fix for the
    // one thing the first attempt broke: hip_span is 190 mm, so at rx 0.1023 each thigh
    // reached 7 mm PAST the midline and the two legs intersected each other.
    th[nth++] = (fnode_t){P.hip[i], kt.pants, kt.pants, 0.0900f, 0.1079f, 0, 0.0f};
    if (i == 1) {
      // The drop-leg rig's LEG STRAP, as two fat nodes OF THIS CHAIN rather than a band
      // wrapped round it.
      th[nth++] = (fnode_t){v3_add(P.hip[i], v3_scale(thv, 0.28f)),
                            kt.pants, kt.pants, 0.0896f, 0.1085f, 0, 0.0f};
      th[nth++] = (fnode_t){v3_add(P.hip[i], v3_scale(thv, 0.36f)),
                            kt.belt, kt.belt, 0.0882f, 0.1070f, 0, 0.0f};
    }
    th[nth++] = (fnode_t){v3_add(P.hip[i], v3_scale(thv, 0.42f)),
                          kt.pants, kt.pants, 0.0855f, 0.1017f, 0, 0.0f};
    // A decisive narrowing INTO the knee (0.75 station, then 0.056/0.058 at
    // the joint): the thin station is what makes the thigh read as a mass —
    // and a knee no fatter than its own limb is what makes the joint read as
    // a hinge instead of a ball.
    th[nth++] = (fnode_t){v3_add(P.hip[i], v3_scale(thv, 0.76f)),
                          kt.pants, kt.pants, 0.0740f, 0.0830f, 0, 0.0f};
    th[nth++] = (fnode_t){P.knee[i], kt.pants, kt.pants, 0.056f, 0.058f, 0, 0.0f};
    probe("thigh");
    g_fig_jit = 0.40f;   // same cap as the torso: cloth, not crates
    fig_chain(th, nth, 8, 1, 1, prt);
    g_fig_jit = 1.0f;
    fnode_t sh[4] = {
      {P.knee[i], kt.pants, kt.pants, 0.056f, 0.058f, 0, 0.0f},
      // Calf belly UP (0.076 -> 0.081): a shank with no calf is a pipe.
      {v3_add(P.knee[i], v3_scale(shv, 0.30f)), kt.pants, kt.pants, 0.081f, 0.083f, 0, 0.0f},
      // The tucked trouser narrows before entering the boot shaft; the leather can
      // therefore remain close-laced without being hidden by a full-width cloth tube.
      {P.ankle[i], kt.dust1, kt.dust1, 0.044f, 0.046f, 0, 0.0f},          // ankle
      // ...and the shin keeps going 3 cm PAST the ankle, into the boot: a cap
      // sitting exactly at the ankle pokes out of the collar the moment the
      // foot pitches, which reads as a boot that came off.
      {v3_add(P.ankle[i], v3_scale(sd, 0.032f)), kt.dust1, kt.dust1, 0.035f, 0.037f, 0, 0.0f}};
    probe("shin");
    g_fig_jit = 0.40f;
    fig_chain(sh, 4, 8, 1, 1, prt);
    g_fig_jit = 1.0f;
    // The knee itself: the cap-swallower between the two leg chains, in the
    // HOST's own trouser value — the old dark rubber ball was the roundest AND
    // darkest thing on the leg, i.e. an articulation dot. >= 6 mm bigger than
    // the tube ends it swallows (0.056/0.058) in EVERY direction, and swell
    // 0.36 puts the flexed mass on the patella side, where a knee's point is.
    {
      v3 hinge = v3_cross(td, sd);
      if (v3_dot(hinge, hinge) < 1e-8f) hinge = prt;
      probe("knee");
      fig_joint(P.knee[i], td, sd, 0.0655f, 0.0760f, 0.36f, 8,
                v3_scale(kt.pants, 0.97f));
    }
    // THE KNEE PAD IS A PLATE, NOT A BALL: a flat dark cap strapped over the
    // patella and the shin's top — the MW cue that reads as a step in the
    // shin line at 15 m. Both ends ride the BONES (buried in the thigh's and
    // shin's own tubes), so the plate bridges the joint's corner and follows
    // the fold; the crown stands off the knee ball by the bend-scaled margin
    // the ball itself grows by (fig_joint: swell 0.36 and the 0.22 out-shift).
    {
      float kbend = f_clamp(1.0f - v3_dot(td, sd), 0.0f, 1.0f);
      v3 pfw = v3_norm(v3_cross(spine, prt));
      // Away from the fold, biased forward so a dead-straight leg keeps a
      // deterministic patella side instead of a spinning frame.
      v3 kout = v3_add(v3_sub(td, sd), v3_scale(pfw, 0.35f));
      kout = v3_sub(kout, v3_scale(v3_norm(v3_add(td, sd)),
                                   v3_dot(kout, v3_norm(v3_add(td, sd)))));
      kout = v3_dot(kout, kout) > 1e-6f ? v3_norm(kout) : pfw;
      float off = 0.0575f + 0.038f * kbend;
      mat_set(MAT_RUBBER_R, 0.0f);
      // Near-black, not the rubber family: at kt.pad the plate read as a
      // BUCKLE strapped between two greens; a black cap reads as kit. The
      // ends are buried DEEP (their outer faces 25+ mm inside the bone tubes)
      // — at 5 mm they ran near-parallel to the thigh's own skin and the seam
      // shimmered, which is the z-race the first cut shipped with.
      v3 kpc = v3_scale(kt.rub, 0.55f);
      fnode_t kp[3] = {
        {v3_add(v3_add(P.knee[i], v3_scale(td, -0.056f)), v3_scale(kout, 0.036f)),
         kpc, kpc, 0.012f, 0.038f, 0, 0.0f},
        {v3_add(P.knee[i], v3_scale(kout, off)), kpc, kpc, 0.013f, 0.041f, 0, 0.0f},
        {v3_add(v3_add(P.knee[i], v3_scale(sd, 0.064f)), v3_scale(kout, 0.032f)),
         kpc, kpc, 0.011f, 0.036f, 0, 0.0f}};
      probe("kneepad");
      fig_chain(kp, 3, 8, 1, 1, kout);
      mat_set(MAT_CLOTH_R, 0.0f);
    }
    if (i == 1) {  // Drop-leg rig on the strong-side thigh. A loaded soldier's
      // widest point below the chest is his belt kit, and with nothing on the thigh the
      // legs taper straight out of the hips like a diver's.
      v3 pfw = v3_norm(v3_cross(spine, prt));                      // pelvis fwd
      v3 out = v3_add(v3_scale(prt, 0.97f), v3_scale(pfw, 0.24f));  // ~14 deg
      out = v3_sub(out, v3_scale(td, v3_dot(out, td)));
      out = v3_dot(out, out) > 1e-6f ? v3_norm(out) : prt;
      mat_set(MAT_GEAR_R, 0.0f);
      // SEATED OFF THE THIGH'S OWN CROSS-SECTION, never off a constant.
      float rhip = fig_rmax(8, th[1].rx, th[1].rx);
      float rbel = fig_rmax(8, th[nth - 3].rx, th[nth - 3].rx);  // the 0.42 belly station (0.76 sits between it and the knee now)
      // Half-depth 40 mm at the belly: the outer face then stands >= 40 mm proud
      // of the leg in every direction, and the inner face is >= 25 mm buried in
      // it — the circumradius is ~15 mm over the inradius on this profile and
      // that difference is the whole margin budget. rx is the OUTWARD half-depth
      // and rz the half-width around the leg, because the hint below is `out`
      // and fig_ring lays rx along the hint.
      const float dls[4] = {0.040f, 0.090f, 0.150f, 0.196f};  // below the hip
      const float dlx[4] = {0.030f, 0.040f, 0.038f, 0.028f};  // depth, along out
      const float dlz[4] = {0.038f, 0.048f, 0.046f, 0.034f};  // width, round leg
      fnode_t dl[4];
      for (int q = 0; q < 4; q++) {
        float u = (dls[q] - dls[0]) / (dls[3] - dls[0]);
        dl[q] = (fnode_t){v3_add(v3_add(P.hip[i], v3_scale(out, rhip + (rbel - rhip) * u)),
                                 v3_scale(td, dls[q])),
                          kt.belt, kt.belt, dlx[q], dlz[q], 0, q ? 0.0f : 0.62f};
      }
      probe("dropleg");
      // The hint is `out`, NOT `td`. fig_chain consults the hint for the FIRST ring
      // only and every chain has to pick one that cannot align with its own first bone
      // — and this chain's first bone IS td, so the hint projected to zero and the
      // fallback anchored the cross-section to cross(td, WORLD UP).
      fig_chain(dl, 4, 8, 1, 1, out);
    }
    if (i == 0) {  // DUMP POUCH on the weak-side hip: the drop-leg rig's own
      // construction (seated off the thigh's cross-section, riding its axis),
      // but a soft bag — shorter, rounder, widest at the BOTTOM because fabric
      // sags, and canted slightly BACK where the rig cants forward. The pair is
      // the belt kit's asymmetry: one hard panel starboard, one soft bag port.
      v3 pfw = v3_norm(v3_cross(spine, prt));
      v3 out = v3_add(v3_scale(prt, -0.97f), v3_scale(pfw, -0.16f));
      out = v3_sub(out, v3_scale(td, v3_dot(out, td)));
      out = v3_dot(out, out) > 1e-6f ? v3_norm(out) : v3_scale(prt, -1.0f);
      mat_set(MAT_GEAR_R, 0.0f);
      float rhip = fig_rmax(8, th[1].rx, th[1].rx);
      float rbel = fig_rmax(8, th[nth - 3].rx, th[nth - 3].rx);  // the 0.42 belly station (0.76 sits between it and the knee now)
      // 0.55 toward the carrier nylon, not 0.30: at 0.30 the bag matched the
      // drop-leg rig's value and the two hip lumps re-symmetrized at 12 m —
      // one hard DARK panel starboard, one soft LIGHT bag port is the read.
      v3 bag = v3_lerp(kt.belt, kt.gear, 0.55f);
      // Top station at 0.012 with 0.78 of the stand-off: the bag's first ring
      // is buried in the glute mass at the belt line — hung at 0.030 its top
      // edge hovered beside the belt with sky behind it.
      const float ds[4] = {0.012f, 0.060f, 0.118f, 0.158f};  // below the hip
      const float dx[4] = {0.022f, 0.040f, 0.048f, 0.030f};  // depth, along out
      const float dz[4] = {0.028f, 0.052f, 0.060f, 0.038f};  // width, round leg
      fnode_t dp[4];
      for (int q = 0; q < 4; q++) {
        float u = (ds[q] - ds[0]) / (ds[3] - ds[0]);
        float so = (rhip + (rbel - rhip) * u) * (q == 0 ? 0.78f : 1.0f);
        dp[q] = (fnode_t){v3_add(v3_add(P.hip[i], v3_scale(out, so)),
                                 v3_scale(td, ds[q])),
                          bag, bag, dx[q], dz[q], 0, q ? 0.0f : 0.62f};
      }
      probe("dumppouch");
      g_fig_jit = 0.45f;
      fig_chain(dp, 4, 8, 1, 1, out);
      g_fig_jit = 1.0f;
    }
    mat_set(MAT_LEATHER_R, 0.0f);   // boot + cuff
    // Boot: swept along the actual foot axis (which carries the toe pitch), so
    // the sole stays flat on the ground and lifts with the step.
    v3 fF = v3_sub(P.toe[i], P.ankle[i]);
    if (v3_dot(fF, fF) > 1e-6f) {
      fF = v3_norm(fF);
      // The boot owns a ground/foot frame. `spine` follows the Leaned torso; using it
      // as boot-up rolled the sole sideways and kinked the ankle during a Lean step.
      v3 foot_up = v3_sub((v3){{0, 1, 0}}, v3_scale(fF, fF.y));
      foot_up = v3_dot(foot_up, foot_up) > 1e-8f
                  ? v3_norm(foot_up) : (v3){{0, 1, 0}};
      // Node heights are chosen so every sole vertex lands ~2 mm above the planted
      // ankle's floor (ANIM_ANKLE_Y): a boot built symmetrically around the ankle rides
      // toe-down and digs its tip into the ground.
      float sole_dip = fminf(fminf(fF.y * -0.086f, fF.y * 0.194f), 0.0f);
      v3 av = v3_add(P.ankle[i], v3_scale(foot_up, -0.028f - sole_dip));
      // A MINIMAL BOOT IS TWO PIECES: a smooth rubber SLAB and a leather UPPER set
      // into it. The five it used to be (lugged outsole, proud welt, upper, lace strip,
      // cross-bars) were all authored for a distance nobody plays at — against this
      // renderer's 960/d px per metre the welt is 1.0 px at 10 m and a lug stands proud
      // in rz, i.e. moves no silhouette side-on, which is the only angle a boot is read
      // from. They also made the node count TIER-DEPENDENT, which is the FIG_NMAX
      // truncation shape this file has already been bitten by.
      //
      // What replaces the welt is the SHAPE the k=6 profile makes: its z = +-1 vertices
      // sit at x = 0, i.e. exactly mid-slab, so the sweep produces one continuous ridge
      // heel to toe at mid-sole height. Under a 38-degree sun that splits the sole into
      // an up-facing chamfer and a down-facing one — a hard horizontal line in the SAME
      // albedo, for zero extra nodes. k=6 also has the widest flat bottom facet of the
      // faceted family, i.e. a square-edged slab rather than a rocker.
      //
      // THE SOLE PLANE IS THE CONTRACT and it does not move: every piece places its
      // ring centres at (rx - H) along `spine`, which puts centre - rx on ONE plane
      // whatever that ring's height is. Shaping any of these by radius alone would lift
      // its own sole with it. 0.047 is the number that puts the sole exactly on the
      // floor, and it is UNCHANGED so `kine`'s `sole` column stays byte-identical and
      // the rebuild is provably contact-neutral.
      const float SOLE_H = 0.047f;    // sole floor — the ground contact
      // 0.021, not 0.026: with the welt gone the leather sits directly into the slab,
      // buried 3-14 mm inside it at every station, which is what closes the "10 mm of
      // open air between the upper's floor and the sole's top" the welt used to fill.
      // 0.031, not 0.021, and this is the single biggest z-race on the figure.
      const float UPPR_H = 0.031f;    // leather floor, well inside the slab
      #define SOLEN(H, T, RX, RZ, C, BC) {                                  \
        v3_add(v3_add(av, v3_scale(fF, (T))), v3_scale(foot_up, (RX) - (H))), \
        C, BC, (RX), (RZ), 0, 0.0f}
      // ANSUR foot length is 271 mm p50 and a combat boot's exterior is ~290; this runs
      // -0.086 to +0.194, i.e. 280 mm heel to toe with the ankle at 31% of it from the
      // heel (a real foot is ~25%).
      mat_set(MAT_RUBBER_R, 0.0f);
      // One value step UNDER the leather, not over it. `kt.rub` measured 1.38x LIGHTER
      // than the upper it carries, which is a trainer's midsole read: a pale band under
      // a dark shoe.
      v3 sole_c = v3_scale(kt.boot, 0.78f);
      fnode_t so[7] = {
        SOLEN(SOLE_H, -0.086f, 0.0160f, 0.0430f, sole_c, sole_c),  // heel, squared
        SOLEN(SOLE_H, -0.050f, 0.0170f, 0.0470f, sole_c, sole_c),  // heel block
        SOLEN(SOLE_H, -0.026f, 0.0140f, 0.0460f, sole_c, sole_c),  // heel breast
        SOLEN(SOLE_H,  0.030f, 0.0160f, 0.0500f, sole_c, sole_c),  // waist
        SOLEN(SOLE_H,  0.090f, 0.0175f, 0.0570f, sole_c, sole_c),  // ball, widest
        SOLEN(SOLE_H,  0.152f, 0.0160f, 0.0540f, sole_c, sole_c),  // toe box
        SOLEN(SOLE_H,  0.194f, 0.0140f, 0.0340f, sole_c, sole_c)}; // toe end
      probe("outsole");
      // Half jitter.
      g_fig_jit = 0.5f;
      fig_chain(so, 7, 6, 1, 1, foot_up);
      g_fig_jit = 1.0f;
      mat_set(MAT_LEATHER_R, 0.0f);
      // The leather upper, set INTO the slab: narrower than it at every station
      // and with its floor buried inside it, so no
      // edge of the leather is ever the silhouette's bottom and the two surfaces meet
      // in exactly one place — a buried floor plate. A high instep and broad protective
      // toe keep the combat boot from collapsing into a thin dress-shoe silhouette.
      v3 lea_c = v3_scale(kt.boot, 1.40f);
      v3 up_c = v3_lerp(lea_c, g_theme.ground, 0.20f);
      fnode_t bt[7] = {
        SOLEN(UPPR_H, -0.074f, 0.0280f, 0.0380f, lea_c, lea_c),     // counter
        SOLEN(UPPR_H, -0.040f, 0.0360f, 0.0430f, lea_c, lea_c),     // counter top
        SOLEN(UPPR_H,  0.012f, 0.0410f, 0.0440f, up_c,   up_c),      // throat
        SOLEN(UPPR_H,  0.068f, 0.0390f, 0.0500f, up_c,   up_c),      // instep
        SOLEN(UPPR_H,  0.124f, 0.0330f, 0.0520f, up_c,   up_c),      // vamp
        SOLEN(UPPR_H,  0.172f, 0.0270f, 0.0430f, lea_c, lea_c),     // toe cap
        SOLEN(UPPR_H,  0.194f, 0.0160f, 0.0300f, lea_c, lea_c)};    // toe tip
      probe("boot");
      fig_chain(bt, 7, 8, 1, 1, foot_up);  // sole down: rx is the boot height
      #undef SOLEN
      // Cuff: a tall, close-laced leather shaft OUTSIDE a narrow trouser end. The
      // first ring is cloth disappearing into the padded collar; every exposed ring
      // below it is leather. This keeps the trouser visibly tucked instead of drawing
      // a broad blouse that made the boot itself read as a thin low shoe.
      fnode_t cf[6] = {
        {v3_add(P.ankle[i], v3_scale(sd, -0.205f)), kt.pants, kt.pants, 0.049f, 0.052f, 0, 0.0f},
        {v3_add(P.ankle[i], v3_scale(sd, -0.190f)), lea_c, lea_c, 0.069f, 0.072f, 0, 0.0f},          // padded collar
        {v3_add(P.ankle[i], v3_scale(sd, -0.145f)), lea_c, lea_c, 0.066f, 0.069f, 0, 0.0f},          // shaft top
        {v3_add(P.ankle[i], v3_scale(sd, -0.090f)), lea_c, lea_c, 0.061f, 0.064f, 0, 0.0f},          // laced shaft
        {v3_add(P.ankle[i], v3_scale(sd, -0.035f)), lea_c, lea_c, 0.0570f, 0.0600f, 0, 0.0f},        // lace pinch
        {v3_add(P.ankle[i], v3_scale(sd,  0.014f)), lea_c, lea_c, 0.0500f, 0.0530f, 0, 0.0f}};
      probe("bootcuff");
      // The shaft leather is the UPPER's band, not the toe-cap's.
      mat_set(MAT_LEATHER_R, 0.0f);
      g_fig_jit = 0.40f;
      fig_chain(cf, 6, 8, 1, 1, prt);
      g_fig_jit = 1.0f;
      // Four low cross-laces are enough to identify a tightly closed tactical shaft
      // in the near tier. They lie only 2 mm proud of the tongue and therefore add
      // detail without changing the boot's contact or distance silhouette.
      if (g_fig_k >= 12) {
        mat_set(MAT_RUBBER_R, 0.0f);
        v3 lace_c = v3_scale(kt.boot, 0.68f);
        const float lace_h[4] = {-0.150f, -0.118f, -0.086f, -0.054f};
        for (int l = 0; l < 4; l++) {
          float front = 0.064f - 0.006f * (float)l / 3.0f;
          v3 lc = v3_add(v3_add(P.ankle[i], v3_scale(sd, lace_h[l])),
                         v3_scale(fF, front + 0.002f));
          scene_box(lc, v3_scale(prt, 0.024f), v3_scale(sd, 0.0016f),
                    v3_scale(fF, 0.0015f), lace_c);
        }
      }
      mat_set(MAT_LEATHER_R, 0.0f);
    }
    mat_set(MAT_CLOTH_R, 0.0f);   // next iteration of the loop starts on trouser
  }
}

// The rifle: the SAME model the player's own viewmodel is built from, anchored so the
// trigger hand lands on the pistol grip and the support hand on the handguard.
// The FP body carries neither rifle nor arms — the viewmodel pass draws both,
// welded to the camera; a second pair posed for a third-person observer would
// stand in the frame beside them.
static void sdf_rifle(const anim_t *A, pose_t P, kit_t kt, v3 Rc, v3 spine, int wpn,
                     gunanim_t gs, int relaxed) {
  float mag01 = gs.mag;
  v3 g_org, g_rt, g_up, g_fw;
  pose_gun_frame(&P, wpn, &g_org, &g_rt, &g_up, &g_fw);
  g_gun_girth = GUN_GIRTH_3P;
  // THE BODY RELOADS TOO. Filmed from outside at five stations, an AR magazine change
  // was a man standing perfectly still holding a rifle for 1.27 seconds — the viewmodel
  // had a whole choreography and the third-person figure had nothing, which is the
  // first/third-person divergence in its purest form.
  if (gs.arc + gs.cyc + gs.hand + gs.bhand > 0.001f) {
    const gun_pose_desc_t *erg = gun_pose_desc(wpn);
    // Cant about the BORE, pivoting at the trigger grip so the hand that stays on the
    // weapon does not have to travel.
    float cant = gs.arc * -0.46f + gs.cyc * (wpn == WPN_SR ? 0.52f : 0.20f);
    float lower = gs.arc * 0.070f;      // off the shoulder, down and inboard
    v3 piv = P.wri[1];
    v3 nu = v3_add(v3_scale(g_up, cosf(cant)), v3_scale(g_rt, sinf(cant)));
    v3 nr = v3_add(v3_scale(g_rt, cosf(cant)), v3_scale(g_up, -sinf(cant)));
    v3 off = v3_add(v3_scale(g_up, -lower),
                    v3_scale(g_fw, -gs.arc * 0.050f - gs.cyc * 0.030f));
    v3 rel = v3_sub(g_org, piv);
    float ra_ = v3_dot(rel, g_rt), rb = v3_dot(rel, g_up), rc_ = v3_dot(rel, g_fw);
    g_org = v3_add(v3_add(piv, off),
                   v3_add(v3_scale(nr, ra_),
                          v3_add(v3_scale(nu, rb), v3_scale(g_fw, rc_))));
    // Apply the action's rigid transform to the rifle and both authored wrists before
    // either hand leaves its station; this preserves the grip pivot and rail/palm
    // clearance.
    for (int h = 0; h < 2; h++) {
      rel = v3_sub(P.wri[h], piv);
      ra_ = v3_dot(rel, g_rt); rb = v3_dot(rel, g_up); rc_ = v3_dot(rel, g_fw);
      P.wri[h] = v3_add(v3_add(piv, off),
                        v3_add(v3_scale(nr, ra_),
                               v3_add(v3_scale(nu, rb), v3_scale(g_fw, rc_))));
    }
    g_rt = nr; g_up = nu;
    // Re-solve both elbows from wherever the hands end up: a wrist that moves
    // without its elbow following is an arm that stretches, which is the defect
    // fig_hand's own reach clamp exists to survive rather than to hide.
    v3 pole0 = v3_add(v3_scale(Rc, -0.6f), v3_scale(spine, -0.8f));
    v3 pole1 = v3_add(v3_scale(Rc, 0.6f), v3_scale(spine, -0.8f));
    if (gs.hand > 0.001f) {   // support hand -> the magazine
      v3 rc, ra; float rr;
      gun_hold(wpn, GH_RELD, g_org, g_rt, g_up, g_fw, &rc, &ra, &rr);
      rc = v3_add(rc, v3_add(v3_scale(g_up, -gs.mag * GUN_MAG_TRAVEL[wpn][0] * GUN_GIRTH_3P),
                             v3_scale(g_rt, -gs.mag * GUN_MAG_TRAVEL[wpn][1] * GUN_GIRTH_3P)));
      v3 route_offset = gun_reload_route_offset(
          erg, g_rt, g_up, g_fw, gs.hand, GUN_GIRTH_3P);
      // The wrist stands off the magazine on the side the arm comes from, the
      // same construction the viewmodel uses — the hand is a fixed size and it
      // is the CYLINDER that has to be reachable, not the palm that stretches.
      v3 br = v3_sub(P.wri[0], rc);
      br = v3_sub(br, v3_scale(ra, v3_dot(br, ra)));
      br = v3_dot(br, br) > 1e-8f ? v3_norm(br) : v3_scale(g_rt, -1.0f);
      gun_hold_profile_t mp = gun_mag_profile(wpn, g_rt, g_up, g_fw,
                                              GUN_GIRTH_3P);
      float contact_r = gun_profile_radius(&mp, ra, br);
      v3 want = v3_add(rc, v3_add(v3_scale(ra, -0.046f * GUN_GIRTH_3P),
                                  v3_scale(br, contact_r + 0.072f)));
      P.wri[0] = v3_add(v3_lerp(P.wri[0], want, gs.hand), route_offset);
    }
    if (gs.bhand > 0.001f && wpn == WPN_SR) {  // trigger hand -> the bolt knob
      v3 knob = gun_bolt_knob(gs.bolt, g_org, g_rt, g_up, g_fw, GUN_GIRTH_3P);
      v3 want = v3_add(knob, gun_pose_offset(g_rt, g_up, g_fw,
                                             erg->bolt_wrist, GUN_GIRTH_3P));
      P.wri[1] = v3_lerp(P.wri[1], want, gs.bhand);
    }
    P.elb[0] = ik_solve(P.sho[0], P.wri[0], ANIM_UARM, ANIM_FARM, pole0);
    P.elb[1] = ik_solve(P.sho[1], P.wri[1], ANIM_UARM, ANIM_FARM, pole1);
  }
  {
    probe("gun");
    int gv0 = g_scene_floats;
    gun_build(wpn, g_org, g_rt, g_up, g_fw, mag01, gs.bolt);
    // Publish the model's own below-origin depth, once per weapon per session.
    if (g_gun_low[wpn] == 0.0f && mag01 <= 0.001f && g_fig_diss == 0.0f &&
        gs.arc <= 0.001f) {
      // Relative to the POSE ANCHOR `P.gun_o`, not to `g_org`: pose_gun_frame puts
      // GUN_GRIP between the two, and the death pose can only place the anchor.
      float lo = 0.0f;
      for (int f = gv0; f + VERT_FLOATS <= g_scene_floats; f += VERT_FLOATS) {
        v3 p = {{g_scene[f], g_scene[f + 1], g_scene[f + 2]}};
        float d = v3_dot(v3_sub(p, P.gun_o), g_up);
        if (d < lo) lo = d;
      }
      // Back out of world units into model units.
      if (lo < 0.0f) g_gun_low[wpn] = -lo / g_gun_girth;
    }
  }
  fig_arms(&P, &kt, Rc, spine, g_org, g_rt, g_up, g_fw, wpn,
           A->slide_s, A->slide_dir, gs, relaxed);
  g_gun_girth = 1.0f;
}

static void scene_draw_figure(const anim_t *A, v3 pos, float yaw,
                              const death_t *dth, float alpha,
                              float flash01, v3 flash_col, int wpn,
                              gunanim_t gs) {
  // Both consumers of `pos` below are asking where this figure's GROUND is, and an air
  // crouch folds 0.60 m of leg tuck into the entity origin in one tick (see
  // anim_t.root_off).
  pos.y -= A->root_off;
  {  // Profile resolution from distance: full detail inside 8 m, then two steps.
    float dx = pos.x - g_cam_eye.x, dy = pos.y + 1.0f - g_cam_eye.y,
          dz = pos.z - g_cam_eye.z;
    float d2 = dx * dx + dy * dy + dz * dz;
    g_fig_k = d2 < 36.0f ? 16 : d2 < 144.0f ? 12 : d2 < 484.0f ? 8 : 6;
    // A CORPSE IS SCENERY AND PAYS SCENERY'S PRICE. It is not something the player aims
    // at, it is about to come apart, and there can be several of them on the floor at
    // once on top of a full 20-bot brawl — measured, eight at the distance tier they
    // happened to be in took the scene to 453711 verts of 393216 and DROPPED 154599,
    // which is holes in whatever was drawn last.
    if (g_fig_kcap && g_fig_k > g_fig_kcap) g_fig_k = g_fig_kcap;
    // OFF SCREEN IS A TIER OF ITS OWN, and it is the coarsest one.
    if (g_cam_cull &&
        !frustum_sees(&g_cam_frustum, (v3){{pos.x, pos.y + 0.9f, pos.z}},
                      SCENE_CHUNK_R) &&
        g_fig_k > 6)
      g_fig_k = 6;   // a CEILING, like g_fig_kcap: it may never raise a tier
  }
  {
    v3 bc = {{pos.x, pos.y + 0.9f, pos.z}};
    // NOT BUILT AT ALL. Culling the DRAW still pays for the vertices: the
    // shadow pass reads the whole buffer, and scene_build is the one part of
    // the frame that is pure main-thread CPU. A body outside the frame whose
    // shadow segment is also outside it contributes nothing to any pass.
    if (g_cam_cull && !frustum_sees(&g_cam_frustum, bc, SCENE_CHUNK_R) &&
        !frustum_sees_cap(&g_cam_frustum, bc, shadow_foot(bc), SCENE_CHUNK_R))
      return;
    scene_chunk_begin(bc, SCENE_CHUNK_R);
  }
  // Interpolate the solved skeleton between the last two ticks.
  pose_t P;
  {
    const float *p0 = (const float *)&A->prev_pose;
    const float *p1 = (const float *)&A->pose;
    float *po = (float *)&P;
    for (unsigned k = 0; k < sizeof(pose_t) / sizeof(float); k++)
      po[k] = p0[k] + (p1[k] - p0[k]) * alpha;
    // The FP body rides behind the camera: translate the POSITIONS only —
    // the flat-array layout is the same property the death topple leans on.
    if (g_fig_fp)
      for (int k = 0; k < POSE_NPOS; k++) {
        po[k * 3 + 0] += g_fig_fp_off.x;
        po[k * 3 + 1] += g_fig_fp_off.y;
        po[k * 3 + 2] += g_fig_fp_off.z;
      }
  }
  // The corpse solve (gravity drape + tip + settle) rewrites the interpolated
  // pose in place; a living figure pays nothing for it, not even the ground ray.
  if (dth && dth->slack > 0.0001f) fig_death_pose(&P, dth, pos, yaw, wpn);

  // No painted blob under the figure any more: the sun casts a real silhouette now,
  // which lands where the light says instead of always straight down, tracks a jump on
  // its own, and shows the pose.
  g_fig_y = pos.y;
  kit_t kt = fig_kit(flash01, flash_col);
  v3 Rc = v3_norm(P.right);
  v3 spine = v3_norm(v3_sub(P.chest, P.pelvis));
  v3 prt = v3_norm(v3_sub(P.hip[1], P.hip[0]));      // pelvis right

  fig_torso(&P, &kt, Rc, spine, prt);
  // Neck + head + helmet, all in the look frame — unless this build IS the
  // head's own point of view.
  if (!g_fig_fp) sdf_head(A, P, kt, Rc, spine);
  sdf_legs(P, kt, spine, prt);
  if (!g_fig_fp)
    sdf_rifle(A, P, kt, Rc, spine, wpn, gs,
              dth && dth->slack > 0.0001f);
  probe(NULL);
  ao_off();
  scene_chunk_end();
}

// The renderer's view of a ragdoll, interpolated.
static death_t rag_death(const rag_t *r, float prev_slack, float slack,
                         float alpha) {
  return (death_t){prev_slack + (slack - prev_slack) * alpha,
                   r->prev_tilt + (r->tilt - r->prev_tilt) * alpha,
                   r->prev_slam + (r->slam - r->prev_slam) * alpha,
                   r->axis,
                   r->prev_yaw + (r->yaw - r->prev_yaw) * alpha,
                   (r->prev_tilt - r->prev_lag) +
                       ((r->tilt - r->lag) - (r->prev_tilt - r->prev_lag)) * alpha};
}

static void scene_draw_bot(const bot_t *b, float alpha) {
  v3 pos = v3_lerp(b->prev_pos, b->pos, alpha);
  float yaw = b->prev_yaw + (b->yaw - b->prev_yaw) * alpha;
  death_t dth = rag_death(&b->rag, b->prev_topple, b->topple, alpha);
  float flash01 = ent_flash01(b->flash, b->flash_head);
  v3 fc = b->flash_head ? (v3){{1.0f, 0.22f, 0.14f}} : (v3){{1, 1, 1}};
  // b->wp.cur, not a hardwired WPN_AR: today masked by bot_spawn always
  // issuing the AR — but any figure drawn through this path holding the SR
  // would render an AR, and the far SR report bake becomes reachable with it.
  scene_draw_figure(&b->anim, pos, yaw, &dth, alpha, flash01, fc, b->wp.cur,
                    gun_anim(&b->wp));
}

// Perpendicular frame for marks/tracers whose only anchor is one direction.
static void axis_frame(v3 n, v3 *t1, v3 *t2) {
  v3 up = n.y > 0.9f || n.y < -0.9f ? (v3){{1, 0, 0}} : (v3){{0, 1, 0}};
  *t1 = v3_norm(v3_cross(n, up));
  *t2 = v3_cross(n, *t1);
}

// One impact crater: a MARK_K-segment fan of four concentric rings, every vertex
// RADIALLY CLAMPED into the face rectangle the mark was stamped on. Outward:
//   0 .. MARK_R1   flat dark core         — the hole
//   MARK_R2        pale spall / dust ring — fresh fractured material
//   1.0            the host surface       — no silhouette step, no sticker edge
//
// The clip is the whole trick and it is a construction guarantee rather than a
// tuning: every vertex is centre + dir*r with r <= the ray's exit distance from
// the window, the window is convex and CONTAINS the centre, so every point on
// the ray at r <= lim is inside it and a triangle with three vertices inside a
// convex set is inside it. The mark cannot leave the face, at any radius, any
// wobble, any K. It costs nothing: the straight edges come out exact (vertices
// clamped by the same half-plane share a u or a v, so the polyline through them
// IS the edge) and only a window CORNER is chamfered, by 1-cos(pi/K) = 7.6% of
// the local radius, which is inside the rim's own +/-14% wobble.
static void scene_mark(const mark_t *m) {
  v3 t1, t2;
  axis_frame(m->nrm, &t1, &t2);
  const float rf[4] = {0.0f, MARK_R1, MARK_R2, 1.0f};
  // The birth bloom. `bl` is 1 at the stamp tick and 0 once settled; it is a squared
  // ease so most of the collapse happens in the first few frames, which is what a spall
  // cloud does.
  float age = (float)(g_tick - m->t0) / (float)MARK_BLOOM_T;
  float bl = age >= 1.0f ? 0.0f : (1.0f - age) * (1.0f - age);
  float grow = 1.0f + (MARK_BLOOM_K - 1.0f) * bl;
  float hot  = 1.0f + (MARK_BLOOM_C - 1.0f) * bl;
  v3 col[4];
  col[0] = col[1] = v3_scale(m->surf, MARK_CORE_K);
  col[2] = v3_scale(m->surf, MARK_HALO_K * hot);
  col[3] = m->surf;                       // ring 3 IS the host: no edge at all
  float a0 = (float)(m->seed & 1023u) * (F_TAU / 1024.0f);
  v3 prev[4] = {{{0, 0, 0}}, {{0, 0, 0}}, {{0, 0, 0}}, {{0, 0, 0}}};
  int prev_live = 0;
  for (int i = 0; i <= MARK_K; i++) {
    float a = a0 + (float)i * (F_TAU / MARK_K);
    float cu = cosf(a), cv = sinf(a);
    // Distance from the centre to the window along this ray: a min over the two half-
    // planes the ray actually runs toward.
    float lim = 1e9f, l;
    if (cu > 1e-6f) lim = m->u1 / cu; else if (cu < -1e-6f) lim = m->u0 / cu;
    if (cv > 1e-6f) { l = m->v1 / cv; if (l < lim) lim = l; }
    else if (cv < -1e-6f) { l = m->v0 / cv; if (l < lim) lim = l; }
    // Wobble on the spall and the rim only — a bullet hole is round and its spall is
    // not.
    float jt = MARK_JIT_LO + MARK_JIT_SPAN *
               (float)((fx_hash(m->seed ^ (uint32_t)(i % MARK_K) * 0x9E3779B9u)
                        >> 4) & 255) / 255.0f;
    int live = lim > 1e-4f;         // pinched onto the centre: no area
    v3 cur[4];
    for (int j = 0; j < 4; j++) {
      float r = m->rad * grow * rf[j] * (j >= 2 ? jt : 1.0f);
      if (r > lim) r = lim;
      cur[j] = v3_add(m->pos, v3_add(v3_scale(t1, cu * r), v3_scale(t2, cv * r)));
    }
    if (i && (live || prev_live)) {
      // Ring 0 has radius 0, so the innermost band is a FAN, not a degenerate strip.
      mat_set(MARK_ROUGH, 0.0f);
      scene_push(prev[0], m->nrm, col[0]);
      scene_push(prev[1], m->nrm, col[1]);
      scene_push(cur[1],  m->nrm, col[1]);
      for (int j = 1; j < 3; j++) {
        mat_set(MARK_ROUGH + (m->rough - MARK_ROUGH) * (float)j * 0.5f, 0.0f);
        scene_push(prev[j],     m->nrm, col[j]);
        scene_push(prev[j + 1], m->nrm, col[j + 1]);
        scene_push(cur[j + 1],  m->nrm, col[j + 1]);
        scene_push(prev[j],     m->nrm, col[j]);
        scene_push(cur[j + 1],  m->nrm, col[j + 1]);
        scene_push(cur[j],      m->nrm, col[j]);
      }
    }
    for (int j = 0; j < 4; j++) prev[j] = cur[j];
    prev_live = live;
  }
}

// The additive FX pass runs the world shader with uEmis set, which puts the
// vertex color on screen untouched — so the normal these carry is never read.
static const v3 FX_N = {{0.0f, 1.0f, 0.0f}};

// Camera-facing tapered segment: width and colour interpolate from a to b —
// the building block for streaks and flash petals. fx_ribbon is the constant
// special case, kept for the callers that want a plain bar.
static void fx_seg(v3 a, v3 b, v3 eye, float wa, float wb, v3 ca, v3 cb) {
  v3 seg = v3_add(b, v3_scale(a, -1.0f));
  v3 mid = v3_add(a, v3_scale(seg, 0.5f));
  v3 side = v3_cross(seg, v3_add(eye, v3_scale(mid, -1.0f)));
  float l = sqrtf(v3_dot(side, side));
  if (l < 1e-6f) return;
  side = v3_scale(side, 1.0f / l);
  v3 sa = v3_scale(side, wa), sb = v3_scale(side, wb);
  scene_push(v3_add(a, sa), FX_N, ca);
  scene_push(v3_add(b, sb), FX_N, cb);
  scene_push(v3_sub(b, sb), FX_N, cb);
  scene_push(v3_add(a, sa), FX_N, ca);
  scene_push(v3_sub(b, sb), FX_N, cb);
  scene_push(v3_sub(a, sa), FX_N, ca);
}

static void fx_ribbon(v3 a, v3 b, v3 eye, float w, v3 col) {
  fx_seg(a, b, eye, w, w, col, col);
}

// The near-camera ease every burst shares (the HITSPARK_NEAR/FADE law): returns 0
// inside the cull radius — the caller skips the particle — else writes the eased 0..1
// factor.
static int fx_near_ease(v3 pos, v3 eye, float *nf) {
  v3 dq = v3_sub(pos, eye);
  float dq2 = v3_dot(dq, dq);
  if (dq2 < HITSPARK_NEAR * HITSPARK_NEAR) return 0;
  *nf = f_clamp((sqrtf(dq2) - HITSPARK_NEAR) / (HITSPARK_FADE - HITSPARK_NEAR),
                0.0f, 1.0f);
  return 1;
}

// Warm-biased halo round a hot core.
static v3 fx_warm_halo(v3 col) {
  return (v3){{col.x * 0.30f, col.y * 0.060f, col.z * 0.025f}};
}

// TEARDROP, not a bar. fx_seg interpolates colour and width ALONG the segment but the
// cross-section is flat, so one quad from head to tail has a BLUNT full-width leading
// edge — at 3x zoom that edge made every spark read as a little orange arrowhead and
// every halo as a solid pale wedge.
static void fx_teardrop(v3 pos, v3 tail, v3 eye, float wc, float wh,
                        v3 col, v3 halo) {
  v3 dv = v3_sub(tail, pos);
  v3 mid = v3_add(pos, v3_scale(dv, 0.25f));
  fx_seg(pos, mid, eye, wh * 0.14f, wh, v3_scale(halo, 0.55f), halo);
  fx_seg(mid, tail, eye, wh, wh * 0.02f, halo, (v3){{0.0f, 0.0f, 0.0f}});
  fx_seg(pos, mid, eye, wc * 0.30f, wc, col, col);
  fx_seg(mid, tail, eye, wc, wc * 0.30f, col, v3_scale(col, 0.20f));
}

// Deterministic per-shot variety with no RNG stream: flash petals rotate and
// jitter off a hash of the shot index, so a burst flickers frame to frame and
// two runs of one script stay byte-identical.
static uint32_t fx_hash(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352du;
  x ^= x >> 15; x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

// One muzzle blast. Rewritten 2026-08-25 from the six-spike star: a clean radial
// star of thin straight bars is the CARTOON glyph for a gunshot — real flash
// photography of a braked rifle shows a fat layered FIREBALL at the crown, two
// hard lateral JETS out of the brake's ports (both of this game's rifles wear
// ported brakes, and the ports own the real flash's shape), a couple of ragged
// short flame lobes, ballistic sparks, and then a frame or three of dim dust
// glow where the blast lit the air — the flame is one frame, the impression is
// five. Everything is stateless off (seed, f): two runs of one script stay
// byte-identical, and a windowed session can render any frame rate through it.
// t1/t2 are kept for the degenerate fallback; the jet frame is rebuilt
// HORIZONTAL inside (cross with world up), because axis_frame's basis is
// arbitrary and a brake never vents into the dirt.
static void fx_flash(v3 pos, v3 fw, v3 t1, v3 t2, v3 eye, float s, float f,
                     uint32_t seed, int wpn) {

  float sr = wpn == WPN_SR ? 1.0f : 0.0f;      // the .338 blast dwarfs the 5.56's
  // The FLAME lives in the first half of the life; the tail of the life is
  // sparks and dust glow only. f runs 1 (birth) -> 0.
  float flame = f_clamp((f - 0.50f) / 0.50f, 0.0f, 1.0f);
  float age = 1.0f - f;
  v3 side = v3_cross(fw, (v3){{0, 1, 0}});
  float sl = v3_dot(side, side);
  side = sl > 1e-6f ? v3_scale(side, 1.0f / sqrtf(sl)) : t1;
  v3 vup = v3_cross(side, fw);
  (void)t2;
  if (flame > 0.001f) {
    float fl2 = flame * flame;
    // FIREBALL: a dense fan of short ragged ribbons, NEVER a fat constant
    // quad. An additive quad's rim is a hard step (the dust-mote note: "a
    // single quad is a hard-edged RECTANGLE"), and a fireball authored as
    // three of them read as stickers with visible card edges — measured, a
    // +60 red step in two pixels against the sand. Each ribbon FADES TO ZERO
    // at its tip and tapers to a hair at its end, so no edge anywhere carries
    // a step; the overlap at the centre is what makes the hot core, and only
    // that overlap crosses the bloom knee — a single authored blob at 3.1
    // was the largest flat-255 mass in the game.
    v3 fhot = v3_scale((v3){{2.05f, 1.70f, 1.20f}}, fl2);
    v3 fzero = {{0, 0, 0}};
    v3 bc = v3_add(pos, v3_scale(fw, 0.08f * s));
    for (int k = 0; k < 9; k++) {
      uint32_t hk = fx_hash(seed * 17u + (uint32_t)k * 29u);
      float ang = (float)(hk & 255) * (F_TAU / 256.0f);
      float lr = s * (0.11f + 0.24f * (float)((hk >> 8) & 255) / 255.0f);
      v3 rd = v3_add(v3_scale(side, cosf(ang)), v3_scale(vup, sinf(ang)));
      fx_seg(v3_add(bc, v3_scale(rd, 0.010f * s)), v3_add(bc, v3_scale(rd, lr)),
             eye, s * 0.055f, s * 0.006f, fhot, fzero);
    }
    // ...and a dim burnt-orange halo fan under it, one octave wider. LOW blue
    // on purpose: the desaturated middle band is what read salmon/plastic.
    v3 fhalo = v3_scale((v3){{0.55f, 0.21f, 0.055f}}, fl2 * 0.6f);
    for (int k = 0; k < 5; k++) {
      uint32_t hk = fx_hash(seed * 23u + (uint32_t)k * 31u);
      float ang = (float)(hk & 255) * (F_TAU / 256.0f);
      float lr = s * (0.20f + 0.30f * (float)((hk >> 8) & 255) / 255.0f);
      v3 rd = v3_add(v3_scale(side, cosf(ang)), v3_scale(vup, sinf(ang)));
      fx_seg(v3_add(bc, v3_scale(rd, 0.02f * s)), v3_add(bc, v3_scale(rd, lr)),
             eye, s * 0.085f, s * 0.010f, fhalo, fzero);
    }
    // BRAKE JETS: 2-3 staggered ribbons per side, each a hashed length, so
    // the leading edge is ragged — one clean taper read as a decal wedge on
    // the .338. Canted a shade forward; the sniper's run half again as long.
    v3 jhot = v3_scale((v3){{2.10f, 1.45f, 0.68f}}, fl2);
    int nj = 2 + (int)sr;
    for (int j = 0; j < 2; j++) {
      float js = j ? 1.0f : -1.0f;
      for (int m = 0; m < nj; m++) {
        uint32_t hj = fx_hash(seed * 3u + (uint32_t)(j * 5 + m) * 41u);
        float jl = s * (0.55f + 0.42f * sr) *
                   (0.55f + 0.45f * (float)(hj & 255) / 255.0f);
        float cant = 0.20f + 0.20f * (float)((hj >> 8) & 255) / 255.0f;
        float lift = 0.16f * ((float)((hj >> 16) & 255) / 255.0f - 0.5f);
        v3 jd = v3_norm(v3_add(v3_scale(side, js),
                v3_add(v3_scale(fw, cant), v3_scale(vup, lift))));
        fx_seg(v3_add(pos, v3_scale(jd, 0.05f * s)), v3_add(pos, v3_scale(jd, jl)),
               eye, s * (0.075f - 0.015f * (float)m), s * 0.008f, jhot, fzero);
      }
    }
    // Forward cone: short — the bore's own blast, mostly eaten by the brake,
    // longer on the .338. Fades to zero like everything else.
    fx_seg(v3_add(pos, v3_scale(fw, 0.04f * s)),
           v3_add(pos, v3_scale(fw, s * (0.80f + 0.45f * sr))),
           eye, s * 0.11f, s * 0.012f, jhot, fzero);
  }
  // SPARKS: a few incandescent grains thrown forward-out, ballistic off the
  // seed — they outlive the flame and are what the eye keeps. FEWER and
  // FATTER than the first cut: seven 1-px dots sat under the film grain's
  // own amplitude and the whole decay half of the life read empty.
  int ns = 4 + (int)(2.0f * sr);
  for (int k = 0; k < ns; k++) {
    uint32_t hk = fx_hash(seed * 13u + (uint32_t)k * 101u);
    float r1 = (float)(hk & 1023) / 1023.0f;
    float r2 = (float)((hk >> 10) & 1023) / 1023.0f;
    float r3 = (float)((hk >> 20) & 1023) / 1023.0f;
    v3 sd2 = v3_norm(v3_add(v3_scale(fw, 0.55f + 0.9f * r1),
             v3_add(v3_scale(side, (r2 - 0.5f) * 1.1f),
                    v3_scale(vup, (r3 - 0.5f) * 0.8f))));
    float sp = s * (2.4f + 3.2f * r1);
    v3 p = v3_add(pos, v3_scale(sd2, 0.12f * s + sp * age));
    p.y -= 2.6f * s * age * age;                        // the grain falls
    v3 scol = v3_scale((v3){{2.45f, 1.20f, 0.30f}}, f * f * (0.6f + 0.4f * r2));
    fx_teardrop(p, v3_sub(p, v3_scale(sd2, s * (0.14f + 0.14f * age))), eye,
                s * 0.019f, s * 0.042f, scol, fx_warm_halo(scol));
  }
  // DUST GLOW: after the flame dies — and ONLY after it: at tick 0 the puff
  // doubled the flash's footprint and fogged the aim point, which is the one
  // place this game never puts haze it didn't price. A soft CROSS of four
  // tapered ribbons fading to zero (a single card here showed its rectangle
  // against the trousers in third person), dim, expanding, cooling.
  if (age > 0.45f) {
    float gs = s * (0.70f + 1.40f * age);
    v3 gcol = v3_scale((v3){{0.115f, 0.050f, 0.016f}}, f * f);
    v3 gzero = {{0, 0, 0}};
    v3 gc = v3_add(pos, v3_add(v3_scale(fw, 0.30f * s), v3_scale(vup, 0.12f * s * age)));
    for (int k = 0; k < 4; k++) {
      float ang = 0.7854f + (float)k * 1.5708f +
                  0.5f * ((float)((fx_hash(seed * 29u + (uint32_t)k) >> 5) & 255) / 255.0f - 0.5f);
      v3 rd = v3_add(v3_scale(side, cosf(ang)), v3_scale(vup, sinf(ang)));
      fx_seg(gc, v3_add(gc, v3_scale(rd, gs)), eye, gs * 0.36f, gs * 0.04f,
             gcol, gzero);
    }
  }
}

static void scene_build(const player_t *p, float alpha, int show_player,
                        int fp_body) {
  (void)p;   // the roster below covers human 0; parameter kept for callers
  scene_reset();
  // Every human body: the LOCAL one only when the camera is not first-person;
  // the other seven ALWAYS — unseen here they also cast no shadow, because the depth
  // pass draws this same list.
  for (int h = 0; h < MAX_HUMANS; h++) {
    if (!g_humans_on[h]) continue;
    const player_t *hp = &g_players[h];
    int local = ent_of_human(h) == g_local_ent;
    if (local && !show_player) continue;
    if (!hp->alive) continue;
    scene_draw_figure(&g_player_anim[h], v3_lerp(hp->prev_pos, hp->pos, alpha),
                      hp->yaw, NULL, alpha, ent_flash01(hp->flash, hp->flash_head),
                      hp->flash_head ? (v3){{1.0f, 0.22f, 0.14f}} : (v3){{1, 1, 1}},
                      hp->wp.cur, gun_anim(&hp->wp));
  }
  for (int i = 0; i < g_match.nbots; i++)
    if (g_bots[i].active) scene_draw_bot(&g_bots[i], alpha);
  // The bodies the entities left behind.
  for (int c = 0; c < MAX_CORPSES; c++) {
    const corpse_t *cp = &g_corpses[c];
    if (!cp->used) continue;
    v3 cpos = v3_lerp(cp->prev_pos, cp->pos, alpha);
    death_t dth = rag_death(&cp->rag, cp->prev_slack, cp->slack, alpha);
    // FRONT-LOADED, because the burst is instantaneous and the geometry has to leave
    // WITH it.
    g_fig_diss = cp->life < CORPSE_DISS
               ? sqrtf(1.0f - (float)cp->life / (float)CORPSE_DISS) : 0.0f;
    g_fig_diss_o = cpos;
    g_fig_diss_seed = cp->seed;
    g_fig_kcap = CORPSE_K;
    scene_draw_figure(&cp->anim, cpos, cp->yaw, &dth, alpha, 0.0f,
                      (v3){{1, 1, 1}}, cp->wpn, cp->gs);
  }
  g_fig_diss = 0.0f;
  g_fig_kcap = 0;
  for (int i = 0; i < MAX_MARKS; i++)
    if (g_marks[i].used) {
      // Rate 0 makes ao_at return the floor for every vertex, i.e. exactly the
      // host face's own interpolated occlusion, resolved once at spawn.
      ao_set(0.0f, 0.0f, g_marks[i].ao);
      scene_mark(&g_marks[i]);
    }
  ao_off();
  // THE PLAYER'S OWN BODY, FIRST PERSON: the same figure, the same pose, the
  // same tick — minus head, arms and rifle (g_fig_fp). Appended LAST so the
  // shadow pass can stop in front of it (g_scene_cast_floats): the partial
  // body may be seen, never its shadow.
  g_scene_cast_floats = g_scene_floats;
  if (fp_body && g_local_ent >= 0) {
    int lh = human_of_ent(g_local_ent);
    if (lh >= 0 && lh < MAX_HUMANS && g_humans_on[lh] && g_players[lh].alive) {
      const player_t *hp = &g_players[lh];
      g_fig_fp = 1;
      // Keep the visual-only proxy behind the near plane when looking down. At steep
      // pitch the camera otherwise enters the upper torso and exposes a flat clipped
      // cross-section instead of the player's chest/legs. Normal look angles retain
      // the authored position; crouch still brings the knees into view.
      float down = f_smoothstep(0.55f, 1.25f, -hp->pitch);
      float bk = 0.21f - 0.04f * g_player_anim[lh].crouch_s + 0.04f * down;
      g_fig_fp_off = (v3){{-sinf(hp->yaw) * bk, -0.08f * down,
                           cosf(hp->yaw) * bk}};
      scene_draw_figure(&g_player_anim[lh],
                        v3_lerp(hp->prev_pos, hp->pos, alpha), hp->yaw, NULL,
                        alpha, ent_flash01(hp->flash, hp->flash_head),
                        hp->flash_head ? (v3){{1.0f, 0.22f, 0.14f}}
                                       : (v3){{1, 1, 1}},
                        hp->wp.cur, gun_anim(&hp->wp));
      g_fig_fp = 0;
    }
  }
}

// The player's first-person tracer re-anchor: render_frame FOV-remaps the viewmodel's
// published muzzle into world-projection space before fx_build runs, so the streak's
// start lands on the same screen pixel as the drawn barrel tip.
static v3  g_fx_muzzle;
static int g_fx_muz_live;
static int g_fx_vm_flash;  // this frame's viewmodel pass will draw the
                           // player's flash star — the world pass must not
                           // draw a second one at the third-person muzzle

// Additive light pass: tracer streaks (hot core cooling downrange under a wide halo,
// the tail receding toward the impact so a burst reads as rounds LEAVING rather than a
// laser held on the wall), muzzle-flash stars for every shooter the world pass can see,
// and impact sparks (motion-stretched embers cooling from white-hot to orange).
static v3 fx_cool(float a1, float knee, v3 hot, v3 mid, v3 cold) {
  return a1 < knee ? v3_lerp(hot, mid, a1 / knee)
                   : v3_lerp(mid, cold, (a1 - knee) / (1.0f - knee));
}

// ---------------------------------------------------------------------------
// AIRBORNE DUST — the grains the haze is made of, close enough to resolve.
// ---------------------------------------------------------------------------
// The aerial perspective says the air has substance; this says so at arm's
// length. Everything else about the atmosphere is a smooth function, and a
// smooth function is exactly what the eye stops believing after a second — what
// sells air is a handful of individual things drifting through it.
//
// NO STATE, NO RNG, NO PARTICLE POOL. The field is a lattice hashed off its own
// integer cell, so it is infinite, seamless and free to walk: the window of
// cells follows the camera and the whole lattice translates on the arena's own
// wind, which means a mote has parallax, a heading, and no spawn or death to
// pop at. It is also a pure function of g_tick, so a harness screenshot at a
// given tick is the same screenshot every time — the same reason the grain is
// seeded off the tick and not off the rendered-frame counter.
#define DUST_CELL   1.02f   // metres between lattice cells
#define DUST_RING   4       // cells each way: 9^3 candidates, ~three in five survive
#define DUST_FAR    9.0f    // past this a mote is under a pixel and only aliases

static uint32_t dust_hash(int x, int y, int z) {
  uint32_t h = (uint32_t)x * 0x8DA6B343u ^ (uint32_t)y * 0xD8163841u
             ^ (uint32_t)z * 0xCB1AB31Fu;
  h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
  return h;
}
static float dust_f(uint32_t h) { return (float)(h >> 8) * (1.0f / 16777216.0f); }

// The SAME Schlick lobe haze_rad uses in the shader, at the same g. It is
// written twice because it has to live on both sides of the GL boundary — the
// haze is a per-pixel integral and this is a per-mote scalar — and the two
// agreeing is what stops the individual grains from being brightest somewhere
// the air around them is not.
static float dust_phase(float mu) {
  float den = 1.0f - 0.792f * mu;
  return 0.0293337f / fmaxf(den * den, 1.0e-4f);
}

// Is this point standing in the sun? One ray against the solids, first hit
// wins. A mote that says no still draws at a fraction rather than vanishing:
// dust in shade is lit by the sky, and a hard zero would make the boundary of
// every shadow flicker as the field drifts across it.
static float dust_sunlit(v3 p) {
  for (int i = 0; i < g_num_solids; i++) {
    v3 n;
    if (ray_aabb(p, g_sun.dir, g_solids[i].min, g_solids[i].max, &n) > 0.0f)
      return 0.13f;
  }
  return 1.0f;
}

static void fx_dust(v3 eye) {
  float dens = atmo_haze();
  if (dens <= 0.0f) return;
  float t = (float)g_tick * (float)TICK_DT;
  v3 wd = wind_dir();
  // The lattice's own displacement. Subtracting it before choosing the cell
  // window and adding it back to the mote is what makes the FIELD move while
  // the WINDOW stays on the camera.
  v3 flow = {{wd.x * 0.42f * t, 0.11f * sinf(t * 0.31f), wd.z * 0.42f * t}};
  v3 base = v3_sub(eye, flow);
  int c0 = (int)floorf(base.x / DUST_CELL);
  int c1 = (int)floorf(base.y / DUST_CELL);
  int c2 = (int)floorf(base.z / DUST_CELL);
  for (int ix = c0 - DUST_RING; ix <= c0 + DUST_RING; ix++)
  for (int iy = c1 - DUST_RING; iy <= c1 + DUST_RING; iy++)
  for (int iz = c2 - DUST_RING; iz <= c2 + DUST_RING; iz++) {
    uint32_t h = dust_hash(ix, iy, iz);
    if ((h & 255u) > 150u) continue;            // ~three cells in five carry one
    v3 p = {{((float)ix + dust_f(h)) * DUST_CELL + flow.x,
             ((float)iy + dust_f(h * 2654435761u)) * DUST_CELL + flow.y,
             ((float)iz + dust_f(h * 40503u)) * DUST_CELL + flow.z}};
    // Dust settles: nothing hangs at head height over a wall, and nothing
    // hangs underground either.
    if (p.y < 0.05f || p.y > 4.2f) continue;
    v3 dv = v3_sub(p, eye);
    float d2 = v3_dot(dv, dv);
    if (d2 < 0.36f || d2 > DUST_FAR * DUST_FAR) continue;
    float dist = sqrtf(d2);
    v3 vd = v3_scale(dv, 1.0f / dist);
    // A GRAIN IS BRIGHTEST WHEN IT IS BETWEEN YOU AND THE SUN, by a factor of
    // twenty. That is the whole reason dust is something you notice on the way
    // out of a doorway and never on the way in, and it is the same forward
    // lobe the haze band is built on.
    // The 0.05 floor is the sky lighting the same grain: without it the lobe
    // falls to a fiftieth away from the sun and the air is empty everywhere
    // but one cone, which is not what dust does — the ratio is what carries
    // the effect, not the extinction.
    float amp = 0.05f + dust_phase(v3_dot(vd, g_sun.dir));
    // Twinkle: a triangle wave off the mote's own hash, squared. A grain is a
    // faceted crystal tumbling in the air, so what the eye actually sees is it
    // catching and losing the sun, not a steady point.
    float tp = (float)((h >> 3) & 1023u) * (1.0f / 1024.0f) + t * 0.37f;
    tp -= floorf(tp);
    float tw = tp < 0.5f ? tp * 2.0f : (1.0f - tp) * 2.0f;
    // Near fade and far fade. The near one keeps a mote from crossing the near
    // plane as a full-screen smear; the far one hands it to the haze, which is
    // the same dust integrated rather than resolved.
    float fade = f_smoothstep(0.6f, 1.4f, dist)
               * (1.0f - f_smoothstep(DUST_FAR * 0.55f, DUST_FAR, dist));
    // tw^3, not tw^2, and the floor drops to a tenth. Grains are not a field of
    // equal dots — most of them sit near the bottom of their cycle and a few
    // are catching the sun at any moment, which is the difference between
    // glitter and falling snow.
    float k = dens * amp * (0.10f + 0.90f * tw * tw * tw) * fade * 0.82f;
    // THE THRESHOLD COMES BEFORE THE RAY, and that ordering is the whole cost
    // of this field: the shadow test walks the solid list, and most candidates
    // are already too faint to draw — away from the sun the phase lobe is a
    // twentieth of its peak. Testing first and culling second was ray-marching
    // for motes that were never going to be pushed.
    if (k < 0.004f) continue;
    k *= dust_sunlit(p);
    if (k < 0.004f) continue;
    // Sized to hold about two pixels at any range rather than to a physical
    // diameter: a 30-micron grain is a quarter of a pixel at four metres, and a
    // quarter-pixel additive dot is not a mote, it is a scintillating artefact
    // that MSAA cannot help with.
    // THE NUMBER IS A HALF-WIDTH. fx_seg pushes a + side*w AND a - side*w, so
    // the quad is 2w across and 3.4w long; sizing this as if it were the full
    // width put six-by-ten pixel RECTANGLES in the sky, which the glare chain
    // then bloomed into unmistakable squares.
    // Size varies per mote as well as brightness: one diameter over a whole
    // field is the other half of what made it read as snow.
    float w = 0.0011f * dist * (0.62f + 0.85f * dust_f(h * 668265263u));
    // Drawn as a short streak ALONG the drift, which is what a grain moving
    // through a 120 Hz exposure looks like and what keeps it from reading as a
    // hard dot.
    // A SPINDLE, NOT A BAR. fx_seg interpolates width and colour along the
    // segment but the cross-section is flat, so one quad is a hard-edged
    // rectangle — at these sizes a lit rectangle is exactly what it looks
    // like. Two segments tapering to nothing at both ends give a speck with
    // ends that fade, which is all a two-pixel mote needs.
    v3 str = v3_scale(wd, w * 2.2f);
    v3 a = v3_sub(p, str), b = v3_add(p, str);
    v3 col = v3_scale(g_sun.col, k);
    const v3 z0 = {{0.0f, 0.0f, 0.0f}};
    fx_seg(a, p, eye, w * 0.12f, w, z0, col);
    fx_seg(p, b, eye, w, w * 0.12f, col, z0);
  }
}
