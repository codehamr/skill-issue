// =============================================================================
// harness/proofs_fig.inc  —  probe_capture_figure, figm/figbury/flinch/
// vmtrig_proof.
// Proof: the proofs themselves. One TU: included once by game.c, in
// definition order — moving a definition out is a semantic change.
// Contract: a pose A/B needs two builds with IDENTICAL flags — the anim
// solver amplifies 1 ULP into centimetres, which invalidates any
// pixel-counting probe built at mismatched optimization levels.
// =============================================================================
// CAPTURE BOT 0's FIGURE INTO THE PROBE BUFFER at full profile resolution.
// figm and figbury both want exactly this, and a tier change must not be able
// to make the two proofs measure different figures — g_cam_eye ON the body is
// what forces the finest tier (see the g_cam_cull rule).
static void probe_stage_figure(bot_t *staged, int wpn) {
  const bot_t *subject = &g_bots[0];
  // Stage both selector and solved joints together. Drawing an SR mesh around
  // the live bot's AR-authored wrists makes figm/figbury look weapon-aware while
  // measuring a pose no player can actually render.
  *staged = *subject;
  staged->wp.cur = wpn;
  int idle_ok = gun_pose_idle_ok(&staged->wp, &staged->anim, staged->vel,
                                 staged->grounded, staged->sliding);
  skel_solve(&staged->anim, staged->pos, staged->yaw, staged->pitch,
             wpn, idle_ok, SKEL_POSE_ONLY);
}

static const bot_t *probe_capture_figure(int wpn) {
  static bot_t staged;
  probe_stage_figure(&staged, wpn);
  g_nprobe = 0;
  g_probe_on = 1;
  scene_reset();
  const bot_t *b = &staged;
  g_cam_eye = b->pos;
  scene_draw_figure(&b->anim, b->pos, b->yaw, NULL, 1.0f, 0.0f,
                    (v3){{1, 1, 1}}, wpn, gun_anim(&b->wp));
  g_probe_on = 0;
  return b;
}

// Every figure part's measured extent in mm, the bone lengths, the elbow angles
// with the reach each one solves, and the cheek-weld rows.
static void figm_proof(void) {
  // Measure the figure the player actually sees, part by part, in the figure's own
  // frame (x right, y up from the ground, z forward), in mm.
  bot_t staged[WPN_COUNT];
  for (int wi = 0; wi < WPN_COUNT; wi++) probe_stage_figure(&staged[wi], wi);
  const bot_t *b = probe_capture_figure(WPN_AR);
  fig_head_metrics_t hm = fig_head_metrics();
  printf("figm head chin-mouth=%+.1fmm chin-jaw=%+.1fmm width-ratio=%.3f "
         "lower-face=%.1fmm\n",
         (double)(hm.chin_mouth_front * 1000.0f),
         (double)(hm.chin_jaw_front * 1000.0f),
         (double)hm.chin_jaw_width_ratio,
         (double)(hm.lower_face_len * 1000.0f));
  float cy = cosf(b->yaw), sy = sinf(b->yaw);
  float glo[3] = {1e9f, 1e9f, 1e9f}, ghi[3] = {-1e9f, -1e9f, -1e9f};
  // THE ELBOWS, as angles. "The arm looks awkward" is not actionable and a screenshot
  // cannot answer it: what decides whether a shouldered stance reads is whether the
  // firing arm makes a TRIANGLE, and a triangle is an angle.
  {
    const pose_t *EP = &b->anim.pose;
    float al = ANIM_UARM + ANIM_FARM;
    // ...and how far the PALM has to stretch to bridge whatever the IK
    // could not reach. fig_hand wraps the weapon's published cylinder while
    // the wrist stays where the two-bone solve put it, so the gap between
    // them is drawn as a longer hand — the number that decides whether a
    // grip reads as a grip or as a glove pointing at a rifle.
    for (int wi = 0; wi < WPN_COUNT; wi++) {
      const pose_t *WP = &staged[wi].anim.pose;
      v3 hc, ha, org2, rt2, up2, fw2; float hr;
      pose_gun_frame(WP, wi, &org2, &rt2, &up2, &fw2);
      float gsave = g_gun_girth;
      g_gun_girth = GUN_GIRTH_3P;
      gun_hold(wi, GH_FORE3, org2, rt2, up2, fw2, &hc, &ha, &hr);
      g_gun_girth = gsave;
      v3 d = v3_sub(WP->wri[0], hc);
      float along = v3_dot(d, ha);
      v3 rad = v3_sub(d, v3_scale(ha, along));
      printf("figm palm %-3s support_gap=%.0fmm (along=%.0f radial=%.0f)\n",
             WPN_DEF[wi].prefix, (double)(v3_len(d) * 1000.0f),
             (double)(along * 1000.0f),
             (double)((v3_len(rad) - hr) * 1000.0f));
    }
    // THE WELD, and it is the one thing about an aimed pose a screenshot argues about
    // and cannot settle: is the EYE on the sight line, or is the head merely near the
    // rifle? The sight line is the bore raised by the weapon's own sight height
    // (scaled, because the third-person model draws at GUN_GIRTH_3P), and the eye is
    // measured off it in the WEAPON'S frame — up/side, plus the eye relief along the
    // bore.
    for (int wi = 0; wi < WPN_COUNT; wi++) {
      const pose_t *WP = &staged[wi].anim.pose;
      v3 e, hr_, hu_, hf_;
      pose_head_frame(WP, anim_head_cant(&staged[wi].anim),
                      &e, &hr_, &hu_, &hf_);
      v3 org3, rt3, up3, fw3;
      pose_gun_frame(WP, wi, &org3, &rt3, &up3, &fw3);
      // The APERTURE, not the anchor's cross-section: relief measured from
      // the origin plane says nothing about whether the eye is behind the
      // glass, and the sniper's ocular sits 52 mm BEHIND that plane while
      // the carbine's aperture sits 184 mm in front of it.
      v3 sp3 = v3_add(org3,
                 v3_add(v3_scale(up3, gun_sight_y(wi) * GUN_GIRTH_3P),
                        v3_scale(fw3, gun_sight_z(wi) * GUN_GIRTH_3P)));
      v3 d3 = v3_sub(e, sp3);
      float along = v3_dot(d3, fw3);
      v3 lat = v3_sub(d3, v3_scale(fw3, along));
      // ...and WHERE ALONG THE WEAPON the eye sits, in the model's own units, which is
      // the number that says whether the head is over the comb or parked behind the
      // butt pad.
      printf("figm weld %-3s eye_z=%+.3f cant=%.1fdeg eye_up=%.0fmm "
             "eye_side=%.0fmm relief=%.0fmm off_axis=%.0fmm\n",
             WPN_DEF[wi].prefix,
             (double)(v3_dot(v3_sub(e, org3), fw3) / GUN_GIRTH_3P),
             (double)(anim_head_cant(&staged[wi].anim) * (180.0f / F_PI)),
             (double)(v3_dot(lat, up3) * 1000.0f),
             (double)(v3_dot(lat, rt3) * 1000.0f),
             (double)(-along * 1000.0f), (double)(v3_len(lat) * 1000.0f));
      // Stock contact is a relationship, not a screenshot judgement. The shoulder
      // pocket lies between the chest and the firing-side joint, slightly forward on
      // the armour face. Measure the descriptor's shared butt pin against that target
      // in the weapon frame so pose edits report which component actually moved.
      const gun_pose_desc_t *erg = gun_pose_desc(wi);
      v3 butt = v3_add(WP->gun_o,
                       gun_pose_offset(rt3, up3, fw3, erg->butt_pin,
                                       GUN_GIRTH_3P));
      v3 torso_up = v3_norm(v3_sub(WP->chest, WP->pelvis));
      v3 pocket = v3_add(v3_lerp(WP->chest, WP->sho[1], 0.60f),
                         v3_add(v3_scale(torso_up, 0.090f),
                                v3_scale(fw3, 0.055f)));
      v3 seat = v3_sub(butt, pocket);
      printf("figm seat %-3s gap=%.0fmm right=%+.0f up=%+.0f forward=%+.0f\n",
             WPN_DEF[wi].prefix, (double)(v3_len(seat) * 1000.0f),
             (double)(v3_dot(seat, rt3) * 1000.0f),
             (double)(v3_dot(seat, up3) * 1000.0f),
             (double)(v3_dot(seat, fw3) * 1000.0f));
    }
    printf("figm elbow");
    for (int i = 0; i < 2; i++) {
      v3 u = v3_sub(EP->sho[i], EP->elb[i]), f = v3_sub(EP->wri[i], EP->elb[i]);
      float lu = v3_len(u), lf = v3_len(f);
      float ang = (lu > 1e-5f && lf > 1e-5f)
                ? acosf(f_clamp(v3_dot(u, f) / (lu * lf), -1.0f, 1.0f)) * (180.0f / F_PI)
                : 180.0f;
      float reach = v3_len(v3_sub(EP->wri[i], EP->sho[i]));
      printf("  %s=%.0fdeg reach=%.0f/%.0fmm", i ? "R" : "L",
             (double)ang, (double)(reach * 1000.0f), (double)(al * 1000.0f));
    }
    printf("\n");
  }
  printf("figm part            w_mm   d_mm   h_mm    y0     y1\n");
  for (int i = 0; i < g_nprobe; i++) {
    if (!g_probe[i].name) continue;
    float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
    int seen = 0;
    // Merge every range sharing this name, so a part drawn once per side
    // reports its true SPAN (both deltoids together = bideltoid breadth).
    for (int j = i; j < g_nprobe; j++) {
      if (!g_probe[j].name || strcmp(g_probe[j].name, g_probe[i].name)) continue;
      for (int f = g_probe[j].v0; f < g_probe[j].v1; f += VERT_FLOATS) {
        float dx = g_scene[f] - b->pos.x, dz = g_scene[f + 2] - b->pos.z;
        float l[3] = {dx * cy + dz * sy, g_scene[f + 1] - b->pos.y,
                      dx * sy - dz * cy};
        for (int k = 0; k < 3; k++) {
          if (l[k] < lo[k]) lo[k] = l[k];
          if (l[k] > hi[k]) hi[k] = l[k];
          if (l[k] < glo[k]) glo[k] = l[k];
          if (l[k] > ghi[k]) ghi[k] = l[k];
        }
        seen = 1;
      }
      if (j > i) g_probe[j].name = NULL;   // consumed into this row
    }
    if (!seen) continue;
    printf("figm %-14s %6.1f %6.1f %6.1f %6.1f %6.1f\n", g_probe[i].name,
           (double)((hi[0] - lo[0]) * 1000.0f), (double)((hi[2] - lo[2]) * 1000.0f),
           (double)((hi[1] - lo[1]) * 1000.0f), (double)(lo[1] * 1000.0f),
           (double)(hi[1] * 1000.0f));
  }
  printf("figm TOTAL          %6.1f %6.1f %6.1f %6.1f %6.1f\n",
         (double)((ghi[0] - glo[0]) * 1000.0f), (double)((ghi[2] - glo[2]) * 1000.0f),
         (double)((ghi[1] - glo[1]) * 1000.0f), (double)(glo[1] * 1000.0f),
         (double)(ghi[1] * 1000.0f));
  // Skeleton lengths, which no vertex bbox can report.
  const pose_t *Q = &b->anim.pose;
  printf("figm bones uarm=%.1f farm=%.1f thigh=%.1f shin=%.1f "
         "sho_span=%.1f hip_span=%.1f hip_y=%.1f head_y=%.1f\n",
         (double)(v3_len(v3_sub(Q->elb[1], Q->sho[1])) * 1000.0f),
         (double)(v3_len(v3_sub(Q->wri[1], Q->elb[1])) * 1000.0f),
         (double)(v3_len(v3_sub(Q->knee[1], Q->hip[1])) * 1000.0f),
         (double)(v3_len(v3_sub(Q->ankle[1], Q->knee[1])) * 1000.0f),
         (double)(v3_len(v3_sub(Q->sho[1], Q->sho[0])) * 1000.0f),
         (double)(v3_len(v3_sub(Q->hip[1], Q->hip[0])) * 1000.0f),
         (double)((Q->pelvis.y - b->pos.y) * 1000.0f),
         (double)((Q->head.y - b->pos.y) * 1000.0f));
}

// The third-person weapon (or the SKULL) against the BODY, by ray parity over
// one closed part at a time — a union is not a solid.
static void figbury_proof(void) {
  // HOW MUCH OF THE THIRD-PERSON WEAPON IS INSIDE THE BODY. The carry pose is the pose
  // a player sees for most of a match — his own back in third person, and every enemy
  // who is not currently shooting at him — and the one way it can be wrong that no
  // existing proof can see is the weapon sinking into the trunk. figcheck cannot: it
  // tests the mesh against ITSELF for manifoldness and coplanarity, and two closed
  // meshes that pass through each other are a perfectly valid mesh. vmtrig cannot: it
  // tests the hand against the weapon, which is the opposite pair.
  static const char *const BODYP[] = {"torso", "carrier", "pack", "packstrap",
                                      "trap", "strap", "magpouch", "thigh",
                                      "dropleg", "neck"};
  // ...and the SAME question about the HEAD, because it is the same defect class and no
  // other proof can see it either: crouching, the head tucks onto the shoulders on
  // purpose, and three separate drops (the crouch tuck, the ADS cheek weld and the
  // spine's hunch) stack into it.
  // Every name is emitted by the current head builder; a stale probe name would
  // silently remove that closed part from the parity sweep.
  static const char *const HEADP[] = {"skull", "helm_shell", "nvg_shroud",
                                      "helm_rail", "goggle", "ear",
                                      "chinstrap"};
  static const char *const HBODY[] = {"torso", "carrier", "pack", "packstrap",
                                      "trap", "strap"};
  int hmode = 0;
  { char *sub = take_tok();
    if (sub && !strcmp(sub, "head")) hmode = 1;
    else if (sub) g_tok_pushback = sub;   // peeked, not consumed, as figcheck's DIST is
  }
  const char *const *BSET = hmode ? HBODY : BODYP;
  unsigned BSETN = hmode ? sizeof HBODY / sizeof *HBODY
                         : sizeof BODYP / sizeof *BODYP;
  printf(hmode ? "figbury sub  deepest_mm  hidden%%   jaw_mm  verts   "
                 "jaw_at(x,y,z mm, figure frame)\n"
               : "figbury wpn  deepest_mm  hidden%%  butt_mm  verts   "
                 "butt_at(x,y,z mm, figure frame)\n");
  for (int wi = 0; wi < (hmode ? 1 : WPN_COUNT); wi++) {
    const bot_t *b = probe_capture_figure(wi);
    // ONE RANGE PER PART, NOT A UNION, and that is the whole correctness of this proof.
    int brng[64][2], nb = 0, srng[24][2], ns = 0;
    v3 blo[64], bhi[64];
    for (int i = 0; i < g_nprobe && nb < 64; i++) {
      if (!g_probe[i].name) continue;
      if (hmode) {
        for (unsigned k = 0; k < sizeof HEADP / sizeof *HEADP && ns < 24; k++)
          if (!strcmp(g_probe[i].name, HEADP[k])) {
            srng[ns][0] = g_probe[i].v0; srng[ns][1] = g_probe[i].v1; ns++;
            break;
          }
      } else if (!strcmp(g_probe[i].name, "gun")) {
        srng[0][0] = g_probe[i].v0; srng[0][1] = g_probe[i].v1; ns = 1;
      }
      for (unsigned k = 0; k < BSETN; k++)
        if (!strcmp(g_probe[i].name, BSET[k])) {
          brng[nb][0] = g_probe[i].v0; brng[nb][1] = g_probe[i].v1;
          // A per-part box, so the O(parts x verts x tris) sweep only pays
          // for the parts a vertex could possibly be inside.
          v3 lo = {{1e9f, 1e9f, 1e9f}}, hi = {{-1e9f, -1e9f, -1e9f}};
          for (int f = g_probe[i].v0; f + VERT_FLOATS <= g_probe[i].v1;
               f += VERT_FLOATS) {
            v3 v = SCENE_V(f);
            for (int c = 0; c < 3; c++) {
              if (v.e[c] < lo.e[c]) lo.e[c] = v.e[c];
              if (v.e[c] > hi.e[c]) hi.e[c] = v.e[c];
            }
          }
          blo[nb] = lo; bhi[nb] = hi;
          nb++;
          break;
        }
    }
    if (!ns || !nb) { printf("figbury %s  n/a\n",
                             hmode ? "head" : WPN_DEF[wi].prefix); continue; }
    // The rearmost vertex ALONG THE BORE is the butt: the frame comes from
    // the same pose_gun_frame the model was placed with, so this is the
    // model's own rear and not a world-axis accident.
    v3 org, rt, up, fw;
    pose_gun_frame(&b->anim.pose, wi, &org, &rt, &up, &fw);
    float cy2 = cosf(b->yaw), sy2 = sinf(b->yaw);
    float deep = 0.0f, butt_z = 1e9f, butt_sd = 0.0f;
    v3 buttp = b->pos, deepp = b->pos;
    int inside = 0, total = 0;
    for (int sr = 0; sr < ns; sr++)
    for (int f = srng[sr][0]; f + VERT_FLOATS <= srng[sr][1]; f += VERT_FLOATS) {
      v3 v = SCENE_V(f);
      float sd = 1e9f;
      for (int q = 0; q < nb; q++) {
        if (v.x < blo[q].x || v.x > bhi[q].x || v.y < blo[q].y ||
            v.y > bhi[q].y || v.z < blo[q].z || v.z > bhi[q].z) continue;
        float d = trig_mesh_sd(v, brng[q], 2);
        if (d < sd) sd = d;
      }
      if (sd > 1e8f) sd = 1.0f;   // outside every part's box: comfortably out
      total++;
      if (sd < 0.0f) inside++;
      if (-sd > deep) { deep = -sd; deepp = v; }
      float z = hmode ? v.y : v3_dot(v3_sub(v, org), fw);
      if (z < butt_z) { butt_z = z; butt_sd = sd; buttp = v; }
    }
    // ...and WHERE it is, in the figure's own frame, which is the number a pose edit is
    // actually aimed at: x right of the spine, y above the ground, z forward of it.
    float dx2 = buttp.x - b->pos.x, dz2 = buttp.z - b->pos.z;
    float bl[3] = {dx2 * cy2 + dz2 * sy2, buttp.y - b->pos.y,
                   dx2 * sy2 - dz2 * cy2};
    float dx3 = deepp.x - b->pos.x, dz3 = deepp.z - b->pos.z;
    float dl[3] = {dx3 * cy2 + dz3 * sy2, deepp.y - b->pos.y,
                   dx3 * sy2 - dz3 * cy2};
    // The butt's own station along the bore, in MODEL units — the number a weapon-aware
    // anchor needs and the one every comment in this file has guessed at.
    printf("figbury-butt %s z=%+.4f\n", WPN_DEF[wi].prefix,
           (double)(butt_z / GUN_GIRTH_3P));
    printf("figbury %-4s %10.1f %8.1f %8.1f  %d/%d   "
           "(%.0f %.0f %.0f) deep_at(%.0f %.0f %.0f)\n",
           hmode ? "head" : WPN_DEF[wi].prefix,
           (double)(deep * 1000.0f),
           (double)(total ? 100.0f * (float)inside / (float)total : 0.0f),
           (double)(-butt_sd * 1000.0f), inside, total,
           (double)(bl[0] * 1000.0f), (double)(bl[1] * 1000.0f),
           (double)(bl[2] * 1000.0f), (double)(dl[0] * 1000.0f),
           (double)(dl[1] * 1000.0f), (double)(dl[2] * 1000.0f));
  }
}

// Six impact locations on bot 0: peak joint travel per case, and how far apart
// the six films are.
static void flinch_proof(sim_ctx_t *s) {
  // WHAT A ROUND DOES TO A MAN WHO SURVIVES IT, per impact location.
  typedef struct {
    int puppet, crouch, jump, slide, ready;
    float mx, mz, speed, face;
  } flinch_drive_t;
  flinch_drive_t was_drive[MAX_BOTS];
  int was_frz = g_bots_frozen;
  g_bots_frozen = 0;
  // Match ticks and every pose solver must keep running, so botfreeze is not an
  // isolation tool here.
  for (int bi = 0; bi < MAX_BOTS; bi++) {
    bot_t *pb = &g_bots[bi];
    was_drive[bi] = (flinch_drive_t){
      pb->puppet, pb->pup_crouch, pb->pup_jump, pb->pup_slide, pb->pup_ready,
      pb->pup_mx, pb->pup_mz, pb->pup_speed, pb->pup_face
    };
    pb->puppet = 1;
    pb->pup_crouch = pb->pup_jump = pb->pup_slide = 0;
    pb->pup_ready = 1;
    pb->pup_mx = pb->pup_mz = pb->pup_speed = 0.0f;
    pb->pup_face = pb->yaw;
  }
  struct { const char *name; float dx, dy; } CASE[6] = {
    {"head",  0.00f, 1.66f}, {"chest", 0.00f, 1.30f},
    {"gut",   0.00f, 1.00f}, {"thigh", 0.00f, 0.62f},
    {"shoL", -0.22f, 1.42f}, {"shoR",  0.22f, 1.42f},
  };
  static const char *JN[POSE_NPOS] = {
    "hipL","hipR","kneeL","kneeR","ankL","ankR","toeL","toeR",
    "shoL","shoR","elbL","elbR","wriL","wriR",
    "pelvis","chest","neck","head","gun"};
  float peak[6][POSE_NPOS] = {{0}};
  bot_t *b = &g_bots[0];
  // Staging leaves the MATCH live (botfreeze cannot be used — the solver must advance),
  // so a stray round from another bot can re-latch anim_hit over the staged case or
  // kill the subject outright: a 26 m respawn teleport then prints as a 26134 mm
  // "flinch" that passes the distinct contract with flying colours.
  int base_hp = b->hp;
  for (int c = 0; c < 6; c++) {
    // Same start state for every case: reset the solver, let the idle pose settle, then
    // latch the base.
    anim_reset(&b->anim, b->pos, b->yaw, b->wp.cur);
    b->anim.hit_k = b->anim.hit_s = b->anim.hit_w = 0.0f;
    // `died` LATCHES: a kill early in the settle window respawns the bot (120 ticks)
    // before the case-end check, with hp back at 100 — so the end-of-case alive/hp test
    // alone reads clean while the respawn teleport sat inside the observation window.
    int died = 0;
    for (int i = 0; i < 90; i++) { sim_advance(s); died |= !b->alive; }
    pose_t base = b->anim.pose;
    const v3 *bj = (const v3 *)&base;
    v3 rt = {{cosf(b->yaw), 0.0f, sinf(b->yaw)}};
    v3 at = {{b->pos.x + rt.x * CASE[c].dx, b->pos.y + CASE[c].dy,
              b->pos.z + rt.z * CASE[c].dx}};
    v3 from = {{at.x, at.y, at.z + 6.0f}};
    anim_hit(&b->anim, b->pos, from, at, c == 0);
    for (int i = 0; i < 45; i++) {
      sim_advance(s);
      died |= !b->alive;
      const v3 *pj = (const v3 *)&b->anim.pose;
      for (int k = 0; k < POSE_NPOS; k++) {
        float d = v3_len(v3_sub(pj[k], bj[k]));
        if (d > peak[c][k]) peak[c][k] = d;
      }
    }
    if (died || b->hp != base_hp) {
      printf("flinch: STAGING VIOLATED — bot 0 %s during case %s (t=%ld); "
             "numbers would be poisoned, aborting\n",
             died ? "was killed" : "took a real round",
             CASE[c].name, g_tick);
      exit(1);
    }
  }
  for (int c = 0; c < 6; c++) {
    int top = 0;
    float sum = 0.0f;
    for (int k = 0; k < POSE_NPOS; k++) {
      sum += peak[c][k];
      if (peak[c][k] > peak[c][top]) top = k;
    }
    float dmin = 1e9f;
    for (int o = 0; o < 6; o++) {
      if (o == c) continue;
      float d = 0.0f;
      for (int k = 0; k < POSE_NPOS; k++)
        d += fabsf(peak[c][k] - peak[o][k]);
      if (d < dmin) dmin = d;
    }
    printf("flinch %-6s peak=%.1fmm<%s> head=%.1f chest=%.1f pelvis=%.1f "
           "sum=%.1f distinct=%.1f\n", CASE[c].name,
           (double)(peak[c][top] * 1000.0f), JN[top],
           (double)(peak[c][17] * 1000.0f), (double)(peak[c][15] * 1000.0f),
           (double)(peak[c][14] * 1000.0f), (double)(sum * 1000.0f),
           (double)(dmin * 1000.0f));
  }
  // Leave bot 0 as it was found, un-flinched: this command burns 810 ticks, and any
  // later command in the same script would otherwise be measuring a poisoned bot.
  b->anim.hit_k = b->anim.hit_s = b->anim.hit_w = 0.0f;
  anim_reset(&b->anim, b->pos, b->yaw, b->wp.cur);
  for (int bi = 0; bi < MAX_BOTS; bi++) {
    bot_t *pb = &g_bots[bi];
    pb->puppet = was_drive[bi].puppet;
    pb->pup_crouch = was_drive[bi].crouch;
    pb->pup_jump = was_drive[bi].jump;
    pb->pup_slide = was_drive[bi].slide;
    pb->pup_ready = was_drive[bi].ready;
    pb->pup_mx = was_drive[bi].mx;
    pb->pup_mz = was_drive[bi].mz;
    pb->pup_speed = was_drive[bi].speed;
    pb->pup_face = was_drive[bi].face;
  }
  g_bots_frozen = was_frz;
}

// The trigger guard and the blade against the HAND'S MESH — the pair figcheck
// and vmtrig's siblings cannot see, per weapon x reload station x view. There is no
// PRESS axis any more: the blade is a rigid part and the index rests on it, so a
// pressed row was byte-identical to its unpressed twin.
typedef struct { v3 lo, hi; } vmtrig_aabb_t;

#define VMTRIG_WORK_LIMIT UINT64_C(64000000000)
typedef struct {
  uint64_t n;
  int overflow;
} vmtrig_work_t;

static void vmtrig_work_add(vmtrig_work_t *work, uint64_t add) {
  if (!work || work->overflow) return;
  if (add > VMTRIG_WORK_LIMIT - work->n) {
    work->n = VMTRIG_WORK_LIMIT;
    work->overflow = 1;
  } else work->n += add;
}

static int vmtrig_u64_add(uint64_t *value, uint64_t add) {
  if (add > UINT64_MAX - *value) {
    *value = UINT64_MAX;
    return 1;
  }
  *value += add;
  return 0;
}

static uint64_t g_vmtrig_inside_ambiguous;

static int vmtrig_ambiguity_gate(uint64_t before, uint64_t after) {
  return after != before;
}

static int vmtrig_v3_nonfinite(v3 p) {
  return f_is_nonfinite(p.x) || f_is_nonfinite(p.y) ||
         f_is_nonfinite(p.z);
}

static v3 vmtrig_tri_closest(v3 p, v3 a, v3 b, v3 c);

// A parity ray that lands on an edge or vertex is unusable, not a hit or a miss.
// Trying independent directions prevents the usual shared-edge double-count while
// preserving a hard ambiguity bit if every ray is exceptional.
static int vmtrig_ray_tri_class(v3 o, v3 d, v3 a, v3 b, v3 c) {
  if (vmtrig_v3_nonfinite(o) || vmtrig_v3_nonfinite(d) ||
      vmtrig_v3_nonfinite(a) || vmtrig_v3_nonfinite(b) ||
      vmtrig_v3_nonfinite(c)) return -1;
  // Double precision and a scale-relative parallel test avoid treating a real,
  // shallow ray crossing as a miss merely because the float determinant is small in
  // absolute units. An indeterminate exactly-parallel face invalidates this ray; the
  // two independent parity directions remain available.
  double e1[3], e2[3], dv[3], tv[3];
  for (int k = 0; k < 3; k++) {
    e1[k] = (double)b.e[k] - (double)a.e[k];
    e2[k] = (double)c.e[k] - (double)a.e[k];
    dv[k] = (double)d.e[k];
    tv[k] = (double)o.e[k] - (double)a.e[k];
  }
  double pv[3] = {
    dv[1] * e2[2] - dv[2] * e2[1],
    dv[2] * e2[0] - dv[0] * e2[2],
    dv[0] * e2[1] - dv[1] * e2[0],
  };
  double det = e1[0] * pv[0] + e1[1] * pv[1] + e1[2] * pv[2];
  double e1_len = sqrt(e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2]);
  double e2_len = sqrt(e2[0] * e2[0] + e2[1] * e2[1] + e2[2] * e2[2]);
  double d_len = sqrt(dv[0] * dv[0] + dv[1] * dv[1] + dv[2] * dv[2]);
  double scale = e1_len * e2_len * d_len;
  if (!(scale > 0.0) || fabs(det) <= scale * 1e-14) return -1;
  double inv = 1.0 / det;
  double u = (tv[0] * pv[0] + tv[1] * pv[1] + tv[2] * pv[2]) * inv;
  double qv[3] = {
    tv[1] * e1[2] - tv[2] * e1[1],
    tv[2] * e1[0] - tv[0] * e1[2],
    tv[0] * e1[1] - tv[1] * e1[0],
  };
  double v = (dv[0] * qv[0] + dv[1] * qv[1] + dv[2] * qv[2]) * inv;
  double t = (e2[0] * qv[0] + e2[1] * qv[1] + e2[2] * qv[2]) * inv;
  const double edge_eps = 2e-6;
  if (t <= 1e-7 || u < -edge_eps || v < -edge_eps ||
      u + v > 1.0 + edge_eps) return 0;
  if (u <= edge_eps || v <= edge_eps || 1.0 - u - v <= edge_eps)
    return -1;
  return 1;
}

static float vmtrig_mesh_sd_at(v3 p, const int range[2], vmtrig_work_t *work,
                               v3 *nearest_at) {
  static const v3 ray[3] = {
    {{0.5773f, 0.5771f, 0.5775f}},
    {{0.3127f, 0.8473f, -0.4291f}},
    {{-0.7311f, 0.2197f, 0.6461f}},
  };
  float dmin = 1e9f;
  int parity = 0, have_parity = 0;
  for (int r = 0; r < 3 && !have_parity; r++) {
    int cross = 0, ambiguous = 0;
    for (int f = range[0]; f + 2 * VERT_FLOATS < range[1];
         f += 3 * VERT_FLOATS) {
      v3 a = SCENE_V(f), b = SCENE_V(f + VERT_FLOATS);
      v3 c = SCENE_V(f + 2 * VERT_FLOATS);
      if (r == 0) {
        v3 q = nearest_at ? vmtrig_tri_closest(p, a, b, c) : p;
        float d = nearest_at ? v3_len(v3_sub(p, q))
                             : pt_tri_dist(p, a, b, c);
        if (d < dmin) {
          dmin = d;
          if (nearest_at) *nearest_at = q;
        }
      }
      int hit = vmtrig_ray_tri_class(p, ray[r], a, b, c);
      ambiguous |= hit < 0;
      cross += hit > 0;
      vmtrig_work_add(work, 1);
    }
    if (!ambiguous) { parity = cross & 1; have_parity = 1; }
  }
  if (!have_parity) {
    if (g_vmtrig_inside_ambiguous != UINT64_MAX) g_vmtrig_inside_ambiguous++;
    return dmin;
  }
  return parity ? -dmin : dmin;
}

static float vmtrig_mesh_sd(v3 p, const int range[2], vmtrig_work_t *work) {
  return vmtrig_mesh_sd_at(p, range, work, NULL);
}

static vmtrig_aabb_t vmtrig_range_aabb(const int r[2]);
static int vmtrig_aabb_overlap(vmtrig_aabb_t a, vmtrig_aabb_t b);
static int vmtrig_range_topology(const int range[2], vmtrig_work_t *work);
static int vmtrig_mesh_cross(const int ar[2], const int br[2],
                             vmtrig_work_t *work);

typedef struct {
  const char *name;
  int r[2];
  int hand;
} vmtrig_hand_range_t;

enum {
  VMTRIG_CHILD_PALM, VMTRIG_CHILD_THUMB, VMTRIG_CHILD_TRIGGER,
  VMTRIG_CHILD_GRIP, VMTRIG_CHILD_COUNT
};

static int vmtrig_child_kind(const char *name) {
  if (!name) return -1;
  if (!strcmp(name, "palm")) return VMTRIG_CHILD_PALM;
  if (!strcmp(name, "thumb")) return VMTRIG_CHILD_THUMB;
  if (!strcmp(name, "trigger_digit")) return VMTRIG_CHILD_TRIGGER;
  if (!strcmp(name, "grip_digits")) return VMTRIG_CHILD_GRIP;
  return -1;
}

static int vmtrig_child_name(const char *name) {
  return vmtrig_child_kind(name) >= 0;
}

static int vmtrig_collect_hands(vmtrig_hand_range_t *out, int view) {
  int n = 0, build_hand = -1, hand = FIG_HAND_FIRE;
  for (int i = 0; i < g_nprobe && n < PROBE_MAX; i++) {
    const probe_t *p = &g_probe[i];
    if (!vmtrig_child_name(p->name) || p->v1 <= p->v0) continue;
    if (!strcmp(p->name, "palm")) {
      build_hand++;
      // VM emits firing then support; the world figure emits support then firing.
      hand = view ? (build_hand ? FIG_HAND_FIRE : FIG_HAND_SUPPORT)
                  : (build_hand ? FIG_HAND_SUPPORT : FIG_HAND_FIRE);
    }
    out[n++] = (vmtrig_hand_range_t){p->name, {p->v0, p->v1}, hand};
  }
  return n;
}

static float vmtrig_tube_parts(const v3 *p, int np, float radius,
                               const vmtrig_hand_range_t *hr, int nh,
                               vmtrig_work_t *work) {
  float best = 1e9f;
  for (int i = 0; i < nh; i++)
    for (int e = 0; e + 1 < np; e++) {
      float length = v3_len(v3_sub(p[e + 1], p[e]));
      int steps = (int)(length / TRIG_PROBE_STEP) + 1;
      for (int u = 0; u <= steps; u++) {
        v3 at = v3_lerp(p[e], p[e + 1], (float)u / (float)steps);
        float d = vmtrig_mesh_sd(at, hr[i].r, work) - radius;
        if (d < best) best = d;
      }
    }
  return best;
}

static float vmtrig_pad_parts(const vmtrig_hand_range_t *hr, int nh,
                              vmtrig_work_t *work) {
  float best = 1e9f;
  for (int i = 0; i < nh; i++)
    if (!strcmp(hr[i].name, "trigger_digit")) {
      float d = vmtrig_mesh_sd(g_gun_trig_p, hr[i].r, work);
      if (d < best) best = d;
    }
  return best;
}

static float vmtrig_allowed_bite(gun_contact_kind_t kind, int hand,
                                 const vmtrig_hand_range_t *hr, int nh) {
  const gun_contact_cylinder_t *contact = &g_gun_contact_cylinder[kind];
  if (!contact->enabled) return -1.0f;
  float bite = 0.0f;
  int ninside = 0;
  for (int i = 0; i < nh; i++) {
    if (hr[i].hand != hand || !strcmp(hr[i].name, "trigger_digit")) continue;
    for (int f = hr[i].r[0]; f + VERT_FLOATS <= hr[i].r[1]; f += VERT_FLOATS) {
      float d = gun_contact_bite(SCENE_V(f), contact);
      if (d > 0.0f) { bite += d; ninside++; }
    }
  }
  return ninside ? bite / (float)ninside : 0.0f;
}

typedef struct {
  float mean, mean_lo, mean_hi, bite_max_upper;
  float same_child_max;
  double bite_boundary_area;
  float hand_max, weapon_max, complement_max;
  float hand_seen, hand_certified, weapon_seen, weapon_certified;
  const char *hand_part, *weapon_part;
  int hand_child, weapon_child, hand_tri, weapon_tri;
  int hand_range, weapon_range;
  int hand_contact_kind, weapon_contact_kind;
  float hand_score, hand_facing, hand_normal, weapon_score;
  v3 hand_at, weapon_at;
  float min_allowed_normal;
  int cross, complement_fail, bite_fail, same_child_fail, boundary_fail;
  uint64_t hand_allowed, hand_forbidden;
  uint64_t weapon_allowed, weapon_forbidden, hand_total, weapon_total;
  uint64_t hand_buried, weapon_buried;
  uint64_t hand_seam, weapon_seam;
  uint64_t hand_unresolved, weapon_unresolved;
  uint64_t hand_boundary, weapon_boundary;
  double contact_boundary_area;
  float contact_boundary_upper;
  int boundary_cross, seam_cross;
  uint64_t hand_source_leaf_peak, weapon_source_leaf_peak;
  int hand_peak_owner, hand_peak_tri, weapon_peak_owner, weapon_peak_tri;
  uint64_t hand_sources, weapon_sources;
  uint64_t ranges, census_missing, census_overlap;
  uint64_t leaf_overflow, leaf_dropped;
  uint64_t host_buried_cells, host_straddle_cells, host_terminal_cells;
} vmtrig_allowed_mesh_t;

typedef struct {
  float seen, certified;
  float boundary_upper;
  double boundary_area;
  uint64_t samples;
  int source_tri;
  v3 at;
  int fail;
  float allowed_max;
  int allowed_fail;
  uint64_t host_buried, host_straddle, host_terminal;
} vmtrig_wall_t;

static int vmtrig_boundary_gate(const vmtrig_allowed_mesh_t *mesh) {
  int uncertain_cross = mesh->boundary_cross || mesh->seam_cross;
  int uncovered_cross = uncertain_cross &&
                        mesh->contact_boundary_area <= 0.0;
  return uncovered_cross || mesh->contact_boundary_upper > 0.0005f;
}

#define VMTRIG_SURFACE_STEP 0.0020f
#define VMTRIG_OPPOSITE_LIMIT 0.0005f
#define VMTRIG_ALLOWED_OPPOSITE_LIMIT 0.0100f
#define VMTRIG_ALLOWED_BITE_LIMIT 0.0060f
#define VMTRIG_CERT_RADIUS  0.00025f

static float vmtrig_wall_depth(v3 p, const int target[2], vmtrig_aabb_t box,
                               vmtrig_work_t *work) {
  for (int k = 0; k < 3; k++)
    if (p.e[k] < box.lo.e[k] || p.e[k] > box.hi.e[k]) return 0.0f;
  return fmaxf(-vmtrig_mesh_sd(p, target, work), 0.0f);
}

static void vmtrig_wall_tri(v3 a, v3 b, v3 c, const int target[2],
                            vmtrig_aabb_t box,
                            vmtrig_wall_t *out, vmtrig_work_t *work, int exhaustive,
                            int source_tri, float limit) {
  if (work && work->overflow) { out->fail = 1; return; }
  if (out->fail && !exhaustive) return;
  float ab = v3_len(v3_sub(b, a)), bc = v3_len(v3_sub(c, b));
  float ca = v3_len(v3_sub(a, c));
  if (fmaxf(ab, fmaxf(bc, ca)) > VMTRIG_SURFACE_STEP) {
    if (ab >= bc && ab >= ca) {
      v3 m = v3_lerp(a, b, 0.5f);
      vmtrig_wall_tri(a, m, c, target, box, out, work, exhaustive, source_tri,
                      limit);
      vmtrig_wall_tri(m, b, c, target, box, out, work, exhaustive, source_tri,
                      limit);
    } else if (bc >= ca) {
      v3 m = v3_lerp(b, c, 0.5f);
      vmtrig_wall_tri(a, b, m, target, box, out, work, exhaustive, source_tri,
                      limit);
      vmtrig_wall_tri(a, m, c, target, box, out, work, exhaustive, source_tri,
                      limit);
    } else {
      v3 m = v3_lerp(c, a, 0.5f);
      vmtrig_wall_tri(a, b, m, target, box, out, work, exhaustive, source_tri,
                      limit);
      vmtrig_wall_tri(m, b, c, target, box, out, work, exhaustive, source_tri,
                      limit);
    }
    return;
  }
  v3 center = v3_scale(v3_add(a, v3_add(b, c)), 1.0f / 3.0f);
  float da = vmtrig_wall_depth(a, target, box, work);
  float db = vmtrig_wall_depth(b, target, box, work);
  float dc = vmtrig_wall_depth(c, target, box, work);
  float dm = vmtrig_wall_depth(center, target, box, work);
  if (vmtrig_u64_add(&out->samples, 4)) out->fail = 1;
  float seen = fmaxf(fmaxf(da, db), fmaxf(dc, dm));
  if (seen > out->seen) {
    out->seen = seen;
    out->source_tri = source_tri;
    out->at = da >= db && da >= dc && da >= dm ? a
            : db >= dc && db >= dm ? b : dc >= dm ? c : center;
  }
  if (seen > limit) { out->fail = 1; return; }
  float radius = fmaxf(v3_len(v3_sub(a, center)),
                 fmaxf(v3_len(v3_sub(b, center)),
                       v3_len(v3_sub(c, center))));
  // Signed distance is 1-Lipschitz. A centre sample plus the leaf radius is a
  // conservative upper bound for every unsampled point in this final triangle.
  float upper = dm + radius;
  if (upper <= limit) {
    if (upper > out->certified) out->certified = upper;
    return;
  }
  if (radius <= VMTRIG_CERT_RADIUS) {
    if (upper > out->certified) out->certified = upper;
    out->fail = upper > limit;
    return;
  }
  // Only an ambiguous near-threshold leaf pays for the sub-0.25 mm refinement.
  if (ab >= bc && ab >= ca) {
    v3 m = v3_lerp(a, b, 0.5f);
    vmtrig_wall_tri(a, m, c, target, box, out, work, exhaustive, source_tri,
                    limit);
    vmtrig_wall_tri(m, b, c, target, box, out, work, exhaustive, source_tri,
                    limit);
  } else if (bc >= ca) {
    v3 m = v3_lerp(b, c, 0.5f);
    vmtrig_wall_tri(a, b, m, target, box, out, work, exhaustive, source_tri,
                    limit);
    vmtrig_wall_tri(a, m, c, target, box, out, work, exhaustive, source_tri,
                    limit);
  } else {
    v3 m = v3_lerp(c, a, 0.5f);
    vmtrig_wall_tri(a, b, m, target, box, out, work, exhaustive, source_tri,
                    limit);
    vmtrig_wall_tri(m, b, c, target, box, out, work, exhaustive, source_tri,
                    limit);
  }
}

// Rasterize the ACTUAL final triangle interiors, not merely their vertices or the
// pre-fig_chain nodes. Each target is one closed range; signed parity across an
// overlapping union would cancel interiors.
static vmtrig_wall_t vmtrig_wall_pair(const int source[2],
                                      const int target[2], vmtrig_work_t *work,
                                      int exhaustive, float limit) {
  vmtrig_wall_t out = {0};
  vmtrig_aabb_t sb = vmtrig_range_aabb(source);
  vmtrig_aabb_t tb = vmtrig_range_aabb(target);
  if (!vmtrig_aabb_overlap(sb, tb)) return out;
  for (int f = source[0]; f + 2 * VERT_FLOATS < source[1];
       f += 3 * VERT_FLOATS) {
    v3 a = SCENE_V(f), b = SCENE_V(f + VERT_FLOATS);
    v3 c = SCENE_V(f + 2 * VERT_FLOATS);
    vmtrig_wall_tri(a, b, c, target, tb, &out, work, exhaustive,
                    (f - source[0]) / (3 * VERT_FLOATS), limit);
    if (out.fail && !exhaustive) break;
  }
  return out;
}

static int vmtrig_wall_selftest(float *shallow, float *thin,
                                vmtrig_work_t *work) {
  float cham = g_cham, diss = g_fig_diss;
  g_cham = 0.0f; g_fig_diss = 0.0f;
  scene_reset();
  int hand[2] = {g_scene_floats, 0};
  scene_box((v3){{0, 0, 0}}, (v3){{0.010f, 0, 0}},
            (v3){{0, 0.010f, 0}}, (v3){{0, 0, 0.010f}}, (v3){{1, 1, 1}});
  hand[1] = g_scene_floats;
  int weapon[2] = {g_scene_floats, 0};
  // Offset the second cube on two axes: the 3 mm overlap remains shallow, while
  // the closed surfaces cross transversely instead of sharing coplanar side faces.
  scene_box((v3){{0.017f, 0.003f, 0}}, (v3){{0.010f, 0, 0}},
            (v3){{0, 0.010f, 0}}, (v3){{0, 0, 0.010f}}, (v3){{1, 1, 1}});
  weapon[1] = g_scene_floats;
  uint64_t ambiguous_before = g_vmtrig_inside_ambiguous;
  int open_range[2] = {hand[0], hand[1] - 3 * VERT_FLOATS};
  int parity_fail = vmtrig_mesh_sd((v3){{0, 0, 0}}, hand, work) >= 0.0f ||
                    vmtrig_mesh_sd((v3){{0.040f, 0, 0}}, hand, work) <= 0.0f ||
                    g_vmtrig_inside_ambiguous != ambiguous_before;
  int topology_fail = vmtrig_range_topology(hand, work) != 0 ||
                      vmtrig_range_topology(weapon, work) != 0 ||
                      vmtrig_range_topology(open_range, work) == 0;
  float finite_save = g_scene[hand[0]];
  g_scene[hand[0]] = NAN;
  topology_fail |= vmtrig_range_topology(hand, work) == 0;
  g_scene[hand[0]] = finite_save;
  v3 ra = {{-1, -1, 0}}, rb = {{1, -1, 0}}, rc = {{1, 1, 0}};
  v3 rd = {{-1, 1, 0}}, ro = {{0, 0, -1}}, ray = {{0, 0, 1}};
  int edge_fail = vmtrig_ray_tri_class(ro, ray, ra, rb, rc) != -1 ||
                  vmtrig_ray_tri_class(ro, ray, ra, rc, rd) != -1;
  // A normalized ray only 1e-11 out of plane still crosses this triangle at
  // (0.2,0.2). The former absolute determinant cutoff silently called it a miss.
  v3 npo = {{0.1f, 0.2f, -1e-12f}}, npd = {{1.0f, 0.0f, 1e-11f}};
  v3 nta = {{0, 0, 0}}, ntb = {{1, 0, 0}}, ntc = {{0, 1, 0}};
  edge_fail |= vmtrig_ray_tri_class(npo, npd, nta, ntb, ntc) != 1;
  npd = (v3){{1.0f, 0.0f, 0.0f}};
  edge_fail |= vmtrig_ray_tri_class(npo, npd, nta, ntb, ntc) != -1;
  vmtrig_wall_t hw = vmtrig_wall_pair(hand, weapon, work, 1, 0.0045f);
  vmtrig_wall_t wh = vmtrig_wall_pair(weapon, hand, work, 1, 0.0045f);
  int shallow_cross = vmtrig_mesh_cross(hand, weapon, work);
  *shallow = fmaxf(hw.seen, wh.seen);
  int shallow_fail = hw.fail || wh.fail || shallow_cross == 0 ||
                     *shallow < 0.0028f || *shallow > 0.0032f;

  scene_reset();
  hand[0] = g_scene_floats;
  scene_box((v3){{0, 0, 0}}, (v3){{0.010f, 0, 0}},
            (v3){{0, 0.010f, 0}}, (v3){{0, 0, 0.010f}}, (v3){{1, 1, 1}});
  hand[1] = g_scene_floats;
  weapon[0] = g_scene_floats;
  // All eight plate vertices are outside the hand cube. Only sampling the broad
  // triangle interiors can see that its 2 mm slab passes through the hand centre.
  scene_box((v3){{0, 0, 0}}, (v3){{0.030f, 0, 0}},
            (v3){{0, 0.001f, 0}}, (v3){{0, 0, 0.030f}}, (v3){{1, 1, 1}});
  weapon[1] = g_scene_floats;
  hw = vmtrig_wall_pair(hand, weapon, work, 1, 0.0045f);
  wh = vmtrig_wall_pair(weapon, hand, work, 1, 0.0045f);
  int thin_cross = vmtrig_mesh_cross(hand, weapon, work);
  *thin = fmaxf(hw.seen, wh.seen);
  int thin_fail = !(hw.fail || wh.fail) || thin_cross == 0 ||
                  *thin <= 0.0045f;
  g_cham = cham; g_fig_diss = diss;
  return (shallow_fail << 0) | (thin_fail << 1) | (parity_fail << 2) |
         (topology_fail << 3) | (edge_fail << 4);
}

static const fig_contact_patch_t *vmtrig_contact_patch(
    const vmtrig_hand_range_t *range, int *matches) {
  const fig_contact_patch_t *found = NULL;
  int n = 0;
  for (int i = 0; i < g_fig_contact_patch_n; i++) {
    const fig_contact_patch_t *patch = &g_fig_contact_patch[i];
    if (patch->hand == range->hand && patch->v0 == range->r[0] &&
        patch->v1 == range->r[1] && !strcmp(patch->name, range->name)) {
      found = patch;
      n++;
    }
  }
  if (matches) *matches = n;
  return found;
}

static int vmtrig_child_census(const vmtrig_hand_range_t *hr, int nh,
                               int live, int trigger_expected) {
  int count[2][VMTRIG_CHILD_COUNT] = {{0}};
  int fail = nh != 12 || g_nprobe >= PROBE_MAX ||
             g_fig_contact_patch_overflow != 0;
  for (int i = 0; i < nh; i++) {
    int kind = vmtrig_child_kind(hr[i].name);
    if (hr[i].hand < 0 || hr[i].hand > 1 || hr[i].r[0] < 0 ||
        kind < 0 || hr[i].r[1] <= hr[i].r[0] ||
        (hr[i].r[1] - hr[i].r[0]) % (3 * VERT_FLOATS)) fail++;
    else count[hr[i].hand][kind]++;
    for (int j = i + 1; j < nh; j++)
      if (hr[i].r[0] < hr[j].r[1] && hr[j].r[0] < hr[i].r[1]) fail++;
  }
  fail += count[FIG_HAND_FIRE][VMTRIG_CHILD_PALM] != 1 ||
          count[FIG_HAND_FIRE][VMTRIG_CHILD_THUMB] != 1 ||
          count[FIG_HAND_FIRE][VMTRIG_CHILD_TRIGGER] != trigger_expected ||
          count[FIG_HAND_FIRE][VMTRIG_CHILD_GRIP] !=
            (trigger_expected ? 3 : 4);
  fail += count[FIG_HAND_SUPPORT][VMTRIG_CHILD_PALM] != 1 ||
          count[FIG_HAND_SUPPORT][VMTRIG_CHILD_THUMB] != 1 ||
          count[FIG_HAND_SUPPORT][VMTRIG_CHILD_TRIGGER] != 0 ||
          count[FIG_HAND_SUPPORT][VMTRIG_CHILD_GRIP] != 4;
  if (!live) return fail + (g_fig_contact_patch_n != 0);
  fail += g_fig_contact_patch_n != nh;
  for (int i = 0; i < nh; i++) {
    int matches = 0;
    (void)vmtrig_contact_patch(&hr[i], &matches);
    fail += matches != 1;
  }
  for (int p = 0; p < g_fig_contact_patch_n; p++) {
    int matches = 0;
    for (int i = 0; i < nh; i++)
      matches += g_fig_contact_patch[p].hand == hr[i].hand &&
                 g_fig_contact_patch[p].v0 == hr[i].r[0] &&
                 g_fig_contact_patch[p].v1 == hr[i].r[1] &&
                 !strcmp(g_fig_contact_patch[p].name, hr[i].name);
    fail += matches != 1;
  }
  return fail;
}

// The patch is a swept ellipse ON the held surface: axial palm/finger breadth and a
// much smaller tangential pad footprint are independent.  In particular, a 45 mm palm
// breadth cannot make the wall 25 mm across a pistol grip part of the same patch.
static float vmtrig_patch_score(v3 p, const fig_contact_patch_t *patch,
                                float margin, int *out_seg, float *out_u) {
  if (!patch || patch->n <= 0) return 1e9f;
  v3 rel = v3_sub(p, patch->origin);
  float p_along = v3_dot(rel, patch->axis);
  v3 radial = v3_sub(rel, v3_scale(patch->axis, p_along));
  float radial_len = v3_len(radial);
  if (radial_len < 1e-6f) return 1e9f;
  radial = v3_scale(radial, 1.0f / radial_len);
  v3 side = v3_norm(v3_cross(patch->axis, patch->radial_ref));
  float raw_angle = atan2f(v3_dot(radial, side),
                           v3_dot(radial, patch->radial_ref));
  float best = 1e9f;
  int last = patch->n > 1 ? patch->n - 1 : 1;
  for (int i = 0; i < last; i++) {
    int j = patch->n > 1 ? i + 1 : i;
    float query_angle = raw_angle;
    float mid_angle = 0.5f * (patch->angle[i] + patch->angle[j]);
    while (query_angle - mid_angle > F_PI) query_angle -= F_TAU;
    while (query_angle - mid_angle < -F_PI) query_angle += F_TAU;
    float ds = patch->along[j] - patch->along[i];
    float da = patch->angle[j] - patch->angle[i];
    float radius = 0.5f * (patch->surface_r[i] + patch->surface_r[j]);
    float den = ds * ds + radius * radius * da * da;
    float u = den > 1e-12f
            ? f_clamp((ds * (p_along - patch->along[i]) +
                        radius * radius * da * (query_angle - patch->angle[i])) /
                       den, 0.0f, 1.0f)
            : 0.0f;
    float axial = fabsf(p_along -
                        (patch->along[i] + (patch->along[j] - patch->along[i]) * u));
    float tangent = radius * fabsf(query_angle -
                       (patch->angle[i] + (patch->angle[j] - patch->angle[i]) * u));
    float wa = patch->width_ax[i] +
               (patch->width_ax[j] - patch->width_ax[i]) * u + margin;
    float wt = patch->width_tan[i] +
               (patch->width_tan[j] - patch->width_tan[i]) * u + margin;
    float score = (axial * axial) / (wa * wa) +
                  (tangent * tangent) / (wt * wt);
    if (score < best) {
      best = score;
      if (out_seg) *out_seg = i;
      if (out_u) *out_u = u;
    }
  }
  return best;
}

static int vmtrig_patch_contains(v3 p, const fig_contact_patch_t *patch,
                                 float margin) {
  return vmtrig_patch_score(p, patch, margin, NULL, NULL) <= 1.0f;
}

static float vmtrig_tri_radius(v3 a, v3 b, v3 c, v3 center) {
  return fmaxf(v3_len(v3_sub(a, center)),
         fmaxf(v3_len(v3_sub(b, center)), v3_len(v3_sub(c, center))));
}

static float vmtrig_origin_segment_dist(v3 a, v3 b) {
  v3 ab = v3_sub(b, a);
  float l2 = v3_dot(ab, ab);
  float t = l2 > 1e-20f ? f_clamp(-v3_dot(a, ab) / l2, 0.0f, 1.0f)
                         : 0.0f;
  return v3_len(v3_add(a, v3_scale(ab, t)));
}

// Projection along the held axis legitimately collapses an axial render triangle to
// a segment. The general Ericson point/triangle routine assumes a non-degenerate
// triangle in its face-region branch; use the exact convex-hull edge distance for the
// collapsed case instead of spuriously returning zero distance from the grip axis.
static float vmtrig_origin_projected_tri_dist(v3 a, v3 b, v3 c) {
  v3 ab = v3_sub(b, a), ac = v3_sub(c, a), bc = v3_sub(c, b);
  float scale2 = fmaxf(v3_dot(ab, ab),
                 fmaxf(v3_dot(ac, ac), v3_dot(bc, bc)));
  float area2 = v3_dot(v3_cross(ab, ac), v3_cross(ab, ac));
  if (area2 <= scale2 * scale2 * 1e-10f)
    return fminf(vmtrig_origin_segment_dist(a, b),
           fminf(vmtrig_origin_segment_dist(b, c),
                 vmtrig_origin_segment_dist(c, a)));
  return pt_tri_dist((v3){{0, 0, 0}}, a, b, c);
}

typedef struct {
  v3 a, b, c, lo, hi;
  int allowed, source, owner, class_mask, boundary;
  float normal_cos;
} vmtrig_patch_leaf_t;

typedef struct {
  v3 lo, hi;
  int first, count, left, right, mask, nearest_mask;
} vmtrig_leaf_bvh_node_t;

typedef struct {
  vmtrig_patch_leaf_t *v;
  vmtrig_leaf_bvh_node_t *node;
  int n, cap, overflow;
  int node_cap, slot;
  int nn, bvh_ready;
  int range[GUN_CONTACT_MESH_MAX][2];
  vmtrig_aabb_t range_box[GUN_CONTACT_MESH_MAX];
  int nranges;
  const fig_contact_patch_t *patch;
  int patch_mode, patch_candidate;
  const gun_mesh_set_t *mesh_set;
  int mesh_range;
} vmtrig_patch_leaves_t;

#define VMTRIG_PATCH_NORMAL_MIN 0.05f
#define VMTRIG_PATCH_LEAF_RADIUS 0.00025f
// Distance leaves alone refine one step further when their 1-Lipschitz interval
// straddles a hard wall threshold. Patch membership and Bite area retain the
// 0.25 mm source partition; this only tightens an otherwise inconclusive depth bound.
#define VMTRIG_WALL_LEAF_RADIUS 0.00010f
#define VMTRIG_HAND_LEAF_CAP 512
#define VMTRIG_HAND_NODE_CAP 128
#define VMTRIG_WEAPON_LEAF_CAP 256
#define VMTRIG_WEAPON_NODE_CAP 128
enum { VMTRIG_PATCH_HAND, VMTRIG_PATCH_WEAPON, VMTRIG_SCRATCH_COUNT };
enum { VMTRIG_CLASS_ALLOWED = 1, VMTRIG_CLASS_FORBIDDEN = 2,
       VMTRIG_CLASS_BOTH = 3 };

static vmtrig_patch_leaf_t g_vmtrig_hand_leaf[VMTRIG_HAND_LEAF_CAP];
static vmtrig_leaf_bvh_node_t g_vmtrig_hand_node[VMTRIG_HAND_NODE_CAP];
static vmtrig_patch_leaf_t g_vmtrig_weapon_leaf[VMTRIG_WEAPON_LEAF_CAP];
static vmtrig_leaf_bvh_node_t g_vmtrig_weapon_node[VMTRIG_WEAPON_NODE_CAP];
static int g_vmtrig_scratch_busy[VMTRIG_SCRATCH_COUNT];
static int g_vmtrig_leaf_peak[VMTRIG_SCRATCH_COUNT];
static int g_vmtrig_node_peak[VMTRIG_SCRATCH_COUNT];

// Only the two mutually facing radial bands may earn the contact exception.  The
// hand's rendered pad spans the feathered 0--4 mm contact rim; the area-weighted Bite
// gate below, not each boundary leaf, owns the authored 2--4 mm mean. A weapon face may
// sit slightly inside the scalar hold reference (faceted/sloped station) or up to
// 4 mm outside it (authored grip ribs/rail lands), but its normal must still face
// outward.  Opposite faces of the same closed primitive therefore remain forbidden
// even when their axial/angular projection is identical.
#define VMTRIG_HAND_RADIAL_IN_MIN 0.0000f
#define VMTRIG_HAND_RADIAL_IN_MAX 0.0040f
#define VMTRIG_WEAPON_RADIAL_LO  (-0.0015f)
#define VMTRIG_WEAPON_RADIAL_HI    0.0040f

static void vmtrig_patch_leaves_init(vmtrig_patch_leaves_t *leaf, int slot) {
  *leaf = (vmtrig_patch_leaves_t){0};
  leaf->slot = slot;
  if (slot < 0 || slot >= VMTRIG_SCRATCH_COUNT ||
      g_vmtrig_scratch_busy[slot]) {
    leaf->overflow = 1;
    return;
  }
  g_vmtrig_scratch_busy[slot] = 1;
  if (slot == VMTRIG_PATCH_HAND) {
    leaf->v = g_vmtrig_hand_leaf;
    leaf->node = g_vmtrig_hand_node;
    leaf->cap = VMTRIG_HAND_LEAF_CAP;
    leaf->node_cap = VMTRIG_HAND_NODE_CAP;
  } else {
    leaf->v = g_vmtrig_weapon_leaf;
    leaf->node = g_vmtrig_weapon_node;
    leaf->cap = VMTRIG_WEAPON_LEAF_CAP;
    leaf->node_cap = VMTRIG_WEAPON_NODE_CAP;
  }
}

static void vmtrig_patch_leaves_release(vmtrig_patch_leaves_t *leaf) {
  if (leaf->slot >= 0 && leaf->slot < VMTRIG_SCRATCH_COUNT &&
      (leaf->v || leaf->node))
    g_vmtrig_scratch_busy[leaf->slot] = 0;
  *leaf = (vmtrig_patch_leaves_t){0};
}

static void vmtrig_patch_leaf_push(vmtrig_patch_leaves_t *out,
                                   v3 a, v3 b, v3 c, int allowed,
                                   int source, float normal_cos) {
  if (out->overflow) return;
  if (vmtrig_v3_nonfinite(a) || vmtrig_v3_nonfinite(b) ||
      vmtrig_v3_nonfinite(c) || f_is_nonfinite(normal_cos)) {
    out->overflow = 1;
    return;
  }
  if (!out->v || out->n >= out->cap) { out->overflow = 1; return; }
  v3 lo = {{fminf(a.x, fminf(b.x, c.x)),
            fminf(a.y, fminf(b.y, c.y)),
            fminf(a.z, fminf(b.z, c.z))}};
  v3 hi = {{fmaxf(a.x, fmaxf(b.x, c.x)),
            fmaxf(a.y, fmaxf(b.y, c.y)),
            fmaxf(a.z, fmaxf(b.z, c.z))}};
  out->v[out->n++] = (vmtrig_patch_leaf_t){
    .a = a, .b = b, .c = c, .lo = lo, .hi = hi,
    .allowed = allowed, .source = source, .owner = -1,
    .class_mask = allowed ? VMTRIG_CLASS_ALLOWED : VMTRIG_CLASS_FORBIDDEN,
    .normal_cos = normal_cos,
  };
  if (out->slot >= 0 && out->slot < VMTRIG_SCRATCH_COUNT &&
      out->n > g_vmtrig_leaf_peak[out->slot])
    g_vmtrig_leaf_peak[out->slot] = out->n;
}

static void vmtrig_patch_range_add(vmtrig_patch_leaves_t *leaf,
                                   const int range[2]) {
  if (leaf->nranges >= GUN_CONTACT_MESH_MAX) {
    leaf->overflow = 1;
    return;
  }
  int r = leaf->nranges++;
  leaf->range[r][0] = range[0];
  leaf->range[r][1] = range[1];
  leaf->range_box[r] = vmtrig_range_aabb(range);
}

static float vmtrig_leaf_center_axis(const vmtrig_patch_leaf_t *leaf, int axis) {
  return (leaf->a.e[axis] + leaf->b.e[axis] + leaf->c.e[axis]) / 3.0f;
}

static void vmtrig_leaf_select(vmtrig_patch_leaf_t *v, int lo, int hi,
                               int nth, int axis) {
  while (lo < hi) {
    float pivot = vmtrig_leaf_center_axis(&v[lo + (hi - lo) / 2], axis);
    int i = lo, j = hi;
    while (i <= j) {
      while (vmtrig_leaf_center_axis(&v[i], axis) < pivot) i++;
      while (vmtrig_leaf_center_axis(&v[j], axis) > pivot) j--;
      if (i <= j) {
        vmtrig_patch_leaf_t t = v[i]; v[i] = v[j]; v[j] = t;
        i++; j--;
      }
    }
    if (nth <= j) hi = j;
    else if (nth >= i) lo = i;
    else return;
  }
}

static int vmtrig_leaf_bvh_node(vmtrig_patch_leaves_t *leaf,
                                int first, int count) {
  if (leaf->overflow || leaf->nn >= leaf->node_cap) {
    leaf->overflow = 1;
    return -1;
  }
  int ni = leaf->nn++;
  if (leaf->slot >= 0 && leaf->slot < VMTRIG_SCRATCH_COUNT &&
      leaf->nn > g_vmtrig_node_peak[leaf->slot])
    g_vmtrig_node_peak[leaf->slot] = leaf->nn;
  vmtrig_leaf_bvh_node_t *node = &leaf->node[ni];
  *node = (vmtrig_leaf_bvh_node_t){
    .lo = {{1e9f, 1e9f, 1e9f}}, .hi = {{-1e9f, -1e9f, -1e9f}},
    .first = first, .count = count, .left = -1, .right = -1};
  for (int i = first; i < first + count; i++) {
    for (int k = 0; k < 3; k++) {
      node->lo.e[k] = fminf(node->lo.e[k], leaf->v[i].lo.e[k]);
      node->hi.e[k] = fmaxf(node->hi.e[k], leaf->v[i].hi.e[k]);
    }
    node->mask |= leaf->v[i].class_mask;
    node->nearest_mask |= leaf->v[i].class_mask;
  }
  if (count <= 8) return ni;
  int axis = 0;
  if (node->hi.y - node->lo.y > node->hi.x - node->lo.x) axis = 1;
  if (node->hi.z - node->lo.z > node->hi.e[axis] - node->lo.e[axis]) axis = 2;
  int mid = first + count / 2;
  vmtrig_leaf_select(leaf->v, first, first + count - 1, mid, axis);
  // Recursive calls may populate later elements but the node allocation is fixed,
  // so reacquire by index instead of keeping a pointer across them.
  int left = vmtrig_leaf_bvh_node(leaf, first, mid - first);
  int right = vmtrig_leaf_bvh_node(leaf, mid, first + count - mid);
  if (left < 0 || right < 0) return -1;
  leaf->node[ni].left = left;
  leaf->node[ni].right = right;
  return ni;
}

static void vmtrig_leaf_bvh_build(vmtrig_patch_leaves_t *leaf) {
  if (leaf->bvh_ready) return;
  leaf->bvh_ready = 1;
  if (leaf->n <= 0) return;
  if (!leaf->node || leaf->node_cap <= 0) {
    leaf->overflow = 1;
    return;
  }
  leaf->nn = 0;
  if (vmtrig_leaf_bvh_node(leaf, 0, leaf->n) < 0) leaf->overflow = 1;
}

static float vmtrig_point_box_dist2(v3 p, v3 lo, v3 hi) {
  float d2 = 0.0f;
  for (int k = 0; k < 3; k++) {
    float d = p.e[k] < lo.e[k] ? lo.e[k] - p.e[k]
            : p.e[k] > hi.e[k] ? p.e[k] - hi.e[k] : 0.0f;
    d2 += d * d;
  }
  return d2;
}

static void vmtrig_leaf_nearest_raw_node(
    v3 p, const vmtrig_patch_leaves_t *leaf, int ni, float *nearest,
    vmtrig_work_t *work) {
  if ((work && work->overflow) || ni < 0 || ni >= leaf->nn) return;
  const vmtrig_leaf_bvh_node_t *node = &leaf->node[ni];
  float box_d2 = vmtrig_point_box_dist2(p, node->lo, node->hi);
  vmtrig_work_add(work, 1);
  if (box_d2 >= *nearest * *nearest) return;
  if (node->left < 0) {
    for (int i = node->first; i < node->first + node->count; i++) {
      const vmtrig_patch_leaf_t *v = &leaf->v[i];
      float leaf_d2 = vmtrig_point_box_dist2(p, v->lo, v->hi);
      if (leaf_d2 >= *nearest * *nearest) continue;
      float d = pt_tri_dist(p, v->a, v->b, v->c);
      if (d < *nearest) *nearest = d;
      vmtrig_work_add(work, 1);
    }
    return;
  }
  int first = node->left, second = node->right;
  float fd2 = vmtrig_point_box_dist2(
      p, leaf->node[first].lo, leaf->node[first].hi);
  float sd2 = vmtrig_point_box_dist2(
      p, leaf->node[second].lo, leaf->node[second].hi);
  if (sd2 < fd2) { int t = first; first = second; second = t; }
  vmtrig_leaf_nearest_raw_node(p, leaf, first, nearest, work);
  vmtrig_leaf_nearest_raw_node(p, leaf, second, nearest, work);
}

// Exact signed depth against one independently closed authored range. Distance uses
// its raw-triangle BVH; sign uses robust multi-ray parity over that same range. This
// never constructs or queries the overlapping whole-weapon union.
static float vmtrig_leaf_range_depth(v3 p,
                                      const vmtrig_patch_leaves_t *leaf,
                                      vmtrig_work_t *work) {
  static const v3 ray[3] = {
    {{0.5773f, 0.5771f, 0.5775f}},
    {{0.3127f, 0.8473f, -0.4291f}},
    {{-0.7311f, 0.2197f, 0.6461f}},
  };
  if (!leaf || !leaf->bvh_ready || leaf->nn <= 0) return 0.0f;
  float nearest = 1e9f;
  vmtrig_leaf_nearest_raw_node(p, leaf, 0, &nearest, work);
  int parity = 0, have_parity = 0;
  for (int r = 0; r < 3 && !have_parity; r++) {
    int cross = 0, ambiguous = 0;
    for (int i = 0; i < leaf->n; i++) {
      const vmtrig_patch_leaf_t *v = &leaf->v[i];
      int hit = vmtrig_ray_tri_class(p, ray[r], v->a, v->b, v->c);
      ambiguous |= hit < 0;
      cross += hit > 0;
      vmtrig_work_add(work, 1);
    }
    if (!ambiguous) { parity = cross & 1; have_parity = 1; }
  }
  if (!have_parity) {
    if (g_vmtrig_inside_ambiguous != UINT64_MAX) g_vmtrig_inside_ambiguous++;
    return -nearest;
  }
  return parity ? nearest : -nearest;
}

enum {
  VMTRIG_HOST_NONE,
  VMTRIG_HOST_BURIED,
  VMTRIG_HOST_OUTSIDE,
  VMTRIG_HOST_STRADDLE,
};

// A host relation is authored on the emitted range. No geometric overlap may invent
// one. A complete source cell is buried when any declared host contains its radius;
// it is outside only when every host excludes that radius.
static int vmtrig_weapon_host_cell(
    v3 center, float radius, const gun_mesh_set_t *set, int q,
    vmtrig_work_t *work) {
  if (!set || q < 0 || q >= set->count || !set->range[q].host_mask)
    return VMTRIG_HOST_NONE;
  uint32_t wanted = set->range[q].host_mask, found = 0;
  int straddle = 0;
  for (int h = 0; h < set->count; h++) {
    uint32_t part = set->range[h].part & wanted;
    if (!part || h == q) continue;
    found |= part;
    int range[2] = {set->range[h].v0, set->range[h].v1};
    float sd = vmtrig_mesh_sd(center, range, work);
    if (sd < -radius - 1e-6f) return VMTRIG_HOST_BURIED;
    if (sd <= radius + 1e-6f) straddle = 1;
  }
  return found != wanted || straddle ? VMTRIG_HOST_STRADDLE
                                     : VMTRIG_HOST_OUTSIDE;
}

// Rigorous segment-by-segment rejection before refining a straddling source triangle.
// Axial intervals are linear. For an angular span below pi, either boundary of the
// extruded polar wedge is also a linear separating half-plane; a triangle wholly on
// its outside cannot contain any point of that patch segment. Only when EVERY path
// segment has such a separator is the leaf known forbidden.
static void vmtrig_patch_radial_bounds(v3 a, v3 b, v3 c,
                                       const fig_contact_patch_t *patch,
                                       float *rmin, float *rmax) {
  v3 p[3] = {a, b, c}, radial[3];
  *rmax = 0.0f;
  for (int i = 0; i < 3; i++) {
    v3 rel = v3_sub(p[i], patch->origin);
    radial[i] = v3_sub(rel, v3_scale(patch->axis,
                                     v3_dot(rel, patch->axis)));
    *rmax = fmaxf(*rmax, v3_len(radial[i]));
  }
  *rmin = vmtrig_origin_projected_tri_dist(radial[0], radial[1], radial[2]);
}

static int vmtrig_patch_disjoint(v3 a, v3 b, v3 c,
                                 const fig_contact_patch_t *patch, int mode) {
  if (!patch || patch->n <= 0) return 1;
  float sa = v3_dot(v3_sub(a, patch->origin), patch->axis);
  float sb = v3_dot(v3_sub(b, patch->origin), patch->axis);
  float sc = v3_dot(v3_sub(c, patch->origin), patch->axis);
  v3 side = v3_norm(v3_cross(patch->axis, patch->radial_ref));
  v3 qa = v3_sub(a, patch->origin), qb = v3_sub(b, patch->origin);
  v3 qc = v3_sub(c, patch->origin);
  qa = v3_sub(qa, v3_scale(patch->axis, v3_dot(qa, patch->axis)));
  qb = v3_sub(qb, v3_scale(patch->axis, v3_dot(qb, patch->axis)));
  qc = v3_sub(qc, v3_scale(patch->axis, v3_dot(qc, patch->axis)));
  float tri_rmin, tri_rmax;
  vmtrig_patch_radial_bounds(a, b, c, patch, &tri_rmin, &tri_rmax);
  int last = patch->n > 1 ? patch->n - 1 : 1;
  for (int i = 0; i < last; i++) {
    int j = patch->n > 1 ? i + 1 : i;
    float max_wa = fmaxf(patch->width_ax[i], patch->width_ax[j]);
    float alo = fminf(patch->along[i], patch->along[j]) - max_wa;
    float ahi = fmaxf(patch->along[i], patch->along[j]) + max_wa;
    if ((sa < alo && sb < alo && sc < alo) ||
        (sa > ahi && sb > ahi && sc > ahi)) continue;
    float surface_lo = fminf(patch->surface_r[i], patch->surface_r[j]);
    float surface_hi = fmaxf(patch->surface_r[i], patch->surface_r[j]);
    float radial_lo = mode == VMTRIG_PATCH_HAND
                    ? surface_lo - VMTRIG_HAND_RADIAL_IN_MAX
                    : surface_lo + VMTRIG_WEAPON_RADIAL_LO;
    float radial_hi = mode == VMTRIG_PATCH_HAND
                    ? surface_hi - VMTRIG_HAND_RADIAL_IN_MIN
                    : surface_hi + VMTRIG_WEAPON_RADIAL_HI;
    if (tri_rmax < radial_lo || tri_rmin > radial_hi) continue;
    float radius = 0.5f * (patch->surface_r[i] + patch->surface_r[j]);
    float angle_pad = fmaxf(patch->width_tan[i], patch->width_tan[j]) /
                      fmaxf(radius, 1e-6f);
    float ang_lo = fminf(patch->angle[i], patch->angle[j]) - angle_pad;
    float ang_hi = fmaxf(patch->angle[i], patch->angle[j]) + angle_pad;
    if (ang_hi - ang_lo >= F_PI - 1e-4f) return 0;
    v3 dlo = v3_add(v3_scale(patch->radial_ref, cosf(ang_lo)),
                    v3_scale(side, sinf(ang_lo)));
    v3 dhi = v3_add(v3_scale(patch->radial_ref, cosf(ang_hi)),
                    v3_scale(side, sinf(ang_hi)));
    float la = v3_dot(v3_cross(dlo, qa), patch->axis);
    float lb = v3_dot(v3_cross(dlo, qb), patch->axis);
    float lc = v3_dot(v3_cross(dlo, qc), patch->axis);
    float ha = v3_dot(v3_cross(dhi, qa), patch->axis);
    float hb = v3_dot(v3_cross(dhi, qb), patch->axis);
    float hc = v3_dot(v3_cross(dhi, qc), patch->axis);
    if (!((la < 0.0f && lb < 0.0f && lc < 0.0f) ||
          (ha > 0.0f && hb > 0.0f && hc > 0.0f))) return 0;
  }
  return 1;
}

// Full-containment certificate for one final leaf. All vertices and the centre must
// be inside the SAME child patch. Then one fixed authored path station must contain
// the entire projected triangle in its ellipse: axial extrema are at vertices, and
// angle along each projected edge is monotone while the radial triangle excludes the
// axis. This proves the whole leaf, without a world-space/angle Lipschitz assumption.
static float vmtrig_patch_min_normal(v3 a, v3 b, v3 c,
                                     const fig_contact_patch_t *patch, int mode) {
  v3 normal = v3_cross(v3_sub(b, a), v3_sub(c, a));
  float nl = v3_len(normal);
  if (nl <= 1e-9f) return -2.0f;
  v3 p[3] = {a, b, c}, radial[3];
  for (int i = 0; i < 3; i++) {
    v3 rel = v3_sub(p[i], patch->origin);
    radial[i] = v3_sub(rel, v3_scale(patch->axis,
                                     v3_dot(rel, patch->axis)));
  }
  float radial_min = vmtrig_origin_projected_tri_dist(
      radial[0], radial[1], radial[2]);
  if (radial_min <= 1e-6f) return -2.0f;
  float lower = 1.0f;
  for (int i = 0; i < 3; i++) {
    v3 facing = mode == VMTRIG_PATCH_HAND
              ? v3_scale(radial[i], -1.0f) : radial[i];
    float den = nl * v3_len(facing);
    if (den <= 1e-12f) return -2.0f;
    lower = fminf(lower, v3_dot(normal, facing) / den);
  }
  // For every convex combination f of the three projected facing vectors,
  // n.f = sum(lambda_i n.f_i) >= lower |n| sum(lambda_i |f_i|)
  //     >= lower |n| |f|.
  // Thus the minimum vertex cosine is a full-triangle lower certificate whenever it
  // can pass the positive facing threshold; radial_min above excludes f=0.
  return lower;
}

static int vmtrig_patch_leaf_cert(v3 a, v3 b, v3 c,
                                  const fig_contact_patch_t *patch, int mode,
                                  float *out_normal) {
  v3 center = v3_scale(v3_add(a, v3_add(b, c)), 1.0f / 3.0f);
  if (!vmtrig_patch_contains(a, patch, 0.0f) ||
      !vmtrig_patch_contains(b, patch, 0.0f) ||
      !vmtrig_patch_contains(c, patch, 0.0f) ||
      !vmtrig_patch_contains(center, patch, 0.0f)) return 0;
  float min_normal = vmtrig_patch_min_normal(a, b, c, patch, mode);
  if (min_normal < VMTRIG_PATCH_NORMAL_MIN) return 0;
  int seg = 0;
  float u = 0.0f;
  float score = vmtrig_patch_score(center, patch, 0.0f, &seg, &u);
  int next = patch->n > 1 ? seg + 1 : seg;
  float wa = patch->width_ax[seg] +
             (patch->width_ax[next] - patch->width_ax[seg]) * u;
  float wt = patch->width_tan[seg] +
             (patch->width_tan[next] - patch->width_tan[seg]) * u;
  if (wa <= 0.0f || wt <= 0.0f || score > 1.0f) return 0;
  float target_along = patch->along[seg] +
                       (patch->along[next] - patch->along[seg]) * u;
  float target_angle = patch->angle[seg] +
                       (patch->angle[next] - patch->angle[seg]) * u;
  float surface_r = patch->surface_r[seg] +
                    (patch->surface_r[next] - patch->surface_r[seg]) * u;
  v3 side = v3_norm(v3_cross(patch->axis, patch->radial_ref));
  v3 p[3] = {a, b, c}, radial[3];
  float max_ax = 0.0f, max_ang = 0.0f;
  for (int i = 0; i < 3; i++) {
    v3 rel = v3_sub(p[i], patch->origin);
    float along = v3_dot(rel, patch->axis);
    radial[i] = v3_sub(rel, v3_scale(patch->axis, along));
    float angle = atan2f(v3_dot(radial[i], side),
                         v3_dot(radial[i], patch->radial_ref));
    while (angle - target_angle > F_PI) angle -= F_TAU;
    while (angle - target_angle < -F_PI) angle += F_TAU;
    max_ax = fmaxf(max_ax, fabsf(along - target_along));
    max_ang = fmaxf(max_ang, fabsf(angle - target_angle));
  }
  float radial_min = vmtrig_origin_projected_tri_dist(
      radial[0], radial[1], radial[2]);
  float radial_max = fmaxf(v3_len(radial[0]),
                     fmaxf(v3_len(radial[1]), v3_len(radial[2])));
  if (radial_min <= 1e-6f) return 0;
  if (mode == VMTRIG_PATCH_HAND) {
    if (radial_min < surface_r - VMTRIG_HAND_RADIAL_IN_MAX ||
        radial_max > surface_r - VMTRIG_HAND_RADIAL_IN_MIN) return 0;
  } else {
    if (radial_min < surface_r + VMTRIG_WEAPON_RADIAL_LO ||
        radial_max > surface_r + VMTRIG_WEAPON_RADIAL_HI) return 0;
  }
  float axn = max_ax / wa, tann = surface_r * max_ang / wt;
  int certified = axn * axn + tann * tann <= 1.0f;
  if (certified && out_normal) *out_normal = min_normal;
  return certified;
}

static int vmtrig_patch_selftest(void) {
  fig_contact_patch_t patch = {
    .name = "self", .n = 1,
    .axis = {{0, 0, 1}}, .radial_ref = {{1, 0, 0}},
  };
  patch.width_ax[0] = patch.width_tan[0] = 0.005f;
  patch.surface_r[0] = 0.020f;
  v3 a = {{0.020f, -0.001f, -0.001f}};
  v3 b = {{0.020f,  0.001f, -0.001f}};
  v3 c = {{0.020f,  0.000f,  0.001f}};
  float normal = -2.0f;
  int fail = !vmtrig_patch_leaf_cert(a, b, c, &patch,
                                      VMTRIG_PATCH_WEAPON, &normal) ||
             normal < VMTRIG_PATCH_NORMAL_MIN;
  {
    float lower = vmtrig_patch_min_normal(a, b, c, &patch,
                                           VMTRIG_PATCH_WEAPON);
    v3 n = v3_cross(v3_sub(b, a), v3_sub(c, a));
    for (int iu = 0; iu <= 64; iu++)
      for (int iv = 0; iv <= 64 - iu; iv++) {
        float u = (float)iu / 64.0f, v = (float)iv / 64.0f;
        float w = 1.0f - u - v;
        v3 p = v3_add(v3_scale(a, w),
                      v3_add(v3_scale(b, u), v3_scale(c, v)));
        v3 rel = v3_sub(p, patch.origin);
        v3 radial = v3_sub(rel, v3_scale(patch.axis,
                                         v3_dot(rel, patch.axis)));
        float den = v3_len(n) * v3_len(radial);
        fail |= den <= 1e-12f || v3_dot(n, radial) / den + 1e-6f < lower;
      }
  }
  fail |= vmtrig_patch_leaf_cert(a, c, b, &patch,
                                  VMTRIG_PATCH_WEAPON, NULL);
  v3 ta = {{0.0195f, -0.001f, 0}}, tb = {{0.0205f, -0.001f, 0}};
  v3 tc = {{0.0200f,  0.001f, 0}};
  fail |= vmtrig_patch_leaf_cert(ta, tb, tc, &patch,
                                  VMTRIG_PATCH_WEAPON, NULL);
  v3 outside = {{0.020f, 0.008f, 0}};
  fail |= vmtrig_patch_leaf_cert(a, b, outside, &patch,
                                  VMTRIG_PATCH_WEAPON, NULL);
  v3 da = {{0.020f, 0, 0}}, db = {{0.021f, 0, 0}};
  v3 dc = {{0.022f, 0, 0}};
  fail |= fabsf(vmtrig_origin_projected_tri_dist(da, db, dc) - 0.020f) >
          1e-6f;

  vmtrig_patch_leaves_t leaf;
  vmtrig_patch_leaves_init(&leaf, VMTRIG_PATCH_WEAPON);
  leaf.n = leaf.cap;
  vmtrig_patch_leaf_push(&leaf, a, b, c, 1, 0, normal);
  fail |= !leaf.overflow;
  vmtrig_patch_leaves_release(&leaf);
  return fail;
}

static float vmtrig_patch_normal(v3 a, v3 b, v3 c,
                                 const fig_contact_patch_t *patch, int mode) {
  v3 center = v3_scale(v3_add(a, v3_add(b, c)), 1.0f / 3.0f);
  v3 normal = v3_cross(v3_sub(b, a), v3_sub(c, a));
  v3 rel = v3_sub(center, patch->origin);
  v3 axis_at = v3_add(patch->origin,
                      v3_scale(patch->axis, v3_dot(rel, patch->axis)));
  v3 facing = mode == VMTRIG_PATCH_HAND ? v3_sub(axis_at, center)
                                         : v3_sub(center, axis_at);
  float den = v3_len(normal) * v3_len(facing);
  return den > 1e-9f ? v3_dot(normal, facing) / den : -2.0f;
}

static int vmtrig_patch_point_class(v3 p, v3 a, v3 b, v3 c,
                                    const fig_contact_patch_t *patch,
                                    int mode) {
  int seg = 0;
  float u = 0.0f;
  if (!patch) return VMTRIG_CLASS_FORBIDDEN;
  float score = vmtrig_patch_score(p, patch, 0.0f, &seg, &u);
  int next = patch->n > 1 ? seg + 1 : seg;
  float surface_r = patch->surface_r[seg] +
                    (patch->surface_r[next] - patch->surface_r[seg]) * u;
  v3 rel = v3_sub(p, patch->origin);
  float along = v3_dot(rel, patch->axis);
  v3 radial = v3_sub(rel, v3_scale(patch->axis, along));
  float r = v3_len(radial);
  float radial_lo, radial_hi;
  if (mode == VMTRIG_PATCH_HAND) {
    radial_lo = surface_r - VMTRIG_HAND_RADIAL_IN_MAX;
    radial_hi = surface_r - VMTRIG_HAND_RADIAL_IN_MIN;
    radial = v3_scale(radial, -1.0f);
  } else {
    radial_lo = surface_r + VMTRIG_WEAPON_RADIAL_LO;
    radial_hi = surface_r + VMTRIG_WEAPON_RADIAL_HI;
  }
  v3 normal = v3_cross(v3_sub(b, a), v3_sub(c, a));
  float den = v3_len(normal) * v3_len(radial);
  float facing = den > 1e-9f ? v3_dot(normal, radial) / den : -2.0f;
  float spatial_margin = 1.0f - score;
  float radial_margin = fminf(r - radial_lo, radial_hi - r);
  float facing_margin = facing - VMTRIG_PATCH_NORMAL_MIN;
  if (spatial_margin > 1e-4f && radial_margin > 1e-6f &&
      facing_margin > 1e-4f) return VMTRIG_CLASS_ALLOWED;
  if (spatial_margin < -1e-4f || radial_margin < -1e-6f ||
      facing_margin < -1e-4f) return VMTRIG_CLASS_FORBIDDEN;
  return VMTRIG_CLASS_BOTH;
}

static void vmtrig_patch_partition(v3 a, v3 b, v3 c,
                                   const fig_contact_patch_t *patch,
                                   int mode, int candidate, float normal_cos, int source,
                                   vmtrig_patch_leaves_t *out) {
  if (out->overflow) return;
  if (!candidate || vmtrig_patch_disjoint(a, b, c, patch, mode)) {
    vmtrig_patch_leaf_push(out, a, b, c, 0, source, normal_cos);
    return;
  }
  float leaf_normal = -2.0f;
  if (vmtrig_patch_leaf_cert(a, b, c, patch, mode, &leaf_normal)) {
    vmtrig_patch_leaf_push(out, a, b, c, 1, source, leaf_normal);
    return;
  }
  int before = out->n;
  vmtrig_patch_leaf_push(out, a, b, c, 0, source, normal_cos);
  if (out->n > before) {
    out->v[before].boundary = 1;
    out->v[before].class_mask = VMTRIG_CLASS_BOTH;
  }
}

static void vmtrig_hand_patch_leaves(v3 a, v3 b, v3 c,
                                     const fig_contact_patch_t *patch,
                                     const char *name, int source,
                                     vmtrig_patch_leaves_t *out) {
  float normal = patch ? vmtrig_patch_normal(a, b, c, patch,
                                              VMTRIG_PATCH_HAND) : -2.0f;
  // A broad source facet can cross the local radial-normal boundary even though its
  // one center witness is back-facing. Refine every spatial/radial candidate; only
  // vmtrig_patch_leaf_cert may grant the normal exception to a final leaf.
  int candidate = patch && strcmp(name, "trigger_digit");
  vmtrig_patch_partition(a, b, c, patch, VMTRIG_PATCH_HAND, candidate,
                         normal, source, out);
}

static void vmtrig_weapon_patch_leaves(v3 a, v3 b, v3 c,
                                       const fig_contact_patch_t *patch,
                                       int contact_surface, int source,
                                       vmtrig_patch_leaves_t *out) {
  float normal = patch ? vmtrig_patch_normal(a, b, c, patch,
                                              VMTRIG_PATCH_WEAPON) : -2.0f;
  int candidate = contact_surface && patch &&
                  strcmp(patch->name, "trigger_digit");
  vmtrig_patch_partition(a, b, c, patch, VMTRIG_PATCH_WEAPON, candidate,
                         normal, source, out);
}

static int vmtrig_tri_box_overlap(v3 a, v3 b, v3 c, vmtrig_aabb_t box) {
  for (int k = 0; k < 3; k++) {
    float lo = fminf(a.e[k], fminf(b.e[k], c.e[k]));
    float hi = fmaxf(a.e[k], fmaxf(b.e[k], c.e[k]));
    if (hi < box.lo.e[k] || lo > box.hi.e[k]) return 0;
  }
  return 1;
}

typedef struct {
  double depth_lo_area, depth_hi_area, boundary_depth_hi_area;
  double inside_area, boundary_area;
  uint64_t samples;
  float max_upper, boundary_upper;
  int fail;
} vmtrig_bite_t;

static int vmtrig_bite_gate_straddle(float lo, float hi,
                                     int source_uncertain);

// Local closest-point witness used only by Bite ownership. The ordinary SD query
// remains per independently closed range; this witness lets the reducer reject a
// nearest face that lies inside another authored range instead of averaging an
// internal panel/back face into the physical hand-area mean.
static v3 vmtrig_tri_closest(v3 p, v3 a, v3 b, v3 c) {
  v3 ab = v3_sub(b, a), ac = v3_sub(c, a), ap = v3_sub(p, a);
  float d1 = v3_dot(ab, ap), d2 = v3_dot(ac, ap);
  if (d1 <= 0.0f && d2 <= 0.0f) return a;
  v3 bp = v3_sub(p, b);
  float d3 = v3_dot(ab, bp), d4 = v3_dot(ac, bp);
  if (d3 >= 0.0f && d4 <= d3) return b;
  float vc = d1 * d4 - d3 * d2;
  if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
    return v3_add(a, v3_scale(ab, d1 / (d1 - d3)));
  v3 cp = v3_sub(p, c);
  float d5 = v3_dot(ab, cp), d6 = v3_dot(ac, cp);
  if (d6 >= 0.0f && d5 <= d6) return c;
  float vb = d5 * d2 - d1 * d6;
  if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
    return v3_add(a, v3_scale(ac, d2 / (d2 - d6)));
  float va = d3 * d6 - d5 * d4;
  if (va <= 0.0f && d4 - d3 >= 0.0f && d5 - d6 >= 0.0f)
    return v3_add(b, v3_scale(v3_sub(c, b),
                  (d4 - d3) / ((d4 - d3) + (d5 - d6))));
  float den = 1.0f / (va + vb + vc);
  return v3_add(a, v3_add(v3_scale(ab, vb * den),
                           v3_scale(ac, vc * den)));
}

// Exact-facet Bite is opt-in metadata.  It never changes the ordinary patch,
// crossing, guard, AABB or complement proofs; it only replaces the conservative
// signed-distance integration for a tagged contact-front triangle.  Cells live on
// this bounded DFS stack and are never copied into either leaf BVH.
#define VMTRIG_EXACT_CERT_STACK_CAP 32
#define VMTRIG_EXACT_CERT_DEPTH_CAP 14
#define VMTRIG_EXACT_CERT_CELL_LIMIT UINT64_C(131072)
#define VMTRIG_EXACT_CERT_RADIUS 0.00015f
#define VMTRIG_EXACT_CERT_EPS 0.000001f
#define VMTRIG_EXACT_CERT_DEPTH_EPS 0.000004f
#define VMTRIG_EXACT_CERT_WORK_LIMIT UINT64_C(320000000)
#define VMTRIG_EXACT_CERT_CANDIDATE_CAP (2 * FIG_KMAX + 2)

// A plane can leave one triangle and one quad, hence at most three children.
// Depth-first traversal can therefore retain at most two siblings per level.
static_assert(VMTRIG_EXACT_CERT_STACK_CAP >=
              1 + 2 * VMTRIG_EXACT_CERT_DEPTH_CAP,
              "exact certificate stack must cover its full split depth");

typedef struct {
  v3 p[3];
  unsigned depth;
} vmtrig_exact_cert_cell_t;

typedef struct {
  uint64_t work, cells, accepted;
  uint64_t work_limit;
  unsigned stack_limit;
  int overflow;
} vmtrig_exact_cert_ctx_t;

typedef struct {
  v3 a, b, c, n;
  float depth[3];
  int source;
} vmtrig_exact_cert_facet_t;

enum {
  VMTRIG_EXACT_DROP,
  VMTRIG_EXACT_ACCEPT,
  VMTRIG_EXACT_REFINE,
  VMTRIG_EXACT_SPLIT,
  VMTRIG_EXACT_FAIL,
};

static int vmtrig_exact_cert_work(vmtrig_exact_cert_ctx_t *ctx,
                                  vmtrig_work_t *work, uint64_t add) {
  if (!ctx || ctx->overflow || ctx->work > ctx->work_limit ||
      add > ctx->work_limit - ctx->work) {
    if (ctx) ctx->overflow = 1;
    return 0;
  }
  ctx->work += add;
  vmtrig_work_add(work, add);
  if (work && work->overflow) { ctx->overflow = 1; return 0; }
  return 1;
}

static int vmtrig_exact_cert_patch_valid(const fig_contact_patch_t *patch) {
  if (!patch || !patch->name || patch->n <= 0 || patch->n > FIG_NMAX ||
      vmtrig_v3_nonfinite(patch->origin) ||
      vmtrig_v3_nonfinite(patch->axis) ||
      vmtrig_v3_nonfinite(patch->radial_ref) ||
      v3_dot(patch->axis, patch->axis) < 0.999f ||
      v3_dot(patch->axis, patch->axis) > 1.001f ||
      v3_dot(patch->radial_ref, patch->radial_ref) < 0.999f ||
      v3_dot(patch->radial_ref, patch->radial_ref) > 1.001f ||
      fabsf(v3_dot(patch->axis, patch->radial_ref)) > 0.001f) return 0;
  for (int i = 0; i < patch->n; i++)
    if (vmtrig_v3_nonfinite(patch->p[i]) ||
        vmtrig_v3_nonfinite(patch->center[i]) ||
        f_is_nonfinite(patch->width_ax[i]) ||
        f_is_nonfinite(patch->width_tan[i]) ||
        f_is_nonfinite(patch->along[i]) ||
        f_is_nonfinite(patch->angle[i]) ||
        f_is_nonfinite(patch->surface_r[i]) ||
        patch->width_ax[i] <= 0.0f || patch->width_tan[i] <= 0.0f ||
        patch->surface_r[i] <= 0.0f) return 0;

  const gun_hold_profile_t *profile = &patch->endpoint_profile;
  const float scalar[] = {
    profile->rx, profile->rz, profile->box01, profile->ref_r,
    profile->fore_r, profile->fore3_r, profile->center,
    profile->axial_lo, profile->axial_mid, profile->axial_hi,
    profile->axial_rx_lo, profile->axial_rz_lo,
    profile->axial_rx_hi, profile->axial_rz_hi,
    profile->axial_center_lo, profile->axial_center_mid,
    profile->axial_center_hi, profile->side_inner, profile->side_outer,
    profile->side_u_lo, profile->side_u_hi,
  };
  for (int i = 0; i < (int)(sizeof scalar / sizeof scalar[0]); i++)
    if (f_is_nonfinite(scalar[i])) return 0;
  if (!profile->enabled || profile->sides < 4 ||
      profile->sides > FIG_KMAX || profile->rx <= 0.0f ||
      profile->rz <= 0.0f || profile->ref_r <= 0.0f ||
      profile->box01 < 0.0f || profile->box01 > 1.0f ||
      profile->under_count < 0 ||
      profile->under_count > GUN_HOLD_UNDER_MAX ||
      profile->section_n < 0 || profile->section_n == 1 ||
      profile->section_n > GUN_HOLD_SECTION_MAX ||
      (profile->axial3 != 0 && profile->axial3 != 1) ||
      vmtrig_v3_nonfinite(profile->u) ||
      v3_dot(gun_profile_u(profile, patch->axis),
             gun_profile_u(profile, patch->axis)) <= 1e-10f) return 0;
  for (int i = 0; i < GUN_HOLD_SECTION_MAX; i++) {
    if (f_is_nonfinite(profile->section_along[i]) ||
        f_is_nonfinite(profile->section_rx[i]) ||
        f_is_nonfinite(profile->section_rz[i]) ||
        f_is_nonfinite(profile->section_center[i])) return 0;
    if (i < profile->section_n &&
        (profile->section_rx[i] <= 0.0f || profile->section_rz[i] <= 0.0f ||
         (i && profile->section_along[i] <=
               profile->section_along[i - 1]))) return 0;
  }
  for (int i = 0; i < GUN_HOLD_UNDER_MAX; i++) {
    if (f_is_nonfinite(profile->under[i].inner) ||
        f_is_nonfinite(profile->under[i].outer) ||
        f_is_nonfinite(profile->under[i].v_half)) return 0;
    if (i < profile->under_count &&
        (profile->under[i].inner < 0.0f ||
         profile->under[i].outer <= profile->under[i].inner ||
         profile->under[i].v_half <= 0.0f)) return 0;
  }
  if (profile->axial3 &&
      (!(profile->axial_lo < profile->axial_mid) ||
       !(profile->axial_mid < profile->axial_hi) ||
       profile->axial_rx_lo <= 0.0f || profile->axial_rz_lo <= 0.0f ||
       profile->axial_rx_hi <= 0.0f || profile->axial_rz_hi <= 0.0f))
    return 0;
  if (profile->side_outer != 0.0f &&
      (profile->side_inner < 0.0f ||
       profile->side_outer <= profile->side_inner ||
       profile->side_u_hi <= profile->side_u_lo)) return 0;
  return 1;
}

static uint32_t vmtrig_exact_cert_profile_bands(
    const gun_hold_profile_t *profile) {
  int bands = profile->section_n >= 2 ? profile->section_n - 1
            : profile->axial3 ? 2 : 1;
  return (UINT32_C(1) << bands) - 1u;
}

static int vmtrig_exact_cert_body_range(const fig_contact_patch_t *patch,
                                         gun_contact_kind_t kind,
                                         const gun_mesh_set_t *set,
                                         int *body_range) {
  const uint32_t body_mask = GUN_MESH_PART_GRIP_BODY |
                             GUN_MESH_PART_FORE_BODY |
                             GUN_MESH_PART_MAG_BODY;
  if (!patch || !patch->cert_tag) return 0;
  uint32_t part = FIG_CONTACT_CERT_PART(patch->cert_tag);
  uint32_t sections = FIG_CONTACT_CERT_SECTIONS(patch->cert_tag);
  int tagged_kind = FIG_CONTACT_CERT_KIND(patch->cert_tag);
  int tagged_range = FIG_CONTACT_CERT_RANGE(patch->cert_tag);
  if (!set || set->overflow || set->count <= 0 ||
      set->count > GUN_CONTACT_MESH_MAX || tagged_range < 0 ||
      tagged_range >= set->count) return -1;
  uint32_t expected_part = kind == GUN_CONTACT_GRIP
                         ? GUN_MESH_PART_GRIP_BODY
                         : kind == GUN_CONTACT_FORE
                         ? GUN_MESH_PART_FORE_BODY
                         : kind == GUN_CONTACT_MAG
                         ? GUN_MESH_PART_MAG_BODY : 0;
  if (!vmtrig_exact_cert_patch_valid(patch) || !sections ||
      (sections & ~vmtrig_exact_cert_profile_bands(
                      &patch->endpoint_profile)) ||
      (patch->cert_tag & UINT32_C(0x80000000)) ||
      tagged_kind != (int)kind || !part || part != expected_part ||
      (part & (part - 1u)) || !(part & body_mask)) return -1;
  const gun_mesh_range_t *range = &set->range[tagged_range];
  int floats = range->v1 - range->v0;
  if (!range->surface || range->part != part || range->host_mask != 0 ||
      range->v0 < 0 || range->v1 > g_scene_floats || floats <= 0 ||
      floats % (3 * VERT_FLOATS)) return -1;
  *body_range = tagged_range;
  return 1;
}

static void vmtrig_exact_cert_bounds(const v3 p[3], v3 *lo, v3 *hi) {
  *lo = *hi = p[0];
  for (int i = 1; i < 3; i++)
    for (int k = 0; k < 3; k++) {
      lo->e[k] = fminf(lo->e[k], p[i].e[k]);
      hi->e[k] = fmaxf(hi->e[k], p[i].e[k]);
    }
}

static int vmtrig_exact_cert_box_overlap(v3 alo, v3 ahi,
                                         v3 blo, v3 bhi, float expand) {
  for (int k = 0; k < 3; k++)
    if (ahi.e[k] < blo.e[k] - expand ||
        alo.e[k] > bhi.e[k] + expand) return 0;
  return 1;
}

static float vmtrig_exact_cert_area(const v3 p[3]) {
  return 0.5f * v3_len(v3_cross(v3_sub(p[1], p[0]),
                                 v3_sub(p[2], p[0])));
}

static int vmtrig_exact_cert_longest_split(
    const vmtrig_exact_cert_cell_t *cell, vmtrig_exact_cert_cell_t out[2]) {
  float edge[3] = {
    v3_len(v3_sub(cell->p[1], cell->p[0])),
    v3_len(v3_sub(cell->p[2], cell->p[1])),
    v3_len(v3_sub(cell->p[0], cell->p[2])),
  };
  int a = 0, b = 1, c = 2;
  if (edge[1] >= edge[0] && edge[1] >= edge[2]) { a = 1; b = 2; c = 0; }
  else if (edge[2] >= edge[0]) { a = 2; b = 0; c = 1; }
  v3 m = v3_lerp(cell->p[a], cell->p[b], 0.5f);
  out[0] = (vmtrig_exact_cert_cell_t){
    .p = {cell->p[a], m, cell->p[c]}, .depth = cell->depth + 1};
  out[1] = (vmtrig_exact_cert_cell_t){
    .p = {m, cell->p[b], cell->p[c]}, .depth = cell->depth + 1};
  return 2;
}

// Split one triangle on the exact zero set of an affine scalar.  The two clipped
// polygons share only their zero-area edge; triangulating each fan neither drops nor
// duplicates area.  This is used for real facet edges and facet-depth tie planes.
static int vmtrig_exact_cert_plane_split(
    const vmtrig_exact_cert_cell_t *cell, const float value[3],
    vmtrig_exact_cert_cell_t out[3]) {
  int nout = 0;
  for (int side = -1; side <= 1; side += 2) {
    v3 poly[4];
    int n = 0;
    for (int i = 0; i < 3; i++) {
      int j = (i + 1) % 3;
      float vi = value[i] * (float)side;
      float vj = value[j] * (float)side;
      int inside_i = vi >= 0.0f, inside_j = vj >= 0.0f;
      if (inside_i) poly[n++] = cell->p[i];
      if (inside_i != inside_j) {
        float den = value[i] - value[j];
        if (fabsf(den) <= 1e-20f || n >= 4) return 0;
        float t = f_clamp(value[i] / den, 0.0f, 1.0f);
        poly[n++] = v3_lerp(cell->p[i], cell->p[j], t);
      }
    }
    for (int i = 1; i + 1 < n; i++) {
      if (nout >= 3) return 0;
      v3 tri[3] = {poly[0], poly[i], poly[i + 1]};
      if (vmtrig_exact_cert_area(tri) <= 1e-16f) continue;
      out[nout++] = (vmtrig_exact_cert_cell_t){
        .p = {tri[0], tri[1], tri[2]}, .depth = cell->depth + 1};
    }
  }
  return nout;
}

static int vmtrig_exact_cert_push(vmtrig_exact_cert_cell_t *stack, int *sp,
                                  unsigned cap,
                                  const vmtrig_exact_cert_cell_t *child,
                                  int nchild) {
  if (nchild <= 0 || (unsigned)(*sp + nchild) > cap) return 0;
  for (int i = nchild - 1; i >= 0; i--) stack[(*sp)++] = child[i];
  return 1;
}

static float vmtrig_exact_cert_edge(v3 a, v3 b, v3 n, v3 p) {
  float len = v3_len(v3_sub(b, a));
  if (len <= 1e-9f) return -1e9f;
  return v3_dot(v3_cross(v3_sub(b, a), v3_sub(p, a)), n) / len;
}

static int vmtrig_exact_cert_facet_load(
    const gun_mesh_set_t *set, int range, int source,
    vmtrig_exact_cert_facet_t *facet) {
  int f = set->range[range].v0 + source * 3 * VERT_FLOATS;
  if (f < set->range[range].v0 ||
      f + 2 * VERT_FLOATS >= set->range[range].v1) return 0;
  facet->a = SCENE_V(f);
  facet->b = SCENE_V(f + VERT_FLOATS);
  facet->c = SCENE_V(f + 2 * VERT_FLOATS);
  v3 cross = v3_cross(v3_sub(facet->b, facet->a),
                       v3_sub(facet->c, facet->a));
  float len = v3_len(cross);
  if (len <= 1e-9f) return 0;
  facet->n = v3_scale(cross, 1.0f / len);
  facet->source = source;
  return !vmtrig_v3_nonfinite(facet->a) &&
         !vmtrig_v3_nonfinite(facet->b) &&
         !vmtrig_v3_nonfinite(facet->c) &&
         !vmtrig_v3_nonfinite(facet->n);
}

static int vmtrig_exact_cert_facet_profile(
    const vmtrig_exact_cert_facet_t *facet,
    const fig_contact_patch_t *patch) {
  const gun_hold_profile_t *profile = &patch->endpoint_profile;
  const v3 vertex[3] = {facet->a, facet->b, facet->c};
  float along[3] = {
    v3_dot(v3_sub(facet->a, patch->origin), patch->axis),
    v3_dot(v3_sub(facet->b, patch->origin), patch->axis),
    v3_dot(v3_sub(facet->c, patch->origin), patch->axis),
  };
  for (int i = 0; i < 3; i++) {
    v3 section_c = v3_add(
      patch->origin,
      v3_add(v3_scale(patch->axis, along[i]),
             hand_profile_endpoint_center(profile, patch->axis, along[i])));
    v3 radial = v3_sub(vertex[i], section_c);
    float radius = v3_len(radial);
    if (radius <= 1e-7f) return 0;
    v3 dir = v3_scale(radial, 1.0f / radius);
    gun_profile_hit_t hit = gun_profile_query(
      profile, patch->axis, dir, along[i]);
    // Profile normals intentionally lack axial taper; they bind ring vertices,
    // while the real captured triangle below remains the normal authority.
    if (!hit.valid || f_is_nonfinite(hit.t) ||
        fabsf(radius - hit.t) > VMTRIG_EXACT_CERT_DEPTH_EPS) return 0;
  }
  return 1;
}

static uint32_t vmtrig_exact_cert_facet_band(
    const vmtrig_exact_cert_facet_t *facet,
    const fig_contact_patch_t *patch) {
  const gun_hold_profile_t *profile = &patch->endpoint_profile;
  float along[3] = {
    v3_dot(v3_sub(facet->a, patch->origin), patch->axis),
    v3_dot(v3_sub(facet->b, patch->origin), patch->axis),
    v3_dot(v3_sub(facet->c, patch->origin), patch->axis),
  };
  float lo = fminf(along[0], fminf(along[1], along[2]));
  float hi = fmaxf(along[0], fmaxf(along[1], along[2]));
  // Axial caps are never contact BODY bands.
  if (hi - lo <= VMTRIG_EXACT_CERT_EPS ||
      fabsf(v3_dot(facet->n, patch->axis)) > 0.999999f) return 0;
  if (profile->section_n >= 2) {
    for (int i = 0; i + 1 < profile->section_n; i++)
      if (lo >= profile->section_along[i] - VMTRIG_EXACT_CERT_DEPTH_EPS &&
          hi <= profile->section_along[i + 1] +
                VMTRIG_EXACT_CERT_DEPTH_EPS)
        return UINT32_C(1) << i;
    return 0;
  }
  if (profile->axial3) {
    const float section[3] = {
      profile->axial_lo, profile->axial_mid, profile->axial_hi};
    for (int i = 0; i < 2; i++)
      if (lo >= section[i] - VMTRIG_EXACT_CERT_DEPTH_EPS &&
          hi <= section[i + 1] + VMTRIG_EXACT_CERT_DEPTH_EPS)
        return UINT32_C(1) << i;
    return 0;
  }
  return UINT32_C(1);
}

// A range AABB may cover gaps between disjoint panels.  Resolve foreign geometry
// down to its real captured triangles so a BODY certificate is not defeated merely
// by that broad box, while every actual detail/second body in the swept footprint
// remains fail-closed regardless of its host annotation.
static int vmtrig_exact_cert_foreign_overlap(
    const gun_mesh_set_t *set, const vmtrig_aabb_t *range_box, int body_range,
    v3 swept_lo, v3 swept_hi, vmtrig_exact_cert_ctx_t *ctx,
    vmtrig_work_t *work) {
  for (int q = 0; q < set->count; q++) {
    if (q == body_range) continue;
    if (!vmtrig_exact_cert_work(ctx, work, 1)) return -1;
    int floats = set->range[q].v1 - set->range[q].v0;
    if (set->range[q].v0 < 0 || set->range[q].v1 > g_scene_floats ||
        floats <= 0 || floats % (3 * VERT_FLOATS)) return -1;
    if (!vmtrig_exact_cert_box_overlap(swept_lo, swept_hi,
                                       range_box[q].lo, range_box[q].hi,
                                       VMTRIG_EXACT_CERT_EPS)) continue;
    int count = floats / (3 * VERT_FLOATS);
    for (int source = 0; source < count; source++) {
      if (!vmtrig_exact_cert_work(ctx, work, 1)) return -1;
      vmtrig_exact_cert_facet_t facet;
      if (!vmtrig_exact_cert_facet_load(set, q, source, &facet)) return -1;
      v3 p[3] = {facet.a, facet.b, facet.c}, lo, hi;
      vmtrig_exact_cert_bounds(p, &lo, &hi);
      if (vmtrig_exact_cert_box_overlap(swept_lo, swept_hi, lo, hi,
                                        VMTRIG_EXACT_CERT_EPS)) return 1;
    }
  }
  return 0;
}

// The exact cert owns depth and facing, so its patch predicate is footprint-only.
// It retains the legacy leaf proof's fixed-station ellipse certificate without
// reintroducing the interpolated radial surface that this path replaces.
static int vmtrig_exact_cert_patch_inside(
    const vmtrig_exact_cert_cell_t *cell,
    const fig_contact_patch_t *patch) {
  v3 center = v3_scale(v3_add(cell->p[0],
                        v3_add(cell->p[1], cell->p[2])), 1.0f / 3.0f);
  if (!vmtrig_patch_contains(cell->p[0], patch, 0.0f) ||
      !vmtrig_patch_contains(cell->p[1], patch, 0.0f) ||
      !vmtrig_patch_contains(cell->p[2], patch, 0.0f) ||
      !vmtrig_patch_contains(center, patch, 0.0f)) return 0;
  int seg = 0;
  float u = 0.0f;
  float score = vmtrig_patch_score(center, patch, 0.0f, &seg, &u);
  int next = patch->n > 1 ? seg + 1 : seg;
  float wa = patch->width_ax[seg] +
             (patch->width_ax[next] - patch->width_ax[seg]) * u;
  float wt = patch->width_tan[seg] +
             (patch->width_tan[next] - patch->width_tan[seg]) * u;
  if (wa <= 0.0f || wt <= 0.0f || score > 1.0f) return 0;
  float target_along = patch->along[seg] +
                       (patch->along[next] - patch->along[seg]) * u;
  float target_angle = patch->angle[seg] +
                       (patch->angle[next] - patch->angle[seg]) * u;
  float surface_r = patch->surface_r[seg] +
                    (patch->surface_r[next] - patch->surface_r[seg]) * u;
  v3 side = v3_norm(v3_cross(patch->axis, patch->radial_ref));
  float max_ax = 0.0f, max_ang = 0.0f;
  v3 radial[3];
  for (int i = 0; i < 3; i++) {
    v3 rel = v3_sub(cell->p[i], patch->origin);
    float along = v3_dot(rel, patch->axis);
    radial[i] = v3_sub(rel, v3_scale(patch->axis, along));
    float angle = atan2f(v3_dot(radial[i], side),
                         v3_dot(radial[i], patch->radial_ref));
    while (angle - target_angle > F_PI) angle -= F_TAU;
    while (angle - target_angle < -F_PI) angle += F_TAU;
    max_ax = fmaxf(max_ax, fabsf(along - target_along));
    max_ang = fmaxf(max_ang, fabsf(angle - target_angle));
  }
  if (vmtrig_origin_projected_tri_dist(
        radial[0], radial[1], radial[2]) <= 1e-6f) return 0;
  float axn = max_ax / wa;
  float tann = surface_r * max_ang / wt;
  return axn * axn + tann * tann <= 1.0f;
}

static int vmtrig_exact_cert_patch_disjoint(
    const vmtrig_exact_cert_cell_t *cell,
    const fig_contact_patch_t *patch) {
  if (!patch || patch->n <= 0) return 1;
  float along[3];
  v3 radial[3];
  v3 side = v3_norm(v3_cross(patch->axis, patch->radial_ref));
  for (int i = 0; i < 3; i++) {
    v3 rel = v3_sub(cell->p[i], patch->origin);
    along[i] = v3_dot(rel, patch->axis);
    radial[i] = v3_sub(rel, v3_scale(patch->axis, along[i]));
  }
  int last = patch->n > 1 ? patch->n - 1 : 1;
  for (int i = 0; i < last; i++) {
    int j = patch->n > 1 ? i + 1 : i;
    float max_wa = fmaxf(patch->width_ax[i], patch->width_ax[j]);
    float alo = fminf(patch->along[i], patch->along[j]) - max_wa;
    float ahi = fmaxf(patch->along[i], patch->along[j]) + max_wa;
    if ((along[0] < alo && along[1] < alo && along[2] < alo) ||
        (along[0] > ahi && along[1] > ahi && along[2] > ahi)) continue;
    float radius = 0.5f * (patch->surface_r[i] + patch->surface_r[j]);
    float angle_pad = fmaxf(patch->width_tan[i], patch->width_tan[j]) /
                      fmaxf(radius, 1e-6f);
    float ang_lo = fminf(patch->angle[i], patch->angle[j]) - angle_pad;
    float ang_hi = fmaxf(patch->angle[i], patch->angle[j]) + angle_pad;
    if (ang_hi - ang_lo >= F_PI - 1e-4f) return 0;
    v3 dlo = v3_add(v3_scale(patch->radial_ref, cosf(ang_lo)),
                    v3_scale(side, sinf(ang_lo)));
    v3 dhi = v3_add(v3_scale(patch->radial_ref, cosf(ang_hi)),
                    v3_scale(side, sinf(ang_hi)));
    float lv[3], hv[3];
    for (int p = 0; p < 3; p++) {
      lv[p] = v3_dot(v3_cross(dlo, radial[p]), patch->axis);
      hv[p] = v3_dot(v3_cross(dhi, radial[p]), patch->axis);
    }
    if (!((lv[0] < 0.0f && lv[1] < 0.0f && lv[2] < 0.0f) ||
          (hv[0] > 0.0f && hv[1] > 0.0f && hv[2] > 0.0f))) return 0;
  }
  return 1;
}

// Return a split plane as soon as one real facet domain or depth-dominance boundary
// crosses the cell.  ACCEPT is possible only when one real render triangle owns the
// whole cell and every other triangle has a rigorous distance lower bound above it.
static int vmtrig_exact_cert_classify(
    const vmtrig_exact_cert_cell_t *cell, const fig_contact_patch_t *patch,
    const gun_mesh_set_t *set, const vmtrig_aabb_t *range_box,
    int body_range, float split_value[3],
    float accepted_depth[3], vmtrig_exact_cert_ctx_t *ctx,
    vmtrig_work_t *work) {
  v3 center = v3_scale(v3_add(cell->p[0],
                        v3_add(cell->p[1], cell->p[2])), 1.0f / 3.0f);
  float radius = vmtrig_tri_radius(cell->p[0], cell->p[1],
                                    cell->p[2], center);
  int count = (set->range[body_range].v1 - set->range[body_range].v0) /
              (3 * VERT_FLOATS);
  uint32_t section_mask = FIG_CONTACT_CERT_SECTIONS(patch->cert_tag);
  uint64_t patch_segments = (uint64_t)(patch->n > 1 ? patch->n - 1 : 1);
  int candidate_source[VMTRIG_EXACT_CERT_CANDIDATE_CAP];
  int ncandidate = 0;
  vmtrig_exact_cert_facet_t winner = {0};
  float winner_center = 1e9f;
  v3 cell_lo, cell_hi;
  vmtrig_exact_cert_bounds(cell->p, &cell_lo, &cell_hi);
  for (int source = 0; source < count; source++) {
    if (!vmtrig_exact_cert_work(ctx, work, 1)) return VMTRIG_EXACT_FAIL;
    vmtrig_exact_cert_facet_t facet;
    if (!vmtrig_exact_cert_facet_load(set, body_range, source, &facet))
      return VMTRIG_EXACT_FAIL;
    uint32_t facet_band = vmtrig_exact_cert_facet_band(&facet, patch);
    if (!(facet_band & section_mask) ||
        !vmtrig_exact_cert_facet_profile(&facet, patch)) {
      if (!vmtrig_exact_cert_work(ctx, work, patch_segments))
        return VMTRIG_EXACT_FAIL;
      vmtrig_exact_cert_cell_t facet_cell = {
        .p = {facet.a, facet.b, facet.c}};
      if (!vmtrig_exact_cert_patch_disjoint(&facet_cell, patch))
        return VMTRIG_EXACT_FAIL;
      continue;
    }
    v3 fp[3] = {facet.a, facet.b, facet.c}, facet_lo, facet_hi;
    vmtrig_exact_cert_bounds(fp, &facet_lo, &facet_hi);
    if (!vmtrig_exact_cert_box_overlap(cell_lo, cell_hi, facet_lo, facet_hi,
                                        0.004001f)) continue;
    v3 edge_a[3] = {facet.a, facet.b, facet.c};
    v3 edge_b[3] = {facet.b, facet.c, facet.a};
    int outside = 0, seam_flat = 0;
    for (int e = 0; e < 3; e++) {
      float lo = 1e9f, hi = -1e9f;
      for (int i = 0; i < 3; i++) {
        float v = vmtrig_exact_cert_edge(
          edge_a[e], edge_b[e], facet.n, cell->p[i]);
        split_value[i] = v;
        lo = fminf(lo, v); hi = fmaxf(hi, v);
      }
      if (lo < -VMTRIG_EXACT_CERT_EPS && hi > VMTRIG_EXACT_CERT_EPS)
        return VMTRIG_EXACT_SPLIT;
      if (hi < -VMTRIG_EXACT_CERT_EPS) { outside = 1; break; }
      if (lo >= -VMTRIG_EXACT_CERT_EPS &&
          hi <= VMTRIG_EXACT_CERT_EPS) seam_flat = 1;
      if (lo < -VMTRIG_EXACT_CERT_EPS) { outside = 1; break; }
    }
    if (outside) continue;
    if (seam_flat) return VMTRIG_EXACT_FAIL;
    float dlo = 1e9f, dhi = -1e9f;
    for (int i = 0; i < 3; i++) {
      facet.depth[i] = v3_dot(v3_sub(facet.a, cell->p[i]), facet.n);
      dlo = fminf(dlo, facet.depth[i]);
      dhi = fmaxf(dhi, facet.depth[i]);
    }
    if (dlo < -VMTRIG_EXACT_CERT_EPS &&
        dhi > VMTRIG_EXACT_CERT_EPS) {
      for (int i = 0; i < 3; i++) split_value[i] = facet.depth[i];
      return VMTRIG_EXACT_SPLIT;
    }
    if (dhi < -VMTRIG_EXACT_CERT_EPS) continue;
    if (dlo < -VMTRIG_EXACT_CERT_EPS) return VMTRIG_EXACT_FAIL;
    if (ncandidate >= VMTRIG_EXACT_CERT_CANDIDATE_CAP)
      return VMTRIG_EXACT_FAIL;
    candidate_source[ncandidate++] = source;
    float facet_center = (facet.depth[0] + facet.depth[1] + facet.depth[2]) /
                         3.0f;
    if (facet_center < winner_center) {
      winner = facet;
      winner_center = facet_center;
    }
  }
  if (!ncandidate) return VMTRIG_EXACT_DROP;

  for (int q = 0; q < ncandidate; q++) {
    if (candidate_source[q] == winner.source) continue;
    if (!vmtrig_exact_cert_work(ctx, work, 1)) return VMTRIG_EXACT_FAIL;
    vmtrig_exact_cert_facet_t facet;
    if (!vmtrig_exact_cert_facet_load(
          set, body_range, candidate_source[q], &facet))
      return VMTRIG_EXACT_FAIL;
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < 3; i++) {
      facet.depth[i] = v3_dot(v3_sub(facet.a, cell->p[i]), facet.n);
      split_value[i] = facet.depth[i] - winner.depth[i];
      lo = fminf(lo, split_value[i]); hi = fmaxf(hi, split_value[i]);
    }
    if (lo < -VMTRIG_EXACT_CERT_EPS && hi > VMTRIG_EXACT_CERT_EPS)
      return VMTRIG_EXACT_SPLIT;
    if (fabsf(lo) <= VMTRIG_EXACT_CERT_EPS &&
        fabsf(hi) <= VMTRIG_EXACT_CERT_EPS)
      return VMTRIG_EXACT_FAIL;  // a positive-area nearest-facet tie
    if (lo < -VMTRIG_EXACT_CERT_EPS) return VMTRIG_EXACT_FAIL;
  }

  float winner_lo = 1e9f, winner_hi = -1e9f;
  for (int i = 0; i < 3; i++) {
    winner_lo = fminf(winner_lo, winner.depth[i]);
    winner_hi = fmaxf(winner_hi, winner.depth[i]);
  }
  const float depth_limit = 0.004f - VMTRIG_EXACT_CERT_DEPTH_EPS;
  if (winner_lo < depth_limit && winner_hi > depth_limit) {
    for (int i = 0; i < 3; i++)
      split_value[i] = depth_limit - winner.depth[i];
    return VMTRIG_EXACT_SPLIT;
  }
  if (winner_hi > depth_limit) return VMTRIG_EXACT_FAIL;

  float max_depth = 0.0f;
  for (int i = 0; i < 3; i++)
    max_depth = fmaxf(max_depth, winner.depth[i]);
  // A facet whose orthogonal-foot prism misses the whole cell can still own a
  // nearer edge/vertex.  The 1-Lipschitz centre bound excludes that case.
  for (int source = 0; source < count; source++) {
    if (source == winner.source) continue;
    if (!vmtrig_exact_cert_work(ctx, work, 1)) return VMTRIG_EXACT_FAIL;
    vmtrig_exact_cert_facet_t facet;
    if (!vmtrig_exact_cert_facet_load(set, body_range, source, &facet))
      return VMTRIG_EXACT_FAIL;
    if (v3_dot(facet.n, winner.n) > 0.999999f &&
        fabsf(v3_dot(v3_sub(facet.a, winner.a), winner.n)) <=
        VMTRIG_EXACT_CERT_EPS)
      continue;
    v3 nearest = vmtrig_tri_closest(center, facet.a, facet.b, facet.c);
    if (v3_len(v3_sub(nearest, center)) - radius <=
        max_depth + VMTRIG_EXACT_CERT_EPS) {
      int known = 0;
      for (int q = 0; q < ncandidate; q++)
        known |= candidate_source[q] == source;
      if (!known)
        return radius > VMTRIG_EXACT_CERT_RADIUS &&
               cell->depth < VMTRIG_EXACT_CERT_DEPTH_CAP
             ? VMTRIG_EXACT_REFINE : VMTRIG_EXACT_FAIL;
    }
  }

  v3 hand_n = v3_cross(v3_sub(cell->p[1], cell->p[0]),
                        v3_sub(cell->p[2], cell->p[0]));
  float hand_n_len = v3_len(hand_n);
  if (hand_n_len <= 1e-9f) return VMTRIG_EXACT_FAIL;
  hand_n = v3_scale(hand_n, 1.0f / hand_n_len);
  if (v3_dot(hand_n, v3_scale(winner.n, -1.0f)) <
      VMTRIG_PATCH_NORMAL_MIN) return VMTRIG_EXACT_DROP;

  v3 swept_lo = cell_lo, swept_hi = cell_hi;
  vmtrig_exact_cert_cell_t surface_cell = {.depth = cell->depth};
  for (int i = 0; i < 3; i++) {
    v3 surface = v3_add(cell->p[i],
                         v3_scale(winner.n, winner.depth[i]));
    surface_cell.p[i] = surface;
    for (int k = 0; k < 3; k++) {
      swept_lo.e[k] = fminf(swept_lo.e[k], surface.e[k]);
      swept_hi.e[k] = fmaxf(swept_hi.e[k], surface.e[k]);
    }
  }
  if (!vmtrig_exact_cert_work(ctx, work, patch_segments))
    return VMTRIG_EXACT_FAIL;
  if (vmtrig_exact_cert_patch_disjoint(&surface_cell, patch))
    return VMTRIG_EXACT_DROP;
  // Four vertex/centroid contains calls plus the explicit centre score each
  // traverse the path once.
  if (!vmtrig_exact_cert_work(ctx, work, 5 * patch_segments))
    return VMTRIG_EXACT_FAIL;
  if (!vmtrig_exact_cert_patch_inside(&surface_cell, patch))
    return radius > VMTRIG_EXACT_CERT_RADIUS &&
           cell->depth < VMTRIG_EXACT_CERT_DEPTH_CAP
         ? VMTRIG_EXACT_REFINE : VMTRIG_EXACT_FAIL;
  int foreign = vmtrig_exact_cert_foreign_overlap(
    set, range_box, body_range, swept_lo, swept_hi, ctx, work);
  if (foreign)
    return foreign > 0 && radius > VMTRIG_EXACT_CERT_RADIUS &&
           cell->depth < VMTRIG_EXACT_CERT_DEPTH_CAP
         ? VMTRIG_EXACT_REFINE : VMTRIG_EXACT_FAIL;
  for (int i = 0; i < 3; i++) accepted_depth[i] = winner.depth[i];
  return VMTRIG_EXACT_ACCEPT;
}

// Returns zero only for an untagged patch, so the caller can execute the legacy
// owner-distance path.  Every malformed/uncertain tagged certificate is handled and
// fails closed instead of silently falling back to a weaker surface description.
static int vmtrig_exact_cert_bite_tri(
    v3 a, v3 b, v3 c, const fig_contact_patch_t *patch,
    gun_contact_kind_t kind, const gun_mesh_set_t *set,
    const vmtrig_aabb_t *range_box, vmtrig_bite_t *out,
    vmtrig_exact_cert_ctx_t *ctx, vmtrig_work_t *work) {
  int body_range = -1;
  if (patch && patch->cert_tag &&
      (!ctx || !vmtrig_exact_cert_work(ctx, work, 1))) {
    out->fail = 1;
    return 1;
  }
  int resolved = vmtrig_exact_cert_body_range(
    patch, kind, set, &body_range);
  if (!resolved) return 0;
  if (resolved < 0 || !ctx || ctx->overflow || out->fail) {
    out->fail = 1;
    return 1;
  }
  vmtrig_exact_cert_cell_t stack[VMTRIG_EXACT_CERT_STACK_CAP];
  unsigned cap = ctx->stack_limit;
  if (!cap || cap > VMTRIG_EXACT_CERT_STACK_CAP)
    cap = VMTRIG_EXACT_CERT_STACK_CAP;
  int sp = 1;
  stack[0] = (vmtrig_exact_cert_cell_t){.p = {a, b, c}};
  while (sp > 0 && !out->fail) {
    vmtrig_exact_cert_cell_t cell = stack[--sp];
    if (ctx->cells >= VMTRIG_EXACT_CERT_CELL_LIMIT) {
      ctx->overflow = out->fail = 1;
      break;
    }
    ctx->cells++;
    if (!vmtrig_exact_cert_work(ctx, work, 1)) { out->fail = 1; break; }
    float split_value[3], depth[3];
    int state = vmtrig_exact_cert_classify(
      &cell, patch, set, range_box, body_range,
      split_value, depth, ctx, work);
    if (state == VMTRIG_EXACT_DROP) continue;
    if (state == VMTRIG_EXACT_ACCEPT) {
      double area = (double)vmtrig_exact_cert_area(cell.p);
      double lo_integral = 0.0, hi_integral = 0.0;
      float upper = 0.0f;
      for (int i = 0; i < 3; i++) {
        float lo = fmaxf(depth[i] - VMTRIG_EXACT_CERT_DEPTH_EPS, 0.0f);
        float hi = fmaxf(depth[i] + VMTRIG_EXACT_CERT_DEPTH_EPS, 0.0f);
        lo_integral += (double)lo;
        hi_integral += (double)hi;
        upper = fmaxf(upper, hi);
      }
      lo_integral *= area / 3.0;
      hi_integral *= area / 3.0;
      if (!(area > 0.0) || f_is_nonfinite((float)area) ||
          f_is_nonfinite((float)lo_integral) ||
          f_is_nonfinite((float)hi_integral) ||
          f_is_nonfinite(upper)) {
        out->fail = 1;
        break;
      }
      out->depth_lo_area += lo_integral;
      out->depth_hi_area += hi_integral;
      out->inside_area += area;
      if (vmtrig_u64_add(&out->samples, 1)) out->fail = 1;
      out->max_upper = fmaxf(out->max_upper, upper);
      ctx->accepted++;
      continue;
    }
    vmtrig_exact_cert_cell_t child[3];
    int nchild = 0;
    if ((state == VMTRIG_EXACT_SPLIT || state == VMTRIG_EXACT_REFINE) &&
        cell.depth >= VMTRIG_EXACT_CERT_DEPTH_CAP) {
      out->fail = 1;
      continue;
    }
    if (state == VMTRIG_EXACT_SPLIT)
      nchild = vmtrig_exact_cert_plane_split(&cell, split_value, child);
    else if (state == VMTRIG_EXACT_REFINE)
      nchild = vmtrig_exact_cert_longest_split(&cell, child);
    if ((state != VMTRIG_EXACT_SPLIT && state != VMTRIG_EXACT_REFINE) ||
        !nchild || !vmtrig_exact_cert_push(stack, &sp, cap, child, nchild)) {
      ctx->overflow |= state == VMTRIG_EXACT_SPLIT ||
                       state == VMTRIG_EXACT_REFINE;
      out->fail = 1;
    }
  }
  if (ctx->overflow) out->fail = 1;
  return 1;
}

static void vmtrig_exact_cert_self_tri(v3 a, v3 b, v3 c) {
  v3 n = v3_norm(v3_cross(v3_sub(b, a), v3_sub(c, a)));
  scene_poly3(a, b, c, n, (v3){{1, 1, 1}});
}

static fig_contact_patch_t vmtrig_exact_cert_self_patch(void) {
  fig_contact_patch_t patch = {
    .name = "exact_self", .n = 1,
    .axis = {{0, 0, 1}}, .radial_ref = {{1, 0, 0}},
    .endpoint_profile = {
      .u = {{1, 0, 0}}, .rx = 0.020f, .rz = 0.020f,
      .ref_r = 0.020f, .enabled = 1, .sides = 4},
    .cert_tag = FIG_CONTACT_CERT_TAG(GUN_CONTACT_MAG, 0,
                                     GUN_MESH_PART_MAG_BODY, UINT32_C(1)),
  };
  patch.surface_r[0] = 0.020f;
  patch.width_ax[0] = patch.width_tan[0] = 0.040f;
  return patch;
}

static int vmtrig_exact_cert_self_run(
    const fig_contact_patch_t *patch, gun_contact_kind_t kind,
    const gun_mesh_set_t *set, v3 a, v3 b, v3 c,
    vmtrig_bite_t *bite, vmtrig_exact_cert_ctx_t *ctx) {
  vmtrig_aabb_t box[GUN_CONTACT_MESH_MAX] = {0};
  for (int q = 0; q < set->count; q++) {
    int range[2] = {set->range[q].v0, set->range[q].v1};
    box[q] = vmtrig_range_aabb(range);
  }
  return vmtrig_exact_cert_bite_tri(
    a, b, c, patch, kind, set, box, bite, ctx, NULL);
}

static int vmtrig_exact_cert_selftest(void) {
  int fail = 0;
  fig_contact_patch_t patch = vmtrig_exact_cert_self_patch();
  const v3 pa = {{0.0175f, -0.005f, -0.005f}};
  const v3 pb = {{0.017f,  0.000f,  0.005f}};
  const v3 pc = {{0.0165f,  0.005f, -0.005f}};

  // One planar physical facet: vertex depths 2.5/3.0/3.5 mm integrate to an
  // exact 3 mm area mean.
  scene_reset();
  gun_mesh_set_t planar = {.count = 1};
  planar.range[0] = (gun_mesh_range_t){
    .v0 = g_scene_floats, .surface = 1,
    .part = GUN_MESH_PART_MAG_BODY};
  vmtrig_exact_cert_self_tri(
    (v3){{0.020f, -0.020f, -0.020f}},
    (v3){{0.020f,  0.020f, -0.020f}},
    (v3){{0.020f,  0.000f,  0.020f}});
  planar.range[0].v1 = g_scene_floats;
  vmtrig_bite_t planar_bite = {0};
  vmtrig_exact_cert_ctx_t planar_ctx = {
    .work_limit = 100000, .stack_limit = VMTRIG_EXACT_CERT_STACK_CAP};
  int handled = vmtrig_exact_cert_self_run(
    &patch, GUN_CONTACT_MAG, &planar, pa, pb, pc,
    &planar_bite, &planar_ctx);
  float planar_mean = planar_bite.inside_area > 0.0
                    ? (float)(planar_bite.depth_lo_area /
                              planar_bite.inside_area) : 0.0f;
  float planar_hi = planar_bite.inside_area > 0.0
                  ? (float)(planar_bite.depth_hi_area /
                            planar_bite.inside_area) : 0.0f;
  fail |= (!handled || planar_bite.fail || planar_ctx.accepted != 1 ||
           fabsf(planar_mean -
                 (0.003f - VMTRIG_EXACT_CERT_DEPTH_EPS)) > 1e-6f ||
           fabsf(planar_hi -
                 (0.003f + VMTRIG_EXACT_CERT_DEPTH_EPS)) > 1e-6f ||
           fabsf(planar_bite.max_upper -
                 (0.0035f + VMTRIG_EXACT_CERT_DEPTH_EPS)) > 1e-6f ||
           planar_bite.boundary_area != 0.0) << 0;

  // A tapered planar quad is still two real triangles.  Its diagonal must split
  // the broad hand source into disjoint affine fragments, not become positive area.
  scene_reset();
  gun_mesh_set_t quad = {.count = 1};
  quad.range[0] = (gun_mesh_range_t){
    .v0 = g_scene_floats, .surface = 1,
    .part = GUN_MESH_PART_MAG_BODY};
  v3 qa = {{0.020f, -0.012f, -0.010f}};
  v3 qb = {{0.020f,  0.012f, -0.010f}};
  v3 qc = {{0.020f,  0.008f,  0.010f}};
  v3 qd = {{0.020f, -0.008f,  0.010f}};
  vmtrig_exact_cert_self_tri(qa, qb, qc);
  vmtrig_exact_cert_self_tri(qa, qc, qd);
  quad.range[0].v1 = g_scene_floats;
  v3 qha = {{0.017f, -0.006f, -0.006f}};
  v3 qhb = {{0.017f,  0.000f,  0.006f}};
  v3 qhc = {{0.017f,  0.006f, -0.006f}};
  vmtrig_bite_t quad_bite = {0};
  vmtrig_exact_cert_ctx_t quad_ctx = {
    .work_limit = 100000, .stack_limit = VMTRIG_EXACT_CERT_STACK_CAP};
  handled = vmtrig_exact_cert_self_run(
    &patch, GUN_CONTACT_MAG, &quad, qha, qhb, qhc,
    &quad_bite, &quad_ctx);
  float quad_mean = quad_bite.inside_area > 0.0
                  ? (float)(quad_bite.depth_lo_area /
                            quad_bite.inside_area) : 0.0f;
  fail |= (!handled || quad_bite.fail || quad_ctx.accepted < 2 ||
           fabsf(quad_mean -
                 (0.003f - VMTRIG_EXACT_CERT_DEPTH_EPS)) > 1e-6f ||
           quad_bite.boundary_area != 0.0) << 1;

  // The two halves of a twisted tapered quad have different real normals.  Offset
  // one shrunken hand face from each physical triangle and require both exact 3 mm
  // depths; a 2-D profile normal cannot satisfy this regression.
  scene_reset();
  gun_mesh_set_t twisted = {.count = 1};
  twisted.range[0] = (gun_mesh_range_t){
    .v0 = g_scene_floats, .surface = 1,
    .part = GUN_MESH_PART_MAG_BODY};
  const v3 tqa = {{0.02000f, 0.00756f, -0.010f}};
  const v3 tqb = {{0.00840f, 0.01800f, -0.010f}};
  const v3 tqc = {{0.00756f, 0.02100f,  0.010f}};
  const v3 tqd = {{0.01800f, 0.00882f,  0.010f}};
  vmtrig_exact_cert_self_tri(tqa, tqb, tqc);
  vmtrig_exact_cert_self_tri(tqa, tqc, tqd);
  twisted.range[0].v1 = g_scene_floats;
  patch.endpoint_profile = (gun_hold_profile_t){
    .u = {{1, 0, 0}}, .rx = 0.019f, .rz = 0.0195f,
    .ref_r = 0.020f, .section_along = {-0.010f, 0.010f},
    .section_rx = {0.020f, 0.018f},
    .section_rz = {0.018f, 0.021f},
    .section_n = 2, .enabled = 1, .sides = 8,
  };
  v3 tn[2] = {
    v3_norm(v3_cross(v3_sub(tqb, tqa), v3_sub(tqc, tqa))),
    v3_norm(v3_cross(v3_sub(tqc, tqa), v3_sub(tqd, tqa))),
  };
  v3 tv[2][3] = {{tqa, tqb, tqc}, {tqa, tqc, tqd}};
  vmtrig_bite_t twisted_bite = {0};
  vmtrig_exact_cert_ctx_t twisted_ctx = {
    .work_limit = 100000, .stack_limit = VMTRIG_EXACT_CERT_STACK_CAP};
  for (int q = 0; q < 2; q++) {
    v3 mid = v3_scale(v3_add(tv[q][0],
                      v3_add(tv[q][1], tv[q][2])), 1.0f / 3.0f);
    v3 anchor = v3_lerp(mid, tv[q][q ? 2 : 1], 0.55f);
    v3 hp[3];
    for (int i = 0; i < 3; i++)
      hp[i] = v3_sub(v3_lerp(anchor, tv[q][i], 0.10f),
                     v3_scale(tn[q], 0.003f));
    handled &= vmtrig_exact_cert_self_run(
      &patch, GUN_CONTACT_MAG, &twisted, hp[0], hp[2], hp[1],
      &twisted_bite, &twisted_ctx);
  }
  float twisted_mean = twisted_bite.inside_area > 0.0
                     ? (float)(twisted_bite.depth_lo_area /
                               twisted_bite.inside_area) : 0.0f;
  fail |= (!handled || twisted_bite.fail || twisted_ctx.accepted != 2 ||
           fabsf(twisted_mean -
                 (0.003f - VMTRIG_EXACT_CERT_DEPTH_EPS)) > 1e-6f ||
           twisted_bite.boundary_area != 0.0) << 2;

  // Two perpendicular facets have an actual positive-area depth tie on x=y.
  // A certificate must reject it instead of selecting one convenient normal.
  patch = vmtrig_exact_cert_self_patch();
  scene_reset();
  gun_mesh_set_t tie = {.count = 1};
  tie.range[0] = (gun_mesh_range_t){
    .v0 = g_scene_floats, .surface = 1,
    .part = GUN_MESH_PART_MAG_BODY};
  vmtrig_exact_cert_self_tri(
    (v3){{0.020f, 0.000f, -0.020f}},
    (v3){{0.020f, 0.020f, -0.020f}},
    (v3){{0.020f, 0.020f,  0.020f}});
  vmtrig_exact_cert_self_tri(
    (v3){{0.000f, 0.020f, -0.020f}},
    (v3){{0.020f, 0.020f,  0.020f}},
    (v3){{0.020f, 0.020f, -0.020f}});
  tie.range[0].v1 = g_scene_floats;
  patch.surface_r[0] = 0.028284271f;
  patch.radial_ref = v3_norm((v3){{1, 1, 0}});
  v3 ta = {{0.017f, 0.017f, -0.005f}};
  v3 tb = {{0.016f, 0.016f,  0.005f}};
  v3 tc = {{0.018f, 0.018f,  0.005f}};
  vmtrig_bite_t tie_bite = {0};
  vmtrig_exact_cert_ctx_t tie_ctx = {
    .work_limit = 100000, .stack_limit = VMTRIG_EXACT_CERT_STACK_CAP};
  handled = vmtrig_exact_cert_self_run(
    &patch, GUN_CONTACT_MAG, &tie, ta, tb, tc, &tie_bite, &tie_ctx);
  fail |= (!handled || !tie_bite.fail) << 3;

  // A tag may resolve only its exact BODY range; DETAIL cannot impersonate it.
  patch = vmtrig_exact_cert_self_patch();
  gun_mesh_set_t wrong = planar;
  wrong.count = 2;
  wrong.range[0].part = GUN_MESH_PART_MAG_DETAIL;
  wrong.range[1] = planar.range[0];
  vmtrig_bite_t wrong_bite = {0};
  vmtrig_exact_cert_ctx_t wrong_ctx = {
    .work_limit = 100000, .stack_limit = VMTRIG_EXACT_CERT_STACK_CAP};
  handled = vmtrig_exact_cert_self_run(
    &patch, GUN_CONTACT_MAG, &wrong, pa, pb, pc,
    &wrong_bite, &wrong_ctx);
  int wrong_failed = handled && wrong_bite.fail;
  patch.cert_tag = FIG_CONTACT_CERT_TAG(
    GUN_CONTACT_MAG, 0, GUN_MESH_PART_MAG_BODY, UINT32_C(2));
  vmtrig_bite_t section_bite = {0};
  vmtrig_exact_cert_ctx_t section_ctx = {
    .work_limit = 100000, .stack_limit = VMTRIG_EXACT_CERT_STACK_CAP};
  handled = vmtrig_exact_cert_self_run(
    &patch, GUN_CONTACT_MAG, &planar, pa, pb, pc,
    &section_bite, &section_ctx);
  int section_failed = handled && section_bite.fail;
  scene_reset();
  gun_mesh_set_t profile_wrong = {.count = 1};
  profile_wrong.range[0] = (gun_mesh_range_t){
    .v0 = g_scene_floats, .surface = 1,
    .part = GUN_MESH_PART_MAG_BODY};
  vmtrig_exact_cert_self_tri(
    (v3){{0.020f, -0.020f, -0.020f}},
    (v3){{0.020f,  0.020f, -0.020f}},
    (v3){{0.020f,  0.000f,  0.020f}});
  profile_wrong.range[0].v1 = g_scene_floats;
  patch = vmtrig_exact_cert_self_patch();
  patch.endpoint_profile.rx = 0.019f;
  vmtrig_bite_t profile_bite = {0};
  vmtrig_exact_cert_ctx_t profile_ctx = {
    .work_limit = 100000, .stack_limit = VMTRIG_EXACT_CERT_STACK_CAP};
  handled = vmtrig_exact_cert_self_run(
    &patch, GUN_CONTACT_MAG, &profile_wrong, pa, pb, pc,
    &profile_bite, &profile_ctx);
  fail |= (!wrong_failed || !section_failed ||
           !handled || !profile_bite.fail) << 4;
  patch = vmtrig_exact_cert_self_patch();

  // Hosted and unhosted foreign details crossing the contact-to-body prism are
  // both fail-closed.  Host annotations optimize other proofs; they cannot grant
  // exact BODY authority.
  scene_reset();
  gun_mesh_set_t hosted = {.count = 2};
  hosted.range[0] = (gun_mesh_range_t){
    .v0 = g_scene_floats, .surface = 1,
    .part = GUN_MESH_PART_MAG_BODY};
  vmtrig_exact_cert_self_tri(
    (v3){{0.020f, -0.020f, -0.020f}},
    (v3){{0.020f,  0.020f, -0.020f}},
    (v3){{0.020f,  0.000f,  0.020f}});
  hosted.range[0].v1 = g_scene_floats;
  hosted.range[1] = (gun_mesh_range_t){
    .v0 = g_scene_floats, .surface = 1,
    .part = GUN_MESH_PART_MAG_DETAIL,
    .host_mask = GUN_MESH_PART_MAG_BODY};
  vmtrig_exact_cert_self_tri(
    (v3){{0.0185f, -0.001f, -0.001f}},
    (v3){{0.0185f,  0.001f, -0.001f}},
    (v3){{0.0185f,  0.000f,  0.001f}});
  hosted.range[1].v1 = g_scene_floats;
  const float tiny = 0.00002f;
  v3 hpa = {{0.017f, -tiny, -tiny}};
  v3 hpb = {{0.017f,  0.0f, tiny}};
  v3 hpc = {{0.017f, tiny, -tiny}};
  for (int hosted_case = 0; hosted_case < 2; hosted_case++) {
    hosted.range[1].host_mask = hosted_case
                              ? GUN_MESH_PART_MAG_BODY : 0;
    vmtrig_bite_t hosted_bite = {0};
    vmtrig_exact_cert_ctx_t hosted_ctx = {
      .work_limit = 100000, .stack_limit = VMTRIG_EXACT_CERT_STACK_CAP};
    handled = vmtrig_exact_cert_self_run(
      &patch, GUN_CONTACT_MAG, &hosted, hpa, hpb, hpc,
      &hosted_bite, &hosted_ctx);
    fail |= (!handled || !hosted_bite.fail) << 5;
  }

  // Reverse winding fails the exact weapon-normal facing gate.
  vmtrig_bite_t facing_bite = {0};
  vmtrig_exact_cert_ctx_t facing_ctx = {
    .work_limit = 100000, .stack_limit = VMTRIG_EXACT_CERT_STACK_CAP};
  handled = vmtrig_exact_cert_self_run(
    &patch, GUN_CONTACT_MAG, &planar, pa, pc, pb,
    &facing_bite, &facing_ctx);
  fail |= (!handled || facing_bite.fail || facing_ctx.accepted != 0 ||
           facing_bite.inside_area != 0.0) << 6;

  // The same q0 contract applies to the firing-hand GRIP.  A source fully outside
  // its finite footprint is handled but contributes no area; tagging BODY never
  // turns unrelated palm/digit sources green.
  fig_contact_patch_t grip_patch = vmtrig_exact_cert_self_patch();
  grip_patch.cert_tag = FIG_CONTACT_CERT_TAG(
    GUN_CONTACT_GRIP, 0, GUN_MESH_PART_GRIP_BODY, UINT32_C(1));
  gun_mesh_set_t grip = planar;
  grip.range[0].part = GUN_MESH_PART_GRIP_BODY;
  vmtrig_bite_t grip_bite = {0};
  vmtrig_exact_cert_ctx_t grip_ctx = {
    .work_limit = 100000, .stack_limit = VMTRIG_EXACT_CERT_STACK_CAP};
  handled = vmtrig_exact_cert_self_run(
    &grip_patch, GUN_CONTACT_GRIP, &grip, pa, pb, pc,
    &grip_bite, &grip_ctx);
  grip_patch.width_tan[0] = 0.001f;
  const v3 oa = {{0.017f, 0.009f, -0.005f}};
  const v3 ob = {{0.017f, 0.010f,  0.005f}};
  const v3 oc = {{0.017f, 0.011f, -0.005f}};
  vmtrig_bite_t outside_bite = {0};
  vmtrig_exact_cert_ctx_t outside_ctx = {
    .work_limit = 100000, .stack_limit = VMTRIG_EXACT_CERT_STACK_CAP};
  int outside_handled = vmtrig_exact_cert_self_run(
    &grip_patch, GUN_CONTACT_GRIP, &grip, oa, ob, oc,
    &outside_bite, &outside_ctx);
  fail |= (!handled || grip_bite.fail || grip_ctx.accepted != 1 ||
           !outside_handled || outside_bite.fail || outside_ctx.accepted != 0 ||
           outside_bite.inside_area != 0.0) << 8;

  // A positive-area source that crosses the curved patch boundary is never
  // rounded inward.  Streaming refinement may isolate interior cells, but the
  // terminal straddler itself remains fail-closed.
  fig_contact_patch_t boundary_patch = vmtrig_exact_cert_self_patch();
  boundary_patch.width_tan[0] = 0.001f;
  const v3 bpa = {{0.017f, 0.0005f, -0.0005f}};
  const v3 bpb = {{0.017f, 0.0005f,  0.0005f}};
  const v3 bpc = {{0.017f, 0.0015f, -0.0005f}};
  vmtrig_bite_t boundary_bite = {0};
  vmtrig_exact_cert_ctx_t boundary_ctx = {
    .work_limit = 100000, .stack_limit = VMTRIG_EXACT_CERT_STACK_CAP};
  handled = vmtrig_exact_cert_self_run(
    &boundary_patch, GUN_CONTACT_MAG, &planar, bpa, bpb, bpc,
    &boundary_bite, &boundary_ctx);
  fail |= (!handled || !boundary_bite.fail) << 9;

  // Work and stack caps are independent of the global vmtrig budget and both
  // fail closed.
  vmtrig_bite_t overflow_bite = {0};
  vmtrig_exact_cert_ctx_t overflow_ctx = {
    .work_limit = 1, .stack_limit = VMTRIG_EXACT_CERT_STACK_CAP};
  handled = vmtrig_exact_cert_self_run(
    &patch, GUN_CONTACT_MAG, &planar, pa, pb, pc,
    &overflow_bite, &overflow_ctx);
  scene_reset();
  gun_mesh_set_t stack_quad = {.count = 1};
  stack_quad.range[0] = (gun_mesh_range_t){
    .v0 = g_scene_floats, .surface = 1,
    .part = GUN_MESH_PART_MAG_BODY};
  vmtrig_exact_cert_self_tri(qa, qb, qc);
  vmtrig_exact_cert_self_tri(qa, qc, qd);
  stack_quad.range[0].v1 = g_scene_floats;
  vmtrig_bite_t stack_bite = {0};
  vmtrig_exact_cert_ctx_t stack_ctx = {
    .work_limit = 100000, .stack_limit = 1};
  int stack_handled = vmtrig_exact_cert_self_run(
    &patch, GUN_CONTACT_MAG, &stack_quad, qha, qhb, qhc,
    &stack_bite, &stack_ctx);
  fail |= (!handled || !overflow_bite.fail || !overflow_ctx.overflow ||
           !stack_handled || !stack_bite.fail || !stack_ctx.overflow) << 7;
  return fail;
}

static int vmtrig_bite_outside_all(v3 p, const gun_mesh_set_t *set,
                                   const vmtrig_aabb_t *range_box,
                                   vmtrig_work_t *work) {
  for (int r = 0; r < set->count; r++) {
    int in_box = 1;
    for (int axis = 0; axis < 3; axis++)
      in_box &= p.e[axis] >= range_box[r].lo.e[axis] &&
                p.e[axis] <= range_box[r].hi.e[axis];
    if (!in_box) continue;
    int rr[2] = {set->range[r].v0, set->range[r].v1};
    if (vmtrig_mesh_sd(p, rr, work) <= 0.0f) return 0;
  }
  return 1;
}

// Distinguish a proved union-exterior point from an unusable parity/work result.
// Callers may only use the point as an upper witness when this returns true.
static int vmtrig_bite_outside_all_checked(
    v3 p, const gun_mesh_set_t *set, const vmtrig_aabb_t *range_box,
    vmtrig_work_t *work, int *outside) {
  if (vmtrig_v3_nonfinite(p) || (work && work->overflow)) return 0;
  uint64_t ambiguity = g_vmtrig_inside_ambiguous;
  *outside = vmtrig_bite_outside_all(p, set, range_box, work);
  return (!work || !work->overflow) &&
         g_vmtrig_inside_ambiguous == ambiguity;
}

// Find one constructive union-exterior endpoint along a single ray. Every accepted
// endpoint has passed outside_all; a parity ambiguity, non-finite value, work cap, or
// missing exit invalidates this direction. Bisection always retains the verified
// exterior endpoint, even when overlapping ranges make the predicate non-monotone.
static float vmtrig_bite_ray_witness(
    v3 p, v3 direction, float start, const gun_mesh_set_t *set,
    const vmtrig_aabb_t *range_box, float best, vmtrig_work_t *work) {
  const float invalid = 1e9f;
  const float witness_step = 4e-6f;
  float direction_len = v3_len(direction);
  if (vmtrig_v3_nonfinite(p) || vmtrig_v3_nonfinite(direction) ||
      f_is_nonfinite(direction_len) || direction_len <= 1e-8f ||
      f_is_nonfinite(start) || start <= 1e-8f ||
      (work && work->overflow)) return invalid;
  direction = v3_scale(direction, 1.0f / direction_len);
  float outside_t = start + witness_step;
  float inside_t = 0.0f;
  int exposed = 0;
  for (int grow = 0; grow <= 20; grow++) {
    if (f_is_nonfinite(outside_t) || outside_t <= 0.0f ||
        outside_t >= invalid || outside_t >= best) return invalid;
    v3 witness = v3_add(p, v3_scale(direction, outside_t));
    if (!vmtrig_bite_outside_all_checked(
          witness, set, range_box, work, &exposed)) return invalid;
    if (exposed) break;
    if (grow == 20) return invalid;
    inside_t = outside_t;
    outside_t = outside_t * 2.0f + witness_step;
  }
  if (!exposed) return invalid;
  if (inside_t > 0.0f)
    for (int split = 0; split < 10; split++) {
      float mid = 0.5f * (inside_t + outside_t);
      if (f_is_nonfinite(mid) || !(mid > inside_t && mid < outside_t))
        return invalid;
      v3 witness = v3_add(p, v3_scale(direction, mid));
      int mid_outside = 0;
      if (!vmtrig_bite_outside_all_checked(
            witness, set, range_box, work, &mid_outside)) return invalid;
      if (mid_outside) outside_t = mid;
      else inside_t = mid;
    }
  return outside_t;
}

// Return a certified exposed-union upper witness. A nearest face may be the buried
// inner wall of an exterior overlay, so search both rays on its normal chord. Only
// verified union-exterior endpoints compete for the minimum; ambiguity and a missing
// exit remain fail-closed through the invalid sentinel.
static float vmtrig_bite_exposed_witness(
    v3 p, const gun_mesh_set_t *set, const vmtrig_aabb_t *range_box,
    const v3 *nearest_at, const float *nearest, const int *inside,
    vmtrig_work_t *work) {
  float upper = 1e9f;
  for (int q = 0; q < set->count; q++) {
    if (!set->range[q].surface || !inside[q] || nearest[q] >= upper ||
        nearest[q] <= 1e-8f) continue;
    v3 chord = v3_sub(nearest_at[q], p);
    float chord_len = v3_len(chord);
    if (vmtrig_v3_nonfinite(chord) || f_is_nonfinite(chord_len) ||
        chord_len <= 1e-8f) return 1e9f;
    v3 forward_dir = v3_scale(chord, 1.0f / chord_len);
    v3 reverse_dir = v3_scale(forward_dir, -1.0f);
    const float first_t = nearest[q] + 4e-6f;
    if (f_is_nonfinite(first_t) || first_t >= upper) continue;
    // Probe both nearest-distance endpoints before growing either ray. Usually the
    // ordinary closest-face direction exits immediately; for an exterior overlay,
    // its reverse exits immediately. In either case no ray can beat first_t because
    // nearest[q] is a Euclidean lower bound for leaving q.
    int first_outside = 0;
    if (!vmtrig_bite_outside_all_checked(
          v3_add(p, v3_scale(forward_dir, first_t)), set, range_box, work,
          &first_outside)) return 1e9f;
    if (first_outside) { upper = first_t; continue; }
    if (!vmtrig_bite_outside_all_checked(
          v3_add(p, v3_scale(reverse_dir, first_t)), set, range_box, work,
          &first_outside)) return 1e9f;
    if (first_outside) { upper = first_t; continue; }
    // The closest-face direction has just been shown internal. Grow the opposite
    // chord first: this finds an exterior overlay's visible wall before traversing
    // its host. Its valid result then bounds the still-buried forward search.
    float reverse = vmtrig_bite_ray_witness(
        p, reverse_dir, nearest[q], set, range_box, upper, work);
    if (reverse < upper) upper = reverse;
    float forward = vmtrig_bite_ray_witness(
        p, forward_dir, nearest[q], set, range_box, upper, work);
    if (forward < upper) upper = forward;
  }
  return upper;
}

// Form a conservative cell interval from a constructive center witness. A valid
// exterior path cannot be shorter than the deepest containing-range depth. Clamp a
// sub-micrometre roundoff discrepancy upward; any material inversion fails closed.
static int vmtrig_bite_make_interval(float deepest, float radius,
                                     int full_inside, float *witness,
                                     float *lo, float *hi) {
  const float fp_eps = 1e-6f;
  if (f_is_nonfinite(deepest) || f_is_nonfinite(radius) ||
      f_is_nonfinite(*witness) || deepest < 0.0f || radius < 0.0f)
    return 0;
  *lo = full_inside ? fmaxf(deepest - radius, 0.0f) : 0.0f;
  if (*witness >= 1e8f) {
    *hi = 1e9f;
    return !f_is_nonfinite(*lo);
  }
  if (*witness < 0.0f || *witness + fp_eps < deepest) return 0;
  if (*witness < deepest) *witness = deepest;
  *hi = *witness + radius;
  return !f_is_nonfinite(*lo) && !f_is_nonfinite(*hi) && *lo <= *hi;
}

// Integrate each hand cell exactly once. Membership and nearest distances are still
// queried per closed range. max(depth_q) is a rigorous lower bound on distance to the
// exposed union (the point must leave every containing primitive); an actually
// exposed closest-point witness supplies the upper bound. Thus hosted/internal faces
// cannot dilute the mean and unrelated seam ambiguity cannot false-green it.
static void vmtrig_range_bite_owner_tri(
    v3 a, v3 b, v3 c, const gun_mesh_set_t *set,
    const vmtrig_aabb_t *range_box, int source_uncertain,
    vmtrig_bite_t *out, vmtrig_work_t *work) {
  if ((work && work->overflow) || out->fail) return;
  if (vmtrig_v3_nonfinite(a) || vmtrig_v3_nonfinite(b) ||
      vmtrig_v3_nonfinite(c)) { out->fail = 1; return; }
  uint64_t ambiguity = g_vmtrig_inside_ambiguous;
  float ab = v3_len(v3_sub(b, a)), bc = v3_len(v3_sub(c, b));
  float ca = v3_len(v3_sub(c, a));
  v3 center = v3_scale(v3_add(a, v3_add(b, c)), 1.0f / 3.0f);
  float radius = vmtrig_tri_radius(a, b, c, center);
  int full_inside = 0, possible_inside = 0;
  float deepest = 0.0f;
  float nearest[GUN_CONTACT_MESH_MAX];
  v3 nearest_at[GUN_CONTACT_MESH_MAX];
  int inside[GUN_CONTACT_MESH_MAX] = {0};
  for (int q = 0; q < GUN_CONTACT_MESH_MAX; q++) nearest[q] = 1e9f;
  for (int q = 0; q < set->count; q++) {
    if (!set->range[q].surface) continue;
    int range[2] = {set->range[q].v0, set->range[q].v1};
    float sd = vmtrig_mesh_sd_at(center, range, work, &nearest_at[q]);
    if (f_is_nonfinite(sd) || vmtrig_v3_nonfinite(nearest_at[q]) ||
        (work && work->overflow)) { out->fail = 1; return; }
    float depth = -sd;
    inside[q] = depth > 0.0f;
    nearest[q] = fabsf(sd);
    if (depth > deepest) deepest = depth;
    full_inside |= depth - radius > 0.0f;
    possible_inside |= depth + radius > 0.0f;
  }
  if (g_vmtrig_inside_ambiguous != ambiguity) { out->fail = 1; return; }
  if (!possible_inside) return;
  // A non-positive surface depth is not by itself an exterior certificate: the
  // center may lie exactly on a mating plane or inside a non-surface host range.
  // Zero is an upper witness only after the actual point is outside every authored
  // range; otherwise subdivision must find a validated exterior witness or fail.
  float witness = 1e9f;
  if (deepest > 0.0f) {
    witness = vmtrig_bite_exposed_witness(
        center, set, range_box, nearest_at, nearest, inside, work);
  } else {
    int center_outside = 0;
    if (!vmtrig_bite_outside_all_checked(
          center, set, range_box, work, &center_outside)) {
      out->fail = 1;
      return;
    }
    if (center_outside) witness = 0.0f;
  }
  if ((work && work->overflow) ||
      g_vmtrig_inside_ambiguous != ambiguity) { out->fail = 1; return; }
  float lo, hi;
  if (!vmtrig_bite_make_interval(
        deepest, radius, full_inside, &witness, &lo, &hi)) {
    out->fail = 1;
    return;
  }
  int gate_straddle = vmtrig_bite_gate_straddle(lo, hi,
                                                 source_uncertain) ||
                       !full_inside || witness >= 1e8f;
  if (gate_straddle && radius > VMTRIG_PATCH_LEAF_RADIUS) {
    if (ab >= bc && ab >= ca) {
      v3 m = v3_lerp(a, b, 0.5f);
      vmtrig_range_bite_owner_tri(a, m, c, set, range_box,
                                   source_uncertain, out, work);
      vmtrig_range_bite_owner_tri(m, b, c, set, range_box,
                                   source_uncertain, out, work);
    } else if (bc >= ca) {
      v3 m = v3_lerp(b, c, 0.5f);
      vmtrig_range_bite_owner_tri(a, b, m, set, range_box,
                                   source_uncertain, out, work);
      vmtrig_range_bite_owner_tri(a, m, c, set, range_box,
                                   source_uncertain, out, work);
    } else {
      v3 m = v3_lerp(c, a, 0.5f);
      vmtrig_range_bite_owner_tri(a, b, m, set, range_box,
                                   source_uncertain, out, work);
      vmtrig_range_bite_owner_tri(m, b, c, set, range_box,
                                   source_uncertain, out, work);
    }
    return;
  }
  if (witness >= 1e8f) { out->fail = 1; return; }
  if (vmtrig_u64_add(&out->samples, 1)) out->fail = 1;
  double area = 0.5 * (double)v3_len(v3_cross(v3_sub(b, a), v3_sub(c, a)));
  if (full_inside && !source_uncertain) {
    out->depth_lo_area += (double)lo * area;
    out->depth_hi_area += (double)hi * area;
    out->inside_area += area;
  } else {
    out->boundary_area += area;
    out->boundary_depth_hi_area += (double)hi * area;
    if (hi > out->boundary_upper) out->boundary_upper = hi;
  }
  if (hi > out->max_upper) out->max_upper = hi;
}

static int vmtrig_bite_gate_straddle(float lo, float hi,
                                     int source_uncertain) {
  return (lo < 0.0f && hi > 0.0f) ||
         (lo < 0.002f && hi > 0.002f) ||
         (lo < 0.004f && hi > 0.004f) || source_uncertain;
}

static void vmtrig_bite_interval(const vmtrig_bite_t *bite,
                                 float *mean_lo, float *mean_hi) {
  double area = bite->inside_area + bite->boundary_area;
  *mean_lo = area > 0.0 ? (float)(bite->depth_lo_area / area) : 0.0f;
  if (bite->inside_area <= 0.0) {
    *mean_hi = bite->boundary_upper;
    return;
  }
  // A boundary cell contributes an unknown fraction of its area. The maximizing
  // subset is either empty or the full available area at the global depth upper;
  // averaging each whole uncertain cell would be unsound for a thin high-depth sliver.
  double known_hi = bite->depth_hi_area / bite->inside_area;
  double with_boundary = (bite->depth_hi_area +
      bite->boundary_area * (double)bite->boundary_upper) / area;
  *mean_hi = (float)fmax(known_hi, with_boundary);
}

static int vmtrig_bite_reduce(const vmtrig_bite_t *bite,
                              float *mean_lo, float *mean_hi,
                              float *max_upper, double *boundary_area) {
  vmtrig_bite_interval(bite, mean_lo, mean_hi);
  *max_upper = bite->max_upper;
  *boundary_area = bite->boundary_area;
  return bite->fail || bite->inside_area + bite->boundary_area <= 0.0;
}

static int vmtrig_bite_selftest(vmtrig_work_t *work) {
  vmtrig_bite_t boundary = {
    .depth_lo_area = 0.027, .depth_hi_area = 0.0315,
    .boundary_depth_hi_area = 0.0035,
    .inside_area = 9.0, .boundary_area = 1.0,
    .max_upper = 0.0035f, .boundary_upper = 0.0035f,
  };
  float lo, hi;
  vmtrig_bite_interval(&boundary, &lo, &hi);
  int fail = lo < 0.002f || hi > VMTRIG_ALLOWED_BITE_LIMIT;
  boundary.boundary_upper = boundary.max_upper = VMTRIG_ALLOWED_BITE_LIMIT;
  boundary.boundary_depth_hi_area = VMTRIG_ALLOWED_BITE_LIMIT;
  vmtrig_bite_interval(&boundary, &lo, &hi);
  fail |= hi > VMTRIG_ALLOWED_BITE_LIMIT ||
          boundary.max_upper < VMTRIG_ALLOWED_BITE_LIMIT;
  fail |= !vmtrig_bite_gate_straddle(-0.001f, 0.001f, 0) ||
          !vmtrig_bite_gate_straddle(0.001f, 0.003f, 0) ||
          !vmtrig_bite_gate_straddle(0.003f, 0.005f, 0) ||
          !vmtrig_bite_gate_straddle(0.003f, 0.0035f, 1) ||
          vmtrig_bite_gate_straddle(0.0025f, 0.0035f, 0) ||
          vmtrig_bite_gate_straddle(0.0045f, 0.0050f, 0);
  vmtrig_bite_t once = {
    .depth_lo_area = 0.003, .depth_hi_area = 0.003,
    .inside_area = 1.0, .max_upper = 0.003f,
  };
  float range_lo, range_hi, range_max;
  double range_boundary;
  fail |= vmtrig_bite_reduce(&once, &range_lo, &range_hi,
                              &range_max, &range_boundary);
  fail |= range_lo < 0.00299f || range_hi > 0.00301f ||
          range_max < 0.00299f || range_boundary != 0.0;

  // Constructive owner/area regression. The tiny child cube is wholly buried in
  // the host and offers a tempting 0.5 mm nearest face. The physical exterior is the
  // host's 4.5 mm wall: adding that duplicate range must not change area or mean, and
  // its internal nearest point must not become an upper witness. The shallow control
  // then proves that a genuinely exposed 1 mm host still fails low.
  float cham = g_cham, diss = g_fig_diss;
  g_cham = 0.0f; g_fig_diss = 0.0f;
  scene_reset();
  gun_mesh_set_t owner_set = {.count = 2};
  owner_set.range[0].v0 = g_scene_floats;
  scene_box((v3){{0, 0, 0}}, (v3){{0.010f, 0, 0}},
            (v3){{0, 0.010f, 0}}, (v3){{0, 0, 0.010f}},
            (v3){{1, 1, 1}});
  owner_set.range[0].v1 = g_scene_floats;
  owner_set.range[0].surface = 1;
  owner_set.range[0].part = GUN_MESH_PART_FORE_BODY;
  owner_set.range[1].v0 = g_scene_floats;
  scene_box((v3){{0.0055f, 0, 0}}, (v3){{0.0005f, 0, 0}},
            (v3){{0, 0.0005f, 0}}, (v3){{0, 0, 0.0005f}},
            (v3){{1, 1, 1}});
  owner_set.range[1].v1 = g_scene_floats;
  owner_set.range[1].surface = 1;
  owner_set.range[1].part = GUN_MESH_PART_FORE_DETAIL;
  owner_set.range[1].host_mask = GUN_MESH_PART_FORE_BODY;
  vmtrig_aabb_t owner_box[2];
  for (int q = 0; q < 2; q++) {
    int range[2] = {owner_set.range[q].v0, owner_set.range[q].v1};
    owner_box[q] = vmtrig_range_aabb(range);
  }
  const float tiny = 0.00001f;
  v3 deep_pad[3] = {{{0.0055f, -tiny, -tiny}},
                    {{0.0055f,  tiny, -tiny}},
                    {{0.0055f, 0.0f, tiny}}};
  vmtrig_bite_t hosted = {0}, host_only = {0}, shallow = {0};
  vmtrig_range_bite_owner_tri(deep_pad[0], deep_pad[1], deep_pad[2],
                               &owner_set, owner_box, 0, &hosted, work);
  gun_mesh_set_t one = owner_set;
  one.count = 1;
  vmtrig_range_bite_owner_tri(deep_pad[0], deep_pad[1], deep_pad[2],
                               &one, owner_box, 0, &host_only, work);
  v3 shallow_pad[3] = {{{0.0090f, -tiny, -tiny}},
                       {{0.0090f,  tiny, -tiny}},
                       {{0.0090f, 0.0f, tiny}}};
  vmtrig_range_bite_owner_tri(shallow_pad[0], shallow_pad[1], shallow_pad[2],
                               &one, owner_box, 0, &shallow, work);
  float hosted_lo, hosted_hi, host_lo, host_hi, shallow_lo, shallow_hi;
  vmtrig_bite_interval(&hosted, &hosted_lo, &hosted_hi);
  vmtrig_bite_interval(&host_only, &host_lo, &host_hi);
  vmtrig_bite_interval(&shallow, &shallow_lo, &shallow_hi);
  fail |= hosted.fail || host_only.fail || shallow.fail ||
          fabsf(hosted_lo - host_lo) > 2e-5f ||
          fabsf(hosted_hi - host_hi) > 2e-5f ||
          fabs(hosted.inside_area - host_only.inside_area) > 1e-12 ||
          hosted_lo < 0.0043f || hosted_hi <= 0.004f ||
          hosted.max_upper <= 0.004f ||
          shallow_lo >= 0.002f || shallow_hi > 0.0012f;

  // A witness may trail the deepest signed depth only by harmless floating-point
  // roundoff, which is clamped upward. A materially inverted interval must fail.
  float rounded_witness = 0.0049995f, interval_lo, interval_hi;
  fail |= !vmtrig_bite_make_interval(
              0.005f, 0.0001f, 1, &rounded_witness,
              &interval_lo, &interval_hi) ||
          rounded_witness < 0.005f || interval_lo > interval_hi;
  float inverted_witness = 0.003f;
  fail |= vmtrig_bite_make_interval(
              0.005f, 0.0001f, 1, &inverted_witness,
              &interval_lo, &interval_hi);

  // The closest face of an exterior overlay can be its buried inner wall. From a
  // point in the overlay but outside the host, the forward ray enters/traverses the
  // host while the reverse ray exits the visible overlay in 0.8 mm. Only the latter
  // verified union-exterior endpoint is the physical upper witness.
  scene_reset();
  gun_mesh_set_t overlay_set = {.count = 2};
  overlay_set.range[0].v0 = g_scene_floats;
  scene_box((v3){{0, 0, 0}}, (v3){{0.010f, 0, 0}},
            (v3){{0, 0.010f, 0}}, (v3){{0, 0, 0.010f}},
            (v3){{1, 1, 1}});
  overlay_set.range[0].v1 = g_scene_floats;
  overlay_set.range[0].surface = 1;
  overlay_set.range[0].part = GUN_MESH_PART_MAG_BODY;
  overlay_set.range[1].v0 = g_scene_floats;
  scene_box((v3){{0.0105f, 0, 0}}, (v3){{0.0005f, 0, 0}},
            (v3){{0, 0.002f, 0}}, (v3){{0, 0, 0.002f}},
            (v3){{1, 1, 1}});
  overlay_set.range[1].v1 = g_scene_floats;
  overlay_set.range[1].surface = 1;
  overlay_set.range[1].part = GUN_MESH_PART_MAG_DETAIL;
  overlay_set.range[1].host_mask = GUN_MESH_PART_MAG_BODY;
  vmtrig_aabb_t overlay_box[2];
  for (int q = 0; q < 2; q++) {
    int range[2] = {overlay_set.range[q].v0, overlay_set.range[q].v1};
    overlay_box[q] = vmtrig_range_aabb(range);
  }
  v3 overlay_pad[3] = {{{0.0102f, -tiny, -tiny}},
                       {{0.0102f,  tiny, -tiny}},
                       {{0.0102f, 0.0f, tiny}}};
  vmtrig_bite_t overlay = {0};
  vmtrig_range_bite_owner_tri(
      overlay_pad[0], overlay_pad[1], overlay_pad[2],
      &overlay_set, overlay_box, 0, &overlay, work);
  float overlay_lo, overlay_hi;
  vmtrig_bite_interval(&overlay, &overlay_lo, &overlay_hi);
  fail |= overlay.fail || overlay.inside_area <= 0.0 ||
          overlay_lo <= 0.00015f || overlay_hi < 0.00075f ||
          overlay_hi > 0.0011f || overlay.max_upper > 0.0011f;

  // Two solids mating at x=0 exercise the topology the buried-child case cannot:
  // the left range's nearest face is also the right range's boundary, but it is deep
  // inside their union. The stepped witness enters the right cube, so the search must
  // continue to the far outer wall (>10 mm) instead of reporting the 0.5 mm interface
  // as exterior.
  scene_reset();
  gun_mesh_set_t touching = {.count = 2};
  touching.range[0].v0 = g_scene_floats;
  scene_box((v3){{-0.005f, 0, 0}}, (v3){{0.005f, 0, 0}},
            (v3){{0, 0.010f, 0}}, (v3){{0, 0, 0.010f}},
            (v3){{1, 1, 1}});
  touching.range[0].v1 = g_scene_floats;
  touching.range[0].surface = 1;
  touching.range[0].part = GUN_MESH_PART_FORE_BODY;
  touching.range[1].v0 = g_scene_floats;
  scene_box((v3){{0.005f, 0, 0}}, (v3){{0.005f, 0, 0}},
            (v3){{0, 0.010f, 0}}, (v3){{0, 0, 0.010f}},
            (v3){{1, 1, 1}});
  touching.range[1].v1 = g_scene_floats;
  touching.range[1].surface = 1;
  touching.range[1].part = GUN_MESH_PART_FORE_DETAIL;
  touching.range[1].host_mask = GUN_MESH_PART_FORE_BODY;
  vmtrig_aabb_t touching_box[2];
  for (int q = 0; q < 2; q++) {
    int range[2] = {touching.range[q].v0, touching.range[q].v1};
    touching_box[q] = vmtrig_range_aabb(range);
  }
  // Center the root cell exactly on the internal x=0 mating plane, with ab as its
  // unique longest edge. The first split therefore produces strictly left/right
  // centers and must retain the union's distant outer-wall witness.
  const float seam_span = 0.0006f;
  v3 tie_pad[3] = {{{-seam_span, -0.1f * seam_span, 0.0f}},
                   {{ seam_span, -0.1f * seam_span, 0.0f}},
                   {{0.0f, 0.2f * seam_span, 0.0f}}};
  vmtrig_bite_t tie = {0};
  vmtrig_range_bite_owner_tri(tie_pad[0], tie_pad[1], tie_pad[2],
                               &touching, touching_box, 0, &tie, work);
  fail |= tie.fail || tie.max_upper < 0.010f;

  // A terminal cell wholly on the same internal plane has no constructive exterior
  // point. It must fail closed rather than treating zero surface depth as a zero-
  // distance complement witness.
  v3 seam_pad[3] = {{{0.0f, -tiny, -tiny}},
                    {{0.0f,  tiny, -tiny}},
                    {{0.0f, 0.0f, tiny}}};
  vmtrig_bite_t seam = {0};
  vmtrig_range_bite_owner_tri(seam_pad[0], seam_pad[1], seam_pad[2],
                               &touching, touching_box, 0, &seam, work);
  fail |= !seam.fail;
  g_cham = cham; g_fig_diss = diss;
  return fail;
}

static void vmtrig_weapon_union_partition(
    v3 a, v3 b, v3 c, const fig_contact_patch_t *const *patch, int npatch,
    int contact_surface, int source, vmtrig_patch_leaves_t *out,
    uint64_t *overlap) {
  if (out->overflow) return;
  if (!contact_surface) {
    vmtrig_patch_leaf_push(out, a, b, c, 0, source, 2.0f);
    return;
  }
  int certified = 0, ambiguous = 0;
  float certified_normal = -2.0f;
  for (int i = 0; i < npatch; i++) {
    if (!strcmp(patch[i]->name, "trigger_digit") ||
        vmtrig_patch_disjoint(a, b, c, patch[i], VMTRIG_PATCH_WEAPON)) continue;
    float leaf_normal = -2.0f;
    if (vmtrig_patch_leaf_cert(a, b, c, patch[i], VMTRIG_PATCH_WEAPON,
                                &leaf_normal)) {
      certified++;
      certified_normal = fmaxf(certified_normal, leaf_normal);
    }
    else ambiguous++;
  }
  // Census is source-exact: ambiguous authored patch cells remain one BOTH source
  // leaf and are refined only by a concrete distance query.
  (void)overlap;
  int before = out->n;
  vmtrig_patch_leaf_push(out, a, b, c, certified > 0, source,
                         certified > 0 ? certified_normal : -2.0f);
  if (out->n > before && !certified && ambiguous)
    out->v[before].boundary = 1,
    out->v[before].class_mask = VMTRIG_CLASS_BOTH;
}

static int vmtrig_range_interval_selftest(vmtrig_work_t *work) {
  (void)work;
  int fail = vmtrig_ambiguity_gate(7, 7) ||
             !vmtrig_ambiguity_gate(7, 8);
  vmtrig_allowed_mesh_t boundary = {
    .boundary_cross = 1, .contact_boundary_area = 1e-6,
    .contact_boundary_upper = 0.0004f,
  };
  fail |= vmtrig_boundary_gate(&boundary);
  boundary.contact_boundary_upper = 0.0006f;
  fail |= !vmtrig_boundary_gate(&boundary);
  boundary.contact_boundary_upper = 0.0004f;
  boundary.contact_boundary_area = 0.0;
  fail |= !vmtrig_boundary_gate(&boundary);
  return fail;
}

static int vmtrig_leaf_overlap(const vmtrig_patch_leaf_t *a,
                               const vmtrig_patch_leaf_t *b) {
  for (int k = 0; k < 3; k++)
    if (a->hi.e[k] < b->lo.e[k] || b->hi.e[k] < a->lo.e[k]) return 0;
  return 1;
}

static int vmtrig_leaf_node_overlap(const vmtrig_leaf_bvh_node_t *a,
                                    const vmtrig_leaf_bvh_node_t *b) {
  for (int k = 0; k < 3; k++)
    if (a->hi.e[k] < b->lo.e[k] || b->hi.e[k] < a->lo.e[k]) return 0;
  return 1;
}

static int vmtrig_segment_tri_cross_at(v3 p0, v3 p1, v3 a, v3 b, v3 c,
                                        v3 *at) {
  if (vmtrig_v3_nonfinite(p0) || vmtrig_v3_nonfinite(p1) ||
      vmtrig_v3_nonfinite(a) || vmtrig_v3_nonfinite(b) ||
      vmtrig_v3_nonfinite(c)) return 0;
  double d[3], e1[3], e2[3], tv[3];
  for (int k = 0; k < 3; k++) {
    d[k] = (double)p1.e[k] - (double)p0.e[k];
    e1[k] = (double)b.e[k] - (double)a.e[k];
    e2[k] = (double)c.e[k] - (double)a.e[k];
    tv[k] = (double)p0.e[k] - (double)a.e[k];
  }
  double pv[3] = {
    d[1] * e2[2] - d[2] * e2[1],
    d[2] * e2[0] - d[0] * e2[2],
    d[0] * e2[1] - d[1] * e2[0],
  };
  double det = e1[0] * pv[0] + e1[1] * pv[1] + e1[2] * pv[2];
  double e1_len = sqrt(e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2]);
  double e2_len = sqrt(e2[0] * e2[0] + e2[1] * e2[1] + e2[2] * e2[2]);
  double d_len = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
  double scale = e1_len * e2_len * d_len;
  if (!(scale > 0.0) || fabs(det) <= scale * 1e-14) return 0;
  double inv = 1.0 / det;
  double u = (tv[0] * pv[0] + tv[1] * pv[1] + tv[2] * pv[2]) * inv;
  double qv[3] = {
    tv[1] * e1[2] - tv[2] * e1[1],
    tv[2] * e1[0] - tv[0] * e1[2],
    tv[0] * e1[1] - tv[1] * e1[0],
  };
  double v = (d[0] * qv[0] + d[1] * qv[1] + d[2] * qv[2]) * inv;
  double t = (e2[0] * qv[0] + e2[1] * qv[1] + e2[2] * qv[2]) * inv;
  // Only exact/roundoff-sized endpoint and shared-edge contacts are open. The former
  // 1e-5 barycentric margin hid a genuine traversal up to ten parts per million into
  // every triangle; double arithmetic supports a two-orders-tighter guard.
  const double open_eps = 1e-7;
  if (u <= open_eps || v <= open_eps || 1.0 - u - v <= open_eps ||
      t <= open_eps || 1.0 - t <= open_eps) return 0;
  if (at) *at = v3_lerp(p0, p1, (float)t);
  return 1;
}

// Each closed source range is independent. A crossing is certified red when either
// face is forbidden-only, green when both are allowed-only, and remains a quantitative
// boundary event when a terminal patch cell carries both classes.
static int vmtrig_leaf_pair_cross(
    const vmtrig_patch_leaf_t *al, const vmtrig_patch_leaves_t *a,
    const vmtrig_patch_leaf_t *bl, const vmtrig_patch_leaves_t *b,
    int *boundary_cross, vmtrig_work_t *work) {
  v3 av[3] = {al->a, al->b, al->c};
  v3 bv[3] = {bl->a, bl->b, bl->c};
  int certified = 0;
  for (int side = 0; side < 2; side++)
    for (int e = 0; e < 3; e++) {
      const v3 *edge = side ? bv : av;
      const v3 *tri = side ? av : bv;
      v3 at;
      if (!vmtrig_segment_tri_cross_at(edge[e], edge[(e + 1) % 3],
                                        tri[0], tri[1], tri[2], &at)) continue;
      // The weapon leaf may be an authored overlay. A crossing on its internal host-
      // buried face is not on production's fused exterior; a host-boundary tie remains
      // visible and therefore conservative.
      if (vmtrig_weapon_host_cell(at, 0.0f, b->mesh_set, b->mesh_range,
                                  work) == VMTRIG_HOST_BURIED)
        continue;
      int ac = al->class_mask, bc = bl->class_mask;
      if (ac == VMTRIG_CLASS_BOTH && a->patch_candidate)
        ac = vmtrig_patch_point_class(at, al->a, al->b, al->c,
                                      a->patch, a->patch_mode);
      if (bc == VMTRIG_CLASS_BOTH && b->patch_candidate)
        bc = vmtrig_patch_point_class(at, bl->a, bl->b, bl->c,
                                      b->patch, b->patch_mode);
      int forbidden = ac == VMTRIG_CLASS_FORBIDDEN ||
                      bc == VMTRIG_CLASS_FORBIDDEN;
      if (forbidden) {
        certified++;
      }
      else if (ac == VMTRIG_CLASS_ALLOWED && bc == VMTRIG_CLASS_ALLOWED)
        continue;
      else if (*boundary_cross < INT_MAX) (*boundary_cross)++;
    }
  vmtrig_work_add(work, 6);
  return certified;
}

static int vmtrig_range_cross_selftest(vmtrig_work_t *work) {
  float cham = g_cham, diss = g_fig_diss;
  g_cham = 0.0f; g_fig_diss = 0.0f;
  scene_reset();
  int host_v0 = g_scene_floats;
  scene_box((v3){{0, 0, 0}}, (v3){{0.010f, 0, 0}},
            (v3){{0, 0.010f, 0}}, (v3){{0, 0, 0.010f}},
            (v3){{1, 1, 1}});
  gun_mesh_set_t set = {
    .range = {
      {host_v0, g_scene_floats, 1, GUN_MESH_PART_FORE_BODY, 0},
      {0, 0, 1, GUN_MESH_PART_FORE_DETAIL, 0},
    },
    .count = 2,
  };
  vmtrig_patch_leaves_t hand, weapon;
  vmtrig_patch_leaves_init(&hand, VMTRIG_PATCH_HAND);
  vmtrig_patch_leaves_init(&weapon, VMTRIG_PATCH_WEAPON);
  v3 ha = {{-0.010f, -0.010f, 0}}, hb = {{0.010f, -0.010f, 0}};
  v3 hc = {{0, 0.010f, 0}};
  v3 wa = {{0, -0.006f, -0.010f}}, wb = {{0, 0.007f, -0.010f}};
  v3 wc = {{0, 0.001f, 0.010f}};
  vmtrig_patch_leaf_push(&hand, ha, hb, hc, 1, 0, 1.0f);
  vmtrig_patch_leaf_push(&weapon, wa, wb, wc, 1, 0, 1.0f);
  weapon.mesh_set = &set;
  weapon.mesh_range = 1;
  int fail = hand.n != 1 || weapon.n != 1 || hand.overflow || weapon.overflow;
  {
    v3 ta = {{0, 0, 0}}, tb = {{1, 0, 0}}, tc = {{0, 1, 0}};
    v3 near0 = {{5e-6f, 0.25f, -1.0f}};
    v3 near1 = {{5e-6f, 0.25f,  1.0f}};
    v3 edge0 = {{0.0f, 0.25f, -1.0f}};
    v3 edge1 = {{0.0f, 0.25f,  1.0f}};
    // Five parts per million inside is a real traversal; exactly on the shared edge
    // remains an open contact. This is the regression that the former 1e-5 margin
    // could not distinguish.
    fail |= !vmtrig_segment_tri_cross_at(near0, near1, ta, tb, tc, NULL);
    fail |= vmtrig_segment_tri_cross_at(edge0, edge1, ta, tb, tc, NULL);
  }
  if (!fail) {
    hand.v[0].owner = weapon.v[0].owner = 0;
    int boundary_cross = 0;
    fail |= vmtrig_leaf_pair_cross(&hand.v[0], &hand, &weapon.v[0], &weapon,
                                    &boundary_cross, work) != 0;
    fail |= boundary_cross != 0;
    weapon.v[0].class_mask = VMTRIG_CLASS_BOTH;
    fail |= vmtrig_leaf_pair_cross(&hand.v[0], &hand, &weapon.v[0], &weapon,
                                    &boundary_cross, work) != 0;
    fail |= boundary_cross == 0;
    weapon.v[0].allowed = 0;
    weapon.v[0].class_mask = VMTRIG_CLASS_FORBIDDEN;
    fail |= vmtrig_leaf_pair_cross(&hand.v[0], &hand, &weapon.v[0], &weapon,
                                    &boundary_cross, work) == 0;
    // The same forbidden overlay crossing is green only with an explicit host and a
    // witness strictly inside it. Boundary/outside cells remain proof-visible.
    set.range[1].host_mask = GUN_MESH_PART_FORE_BODY;
    fail |= vmtrig_leaf_pair_cross(&hand.v[0], &hand, &weapon.v[0], &weapon,
                                    &boundary_cross, work) != 0;
    fail |= vmtrig_weapon_host_cell((v3){{0, 0, 0}}, 0.001f, &set, 1,
                                     work) != VMTRIG_HOST_BURIED;
    fail |= vmtrig_weapon_host_cell((v3){{0.0095f, 0, 0}}, 0.001f, &set, 1,
                                     work) != VMTRIG_HOST_STRADDLE;
    fail |= vmtrig_weapon_host_cell((v3){{0.020f, 0, 0}}, 0.001f, &set, 1,
                                     work) != VMTRIG_HOST_OUTSIDE;
  }
  vmtrig_patch_leaves_release(&weapon);
  vmtrig_patch_leaves_release(&hand);
  g_cham = cham; g_fig_diss = diss;
  return fail;
}

static int vmtrig_leaf_cross_nodes(const vmtrig_patch_leaves_t *a, int ai,
                                   const vmtrig_patch_leaves_t *b, int bi,
                                   int a_owner, int *boundary_cross,
                                   vmtrig_work_t *work) {
  if (work && work->overflow) return 65;
  const vmtrig_leaf_bvh_node_t *an = &a->node[ai], *bn = &b->node[bi];
  vmtrig_work_add(work, 1);
  if (!an->mask || !bn->mask || !vmtrig_leaf_node_overlap(an, bn) ||
      (an->mask == 1 && bn->mask == 1)) return 0;
  if (an->left < 0 && bn->left < 0) {
    int cross = 0;
    for (int i = an->first; i < an->first + an->count; i++)
      for (int j = bn->first; j < bn->first + bn->count; j++) {
        const vmtrig_patch_leaf_t *al = &a->v[i], *bl = &b->v[j];
        if (al->owner != a_owner || !vmtrig_leaf_overlap(al, bl)) continue;
        cross += vmtrig_leaf_pair_cross(al, a, bl, b, boundary_cross, work);
        if (cross > 64) return cross;
      }
    return cross;
  }
  int first, second;
  if (bn->left < 0 ||
      (an->left >= 0 && an->count >= bn->count)) {
    first = vmtrig_leaf_cross_nodes(a, an->left, b, bi, a_owner,
                                     boundary_cross, work);
    if (first > 64) return first;
    second = vmtrig_leaf_cross_nodes(a, an->right, b, bi, a_owner,
                                      boundary_cross, work);
  } else {
    first = vmtrig_leaf_cross_nodes(a, ai, b, bn->left, a_owner,
                                     boundary_cross, work);
    if (first > 64) return first;
    second = vmtrig_leaf_cross_nodes(a, ai, b, bn->right, a_owner,
                                      boundary_cross, work);
  }
  return second > 64 - first ? 65 : first + second;
}

static int vmtrig_leaf_cross(const vmtrig_patch_leaves_t *a,
                             const vmtrig_patch_leaves_t *b,
                             int a_owner, int *boundary_cross,
                             vmtrig_work_t *work) {
  if (!a->bvh_ready || !b->bvh_ready || !a->node || !b->node) return 0;
  return vmtrig_leaf_cross_nodes(a, 0, b, 0, a_owner, boundary_cross,
                                  work);
}

static void vmtrig_range_bite_tri(
    v3 a, v3 b, v3 c, const vmtrig_patch_leaves_t *target,
    int source_uncertain, vmtrig_bite_t *out,
                                  vmtrig_work_t *work) {
  if (work && work->overflow) { out->fail = 1; return; }
  float ab = v3_len(v3_sub(b, a)), bc = v3_len(v3_sub(c, b));
  float ca = v3_len(v3_sub(c, a));
  v3 center = v3_scale(v3_add(a, v3_add(b, c)), 1.0f / 3.0f);
  float radius = vmtrig_tri_radius(a, b, c, center);
  float depth = vmtrig_leaf_range_depth(center, target, work);
  float lo = depth - radius, hi = depth + radius;
  int outside = hi < 0.0f, inside = lo > 0.0f;
  int gate_straddle = vmtrig_bite_gate_straddle(lo, hi, source_uncertain);
  if (gate_straddle && radius > VMTRIG_PATCH_LEAF_RADIUS) {
    if (ab >= bc && ab >= ca) {
      v3 m = v3_lerp(a, b, 0.5f);
      vmtrig_range_bite_tri(a, m, c, target, source_uncertain, out, work);
      vmtrig_range_bite_tri(m, b, c, target, source_uncertain, out, work);
    } else if (bc >= ca) {
      v3 m = v3_lerp(b, c, 0.5f);
      vmtrig_range_bite_tri(a, b, m, target, source_uncertain, out, work);
      vmtrig_range_bite_tri(a, m, c, target, source_uncertain, out, work);
    } else {
      v3 m = v3_lerp(c, a, 0.5f);
      vmtrig_range_bite_tri(a, b, m, target, source_uncertain, out, work);
      vmtrig_range_bite_tri(m, b, c, target, source_uncertain, out, work);
    }
    return;
  }
  if (vmtrig_u64_add(&out->samples, 1)) out->fail = 1;
  if (outside) return;
  double area = 0.5 * (double)v3_len(v3_cross(v3_sub(b, a), v3_sub(c, a)));
  if (inside && !source_uncertain) {
    out->depth_lo_area += (double)lo * area;
    out->depth_hi_area += (double)hi * area;
    out->inside_area += area;
  } else {
    float upper = fmaxf(hi, 0.0f);
    out->boundary_area += area;
    out->boundary_depth_hi_area += (double)upper * area;
    if (upper > out->boundary_upper) out->boundary_upper = upper;
  }
  if (hi > out->max_upper) out->max_upper = hi;
}

static void vmtrig_range_wall_class_tri(
    v3 a, v3 b, v3 c, const vmtrig_patch_leaves_t *target, int source_class,
    int target_contact_surface, vmtrig_wall_t *out, vmtrig_work_t *work,
    int source_tri) {
  if ((work && work->overflow) || out->fail || out->allowed_fail) return;
  float ab = v3_len(v3_sub(b, a)), bc = v3_len(v3_sub(c, b));
  float ca = v3_len(v3_sub(c, a));
  v3 center = v3_scale(v3_add(a, v3_add(b, c)), 1.0f / 3.0f);
  float radius = vmtrig_tri_radius(a, b, c, center);
  float depth = vmtrig_leaf_range_depth(center, target, work);
  float upper = fmaxf(depth + radius, 0.0f);
  int contact = source_class == VMTRIG_CLASS_ALLOWED &&
                target_contact_surface;
  float limit = contact ? VMTRIG_ALLOWED_BITE_LIMIT : VMTRIG_OPPOSITE_LIMIT;
  if (vmtrig_u64_add(&out->samples, 1)) out->fail = 1;
  if (!contact && depth > out->seen) {
    out->seen = depth;
    out->source_tri = source_tri;
    out->at = center;
  }
  if (upper <= limit) {
    if (contact) {
      if (upper > out->allowed_max) out->allowed_max = upper;
    } else if (upper > out->certified) out->certified = upper;
    if (source_class == VMTRIG_CLASS_BOTH) {
      out->boundary_area += 0.5 * (double)v3_len(
          v3_cross(v3_sub(b, a), v3_sub(c, a)));
      if (upper > out->boundary_upper) out->boundary_upper = upper;
    }
    return;
  }
  if ((!contact && depth > limit) || (contact && depth > 0.004f)) {
    out->source_tri = source_tri;
    out->at = center;
    if (contact) out->allowed_fail = 1;
    else out->fail = 1;
    return;
  }
  if (radius <= VMTRIG_WALL_LEAF_RADIUS) {
    out->source_tri = source_tri;
    out->at = center;
    if (source_class == VMTRIG_CLASS_BOTH) {
      out->boundary_area += 0.5 * (double)v3_len(
          v3_cross(v3_sub(b, a), v3_sub(c, a)));
      if (upper > out->boundary_upper) out->boundary_upper = upper;
    }
    if (contact) {
      if (upper > out->allowed_max) out->allowed_max = upper;
      out->allowed_fail = 1;
    } else {
      if (upper > out->certified) out->certified = upper;
      out->fail = 1;
    }
    return;
  }
  if (ab >= bc && ab >= ca) {
    v3 m = v3_lerp(a, b, 0.5f);
    vmtrig_range_wall_class_tri(a, m, c, target, source_class,
                                 target_contact_surface, out, work, source_tri);
    vmtrig_range_wall_class_tri(m, b, c, target, source_class,
                                 target_contact_surface, out, work, source_tri);
  } else if (bc >= ca) {
    v3 m = v3_lerp(b, c, 0.5f);
    vmtrig_range_wall_class_tri(a, b, m, target, source_class,
                                 target_contact_surface, out, work, source_tri);
    vmtrig_range_wall_class_tri(a, m, c, target, source_class,
                                 target_contact_surface, out, work, source_tri);
  } else {
    v3 m = v3_lerp(c, a, 0.5f);
    vmtrig_range_wall_class_tri(a, b, m, target, source_class,
                                 target_contact_surface, out, work, source_tri);
    vmtrig_range_wall_class_tri(m, b, c, target, source_class,
                                 target_contact_surface, out, work, source_tri);
  }
}

// Exercise the production per-range interval code against one closed primitive.
// The broad crossing test above proves topology/parity; these tiny planar witnesses
// specifically guard the two numeric contracts that geometry tuning depends on:
// ordinary walls separate 0.35 from 0.65 mm, while an authored contact separates a
// safe 3 mm pad from a genuine 5 mm spike.  No threshold is represented by a mocked
// result -- all depths come from the exact closed cube and its BVH/parity query.
static int vmtrig_range_depth_selftest(vmtrig_work_t *work) {
  float cham = g_cham, diss = g_fig_diss;
  g_cham = 0.0f; g_fig_diss = 0.0f;
  scene_reset();
  int cube[2] = {g_scene_floats, 0};
  scene_box((v3){{0, 0, 0}}, (v3){{0.010f, 0, 0}},
            (v3){{0, 0.010f, 0}}, (v3){{0, 0, 0.010f}},
            (v3){{1, 1, 1}});
  cube[1] = g_scene_floats;

  vmtrig_patch_leaves_t target;
  vmtrig_patch_leaves_init(&target, VMTRIG_PATCH_WEAPON);
  vmtrig_patch_range_add(&target, cube);
  for (int f = cube[0]; f + 2 * VERT_FLOATS < cube[1];
       f += 3 * VERT_FLOATS)
    vmtrig_patch_leaf_push(&target, SCENE_V(f), SCENE_V(f + VERT_FLOATS),
                            SCENE_V(f + 2 * VERT_FLOATS), 1, 0, 1.0f);
  vmtrig_leaf_bvh_build(&target);

  const float yz = 0.00004f;
  v3 safe_wall[3] = {{{0.00965f, -yz, -yz}},
                     {{0.00965f,  yz, -yz}},
                     {{0.00965f, 0.0f, yz}}};
  v3 bad_wall[3] = {{{0.00935f, -yz, -yz}},
                    {{0.00935f,  yz, -yz}},
                    {{0.00935f, 0.0f, yz}}};
  v3 safe_pad[3] = {{{0.00700f, -yz, -yz}},
                    {{0.00700f,  yz, -yz}},
                    {{0.00700f, 0.0f, yz}}};
  v3 bad_pad[3] = {{{0.00350f, -yz, -yz}},
                   {{0.00350f,  yz, -yz}},
                   {{0.00350f, 0.0f, yz}}};
  vmtrig_wall_t sw = {0}, bw = {0}, sp = {0}, bp = {0};
  vmtrig_range_wall_class_tri(safe_wall[0], safe_wall[1], safe_wall[2],
                               &target, VMTRIG_CLASS_FORBIDDEN, 0, &sw, work, 0);
  vmtrig_range_wall_class_tri(bad_wall[0], bad_wall[1], bad_wall[2],
                               &target, VMTRIG_CLASS_FORBIDDEN, 0, &bw, work, 1);
  vmtrig_range_wall_class_tri(safe_pad[0], safe_pad[1], safe_pad[2],
                               &target, VMTRIG_CLASS_ALLOWED, 1, &sp, work, 2);
  vmtrig_range_wall_class_tri(bad_pad[0], bad_pad[1], bad_pad[2],
                               &target, VMTRIG_CLASS_ALLOWED, 1, &bp, work, 3);

  vmtrig_bite_t safe_bite = {0}, bad_bite = {0};
  vmtrig_range_bite_tri(safe_pad[0], safe_pad[1], safe_pad[2], &target, 0,
                         &safe_bite, work);
  vmtrig_range_bite_tri(bad_pad[0], bad_pad[1], bad_pad[2], &target, 0,
                         &bad_bite, work);
  float safe_lo = 0.0f, safe_hi = 0.0f;
  vmtrig_bite_interval(&safe_bite, &safe_lo, &safe_hi);
  int fail = target.overflow || !target.bvh_ready || target.n != 12 ||
             sw.fail || sw.certified <= 0.0f || !bw.fail ||
             sp.allowed_fail || sp.allowed_max < 0.0028f ||
             sp.allowed_max > VMTRIG_ALLOWED_BITE_LIMIT || !bp.allowed_fail ||
             safe_bite.fail || safe_lo < 0.002f ||
             safe_hi > VMTRIG_ALLOWED_BITE_LIMIT ||
             safe_bite.max_upper > VMTRIG_ALLOWED_BITE_LIMIT ||
             bad_bite.max_upper <= VMTRIG_ALLOWED_BITE_LIMIT;
  vmtrig_patch_leaves_release(&target);
  g_cham = cham; g_fig_diss = diss;
  return fail;
}

static void vmtrig_range_patch_tri(
    v3 a, v3 b, v3 c, const fig_contact_patch_t *patch, int mode,
    int candidate, const vmtrig_patch_leaves_t *target,
    int target_contact_surface, const gun_mesh_set_t *source_set,
    int source_range,
    vmtrig_bite_t *bite, vmtrig_wall_t *wall, vmtrig_work_t *work,
    int source_tri) {
  if ((work && work->overflow) || wall->fail || wall->allowed_fail) return;
  v3 center = v3_scale(v3_add(a, v3_add(b, c)), 1.0f / 3.0f);
  float radius = vmtrig_tri_radius(a, b, c, center);
  float depth = vmtrig_leaf_range_depth(center, target, work);
  float upper = fmaxf(depth + radius, 0.0f);
  // Patch membership cannot affect a source cell whose complete depth interval is
  // already within the ordinary 0.5 mm wall gate. For Bite, retain shallow cells
  // that may be inside; outside cells have no contact area and stop here as well.
  if (upper <= VMTRIG_OPPOSITE_LIMIT &&
      (!bite || !target_contact_surface || upper <= 0.0f)) {
    if (vmtrig_u64_add(&wall->samples, 1)) wall->fail = 1;
    if (upper > wall->certified) wall->certified = upper;
    return;
  }
  int host_state = mode == VMTRIG_PATCH_WEAPON
                 ? vmtrig_weapon_host_cell(center, radius, source_set,
                                            source_range, work)
                 : VMTRIG_HOST_NONE;
  if (host_state == VMTRIG_HOST_BURIED) {
    if (vmtrig_u64_add(&wall->host_buried, 1)) wall->fail = 1;
    return;
  }
  if (host_state == VMTRIG_HOST_STRADDLE) {
    if (vmtrig_u64_add(&wall->host_straddle, 1)) wall->fail = 1;
    if (radius > VMTRIG_PATCH_LEAF_RADIUS) {
      float ab = v3_len(v3_sub(b, a)), bc = v3_len(v3_sub(c, b));
      float ca = v3_len(v3_sub(c, a));
      if (ab >= bc && ab >= ca) {
        v3 m = v3_lerp(a, b, 0.5f);
        vmtrig_range_patch_tri(a, m, c, patch, mode, candidate, target,
                                target_contact_surface, source_set, source_range,
                                bite, wall, work, source_tri);
        vmtrig_range_patch_tri(m, b, c, patch, mode, candidate, target,
                                target_contact_surface, source_set, source_range,
                                bite, wall, work, source_tri);
      } else if (bc >= ca) {
        v3 m = v3_lerp(b, c, 0.5f);
        vmtrig_range_patch_tri(a, b, m, patch, mode, candidate, target,
                                target_contact_surface, source_set, source_range,
                                bite, wall, work, source_tri);
        vmtrig_range_patch_tri(a, m, c, patch, mode, candidate, target,
                                target_contact_surface, source_set, source_range,
                                bite, wall, work, source_tri);
      } else {
        v3 m = v3_lerp(c, a, 0.5f);
        vmtrig_range_patch_tri(a, b, m, patch, mode, candidate, target,
                                target_contact_surface, source_set, source_range,
                                bite, wall, work, source_tri);
        vmtrig_range_patch_tri(m, b, c, patch, mode, candidate, target,
                                target_contact_surface, source_set, source_range,
                                bite, wall, work, source_tri);
      }
      return;
    }
    // A terminal cell contains only two possible subsets: host-buried source, which
    // is not on the production exterior, and host-exterior source, which retains the
    // ordinary authored patch classification. If the complete cell is patch-certified
    // it is therefore safe to grant that exterior subset contact; a patch straddle
    // remains BOTH below and is never guessed allowed.
    if (vmtrig_u64_add(&wall->host_terminal, 1)) wall->fail = 1;
  }
  int source_class = VMTRIG_CLASS_FORBIDDEN;
  if (candidate && !vmtrig_patch_disjoint(a, b, c, patch, mode)) {
    source_class = vmtrig_patch_leaf_cert(a, b, c, patch, mode, NULL)
                 ? VMTRIG_CLASS_ALLOWED : VMTRIG_CLASS_BOTH;
  }
  if (source_class == VMTRIG_CLASS_BOTH &&
      radius > VMTRIG_PATCH_LEAF_RADIUS) {
    float ab = v3_len(v3_sub(b, a)), bc = v3_len(v3_sub(c, b));
    float ca = v3_len(v3_sub(c, a));
    if (ab >= bc && ab >= ca) {
      v3 m = v3_lerp(a, b, 0.5f);
      vmtrig_range_patch_tri(a, m, c, patch, mode, candidate, target,
                              target_contact_surface, source_set, source_range,
                              bite, wall, work,
                              source_tri);
      vmtrig_range_patch_tri(m, b, c, patch, mode, candidate, target,
                              target_contact_surface, source_set, source_range,
                              bite, wall, work,
                              source_tri);
    } else if (bc >= ca) {
      v3 m = v3_lerp(b, c, 0.5f);
      vmtrig_range_patch_tri(a, b, m, patch, mode, candidate, target,
                              target_contact_surface, source_set, source_range,
                              bite, wall, work,
                              source_tri);
      vmtrig_range_patch_tri(a, m, c, patch, mode, candidate, target,
                              target_contact_surface, source_set, source_range,
                              bite, wall, work,
                              source_tri);
    } else {
      v3 m = v3_lerp(c, a, 0.5f);
      vmtrig_range_patch_tri(a, b, m, patch, mode, candidate, target,
                              target_contact_surface, source_set, source_range,
                              bite, wall, work,
                              source_tri);
      vmtrig_range_patch_tri(m, b, c, patch, mode, candidate, target,
                              target_contact_surface, source_set, source_range,
                              bite, wall, work,
                              source_tri);
    }
    return;
  }
  if (bite && (source_class == VMTRIG_CLASS_ALLOWED ||
               source_class == VMTRIG_CLASS_BOTH) && target_contact_surface)
    vmtrig_range_bite_tri(a, b, c, target,
                           source_class == VMTRIG_CLASS_BOTH, bite, work);
  vmtrig_range_wall_class_tri(a, b, c, target, source_class,
                               target_contact_surface, wall, work, source_tri);
}

// Production patch versus its exact closed held primitive. Every final source
// triangle is classified exactly once. Only contact-facing hand triangles and weapon
// triangles covered by the authored projected node path may overlap; every other pair
// is the forbidden opposite-wall complement and is rasterized/crossed exhaustively.
static vmtrig_allowed_mesh_t vmtrig_allowed_mesh(gun_contact_kind_t kind, int hand,
                                                 const vmtrig_hand_range_t *hr,
                                                 int nh, vmtrig_work_t *work) {
  const gun_mesh_set_t *set = &g_gun_contact_mesh[kind];
  vmtrig_allowed_mesh_t out = {
    .min_allowed_normal = 1.0f,
    .hand_child = -1, .weapon_child = -1,
    .hand_tri = -1, .weapon_tri = -1,
    .hand_range = -1, .weapon_range = -1,
    .hand_contact_kind = -1, .weapon_contact_kind = -1,
    .hand_peak_owner = -1, .weapon_peak_owner = -1,
  };
  vmtrig_bite_t bite = {0};
  vmtrig_exact_cert_ctx_t exact_cert = {
    .work_limit = VMTRIG_EXACT_CERT_WORK_LIMIT,
    .stack_limit = VMTRIG_EXACT_CERT_STACK_CAP,
  };
  vmtrig_aabb_t range_box[GUN_CONTACT_MESH_MAX] = {0};
  for (int q = 0; q < set->count; q++) {
    int range[2] = {set->range[q].v0, set->range[q].v1};
    range_box[q] = vmtrig_range_aabb(range);
  }
  const fig_contact_patch_t *hand_patch[FIG_CONTACT_PATCH_MAX];
  int npatch = 0;
  int patch_children = 0;
  uint64_t expected_hand_sources = 0, expected_weapon_sources = 0;
  for (int i = 0; i < nh; i++) {
    if (hr[i].hand == hand) {
      patch_children++;
      int matches = 0;
      const fig_contact_patch_t *patch = vmtrig_contact_patch(&hr[i], &matches);
      if (matches != 1) out.census_missing++;
      if (patch && strcmp(patch->name, "trigger_digit") &&
          npatch < FIG_CONTACT_PATCH_MAX) hand_patch[npatch++] = patch;
    }
  }
  if (patch_children != 6 || g_fig_contact_patch_overflow) out.census_missing++;
  for (int a = 0; a < set->count; a++)
    for (int b = a + 1; b < set->count; b++)
      if (set->range[a].v0 < set->range[b].v1 &&
          set->range[b].v0 < set->range[a].v1) out.census_overlap++;
  for (int q = 0; q < set->count; q++) {
    uint32_t part = set->range[q].part;
    out.census_missing += part && (part & (part - 1u));
    for (uint32_t bit = 1u; bit; bit <<= 1)
      if (set->range[q].host_mask & bit) {
        int hosts = 0;
        for (int h = 0; h < set->count; h++)
          hosts += h != q && (set->range[h].part & bit) != 0;
        out.census_missing += hosts != 1;
      }
  }

  // Classify each source triangle against all child patches, then immediately fold
  // its leaves into the exact census. The scratch peak is therefore the maximum
  // source fanout, never the sum of unrelated closed ranges.
  for (int q = 0; q < set->count; q++) {
    int wr[2] = {set->range[q].v0, set->range[q].v1};
    int floats = wr[1] - wr[0];
    out.census_missing += vmtrig_u64_add(&out.ranges, 1);
    if (floats <= 0 || floats % (3 * VERT_FLOATS)) out.census_missing++;
    else out.census_missing += vmtrig_u64_add(
        &expected_weapon_sources, (uint64_t)(floats / (3 * VERT_FLOATS)));
    for (int wf = wr[0]; wf + 2 * VERT_FLOATS < wr[1]; wf += 3 * VERT_FLOATS) {
      v3 wa = SCENE_V(wf), wbv = SCENE_V(wf + VERT_FLOATS);
      v3 wc = SCENE_V(wf + 2 * VERT_FLOATS);
      vmtrig_patch_leaves_t source_leaf;
      vmtrig_patch_leaves_init(&source_leaf, VMTRIG_PATCH_WEAPON);
      out.census_missing += vmtrig_u64_add(&out.weapon_sources, 1);
      vmtrig_weapon_union_partition(
          wa, wbv, wc, hand_patch, npatch, set->range[q].surface,
          (wf - wr[0]) / (3 * VERT_FLOATS), &source_leaf,
          &out.census_overlap);
      uint64_t fanout = (uint64_t)source_leaf.n;
      if (fanout > out.weapon_source_leaf_peak) {
        out.weapon_source_leaf_peak = fanout;
        out.weapon_peak_owner = q;
        out.weapon_peak_tri = (wf - wr[0]) / (3 * VERT_FLOATS);
      }
      if (!source_leaf.n)
        out.census_missing += vmtrig_u64_add(&out.leaf_dropped, 1);
      out.census_missing += vmtrig_u64_add(
          &out.leaf_overflow, (uint64_t)(source_leaf.overflow != 0));
      out.census_missing += vmtrig_u64_add(&out.weapon_total, fanout);
      for (int l = 0; l < source_leaf.n; l++) {
        if (source_leaf.v[l].boundary)
          out.census_missing += vmtrig_u64_add(&out.weapon_boundary, 1);
        else if (source_leaf.v[l].allowed)
          out.census_missing += vmtrig_u64_add(&out.weapon_allowed, 1);
        else out.census_missing += vmtrig_u64_add(&out.weapon_forbidden, 1);
      }
      vmtrig_patch_leaves_release(&source_leaf);
    }
  }
  for (int i = 0; i < nh; i++) {
    if (hr[i].hand != hand) continue;
    const fig_contact_patch_t *patch = vmtrig_contact_patch(&hr[i], NULL);
    vmtrig_patch_leaves_t hleaf;
    vmtrig_patch_leaves_init(&hleaf, VMTRIG_PATCH_HAND);
    hleaf.patch = patch;
    hleaf.patch_mode = VMTRIG_PATCH_HAND;
    hleaf.patch_candidate = patch && strcmp(patch->name, "trigger_digit");
    int hfloats = hr[i].r[1] - hr[i].r[0];
    if (hfloats <= 0 || hfloats % (3 * VERT_FLOATS)) out.census_missing++;
    else out.census_missing += vmtrig_u64_add(
        &expected_hand_sources, (uint64_t)(hfloats / (3 * VERT_FLOATS)));
    vmtrig_patch_range_add(&hleaf, hr[i].r);
    for (int hf = hr[i].r[0]; hf + 2 * VERT_FLOATS < hr[i].r[1];
         hf += 3 * VERT_FLOATS) {
      v3 ha = SCENE_V(hf), hbv = SCENE_V(hf + VERT_FLOATS);
      v3 hc = SCENE_V(hf + 2 * VERT_FLOATS);
      int before = hleaf.n;
      out.census_missing += vmtrig_u64_add(&out.hand_sources, 1);
      vmtrig_hand_patch_leaves(
          ha, hbv, hc, patch, patch ? patch->name : "",
          (hf - hr[i].r[0]) / (3 * VERT_FLOATS), &hleaf);
      for (int l = before; l < hleaf.n; l++) hleaf.v[l].owner = i;
      uint64_t fanout = (uint64_t)(hleaf.n - before);
      if (fanout > out.hand_source_leaf_peak) {
        out.hand_source_leaf_peak = fanout;
        out.hand_peak_owner = i;
        out.hand_peak_tri = (hf - hr[i].r[0]) / (3 * VERT_FLOATS);
      }
      if (hleaf.n == before)
        out.census_missing += vmtrig_u64_add(&out.leaf_dropped, 1);
    }
    out.census_missing += vmtrig_u64_add(&out.leaf_overflow,
                                          (uint64_t)(hleaf.overflow != 0));
    for (int h = 0; h < hleaf.n; h++) {
      if (hleaf.v[h].owner != i) continue;
      out.census_missing += vmtrig_u64_add(&out.hand_total, 1);
      if (hleaf.v[h].boundary) {
        out.census_missing += vmtrig_u64_add(&out.hand_boundary, 1);
        continue;
      }
      if (hleaf.v[h].allowed) {
        out.census_missing += vmtrig_u64_add(&out.hand_allowed, 1);
        if (hleaf.v[h].normal_cos < out.min_allowed_normal)
          out.min_allowed_normal = hleaf.v[h].normal_cos;
      } else out.census_missing += vmtrig_u64_add(&out.hand_forbidden, 1);
    }
    vmtrig_leaf_bvh_build(&hleaf);
    out.census_missing += vmtrig_u64_add(&out.leaf_overflow,
                                          (uint64_t)(hleaf.overflow != 0));
    vmtrig_aabb_t hb = hleaf.range_box[0];
    for (int r = 1; r < hleaf.nranges; r++)
      for (int k = 0; k < 3; k++) {
        hb.lo.e[k] = fminf(hb.lo.e[k], hleaf.range_box[r].lo.e[k]);
        hb.hi.e[k] = fmaxf(hb.hi.e[k], hleaf.range_box[r].hi.e[k]);
      }

    for (int q = 0; q < set->count; q++) {
      int wr[2] = {set->range[q].v0, set->range[q].v1};
      vmtrig_patch_leaves_t wleaf;
      vmtrig_patch_leaves_init(&wleaf, VMTRIG_PATCH_WEAPON);
      wleaf.patch = patch;
      wleaf.patch_mode = VMTRIG_PATCH_WEAPON;
      wleaf.patch_candidate = set->range[q].surface && patch &&
                              strcmp(patch->name, "trigger_digit");
      wleaf.mesh_set = set;
      wleaf.mesh_range = q;
      vmtrig_patch_range_add(&wleaf, wr);
      for (int wf = wr[0]; wf + 2 * VERT_FLOATS < wr[1];
           wf += 3 * VERT_FLOATS) {
        v3 wa = SCENE_V(wf), wbv = SCENE_V(wf + VERT_FLOATS);
        v3 wc = SCENE_V(wf + 2 * VERT_FLOATS);
        int before = wleaf.n;
        vmtrig_weapon_patch_leaves(
            wa, wbv, wc, patch, set->range[q].surface,
            (wf - wr[0]) / (3 * VERT_FLOATS), &wleaf);
        for (int l = before; l < wleaf.n; l++) wleaf.v[l].owner = q;
      }
      out.census_missing += vmtrig_u64_add(&out.leaf_overflow,
                                            (uint64_t)(wleaf.overflow != 0));
      vmtrig_leaf_bvh_build(&wleaf);
      out.census_missing += vmtrig_u64_add(&out.leaf_overflow,
                                            (uint64_t)(wleaf.overflow != 0));
      vmtrig_aabb_t wb = wleaf.nranges > 0 ? wleaf.range_box[0]
                                           : (vmtrig_aabb_t){0};
      for (int h = 0; h < hleaf.n; h++) {
        const vmtrig_patch_leaf_t *hl = &hleaf.v[h];
        if (hl->owner != i ||
            !vmtrig_tri_box_overlap(hl->a, hl->b, hl->c, wb)) continue;
        vmtrig_wall_t depth = {0};
        vmtrig_range_patch_tri(
            hl->a, hl->b, hl->c, patch, VMTRIG_PATCH_HAND,
            hleaf.patch_candidate, &wleaf, set->range[q].surface, NULL, -1,
            NULL, &depth, work, hl->source);
        float proved = fmaxf(depth.seen, depth.certified);
        out.contact_boundary_area += depth.boundary_area;
        if (depth.boundary_upper > out.contact_boundary_upper)
          out.contact_boundary_upper = depth.boundary_upper;
        out.complement_fail |= depth.fail;
        out.same_child_fail |= depth.allowed_fail;
        if (depth.allowed_max > out.same_child_max)
          out.same_child_max = depth.allowed_max;
        if (proved > out.hand_max) {
          out.hand_max = proved; out.hand_part = hr[i].name;
          out.hand_seen = depth.seen; out.hand_certified = depth.certified;
          out.hand_child = i; out.hand_tri = depth.source_tri;
          out.hand_range = q; out.hand_at = depth.at;
          out.hand_contact_kind = kind;
          out.hand_score = fmaxf(
              vmtrig_patch_score(hl->a, patch, 0.0f, NULL, NULL),
              fmaxf(vmtrig_patch_score(hl->b, patch, 0.0f, NULL, NULL),
                    vmtrig_patch_score(hl->c, patch, 0.0f, NULL, NULL)));
          out.hand_normal = hl->normal_cos;
        }
      }

      int boundary_before = out.boundary_cross;
      int range_cross = vmtrig_leaf_cross(&hleaf, &wleaf, i,
                                           &out.boundary_cross, work);
      out.cross += range_cross;
      if (g_fchk_v && (range_cross || out.boundary_cross > boundary_before))
        fprintf(stderr,
                "vmtrig-cross hand=%d child=%d:%s kind=%d q=%d surf=%d "
                "hard=%d boundary=%d\n",
                hand, i, hr[i].name, kind, q, set->range[q].surface,
                range_cross, out.boundary_cross - boundary_before);
      for (int w = 0; w < wleaf.n; w++) {
        const vmtrig_patch_leaf_t *wl = &wleaf.v[w];
        if (!vmtrig_tri_box_overlap(wl->a, wl->b, wl->c, hb))
          continue;
        vmtrig_wall_t depth = {0};
        vmtrig_range_patch_tri(
            wl->a, wl->b, wl->c, patch, VMTRIG_PATCH_WEAPON,
            wleaf.patch_candidate, &hleaf, 1, set, q, NULL, &depth, work,
            wl->source);
        out.census_missing += vmtrig_u64_add(
            &out.host_buried_cells, depth.host_buried);
        out.census_missing += vmtrig_u64_add(
            &out.host_straddle_cells, depth.host_straddle);
        out.census_missing += vmtrig_u64_add(
            &out.host_terminal_cells, depth.host_terminal);
        float proved = fmaxf(depth.seen, depth.certified);
        out.contact_boundary_area += depth.boundary_area;
        if (depth.boundary_upper > out.contact_boundary_upper)
          out.contact_boundary_upper = depth.boundary_upper;
        out.complement_fail |= depth.fail;
        out.same_child_fail |= depth.allowed_fail;
        if (depth.allowed_max > out.same_child_max)
          out.same_child_max = depth.allowed_max;
        if (proved > out.weapon_max) {
          out.weapon_max = proved; out.weapon_part = hr[i].name;
          out.weapon_seen = depth.seen; out.weapon_certified = depth.certified;
          out.weapon_child = i; out.weapon_tri = depth.source_tri;
          out.weapon_range = q; out.weapon_at = depth.at;
          out.weapon_contact_kind = kind;
          out.weapon_score = fmaxf(
              vmtrig_patch_score(wl->a, patch, 0.0f, NULL, NULL),
              fmaxf(vmtrig_patch_score(wl->b, patch, 0.0f, NULL, NULL),
                    vmtrig_patch_score(wl->c, patch, 0.0f, NULL, NULL)));
        }
      }
      vmtrig_patch_leaves_release(&wleaf);
    }

    // One physical hand-area cell, one Bite contribution. Per-range parity supplies
    // membership and exposed closest-point witnesses select the outer owner; hosted
    // backs and coincident relief ranges therefore cannot duplicate the denominator.
    double bite_inside_before = bite.inside_area;
    double bite_boundary_before = bite.boundary_area;
    float bite_max_before = bite.max_upper;
    for (int h = 0; h < hleaf.n; h++) {
      const vmtrig_patch_leaf_t *hl = &hleaf.v[h];
      if (hl->owner != i) continue;
      int exact_handled = vmtrig_exact_cert_bite_tri(
            hl->a, hl->b, hl->c, patch, kind, set, range_box,
            &bite, &exact_cert, work);
      if (!exact_handled && (hl->allowed || hl->boundary))
        vmtrig_range_bite_owner_tri(hl->a, hl->b, hl->c, set, range_box,
                                     hl->boundary, &bite, work);
    }
    if (g_fchk_v && (bite.inside_area > bite_inside_before ||
                     bite.boundary_area > bite_boundary_before ||
                     bite.max_upper > bite_max_before))
      fprintf(stderr,
              "vmtrig-bite hand=%d child=%d:%s kind=%d area=%.2f/%.2f "
              "max=%.1f->%.1f fail=%d\n",
              hand, i, hr[i].name, kind,
              (bite.inside_area - bite_inside_before) * 1e6,
              (bite.boundary_area - bite_boundary_before) * 1e6,
              (double)(bite_max_before * 1000.0f),
              (double)(bite.max_upper * 1000.0f), bite.fail);
    vmtrig_patch_leaves_release(&hleaf);
  }
  out.bite_fail = vmtrig_bite_reduce(
      &bite, &out.mean_lo, &out.mean_hi, &out.bite_max_upper,
      &out.bite_boundary_area);
  out.mean = 0.5f * (out.mean_lo + out.mean_hi);
  out.complement_max = fmaxf(out.hand_max, out.weapon_max);
  out.census_missing += out.hand_total <= 0 || out.hand_allowed +
                        out.hand_forbidden + out.hand_buried + out.hand_seam +
                        out.hand_boundary != out.hand_total;
  out.census_missing += out.weapon_total <= 0 || out.weapon_allowed +
                        out.weapon_forbidden + out.weapon_buried +
                        out.weapon_seam + out.weapon_boundary != out.weapon_total;
  out.census_missing += out.ranges != (uint64_t)set->count;
  out.census_missing += out.hand_sources != expected_hand_sources ||
                        out.weapon_sources != expected_weapon_sources;
  out.census_missing += out.leaf_overflow != 0 || out.leaf_dropped != 0;
  out.boundary_fail = vmtrig_boundary_gate(&out);
  return out;
}

static int vmtrig_int_sat_add(int a, int b) {
  return b > 0 && a > INT_MAX - b ? INT_MAX : a + b;
}

// A transition renders one hand against two independently closed held classes. Both
// classes retain their complete patch/complement/cross/census proof; this reducer only
// folds their diagnostics after those proofs have run.
static void vmtrig_allowed_mesh_merge(vmtrig_allowed_mesh_t *dst,
                                      const vmtrig_allowed_mesh_t *src) {
  int counter_overflow = 0;
#define VMTRIG_MERGE_U64(FIELD) \
  do { counter_overflow |= vmtrig_u64_add(&dst->FIELD, src->FIELD); } while (0)
  if (src->hand_max > dst->hand_max) {
    dst->hand_max = src->hand_max;
    dst->hand_seen = src->hand_seen;
    dst->hand_certified = src->hand_certified;
    dst->hand_part = src->hand_part;
    dst->hand_child = src->hand_child;
    dst->hand_tri = src->hand_tri;
    dst->hand_range = src->hand_range;
    dst->hand_contact_kind = src->hand_contact_kind;
    dst->hand_score = src->hand_score;
    dst->hand_facing = src->hand_facing;
    dst->hand_normal = src->hand_normal;
    dst->hand_at = src->hand_at;
  }
  if (src->weapon_max > dst->weapon_max) {
    dst->weapon_max = src->weapon_max;
    dst->weapon_seen = src->weapon_seen;
    dst->weapon_certified = src->weapon_certified;
    dst->weapon_part = src->weapon_part;
    dst->weapon_child = src->weapon_child;
    dst->weapon_tri = src->weapon_tri;
    dst->weapon_range = src->weapon_range;
    dst->weapon_contact_kind = src->weapon_contact_kind;
    dst->weapon_score = src->weapon_score;
    dst->weapon_at = src->weapon_at;
  }
  dst->mean_lo = fminf(dst->mean_lo, src->mean_lo);
  dst->mean_hi = fmaxf(dst->mean_hi, src->mean_hi);
  dst->mean = 0.5f * (dst->mean_lo + dst->mean_hi);
  dst->bite_max_upper = fmaxf(dst->bite_max_upper, src->bite_max_upper);
  dst->same_child_max = fmaxf(dst->same_child_max, src->same_child_max);
  dst->complement_max = fmaxf(dst->complement_max, src->complement_max);
  dst->min_allowed_normal = fminf(dst->min_allowed_normal,
                                   src->min_allowed_normal);
  dst->bite_boundary_area += src->bite_boundary_area;
  dst->contact_boundary_area += src->contact_boundary_area;
  dst->contact_boundary_upper = fmaxf(dst->contact_boundary_upper,
                                       src->contact_boundary_upper);
  dst->cross = vmtrig_int_sat_add(dst->cross, src->cross);
  dst->boundary_cross = vmtrig_int_sat_add(dst->boundary_cross,
                                            src->boundary_cross);
  dst->seam_cross = vmtrig_int_sat_add(dst->seam_cross, src->seam_cross);
  dst->complement_fail |= src->complement_fail;
  dst->bite_fail |= src->bite_fail;
  dst->same_child_fail |= src->same_child_fail;
  dst->boundary_fail |= src->boundary_fail;
  VMTRIG_MERGE_U64(hand_allowed);
  VMTRIG_MERGE_U64(hand_forbidden);
  VMTRIG_MERGE_U64(weapon_allowed);
  VMTRIG_MERGE_U64(weapon_forbidden);
  VMTRIG_MERGE_U64(hand_total);
  VMTRIG_MERGE_U64(weapon_total);
  VMTRIG_MERGE_U64(hand_buried);
  VMTRIG_MERGE_U64(weapon_buried);
  VMTRIG_MERGE_U64(hand_seam);
  VMTRIG_MERGE_U64(weapon_seam);
  VMTRIG_MERGE_U64(hand_unresolved);
  VMTRIG_MERGE_U64(weapon_unresolved);
  VMTRIG_MERGE_U64(hand_boundary);
  VMTRIG_MERGE_U64(weapon_boundary);
  VMTRIG_MERGE_U64(hand_sources);
  VMTRIG_MERGE_U64(weapon_sources);
  VMTRIG_MERGE_U64(ranges);
  VMTRIG_MERGE_U64(census_overlap);
  VMTRIG_MERGE_U64(leaf_overflow);
  VMTRIG_MERGE_U64(leaf_dropped);
  VMTRIG_MERGE_U64(host_buried_cells);
  VMTRIG_MERGE_U64(host_straddle_cells);
  VMTRIG_MERGE_U64(host_terminal_cells);
  counter_overflow |= vmtrig_u64_add(&dst->census_missing,
                                      src->census_missing);
  if (counter_overflow) (void)vmtrig_u64_add(&dst->census_missing, 1);
  if (src->hand_source_leaf_peak > dst->hand_source_leaf_peak) {
    dst->hand_source_leaf_peak = src->hand_source_leaf_peak;
    dst->hand_peak_owner = src->hand_peak_owner;
    dst->hand_peak_tri = src->hand_peak_tri;
  }
  if (src->weapon_source_leaf_peak > dst->weapon_source_leaf_peak) {
    dst->weapon_source_leaf_peak = src->weapon_source_leaf_peak;
    dst->weapon_peak_owner = src->weapon_peak_owner;
    dst->weapon_peak_tri = src->weapon_peak_tri;
  }
#undef VMTRIG_MERGE_U64
}

static vmtrig_allowed_mesh_t vmtrig_allowed_contact_mesh(
    uint32_t mask, int primary, int hand, const vmtrig_hand_range_t *hr,
    int nh, vmtrig_work_t *work) {
  vmtrig_allowed_mesh_t out = {0};
  int have = 0;
  if (primary >= GUN_CONTACT_GRIP && primary <= GUN_CONTACT_BOLT &&
      (mask & (1u << primary))) {
    out = vmtrig_allowed_mesh((gun_contact_kind_t)primary,
                               hand, hr, nh, work);
    have = 1;
  }
  for (int kind = GUN_CONTACT_GRIP; kind <= GUN_CONTACT_BOLT; kind++) {
    if (kind == primary || !(mask & (1u << kind))) continue;
    vmtrig_allowed_mesh_t next = vmtrig_allowed_mesh(
        (gun_contact_kind_t)kind, hand, hr, nh, work);
    if (!have) { out = next; have = 1; }
    else vmtrig_allowed_mesh_merge(&out, &next);
  }
  return out;
}

static int vmtrig_boundary_merge_selftest(void) {
  vmtrig_allowed_mesh_t uncovered = {
    .boundary_cross = 1,
    .min_allowed_normal = 1.0f,
  };
  vmtrig_allowed_mesh_t unrelated = {
    .contact_boundary_area = 1.0,
    .min_allowed_normal = 1.0f,
  };
  uncovered.boundary_fail = vmtrig_boundary_gate(&uncovered);
  unrelated.boundary_fail = vmtrig_boundary_gate(&unrelated);
  vmtrig_allowed_mesh_merge(&uncovered, &unrelated);
  // The post-merge geometric expression is deliberately false: the unrelated area
  // can cover the syntactic `area == 0` test. The latched per-mesh failure must live.
  return !uncovered.boundary_fail || vmtrig_boundary_gate(&uncovered);
}

static void vmtrig_proxy_vertices(const gun_contact_proxy_t *b, v3 v[8]) {
  for (int i = 0; i < 8; i++)
    v[i] = v3_add(b->c, v3_add(v3_scale(b->e[0], (i & 1) ? 1.0f : -1.0f),
           v3_add(v3_scale(b->e[1], (i & 2) ? 1.0f : -1.0f),
                  v3_scale(b->e[2], (i & 4) ? 1.0f : -1.0f))));
}

static int vmtrig_proxy_cross(const gun_contact_proxy_t *b, const int hr[2]) {
  static const unsigned char TRI[12][3] = {
    {0,2,6},{0,6,4}, {1,5,7},{1,7,3},
    {0,4,5},{0,5,1}, {2,3,7},{2,7,6},
    {0,1,3},{0,3,2}, {4,6,7},{4,7,5},
  };
  static const unsigned char EDGE[12][2] = {
    {0,1},{0,2},{0,4},{1,3},{1,5},{2,3},
    {2,6},{3,7},{4,5},{4,6},{5,7},{6,7},
  };
  v3 pv[8];
  vmtrig_proxy_vertices(b, pv);
  int cross = 0;
  for (int f = hr[0]; f + 2 * VERT_FLOATS < hr[1]; f += 3 * VERT_FLOATS) {
    v3 h[3] = {SCENE_V(f), SCENE_V(f + VERT_FLOATS),
               SCENE_V(f + 2 * VERT_FLOATS)};
    for (int e = 0; e < 12; e++)
      cross += vmtrig_segment_tri_cross_at(
          pv[EDGE[e][0]], pv[EDGE[e][1]], h[0], h[1], h[2], NULL);
    for (int e = 0; e < 3; e++)
      for (int t = 0; t < 12; t++)
        cross += vmtrig_segment_tri_cross_at(
            h[e], h[(e + 1) % 3], pv[TRI[t][0]], pv[TRI[t][1]],
            pv[TRI[t][2]], NULL);
    if (cross > 64) return cross;
  }
  return cross;
}

static vmtrig_aabb_t vmtrig_range_aabb(const int r[2]) {
  vmtrig_aabb_t b = {{{1e9f, 1e9f, 1e9f}}, {{-1e9f, -1e9f, -1e9f}}};
  for (int f = r[0]; f + VERT_FLOATS <= r[1]; f += VERT_FLOATS) {
    v3 p = SCENE_V(f);
    for (int k = 0; k < 3; k++) {
      if (p.e[k] < b.lo.e[k]) b.lo.e[k] = p.e[k];
      if (p.e[k] > b.hi.e[k]) b.hi.e[k] = p.e[k];
    }
  }
  return b;
}

static int vmtrig_aabb_overlap(vmtrig_aabb_t a, vmtrig_aabb_t b) {
  for (int k = 0; k < 3; k++)
    if (a.hi.e[k] < b.lo.e[k] || b.hi.e[k] < a.lo.e[k]) return 0;
  return 1;
}

#define VMTRIG_TOPO_EDGE_CAP 16384
typedef struct { uint32_t e[3]; } vmtrig_topo_vertex_t;
typedef struct {
  vmtrig_topo_vertex_t lo, hi;
  int dir;
} vmtrig_topo_edge_t;

static vmtrig_topo_vertex_t vmtrig_topo_vertex(const float *v) {
  vmtrig_topo_vertex_t out;
  for (int k = 0; k < 3; k++) {
    memcpy(&out.e[k], &v[k], sizeof out.e[k]);
    if (out.e[k] == UINT32_C(0x80000000)) out.e[k] = 0;
  }
  return out;
}

static int vmtrig_topo_vertex_cmp(vmtrig_topo_vertex_t a,
                                  vmtrig_topo_vertex_t b) {
  for (int k = 0; k < 3; k++)
    if (a.e[k] != b.e[k]) return a.e[k] < b.e[k] ? -1 : 1;
  return 0;
}

static int vmtrig_topo_edge_cmp(const void *av, const void *bv) {
  const vmtrig_topo_edge_t *a = av, *b = bv;
  int c = vmtrig_topo_vertex_cmp(a->lo, b->lo);
  return c ? c : vmtrig_topo_vertex_cmp(a->hi, b->hi);
}

static int vmtrig_range_topology(const int range[2], vmtrig_work_t *work) {
  static vmtrig_topo_edge_t edge[VMTRIG_TOPO_EDGE_CAP];
  int ne = 0, fail = 0;
  if (range[0] < 0 || range[1] <= range[0] ||
      (range[1] - range[0]) % (3 * VERT_FLOATS)) return 1;
  for (int f = range[0]; f + 2 * VERT_FLOATS < range[1];
       f += 3 * VERT_FLOATS) {
    const float *tri = &g_scene[f];
    v3 a = SCENE_V(f), b = SCENE_V(f + VERT_FLOATS);
    v3 c = SCENE_V(f + 2 * VERT_FLOATS);
    if (vmtrig_v3_nonfinite(a) || vmtrig_v3_nonfinite(b) ||
        vmtrig_v3_nonfinite(c)) {
      fail++;
      vmtrig_work_add(work, 3);
      continue;
    }
    if (v3_len(v3_cross(v3_sub(b, a), v3_sub(c, a))) < 1e-9f) fail++;
    if (ne + 3 > VMTRIG_TOPO_EDGE_CAP) return fail + 1;
    vmtrig_topo_vertex_t key[3] = {
      vmtrig_topo_vertex(&tri[0]),
      vmtrig_topo_vertex(&tri[VERT_FLOATS]),
      vmtrig_topo_vertex(&tri[2 * VERT_FLOATS]),
    };
    for (int e = 0; e < 3; e++) {
      vmtrig_topo_vertex_t ka = key[e], kb = key[(e + 1) % 3];
      int cmp = vmtrig_topo_vertex_cmp(ka, kb);
      edge[ne++] = (vmtrig_topo_edge_t){cmp < 0 ? ka : kb,
                                        cmp < 0 ? kb : ka,
                                        cmp < 0 ? 1 : -1};
    }
    vmtrig_work_add(work, 3);
  }
  qsort(edge, (size_t)ne, sizeof *edge, vmtrig_topo_edge_cmp);
  for (int i = 0; i < ne;) {
    int j = i, direction = 0;
    while (j < ne && !vmtrig_topo_edge_cmp(&edge[i], &edge[j]))
      direction += edge[j++].dir;
    fail += j - i != 2 || direction != 0;
    i = j;
  }
  return fail;
}

// -----------------------------------------------------------------------------
// FIRE-F1C exact feature contract.  This is independent of the Phase-A BODY tag:
// an absent descriptor does nothing, while any present but malformed descriptor is
// a hard failure.  All dimensional constants below are the single frozen F1C form.

#define VMTRIG_FIRE_F1C_PROFILE_EPS 0.000004f
#define VMTRIG_FIRE_F1C_BACKING_MIN 0.0045f
#define VMTRIG_FIRE_F1C_FOREIGN_R   0.00015f
#define VMTRIG_FIRE_F1C_TOPO_V_CAP 768
#define VMTRIG_FIRE_F1C_TOPO_E_CAP 1024
#define VMTRIG_FIRE_F1C_WORK_LIMIT UINT64_C(320000000)
#define VMTRIG_FIRE_F1C_TRIANGLE_CAP UINT64_C(36)

// A FIRE census is a separately authored, compile-time description of one exact
// weapon/tier/state build.  It is intentionally not populated from the live capture:
// omitting a begin/add call therefore changes a count/ordinal and cannot shrink the
// .150-mm foreign union silently.  Product census rows are added only with their
// corresponding emitter; Step 1 owns this closed selftest row alone.
#define VMTRIG_FIRE_F1C_FOREIGN_CENSUS_SELFTEST 1
#define VMTRIG_FIRE_F1C_FOREIGN_CENSUS_MAGIC UINT32_C(0xf1cce451)
enum {
  VMTRIG_FIRE_F1C_GEOMETRY_Q0 = 1,
  VMTRIG_FIRE_F1C_GEOMETRY_PANEL,
  VMTRIG_FIRE_F1C_GEOMETRY_BOX,
};
typedef struct {
  int sources, surface;
  uint32_t part, host;
  int geometry_identity;
  v3 geometry_origin, geometry_axis[3];
} vmtrig_fire_f1c_foreign_range_t;
typedef struct {
  uint32_t magic;
  int weapon, build_id;
  int count[GUN_CONTACT_COUNT];
  vmtrig_fire_f1c_foreign_range_t
    range[GUN_CONTACT_COUNT][GUN_CONTACT_MESH_MAX];
  v3 guard[4], blade[3];
  float guard_r, blade_r;
} vmtrig_fire_f1c_foreign_census_t;

#define VMTRIG_FIRE_F1C_IDENTITY_AXES \
  {{{1, 0, 0}}, {{0, 1, 0}}, {{0, 0, 1}}}
#define VMTRIG_FIRE_F1C_SELFTEST_RANGE(PART, HOST, X) \
  {.sources = 12, .surface = 1, .part = (PART), .host = (HOST), \
   .geometry_identity = VMTRIG_FIRE_F1C_GEOMETRY_BOX, \
   .geometry_origin = {{(X), 0.625f, 0.5f}}, \
   .geometry_axis = VMTRIG_FIRE_F1C_IDENTITY_AXES}
static const vmtrig_fire_f1c_foreign_census_t
vmtrig_fire_f1c_foreign_census_selftest = {
  .magic = VMTRIG_FIRE_F1C_FOREIGN_CENSUS_MAGIC,
  .weapon = WPN_AR, .build_id = VMTRIG_FIRE_F1C_FOREIGN_CENSUS_SELFTEST,
  .count = {4, 1, 1, 1, 1, 1, 1, 1},
  .range = {
    [GUN_CONTACT_GRIP] = {
      {.sources = 120, .surface = 1, .part = GUN_MESH_PART_GRIP_BODY,
       .geometry_identity = VMTRIG_FIRE_F1C_GEOMETRY_Q0,
       .geometry_axis = VMTRIG_FIRE_F1C_IDENTITY_AXES},
      {.sources = 44, .surface = 1, .part = GUN_MESH_PART_GRIP_PANEL,
       .host = GUN_MESH_PART_GRIP_BODY,
       .geometry_identity = VMTRIG_FIRE_F1C_GEOMETRY_PANEL,
       .geometry_origin = {{0.0f, -0.022f, 0.020f}},
       .geometry_axis = {{{0, -1, 0}}, {{1, 0, 0}}, {{0, 0, 1}}}},
      {.sources = 44, .surface = 1, .part = GUN_MESH_PART_GRIP_PANEL,
       .host = GUN_MESH_PART_GRIP_BODY,
       .geometry_identity = VMTRIG_FIRE_F1C_GEOMETRY_PANEL,
       .geometry_origin = {{0.040f, -0.022f, 0.020f}},
       .geometry_axis = {{{0, -1, 0}}, {{1, 0, 0}}, {{0, 0, 1}}}},
      {.sources = 44, .surface = 1, .part = GUN_MESH_PART_GRIP_PANEL,
       .host = GUN_MESH_PART_GRIP_BODY,
       .geometry_identity = VMTRIG_FIRE_F1C_GEOMETRY_PANEL,
       .geometry_origin = {{0.080f, -0.022f, 0.020f}},
       .geometry_axis = {{{0, -1, 0}}, {{1, 0, 0}}, {{0, 0, 1}}}},
    },
    [GUN_CONTACT_FORE] = {
      VMTRIG_FIRE_F1C_SELFTEST_RANGE(
        GUN_MESH_PART_FORE_BODY, 0, 0.53125f),
    },
    [GUN_CONTACT_MAG] = {
      VMTRIG_FIRE_F1C_SELFTEST_RANGE(
        GUN_MESH_PART_MAG_BODY, 0, 0.5625f),
    },
    [GUN_CONTACT_BOLT] = {
      VMTRIG_FIRE_F1C_SELFTEST_RANGE(0, 0, 0.59375f)},
    [GUN_CONTACT_RECEIVER] = {
      VMTRIG_FIRE_F1C_SELFTEST_RANGE(0, 0, 0.625f)},
    [GUN_CONTACT_SCOPE] = {
      VMTRIG_FIRE_F1C_SELFTEST_RANGE(0, 0, 0.65625f)},
    [GUN_CONTACT_STOCK] = {
      VMTRIG_FIRE_F1C_SELFTEST_RANGE(0, 0, 0.6875f)},
    [GUN_CONTACT_CHASSIS] = {
      VMTRIG_FIRE_F1C_SELFTEST_RANGE(0, 0, 0.71875f)},
  },
  .guard = {{{0.75f, 0.75f, 0.75f}},
            {{0.75f, 0.765625f, 0.75f}},
            {{0.75f, 0.78125f, 0.75f}},
            {{0.75f, 0.796875f, 0.75f}}},
  .blade = {{{0.875f, 0.875f, 0.875f}},
            {{0.875f, 0.890625f, 0.875f}},
            {{0.875f, 0.90625f, 0.875f}}},
  .guard_r = 0.0009765625f, .blade_r = 0.0009765625f,
};
#undef VMTRIG_FIRE_F1C_SELFTEST_RANGE
#undef VMTRIG_FIRE_F1C_IDENTITY_AXES

// Ordered local-frame vertices from /tmp/fire-f1c-portable-reference.py.  The
// independent high-precision model reads no Scene capture or binary.  Integer
// nanometres make this one authored geometry identity on every compiler; the
// validator below admits only a 0.5-micrometre Euclidean reference ball.
static const int32_t vmtrig_fire_f1c_q0_reference_nm[360][3] = {
  {12218962, -5176381, -40000000},
  {8944901, -14142136, -40000000},
  {8566168, -13543348, -40700000},
  {12218962, -5176381, -40000000},
  {8566168, -13543348, -40700000},
  {11701603, -4957210, -40700000},
  {8944901, -14142136, -40000000},
  {3274061, -19318517, -40000000},
  {3135435, -18500558, -40700000},
  {8944901, -14142136, -40000000},
  {3135435, -18500558, -40700000},
  {8566168, -13543348, -40700000},
  {3274061, -19318517, -40000000},
  {-3274061, -19318517, -40000000},
  {-3135435, -18500558, -40700000},
  {3274061, -19318517, -40000000},
  {-3135435, -18500558, -40700000},
  {3135435, -18500558, -40700000},
  {-3274061, -19318517, -40000000},
  {-8944901, -14142136, -40000000},
  {-8566168, -13543348, -40700000},
  {-3274061, -19318517, -40000000},
  {-8566168, -13543348, -40700000},
  {-3135435, -18500558, -40700000},
  {-8944901, -14142136, -40000000},
  {-12218962, -5176381, -40000000},
  {-11701603, -4957210, -40700000},
  {-8944901, -14142136, -40000000},
  {-11701603, -4957210, -40700000},
  {-8566168, -13543348, -40700000},
  {-12218962, -5176381, -40000000},
  {-12218962, 5176381, -40000000},
  {-11701603, 4957209, -40700000},
  {-12218962, -5176381, -40000000},
  {-11701603, 4957209, -40700000},
  {-11701603, -4957210, -40700000},
  {-12218962, 5176381, -40000000},
  {-8944901, 14142136, -40000000},
  {-8566168, 13543348, -40700000},
  {-12218962, 5176381, -40000000},
  {-8566168, 13543348, -40700000},
  {-11701603, 4957209, -40700000},
  {-8944901, 14142136, -40000000},
  {-3274061, 19318516, -40000000},
  {-3135435, 18500558, -40700000},
  {-8944901, 14142136, -40000000},
  {-3135435, 18500558, -40700000},
  {-8566168, 13543348, -40700000},
  {-3274061, 19318516, -40000000},
  {3274061, 19318517, -40000000},
  {3135435, 18500558, -40700000},
  {-3274061, 19318516, -40000000},
  {3135435, 18500558, -40700000},
  {-3135435, 18500558, -40700000},
  {3274061, 19318517, -40000000},
  {8944901, 14142136, -40000000},
  {8566168, 13543348, -40700000},
  {3274061, 19318517, -40000000},
  {8566168, 13543348, -40700000},
  {3135435, 18500558, -40700000},
  {8944901, 14142136, -40000000},
  {12218962, 5176381, -40000000},
  {11701603, 4957210, -40700000},
  {8944901, 14142136, -40000000},
  {11701603, 4957210, -40700000},
  {8566168, 13543348, -40700000},
  {12218962, 5176381, -40000000},
  {12218962, -5176381, -40000000},
  {11701603, -4957210, -40700000},
  {12218962, 5176381, -40000000},
  {11701603, -4957210, -40700000},
  {11701603, 4957210, -40700000},
  {0, 0, -40700000},
  {11701603, -4957210, -40700000},
  {8566168, -13543348, -40700000},
  {0, 0, -40700000},
  {8566168, -13543348, -40700000},
  {3135435, -18500558, -40700000},
  {0, 0, -40700000},
  {3135435, -18500558, -40700000},
  {-3135435, -18500558, -40700000},
  {0, 0, -40700000},
  {-3135435, -18500558, -40700000},
  {-8566168, -13543348, -40700000},
  {0, 0, -40700000},
  {-8566168, -13543348, -40700000},
  {-11701603, -4957210, -40700000},
  {0, 0, -40700000},
  {-11701603, -4957210, -40700000},
  {-11701603, 4957209, -40700000},
  {0, 0, -40700000},
  {-11701603, 4957209, -40700000},
  {-8566168, 13543348, -40700000},
  {0, 0, -40700000},
  {-8566168, 13543348, -40700000},
  {-3135435, 18500558, -40700000},
  {0, 0, -40700000},
  {-3135435, 18500558, -40700000},
  {3135435, 18500558, -40700000},
  {0, 0, -40700000},
  {3135435, 18500558, -40700000},
  {8566168, 13543348, -40700000},
  {0, 0, -40700000},
  {8566168, 13543348, -40700000},
  {11701603, 4957210, -40700000},
  {0, 0, -40700000},
  {11701603, 4957210, -40700000},
  {11701603, -4957210, -40700000},
  {12218962, -5176381, -40000000},
  {12218962, -5176381, 0},
  {8944901, -14142136, 0},
  {12218962, -5176381, -40000000},
  {8944901, -14142136, 0},
  {8944901, -14142136, -40000000},
  {8944901, -14142136, -40000000},
  {8944901, -14142136, 0},
  {3274061, -19318517, 0},
  {8944901, -14142136, -40000000},
  {3274061, -19318517, 0},
  {3274061, -19318517, -40000000},
  {3274061, -19318517, -40000000},
  {3274061, -19318517, 0},
  {-3274061, -19318517, 0},
  {3274061, -19318517, -40000000},
  {-3274061, -19318517, 0},
  {-3274061, -19318517, -40000000},
  {-3274061, -19318517, -40000000},
  {-3274061, -19318517, 0},
  {-8944901, -14142136, 0},
  {-3274061, -19318517, -40000000},
  {-8944901, -14142136, 0},
  {-8944901, -14142136, -40000000},
  {-8944901, -14142136, -40000000},
  {-8944901, -14142136, 0},
  {-12218962, -5176381, 0},
  {-8944901, -14142136, -40000000},
  {-12218962, -5176381, 0},
  {-12218962, -5176381, -40000000},
  {-12218962, -5176381, -40000000},
  {-12218962, -5176381, 0},
  {-12218962, 5176381, 0},
  {-12218962, -5176381, -40000000},
  {-12218962, 5176381, 0},
  {-12218962, 5176381, -40000000},
  {-12218962, 5176381, -40000000},
  {-12218962, 5176381, 0},
  {-8944901, 14142136, 0},
  {-12218962, 5176381, -40000000},
  {-8944901, 14142136, 0},
  {-8944901, 14142136, -40000000},
  {-8944901, 14142136, -40000000},
  {-8944901, 14142136, 0},
  {-3274061, 19318516, 0},
  {-8944901, 14142136, -40000000},
  {-3274061, 19318516, 0},
  {-3274061, 19318516, -40000000},
  {-3274061, 19318516, -40000000},
  {-3274061, 19318516, 0},
  {3274061, 19318517, 0},
  {-3274061, 19318516, -40000000},
  {3274061, 19318517, 0},
  {3274061, 19318517, -40000000},
  {3274061, 19318517, -40000000},
  {3274061, 19318517, 0},
  {8944901, 14142136, 0},
  {3274061, 19318517, -40000000},
  {8944901, 14142136, 0},
  {8944901, 14142136, -40000000},
  {8944901, 14142136, -40000000},
  {8944901, 14142136, 0},
  {12218962, 5176381, 0},
  {8944901, 14142136, -40000000},
  {12218962, 5176381, 0},
  {12218962, 5176381, -40000000},
  {12218962, 5176381, -40000000},
  {12218962, 5176381, 0},
  {12218962, -5176381, 0},
  {12218962, 5176381, -40000000},
  {12218962, -5176381, 0},
  {12218962, -5176381, -40000000},
  {12218962, -5176381, 0},
  {12218962, -5176381, 40000000},
  {8944901, -14142136, 40000000},
  {12218962, -5176381, 0},
  {8944901, -14142136, 40000000},
  {8944901, -14142136, 0},
  {8944901, -14142136, 0},
  {8944901, -14142136, 40000000},
  {3274061, -19318517, 40000000},
  {8944901, -14142136, 0},
  {3274061, -19318517, 40000000},
  {3274061, -19318517, 0},
  {3274061, -19318517, 0},
  {3274061, -19318517, 40000000},
  {-3274061, -19318517, 40000000},
  {3274061, -19318517, 0},
  {-3274061, -19318517, 40000000},
  {-3274061, -19318517, 0},
  {-3274061, -19318517, 0},
  {-3274061, -19318517, 40000000},
  {-8944901, -14142136, 40000000},
  {-3274061, -19318517, 0},
  {-8944901, -14142136, 40000000},
  {-8944901, -14142136, 0},
  {-8944901, -14142136, 0},
  {-8944901, -14142136, 40000000},
  {-12218962, -5176381, 40000000},
  {-8944901, -14142136, 0},
  {-12218962, -5176381, 40000000},
  {-12218962, -5176381, 0},
  {-12218962, -5176381, 0},
  {-12218962, -5176381, 40000000},
  {-12218962, 5176381, 40000000},
  {-12218962, -5176381, 0},
  {-12218962, 5176381, 40000000},
  {-12218962, 5176381, 0},
  {-12218962, 5176381, 0},
  {-12218962, 5176381, 40000000},
  {-8944901, 14142136, 40000000},
  {-12218962, 5176381, 0},
  {-8944901, 14142136, 40000000},
  {-8944901, 14142136, 0},
  {-8944901, 14142136, 0},
  {-8944901, 14142136, 40000000},
  {-3274061, 19318516, 40000000},
  {-8944901, 14142136, 0},
  {-3274061, 19318516, 40000000},
  {-3274061, 19318516, 0},
  {-3274061, 19318516, 0},
  {-3274061, 19318516, 40000000},
  {3274061, 19318517, 40000000},
  {-3274061, 19318516, 0},
  {3274061, 19318517, 40000000},
  {3274061, 19318517, 0},
  {3274061, 19318517, 0},
  {3274061, 19318517, 40000000},
  {8944901, 14142136, 40000000},
  {3274061, 19318517, 0},
  {8944901, 14142136, 40000000},
  {8944901, 14142136, 0},
  {8944901, 14142136, 0},
  {8944901, 14142136, 40000000},
  {12218962, 5176381, 40000000},
  {8944901, 14142136, 0},
  {12218962, 5176381, 40000000},
  {12218962, 5176381, 0},
  {12218962, 5176381, 0},
  {12218962, 5176381, 40000000},
  {12218962, -5176381, 40000000},
  {12218962, 5176381, 0},
  {12218962, -5176381, 40000000},
  {12218962, -5176381, 0},
  {11701603, -4957210, 40700000},
  {8566168, -13543348, 40700000},
  {8944901, -14142136, 40000000},
  {11701603, -4957210, 40700000},
  {8944901, -14142136, 40000000},
  {12218962, -5176381, 40000000},
  {8566168, -13543348, 40700000},
  {3135435, -18500558, 40700000},
  {3274061, -19318517, 40000000},
  {8566168, -13543348, 40700000},
  {3274061, -19318517, 40000000},
  {8944901, -14142136, 40000000},
  {3135435, -18500558, 40700000},
  {-3135435, -18500558, 40700000},
  {-3274061, -19318517, 40000000},
  {3135435, -18500558, 40700000},
  {-3274061, -19318517, 40000000},
  {3274061, -19318517, 40000000},
  {-3135435, -18500558, 40700000},
  {-8566168, -13543348, 40700000},
  {-8944901, -14142136, 40000000},
  {-3135435, -18500558, 40700000},
  {-8944901, -14142136, 40000000},
  {-3274061, -19318517, 40000000},
  {-8566168, -13543348, 40700000},
  {-11701603, -4957210, 40700000},
  {-12218962, -5176381, 40000000},
  {-8566168, -13543348, 40700000},
  {-12218962, -5176381, 40000000},
  {-8944901, -14142136, 40000000},
  {-11701603, -4957210, 40700000},
  {-11701603, 4957209, 40700000},
  {-12218962, 5176381, 40000000},
  {-11701603, -4957210, 40700000},
  {-12218962, 5176381, 40000000},
  {-12218962, -5176381, 40000000},
  {-11701603, 4957209, 40700000},
  {-8566168, 13543348, 40700000},
  {-8944901, 14142136, 40000000},
  {-11701603, 4957209, 40700000},
  {-8944901, 14142136, 40000000},
  {-12218962, 5176381, 40000000},
  {-8566168, 13543348, 40700000},
  {-3135435, 18500558, 40700000},
  {-3274061, 19318516, 40000000},
  {-8566168, 13543348, 40700000},
  {-3274061, 19318516, 40000000},
  {-8944901, 14142136, 40000000},
  {-3135435, 18500558, 40700000},
  {3135435, 18500558, 40700000},
  {3274061, 19318517, 40000000},
  {-3135435, 18500558, 40700000},
  {3274061, 19318517, 40000000},
  {-3274061, 19318516, 40000000},
  {3135435, 18500558, 40700000},
  {8566168, 13543348, 40700000},
  {8944901, 14142136, 40000000},
  {3135435, 18500558, 40700000},
  {8944901, 14142136, 40000000},
  {3274061, 19318517, 40000000},
  {8566168, 13543348, 40700000},
  {11701603, 4957210, 40700000},
  {12218962, 5176381, 40000000},
  {8566168, 13543348, 40700000},
  {12218962, 5176381, 40000000},
  {8944901, 14142136, 40000000},
  {11701603, 4957210, 40700000},
  {11701603, -4957210, 40700000},
  {12218962, -5176381, 40000000},
  {11701603, 4957210, 40700000},
  {12218962, -5176381, 40000000},
  {12218962, 5176381, 40000000},
  {0, 0, 40700000},
  {8566168, -13543348, 40700000},
  {11701603, -4957210, 40700000},
  {0, 0, 40700000},
  {3135435, -18500558, 40700000},
  {8566168, -13543348, 40700000},
  {0, 0, 40700000},
  {-3135435, -18500558, 40700000},
  {3135435, -18500558, 40700000},
  {0, 0, 40700000},
  {-8566168, -13543348, 40700000},
  {-3135435, -18500558, 40700000},
  {0, 0, 40700000},
  {-11701603, -4957210, 40700000},
  {-8566168, -13543348, 40700000},
  {0, 0, 40700000},
  {-11701603, 4957209, 40700000},
  {-11701603, -4957210, 40700000},
  {0, 0, 40700000},
  {-8566168, 13543348, 40700000},
  {-11701603, 4957209, 40700000},
  {0, 0, 40700000},
  {-3135435, 18500558, 40700000},
  {-8566168, 13543348, 40700000},
  {0, 0, 40700000},
  {3135435, 18500558, 40700000},
  {-3135435, 18500558, 40700000},
  {0, 0, 40700000},
  {8566168, 13543348, 40700000},
  {3135435, 18500558, 40700000},
  {0, 0, 40700000},
  {11701603, 4957210, 40700000},
  {8566168, 13543348, 40700000},
  {0, 0, 40700000},
  {11701603, -4957210, 40700000},
  {11701603, 4957210, 40700000},
};
/* vmtrig_fire_f1c_q0_reference_nm: sources=120 sha256=50b88e957a0e483e517bd608a0c3dea57248c58c5eb4da1ecc7acf50627c41ef */
static const int32_t vmtrig_fire_f1c_panel_reference_nm[132][3] = {
  {-3000000, -6000000, 4500000},
  {-3000000, 6000000, 4500000},
  {-3000000, 6000000, -4500000},
  {-3000000, -6000000, 4500000},
  {-3000000, 6000000, -4500000},
  {-3000000, -6000000, -4500000},
  {-2300000, -6700000, -4500000},
  {2300000, -6700000, -4500000},
  {2300000, -6700000, 4500000},
  {-2300000, -6700000, -4500000},
  {2300000, -6700000, 4500000},
  {-2300000, -6700000, 4500000},
  {-2300000, 6000000, -5200000},
  {2300000, 6000000, -5200000},
  {2300000, -6000000, -5200000},
  {-2300000, 6000000, -5200000},
  {2300000, -6000000, -5200000},
  {-2300000, -6000000, -5200000},
  {3000000, -6000000, -4500000},
  {3000000, 6000000, -4500000},
  {3000000, 6000000, 4500000},
  {3000000, -6000000, -4500000},
  {3000000, 6000000, 4500000},
  {3000000, -6000000, 4500000},
  {-2300000, 6700000, 4500000},
  {2300000, 6700000, 4500000},
  {2300000, 6700000, -4500000},
  {-2300000, 6700000, 4500000},
  {2300000, 6700000, -4500000},
  {-2300000, 6700000, -4500000},
  {-2300000, -6000000, 5200000},
  {2300000, -6000000, 5200000},
  {2300000, 6000000, 5200000},
  {-2300000, -6000000, 5200000},
  {2300000, 6000000, 5200000},
  {-2300000, 6000000, 5200000},
  {-2300000, -6700000, -4500000},
  {-2300000, -6700000, 4500000},
  {-3000000, -6000000, 4500000},
  {-2300000, -6700000, -4500000},
  {-3000000, -6000000, 4500000},
  {-3000000, -6000000, -4500000},
  {-3000000, -6000000, -4500000},
  {-3000000, 6000000, -4500000},
  {-2300000, 6000000, -5200000},
  {-3000000, -6000000, -4500000},
  {-2300000, 6000000, -5200000},
  {-2300000, -6000000, -5200000},
  {-2300000, -6000000, -5200000},
  {2300000, -6000000, -5200000},
  {2300000, -6700000, -4500000},
  {-2300000, -6000000, -5200000},
  {2300000, -6700000, -4500000},
  {-2300000, -6700000, -4500000},
  {-3000000, 6000000, -4500000},
  {-3000000, 6000000, 4500000},
  {-2300000, 6700000, 4500000},
  {-3000000, 6000000, -4500000},
  {-2300000, 6700000, 4500000},
  {-2300000, 6700000, -4500000},
  {-2300000, -6000000, 5200000},
  {-2300000, 6000000, 5200000},
  {-3000000, 6000000, 4500000},
  {-2300000, -6000000, 5200000},
  {-3000000, 6000000, 4500000},
  {-3000000, -6000000, 4500000},
  {-2300000, -6700000, 4500000},
  {2300000, -6700000, 4500000},
  {2300000, -6000000, 5200000},
  {-2300000, -6700000, 4500000},
  {2300000, -6000000, 5200000},
  {-2300000, -6000000, 5200000},
  {3000000, -6000000, -4500000},
  {3000000, -6000000, 4500000},
  {2300000, -6700000, 4500000},
  {3000000, -6000000, -4500000},
  {2300000, -6700000, 4500000},
  {2300000, -6700000, -4500000},
  {2300000, -6000000, -5200000},
  {2300000, 6000000, -5200000},
  {3000000, 6000000, -4500000},
  {2300000, -6000000, -5200000},
  {3000000, 6000000, -4500000},
  {3000000, -6000000, -4500000},
  {-2300000, 6700000, -4500000},
  {2300000, 6700000, -4500000},
  {2300000, 6000000, -5200000},
  {-2300000, 6700000, -4500000},
  {2300000, 6000000, -5200000},
  {-2300000, 6000000, -5200000},
  {2300000, 6700000, -4500000},
  {2300000, 6700000, 4500000},
  {3000000, 6000000, 4500000},
  {2300000, 6700000, -4500000},
  {3000000, 6000000, 4500000},
  {3000000, 6000000, -4500000},
  {3000000, -6000000, 4500000},
  {3000000, 6000000, 4500000},
  {2300000, 6000000, 5200000},
  {3000000, -6000000, 4500000},
  {2300000, 6000000, 5200000},
  {2300000, -6000000, 5200000},
  {-2300000, 6000000, 5200000},
  {2300000, 6000000, 5200000},
  {2300000, 6700000, 4500000},
  {-2300000, 6000000, 5200000},
  {2300000, 6700000, 4500000},
  {-2300000, 6700000, 4500000},
  {-2300000, -6000000, -5200000},
  {-2300000, -6700000, -4500000},
  {-3000000, -6000000, -4500000},
  {3000000, -6000000, -4500000},
  {2300000, -6700000, -4500000},
  {2300000, -6000000, -5200000},
  {-3000000, 6000000, -4500000},
  {-2300000, 6700000, -4500000},
  {-2300000, 6000000, -5200000},
  {2300000, 6000000, -5200000},
  {2300000, 6700000, -4500000},
  {3000000, 6000000, -4500000},
  {-3000000, -6000000, 4500000},
  {-2300000, -6700000, 4500000},
  {-2300000, -6000000, 5200000},
  {2300000, -6000000, 5200000},
  {2300000, -6700000, 4500000},
  {3000000, -6000000, 4500000},
  {-2300000, 6000000, 5200000},
  {-2300000, 6700000, 4500000},
  {-3000000, 6000000, 4500000},
  {3000000, 6000000, 4500000},
  {2300000, 6700000, 4500000},
  {2300000, 6000000, 5200000},
};
/* vmtrig_fire_f1c_panel_reference_nm: sources=44 sha256=8d0416a63159eecc9b4f515b9866727f791fa0ba747ef5bf01f052fae3a3898a */
static const int32_t vmtrig_fire_f1c_box_reference_nm[36][3] = {
  {976563, -976563, -976563},
  {976563, 976563, -976563},
  {976563, 976563, 976563},
  {976563, -976563, -976563},
  {976563, 976563, 976563},
  {976563, -976563, 976563},
  {-976563, -976563, 976563},
  {-976563, 976563, 976563},
  {-976563, 976563, -976563},
  {-976563, -976563, 976563},
  {-976563, 976563, -976563},
  {-976563, -976563, -976563},
  {-976563, 976563, -976563},
  {-976563, 976563, 976563},
  {976563, 976563, 976563},
  {-976563, 976563, -976563},
  {976563, 976563, 976563},
  {976563, 976563, -976563},
  {-976563, -976563, 976563},
  {-976563, -976563, -976563},
  {976563, -976563, -976563},
  {-976563, -976563, 976563},
  {976563, -976563, -976563},
  {976563, -976563, 976563},
  {-976563, -976563, 976563},
  {976563, -976563, 976563},
  {976563, 976563, 976563},
  {-976563, -976563, 976563},
  {976563, 976563, 976563},
  {-976563, 976563, 976563},
  {-976563, -976563, -976563},
  {-976563, 976563, -976563},
  {976563, 976563, -976563},
  {-976563, -976563, -976563},
  {976563, 976563, -976563},
  {976563, -976563, -976563},
};
/* vmtrig_fire_f1c_box_reference_nm: sources=12 sha256=bd32e7f995266af46b2ff4838e6334566a270564138b7cd8ccda2016af6e151a */

static const vmtrig_fire_f1c_foreign_census_t *
vmtrig_fire_f1c_foreign_census_lookup(int id) {
  return id == VMTRIG_FIRE_F1C_FOREIGN_CENSUS_SELFTEST
       ? &vmtrig_fire_f1c_foreign_census_selftest : NULL;
}

// FIRE proof emitters append above a protected Scene prefix.  Rewinding to this
// mark gives each fixture a clean tail without invalidating a caller's live ranges.
#define VMTRIG_FIRE_F1C_SCENE_TAIL_FLOATS 131072
static int vmtrig_fire_f1c_scene_base;
static int vmtrig_fire_f1c_scene_entry_valid(int floats, int want) {
  const int capacity = SCENE_MAX_VERTS * VERT_FLOATS;
  return floats >= 0 && floats <= capacity &&
         !(floats % (3 * VERT_FLOATS)) && want == floats &&
         VMTRIG_FIRE_F1C_SCENE_TAIL_FLOATS <= capacity &&
         floats <= capacity - VMTRIG_FIRE_F1C_SCENE_TAIL_FLOATS;
}

static long vmtrig_fire_f1c_scene_drops_canary(long saved) {
  return saved == 19L ? 20L : 19L;
}

static int vmtrig_fire_f1c_scene_rewind(void) {
  if (vmtrig_fire_f1c_scene_base < 0 ||
      vmtrig_fire_f1c_scene_base > SCENE_MAX_VERTS * VERT_FLOATS ||
      vmtrig_fire_f1c_scene_base % (3 * VERT_FLOATS)) return 0;
  g_scene_floats = vmtrig_fire_f1c_scene_base;
  g_scene_want = vmtrig_fire_f1c_scene_base;
  g_scene_dirty = 1;
  g_nschunk = 0;
  g_schunk_open = -1;
  g_scene_cast_floats = -1;
  return 1;
}

typedef struct {
  uint64_t work, limit;
  int overflow;
} vmtrig_fire_f1c_ctx_t;

typedef struct {
  double area, moment_lo, moment_hi;
  float max_upper, min_facing;
  uint64_t triangles;
  int fail;
} vmtrig_fire_f1c_bite_t;

static int vmtrig_fire_f1c_work(vmtrig_fire_f1c_ctx_t *ctx, uint64_t add) {
  if (!ctx || ctx->overflow || ctx->work > ctx->limit ||
      add > ctx->limit - ctx->work) {
    if (ctx) ctx->overflow = 1;
    return 0;
  }
  ctx->work += add;
  return 1;
}

static int vmtrig_fire_f1c_float(float got, float want) {
  return !memcmp(&got, &want, sizeof got);
}

static fig_contact_fire_f1c_t vmtrig_fire_f1c_descriptor(int weapon,
                                                          int section) {
  static const int full_lo[3] = {62, 65, 40};
  static const int full_hi[3] = {67, 69, 45};
  static const int zero_lo[3] = {62, 65, 40};
  static const int zero_hi[3] = {67, 69, 44};
  static const int seam_lo[3] = {62, 65, 40};
  static const int seam_hi[3] = {66, 69, 44};
  fig_contact_fire_f1c_t out = {
    .magic = FIG_CONTACT_FIRE_F1C_MAGIC,
    .weapon = weapon, .kind = GUN_CONTACT_GRIP,
    .feature_range = section + 1, .backing_range = 0, .section = section,
    .feature_part = GUN_MESH_PART_GRIP_PANEL,
    .feature_host = GUN_MESH_PART_GRIP_BODY,
    .feature_source0 = 6, .feature_source1 = 7,
    .feature_source_count = 44,
    .backing_full_lo = full_lo[section], .backing_full_hi = full_hi[section],
    .backing_zero_lo = zero_lo[section], .backing_zero_hi = zero_hi[section],
    .backing_seam_lo = seam_lo[section], .backing_seam_hi = seam_hi[section],
    .backing_source_count = 120,
    .hand = FIG_HAND_FIRE, .hand_section = section,
    .hand_source_lo = 204, .hand_source_hi = 239,
    .hand_source_count = 240,
    .foreign_census = VMTRIG_FIRE_F1C_FOREIGN_CENSUS_SELFTEST,
    .normal = {{1, 0, 0}}, .tangent = {{0, 1, 0}}, .axial = {{0, 0, 1}},
    .half_tangent = 0.0060f, .half_axial = 0.0045f,
    .zero_half_tangent = 0.005541891892f,
    .zero_half_axial = 0.004041891892f,
    .profile_epsilon = VMTRIG_FIRE_F1C_PROFILE_EPS,
    .backing_min = VMTRIG_FIRE_F1C_BACKING_MIN,
    .foreign_radius = VMTRIG_FIRE_F1C_FOREIGN_R,
  };
  return out;
}

static int vmtrig_fire_f1c_descriptor_valid(
    const fig_contact_fire_f1c_t *descriptor) {
  if (!descriptor || descriptor->magic != FIG_CONTACT_FIRE_F1C_MAGIC ||
      descriptor->weapon < WPN_AR || descriptor->weapon >= WPN_COUNT ||
      descriptor->kind != GUN_CONTACT_GRIP || descriptor->section < 0 ||
      descriptor->section >= 3) return 0;
  if (vmtrig_v3_nonfinite(descriptor->origin) ||
      vmtrig_v3_nonfinite(descriptor->normal) ||
      vmtrig_v3_nonfinite(descriptor->tangent) ||
      vmtrig_v3_nonfinite(descriptor->axial)) return 0;
  v3 handed_v = v3_cross(descriptor->tangent, descriptor->axial);
  float nn = v3_dot(descriptor->normal, descriptor->normal);
  float tt = v3_dot(descriptor->tangent, descriptor->tangent);
  float aa = v3_dot(descriptor->axial, descriptor->axial);
  float nt = v3_dot(descriptor->normal, descriptor->tangent);
  float na = v3_dot(descriptor->normal, descriptor->axial);
  float ta = v3_dot(descriptor->tangent, descriptor->axial);
  float handed = v3_dot(handed_v, descriptor->normal);
  if (vmtrig_v3_nonfinite(handed_v) || f_is_nonfinite(nn) ||
      f_is_nonfinite(tt) || f_is_nonfinite(aa) || f_is_nonfinite(nt) ||
      f_is_nonfinite(na) || f_is_nonfinite(ta) || f_is_nonfinite(handed))
    return 0;
  fig_contact_fire_f1c_t expected = vmtrig_fire_f1c_descriptor(
    descriptor->weapon, descriptor->section);
  const vmtrig_fire_f1c_foreign_census_t *foreign_census =
    vmtrig_fire_f1c_foreign_census_lookup(descriptor->foreign_census);
  if (descriptor->feature_range != expected.feature_range ||
      descriptor->backing_range != expected.backing_range ||
      descriptor->feature_part != expected.feature_part ||
      descriptor->feature_host != expected.feature_host ||
      descriptor->feature_source0 != expected.feature_source0 ||
      descriptor->feature_source1 != expected.feature_source1 ||
      descriptor->feature_source_count != expected.feature_source_count ||
      descriptor->backing_full_lo != expected.backing_full_lo ||
      descriptor->backing_full_hi != expected.backing_full_hi ||
      descriptor->backing_zero_lo != expected.backing_zero_lo ||
      descriptor->backing_zero_hi != expected.backing_zero_hi ||
      descriptor->backing_seam_lo != expected.backing_seam_lo ||
      descriptor->backing_seam_hi != expected.backing_seam_hi ||
      descriptor->backing_source_count != expected.backing_source_count ||
      descriptor->hand != expected.hand ||
      descriptor->hand_section != expected.hand_section ||
      descriptor->hand_source_lo != expected.hand_source_lo ||
      descriptor->hand_source_hi != expected.hand_source_hi ||
      descriptor->hand_source_count != expected.hand_source_count ||
      descriptor->foreign_census != expected.foreign_census ||
      !foreign_census ||
      foreign_census->magic != VMTRIG_FIRE_F1C_FOREIGN_CENSUS_MAGIC ||
      foreign_census->weapon != descriptor->weapon ||
      foreign_census->build_id != descriptor->foreign_census ||
      descriptor->hand_v0 < 0 || descriptor->hand_v1 <= descriptor->hand_v0 ||
      descriptor->hand_v1 > g_scene_floats ||
      descriptor->hand_v0 % (3 * VERT_FLOATS) ||
      descriptor->hand_v1 % (3 * VERT_FLOATS) ||
      (int64_t)descriptor->hand_v1 - (int64_t)descriptor->hand_v0 !=
        (int64_t)descriptor->hand_source_count * 3 * VERT_FLOATS ||
      fabsf(nn - 1.0f) > 1e-6f || fabsf(tt - 1.0f) > 1e-6f ||
      fabsf(aa - 1.0f) > 1e-6f || fabsf(nt) > 1e-6f ||
      fabsf(na) > 1e-6f || fabsf(ta) > 1e-6f || handed < 0.999999f ||
      !vmtrig_fire_f1c_float(descriptor->half_tangent,
                             expected.half_tangent) ||
      !vmtrig_fire_f1c_float(descriptor->half_axial,
                             expected.half_axial) ||
      !vmtrig_fire_f1c_float(descriptor->zero_half_tangent,
                             expected.zero_half_tangent) ||
      !vmtrig_fire_f1c_float(descriptor->zero_half_axial,
                             expected.zero_half_axial) ||
      !vmtrig_fire_f1c_float(descriptor->profile_epsilon,
                             expected.profile_epsilon) ||
      !vmtrig_fire_f1c_float(descriptor->backing_min,
                             expected.backing_min) ||
      !vmtrig_fire_f1c_float(descriptor->foreign_radius,
                             expected.foreign_radius)) return 0;
  return 1;
}

static int vmtrig_fire_f1c_vertex_ptr_cmp(const void *av, const void *bv) {
  const vmtrig_topo_vertex_t *a = av, *b = bv;
  return vmtrig_topo_vertex_cmp(*a, *b);
}

static int vmtrig_fire_f1c_vertex_find(
    const vmtrig_topo_vertex_t *vertex, int n, vmtrig_topo_vertex_t key) {
  int lo = 0, hi = n;
  while (lo < hi) {
    int mid = lo + (hi - lo) / 2;
    int cmp = vmtrig_topo_vertex_cmp(vertex[mid], key);
    if (cmp < 0) lo = mid + 1;
    else hi = mid;
  }
  return lo < n && !vmtrig_topo_vertex_cmp(vertex[lo], key) ? lo : -1;
}

static int vmtrig_fire_f1c_component_root(int parent[], int at) {
  int root = at;
  while (parent[root] != root) root = parent[root];
  while (parent[at] != at) {
    int next = parent[at];
    parent[at] = root;
    at = next;
  }
  return root;
}

static int vmtrig_fire_f1c_topology_exact_components(
    const int range[2], int want_v, int want_e, int want_f,
    int want_components, vmtrig_fire_f1c_ctx_t *ctx) {
  static vmtrig_topo_vertex_t vertex[VMTRIG_FIRE_F1C_TOPO_V_CAP];
  static vmtrig_topo_edge_t edge[VMTRIG_FIRE_F1C_TOPO_E_CAP];
  static int parent[VMTRIG_FIRE_F1C_TOPO_V_CAP];
  int nv = 0, ne = 0;
  if (!ctx || !vmtrig_fire_f1c_work(ctx, 1) || !range || range[0] < 0 ||
      range[1] <= range[0] || range[1] > g_scene_floats ||
      range[0] % (3 * VERT_FLOATS) ||
      range[1] % (3 * VERT_FLOATS)) return 0;
  int64_t span = (int64_t)range[1] - (int64_t)range[0];
  if (span <= 0 || span % (3 * VERT_FLOATS) ||
      span / (3 * VERT_FLOATS) > INT_MAX) return 0;
  int nf = (int)(span / (3 * VERT_FLOATS));
  if (nf != want_f || 3 * nf > VMTRIG_FIRE_F1C_TOPO_E_CAP ||
      3 * nf > VMTRIG_FIRE_F1C_TOPO_V_CAP) return 0;
  for (int f = range[0]; f < range[1]; f += 3 * VERT_FLOATS) {
    v3 a = SCENE_V(f), b = SCENE_V(f + VERT_FLOATS);
    v3 c = SCENE_V(f + 2 * VERT_FLOATS);
    v3 ab = v3_sub(b, a), ac = v3_sub(c, a);
    v3 cross = v3_cross(ab, ac);
    float cross_len = v3_len(cross);
    if (!vmtrig_fire_f1c_work(ctx, 3) || vmtrig_v3_nonfinite(a) ||
        vmtrig_v3_nonfinite(b) || vmtrig_v3_nonfinite(c) ||
        vmtrig_v3_nonfinite(ab) || vmtrig_v3_nonfinite(ac) ||
        vmtrig_v3_nonfinite(cross) || f_is_nonfinite(cross_len) ||
        cross_len <= 1e-9f) return 0;
    const float *raw = &g_scene[f];
    vmtrig_topo_vertex_t key[3] = {
      vmtrig_topo_vertex(raw),
      vmtrig_topo_vertex(raw + VERT_FLOATS),
      vmtrig_topo_vertex(raw + 2 * VERT_FLOATS),
    };
    for (int i = 0; i < 3; i++) vertex[nv++] = key[i];
    for (int i = 0; i < 3; i++) {
      vmtrig_topo_vertex_t va = key[i], vb = key[(i + 1) % 3];
      int cmp = vmtrig_topo_vertex_cmp(va, vb);
      if (!cmp) return 0;
      edge[ne++] = (vmtrig_topo_edge_t){cmp < 0 ? va : vb,
                                        cmp < 0 ? vb : va,
                                        cmp < 0 ? 1 : -1};
    }
  }
  qsort(vertex, (size_t)nv, sizeof *vertex, vmtrig_fire_f1c_vertex_ptr_cmp);
  int unique_v = 0;
  for (int i = 0; i < nv; i++)
    if (!i || vmtrig_topo_vertex_cmp(vertex[i - 1], vertex[i]))
      vertex[unique_v++] = vertex[i];
  for (int i = 0; i < unique_v; i++) parent[i] = i;
  qsort(edge, (size_t)ne, sizeof *edge, vmtrig_topo_edge_cmp);
  int unique_e = 0;
  for (int i = 0; i < ne;) {
    int j = i, direction = 0;
    while (j < ne && !vmtrig_topo_edge_cmp(&edge[i], &edge[j]))
      direction += edge[j++].dir;
    if (j - i != 2 || direction != 0) return 0;
    int a = vmtrig_fire_f1c_vertex_find(vertex, unique_v, edge[i].lo);
    int b = vmtrig_fire_f1c_vertex_find(vertex, unique_v, edge[i].hi);
    if (a < 0 || b < 0) return 0;
    a = vmtrig_fire_f1c_component_root(parent, a);
    b = vmtrig_fire_f1c_component_root(parent, b);
    if (a != b) parent[b] = a;
    unique_e++;
    i = j;
  }
  int components = 0;
  for (int i = 0; i < unique_v; i++)
    components += vmtrig_fire_f1c_component_root(parent, i) == i;
  return !ctx->overflow && unique_v == want_v && unique_e == want_e &&
         nf == want_f && components == want_components &&
         unique_v - unique_e + nf == 2;
}

static int vmtrig_fire_f1c_topology_exact(const int range[2], int want_v,
                                           int want_e, int want_f,
                                           vmtrig_fire_f1c_ctx_t *ctx) {
  return vmtrig_fire_f1c_topology_exact_components(
    range, want_v, want_e, want_f, 1, ctx);
}

static int vmtrig_fire_f1c_topology_closed(
    const int range[2], int want_f, vmtrig_fire_f1c_ctx_t *ctx) {
  if (want_f <= 0 || (want_f & 1) || want_f > INT_MAX / 3) return 0;
  int want_e = 3 * want_f / 2;
  int want_v = want_f / 2 + 2;
  return vmtrig_fire_f1c_topology_exact(
    range, want_v, want_e, want_f, ctx);
}

static int vmtrig_fire_f1c_geometry_reference(
    const vmtrig_fire_f1c_foreign_range_t *expected,
    const int32_t (**reference)[3], int *vertices) {
  if (reference) *reference = NULL;
  if (vertices) *vertices = 0;
  if (!expected || !reference || !vertices || expected->sources <= 0 ||
      expected->sources > INT_MAX / 3) return 0;
  switch (expected->geometry_identity) {
    case VMTRIG_FIRE_F1C_GEOMETRY_Q0:
      *reference = vmtrig_fire_f1c_q0_reference_nm;
      *vertices = (int)(sizeof vmtrig_fire_f1c_q0_reference_nm /
                        sizeof vmtrig_fire_f1c_q0_reference_nm[0]);
      break;
    case VMTRIG_FIRE_F1C_GEOMETRY_PANEL:
      *reference = vmtrig_fire_f1c_panel_reference_nm;
      *vertices = (int)(sizeof vmtrig_fire_f1c_panel_reference_nm /
                        sizeof vmtrig_fire_f1c_panel_reference_nm[0]);
      break;
    case VMTRIG_FIRE_F1C_GEOMETRY_BOX:
      *reference = vmtrig_fire_f1c_box_reference_nm;
      *vertices = (int)(sizeof vmtrig_fire_f1c_box_reference_nm /
                        sizeof vmtrig_fire_f1c_box_reference_nm[0]);
      break;
    default: return 0;
  }
  return *vertices == expected->sources * 3;
}

static int vmtrig_fire_f1c_local_vertex_exact(
    const vmtrig_fire_f1c_foreign_range_t *expected,
    const int32_t reference_nm[3], v3 p) {
  if (!expected || !reference_nm || vmtrig_v3_nonfinite(p) ||
      vmtrig_v3_nonfinite(expected->geometry_origin)) return 0;
  double rel[3] = {
    (double)p.x - (double)expected->geometry_origin.x,
    (double)p.y - (double)expected->geometry_origin.y,
    (double)p.z - (double)expected->geometry_origin.z,
  };
  double error2_nm = 0.0;
  for (int axis = 0; axis < 3; axis++) {
    v3 basis = expected->geometry_axis[axis];
    if (vmtrig_v3_nonfinite(basis)) return 0;
    double local = rel[0] * (double)basis.x +
                   rel[1] * (double)basis.y +
                   rel[2] * (double)basis.z;
    double error_nm = local * 1000000000.0 - (double)reference_nm[axis];
    if (f_is_nonfinite((float)local) ||
        f_is_nonfinite((float)error_nm)) return 0;
    error2_nm += error_nm * error_nm;
    if (f_is_nonfinite((float)error2_nm) || error2_nm > 250000.0)
      return 0;
  }
  return 1;
}

static int vmtrig_fire_f1c_foreign_census_valid(
    const fig_contact_fire_f1c_t *descriptor,
    vmtrig_fire_f1c_ctx_t *ctx) {
  if (!descriptor || !ctx || !vmtrig_fire_f1c_descriptor_valid(descriptor) ||
      !g_gun_contact_mesh_capture ||
      g_gun_contact_mesh_active != -1 || g_gun_contact_mesh_depth != 0 ||
      g_gun_contact_mesh_ignored != 0 || g_gun_contact_surface_active != 0 ||
      g_gun_contact_mesh_unclassified != 0 ||
      g_gun_contact_mesh_scope_overflow != 0 ||
      g_gun_contact_mesh_part != 0 || g_gun_contact_mesh_host_mask != 0 ||
      g_scene_want != g_scene_floats) return 0;
  const vmtrig_fire_f1c_foreign_census_t *census =
    vmtrig_fire_f1c_foreign_census_lookup(descriptor->foreign_census);
  if (!census || census->magic != VMTRIG_FIRE_F1C_FOREIGN_CENSUS_MAGIC ||
      census->weapon != descriptor->weapon ||
      census->build_id != descriptor->foreign_census) return 0;

  for (int kind = GUN_CONTACT_GRIP; kind < GUN_CONTACT_COUNT; kind++) {
    const gun_mesh_set_t *set = &g_gun_contact_mesh[kind];
    if (census->count[kind] < 0 ||
        census->count[kind] > GUN_CONTACT_MESH_MAX || set->overflow ||
        set->count < 0 || set->count > GUN_CONTACT_MESH_MAX ||
        set->count != census->count[kind]) return 0;
    for (int q = 0; q < set->count; q++) {
      const gun_mesh_range_t *actual = &set->range[q];
      const vmtrig_fire_f1c_foreign_range_t *expected =
        &census->range[kind][q];
      if (expected->sources <= 0 || actual->v0 < 0 ||
          actual->v1 <= actual->v0 || actual->v1 > g_scene_floats ||
          actual->v0 % (3 * VERT_FLOATS) ||
          actual->v1 % (3 * VERT_FLOATS) ||
          (q && set->range[q - 1].v1 > actual->v0) ||
          actual->surface != expected->surface ||
          actual->part != expected->part ||
          actual->host_mask != expected->host) return 0;
      int64_t span = (int64_t)actual->v1 - (int64_t)actual->v0;
      if (span != (int64_t)expected->sources * 3 * VERT_FLOATS) return 0;
      const int32_t (*reference_nm)[3] = NULL;
      int reference_vertices = 0;
      if (!vmtrig_fire_f1c_geometry_reference(
            expected, &reference_nm, &reference_vertices)) return 0;
      v3 lo = {{1e9f, 1e9f, 1e9f}}, hi = {{-1e9f, -1e9f, -1e9f}};
      for (int f = actual->v0; f < actual->v1; f += VERT_FLOATS) {
        if (!vmtrig_fire_f1c_work(ctx, 1)) return 0;
        for (int k = 0; k < VERT_FLOATS; k++)
          if (f_is_nonfinite(g_scene[f + k])) return 0;
        v3 p = SCENE_V(f);
        int vertex = (f - actual->v0) / VERT_FLOATS;
        if (vertex < 0 || vertex >= reference_vertices ||
            !vmtrig_fire_f1c_local_vertex_exact(
              expected, reference_nm[vertex], p)) return 0;
        for (int k = 0; k < 3; k++) {
          lo.e[k] = fminf(lo.e[k], p.e[k]);
          hi.e[k] = fmaxf(hi.e[k], p.e[k]);
        }
      }
      if (vmtrig_v3_nonfinite(lo) || vmtrig_v3_nonfinite(hi) ||
          reference_vertices != expected->sources * 3) return 0;
      int range[2] = {actual->v0, actual->v1};
      if (!vmtrig_fire_f1c_topology_closed(
            range, expected->sources, ctx)) return 0;
    }
  }
  // Capture ranges are disjoint Scene intervals even when their closed solids
  // intentionally overlap as hosted geometry.  An aliased/duplicated ordinal is
  // therefore never an alternate spelling of the same authored range.
  for (int ka = GUN_CONTACT_GRIP; ka < GUN_CONTACT_COUNT; ka++)
    for (int qa = 0; qa < g_gun_contact_mesh[ka].count; qa++)
      for (int kb = ka; kb < GUN_CONTACT_COUNT; kb++)
        for (int qb = kb == ka ? qa + 1 : 0;
             qb < g_gun_contact_mesh[kb].count; qb++) {
          const gun_mesh_range_t *a = &g_gun_contact_mesh[ka].range[qa];
          const gun_mesh_range_t *b = &g_gun_contact_mesh[kb].range[qb];
          if (a->v0 < b->v1 && b->v0 < a->v1) return 0;
        }

  if (memcmp(g_gun_guard, census->guard, sizeof census->guard) ||
      memcmp(g_gun_blade, census->blade, sizeof census->blade) ||
      memcmp(&g_gun_guard_r, &census->guard_r, sizeof g_gun_guard_r) ||
      memcmp(&g_gun_blade_r, &census->blade_r, sizeof g_gun_blade_r) ||
      !(g_gun_guard_r > 0.0f) || !(g_gun_blade_r > 0.0f) ||
      f_is_nonfinite(g_gun_guard_r) || f_is_nonfinite(g_gun_blade_r)) return 0;
  const v3 *tube[2] = {g_gun_guard, g_gun_blade};
  const int tube_n[2] = {4, 3};
  for (int t = 0; t < 2; t++)
    for (int i = 0; i < tube_n[t]; i++) {
      if (vmtrig_v3_nonfinite(tube[t][i])) return 0;
      if (i) {
        v3 delta = v3_sub(tube[t][i], tube[t][i - 1]);
        float length = v3_len(delta);
        if (vmtrig_v3_nonfinite(delta) || f_is_nonfinite(length) ||
            length <= 1e-9f) return 0;
      }
    }
  return !ctx->overflow;
}

static int vmtrig_fire_f1c_feature_mesh_valid(
    const fig_contact_fire_f1c_t *descriptor, const gun_mesh_set_t *set,
    vmtrig_fire_f1c_ctx_t *ctx) {
  if (!vmtrig_fire_f1c_descriptor_valid(descriptor) || !set || set->overflow ||
      set->count != 4 || descriptor->feature_range >= set->count ||
      descriptor->backing_range >= set->count) return 0;
  const gun_mesh_range_t *feature = &set->range[descriptor->feature_range];
  const gun_mesh_range_t *backing = &set->range[descriptor->backing_range];
  if (feature->v0 < 0 || feature->v1 <= feature->v0 ||
      feature->v1 > g_scene_floats || backing->v0 < 0 ||
      backing->v1 <= backing->v0 || backing->v1 > g_scene_floats ||
      feature->v0 % (3 * VERT_FLOATS) ||
      feature->v1 % (3 * VERT_FLOATS) ||
      backing->v0 % (3 * VERT_FLOATS) ||
      backing->v1 % (3 * VERT_FLOATS)) return 0;
  int feature_floats = feature->v1 - feature->v0;
  int backing_floats = backing->v1 - backing->v0;
  if (!feature->surface || feature->part != descriptor->feature_part ||
      feature->host_mask != descriptor->feature_host ||
      feature_floats != descriptor->feature_source_count * 3 * VERT_FLOATS ||
      !backing->surface || backing->part != GUN_MESH_PART_GRIP_BODY ||
      backing->host_mask != 0 ||
      backing_floats != descriptor->backing_source_count * 3 * VERT_FLOATS ||
      feature_floats % (3 * VERT_FLOATS) ||
      backing_floats % (3 * VERT_FLOATS)) return 0;
  int feature_range[2] = {feature->v0, feature->v1};
  int backing_range[2] = {backing->v0, backing->v1};
  if (!vmtrig_fire_f1c_topology_exact(feature_range, 24, 66, 44, ctx) ||
      !vmtrig_fire_f1c_topology_exact(backing_range, 62, 180, 120, ctx))
    return 0;

  float umin = 1e9f, umax = -1e9f, vmin = 1e9f, vmax = -1e9f;
  double area = 0.0;
  for (int source = descriptor->feature_source0;
       source <= descriptor->feature_source1; source++) {
    if (!vmtrig_fire_f1c_work(ctx, 1)) return 0;
    vmtrig_exact_cert_facet_t facet;
    if (!vmtrig_exact_cert_facet_load(set, descriptor->feature_range,
                                      source, &facet)) return 0;
    float normal_dot = v3_dot(facet.n, descriptor->normal);
    if (vmtrig_v3_nonfinite(facet.n) || f_is_nonfinite(normal_dot) ||
        normal_dot < 0.999999f) return 0;
    v3 p[3] = {facet.a, facet.b, facet.c};
    for (int i = 0; i < 3; i++) {
      v3 rel = v3_sub(p[i], descriptor->origin);
      float residual = v3_dot(rel, descriptor->normal);
      float u = v3_dot(rel, descriptor->tangent);
      float v = v3_dot(rel, descriptor->axial);
      if (vmtrig_v3_nonfinite(p[i]) || vmtrig_v3_nonfinite(rel) ||
          f_is_nonfinite(residual) || f_is_nonfinite(u) ||
          f_is_nonfinite(v) ||
          fabsf(residual) > descriptor->profile_epsilon ||
          fabsf(u) > descriptor->half_tangent +
                     descriptor->profile_epsilon ||
          fabsf(v) > descriptor->half_axial + descriptor->profile_epsilon)
        return 0;
      umin = fminf(umin, u); umax = fmaxf(umax, u);
      vmin = fminf(vmin, v); vmax = fmaxf(vmax, v);
    }
    float facet_area = vmtrig_exact_cert_area(p);
    if (f_is_nonfinite(facet_area)) return 0;
    area += (double)facet_area;
    if (f_is_nonfinite((float)area)) return 0;
  }
  const double want_area = 0.012 * 0.009;
  if (f_is_nonfinite(umin) || f_is_nonfinite(umax) ||
      f_is_nonfinite(vmin) || f_is_nonfinite(vmax) ||
      f_is_nonfinite((float)area) ||
      fabs((double)umin + 0.006) > 4e-7 ||
      fabs((double)umax - 0.006) > 4e-7 ||
      fabs((double)vmin + 0.0045) > 4e-7 ||
      fabs((double)vmax - 0.0045) > 4e-7 ||
      fabs(area - want_area) > 1e-10) return 0;

  // Sources outside 6/7 may meet the top only along a zero-area arris.  A third
  // coplanar positive-area source would silently enlarge the feature authority.
  for (int source = 0; source < descriptor->feature_source_count; source++) {
    if (source == descriptor->feature_source0 ||
        source == descriptor->feature_source1) continue;
    if (!vmtrig_fire_f1c_work(ctx, 1)) return 0;
    vmtrig_exact_cert_facet_t facet;
    if (!vmtrig_exact_cert_facet_load(set, descriptor->feature_range,
                                      source, &facet)) return 0;
    v3 p[3] = {facet.a, facet.b, facet.c};
    int coplanar = 1;
    for (int i = 0; i < 3; i++) {
      v3 rel = v3_sub(p[i], descriptor->origin);
      float residual = v3_dot(rel, descriptor->normal);
      if (vmtrig_v3_nonfinite(p[i]) || vmtrig_v3_nonfinite(rel) ||
          f_is_nonfinite(residual)) return 0;
      coplanar &= fabsf(residual) <= descriptor->profile_epsilon;
    }
    float facet_area = vmtrig_exact_cert_area(p);
    if (f_is_nonfinite(facet_area) || (coplanar && facet_area > 1e-12f))
      return 0;
  }
  return !ctx->overflow;
}

static const fig_contact_patch_t *vmtrig_fire_f1c_patch(
    const fig_contact_fire_f1c_t *descriptor, int *matches) {
  const fig_contact_patch_t *found = NULL;
  int count = 0;
  if (matches) *matches = 0;
  if (!descriptor || g_fig_contact_patch_overflow ||
      g_fig_contact_patch_n < 0 ||
      g_fig_contact_patch_n > FIG_CONTACT_PATCH_MAX) return NULL;
  for (int i = 0; i < g_fig_contact_patch_n; i++) {
    const fig_contact_patch_t *patch = &g_fig_contact_patch[i];
    if (patch->hand == descriptor->hand && patch->v0 == descriptor->hand_v0 &&
        patch->v1 == descriptor->hand_v1 && patch->name &&
        !strcmp(patch->name, "grip_digits")) {
      found = patch;
      count++;
    }
  }
  if (matches) *matches = count;
  return found;
}

static int vmtrig_fire_f1c_hand_mesh_valid(
    const fig_contact_fire_f1c_t *descriptor,
    vmtrig_fire_f1c_ctx_t *ctx) {
  if (!descriptor || !ctx || !vmtrig_fire_f1c_descriptor_valid(descriptor))
    return 0;
  int matches = 0;
  const fig_contact_patch_t *patch = vmtrig_fire_f1c_patch(descriptor, &matches);
  int range[2] = {descriptor->hand_v0, descriptor->hand_v1};
  return matches == 1 && patch && !patch->cert_tag && patch->n == 9 &&
         descriptor->hand_source_lo == 204 &&
         descriptor->hand_source_hi == 239 &&
         vmtrig_fire_f1c_topology_exact(range, 122, 360, 240, ctx);
}

static int vmtrig_fire_f1c_registry_valid(int weapon,
                                          const gun_mesh_set_t *set,
                                          vmtrig_fire_f1c_ctx_t *ctx) {
  if (weapon < WPN_AR || weapon >= WPN_COUNT ||
      g_fig_contact_fire_f1c_overflow || g_fig_contact_fire_f1c_n != 3 ||
      g_fig_contact_fire_f1c_n < 0 ||
      g_fig_contact_fire_f1c_n > FIG_CONTACT_FIRE_F1C_MAX ||
      g_fig_contact_patch_overflow || g_fig_contact_patch_n < 0 ||
      g_fig_contact_patch_n > FIG_CONTACT_PATCH_MAX)
    return 0;
  unsigned sections = 0;
  for (int i = 0; i < g_fig_contact_fire_f1c_n; i++) {
    const fig_contact_fire_f1c_t *descriptor = &g_fig_contact_fire_f1c[i];
    if (descriptor->weapon != weapon ||
        !vmtrig_fire_f1c_descriptor_valid(descriptor) ||
        !vmtrig_fire_f1c_feature_mesh_valid(descriptor, set, ctx) ||
        !vmtrig_fire_f1c_hand_mesh_valid(descriptor, ctx) ||
        (sections & (1u << descriptor->section))) return 0;
    sections |= 1u << descriptor->section;
    for (int j = i + 1; j < g_fig_contact_fire_f1c_n; j++)
      if (descriptor->feature_range ==
            g_fig_contact_fire_f1c[j].feature_range ||
          (descriptor->hand_v0 < g_fig_contact_fire_f1c[j].hand_v1 &&
           g_fig_contact_fire_f1c[j].hand_v0 < descriptor->hand_v1)) return 0;
  }
  return sections == 7u && !ctx->overflow;
}

#define VMTRIG_FIRE_F1C_POLY_CAP 16
typedef struct { double u, v, depth; } vmtrig_fire_f1c_p2_t;
typedef struct {
  vmtrig_fire_f1c_p2_t p[VMTRIG_FIRE_F1C_POLY_CAP];
  int n;
} vmtrig_fire_f1c_poly_t;

static int vmtrig_fire_f1c_p2_valid(vmtrig_fire_f1c_p2_t p) {
  return !f_is_nonfinite((float)p.u) && !f_is_nonfinite((float)p.v) &&
         !f_is_nonfinite((float)p.depth);
}

static int vmtrig_fire_f1c_poly_signed_area(
    const vmtrig_fire_f1c_poly_t *poly, double *result) {
  if (!poly || !result || poly->n < 0 ||
      poly->n > VMTRIG_FIRE_F1C_POLY_CAP) return 0;
  double area = 0.0;
  for (int i = 0; i < poly->n; i++) {
    int j = (i + 1) % poly->n;
    if (!vmtrig_fire_f1c_p2_valid(poly->p[i]) ||
        !vmtrig_fire_f1c_p2_valid(poly->p[j])) return 0;
    area += poly->p[i].u * poly->p[j].v -
            poly->p[j].u * poly->p[i].v;
    if (f_is_nonfinite((float)area)) return 0;
  }
  *result = 0.5 * area;
  return !f_is_nonfinite((float)*result);
}

static int vmtrig_fire_f1c_poly_area(
    const vmtrig_fire_f1c_poly_t *poly, double *result) {
  double signed_area = 0.0;
  if (!vmtrig_fire_f1c_poly_signed_area(poly, &signed_area)) return 0;
  *result = fabs(signed_area);
  return !f_is_nonfinite((float)*result);
}

static int vmtrig_fire_f1c_poly_halfspace(
    const vmtrig_fire_f1c_poly_t *in, double au, double av, double c,
    vmtrig_fire_f1c_poly_t *out) {
  if (!in || !out || in->n < 0 || in->n > VMTRIG_FIRE_F1C_POLY_CAP ||
      f_is_nonfinite((float)au) || f_is_nonfinite((float)av) ||
      f_is_nonfinite((float)c)) return 0;
  *out = (vmtrig_fire_f1c_poly_t){0};
  if (in->n < 3) return 1;
  for (int i = 0; i < in->n; i++) {
    int j = (i + 1) % in->n;
    vmtrig_fire_f1c_p2_t a = in->p[i], b = in->p[j];
    if (!vmtrig_fire_f1c_p2_valid(a) || !vmtrig_fire_f1c_p2_valid(b))
      return 0;
    double da = au * a.u + av * a.v - c;
    double db = au * b.u + av * b.v - c;
    if (f_is_nonfinite((float)da) || f_is_nonfinite((float)db)) return 0;
    int ina = da <= 1e-14, inb = db <= 1e-14;
    if (ina) {
      if (out->n >= VMTRIG_FIRE_F1C_POLY_CAP) return 0;
      out->p[out->n++] = a;
    }
    if (ina != inb) {
      double den = da - db;
      if (f_is_nonfinite((float)den) || fabs(den) <= 1e-30 ||
          out->n >= VMTRIG_FIRE_F1C_POLY_CAP)
        return 0;
      double t = da / den;
      vmtrig_fire_f1c_p2_t at = {
        a.u + (b.u - a.u) * t,
        a.v + (b.v - a.v) * t,
        a.depth + (b.depth - a.depth) * t,
      };
      if (f_is_nonfinite((float)t) || !vmtrig_fire_f1c_p2_valid(at)) return 0;
      out->p[out->n++] = at;
    }
  }
  return 1;
}

static int vmtrig_fire_f1c_poly_rect(vmtrig_fire_f1c_poly_t *poly,
                                      double hu, double hv) {
  static const double h[4][3] = {
    { 1, 0, 1}, {-1, 0, 1}, {0, 1, 1}, {0, -1, 1},
  };
  if (!poly || f_is_nonfinite((float)hu) || f_is_nonfinite((float)hv) ||
      !(hu > 0.0) || !(hv > 0.0)) return 0;
  for (int i = 0; i < 4; i++) {
    vmtrig_fire_f1c_poly_t next;
    double scale = i < 2 ? hu : hv;
    if (!vmtrig_fire_f1c_poly_halfspace(poly, h[i][0], h[i][1],
                                         h[i][2] * scale, &next)) return 0;
    *poly = next;
  }
  return 1;
}

static int vmtrig_fire_f1c_poly_intersection(
    const vmtrig_fire_f1c_poly_t *a, const vmtrig_fire_f1c_poly_t *b,
    vmtrig_fire_f1c_poly_t *out) {
  if (!a || !b || !out || a->n < 0 || b->n < 0 ||
      a->n > VMTRIG_FIRE_F1C_POLY_CAP ||
      b->n > VMTRIG_FIRE_F1C_POLY_CAP) return 0;
  *out = *a;
  if (a->n < 3 || b->n < 3) { out->n = 0; return 1; }
  double signed_area = 0.0;
  if (!vmtrig_fire_f1c_poly_signed_area(b, &signed_area) ||
      fabs(signed_area) <= 1e-30) return 0;
  double sign = signed_area >= 0.0 ? 1.0 : -1.0;
  for (int i = 0; i < b->n; i++) {
    int j = (i + 1) % b->n;
    double du = b->p[j].u - b->p[i].u;
    double dv = b->p[j].v - b->p[i].v;
    // CCW polygon interior is left of each edge: dv*u-du*v <= constant.
    double au = sign * dv, av = -sign * du;
    double c = au * b->p[i].u + av * b->p[i].v;
    if (f_is_nonfinite((float)du) || f_is_nonfinite((float)dv) ||
        f_is_nonfinite((float)au) || f_is_nonfinite((float)av) ||
        f_is_nonfinite((float)c)) return 0;
    vmtrig_fire_f1c_poly_t next;
    if (!vmtrig_fire_f1c_poly_halfspace(out, au, av, c, &next)) return 0;
    *out = next;
    if (out->n < 3) return 1;
  }
  return 1;
}

static int vmtrig_fire_f1c_backing_cover(
    const fig_contact_fire_f1c_t *descriptor, const gun_mesh_set_t *set,
    double half_u, double half_v, int source_lo, int source_hi,
    float *minimum, vmtrig_fire_f1c_ctx_t *ctx) {
  vmtrig_fire_f1c_poly_t piece[8];
  int piece_n = 0;
  unsigned seen = 0;
  double area_sum = 0.0;
  float min_depth = 1e9f;
  for (int source = 0; source < descriptor->backing_source_count; source++) {
    if (!vmtrig_fire_f1c_work(ctx, 1)) return 0;
    vmtrig_exact_cert_facet_t facet;
    if (!vmtrig_exact_cert_facet_load(set, descriptor->backing_range,
                                      source, &facet)) return 0;
    float facing = v3_dot(facet.n, descriptor->normal);
    if (vmtrig_v3_nonfinite(facet.n) || f_is_nonfinite(facing)) return 0;
    if (!(facing > 0.0f)) continue;
    v3 p[3] = {facet.a, facet.b, facet.c};
    vmtrig_fire_f1c_poly_t poly = {.n = 3};
    for (int i = 0; i < 3; i++) {
      v3 rel = v3_sub(p[i], descriptor->origin);
      if (vmtrig_v3_nonfinite(p[i]) || vmtrig_v3_nonfinite(rel)) return 0;
      poly.p[i] = (vmtrig_fire_f1c_p2_t){
        v3_dot(rel, descriptor->tangent),
        v3_dot(rel, descriptor->axial),
        -v3_dot(rel, descriptor->normal),
      };
      if (!vmtrig_fire_f1c_p2_valid(poly.p[i])) return 0;
    }
    if (!vmtrig_fire_f1c_poly_rect(&poly, half_u, half_v)) return 0;
    double area = 0.0;
    if (!vmtrig_fire_f1c_poly_area(&poly, &area)) return 0;
    if (area <= 1e-14) continue;
    if (source < source_lo || source > source_hi || piece_n >= 8) return 0;
    piece[piece_n++] = poly;
    seen |= 1u << (source - source_lo);
    area_sum += area;
    if (f_is_nonfinite((float)area_sum)) return 0;
    for (int i = 0; i < poly.n; i++) {
      min_depth = fminf(min_depth, (float)poly.p[i].depth);
      if (f_is_nonfinite(min_depth)) return 0;
    }
  }
  unsigned want_seen = (1u << (source_hi - source_lo + 1)) - 1u;
  if (seen != want_seen || piece_n != source_hi - source_lo + 1) return 0;
  for (int i = 0; i < piece_n; i++)
    for (int j = i + 1; j < piece_n; j++) {
      vmtrig_fire_f1c_poly_t overlap;
      double overlap_area = 0.0;
      if (!vmtrig_fire_f1c_poly_intersection(&piece[i], &piece[j], &overlap) ||
          !vmtrig_fire_f1c_poly_area(&overlap, &overlap_area) ||
          overlap_area > 1e-12) return 0;
    }
  double want_area = 4.0 * half_u * half_v;
  if (f_is_nonfinite((float)want_area) ||
      f_is_nonfinite((float)area_sum) ||
      fabs(area_sum - want_area) > 1e-10 ||
      !(min_depth >= descriptor->backing_min)) return 0;
  if (minimum) *minimum = min_depth;
  return !ctx->overflow;
}

static int vmtrig_fire_f1c_backing_valid(
    const fig_contact_fire_f1c_t *descriptor, const gun_mesh_set_t *set,
    float *minimum, vmtrig_fire_f1c_ctx_t *ctx) {
  if (!ctx || !vmtrig_fire_f1c_descriptor_valid(descriptor) || !set ||
      set->overflow ||
      descriptor->backing_range < 0 ||
      descriptor->backing_range >= set->count) return 0;
  const gun_mesh_range_t *backing = &set->range[descriptor->backing_range];
  if (!backing->surface || backing->part != GUN_MESH_PART_GRIP_BODY ||
      backing->host_mask != 0 || backing->v0 < 0 ||
      backing->v1 <= backing->v0 || backing->v1 > g_scene_floats ||
      backing->v0 % (3 * VERT_FLOATS) ||
      backing->v1 % (3 * VERT_FLOATS) ||
      (int64_t)backing->v1 - (int64_t)backing->v0 !=
        (int64_t)descriptor->backing_source_count * 3 * VERT_FLOATS) return 0;
  float full_min = 0.0f, zero_min = 0.0f;
  int full = vmtrig_fire_f1c_backing_cover(
    descriptor, set, descriptor->half_tangent, descriptor->half_axial,
    descriptor->backing_full_lo, descriptor->backing_full_hi,
    &full_min, ctx);
  int zero = vmtrig_fire_f1c_backing_cover(
    descriptor, set, descriptor->zero_half_tangent,
    descriptor->zero_half_axial, descriptor->backing_zero_lo,
    descriptor->backing_zero_hi, &zero_min, ctx);
  if (minimum) *minimum = fminf(full_min, zero_min);
  return full && zero && !ctx->overflow;
}

static int vmtrig_fire_f1c_depth_clip(const v3 in[4], int nin,
                                      const fig_contact_fire_f1c_t *descriptor,
                                      v3 out[4], int *nout) {
  int count = 0;
  for (int i = 0; i < nin; i++) {
    int j = (i + 1) % nin;
    float di = -v3_dot(v3_sub(in[i], descriptor->origin), descriptor->normal);
    float dj = -v3_dot(v3_sub(in[j], descriptor->origin), descriptor->normal);
    if (f_is_nonfinite(di) || f_is_nonfinite(dj)) return 0;
    int inside_i = di >= 0.0f, inside_j = dj >= 0.0f;
    if (inside_i) {
      if (count >= 4) return 0;
      out[count++] = in[i];
    }
    if (inside_i != inside_j) {
      float den = di - dj;
      if (f_is_nonfinite(den) || fabsf(den) <= 1e-20f || count >= 4)
        return 0;
      float t = di / den;
      if (f_is_nonfinite(t)) return 0;
      v3 at = v3_lerp(in[i], in[j], f_clamp(t, 0.0f, 1.0f));
      if (vmtrig_v3_nonfinite(at)) return 0;
      out[count++] = at;
    }
  }
  *nout = count;
  return 1;
}

static int vmtrig_fire_f1c_bite_geom(
    const fig_contact_fire_f1c_t *descriptor, int hand_source,
    v3 a, v3 b, v3 c, vmtrig_fire_f1c_bite_t *out,
    vmtrig_fire_f1c_ctx_t *ctx) {
  if (!descriptor || !out || !vmtrig_fire_f1c_work(ctx, 1) ||
      !vmtrig_fire_f1c_descriptor_valid(descriptor) ||
      hand_source < descriptor->hand_source_lo ||
      hand_source > descriptor->hand_source_hi ||
      f_is_nonfinite((float)out->area) ||
      f_is_nonfinite((float)out->moment_lo) ||
      f_is_nonfinite((float)out->moment_hi) ||
      f_is_nonfinite(out->max_upper) || f_is_nonfinite(out->min_facing) ||
      vmtrig_v3_nonfinite(a) || vmtrig_v3_nonfinite(b) ||
      vmtrig_v3_nonfinite(c)) {
    if (out) out->fail = 1;
    return 0;
  }
  v3 cross = v3_cross(v3_sub(b, a), v3_sub(c, a));
  float cross_len = v3_len(cross);
  if (vmtrig_v3_nonfinite(cross) || f_is_nonfinite(cross_len) ||
      cross_len <= 1e-9f) { out->fail = 1; return 0; }
  v3 normal = v3_scale(cross, 1.0f / cross_len);
  float facing = v3_dot(normal, v3_scale(descriptor->normal, -1.0f));
  if (vmtrig_v3_nonfinite(normal) || f_is_nonfinite(facing)) {
    out->fail = 1;
    return 0;
  }
  v3 input[4] = {a, b, c}, positive[4];
  int npositive = 0;
  if (!vmtrig_fire_f1c_depth_clip(input, 3, descriptor,
                                   positive, &npositive)) {
    out->fail = 1;
    return 0;
  }
  if (npositive < 3) return 1;
  if (!(facing > VMTRIG_PATCH_NORMAL_MIN)) {
    out->fail = 1;
    return 0;
  }
  const float margin_u = descriptor->half_tangent -
                         descriptor->foreign_radius -
                         descriptor->profile_epsilon;
  const float margin_v = descriptor->half_axial -
                         descriptor->foreign_radius -
                         descriptor->profile_epsilon;
  for (int i = 0; i < npositive; i++) {
    v3 rel = v3_sub(positive[i], descriptor->origin);
    float u = v3_dot(rel, descriptor->tangent);
    float v = v3_dot(rel, descriptor->axial);
    if (vmtrig_v3_nonfinite(rel) || f_is_nonfinite(u) || f_is_nonfinite(v) ||
        fabsf(u) > margin_u + 1e-8f ||
        fabsf(v) > margin_v + 1e-8f) {
      out->fail = 1;
      return 0;
    }
  }
  for (int i = 1; i + 1 < npositive; i++) {
    v3 p[3] = {positive[0], positive[i], positive[i + 1]};
    double area = (double)vmtrig_exact_cert_area(p);
    double depth_sum = 0.0;
    float upper = 0.0f;
    for (int j = 0; j < 3; j++) {
      float depth = -v3_dot(v3_sub(p[j], descriptor->origin),
                            descriptor->normal);
      if (depth < -1e-8f || f_is_nonfinite(depth)) {
        out->fail = 1;
        return 0;
      }
      depth_sum += (double)fmaxf(depth, 0.0f);
      upper = fmaxf(upper, depth + descriptor->profile_epsilon);
      if (f_is_nonfinite((float)depth_sum) || f_is_nonfinite(upper)) {
        out->fail = 1;
        return 0;
      }
    }
    double moment = area * depth_sum / 3.0;
    if (!(area > 0.0) || f_is_nonfinite((float)area) ||
        f_is_nonfinite((float)moment)) {
      out->fail = 1;
      return 0;
    }
    out->area += area;
    out->moment_lo += moment -
      (double)descriptor->profile_epsilon * area;
    out->moment_hi += moment +
      (double)descriptor->profile_epsilon * area;
    out->max_upper = fmaxf(out->max_upper, upper);
    if (f_is_nonfinite((float)out->area) ||
        f_is_nonfinite((float)out->moment_lo) ||
        f_is_nonfinite((float)out->moment_hi) ||
        f_is_nonfinite(out->max_upper)) {
      out->fail = 1;
      return 0;
    }
  }
  out->min_facing = out->triangles
                  ? fminf(out->min_facing, facing) : facing;
  if (out->triangles >= VMTRIG_FIRE_F1C_TRIANGLE_CAP) {
    out->fail = 1;
    return 0;
  }
  out->triangles++;
  return !out->fail && !ctx->overflow;
}

static int vmtrig_fire_f1c_bite_source(
    const fig_contact_fire_f1c_t *descriptor, int hand_source,
    vmtrig_fire_f1c_bite_t *out, vmtrig_fire_f1c_ctx_t *ctx) {
  if (!descriptor || !vmtrig_fire_f1c_descriptor_valid(descriptor) ||
      hand_source < descriptor->hand_source_lo ||
      hand_source > descriptor->hand_source_hi) {
    if (out) out->fail = 1;
    return 0;
  }
  int64_t offset = (int64_t)descriptor->hand_v0 +
                   (int64_t)hand_source * 3 * VERT_FLOATS;
  if (offset < descriptor->hand_v0 || offset > INT_MAX ||
      offset + 3 * VERT_FLOATS > descriptor->hand_v1) {
    if (out) out->fail = 1;
    return 0;
  }
  int f = (int)offset;
  return vmtrig_fire_f1c_bite_geom(
    descriptor, hand_source, SCENE_V(f), SCENE_V(f + VERT_FLOATS),
    SCENE_V(f + 2 * VERT_FLOATS), out, ctx);
}

static int vmtrig_fire_f1c_segment_segment_distance(
    v3 p1, v3 q1, v3 p2, v3 q2, float *distance) {
  if (!distance || vmtrig_v3_nonfinite(p1) || vmtrig_v3_nonfinite(q1) ||
      vmtrig_v3_nonfinite(p2) || vmtrig_v3_nonfinite(q2)) return 0;
  v3 d1 = v3_sub(q1, p1), d2 = v3_sub(q2, p2), r = v3_sub(p1, p2);
  if (vmtrig_v3_nonfinite(d1) || vmtrig_v3_nonfinite(d2) ||
      vmtrig_v3_nonfinite(r)) return 0;
  float a = v3_dot(d1, d1), e = v3_dot(d2, d2), f = v3_dot(d2, r);
  float s = 0.0f, t = 0.0f;
  if (f_is_nonfinite(a) || f_is_nonfinite(e) || f_is_nonfinite(f)) return 0;
  if (a <= 1e-20f && e <= 1e-20f) {
    *distance = v3_len(r);
    return !f_is_nonfinite(*distance);
  }
  if (a <= 1e-20f) {
    t = f_clamp(f / e, 0.0f, 1.0f);
  } else {
    float c = v3_dot(d1, r);
    if (f_is_nonfinite(c)) return 0;
    if (e <= 1e-20f) {
      s = f_clamp(-c / a, 0.0f, 1.0f);
    } else {
      float b = v3_dot(d1, d2), den = a * e - b * b;
      if (f_is_nonfinite(b) || f_is_nonfinite(den)) return 0;
      if (fabsf(den) > 1e-20f) s = f_clamp((b * f - c * e) / den,
                                           0.0f, 1.0f);
      t = (b * s + f) / e;
      if (t < 0.0f) { t = 0.0f; s = f_clamp(-c / a, 0.0f, 1.0f); }
      else if (t > 1.0f) {
        t = 1.0f; s = f_clamp((b - c) / a, 0.0f, 1.0f);
      }
    }
  }
  if (f_is_nonfinite(s) || f_is_nonfinite(t)) return 0;
  v3 c1 = v3_add(p1, v3_scale(d1, s));
  v3 c2 = v3_add(p2, v3_scale(d2, t));
  if (vmtrig_v3_nonfinite(c1) || vmtrig_v3_nonfinite(c2)) return 0;
  *distance = v3_len(v3_sub(c1, c2));
  return !f_is_nonfinite(*distance);
}

static int vmtrig_fire_f1c_point_in_tri(v3 p, v3 a, v3 b, v3 c, v3 n) {
  float sign = 0.0f;
  v3 v[3] = {a, b, c};
  for (int i = 0; i < 3; i++) {
    float side = v3_dot(v3_cross(v3_sub(v[(i + 1) % 3], v[i]),
                                  v3_sub(p, v[i])), n);
    if (f_is_nonfinite(side)) return -1;
    if (fabsf(side) <= 1e-9f) continue;
    if (sign == 0.0f) sign = side;
    else if ((sign < 0.0f) != (side < 0.0f)) return 0;
  }
  return 1;
}

static int vmtrig_fire_f1c_segment_tri_distance(
    v3 p0, v3 p1, v3 a, v3 b, v3 c, float *distance) {
  if (!distance || vmtrig_v3_nonfinite(p0) || vmtrig_v3_nonfinite(p1) ||
      vmtrig_v3_nonfinite(a) || vmtrig_v3_nonfinite(b) ||
      vmtrig_v3_nonfinite(c)) return 0;
  v3 cross = v3_cross(v3_sub(b, a), v3_sub(c, a));
  float len = v3_len(cross);
  if (vmtrig_v3_nonfinite(cross) || f_is_nonfinite(len)) return 0;
  if (len <= 1e-9f) { *distance = 0.0f; return 1; }
  v3 n = v3_scale(cross, 1.0f / len);
  float d0 = v3_dot(v3_sub(p0, a), n), d1 = v3_dot(v3_sub(p1, a), n);
  if (vmtrig_v3_nonfinite(n) || f_is_nonfinite(d0) ||
      f_is_nonfinite(d1)) return 0;
  if ((d0 <= 0.0f && d1 >= 0.0f) || (d0 >= 0.0f && d1 <= 0.0f)) {
    float den = d0 - d1;
    if (f_is_nonfinite(den)) return 0;
    if (fabsf(den) > 1e-20f) {
      float u = d0 / den;
      if (f_is_nonfinite(u)) return 0;
      v3 at = v3_lerp(p0, p1, f_clamp(u, 0.0f, 1.0f));
      if (vmtrig_v3_nonfinite(at)) return 0;
      int inside = vmtrig_fire_f1c_point_in_tri(at, a, b, c, n);
      if (inside < 0) return 0;
      if (inside) { *distance = 0.0f; return 1; }
    }
  }
  float dfirst = pt_tri_dist(p0, a, b, c);
  float dsecond = pt_tri_dist(p1, a, b, c);
  if (f_is_nonfinite(dfirst) || f_is_nonfinite(dsecond)) return 0;
  float best = fminf(dfirst, dsecond);
  v3 t[3] = {a, b, c};
  for (int i = 0; i < 3; i++) {
    float edge = 0.0f;
    if (!vmtrig_fire_f1c_segment_segment_distance(
          p0, p1, t[i], t[(i + 1) % 3], &edge)) return 0;
    best = fminf(best, edge);
  }
  if (f_is_nonfinite(best)) return 0;
  *distance = best;
  return 1;
}

static int vmtrig_fire_f1c_tri_distance(
    v3 a0, v3 a1, v3 a2, v3 b0, v3 b1, v3 b2, float *distance) {
  if (!distance) return 0;
  v3 a[3] = {a0, a1, a2}, b[3] = {b0, b1, b2};
  float best = 1e9f;
  for (int i = 0; i < 3; i++) {
    float edge = 0.0f;
    if (!vmtrig_fire_f1c_segment_tri_distance(
          a[i], a[(i + 1) % 3], b0, b1, b2, &edge)) return 0;
    best = fminf(best, edge);
    if (!vmtrig_fire_f1c_segment_tri_distance(
          b[i], b[(i + 1) % 3], a0, a1, a2, &edge)) return 0;
    best = fminf(best, edge);
  }
  if (f_is_nonfinite(best)) return 0;
  *distance = best;
  return 1;
}

static int vmtrig_fire_f1c_descriptor_registered(
    const fig_contact_fire_f1c_t *descriptor);

// Geometry-only kernel. Product-style callers must enter through the source
// loader below. It consumes every captured contact kind plus the separately
// published Guard/Blade solids; no four-range GRIP subset is a foreign proof.
static int vmtrig_fire_f1c_foreign_geom(
    const fig_contact_fire_f1c_t *descriptor, v3 a, v3 b, v3 c,
    float *minimum, vmtrig_fire_f1c_ctx_t *ctx) {
  float best = 1e9f;
  const gun_mesh_set_t *grip = &g_gun_contact_mesh[GUN_CONTACT_GRIP];
  if (!ctx || !vmtrig_fire_f1c_descriptor_valid(descriptor) ||
      !vmtrig_fire_f1c_descriptor_registered(descriptor) ||
      !vmtrig_fire_f1c_registry_valid(descriptor->weapon, grip, ctx) ||
      !vmtrig_fire_f1c_foreign_census_valid(descriptor, ctx) ||
      vmtrig_v3_nonfinite(a) || vmtrig_v3_nonfinite(b) ||
      vmtrig_v3_nonfinite(c)) return 0;
  for (int kind = GUN_CONTACT_GRIP; kind < GUN_CONTACT_COUNT; kind++) {
    const gun_mesh_set_t *set = &g_gun_contact_mesh[kind];
    if (set->overflow || set->count < 0 ||
        set->count > GUN_CONTACT_MESH_MAX) return 0;
    for (int q = 0; q < set->count; q++) {
      const gun_mesh_range_t *range = &set->range[q];
      if (range->v0 < 0 || range->v1 > g_scene_floats ||
          range->v1 <= range->v0 || range->v0 % (3 * VERT_FLOATS) ||
          range->v1 % (3 * VERT_FLOATS)) return 0;
      int64_t span = (int64_t)range->v1 - (int64_t)range->v0;
      if (span <= 0 || span % (3 * VERT_FLOATS) ||
          span / (3 * VERT_FLOATS) > INT_MAX) return 0;
      int count = (int)(span / (3 * VERT_FLOATS));
      for (int source = 0; source < count; source++) {
        if (kind == GUN_CONTACT_GRIP && q == descriptor->feature_range &&
            (source == descriptor->feature_source0 ||
             source == descriptor->feature_source1)) continue;
        if (!vmtrig_fire_f1c_work(ctx, 1)) return 0;
        vmtrig_exact_cert_facet_t facet;
        float distance = 0.0f;
        if (!vmtrig_exact_cert_facet_load(set, q, source, &facet) ||
            !vmtrig_fire_f1c_tri_distance(
              a, b, c, facet.a, facet.b, facet.c, &distance)) return 0;
        best = fminf(best, distance);
        if (f_is_nonfinite(best)) return 0;
      }
    }
  }
  if (!(g_gun_guard_r > 0.0f) || !(g_gun_blade_r > 0.0f) ||
      f_is_nonfinite(g_gun_guard_r) || f_is_nonfinite(g_gun_blade_r))
    return 0;
  const v3 *tube[2] = {g_gun_guard, g_gun_blade};
  const int tube_n[2] = {4, 3};
  const float tube_r[2] = {g_gun_guard_r, g_gun_blade_r};
  for (int t = 0; t < 2; t++)
    for (int i = 0; i + 1 < tube_n[t]; i++) {
      float axis_distance = 0.0f;
      if (!vmtrig_fire_f1c_work(ctx, 1) ||
          !vmtrig_fire_f1c_segment_tri_distance(
            tube[t][i], tube[t][i + 1], a, b, c, &axis_distance)) return 0;
      float solid_distance = axis_distance - tube_r[t];
      if (f_is_nonfinite(solid_distance)) return 0;
      best = fminf(best, solid_distance);
    }
  if (minimum) *minimum = best;
  return !f_is_nonfinite(best) &&
         best >= descriptor->foreign_radius && !ctx->overflow;
}

static int vmtrig_fire_f1c_foreign_source(
    const fig_contact_fire_f1c_t *descriptor, int hand_source,
    float *minimum, vmtrig_fire_f1c_ctx_t *ctx) {
  if (!descriptor || !vmtrig_fire_f1c_descriptor_valid(descriptor) ||
      hand_source < descriptor->hand_source_lo ||
      hand_source > descriptor->hand_source_hi) return 0;
  int64_t offset = (int64_t)descriptor->hand_v0 +
                   (int64_t)hand_source * 3 * VERT_FLOATS;
  if (offset < descriptor->hand_v0 || offset > INT_MAX ||
      offset + 3 * VERT_FLOATS > descriptor->hand_v1) return 0;
  int f = (int)offset;
  return vmtrig_fire_f1c_foreign_geom(
    descriptor, SCENE_V(f), SCENE_V(f + VERT_FLOATS),
    SCENE_V(f + 2 * VERT_FLOATS), minimum, ctx);
}

static int vmtrig_fire_f1c_plane_slice(const v3 tri[3], v3 plane_origin,
                                        v3 plane_normal, v3 line,
                                        double out[2]) {
  v3 point[3];
  int count = 0;
  float distance[3];
  if (!out || vmtrig_v3_nonfinite(plane_origin) ||
      vmtrig_v3_nonfinite(plane_normal) || vmtrig_v3_nonfinite(line)) return 0;
  for (int i = 0; i < 3; i++) {
    if (vmtrig_v3_nonfinite(tri[i])) return 0;
    distance[i] = v3_dot(v3_sub(tri[i], plane_origin), plane_normal);
    if (f_is_nonfinite(distance[i])) return 0;
  }
  for (int i = 0; i < 3; i++) {
    int j = (i + 1) % 3;
    float di = distance[i], dj = distance[j];
    int zi = fabsf(di) <= 1e-8f, zj = fabsf(dj) <= 1e-8f;
    if (zi && zj) return 0;
    v3 at;
    int have = 0;
    if (zi) { at = tri[i]; have = 1; }
    else if ((di < 0.0f && dj > 0.0f) || (di > 0.0f && dj < 0.0f)) {
      float den = di - dj;
      if (f_is_nonfinite(den) || fabsf(den) <= 1e-20f) return 0;
      float t = di / den;
      if (f_is_nonfinite(t)) return 0;
      at = v3_lerp(tri[i], tri[j], t);
      if (vmtrig_v3_nonfinite(at)) return 0;
      have = 1;
    }
    if (!have) continue;
    int duplicate = 0;
    for (int k = 0; k < count; k++) {
      v3 delta = v3_sub(point[k], at);
      float delta_len = v3_len(delta);
      if (vmtrig_v3_nonfinite(delta) || f_is_nonfinite(delta_len)) return 0;
      duplicate |= delta_len <= 1e-8f;
    }
    if (!duplicate) {
      if (count >= 3) return 0;
      point[count++] = at;
    }
  }
  if (count != 2) return 0;
  out[0] = (double)v3_dot(point[0], line);
  out[1] = (double)v3_dot(point[1], line);
  if (f_is_nonfinite((float)out[0]) || f_is_nonfinite((float)out[1]))
    return 0;
  if (out[1] < out[0]) {
    double swap = out[0]; out[0] = out[1]; out[1] = swap;
  }
  return out[1] - out[0] > 1e-9;
}

enum {
  VMTRIG_FIRE_F1C_CROSS_NONE,
  VMTRIG_FIRE_F1C_CROSS_SEGMENT,
  VMTRIG_FIRE_F1C_CROSS_HARD,
};

typedef struct {
  int allowed, hard, duplicate, overflow;
} vmtrig_fire_f1c_cross_t;

static int vmtrig_fire_f1c_cross_separated(
    const v3 hand_tri[3], const v3 weapon_tri[3]) {
  float distance = 0.0f;
  if (!vmtrig_fire_f1c_tri_distance(
        hand_tri[0], hand_tri[1], hand_tri[2],
        weapon_tri[0], weapon_tri[1], weapon_tri[2], &distance)) return -1;
  return distance > VMTRIG_FIRE_F1C_PROFILE_EPS;
}

static int vmtrig_fire_f1c_cross_relation(
    const v3 hand_tri[3], const v3 weapon_tri[3],
    v3 endpoint[2]) {
  for (int i = 0; i < 3; i++)
    if (vmtrig_v3_nonfinite(hand_tri[i]) ||
        vmtrig_v3_nonfinite(weapon_tri[i])) return VMTRIG_FIRE_F1C_CROSS_HARD;
  int shared_vertex = 0;
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      shared_vertex += v3_len(v3_sub(hand_tri[i], weapon_tri[j])) <= 1e-8f;
  if (shared_vertex >= 2) return VMTRIG_FIRE_F1C_CROSS_HARD;
  v3 hn = v3_cross(v3_sub(hand_tri[1], hand_tri[0]),
                    v3_sub(hand_tri[2], hand_tri[0]));
  v3 fn = v3_cross(v3_sub(weapon_tri[1], weapon_tri[0]),
                    v3_sub(weapon_tri[2], weapon_tri[0]));
  float hl = v3_len(hn), fl = v3_len(fn);
  if (vmtrig_v3_nonfinite(hn) || vmtrig_v3_nonfinite(fn) ||
      f_is_nonfinite(hl) || f_is_nonfinite(fl) ||
      hl <= 1e-9f || fl <= 1e-9f) return VMTRIG_FIRE_F1C_CROSS_HARD;
  hn = v3_scale(hn, 1.0f / hl);
  fn = v3_scale(fn, 1.0f / fl);
  v3 line = v3_cross(hn, fn);
  float line_len = v3_len(line);
  if (vmtrig_v3_nonfinite(line) || f_is_nonfinite(line_len))
    return VMTRIG_FIRE_F1C_CROSS_HARD;
  if (line_len <= 1e-7f) {
    int separated = vmtrig_fire_f1c_cross_separated(hand_tri, weapon_tri);
    return separated > 0 ? VMTRIG_FIRE_F1C_CROSS_NONE
                         : VMTRIG_FIRE_F1C_CROSS_HARD;
  }
  line = v3_scale(line, 1.0f / line_len);
  double hand_interval[2], feature_interval[2];
  if (!vmtrig_fire_f1c_plane_slice(hand_tri, weapon_tri[0], fn, line,
                                    hand_interval) ||
      !vmtrig_fire_f1c_plane_slice(weapon_tri, hand_tri[0], hn, line,
                                    feature_interval)) {
    int separated = vmtrig_fire_f1c_cross_separated(hand_tri, weapon_tri);
    return separated > 0 ? VMTRIG_FIRE_F1C_CROSS_NONE
                         : VMTRIG_FIRE_F1C_CROSS_HARD;
  }
  double lo = fmax(hand_interval[0], feature_interval[0]);
  double hi = fmin(hand_interval[1], feature_interval[1]);
  if (f_is_nonfinite((float)lo) || f_is_nonfinite((float)hi))
    return VMTRIG_FIRE_F1C_CROSS_HARD;
  if (!(hi - lo > 1e-9)) {
    int separated = vmtrig_fire_f1c_cross_separated(hand_tri, weapon_tri);
    return separated > 0 ? VMTRIG_FIRE_F1C_CROSS_NONE
                         : VMTRIG_FIRE_F1C_CROSS_HARD;
  }

  // Recover both segment endpoints from the two actual triangle planes.
  // Any point on the line can be obtained from the two plane equations.
  double d1 = (double)v3_dot(hn, hand_tri[0]);
  double d2 = (double)v3_dot(fn, weapon_tri[0]);
  double nd = (double)v3_dot(hn, fn);
  double den = 1.0 - nd * nd;
  if (f_is_nonfinite((float)d1) || f_is_nonfinite((float)d2) ||
      f_is_nonfinite((float)nd) || f_is_nonfinite((float)den) ||
      !(den > 1e-12)) return VMTRIG_FIRE_F1C_CROSS_HARD;
  double ca = (d1 - nd * d2) / den;
  double cb = (d2 - nd * d1) / den;
  if (f_is_nonfinite((float)ca) || f_is_nonfinite((float)cb))
    return VMTRIG_FIRE_F1C_CROSS_HARD;
  v3 base = v3_add(v3_scale(hn, (float)ca), v3_scale(fn, (float)cb));
  double base_t = (double)v3_dot(base, line);
  if (vmtrig_v3_nonfinite(base) || f_is_nonfinite((float)base_t))
    return VMTRIG_FIRE_F1C_CROSS_HARD;
  endpoint[0] = v3_add(base, v3_scale(line, (float)(lo - base_t)));
  endpoint[1] = v3_add(base, v3_scale(line, (float)(hi - base_t)));
  if (vmtrig_v3_nonfinite(endpoint[0]) || vmtrig_v3_nonfinite(endpoint[1]))
    return VMTRIG_FIRE_F1C_CROSS_HARD;
  return VMTRIG_FIRE_F1C_CROSS_SEGMENT;
}

static int vmtrig_fire_f1c_set_ranges_valid(
    const fig_contact_fire_f1c_t *descriptor, const gun_mesh_set_t *set) {
  if (!vmtrig_fire_f1c_descriptor_valid(descriptor) || !set || set->overflow ||
      set->count != 4) return 0;
  for (int q = 0; q < set->count; q++) {
    const gun_mesh_range_t *range = &set->range[q];
    if (range->v0 < 0 || range->v1 <= range->v0 ||
        range->v1 > g_scene_floats || range->v0 % (3 * VERT_FLOATS) ||
        range->v1 % (3 * VERT_FLOATS) ||
        (q && set->range[q - 1].v1 > range->v0)) return 0;
    int64_t span = (int64_t)range->v1 - (int64_t)range->v0;
    int count = q == descriptor->backing_range
              ? descriptor->backing_source_count
              : descriptor->feature_source_count;
    uint32_t part = q == descriptor->backing_range
                  ? GUN_MESH_PART_GRIP_BODY : GUN_MESH_PART_GRIP_PANEL;
    uint32_t host = q == descriptor->backing_range
                  ? 0 : GUN_MESH_PART_GRIP_BODY;
    if (!range->surface || range->part != part || range->host_mask != host ||
        span != (int64_t)count * 3 * VERT_FLOATS) return 0;
  }
  return descriptor->feature_range == descriptor->section + 1 &&
         descriptor->backing_range == 0;
}

static int vmtrig_fire_f1c_feature_top_exact(
    const fig_contact_fire_f1c_t *descriptor, const gun_mesh_set_t *set,
    vmtrig_fire_f1c_ctx_t *ctx) {
  float umin = 1e9f, umax = -1e9f, vmin = 1e9f, vmax = -1e9f;
  double area = 0.0;
  for (int source = 0; source < descriptor->feature_source_count; source++) {
    if (!vmtrig_fire_f1c_work(ctx, 1)) return 0;
    vmtrig_exact_cert_facet_t facet;
    if (!vmtrig_exact_cert_facet_load(
          set, descriptor->feature_range, source, &facet)) return 0;
    v3 p[3] = {facet.a, facet.b, facet.c};
    int allowed = source == descriptor->feature_source0 ||
                  source == descriptor->feature_source1;
    int coplanar = 1;
    for (int i = 0; i < 3; i++) {
      v3 rel = v3_sub(p[i], descriptor->origin);
      float residual = v3_dot(rel, descriptor->normal);
      if (vmtrig_v3_nonfinite(p[i]) || vmtrig_v3_nonfinite(rel) ||
          f_is_nonfinite(residual)) return 0;
      coplanar &= fabsf(residual) <= descriptor->profile_epsilon;
      if (!allowed) continue;
      float u = v3_dot(rel, descriptor->tangent);
      float v = v3_dot(rel, descriptor->axial);
      if (f_is_nonfinite(u) || f_is_nonfinite(v) ||
          fabsf(residual) > descriptor->profile_epsilon ||
          fabsf(u) > descriptor->half_tangent + descriptor->profile_epsilon ||
          fabsf(v) > descriptor->half_axial + descriptor->profile_epsilon)
        return 0;
      umin = fminf(umin, u); umax = fmaxf(umax, u);
      vmin = fminf(vmin, v); vmax = fmaxf(vmax, v);
    }
    float facet_area = vmtrig_exact_cert_area(p);
    if (f_is_nonfinite(facet_area)) return 0;
    if (allowed) {
      float normal_dot = v3_dot(facet.n, descriptor->normal);
      if (vmtrig_v3_nonfinite(facet.n) || f_is_nonfinite(normal_dot) ||
          normal_dot < 0.999999f) return 0;
      area += (double)facet_area;
      if (f_is_nonfinite((float)area)) return 0;
    } else if (coplanar && facet_area > 1e-12f) {
      return 0;
    }
  }
  return !f_is_nonfinite(umin) && !f_is_nonfinite(umax) &&
         !f_is_nonfinite(vmin) && !f_is_nonfinite(vmax) &&
         !f_is_nonfinite((float)area) &&
         fabs((double)umin + 0.006) <= 4e-7 &&
         fabs((double)umax - 0.006) <= 4e-7 &&
         fabs((double)vmin + 0.0045) <= 4e-7 &&
         fabs((double)vmax - 0.0045) <= 4e-7 &&
         fabs(area - 0.012 * 0.009) <= 1e-10 && !ctx->overflow;
}

static int vmtrig_fire_f1c_seam_valid(
    const fig_contact_fire_f1c_t *descriptor, const gun_mesh_set_t *set,
    vmtrig_fire_f1c_ctx_t *ctx) {
  unsigned char seen[120] = {0};
  if (!ctx || !vmtrig_fire_f1c_set_ranges_valid(descriptor, set)) return 0;
  for (int feature_source = 0;
       feature_source < descriptor->feature_source_count; feature_source++) {
    vmtrig_exact_cert_facet_t feature;
    if (!vmtrig_exact_cert_facet_load(
          set, descriptor->feature_range, feature_source, &feature)) return 0;
    v3 ft[3] = {feature.a, feature.b, feature.c};
    for (int body_source = 0;
         body_source < descriptor->backing_source_count; body_source++) {
      if (!vmtrig_fire_f1c_work(ctx, 1)) return 0;
      vmtrig_exact_cert_facet_t body;
      if (!vmtrig_exact_cert_facet_load(
            set, descriptor->backing_range, body_source, &body)) return 0;
      v3 bt[3] = {body.a, body.b, body.c}, endpoint[2];
      int relation = vmtrig_fire_f1c_cross_relation(ft, bt, endpoint);
      if (relation == VMTRIG_FIRE_F1C_CROSS_NONE) continue;
      if (relation != VMTRIG_FIRE_F1C_CROSS_SEGMENT ||
          body_source < descriptor->backing_seam_lo ||
          body_source > descriptor->backing_seam_hi) return 0;
      seen[body_source] = 1;
    }
  }
  for (int source = descriptor->backing_seam_lo;
       source <= descriptor->backing_seam_hi; source++)
    if (!seen[source]) return 0;
  return !ctx->overflow;
}

static int vmtrig_fire_f1c_descriptor_registered(
    const fig_contact_fire_f1c_t *descriptor) {
  if (!descriptor || g_fig_contact_fire_f1c_overflow ||
      g_fig_contact_fire_f1c_n < 0 ||
      g_fig_contact_fire_f1c_n > FIG_CONTACT_FIRE_F1C_MAX) return 0;
  int matches = 0;
  for (int i = 0; i < g_fig_contact_fire_f1c_n; i++)
    matches += descriptor == &g_fig_contact_fire_f1c[i];
  return matches == 1;
}

static int vmtrig_fire_f1c_hand_plane_interval(
    const fig_contact_fire_f1c_t *descriptor, const v3 tri[3],
    int *proper, v3 *line, double expected[2]) {
  if (!descriptor || !tri || !proper || !line || !expected ||
      vmtrig_v3_nonfinite(descriptor->origin) ||
      vmtrig_v3_nonfinite(descriptor->normal)) return 0;
  float lo = 1e9f, hi = -1e9f;
  for (int i = 0; i < 3; i++) {
    v3 rel = v3_sub(tri[i], descriptor->origin);
    float d = v3_dot(rel, descriptor->normal);
    if (vmtrig_v3_nonfinite(tri[i]) || vmtrig_v3_nonfinite(rel) ||
        f_is_nonfinite(d)) return 0;
    lo = fminf(lo, d);
    hi = fmaxf(hi, d);
  }
  *proper = lo < -1e-8f && hi > 1e-8f;
  if (!*proper) return 1;
  v3 e0 = v3_sub(tri[1], tri[0]), e1 = v3_sub(tri[2], tri[0]);
  v3 hn = v3_cross(e0, e1);
  float hn_len = v3_len(hn);
  if (vmtrig_v3_nonfinite(e0) || vmtrig_v3_nonfinite(e1) ||
      vmtrig_v3_nonfinite(hn) || f_is_nonfinite(hn_len) ||
      hn_len <= 1e-9f) return 0;
  hn = v3_scale(hn, 1.0f / hn_len);
  *line = v3_cross(hn, descriptor->normal);
  float line_len = v3_len(*line);
  if (vmtrig_v3_nonfinite(hn) || vmtrig_v3_nonfinite(*line) ||
      f_is_nonfinite(line_len) || line_len <= 1e-7f) return 0;
  *line = v3_scale(*line, 1.0f / line_len);
  return vmtrig_fire_f1c_plane_slice(
    tri, descriptor->origin, descriptor->normal, *line, expected);
}

static int vmtrig_fire_f1c_ordered_union(
    const double expected[2], double interval[2][2], int n,
    int *duplicate) {
  const double tol = 1e-8;
  if (duplicate) *duplicate = 0;
  if (n < 1 || n > 2 ||
      f_is_nonfinite((float)expected[0]) ||
      f_is_nonfinite((float)expected[1]) ||
      !(expected[1] - expected[0] > tol)) return 0;
  for (int i = 0; i < n; i++) {
    if (f_is_nonfinite((float)interval[i][0]) ||
        f_is_nonfinite((float)interval[i][1]) ||
        !(interval[i][1] - interval[i][0] > tol)) return 0;
  }
  if (n == 2 && interval[1][0] < interval[0][0]) {
    double lo = interval[0][0], hi = interval[0][1];
    interval[0][0] = interval[1][0]; interval[0][1] = interval[1][1];
    interval[1][0] = lo; interval[1][1] = hi;
  }
  if (fabs(interval[0][0] - expected[0]) > tol) return 0;
  double cursor = interval[0][1];
  for (int i = 1; i < n; i++) {
    if (interval[i][0] < cursor - tol) {
      if (duplicate) *duplicate = 1;
      return 0;
    }
    if (interval[i][0] > cursor + tol) return 0;
    cursor = interval[i][1];
  }
  return fabs(cursor - expected[1]) <= tol;
}

#define VMTRIG_FIRE_F1C_COVERAGE_CAP 2
static int vmtrig_fire_f1c_feature_source_slot(
    const fig_contact_fire_f1c_t *descriptor, int range, int source) {
  // This is the complete allowed source bitmap, not merely a pair supplied by a
  // caller.  Descriptor validation freezes 44 emitted sources and ordinals 6/7;
  // the ordered source loop can therefore visit each of these two slots once.
  if (!descriptor || descriptor->feature_source_count != 44 ||
      descriptor->feature_source0 != 6 || descriptor->feature_source1 != 7 ||
      range != descriptor->feature_range) return -1;
  if (source == descriptor->feature_source0) return 0;
  if (source == descriptor->feature_source1) return 1;
  return -1;
}

static int vmtrig_fire_f1c_coverage_add(
    double covered[VMTRIG_FIRE_F1C_COVERAGE_CAP][2], int *covered_n,
    const double interval[2]) {
  // Count is checked before either caller buffer is touched.  A third event is a
  // hard capacity failure even if a malformed caller supplied null storage.
  if (!covered_n || *covered_n < 0 ||
      *covered_n >= VMTRIG_FIRE_F1C_COVERAGE_CAP) return 0;
  if (!covered || !interval || f_is_nonfinite((float)interval[0]) ||
      f_is_nonfinite((float)interval[1])) return 0;
  covered[*covered_n][0] = interval[0];
  covered[*covered_n][1] = interval[1];
  (*covered_n)++;
  return 1;
}

// Internal scanner still takes a set so the forged-pointer negative can exercise
// the boundary.  It rejects every pointer except the actual captured GRIP set.
static int vmtrig_fire_f1c_cross_scan_bound(
    const fig_contact_fire_f1c_t *descriptor, const gun_mesh_set_t *set,
    vmtrig_fire_f1c_cross_t *out, vmtrig_fire_f1c_ctx_t *ctx) {
  int matches = 0;
  if (out) *out = (vmtrig_fire_f1c_cross_t){0};
  const fig_contact_patch_t *patch = descriptor
    ? vmtrig_fire_f1c_patch(descriptor, &matches) : NULL;
  if (!out || !ctx || set != &g_gun_contact_mesh[GUN_CONTACT_GRIP] ||
      !vmtrig_fire_f1c_descriptor_registered(descriptor) ||
      !vmtrig_fire_f1c_registry_valid(descriptor->weapon, set, ctx) ||
      !vmtrig_fire_f1c_set_ranges_valid(descriptor, set) ||
      matches != 1 || !patch || patch->cert_tag || patch->n != 9 ||
      !vmtrig_fire_f1c_feature_top_exact(descriptor, set, ctx) ||
      !vmtrig_fire_f1c_backing_valid(descriptor, set, NULL, ctx) ||
      !vmtrig_fire_f1c_seam_valid(descriptor, set, ctx)) {
    if (out) out->hard = 1;
    return 0;
  }
  float limit_u = descriptor->half_tangent - descriptor->foreign_radius -
                  descriptor->profile_epsilon;
  float limit_v = descriptor->half_axial - descriptor->foreign_radius -
                  descriptor->profile_epsilon;
  for (int hand_source = 0;
       hand_source < descriptor->hand_source_count; hand_source++) {
    int hf = descriptor->hand_v0 + hand_source * 3 * VERT_FLOATS;
    v3 ht[3] = {SCENE_V(hf), SCENE_V(hf + VERT_FLOATS),
                SCENE_V(hf + 2 * VERT_FLOATS)};
    int proper = 0, covered_n = 0;
    v3 expected_line = {{0, 0, 0}};
    double expected[2] = {0}, covered[2][2] = {{0}};
    if (!vmtrig_fire_f1c_hand_plane_interval(
          descriptor, ht, &proper, &expected_line, expected)) {
      out->hard = 1;
      return 0;
    }
    for (int q = 0; q < set->count; q++) {
      int count = q == descriptor->backing_range
                ? descriptor->backing_source_count
                : descriptor->feature_source_count;
      for (int source = 0; source < count; source++) {
        if (!vmtrig_fire_f1c_work(ctx, 1)) {
          out->overflow = out->hard = 1;
          return 0;
        }
        vmtrig_exact_cert_facet_t facet;
        if (!vmtrig_exact_cert_facet_load(set, q, source, &facet)) {
          out->hard = 1;
          return 0;
        }
        v3 ft[3] = {facet.a, facet.b, facet.c}, endpoint[2];
        int relation = vmtrig_fire_f1c_cross_relation(ht, ft, endpoint);
        if (relation == VMTRIG_FIRE_F1C_CROSS_NONE) continue;
        int feature_slot = vmtrig_fire_f1c_feature_source_slot(
          descriptor, q, source);
        int allowed = relation == VMTRIG_FIRE_F1C_CROSS_SEGMENT &&
          hand_source >= descriptor->hand_source_lo &&
          hand_source <= descriptor->hand_source_hi &&
          feature_slot >= 0;
        for (int i = 0; allowed && i < 2; i++) {
          v3 rel = v3_sub(endpoint[i], descriptor->origin);
          float residual = fabsf(v3_dot(rel, descriptor->normal));
          float u = fabsf(v3_dot(rel, descriptor->tangent));
          float v = fabsf(v3_dot(rel, descriptor->axial));
          allowed &= !vmtrig_v3_nonfinite(endpoint[i]) &&
                     !vmtrig_v3_nonfinite(rel) &&
                     !f_is_nonfinite(residual) && !f_is_nonfinite(u) &&
                     !f_is_nonfinite(v) &&
                     residual <= descriptor->profile_epsilon &&
                     u < limit_u - 1e-9f && v < limit_v - 1e-9f;
        }
        if (!allowed) {
          out->hard = 1;
          return 0;
        }
        if (!proper || covered_n < 0 ||
            covered_n >= VMTRIG_FIRE_F1C_COVERAGE_CAP) {
          out->duplicate = out->hard = 1;
          return 0;
        }
        double interval[2] = {
          (double)v3_dot(endpoint[0], expected_line),
          (double)v3_dot(endpoint[1], expected_line),
        };
        if (interval[1] < interval[0]) {
          double swap = interval[0];
          interval[0] = interval[1];
          interval[1] = swap;
        }
        if (!vmtrig_fire_f1c_coverage_add(
              covered, &covered_n, interval)) {
          out->duplicate = out->hard = 1;
          return 0;
        }
        out->allowed++;
      }
    }
    int authorized = hand_source >= descriptor->hand_source_lo &&
                     hand_source <= descriptor->hand_source_hi;
    if (proper && authorized) {
      int duplicate = 0;
      if (!vmtrig_fire_f1c_ordered_union(
            expected, covered, covered_n, &duplicate)) {
        out->duplicate |= duplicate;
        out->hard = 1;
        return 0;
      }
    } else if (covered_n) {
      out->hard = 1;
      return 0;
    }
  }
  return !out->hard && !ctx->overflow;
}

// Sole product-style entry: callers cannot supply a set, triangle, interval or
// event flag.  Every source is loaded from the live captured Scene registry.
static int vmtrig_fire_f1c_cross_scan(
    const fig_contact_fire_f1c_t *descriptor,
    vmtrig_fire_f1c_cross_t *out, vmtrig_fire_f1c_ctx_t *ctx) {
  return vmtrig_fire_f1c_cross_scan_bound(
    descriptor, &g_gun_contact_mesh[GUN_CONTACT_GRIP], out, ctx);
}

typedef struct {
  int cases, passed;
  const char *first_fail;
} vmtrig_fire_f1c_test_t;

static void vmtrig_fire_f1c_case(vmtrig_fire_f1c_test_t *test,
                                  const char *name, int pass) {
  test->cases++;
  if (pass) test->passed++;
  else if (!test->first_fail) test->first_fail = name;
  printf("vmfiref1c-case %02d %-32s %s\n",
         test->cases, name, pass ? "PASS" : "FAIL");
}

static void vmtrig_fire_f1c_emit_hand(
    const fig_contact_fire_f1c_t *descriptor, int range[2]) {
  fnode_t node[10];
  static const float normal_offset[10] = {
    0.0070f, 0.0060f, 0.0050f, 0.0040f, 0.0030f,
    0.0020f, 0.0010f, 0.0002f, 0.0001f, -0.0036f,
  };
  for (int i = 0; i < 10; i++) {
    float half_tangent = i == 9 ? 0.00525f : 0.00555f;
    float half_axial = i == 9 ? 0.00375f : 0.00405f;
    node[i] = (fnode_t){
      .p = v3_add(descriptor->origin,
                   v3_scale(descriptor->normal, normal_offset[i])),
      .col = {{1, 1, 1}}, .bcol = {{1, 1, 1}},
      // The fixed hint is axial, so rx spans axial and rz spans tangent.
      .rx = half_axial, .rz = half_tangent,
    };
  }
  range[0] = g_scene_floats;
  fig_hand_chain(node, 10, 12, descriptor->axial);
  range[1] = g_scene_floats;
}

static void vmtrig_fire_f1c_emit_disconnected_fixture(int range[2]) {
  v3 tetra[4] = {
    {{-0.205f,-0.005f,-0.005f}}, {{-0.195f,-0.005f,-0.005f}},
    {{-0.200f, 0.005f,-0.005f}}, {{-0.200f, 0.000f, 0.005f}},
  };
  static const int tetra_face[4][3] = {
    {0,2,1}, {0,1,3}, {1,2,3}, {2,0,3},
  };
  v3 torus[3][3];
  for (int u = 0; u < 3; u++)
    for (int v = 0; v < 3; v++) {
      float pu = F_TAU * (float)u / 3.0f;
      float pv = F_TAU * (float)v / 3.0f;
      float radial = 0.020f + 0.005f * cosf(pv);
      torus[u][v] = (v3){{0.200f + radial * cosf(pu),
                           radial * sinf(pu), 0.005f * sinf(pv)}};
    }
  range[0] = g_scene_floats;
  for (int i = 0; i < 4; i++)
    vmtrig_exact_cert_self_tri(tetra[tetra_face[i][0]],
                                tetra[tetra_face[i][1]],
                                tetra[tetra_face[i][2]]);
  for (int u = 0; u < 3; u++)
    for (int v = 0; v < 3; v++) {
      v3 a = torus[u][v];
      v3 b = torus[(u + 1) % 3][v];
      v3 c = torus[(u + 1) % 3][(v + 1) % 3];
      v3 d = torus[u][(v + 1) % 3];
      vmtrig_exact_cert_self_tri(a, b, c);
      vmtrig_exact_cert_self_tri(a, c, d);
    }
  range[1] = g_scene_floats;
}

static int vmtrig_fire_f1c_build_topology_fixture(
    gun_mesh_set_t *set, fig_contact_fire_f1c_t descriptor[3],
    int hand_range[3][2]) {
  if (!vmtrig_fire_f1c_scene_rewind()) return 0;
  g_fig_diss = 0.0f;
  g_fig_k = 12;
  g_cham = 0.0007f;
  fnode_t body[3] = {
    {.p = {{0, 0, -0.040f}}, .col = {{1,1,1}}, .bcol = {{1,1,1}},
     .rx = 0.01265f, .rz = 0.020f},
    {.p = {{0, 0,  0.000f}}, .col = {{1,1,1}}, .bcol = {{1,1,1}},
     .rx = 0.01265f, .rz = 0.020f},
    {.p = {{0, 0,  0.040f}}, .col = {{1,1,1}}, .bcol = {{1,1,1}},
     .rx = 0.01265f, .rz = 0.020f},
  };
  set->count = 4;
  set->range[0] = (gun_mesh_range_t){
    .v0 = g_scene_floats, .surface = 1,
    .part = GUN_MESH_PART_GRIP_BODY,
  };
  fig_chain(body, 3, 12, 1, 1, (v3){{1, 0, 0}});
  set->range[0].v1 = g_scene_floats;
  for (int section = 0; section < 3; section++) {
    v3 origin = {{0.040f * (float)section, -0.025f, 0.020f}};
    v3 normal = {{0, -1, 0}}, tangent = {{1, 0, 0}};
    v3 axial = {{0, 0, 1}};
    set->range[section + 1] = (gun_mesh_range_t){
      .v0 = g_scene_floats, .surface = 1,
      .part = GUN_MESH_PART_GRIP_PANEL,
      .host_mask = GUN_MESH_PART_GRIP_BODY,
    };
    scene_box(v3_add(origin, v3_scale(normal, -0.003f)),
              v3_scale(normal, 0.003f), v3_scale(tangent, 0.0067f),
              v3_scale(axial, 0.0052f), (v3){{1, 1, 1}});
    set->range[section + 1].v1 = g_scene_floats;
    descriptor[section] = vmtrig_fire_f1c_descriptor(WPN_AR, section);
    descriptor[section].origin = origin;
    descriptor[section].normal = normal;
    descriptor[section].tangent = tangent;
    descriptor[section].axial = axial;
  }
  g_cham = 0.0f;
  g_fig_contact_patch_n = 0;
  g_fig_contact_patch_overflow = 0;
  g_fig_contact_fire_f1c_n = 0;
  g_fig_contact_fire_f1c_overflow = 0;
  for (int section = 0; section < 3; section++) {
    vmtrig_fire_f1c_emit_hand(&descriptor[section], hand_range[section]);
    descriptor[section].hand_v0 = hand_range[section][0];
    descriptor[section].hand_v1 = hand_range[section][1];
    g_fig_contact_patch[g_fig_contact_patch_n++] = (fig_contact_patch_t){
      .name = "grip_digits", .hand = FIG_HAND_FIRE,
      .v0 = hand_range[section][0], .v1 = hand_range[section][1], .n = 9,
    };
    fig_contact_fire_f1c_add(descriptor[section]);
  }
  return (set->range[0].v1 - set->range[0].v0) /
           (3 * VERT_FLOATS) == 120;
}

static void vmtrig_fire_f1c_rect_ring(
    const fig_contact_fire_f1c_t *descriptor, float depth,
    float half_u, float half_v, v3 ring[12]) {
  const float uv[12][2] = {
    {-1.0f,-1.0f}, {-0.333333333f,-1.0f}, {0.333333333f,-1.0f},
    { 1.0f,-1.0f}, { 1.0f,-0.333333333f}, {1.0f,0.333333333f},
    { 1.0f, 1.0f}, { 0.333333333f, 1.0f}, {-0.333333333f,1.0f},
    {-1.0f, 1.0f}, {-1.0f, 0.333333333f}, {-1.0f,-0.333333333f},
  };
  for (int i = 0; i < 12; i++)
    ring[i] = v3_add(descriptor->origin,
      v3_add(v3_scale(descriptor->normal, -depth),
      v3_add(v3_scale(descriptor->tangent, uv[i][0] * half_u),
             v3_scale(descriptor->axial, uv[i][1] * half_v))));
}

static int vmtrig_fire_f1c_frustum_bite(
    const fig_contact_fire_f1c_t *descriptor,
    vmtrig_fire_f1c_bite_t *bite, vmtrig_fire_f1c_ctx_t *ctx) {
  v3 h[12], d[12];
  vmtrig_fire_f1c_rect_ring(descriptor, -0.0001f, 0.00555f, 0.00405f, h);
  vmtrig_fire_f1c_rect_ring(descriptor,  0.0036f, 0.00525f, 0.00375f, d);
  for (int i = 0; i < 12; i++) {
    int j = (i + 1) % 12;
    v3 tri[2][3] = {{h[i], d[i], d[j]}, {h[i], d[j], h[j]}};
    for (int q = 0; q < 2; q++) {
      v3 n = v3_cross(v3_sub(tri[q][1], tri[q][0]),
                      v3_sub(tri[q][2], tri[q][0]));
      if (v3_dot(n, v3_scale(descriptor->normal, -1.0f)) <= 0.0f) {
        v3 swap = tri[q][1]; tri[q][1] = tri[q][2]; tri[q][2] = swap;
      }
      if (!vmtrig_fire_f1c_bite_geom(
            descriptor, 204 + 2 * i + q,
            tri[q][0], tri[q][1], tri[q][2], bite, ctx)) return 0;
    }
  }
  for (int i = 0; i < 12; i++) {
    int j = (i + 1) % 12;
    v3 tri[3] = {v3_add(descriptor->origin,
                        v3_scale(descriptor->normal, -0.0036f)), d[i], d[j]};
    v3 n = v3_cross(v3_sub(tri[1], tri[0]), v3_sub(tri[2], tri[0]));
    if (v3_dot(n, v3_scale(descriptor->normal, -1.0f)) <= 0.0f) {
      v3 swap = tri[1]; tri[1] = tri[2]; tri[2] = swap;
    }
    if (!vmtrig_fire_f1c_bite_geom(descriptor, 228 + i,
                                   tri[0], tri[1], tri[2], bite, ctx)) return 0;
  }
  return 1;
}

static void vmtrig_fire_f1c_emit_backing_fixture(
    gun_mesh_set_t *set, fig_contact_fire_f1c_t *descriptor,
    float depth) {
  if (!vmtrig_fire_f1c_scene_rewind()) {
    *set = (gun_mesh_set_t){.overflow = 1};
    return;
  }
  *set = (gun_mesh_set_t){.count = 1};
  set->range[0] = (gun_mesh_range_t){
    .v0 = g_scene_floats, .surface = 1,
    .part = GUN_MESH_PART_GRIP_BODY,
  };
  const double boundary[6][2] = {
    {-0.006,-0.0045}, {0.0,-0.0045}, {0.006,-0.0045},
    {0.006,0.0045}, {0.0,0.0045}, {-0.006,0.0045},
  };
  for (int source = 0; source < 120; source++) {
    v3 tri[3];
    if (source >= 62 && source <= 67) {
      int i = source - 62, j = (i + 1) % 6;
      tri[0] = v3_add(descriptor->origin,
                      v3_scale(descriptor->normal, -depth));
      tri[1] = v3_add(tri[0],
        v3_add(v3_scale(descriptor->tangent, (float)boundary[i][0]),
               v3_scale(descriptor->axial, (float)boundary[i][1])));
      tri[2] = v3_add(tri[0],
        v3_add(v3_scale(descriptor->tangent, (float)boundary[j][0]),
               v3_scale(descriptor->axial, (float)boundary[j][1])));
    } else {
      float far = 0.100f + 0.002f * (float)source;
      tri[0] = (v3){{-depth, far, 0.020f}};
      tri[1] = (v3){{-depth, far + 0.0005f, 0.020f}};
      tri[2] = (v3){{-depth, far, 0.0205f}};
    }
    vmtrig_exact_cert_self_tri(tri[0], tri[1], tri[2]);
  }
  set->range[0].v1 = g_scene_floats;
  descriptor->hand_v0 = g_scene_floats;
  for (int source = 0; source < descriptor->hand_source_count; source++) {
    float far = 0.400f + 0.001f * (float)source;
    vmtrig_exact_cert_self_tri(
      (v3){{0.300f,far,0.300f}},
      (v3){{0.300f,far + 0.0004f,0.300f}},
      (v3){{0.300f,far,0.3004f}});
  }
  descriptor->hand_v1 = g_scene_floats;
}

static void vmtrig_fire_f1c_scene_tri_set(int f, const v3 tri[3]) {
  for (int i = 0; i < 3; i++) {
    g_scene[f + i * VERT_FLOATS] = tri[i].x;
    g_scene[f + i * VERT_FLOATS + 1] = tri[i].y;
    g_scene[f + i * VERT_FLOATS + 2] = tri[i].z;
  }
}

static void vmtrig_fire_f1c_scene_tri_get(int f, v3 tri[3]) {
  tri[0] = SCENE_V(f);
  tri[1] = SCENE_V(f + VERT_FLOATS);
  tri[2] = SCENE_V(f + 2 * VERT_FLOATS);
}

static int vmtrig_fire_f1c_hand_translate(
    const fig_contact_fire_f1c_t *descriptor, v3 delta) {
  if (!descriptor || vmtrig_v3_nonfinite(delta) ||
      descriptor->hand_v0 < 0 || descriptor->hand_v1 <= descriptor->hand_v0 ||
      descriptor->hand_v1 > g_scene_floats ||
      descriptor->hand_v0 % VERT_FLOATS ||
      descriptor->hand_v1 % VERT_FLOATS) return 0;
  for (int f = descriptor->hand_v0; f < descriptor->hand_v1;
       f += VERT_FLOATS) {
    v3 moved = v3_add(SCENE_V(f), delta);
    if (vmtrig_v3_nonfinite(moved)) return 0;
    g_scene[f] = moved.x;
    g_scene[f + 1] = moved.y;
    g_scene[f + 2] = moved.z;
  }
  return 1;
}

static int vmtrig_fire_f1c_source_offset(
    const gun_mesh_set_t *set, int range, int source) {
  return set->range[range].v0 + source * 3 * VERT_FLOATS;
}

static void vmtrig_fire_f1c_emit_cross_fixture(
    gun_mesh_set_t *set, fig_contact_fire_f1c_t *descriptor) {
  if (!vmtrig_fire_f1c_scene_rewind()) {
    *set = (gun_mesh_set_t){.overflow = 1};
    *descriptor = (fig_contact_fire_f1c_t){0};
    return;
  }
  g_fig_diss = 0.0f;
  g_fig_k = 12;
  g_cham = 0.0007f;
  *set = (gun_mesh_set_t){.count = 4};
  set->range[0] = (gun_mesh_range_t){
    .v0 = g_scene_floats, .surface = 1,
    .part = GUN_MESH_PART_GRIP_BODY,
  };
  fnode_t body[3] = {
    {.p = {{0, 0, -0.040f}}, .col = {{1,1,1}}, .bcol = {{1,1,1}},
     .rx = 0.01265f, .rz = 0.014f},
    {.p = {{0, 0,  0.000f}}, .col = {{1,1,1}}, .bcol = {{1,1,1}},
     .rx = 0.01265f, .rz = 0.014f},
    {.p = {{0, 0,  0.040f}}, .col = {{1,1,1}}, .bcol = {{1,1,1}},
     .rx = 0.01265f, .rz = 0.014f},
  };
  fig_chain(body, 3, 12, 1, 1, (v3){{1, 0, 0}});
  set->range[0].v1 = g_scene_floats;
  // Proof-only asymmetric support witness: move two complete q0 vertex
  // equivalence classes, never individual face occurrences.  Their deep ends
  // stay axially outside the certified top while the closed 62/180/120 mesh
  // exposes exactly the frozen f0 panel/BODY seam sources 62..66.
  v3 seam_vertex[2] = {
    SCENE_V(set->range[0].v0 + 62 * 3 * VERT_FLOATS + 2 * VERT_FLOATS),
    SCENE_V(set->range[0].v0 + 64 * 3 * VERT_FLOATS + 2 * VERT_FLOATS),
  };
  int seam_vertex_matches[2] = {0};
  for (int f = set->range[0].v0; f < set->range[0].v1; f += VERT_FLOATS) {
    v3 p = SCENE_V(f);
    for (int i = 0; i < 2; i++)
      if (p.x == seam_vertex[i].x && p.y == seam_vertex[i].y &&
          p.z == seam_vertex[i].z) {
        g_scene[f + 1] = -0.0248f;
        seam_vertex_matches[i]++;
      }
    }
  if (seam_vertex_matches[0] < 3 || seam_vertex_matches[1] < 3)
    set->overflow = 1;
  for (int q = 1; q < 4; q++) {
    v3 origin = {{0.040f * (float)(q - 1), -0.025f, 0.020f}};
    v3 normal = {{0, -1, 0}}, tangent = {{1, 0, 0}};
    v3 axial = {{0, 0, 1}};
    set->range[q] = (gun_mesh_range_t){
      .v0 = g_scene_floats, .surface = 1,
      .part = GUN_MESH_PART_GRIP_PANEL,
      .host_mask = GUN_MESH_PART_GRIP_BODY,
    };
    scene_box(v3_add(origin, v3_scale(normal, -0.003f)),
              v3_scale(normal, 0.003f), v3_scale(tangent, 0.0067f),
              v3_scale(axial, 0.0052f), (v3){{1, 1, 1}});
    set->range[q].v1 = g_scene_floats;
  }
  g_cham = 0.0f;
  g_fig_contact_patch_n = 0;
  g_fig_contact_patch_overflow = 0;
  g_fig_contact_fire_f1c_n = 0;
  g_fig_contact_fire_f1c_overflow = 0;
  for (int section = 0; section < 3; section++) {
    fig_contact_fire_f1c_t current =
      vmtrig_fire_f1c_descriptor(WPN_AR, section);
    current.origin = (v3){{0.040f * (float)section, -0.025f, 0.020f}};
    current.normal = (v3){{0, -1, 0}};
    current.tangent = (v3){{1, 0, 0}};
    current.axial = (v3){{0, 0, 1}};
    int hand_range[2];
    vmtrig_fire_f1c_emit_hand(&current, hand_range);
    current.hand_v0 = hand_range[0];
    current.hand_v1 = hand_range[1];
    g_fig_contact_patch[g_fig_contact_patch_n++] = (fig_contact_patch_t){
      .name = "grip_digits", .hand = FIG_HAND_FIRE,
      .v0 = current.hand_v0, .v1 = current.hand_v1, .n = 9,
    };
    fig_contact_fire_f1c_add(current);
  }
  *descriptor = g_fig_contact_fire_f1c[0];
}

static int vmtrig_fire_f1c_emit_foreign_fixture(
    int foreign_v0[GUN_CONTACT_COUNT],
    fig_contact_fire_f1c_t **descriptor) {
  gun_mesh_set_t grip = {0};
  fig_contact_fire_f1c_t local[3];
  int hand_range[3][2];
  if (!vmtrig_fire_f1c_build_topology_fixture(
        &grip, local, hand_range)) return 0;
  for (int kind = 0; kind < GUN_CONTACT_COUNT; kind++) {
    g_gun_contact_mesh[kind] = (gun_mesh_set_t){0};
    foreign_v0[kind] = -1;
  }
  g_gun_contact_mesh[GUN_CONTACT_GRIP] = grip;
  float fixture_cham = g_cham;
  g_cham = 0.0f;
  for (int kind = GUN_CONTACT_FORE; kind < GUN_CONTACT_COUNT; kind++) {
    gun_mesh_set_t *set = &g_gun_contact_mesh[kind];
    set->count = 1;
    uint32_t part = kind == GUN_CONTACT_FORE ? GUN_MESH_PART_FORE_BODY
                  : kind == GUN_CONTACT_MAG ? GUN_MESH_PART_MAG_BODY : 0;
    set->range[0] = (gun_mesh_range_t){
      .v0 = g_scene_floats, .surface = 1, .part = part,
    };
    foreign_v0[kind] = g_scene_floats;
    float x = 0.5f + 0.03125f * (float)kind;
    scene_box((v3){{x, 0.625f, 0.5f}},
              (v3){{0x1p-10f, 0, 0}},
              (v3){{0, 0x1p-10f, 0}},
              (v3){{0, 0, 0x1p-10f}}, (v3){{1, 1, 1}});
    set->range[0].v1 = g_scene_floats;
  }
  g_cham = fixture_cham;
  for (int i = 0; i < 4; i++)
    g_gun_guard[i] = (v3){{0.75f,
      0.75f + 0.015625f * (float)i, 0.75f}};
  for (int i = 0; i < 3; i++)
    g_gun_blade[i] = (v3){{0.875f,
      0.875f + 0.015625f * (float)i, 0.875f}};
  g_gun_guard_r = 0x1p-10f;
  g_gun_blade_r = 0x1p-10f;
  g_gun_contact_mesh_capture = 1;
  g_gun_contact_mesh_active = -1;
  g_gun_contact_mesh_depth = 0;
  g_gun_contact_mesh_ignored = 0;
  g_gun_contact_surface_active = 0;
  g_gun_contact_mesh_unclassified = 0;
  g_gun_contact_mesh_scope_overflow = 0;
  g_gun_contact_mesh_part = 0;
  g_gun_contact_mesh_host_mask = 0;
  *descriptor = &g_fig_contact_fire_f1c[0];
  return 1;
}

enum {
  VMTRIG_FIRE_F1C_FIX_COMPLETE,
  VMTRIG_FIRE_F1C_FIX_PANEL5,
  VMTRIG_FIRE_F1C_FIX_PANEL8,
  VMTRIG_FIRE_F1C_FIX_HAND203,
  VMTRIG_FIRE_F1C_FIX_WRONG_RANGE,
  VMTRIG_FIRE_F1C_FIX_WRONG_CHILD,
  VMTRIG_FIRE_F1C_FIX_WRONG_SECTION,
  VMTRIG_FIRE_F1C_FIX_PARTIAL_TOP,
  VMTRIG_FIRE_F1C_FIX_MISSING_TOP,
  VMTRIG_FIRE_F1C_FIX_MIXED,
  VMTRIG_FIRE_F1C_FIX_BODY_TIE,
  VMTRIG_FIRE_F1C_FIX_DUPLICATE,
  VMTRIG_FIRE_F1C_FIX_Q0_DUPLICATE,
  VMTRIG_FIRE_F1C_FIX_PANEL_DUPLICATE,
  VMTRIG_FIRE_F1C_FIX_PATCH_COUNT,
  VMTRIG_FIRE_F1C_FIX_Q0_FINITE_MAX,
  VMTRIG_FIRE_F1C_FIX_PANEL_FINITE_MAX,
  VMTRIG_FIRE_F1C_FIX_HAND_FINITE_MAX,
  VMTRIG_FIRE_F1C_FIX_OPEN,
  VMTRIG_FIRE_F1C_FIX_COPLANAR,
  VMTRIG_FIRE_F1C_FIX_NEAR_PARALLEL,
  VMTRIG_FIRE_F1C_FIX_SHARED_EDGE,
  VMTRIG_FIRE_F1C_FIX_ZERO_LENGTH,
  VMTRIG_FIRE_F1C_FIX_POINT_ONLY,
  VMTRIG_FIRE_F1C_FIX_FOOTPRINT,
  VMTRIG_FIRE_F1C_FIX_NAN,
  VMTRIG_FIRE_F1C_FIX_INF,
  VMTRIG_FIRE_F1C_FIX_WORK_CAP,
  VMTRIG_FIRE_F1C_FIX_SEAM_OUTSIDER,
  VMTRIG_FIRE_F1C_FIX_SEAM_MISSING,
  VMTRIG_FIRE_F1C_FIX_RANGE_INTMAX,
};

static int vmtrig_fire_f1c_cross_fixture_run(
    int variant, vmtrig_fire_f1c_cross_t *result,
    vmtrig_fire_f1c_ctx_t *final_ctx) {
  gun_mesh_set_t set;
  fig_contact_fire_f1c_t emitted;
  vmtrig_fire_f1c_emit_cross_fixture(&set, &emitted);
  fig_contact_fire_f1c_t *descriptor = &g_fig_contact_fire_f1c[0];
  int q = descriptor->feature_range;
  int top6 = vmtrig_fire_f1c_source_offset(&set, q, 6);
  int hand204 = descriptor->hand_v0 + 204 * 3 * VERT_FLOATS;
  v3 top_tri[3], hand_tri[3];
  vmtrig_fire_f1c_scene_tri_get(top6, top_tri);
  vmtrig_fire_f1c_scene_tri_get(hand204, hand_tri);
  if (variant == VMTRIG_FIRE_F1C_FIX_PANEL5 ||
      variant == VMTRIG_FIRE_F1C_FIX_PANEL8 ||
      variant == VMTRIG_FIRE_F1C_FIX_MIXED) {
    int source = variant == VMTRIG_FIRE_F1C_FIX_PANEL5 ? 5
               : variant == VMTRIG_FIRE_F1C_FIX_PANEL8 ? 8 : 13;
    vmtrig_fire_f1c_scene_tri_set(
      vmtrig_fire_f1c_source_offset(&set, q, source), top_tri);
  } else if (variant == VMTRIG_FIRE_F1C_FIX_HAND203) {
    vmtrig_fire_f1c_scene_tri_set(
      descriptor->hand_v0 + 203 * 3 * VERT_FLOATS, hand_tri);
  } else if (variant == VMTRIG_FIRE_F1C_FIX_WRONG_RANGE) {
    vmtrig_fire_f1c_scene_tri_set(
      vmtrig_fire_f1c_source_offset(&set, 2, 6), top_tri);
  } else if (variant == VMTRIG_FIRE_F1C_FIX_WRONG_CHILD) {
    descriptor->hand_v0 -= 3 * VERT_FLOATS;
    descriptor->hand_v1 -= 3 * VERT_FLOATS;
  } else if (variant == VMTRIG_FIRE_F1C_FIX_WRONG_SECTION) {
    descriptor->section = 1;
  } else if (variant == VMTRIG_FIRE_F1C_FIX_PARTIAL_TOP) {
    int top7 = vmtrig_fire_f1c_source_offset(&set, q, 7);
    v3 tri[3];
    vmtrig_fire_f1c_scene_tri_get(top7, tri);
    tri[1].y = 0.005f;
    vmtrig_fire_f1c_scene_tri_set(top7, tri);
  } else if (variant == VMTRIG_FIRE_F1C_FIX_MISSING_TOP) {
    const v3 far[3] = {
      {{0.150f,0.600f,0.250f}}, {{0.150f,0.6002f,0.250f}},
      {{0.150f,0.600f,0.2502f}},
    };
    vmtrig_fire_f1c_scene_tri_set(
      vmtrig_fire_f1c_source_offset(&set, q, 7), far);
  } else if (variant == VMTRIG_FIRE_F1C_FIX_BODY_TIE) {
    vmtrig_fire_f1c_scene_tri_set(
      vmtrig_fire_f1c_source_offset(&set, 0, 0), top_tri);
  } else if (variant == VMTRIG_FIRE_F1C_FIX_DUPLICATE) {
    vmtrig_fire_f1c_scene_tri_set(
      descriptor->hand_v0 + 205 * 3 * VERT_FLOATS, hand_tri);
  } else if (variant == VMTRIG_FIRE_F1C_FIX_Q0_DUPLICATE) {
    v3 q0_tri[3];
    vmtrig_fire_f1c_scene_tri_get(
      vmtrig_fire_f1c_source_offset(&set, 0, 0), q0_tri);
    vmtrig_fire_f1c_scene_tri_set(
      vmtrig_fire_f1c_source_offset(&set, 0, 1), q0_tri);
  } else if (variant == VMTRIG_FIRE_F1C_FIX_PANEL_DUPLICATE) {
    v3 panel_tri[3];
    vmtrig_fire_f1c_scene_tri_get(
      vmtrig_fire_f1c_source_offset(&set, q, 0), panel_tri);
    vmtrig_fire_f1c_scene_tri_set(
      vmtrig_fire_f1c_source_offset(&set, q, 1), panel_tri);
  } else if (variant == VMTRIG_FIRE_F1C_FIX_PATCH_COUNT) {
    g_fig_contact_patch_n = FIG_CONTACT_PATCH_MAX + 1;
    g_fig_contact_patch_overflow = 0;
  } else if (variant == VMTRIG_FIRE_F1C_FIX_Q0_FINITE_MAX ||
             variant == VMTRIG_FIRE_F1C_FIX_PANEL_FINITE_MAX ||
             variant == VMTRIG_FIRE_F1C_FIX_HAND_FINITE_MAX) {
    const float huge = 0x1.fffffep127f;
    const v3 finite_tri[3] = {
      {{huge, huge, 0}}, {{-huge, huge, 1}}, {{huge, -huge, -1}},
    };
    int at = variant == VMTRIG_FIRE_F1C_FIX_Q0_FINITE_MAX
           ? vmtrig_fire_f1c_source_offset(&set, 0, 0)
           : variant == VMTRIG_FIRE_F1C_FIX_PANEL_FINITE_MAX
           ? vmtrig_fire_f1c_source_offset(&set, q, 0) : hand204;
    vmtrig_fire_f1c_scene_tri_set(at, finite_tri);
  } else if (variant == VMTRIG_FIRE_F1C_FIX_OPEN) {
    float limit = descriptor->half_tangent - descriptor->foreign_radius -
                  descriptor->profile_epsilon;
    float shift = limit - 0.0015f;
    const v3 tri[3] = {
      {{-0.002f,shift,0}}, {{0.002f,shift - 0.003f,0}},
      {{0.002f,shift + 0.003f,0}},
    };
    vmtrig_fire_f1c_scene_tri_set(hand204, tri);
  } else if (variant == VMTRIG_FIRE_F1C_FIX_COPLANAR) {
    vmtrig_fire_f1c_scene_tri_set(hand204, top_tri);
  } else if (variant == VMTRIG_FIRE_F1C_FIX_NEAR_PARALLEL) {
    const v3 tri[3] = {
      {{-0.00000000015f,-0.003f,-0.002f}},
      {{-0.00000000015f,-0.003f, 0.002f}},
      {{ 0.00000000015f, 0.003f, 0.000f}},
    };
    vmtrig_fire_f1c_scene_tri_set(hand204, tri);
  } else if (variant == VMTRIG_FIRE_F1C_FIX_SHARED_EDGE) {
    const v3 tri[3] = {top_tri[0], top_tri[1], {{0.002f,0,0}}};
    vmtrig_fire_f1c_scene_tri_set(hand204, tri);
  } else if (variant == VMTRIG_FIRE_F1C_FIX_ZERO_LENGTH) {
    const v3 tri[3] = {hand_tri[0], hand_tri[0], hand_tri[2]};
    vmtrig_fire_f1c_scene_tri_set(hand204, tri);
  } else if (variant == VMTRIG_FIRE_F1C_FIX_POINT_ONLY) {
    const v3 tri[3] = {
      {{-0.002f,0,0.0045f}}, {{0.002f,0,0.0045f}},
      {{0.002f,0.003f,0.0045f}},
    };
    vmtrig_fire_f1c_scene_tri_set(hand204, tri);
  } else if (variant == VMTRIG_FIRE_F1C_FIX_FOOTPRINT) {
    const v3 tri[3] = {
      {{-0.002f,0.0059f,0}}, {{0.002f,0.0029f,0}},
      {{0.002f,0.0089f,0}},
    };
    vmtrig_fire_f1c_scene_tri_set(hand204, tri);
  } else if (variant == VMTRIG_FIRE_F1C_FIX_NAN ||
             variant == VMTRIG_FIRE_F1C_FIX_INF) {
    hand_tri[0].x = variant == VMTRIG_FIRE_F1C_FIX_NAN ? NAN : INFINITY;
    vmtrig_fire_f1c_scene_tri_set(hand204, hand_tri);
  } else if (variant == VMTRIG_FIRE_F1C_FIX_SEAM_OUTSIDER) {
    float u = (-0.006f - 0.006f) / 3.0f;
    float v = (0.0045f - 0.0045f) / 3.0f;
    const v3 tri[3] = {
      {{-0.006f,u - 0.00002f,v - 0.00002f}},
      {{-0.006f,u + 0.00002f,v - 0.00002f}}, {{0,u,v + 0.00002f}},
    };
    vmtrig_fire_f1c_scene_tri_set(
      vmtrig_fire_f1c_source_offset(&set, q, 13), tri);
  } else if (variant == VMTRIG_FIRE_F1C_FIX_SEAM_MISSING) {
    const v3 far[3] = {
      {{0.150f,0.610f,0.250f}}, {{0.150f,0.6102f,0.250f}},
      {{0.150f,0.610f,0.2502f}},
    };
    vmtrig_fire_f1c_scene_tri_set(
      vmtrig_fire_f1c_source_offset(&set, q, 12), far);
  } else if (variant == VMTRIG_FIRE_F1C_FIX_RANGE_INTMAX) {
    set.range[q].v1 = INT_MAX;
  }
  vmtrig_fire_f1c_ctx_t ctx = {
    .limit = variant == VMTRIG_FIRE_F1C_FIX_WORK_CAP
           ? 0 : VMTRIG_FIRE_F1C_WORK_LIMIT,
  };
  vmtrig_fire_f1c_cross_t local = {0};
  g_gun_contact_mesh[GUN_CONTACT_GRIP] = set;
  int ok = vmtrig_fire_f1c_cross_scan(descriptor, &local, &ctx);
  if (result) *result = local;
  if (final_ctx) *final_ctx = ctx;
  return ok;
}

static int vmtrig_fire_f1c_selftest(int *case_count, int *pass_count,
                                     const char **first_fail) {
  vmtrig_fire_f1c_test_t test = {0};
  float saved_cham = g_cham, saved_diss = g_fig_diss;
  int saved_k = g_fig_k, saved_probe = g_probe_on;
  int saved_scene_floats = g_scene_floats;
  int saved_scene_want = g_scene_want, saved_scene_peak = g_scene_peak;
  int saved_scene_dirty = g_scene_dirty, saved_nschunk = g_nschunk;
  int saved_schunk_open = g_schunk_open;
  int saved_scene_cast_floats = g_scene_cast_floats;
  int saved_fire_scene_base = vmtrig_fire_f1c_scene_base;
  long saved_scene_drops = g_scene_drops;
  float saved_ao_mul = g_ao_mul, saved_mat_rough = g_mat_rough;
  float saved_mat_metal = g_mat_metal;
  int saved_prof_ready = g_prof_ready, saved_nprobe = g_nprobe;
  uint64_t saved_inside_ambiguous = g_vmtrig_inside_ambiguous;
  static schunk_t saved_schunk[SCENE_MAX_CHUNKS];
  static schunk_t preloaded_schunk[SCENE_MAX_CHUNKS];
  static probe_t saved_probe_registry[PROBE_MAX];
  static probe_t preloaded_probe_registry[PROBE_MAX];
  static float saved_prof[6][FIG_KMAX][2];
  static float preloaded_prof[6][FIG_KMAX][2];
  int scene_state_valid = vmtrig_fire_f1c_scene_entry_valid(
    saved_scene_floats, saved_scene_want);
  memcpy(saved_schunk, g_schunk, sizeof saved_schunk);
  memcpy(saved_probe_registry, g_probe, sizeof saved_probe_registry);
  memcpy(saved_prof, g_prof, sizeof saved_prof);
  static fig_contact_patch_t original_patch[FIG_CONTACT_PATCH_MAX];
  static fig_contact_patch_t preloaded_patch[FIG_CONTACT_PATCH_MAX];
  static fig_contact_fire_f1c_t
    original_fire[FIG_CONTACT_FIRE_F1C_MAX];
  static fig_contact_fire_f1c_t
    preloaded_fire[FIG_CONTACT_FIRE_F1C_MAX];
  int original_patch_n = g_fig_contact_patch_n;
  int original_patch_overflow = g_fig_contact_patch_overflow;
  int original_fire_n = g_fig_contact_fire_f1c_n;
  int original_fire_overflow = g_fig_contact_fire_f1c_overflow;
  memcpy(original_patch, g_fig_contact_patch, sizeof original_patch);
  memcpy(original_fire, g_fig_contact_fire_f1c, sizeof original_fire);
  int original_mesh_stack[GUN_CONTACT_SCOPE_MAX];
  int preloaded_mesh_stack[GUN_CONTACT_SCOPE_MAX];
  memcpy(original_mesh_stack, g_gun_contact_mesh_stack,
         sizeof original_mesh_stack);
  static gun_mesh_set_t saved_contact_mesh[GUN_CONTACT_COUNT];
  static gun_mesh_set_t preloaded_contact_mesh[GUN_CONTACT_COUNT];
  memcpy(saved_contact_mesh, g_gun_contact_mesh,
         sizeof saved_contact_mesh);
  v3 saved_guard[4], saved_blade[3];
  memcpy(saved_guard, g_gun_guard, sizeof saved_guard);
  memcpy(saved_blade, g_gun_blade, sizeof saved_blade);
  float saved_guard_r = g_gun_guard_r, saved_blade_r = g_gun_blade_r;
  int saved_mesh_capture = g_gun_contact_mesh_capture;
  int saved_mesh_active = g_gun_contact_mesh_active;
  int saved_mesh_depth = g_gun_contact_mesh_depth;
  int saved_mesh_ignored = g_gun_contact_mesh_ignored;
  int saved_surface_active = g_gun_contact_surface_active;
  int saved_unclassified = g_gun_contact_mesh_unclassified;
  int saved_scope_overflow = g_gun_contact_mesh_scope_overflow;
  uint32_t saved_mesh_part = g_gun_contact_mesh_part;
  uint32_t saved_mesh_host = g_gun_contact_mesh_host_mask;
  int original_scene_canary_f[2] = {-1, -1};
  float original_scene_canary[2][3 * VERT_FLOATS] = {{0}};
  int original_scene_canary_n = 0;
  if (scene_state_valid && saved_scene_floats >= 3 * VERT_FLOATS) {
    original_scene_canary_f[original_scene_canary_n] = 0;
    memcpy(original_scene_canary[original_scene_canary_n++], g_scene,
           3 * VERT_FLOATS * sizeof(float));
    if (saved_scene_floats > 3 * VERT_FLOATS) {
      original_scene_canary_f[original_scene_canary_n] =
        saved_scene_floats - 3 * VERT_FLOATS;
      memcpy(original_scene_canary[original_scene_canary_n++],
             &g_scene[saved_scene_floats - 3 * VERT_FLOATS],
             3 * VERT_FLOATS * sizeof(float));
    }
  }

  // Entry validation is pure and precedes every Scene or registry mutation.
  // These boundary cases exercise the same fail-closed predicate without
  // placing the live Scene near its cap or making want/floats incoherent.
  const int scene_capacity = SCENE_MAX_VERTS * VERT_FLOATS;
  int near_cap = (scene_capacity - VMTRIG_FIRE_F1C_SCENE_TAIL_FLOATS) /
    (3 * VERT_FLOATS) * (3 * VERT_FLOATS) + 3 * VERT_FLOATS;
  vmtrig_fire_f1c_case(&test, "state_entry_near_cap",
    !vmtrig_fire_f1c_scene_entry_valid(near_cap, near_cap) &&
    g_scene_floats == saved_scene_floats &&
    g_scene_want == saved_scene_want);
  vmtrig_fire_f1c_case(&test, "state_entry_want_mismatch",
    !vmtrig_fire_f1c_scene_entry_valid(
      saved_scene_floats, saved_scene_floats + VERT_FLOATS) &&
    g_scene_floats == saved_scene_floats &&
    g_scene_want == saved_scene_want);
  vmtrig_fire_f1c_case(&test, "state_entry_alignment",
    !vmtrig_fire_f1c_scene_entry_valid(
      saved_scene_floats + VERT_FLOATS,
      saved_scene_floats + VERT_FLOATS) &&
    g_scene_floats == saved_scene_floats &&
    g_scene_want == saved_scene_want);
  vmtrig_fire_f1c_case(&test, "state_prefix_capacity", scene_state_valid);
  vmtrig_fire_f1c_case(&test, "state_drops_canary_collision",
    vmtrig_fire_f1c_scene_drops_canary(19L) == 20L &&
    vmtrig_fire_f1c_scene_drops_canary(20L) == 19L &&
    vmtrig_fire_f1c_scene_drops_canary(19L) != 19L &&
    vmtrig_fire_f1c_scene_drops_canary(20L) != 20L);
  if (!scene_state_valid) goto restore_original;

  // A real closed prefix triangle and a range which references it make dangling
  // restore errors observable.  Every FIRE fixture starts strictly after this mark.
  int sentinel_v0 = g_scene_floats;
  if (scene_state_valid)
    vmtrig_exact_cert_self_tri(
      (v3){{0.03125f, 0.0625f, 0.09375f}},
      (v3){{0.03125f, 0.064453125f, 0.09375f}},
      (v3){{0.03125f, 0.0625f, 0.095703125f}});
  int sentinel_v1 = g_scene_floats;
  float sentinel_scene[3 * VERT_FLOATS] = {0};
  if (scene_state_valid)
    memcpy(sentinel_scene, &g_scene[sentinel_v0], sizeof sentinel_scene);

  // Deliberately non-default but valid preloaded state.  Rewind/builders must
  // perturb these channels; the common restore below must recover every byte.
  g_scene_peak = sentinel_v1 + 4096;
  g_scene_dirty = 7;
  // A caller may legitimately enter with either ordinary canary value.  Select
  // the other one so the mutation witness can never collide with saved state.
  g_scene_drops = vmtrig_fire_f1c_scene_drops_canary(saved_scene_drops);
  memset(g_schunk, 0, sizeof g_schunk);
  g_schunk[0] = (schunk_t){sentinel_v0, sentinel_v1,
                            (v3){{0.125f, 0.25f, 0.5f}}, 0.75f};
  g_nschunk = 1;
  g_schunk_open = 0;
  g_scene_cast_floats = sentinel_v0;
  g_ao_mul = 0.75f;
  g_mat_rough = 0.625f;
  g_mat_metal = 0.25f;
  fig_prof_init();
  g_prof_ready = 1;
  memset(g_probe, 0, sizeof g_probe);
  g_probe[0] = (probe_t){"fire_f1c_state_canary", sentinel_v0, sentinel_v1};
  g_nprobe = 1;
  g_probe_on = 1;
  g_vmtrig_inside_ambiguous = UINT64_C(37);
  g_cham = 0.0009765625f;
  g_fig_diss = 0.125f;
  g_fig_k = 7;
  int sentinel_scene_want = g_scene_want;
  int sentinel_scene_peak = g_scene_peak;
  int sentinel_scene_dirty = g_scene_dirty;
  int sentinel_nschunk = g_nschunk, sentinel_schunk_open = g_schunk_open;
  int sentinel_scene_cast_floats = g_scene_cast_floats;
  long sentinel_scene_drops = g_scene_drops;
  float sentinel_ao_mul = g_ao_mul, sentinel_mat_rough = g_mat_rough;
  float sentinel_mat_metal = g_mat_metal;
  int sentinel_prof_ready = g_prof_ready, sentinel_nprobe = g_nprobe;
  int sentinel_probe_on = g_probe_on;
  uint64_t sentinel_inside_ambiguous = g_vmtrig_inside_ambiguous;
  float sentinel_cham = g_cham, sentinel_diss = g_fig_diss;
  int sentinel_k = g_fig_k;
  memcpy(preloaded_schunk, g_schunk, sizeof preloaded_schunk);
  memcpy(preloaded_probe_registry, g_probe,
         sizeof preloaded_probe_registry);
  memcpy(preloaded_prof, g_prof, sizeof preloaded_prof);
  vmtrig_fire_f1c_scene_base = sentinel_v1;

  // Exercise restoration from a genuinely populated registry rather than
  // relying on the fresh-process all-zero state.
  memset(g_fig_contact_patch, 0, sizeof g_fig_contact_patch);
  memset(g_fig_contact_fire_f1c, 0, sizeof g_fig_contact_fire_f1c);
  g_fig_contact_patch[0] = (fig_contact_patch_t){
    .name = "fire_f1c_state_preload", .hand = FIG_HAND_FIRE,
    .v0 = sentinel_v0, .v1 = sentinel_v1, .n = 1,
  };
  g_fig_contact_patch_n = 1;
  g_fig_contact_patch_overflow = 0;
  g_fig_contact_fire_f1c[0] = (fig_contact_fire_f1c_t){
    .magic = UINT32_C(0x13579bdf), .weapon = WPN_SR, .section = 2,
    .hand_v0 = sentinel_v0, .hand_v1 = sentinel_v1,
  };
  g_fig_contact_fire_f1c_n = 1;
  g_fig_contact_fire_f1c_overflow = 0;
  memset(g_gun_contact_mesh, 0, sizeof g_gun_contact_mesh);
  g_gun_contact_mesh[GUN_CONTACT_FORE].count = 1;
  g_gun_contact_mesh[GUN_CONTACT_FORE].range[0] = (gun_mesh_range_t){
    .v0 = sentinel_v0, .v1 = sentinel_v1, .surface = 1,
    .part = GUN_MESH_PART_FORE_BODY,
  };
  for (int i = 0; i < GUN_CONTACT_SCOPE_MAX; i++)
    g_gun_contact_mesh_stack[i] = 1000 + i;
  memcpy(preloaded_patch, g_fig_contact_patch, sizeof preloaded_patch);
  memcpy(preloaded_fire, g_fig_contact_fire_f1c, sizeof preloaded_fire);
  memcpy(preloaded_mesh_stack, g_gun_contact_mesh_stack,
         sizeof preloaded_mesh_stack);
  memcpy(preloaded_contact_mesh, g_gun_contact_mesh,
         sizeof preloaded_contact_mesh);
  v3 preloaded_guard[4], preloaded_blade[3];
  memcpy(g_gun_guard, vmtrig_fire_f1c_foreign_census_selftest.guard,
         sizeof g_gun_guard);
  memcpy(g_gun_blade, vmtrig_fire_f1c_foreign_census_selftest.blade,
         sizeof g_gun_blade);
  g_gun_guard_r = vmtrig_fire_f1c_foreign_census_selftest.guard_r;
  g_gun_blade_r = vmtrig_fire_f1c_foreign_census_selftest.blade_r;
  memcpy(preloaded_guard, g_gun_guard, sizeof preloaded_guard);
  memcpy(preloaded_blade, g_gun_blade, sizeof preloaded_blade);
  float preloaded_guard_r = g_gun_guard_r;
  float preloaded_blade_r = g_gun_blade_r;
  g_gun_contact_mesh_capture = 1;
  g_gun_contact_mesh_active = -1;
  g_gun_contact_mesh_depth = 0;
  g_gun_contact_mesh_ignored = 0;
  g_gun_contact_surface_active = 0;
  g_gun_contact_mesh_unclassified = 0;
  g_gun_contact_mesh_scope_overflow = 0;
  g_gun_contact_mesh_part = 0;
  g_gun_contact_mesh_host_mask = 0;
  g_fig_contact_patch_n = 0;
  g_fig_contact_patch_overflow = 0;
  g_fig_contact_fire_f1c_n = 0;
  g_fig_contact_fire_f1c_overflow = 0;
  memset(g_gun_contact_mesh_stack, 0, sizeof g_gun_contact_mesh_stack);
  memset(g_gun_contact_mesh, 0, sizeof g_gun_contact_mesh);
  g_probe_on = 1;

  vmtrig_fire_f1c_case(&test, "state_prefix_sentinel",
    sentinel_v1 - sentinel_v0 == 3 * VERT_FLOATS &&
    g_scene_want == sentinel_v1 && g_scene_drops == sentinel_scene_drops &&
    sentinel_scene_drops != saved_scene_drops && sentinel_nschunk == 1 &&
    sentinel_schunk_open == 0 && sentinel_nprobe == 1 && sentinel_probe_on &&
    sentinel_inside_ambiguous == UINT64_C(37));
  vmtrig_fire_f1c_scene_rewind();
  int descriptor_hand_v0 = g_scene_floats;
  for (int source = 0; source < 240; source++) {
    float y = 0.020f + 0.001f * (float)source;
    vmtrig_exact_cert_self_tri(
      (v3){{0.100f,y,0.000f}}, (v3){{0.100f,y + 0.0004f,0.000f}},
      (v3){{0.100f,y,0.0004f}});
  }
  fig_contact_fire_f1c_t descriptor = vmtrig_fire_f1c_descriptor(WPN_AR, 0);
  descriptor.hand_v0 = descriptor_hand_v0;
  descriptor.hand_v1 = g_scene_floats;
  vmtrig_fire_f1c_case(&test, "descriptor_valid",
                        vmtrig_fire_f1c_descriptor_valid(&descriptor));
#define FIRE_BAD_FIELD(NAME, FIELD, VALUE) do { \
    fig_contact_fire_f1c_t bad_ = descriptor; bad_.FIELD = (VALUE); \
    vmtrig_fire_f1c_case(&test, (NAME), \
                         !vmtrig_fire_f1c_descriptor_valid(&bad_)); \
  } while (0)
  FIRE_BAD_FIELD("descriptor_magic", magic, 0);
  FIRE_BAD_FIELD("descriptor_wrong_kind", kind, GUN_CONTACT_FORE);
  FIRE_BAD_FIELD("descriptor_weapon", weapon, WPN_COUNT);
  FIRE_BAD_FIELD("descriptor_range", feature_range, 0);
  FIRE_BAD_FIELD("descriptor_backing_range", backing_range, 1);
  FIRE_BAD_FIELD("descriptor_part", feature_part, GUN_MESH_PART_GRIP_BODY);
  FIRE_BAD_FIELD("descriptor_host", feature_host, 0);
  FIRE_BAD_FIELD("descriptor_section", section, 1);
  FIRE_BAD_FIELD("descriptor_hand", hand, FIG_HAND_SUPPORT);
  FIRE_BAD_FIELD("descriptor_child", hand_v1,
                 239 * 3 * VERT_FLOATS);
  FIRE_BAD_FIELD("descriptor_source_pair", feature_source0, 5);
  FIRE_BAD_FIELD("descriptor_source_count", feature_source_count, 43);
  FIRE_BAD_FIELD("descriptor_hand_source", hand_source_lo, 203);
  FIRE_BAD_FIELD("descriptor_hand_count", hand_source_count, 239);
  FIRE_BAD_FIELD("descriptor_q0_full_outsider", backing_full_hi, 68);
  FIRE_BAD_FIELD("descriptor_q0_band_outsider", backing_zero_hi, 68);
  FIRE_BAD_FIELD("descriptor_q0_seam_outsider", backing_seam_hi, 67);
  FIRE_BAD_FIELD("descriptor_q0_source_count", backing_source_count, 119);
  FIRE_BAD_FIELD("descriptor_profile_eps", profile_epsilon, 0.000005f);
  FIRE_BAD_FIELD("descriptor_backing_gate", backing_min, 0.0044f);
  FIRE_BAD_FIELD("descriptor_foreign_gate", foreign_radius, 0.00016f);
  FIRE_BAD_FIELD("descriptor_foreign_census", foreign_census, 0);
#undef FIRE_BAD_FIELD
  fig_contact_fire_f1c_t descriptor_ulp = descriptor;
  uint32_t profile_bits = 0;
  memcpy(&profile_bits, &descriptor_ulp.profile_epsilon,
         sizeof profile_bits);
  profile_bits++;
  memcpy(&descriptor_ulp.profile_epsilon, &profile_bits,
         sizeof profile_bits);
  vmtrig_fire_f1c_case(&test, "descriptor_profile_one_ulp",
    !vmtrig_fire_f1c_descriptor_valid(&descriptor_ulp));
  fig_contact_fire_f1c_t descriptor_sr_census = descriptor;
  descriptor_sr_census.weapon = WPN_SR;
  vmtrig_fire_f1c_case(&test, "descriptor_census_weapon_mismatch",
    !vmtrig_fire_f1c_descriptor_valid(&descriptor_sr_census));
  const vmtrig_fire_f1c_foreign_range_t identity_frame = {
    .geometry_axis = {{{1, 0, 0}}, {{0, 1, 0}}, {{0, 0, 1}}},
  };
  const int32_t identity_origin_nm[3] = {0, 0, 0};
  vmtrig_fire_f1c_case(&test, "foreign_identity_0_499um",
    vmtrig_fire_f1c_local_vertex_exact(
      &identity_frame, identity_origin_nm, (v3){{0.000000499f, 0, 0}}));
  vmtrig_fire_f1c_case(&test, "foreign_identity_0_501um",
    !vmtrig_fire_f1c_local_vertex_exact(
      &identity_frame, identity_origin_nm, (v3){{0.000000501f, 0, 0}}));
  fig_contact_fire_f1c_t descriptor_zero = {0};
  vmtrig_fire_f1c_case(&test, "descriptor_all_zero",
                        !vmtrig_fire_f1c_descriptor_valid(&descriptor_zero));
  fig_contact_fire_f1c_t descriptor_oob = descriptor;
  descriptor_oob.hand_v1 = INT_MAX;
  vmtrig_fire_f1c_case(&test, "descriptor_hand_intmax",
                        !vmtrig_fire_f1c_descriptor_valid(&descriptor_oob));
  descriptor_oob = descriptor;
  descriptor_oob.hand_v0 = INT_MIN;
  vmtrig_fire_f1c_case(&test, "descriptor_hand_intmin",
                        !vmtrig_fire_f1c_descriptor_valid(&descriptor_oob));

  // A zero Phase-A tag and an absent FIRE registry are pure no-ops: output,
  // work and exact-cert context remain byte-identical.
  fig_contact_patch_t null_patch = vmtrig_exact_cert_self_patch();
  null_patch.cert_tag = 0;
  vmtrig_bite_t null_bite = {.max_upper = 0.123f};
  vmtrig_bite_t null_before = null_bite;
  vmtrig_exact_cert_ctx_t null_ctx = {
    .work = 17, .cells = 3, .accepted = 2,
    .work_limit = 100000, .stack_limit = VMTRIG_EXACT_CERT_STACK_CAP,
  };
  vmtrig_exact_cert_ctx_t null_ctx_before = null_ctx;
  gun_mesh_set_t null_set = {0};
  vmtrig_aabb_t null_box[GUN_CONTACT_MESH_MAX] = {0};
  int null_handled = vmtrig_exact_cert_bite_tri(
    (v3){{0,0,0}}, (v3){{0,1,0}}, (v3){{0,0,1}}, &null_patch,
    GUN_CONTACT_GRIP, &null_set, null_box, &null_bite, &null_ctx, NULL);
  vmtrig_fire_f1c_case(&test, "nulltag_byteexact",
    !null_handled && !memcmp(&null_bite, &null_before, sizeof null_bite) &&
    !memcmp(&null_ctx, &null_ctx_before, sizeof null_ctx) &&
    !(FIG_CONTACT_CERT_TAG(GUN_CONTACT_GRIP, 0,
       GUN_MESH_PART_GRIP_BODY, 1) & UINT32_C(0x80000000)));

  vmtrig_fire_f1c_scene_rewind();
  gun_mesh_set_t bit31_set = {.count = 1};
  bit31_set.range[0] = (gun_mesh_range_t){
    .v0 = g_scene_floats, .surface = 1,
    .part = GUN_MESH_PART_MAG_BODY,
  };
  vmtrig_exact_cert_self_tri(
    (v3){{0.020f,-0.020f,-0.020f}},
    (v3){{0.020f, 0.020f,-0.020f}},
    (v3){{0.020f, 0.000f, 0.020f}});
  bit31_set.range[0].v1 = g_scene_floats;
  fig_contact_patch_t bit31_patch = vmtrig_exact_cert_self_patch();
  bit31_patch.cert_tag |= UINT32_C(0x80000000);
  vmtrig_bite_t bit31_bite = {0};
  vmtrig_exact_cert_ctx_t bit31_ctx = {
    .work_limit = 100000, .stack_limit = VMTRIG_EXACT_CERT_STACK_CAP,
  };
  int bit31_handled = vmtrig_exact_cert_self_run(
    &bit31_patch, GUN_CONTACT_MAG, &bit31_set,
    (v3){{0.0175f,-0.005f,-0.005f}},
    (v3){{0.0170f, 0.000f, 0.005f}},
    (v3){{0.0165f, 0.005f,-0.005f}}, &bit31_bite, &bit31_ctx);
  vmtrig_fire_f1c_case(&test, "phase_a_bit31_reject",
                        bit31_handled && bit31_bite.fail &&
                        bit31_ctx.work == 1 && !bit31_ctx.overflow);
  gun_mesh_set_t topology_set = {0};
  fig_contact_fire_f1c_t topology_descriptor[3];
  int hand_range[3][2];
  int topology_built = vmtrig_fire_f1c_build_topology_fixture(
    &topology_set, topology_descriptor, hand_range);
  vmtrig_fire_f1c_ctx_t registry_ctx = {.limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "registry_exact",
    topology_built && vmtrig_fire_f1c_registry_valid(
      WPN_AR, &topology_set, &registry_ctx));
  int registry_n = g_fig_contact_fire_f1c_n;
  g_fig_contact_fire_f1c_n = 2;
  vmtrig_fire_f1c_ctx_t registry_missing_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "registry_missing_descriptor",
    !vmtrig_fire_f1c_registry_valid(
      WPN_AR, &topology_set, &registry_missing_ctx));
  g_fig_contact_fire_f1c_n = registry_n;
  fig_contact_fire_f1c_t registry_saved = g_fig_contact_fire_f1c[2];
  g_fig_contact_fire_f1c[2] = g_fig_contact_fire_f1c[0];
  vmtrig_fire_f1c_ctx_t registry_duplicate_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "registry_duplicate_section",
    !vmtrig_fire_f1c_registry_valid(
      WPN_AR, &topology_set, &registry_duplicate_ctx));
  g_fig_contact_fire_f1c[2] = registry_saved;
  fig_contact_fire_f1c_add(topology_descriptor[0]);
  vmtrig_fire_f1c_ctx_t registry_cap_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "registry_descriptor_cap",
    g_fig_contact_fire_f1c_overflow == 1 &&
    !vmtrig_fire_f1c_registry_valid(
      WPN_AR, &topology_set, &registry_cap_ctx));
  g_fig_contact_fire_f1c_overflow = 0;
  g_fig_contact_patch_overflow = 1;
  vmtrig_fire_f1c_ctx_t patch_cap_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "registry_patch_cap",
    !vmtrig_fire_f1c_registry_valid(WPN_AR, &topology_set, &patch_cap_ctx));
  g_fig_contact_patch_overflow = 0;
  int exact_patch_n = g_fig_contact_patch_n;
  g_fig_contact_patch_n = -1;
  vmtrig_fire_f1c_ctx_t patch_negative_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "registry_patch_count_negative",
    !vmtrig_fire_f1c_registry_valid(
      WPN_AR, &topology_set, &patch_negative_ctx));
  g_fig_contact_patch_n = FIG_CONTACT_PATCH_MAX + 1;
  vmtrig_fire_f1c_ctx_t patch_above_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "registry_patch_count_above_cap",
    !vmtrig_fire_f1c_registry_valid(WPN_AR, &topology_set, &patch_above_ctx));
  g_fig_contact_patch_n = exact_patch_n;
  int exact_fire_n = g_fig_contact_fire_f1c_n;
  g_fig_contact_fire_f1c_n = -1;
  vmtrig_fire_f1c_ctx_t fire_negative_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "registry_fire_count_negative",
    !vmtrig_fire_f1c_registry_valid(
      WPN_AR, &topology_set, &fire_negative_ctx));
  g_fig_contact_fire_f1c_n = FIG_CONTACT_FIRE_F1C_MAX + 1;
  vmtrig_fire_f1c_ctx_t fire_above_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "registry_fire_count_above_cap",
    !vmtrig_fire_f1c_registry_valid(WPN_AR, &topology_set, &fire_above_ctx));
  g_fig_contact_fire_f1c_n = exact_fire_n;
  vmtrig_fire_f1c_ctx_t q0_ctx = {.limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "topology_q0_62_180_120",
    vmtrig_fire_f1c_topology_exact(
      (int[2]){topology_set.range[0].v0, topology_set.range[0].v1},
      62, 180, 120, &q0_ctx));
  vmtrig_fire_f1c_ctx_t panel_ctx = {.limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "topology_panel_24_66_44",
    vmtrig_fire_f1c_topology_exact(
      (int[2]){topology_set.range[1].v0, topology_set.range[1].v1},
      24, 66, 44, &panel_ctx));
  vmtrig_fire_f1c_ctx_t hand_ctx = {.limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "topology_hand_122_360_240",
    vmtrig_fire_f1c_topology_exact(hand_range[0], 122, 360, 240,
                                    &hand_ctx));
  vmtrig_fire_f1c_ctx_t hand_null_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "hand_mesh_null_descriptor",
    !vmtrig_fire_f1c_hand_mesh_valid(NULL, &hand_null_ctx));
  int finite_topology_f = topology_set.range[1].v0;
  v3 finite_topology_saved[3];
  vmtrig_fire_f1c_scene_tri_get(finite_topology_f, finite_topology_saved);
  const float finite_max = 0x1.fffffep127f;
  v3 finite_topology_tri[3] = {
    {{finite_max, finite_max, 0}}, {{-finite_max, finite_max, 1}},
    {{finite_max, -finite_max, -1}},
  };
  vmtrig_fire_f1c_scene_tri_set(finite_topology_f, finite_topology_tri);
  vmtrig_fire_f1c_ctx_t topology_finite_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "topology_finite_overflow",
    !vmtrig_fire_f1c_topology_exact(
      (int[2]){topology_set.range[1].v0, topology_set.range[1].v1},
      24, 66, 44, &topology_finite_ctx));
  vmtrig_fire_f1c_scene_tri_set(finite_topology_f, finite_topology_saved);
  vmtrig_fire_f1c_ctx_t topology_oob_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "topology_range_above_scene",
    !vmtrig_fire_f1c_topology_exact(
      (int[2]){hand_range[0][0], g_scene_floats + 3 * VERT_FLOATS},
      122, 360, 240, &topology_oob_ctx));
  vmtrig_fire_f1c_ctx_t topology_intmax_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "topology_range_intmax",
    !vmtrig_fire_f1c_topology_exact(
      (int[2]){hand_range[0][0], INT_MAX}, 122, 360, 240,
      &topology_intmax_ctx));
  vmtrig_fire_f1c_ctx_t topology_intmin_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "topology_range_intmin",
    !vmtrig_fire_f1c_topology_exact(
      (int[2]){INT_MIN, hand_range[0][1]}, 122, 360, 240,
      &topology_intmin_ctx));
  vmtrig_fire_f1c_ctx_t topology_align_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "topology_range_misaligned",
    !vmtrig_fire_f1c_topology_exact(
      (int[2]){hand_range[0][0] + VERT_FLOATS, hand_range[0][1]},
      122, 360, 240, &topology_align_ctx));
  int disconnected_range[2];
  vmtrig_fire_f1c_emit_disconnected_fixture(disconnected_range);
  vmtrig_fire_f1c_ctx_t disconnected_control_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_ctx_t disconnected_reject_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "topology_extra_component",
    vmtrig_fire_f1c_topology_exact_components(
      disconnected_range, 13, 33, 22, 2, &disconnected_control_ctx) &&
    !vmtrig_fire_f1c_topology_exact(
      disconnected_range, 13, 33, 22, &disconnected_reject_ctx));

  // Profile binding is to the independent plane, including the exact 4-um edge.
  int panel_v0 = topology_set.range[1].v0;
  v3 corner = SCENE_V(panel_v0 + 6 * 3 * VERT_FLOATS);
  int moved[16], moved_n = 0, moved_total = 0;
  v3 moved_saved[16];
  for (int f = topology_set.range[1].v0;
       f < topology_set.range[1].v1; f += VERT_FLOATS) {
    v3 p = SCENE_V(f);
    if (p.x == corner.x && p.y == corner.y && p.z == corner.z) {
      if (moved_n < 16) {
        moved[moved_n] = f;
        moved_saved[moved_n++] = p;
      }
      moved_total++;
    }
  }
  for (int i = 0; i < moved_n; i++) {
    v3 shifted = v3_add(moved_saved[i],
                        v3_scale(topology_descriptor[0].normal, 0.000003999f));
    g_scene[moved[i]] = shifted.x;
    g_scene[moved[i] + 1] = shifted.y;
    g_scene[moved[i] + 2] = shifted.z;
  }
  vmtrig_fire_f1c_ctx_t residual_pass_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "profile_residual_3_999um",
    moved_total == moved_n && moved_n > 0 &&
    vmtrig_fire_f1c_feature_mesh_valid(&topology_descriptor[0],
                                        &topology_set, &residual_pass_ctx));
  for (int i = 0; i < moved_n; i++) {
    v3 shifted = v3_add(moved_saved[i],
                        v3_scale(topology_descriptor[0].normal, 0.000004001f));
    g_scene[moved[i]] = shifted.x;
    g_scene[moved[i] + 1] = shifted.y;
    g_scene[moved[i] + 2] = shifted.z;
  }
  vmtrig_fire_f1c_ctx_t residual_fail_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "profile_residual_4_001um",
    !vmtrig_fire_f1c_feature_mesh_valid(&topology_descriptor[0],
                                         &topology_set, &residual_fail_ctx));
  for (int i = 0; i < moved_n; i++) {
    g_scene[moved[i]] = moved_saved[i].x;
    g_scene[moved[i] + 1] = moved_saved[i].y;
    g_scene[moved[i] + 2] = moved_saved[i].z;
  }

  // Source and topology corruption remain fail-closed.
  vmtrig_fire_f1c_ctx_t source5_ctx = {.limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  fig_contact_fire_f1c_t source5 = topology_descriptor[0];
  source5.feature_source0 = 5;
  vmtrig_fire_f1c_case(&test, "panel_source5",
    !vmtrig_fire_f1c_feature_mesh_valid(&source5, &topology_set, &source5_ctx));
  vmtrig_fire_f1c_ctx_t source8_ctx = {.limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  fig_contact_fire_f1c_t source8 = topology_descriptor[0];
  source8.feature_source1 = 8;
  vmtrig_fire_f1c_case(&test, "panel_source8",
    !vmtrig_fire_f1c_feature_mesh_valid(&source8, &topology_set, &source8_ctx));

  float record[VERT_FLOATS];
  int corrupt = topology_set.range[1].v0;
  memcpy(record, &g_scene[corrupt + VERT_FLOATS], sizeof record);
  memcpy(&g_scene[corrupt + VERT_FLOATS],
         &g_scene[corrupt + 2 * VERT_FLOATS], sizeof record);
  memcpy(&g_scene[corrupt + 2 * VERT_FLOATS], record, sizeof record);
  vmtrig_fire_f1c_ctx_t flipped_ctx = {.limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "topology_flipped_face",
    !vmtrig_fire_f1c_topology_exact(
      (int[2]){topology_set.range[1].v0, topology_set.range[1].v1},
      24, 66, 44, &flipped_ctx));
  memcpy(record, &g_scene[corrupt + VERT_FLOATS], sizeof record);
  memcpy(&g_scene[corrupt + VERT_FLOATS],
         &g_scene[corrupt + 2 * VERT_FLOATS], sizeof record);
  memcpy(&g_scene[corrupt + 2 * VERT_FLOATS], record, sizeof record);
  vmtrig_fire_f1c_ctx_t missing_ctx = {.limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "topology_missing_face",
    !vmtrig_fire_f1c_topology_exact(
      (int[2]){topology_set.range[1].v0,
               topology_set.range[1].v1 - 3 * VERT_FLOATS},
      24, 66, 44, &missing_ctx));
  float duplicate_save[3 * VERT_FLOATS];
  memcpy(duplicate_save, &g_scene[corrupt + 3 * VERT_FLOATS],
         sizeof duplicate_save);
  memcpy(&g_scene[corrupt + 3 * VERT_FLOATS], &g_scene[corrupt],
         sizeof duplicate_save);
  vmtrig_fire_f1c_ctx_t duplicate_ctx = {.limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "topology_duplicate_face",
    !vmtrig_fire_f1c_topology_exact(
      (int[2]){topology_set.range[1].v0, topology_set.range[1].v1},
      24, 66, 44, &duplicate_ctx));
  memcpy(&g_scene[corrupt + 3 * VERT_FLOATS], duplicate_save,
         sizeof duplicate_save);
  int incidence_face = corrupt + 10 * 3 * VERT_FLOATS;
  float incidence_save[3 * VERT_FLOATS];
  memcpy(incidence_save, &g_scene[incidence_face], sizeof incidence_save);
  memcpy(&g_scene[incidence_face], &g_scene[corrupt], VERT_FLOATS * sizeof(float));
  memcpy(&g_scene[incidence_face + VERT_FLOATS],
         &g_scene[corrupt + VERT_FLOATS], VERT_FLOATS * sizeof(float));
  v3 incidence_a = SCENE_V(incidence_face);
  v3 incidence_b = SCENE_V(incidence_face + VERT_FLOATS);
  v3 incidence_d = v3_add(v3_lerp(incidence_a, incidence_b, 0.5f),
                           (v3){{0.001f, 0.002f, 0.003f}});
  g_scene[incidence_face + 2 * VERT_FLOATS] = incidence_d.x;
  g_scene[incidence_face + 2 * VERT_FLOATS + 1] = incidence_d.y;
  g_scene[incidence_face + 2 * VERT_FLOATS + 2] = incidence_d.z;
  vmtrig_fire_f1c_ctx_t incidence_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "topology_incidence_three",
    !vmtrig_fire_f1c_topology_exact(
      (int[2]){topology_set.range[1].v0, topology_set.range[1].v1},
      24, 66, 44, &incidence_ctx));
  memcpy(&g_scene[incidence_face], incidence_save, sizeof incidence_save);
  int backing_incidence = topology_set.range[0].v0 + 20 * 3 * VERT_FLOATS;
  float backing_incidence_save[3 * VERT_FLOATS];
  memcpy(backing_incidence_save, &g_scene[backing_incidence],
         sizeof backing_incidence_save);
  memcpy(&g_scene[backing_incidence], &g_scene[topology_set.range[0].v0],
         VERT_FLOATS * sizeof(float));
  memcpy(&g_scene[backing_incidence + VERT_FLOATS],
         &g_scene[topology_set.range[0].v0 + VERT_FLOATS],
         VERT_FLOATS * sizeof(float));
  v3 backing_incidence_a = SCENE_V(backing_incidence);
  v3 backing_incidence_b = SCENE_V(backing_incidence + VERT_FLOATS);
  v3 backing_incidence_d = v3_add(
    v3_lerp(backing_incidence_a, backing_incidence_b, 0.5f),
    (v3){{0.001f, 0.002f, 0.003f}});
  g_scene[backing_incidence + 2 * VERT_FLOATS] = backing_incidence_d.x;
  g_scene[backing_incidence + 2 * VERT_FLOATS + 1] = backing_incidence_d.y;
  g_scene[backing_incidence + 2 * VERT_FLOATS + 2] = backing_incidence_d.z;
  vmtrig_fire_f1c_ctx_t backing_incidence_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "backing_nonmanifold_incidence",
    !vmtrig_fire_f1c_topology_exact(
      (int[2]){topology_set.range[0].v0, topology_set.range[0].v1},
      62, 180, 120, &backing_incidence_ctx));
  memcpy(&g_scene[backing_incidence], backing_incidence_save,
         sizeof backing_incidence_save);
  float tjunction_save[VERT_FLOATS];
  memcpy(tjunction_save, &g_scene[corrupt], sizeof tjunction_save);
  v3 ta = SCENE_V(corrupt + VERT_FLOATS),
     tb = SCENE_V(corrupt + 2 * VERT_FLOATS);
  v3 tm = v3_lerp(ta, tb, 0.5f);
  g_scene[corrupt] = tm.x; g_scene[corrupt + 1] = tm.y;
  g_scene[corrupt + 2] = tm.z;
  vmtrig_fire_f1c_ctx_t tjunction_ctx = {.limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "topology_t_junction",
    !vmtrig_fire_f1c_topology_exact(
      (int[2]){topology_set.range[1].v0, topology_set.range[1].v1},
      24, 66, 44, &tjunction_ctx));
  memcpy(&g_scene[corrupt], tjunction_save, sizeof tjunction_save);
  gun_mesh_set_t stale_set = topology_set;
  stale_set.range[1].v0 += 3 * VERT_FLOATS;
  vmtrig_fire_f1c_ctx_t stale_ctx = {.limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "topology_stale_ordinal",
    !vmtrig_fire_f1c_feature_mesh_valid(&topology_descriptor[0],
                                         &stale_set, &stale_ctx));
  vmtrig_fire_f1c_ctx_t topo_cap_ctx = {.limit = 1};
  vmtrig_fire_f1c_case(&test, "topology_work_cap",
    !vmtrig_fire_f1c_topology_exact(hand_range[0], 122, 360, 240,
                                     &topo_cap_ctx) && topo_cap_ctx.overflow);
  const int topo_over_faces = VMTRIG_FIRE_F1C_TOPO_E_CAP / 3 + 1;
  int topo_over_range[2] = {
    topology_set.range[0].v0,
    topology_set.range[0].v0 + topo_over_faces * 3 * VERT_FLOATS,
  };
  vmtrig_fire_f1c_ctx_t topo_fixed_cap_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "topology_fixed_leaf_cap",
    !vmtrig_fire_f1c_topology_exact(topo_over_range, 0, 0,
                                     topo_over_faces, &topo_fixed_cap_ctx));

  // Exact terminal form and hand-source authority.
  descriptor = topology_descriptor[0];
  vmtrig_fire_f1c_bite_t bite = {0};
  vmtrig_fire_f1c_ctx_t bite_ctx = {.limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  int bite_ok = vmtrig_fire_f1c_frustum_bite(&descriptor, &bite, &bite_ctx);
  double mean_lo = bite.area > 0.0 ? bite.moment_lo / bite.area : 0.0;
  double mean_hi = bite.area > 0.0 ? bite.moment_hi / bite.area : 0.0;
  vmtrig_fire_f1c_case(&test, "bite_fixed_frustum",
    bite_ok && !bite.fail && bite.triangles == 36 &&
    fabs(mean_lo - 0.002449637476061) < 2e-9 &&
    fabs(mean_hi - 0.002457637476061) < 2e-9 &&
    fabs((double)bite.max_upper - 0.003604) < 2e-8 &&
    bite.min_facing > 0.0808f && bite.min_facing > VMTRIG_PATCH_NORMAL_MIN);
  v3 source_center = v3_add(
    descriptor.origin, v3_scale(descriptor.normal, -0.0036f));
  v3 source_tri[3] = {
    v3_add(source_center,
      v3_add(v3_scale(descriptor.tangent, -0.001f),
             v3_scale(descriptor.axial, -0.001f))),
    v3_add(source_center, v3_scale(descriptor.axial, 0.001f)),
    v3_add(source_center,
      v3_add(v3_scale(descriptor.tangent, 0.001f),
             v3_scale(descriptor.axial, -0.001f))),
  };
  int source228 = descriptor.hand_v0 + 228 * 3 * VERT_FLOATS;
  float source_saved[3 * VERT_FLOATS];
  memcpy(source_saved, &g_scene[source228], sizeof source_saved);
  vmtrig_fire_f1c_scene_tri_set(source228, source_tri);
  vmtrig_fire_f1c_bite_t source_bite = {0};
  vmtrig_fire_f1c_ctx_t source_ctx = {.limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "bite_actual_source228",
    vmtrig_fire_f1c_bite_source(
      &descriptor, 228, &source_bite, &source_ctx) && !source_bite.fail &&
    source_bite.triangles == 1 && source_bite.area > 0.0);
  v3 valid_source_tri[3] = {source_tri[0], source_tri[1], source_tri[2]};
  source_bite = (vmtrig_fire_f1c_bite_t){0};
  source_ctx = (vmtrig_fire_f1c_ctx_t){.limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "hand_source203",
    !vmtrig_fire_f1c_bite_source(
      &descriptor, 203, &source_bite, &source_ctx) && source_bite.fail);
  source_bite = (vmtrig_fire_f1c_bite_t){0};
  source_ctx = (vmtrig_fire_f1c_ctx_t){.limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "hand_unrelated_source",
    !vmtrig_fire_f1c_bite_source(
      &descriptor, 100, &source_bite, &source_ctx) && source_bite.fail);
  source_bite = (vmtrig_fire_f1c_bite_t){.moment_lo = NAN};
  source_ctx = (vmtrig_fire_f1c_ctx_t){.limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "bite_moment_nan",
    !vmtrig_fire_f1c_bite_source(
      &descriptor, 228, &source_bite, &source_ctx) && source_bite.fail);
  source_bite = (vmtrig_fire_f1c_bite_t){.moment_hi = INFINITY};
  source_ctx = (vmtrig_fire_f1c_ctx_t){.limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "bite_moment_inf",
    !vmtrig_fire_f1c_bite_source(
      &descriptor, 228, &source_bite, &source_ctx) && source_bite.fail);
  source_tri[0].x = NAN;
  vmtrig_fire_f1c_scene_tri_set(source228, source_tri);
  source_bite = (vmtrig_fire_f1c_bite_t){0};
  source_ctx = (vmtrig_fire_f1c_ctx_t){.limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "bite_vertex_nan",
    !vmtrig_fire_f1c_bite_source(
      &descriptor, 228, &source_bite, &source_ctx) && source_bite.fail);
  source_tri[0].x = INFINITY;
  vmtrig_fire_f1c_scene_tri_set(source228, source_tri);
  source_bite = (vmtrig_fire_f1c_bite_t){0};
  source_ctx = (vmtrig_fire_f1c_ctx_t){.limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "bite_vertex_inf",
    !vmtrig_fire_f1c_bite_source(
      &descriptor, 228, &source_bite, &source_ctx) && source_bite.fail);
  v3 bite_huge[3] = {
    {{finite_max, finite_max, 0}}, {{-finite_max, finite_max, 1}},
    {{finite_max, -finite_max, -1}},
  };
  vmtrig_fire_f1c_scene_tri_set(source228, bite_huge);
  source_bite = (vmtrig_fire_f1c_bite_t){0};
  source_ctx = (vmtrig_fire_f1c_ctx_t){.limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "bite_finite_overflow",
    !vmtrig_fire_f1c_bite_source(
      &descriptor, 228, &source_bite, &source_ctx) && source_bite.fail);
  vmtrig_fire_f1c_scene_tri_set(source228, valid_source_tri);
  source_bite = (vmtrig_fire_f1c_bite_t){
    .triangles = UINT64_MAX, .min_facing = 1.0f};
  source_ctx = (vmtrig_fire_f1c_ctx_t){.limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "bite_triangle_counter_cap",
    !vmtrig_fire_f1c_bite_source(
      &descriptor, 228, &source_bite, &source_ctx) && source_bite.fail &&
    source_bite.triangles == UINT64_MAX);
  memcpy(&g_scene[source228], source_saved, sizeof source_saved);
  source_bite = (vmtrig_fire_f1c_bite_t){0};
  source_ctx = (vmtrig_fire_f1c_ctx_t){.limit = 0};
  vmtrig_fire_f1c_case(&test, "bite_work_cap",
    !vmtrig_fire_f1c_bite_source(
      &descriptor, 228, &source_bite, &source_ctx) && source_bite.fail &&
    source_ctx.overflow);

  // Complete backing union at the exact 4.5-mm boundary.
  gun_mesh_set_t backing_set;
  vmtrig_fire_f1c_emit_backing_fixture(&backing_set, &descriptor, 0.004501f);
  vmtrig_fire_f1c_ctx_t backing_pass_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  float backing_min = 0.0f;
  vmtrig_fire_f1c_case(&test, "backing_4_501mm",
    vmtrig_fire_f1c_backing_valid(&descriptor, &backing_set,
                                   &backing_min, &backing_pass_ctx) &&
    backing_min > 0.004500f);
  gun_mesh_set_t backing_bad_part = backing_set;
  backing_bad_part.range[0].part = GUN_MESH_PART_GRIP_PANEL;
  vmtrig_fire_f1c_ctx_t backing_part_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "backing_bad_part_identity",
    !vmtrig_fire_f1c_backing_valid(&descriptor, &backing_bad_part,
                                    NULL, &backing_part_ctx));
  gun_mesh_set_t backing_intmax = backing_set;
  backing_intmax.range[0].v1 = INT_MAX;
  vmtrig_fire_f1c_ctx_t backing_intmax_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "backing_range_intmax",
    !vmtrig_fire_f1c_backing_valid(&descriptor, &backing_intmax,
                                    NULL, &backing_intmax_ctx));
  gun_mesh_set_t backing_intmin = backing_set;
  backing_intmin.range[0].v0 = INT_MIN;
  vmtrig_fire_f1c_ctx_t backing_intmin_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "backing_range_intmin",
    !vmtrig_fire_f1c_backing_valid(&descriptor, &backing_intmin,
                                    NULL, &backing_intmin_ctx));
  vmtrig_fire_f1c_emit_backing_fixture(&backing_set, &descriptor, 0.004499f);
  vmtrig_fire_f1c_ctx_t backing_fail_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "backing_4_499mm",
    !vmtrig_fire_f1c_backing_valid(&descriptor, &backing_set,
                                    NULL, &backing_fail_ctx));
  vmtrig_fire_f1c_emit_backing_fixture(&backing_set, &descriptor, 0.004501f);
  gun_mesh_set_t backing_missing = backing_set;
  backing_missing.range[0].v1 -= 3 * VERT_FLOATS;
  vmtrig_fire_f1c_ctx_t backing_missing_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "backing_missing",
    !vmtrig_fire_f1c_backing_valid(&descriptor, &backing_missing,
                                    NULL, &backing_missing_ctx));
  int source62 = backing_set.range[0].v0 + 62 * 3 * VERT_FLOATS;
  int source67 = backing_set.range[0].v0 + 67 * 3 * VERT_FLOATS;
  float backing_save[3 * VERT_FLOATS];
  memcpy(backing_save, &g_scene[source67], sizeof backing_save);
  memcpy(&g_scene[source67], &g_scene[source62], sizeof backing_save);
  vmtrig_fire_f1c_ctx_t backing_dup_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "backing_duplicate",
    !vmtrig_fire_f1c_backing_valid(&descriptor, &backing_set,
                                    NULL, &backing_dup_ctx));
  memcpy(&g_scene[source67], backing_save, sizeof backing_save);
  v3 partial_save[3];
  vmtrig_fire_f1c_scene_tri_get(source67, partial_save);
  v3 partial_tri[3] = {partial_save[0], partial_save[1], partial_save[2]};
  partial_tri[1] = v3_add(
    partial_tri[1], v3_scale(descriptor.tangent, 0.001f));
  vmtrig_fire_f1c_scene_tri_set(source67, partial_tri);
  vmtrig_fire_f1c_ctx_t backing_partial_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "backing_partial",
    !vmtrig_fire_f1c_backing_valid(&descriptor, &backing_set,
                                    NULL, &backing_partial_ctx));
  vmtrig_fire_f1c_scene_tri_set(source67, partial_save);
  v3 backing_finite_saved[3];
  vmtrig_fire_f1c_scene_tri_get(source62, backing_finite_saved);
  vmtrig_fire_f1c_scene_tri_set(source62, finite_topology_tri);
  vmtrig_fire_f1c_ctx_t backing_finite_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "backing_finite_overflow",
    !vmtrig_fire_f1c_backing_valid(
      &descriptor, &backing_set, NULL, &backing_finite_ctx));
  vmtrig_fire_f1c_scene_tri_set(source62, backing_finite_saved);
  vmtrig_fire_f1c_ctx_t backing_cap_ctx = {.limit = 1};
  vmtrig_fire_f1c_case(&test, "backing_work_cap",
    !vmtrig_fire_f1c_backing_valid(&descriptor, &backing_set,
                                    NULL, &backing_cap_ctx) &&
    backing_cap_ctx.overflow);

  // The productive foreign entry loads an actual authorized hand source, then
  // scans every captured contact kind and the separately published Guard/Blade.
  int foreign_v0[GUN_CONTACT_COUNT];
  fig_contact_fire_f1c_t *foreign_contract = NULL;
  int foreign_built = vmtrig_fire_f1c_emit_foreign_fixture(
    foreign_v0, &foreign_contract);
  if (!foreign_built) {
    foreign_contract = &descriptor;
    for (int kind = 0; kind < GUN_CONTACT_COUNT; kind++) foreign_v0[kind] = 0;
  }
  int foreign_hand_f = foreign_built
    ? foreign_contract->hand_v0 + 228 * 3 * VERT_FLOATS : 0;
  v3 foreign_hand[3] = {0};
  if (foreign_built)
    vmtrig_fire_f1c_scene_tri_get(foreign_hand_f, foreign_hand);
  static float foreign_hand_saved[240 * 3 * VERT_FLOATS];
  if (foreign_built)
    memcpy(foreign_hand_saved, &g_scene[foreign_contract->hand_v0],
           sizeof foreign_hand_saved);
  static const char *kind_case[GUN_CONTACT_COUNT] = {
    [GUN_CONTACT_FORE] = "foreign_fore_0_149mm",
    [GUN_CONTACT_MAG] = "foreign_mag_0_149mm",
    [GUN_CONTACT_BOLT] = "foreign_bolt_0_149mm",
    [GUN_CONTACT_RECEIVER] = "foreign_receiver_0_149mm",
    [GUN_CONTACT_SCOPE] = "foreign_scope_0_149mm",
    [GUN_CONTACT_STOCK] = "foreign_stock_0_149mm",
    [GUN_CONTACT_CHASSIS] = "foreign_chassis_0_149mm",
  };
  for (int kind = GUN_CONTACT_FORE; kind < GUN_CONTACT_COUNT; kind++) {
    float x = 0.5f + 0.03125f * (float)kind;
    v3 target = {{x, 0.625f - 0x1p-10f - 0.000149f, 0.5f}};
    v3 delta = v3_sub(target, foreign_hand[0]);
    int translated = foreign_built &&
      vmtrig_fire_f1c_hand_translate(foreign_contract, delta);
    vmtrig_fire_f1c_ctx_t kind_ctx = {
      .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
    vmtrig_fire_f1c_case(&test, kind_case[kind],
      translated && !vmtrig_fire_f1c_foreign_source(
        foreign_contract, 228, NULL, &kind_ctx));
    if (foreign_built)
      memcpy(&g_scene[foreign_contract->hand_v0], foreign_hand_saved,
             sizeof foreign_hand_saved);
  }
  int receiver_v0 = foreign_v0[GUN_CONTACT_RECEIVER];
  v3 receiver_saved[3];
  vmtrig_fire_f1c_scene_tri_get(receiver_v0, receiver_saved);
  v3 receiver_target = {{0.625f,
    0.625f - 0x1p-10f - 0.000151f, 0.5f}};
  int translated151 = foreign_built && vmtrig_fire_f1c_hand_translate(
    foreign_contract, v3_sub(receiver_target, foreign_hand[0]));
  vmtrig_fire_f1c_ctx_t foreign151_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "foreign_all_0_151mm",
    translated151 && vmtrig_fire_f1c_foreign_source(
      foreign_contract, 228, NULL, &foreign151_ctx));
  if (foreign_built)
    memcpy(&g_scene[foreign_contract->hand_v0], foreign_hand_saved,
           sizeof foreign_hand_saved);
  vmtrig_fire_f1c_ctx_t foreign_source_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "foreign_actual_source203",
    foreign_built && !vmtrig_fire_f1c_foreign_source(
      foreign_contract, 203, NULL, &foreign_source_ctx));
  int grip_count = g_gun_contact_mesh[GUN_CONTACT_GRIP].count;
  g_gun_contact_mesh[GUN_CONTACT_GRIP].count = 3;
  vmtrig_fire_f1c_ctx_t foreign_identity_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "foreign_bad_grip_identity",
    !vmtrig_fire_f1c_foreign_source(
      foreign_contract, 228, NULL, &foreign_identity_ctx));
  g_gun_contact_mesh[GUN_CONTACT_GRIP].count = grip_count;
  gun_mesh_set_t *foreign_set = &g_gun_contact_mesh[GUN_CONTACT_FORE];
  int foreign_count = foreign_set->count;
  foreign_set->count = 0;
  vmtrig_fire_f1c_ctx_t foreign_missing_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "foreign_census_missing_range",
    foreign_built && !vmtrig_fire_f1c_foreign_source(
      foreign_contract, 228, NULL, &foreign_missing_ctx));
  foreign_set->count = -1;
  vmtrig_fire_f1c_ctx_t foreign_negative_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "foreign_census_negative_count",
    foreign_built && !vmtrig_fire_f1c_foreign_source(
      foreign_contract, 228, NULL, &foreign_negative_ctx));
  foreign_set->count = GUN_CONTACT_MESH_MAX + 1;
  vmtrig_fire_f1c_ctx_t foreign_above_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "foreign_census_count_above_cap",
    foreign_built && !vmtrig_fire_f1c_foreign_source(
      foreign_contract, 228, NULL, &foreign_above_ctx));
  foreign_set->count = foreign_count;
  int foreign_v1 = foreign_set->range[0].v1;
  foreign_set->range[0].v1 -= 3 * VERT_FLOATS;
  vmtrig_fire_f1c_ctx_t foreign_open_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "foreign_census_open_range",
    foreign_built && !vmtrig_fire_f1c_foreign_source(
      foreign_contract, 228, NULL, &foreign_open_ctx));
  foreign_set->range[0].v1 = g_scene_floats + 3 * VERT_FLOATS;
  vmtrig_fire_f1c_ctx_t foreign_oob_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "foreign_census_oob_range",
    foreign_built && !vmtrig_fire_f1c_foreign_source(
      foreign_contract, 228, NULL, &foreign_oob_ctx));
  foreign_set->range[0].v1 = foreign_v1;
  int foreign_surface = foreign_set->range[0].surface;
  foreign_set->range[0].surface = foreign_surface + 1;
  vmtrig_fire_f1c_ctx_t foreign_surface_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "foreign_census_surface",
    foreign_built && !vmtrig_fire_f1c_foreign_source(
      foreign_contract, 228, NULL, &foreign_surface_ctx));
  foreign_set->range[0].surface = foreign_surface;
  uint32_t foreign_part = foreign_set->range[0].part;
  foreign_set->range[0].part = foreign_part ^ UINT32_C(1);
  vmtrig_fire_f1c_ctx_t foreign_part_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "foreign_census_part",
    foreign_built && !vmtrig_fire_f1c_foreign_source(
      foreign_contract, 228, NULL, &foreign_part_ctx));
  foreign_set->range[0].part = foreign_part;
  uint32_t foreign_host = foreign_set->range[0].host_mask;
  foreign_set->range[0].host_mask = foreign_host ^ UINT32_C(1);
  vmtrig_fire_f1c_ctx_t foreign_host_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "foreign_census_host",
    foreign_built && !vmtrig_fire_f1c_foreign_source(
      foreign_contract, 228, NULL, &foreign_host_ctx));
  foreign_set->range[0].host_mask = foreign_host;
  foreign_set->overflow = 1;
  vmtrig_fire_f1c_ctx_t foreign_overflow_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "foreign_census_overflow",
    foreign_built && !vmtrig_fire_f1c_foreign_source(
      foreign_contract, 228, NULL, &foreign_overflow_ctx));
  foreign_set->overflow = 0;
  gun_mesh_range_t mag_range_saved =
    g_gun_contact_mesh[GUN_CONTACT_MAG].range[0];
  g_gun_contact_mesh[GUN_CONTACT_MAG].range[0] = foreign_set->range[0];
  vmtrig_fire_f1c_ctx_t foreign_overlap_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "foreign_census_overlap",
    foreign_built && !vmtrig_fire_f1c_foreign_source(
      foreign_contract, 228, NULL, &foreign_overlap_ctx));
  g_gun_contact_mesh[GUN_CONTACT_MAG].range[0] = mag_range_saved;
  static float foreign_geometry_saved[12 * 3 * VERT_FLOATS];
  memcpy(foreign_geometry_saved, &g_scene[foreign_set->range[0].v0],
         sizeof foreign_geometry_saved);
  v3 foreign_corner = SCENE_V(foreign_set->range[0].v0);
  int foreign_corner_matches = 0;
  for (int f = foreign_set->range[0].v0; f < foreign_set->range[0].v1;
       f += VERT_FLOATS) {
    v3 p = SCENE_V(f);
    if (p.x == foreign_corner.x && p.y == foreign_corner.y &&
        p.z == foreign_corner.z) {
      v3 shifted = v3_add(p, (v3){{0.0001f, 0.0001f, 0.0001f}});
      g_scene[f] = shifted.x;
      g_scene[f + 1] = shifted.y;
      g_scene[f + 2] = shifted.z;
      foreign_corner_matches++;
    }
  }
  vmtrig_fire_f1c_ctx_t foreign_digest_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "foreign_census_geometry_identity",
    foreign_built && foreign_corner_matches >= 3 &&
    !vmtrig_fire_f1c_foreign_source(
      foreign_contract, 228, NULL, &foreign_digest_ctx));
  memcpy(&g_scene[foreign_set->range[0].v0], foreign_geometry_saved,
         sizeof foreign_geometry_saved);
  int scene_want_exact = g_scene_want;
  g_scene_want++;
  vmtrig_fire_f1c_ctx_t foreign_drop_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "foreign_capture_drop_channel",
    foreign_built && !vmtrig_fire_f1c_foreign_source(
      foreign_contract, 228, NULL, &foreign_drop_ctx));
  g_scene_want = scene_want_exact;
  vmtrig_fire_f1c_ctx_t foreign_work_ctx = {.limit = 1};
  vmtrig_fire_f1c_case(&test, "foreign_census_work_cap",
    foreign_built && !vmtrig_fire_f1c_foreign_source(
      foreign_contract, 228, NULL, &foreign_work_ctx) &&
    foreign_work_ctx.overflow);
  g_gun_contact_mesh_scope_overflow = 1;
  vmtrig_fire_f1c_ctx_t foreign_capture_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "foreign_capture_incomplete",
    !vmtrig_fire_f1c_foreign_source(
      foreign_contract, 228, NULL, &foreign_capture_ctx));
  g_gun_contact_mesh_scope_overflow = 0;
  v3 guard_target = {{g_gun_guard[0].x - g_gun_guard_r - 0.000149f,
                      0.5f * (g_gun_guard[1].y + g_gun_guard[2].y),
                      g_gun_guard[0].z}};
  int guard_translated = foreign_built && vmtrig_fire_f1c_hand_translate(
    foreign_contract, v3_sub(guard_target, foreign_hand[0]));
  vmtrig_fire_f1c_ctx_t foreign_guard_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "foreign_guard_0_149mm",
    guard_translated && !vmtrig_fire_f1c_foreign_source(
      foreign_contract, 228, NULL, &foreign_guard_ctx));
  if (foreign_built)
    memcpy(&g_scene[foreign_contract->hand_v0], foreign_hand_saved,
           sizeof foreign_hand_saved);
  v3 blade_target = {{g_gun_blade[0].x - g_gun_blade_r - 0.000149f,
                      g_gun_blade[1].y, g_gun_blade[0].z}};
  int blade_translated = foreign_built && vmtrig_fire_f1c_hand_translate(
    foreign_contract, v3_sub(blade_target, foreign_hand[0]));
  vmtrig_fire_f1c_ctx_t foreign_blade_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "foreign_blade_0_149mm",
    blade_translated && !vmtrig_fire_f1c_foreign_source(
      foreign_contract, 228, NULL, &foreign_blade_ctx));
  if (foreign_built)
    memcpy(&g_scene[foreign_contract->hand_v0], foreign_hand_saved,
           sizeof foreign_hand_saved);
  float guard_radius_exact = g_gun_guard_r;
  uint32_t guard_radius_ulp = 0;
  memcpy(&guard_radius_ulp, &g_gun_guard_r, sizeof guard_radius_ulp);
  guard_radius_ulp++;
  memcpy(&g_gun_guard_r, &guard_radius_ulp, sizeof g_gun_guard_r);
  vmtrig_fire_f1c_ctx_t guard_radius_ulp_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "foreign_guard_radius_one_ulp",
    foreign_built && !vmtrig_fire_f1c_foreign_source(
      foreign_contract, 228, NULL, &guard_radius_ulp_ctx));
  g_gun_guard_r = guard_radius_exact;
  g_gun_guard_r = 0.0f;
  vmtrig_fire_f1c_ctx_t guard_zero_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "foreign_guard_zero_radius",
    foreign_built && !vmtrig_fire_f1c_foreign_source(
      foreign_contract, 228, NULL, &guard_zero_ctx));
  g_gun_guard_r = guard_radius_exact;
  v3 guard_node_exact = g_gun_guard[0];
  uint32_t guard_node_ulp = 0;
  memcpy(&guard_node_ulp, &g_gun_guard[0].x, sizeof guard_node_ulp);
  guard_node_ulp++;
  memcpy(&g_gun_guard[0].x, &guard_node_ulp, sizeof g_gun_guard[0].x);
  vmtrig_fire_f1c_ctx_t guard_node_ulp_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "foreign_guard_node_one_ulp",
    foreign_built && !vmtrig_fire_f1c_foreign_source(
      foreign_contract, 228, NULL, &guard_node_ulp_ctx));
  g_gun_guard[0] = guard_node_exact;
  g_gun_guard[0].x = NAN;
  vmtrig_fire_f1c_ctx_t guard_nan_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "foreign_guard_nan_node",
    foreign_built && !vmtrig_fire_f1c_foreign_source(
      foreign_contract, 228, NULL, &guard_nan_ctx));
  g_gun_guard[0] = guard_node_exact;
  g_gun_guard[0].x = finite_max;
  vmtrig_fire_f1c_ctx_t guard_finite_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "foreign_guard_finite_overflow",
    foreign_built && !vmtrig_fire_f1c_foreign_source(
      foreign_contract, 228, NULL, &guard_finite_ctx));
  g_gun_guard[0] = guard_node_exact;
  float blade_radius_exact = g_gun_blade_r;
  uint32_t blade_radius_ulp = 0;
  memcpy(&blade_radius_ulp, &g_gun_blade_r, sizeof blade_radius_ulp);
  blade_radius_ulp++;
  memcpy(&g_gun_blade_r, &blade_radius_ulp, sizeof g_gun_blade_r);
  vmtrig_fire_f1c_ctx_t blade_radius_ulp_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "foreign_blade_radius_one_ulp",
    foreign_built && !vmtrig_fire_f1c_foreign_source(
      foreign_contract, 228, NULL, &blade_radius_ulp_ctx));
  g_gun_blade_r = blade_radius_exact;
  g_gun_blade_r = 0.0f;
  vmtrig_fire_f1c_ctx_t blade_zero_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "foreign_blade_zero_radius",
    foreign_built && !vmtrig_fire_f1c_foreign_source(
      foreign_contract, 228, NULL, &blade_zero_ctx));
  g_gun_blade_r = blade_radius_exact;
  v3 blade_node_exact = g_gun_blade[0];
  uint32_t blade_node_ulp = 0;
  memcpy(&blade_node_ulp, &g_gun_blade[0].x, sizeof blade_node_ulp);
  blade_node_ulp++;
  memcpy(&g_gun_blade[0].x, &blade_node_ulp, sizeof g_gun_blade[0].x);
  vmtrig_fire_f1c_ctx_t blade_node_ulp_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "foreign_blade_node_one_ulp",
    foreign_built && !vmtrig_fire_f1c_foreign_source(
      foreign_contract, 228, NULL, &blade_node_ulp_ctx));
  g_gun_blade[0] = blade_node_exact;
  g_gun_blade[0].x = NAN;
  vmtrig_fire_f1c_ctx_t blade_nan_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "foreign_blade_nan_node",
    foreign_built && !vmtrig_fire_f1c_foreign_source(
      foreign_contract, 228, NULL, &blade_nan_ctx));
  g_gun_blade[0] = blade_node_exact;
  g_gun_blade[0].x = finite_max;
  vmtrig_fire_f1c_ctx_t blade_finite_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "foreign_blade_finite_overflow",
    foreign_built && !vmtrig_fire_f1c_foreign_source(
      foreign_contract, 228, NULL, &blade_finite_ctx));
  g_gun_blade[0] = blade_node_exact;
  v3 receiver_huge[3] = {
    {{finite_max, -finite_max, 0}}, {{-finite_max, finite_max, 1}},
    {{finite_max, finite_max, -1}},
  };
  vmtrig_fire_f1c_scene_tri_set(receiver_v0, receiver_huge);
  vmtrig_fire_f1c_ctx_t foreign_huge_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  vmtrig_fire_f1c_case(&test, "foreign_finite_overflow",
    !vmtrig_fire_f1c_foreign_source(
      foreign_contract, 228, NULL, &foreign_huge_ctx));
  vmtrig_fire_f1c_scene_tri_set(receiver_v0, receiver_saved);

  // Cross evidence is derived only by scanning the descriptor-bound Scene ranges.
  // There is no caller-provided triangle, coverage interval or event flag.
  vmtrig_fire_f1c_cross_t cross_result;
  vmtrig_fire_f1c_ctx_t cross_result_ctx;
  int cross_complete = vmtrig_fire_f1c_cross_fixture_run(
    VMTRIG_FIRE_F1C_FIX_COMPLETE, &cross_result, &cross_result_ctx);
  vmtrig_fire_f1c_case(&test, "cross_actual_complete",
    cross_complete && cross_result.allowed == 26 && !cross_result.hard &&
    !cross_result_ctx.overflow);
  gun_mesh_set_t forged_grip = g_gun_contact_mesh[GUN_CONTACT_GRIP];
  vmtrig_fire_f1c_cross_t forged_result = {0};
  vmtrig_fire_f1c_ctx_t forged_ctx = {
    .limit = VMTRIG_FIRE_F1C_WORK_LIMIT};
  int forged_ok = vmtrig_fire_f1c_cross_scan_bound(
    &g_fig_contact_fire_f1c[0], &forged_grip, &forged_result, &forged_ctx);
  vmtrig_fire_f1c_case(&test, "cross_forged_local_set",
    !forged_ok && forged_result.hard);
#define FIRE_ACTUAL_CROSS_FAIL(NAME, VARIANT) do { \
    vmtrig_fire_f1c_cross_t cross_out_ = {0}; \
    vmtrig_fire_f1c_ctx_t cross_ctx_ = {0}; \
    int cross_ok_ = vmtrig_fire_f1c_cross_fixture_run( \
      (VARIANT), &cross_out_, &cross_ctx_); \
    vmtrig_fire_f1c_case(&test, (NAME), !cross_ok_ && cross_out_.hard); \
  } while (0)
  FIRE_ACTUAL_CROSS_FAIL("cross_panel_source5",
                         VMTRIG_FIRE_F1C_FIX_PANEL5);
  FIRE_ACTUAL_CROSS_FAIL("cross_panel_source8",
                         VMTRIG_FIRE_F1C_FIX_PANEL8);
  FIRE_ACTUAL_CROSS_FAIL("cross_p1_h_source203",
                         VMTRIG_FIRE_F1C_FIX_HAND203);
  FIRE_ACTUAL_CROSS_FAIL("cross_wrong_range",
                         VMTRIG_FIRE_F1C_FIX_WRONG_RANGE);
  FIRE_ACTUAL_CROSS_FAIL("cross_wrong_child_range",
                         VMTRIG_FIRE_F1C_FIX_WRONG_CHILD);
  FIRE_ACTUAL_CROSS_FAIL("cross_wrong_section",
                         VMTRIG_FIRE_F1C_FIX_WRONG_SECTION);
  FIRE_ACTUAL_CROSS_FAIL("cross_partial_top_union",
                         VMTRIG_FIRE_F1C_FIX_PARTIAL_TOP);
  FIRE_ACTUAL_CROSS_FAIL("cross_incomplete_top_union",
                         VMTRIG_FIRE_F1C_FIX_MISSING_TOP);
  FIRE_ACTUAL_CROSS_FAIL("cross_mixed_hard",
                         VMTRIG_FIRE_F1C_FIX_MIXED);
  FIRE_ACTUAL_CROSS_FAIL("cross_body_edge_tie",
                         VMTRIG_FIRE_F1C_FIX_BODY_TIE);
  vmtrig_fire_f1c_cross_t duplicate_result = {0};
  vmtrig_fire_f1c_ctx_t duplicate_result_ctx = {0};
  int duplicate_ok = vmtrig_fire_f1c_cross_fixture_run(
    VMTRIG_FIRE_F1C_FIX_DUPLICATE, &duplicate_result,
    &duplicate_result_ctx);
  vmtrig_fire_f1c_case(&test, "cross_duplicate_actual",
    !duplicate_ok && duplicate_result.hard);
  FIRE_ACTUAL_CROSS_FAIL("cross_q0_duplicate_actual",
                         VMTRIG_FIRE_F1C_FIX_Q0_DUPLICATE);
  FIRE_ACTUAL_CROSS_FAIL("cross_panel_duplicate_actual",
                         VMTRIG_FIRE_F1C_FIX_PANEL_DUPLICATE);
  FIRE_ACTUAL_CROSS_FAIL("cross_patch_count_above_cap",
                         VMTRIG_FIRE_F1C_FIX_PATCH_COUNT);
  FIRE_ACTUAL_CROSS_FAIL("cross_q0_finite_overflow",
                         VMTRIG_FIRE_F1C_FIX_Q0_FINITE_MAX);
  FIRE_ACTUAL_CROSS_FAIL("cross_panel_finite_overflow",
                         VMTRIG_FIRE_F1C_FIX_PANEL_FINITE_MAX);
  FIRE_ACTUAL_CROSS_FAIL("cross_hand_finite_overflow",
                         VMTRIG_FIRE_F1C_FIX_HAND_FINITE_MAX);
  FIRE_ACTUAL_CROSS_FAIL("cross_open_endpoint",
                         VMTRIG_FIRE_F1C_FIX_OPEN);
  FIRE_ACTUAL_CROSS_FAIL("cross_coplanar",
                         VMTRIG_FIRE_F1C_FIX_COPLANAR);
  FIRE_ACTUAL_CROSS_FAIL("cross_near_parallel",
                         VMTRIG_FIRE_F1C_FIX_NEAR_PARALLEL);
  FIRE_ACTUAL_CROSS_FAIL("cross_shared_edge",
                         VMTRIG_FIRE_F1C_FIX_SHARED_EDGE);
  FIRE_ACTUAL_CROSS_FAIL("cross_zero_length",
                         VMTRIG_FIRE_F1C_FIX_ZERO_LENGTH);
  FIRE_ACTUAL_CROSS_FAIL("cross_point_only",
                         VMTRIG_FIRE_F1C_FIX_POINT_ONLY);
  FIRE_ACTUAL_CROSS_FAIL("cross_partial_footprint",
                         VMTRIG_FIRE_F1C_FIX_FOOTPRINT);
  FIRE_ACTUAL_CROSS_FAIL("cross_nan_actual",
                         VMTRIG_FIRE_F1C_FIX_NAN);
  FIRE_ACTUAL_CROSS_FAIL("cross_inf_actual",
                         VMTRIG_FIRE_F1C_FIX_INF);
  FIRE_ACTUAL_CROSS_FAIL("seam_outsider_actual",
                         VMTRIG_FIRE_F1C_FIX_SEAM_OUTSIDER);
  FIRE_ACTUAL_CROSS_FAIL("seam_missing_actual",
                         VMTRIG_FIRE_F1C_FIX_SEAM_MISSING);
  FIRE_ACTUAL_CROSS_FAIL("cross_range_intmax",
                         VMTRIG_FIRE_F1C_FIX_RANGE_INTMAX);
#undef FIRE_ACTUAL_CROSS_FAIL
  v3 finite_endpoint[2];
  const v3 ordinary_cross_tri[3] = {
    {{0, 0, 0}}, {{0.001f, 0, 0}}, {{0, 0.001f, 0}},
  };
  vmtrig_fire_f1c_case(&test, "cross_relation_finite_overflow",
    vmtrig_fire_f1c_cross_relation(
      finite_topology_tri, ordinary_cross_tri, finite_endpoint) ==
      VMTRIG_FIRE_F1C_CROSS_HARD);
  vmtrig_fire_f1c_case(&test, "cross_feature_source6_7_exact",
    VMTRIG_FIRE_F1C_COVERAGE_CAP == 2 &&
    vmtrig_fire_f1c_feature_source_slot(
      &descriptor, descriptor.feature_range, 6) == 0 &&
    vmtrig_fire_f1c_feature_source_slot(
      &descriptor, descriptor.feature_range, 7) == 1 &&
    vmtrig_fire_f1c_feature_source_slot(
      &descriptor, descriptor.feature_range, 5) < 0 &&
    vmtrig_fire_f1c_feature_source_slot(
      &descriptor, descriptor.feature_range, 8) < 0 &&
    vmtrig_fire_f1c_feature_source_slot(
      &descriptor, descriptor.feature_range + 1, 6) < 0);
  double coverage[VMTRIG_FIRE_F1C_COVERAGE_CAP][2] = {
    {0.0, 0.5}, {7.0, 8.0},
  };
  double coverage_second[2] = {0.5, 1.0};
  int coverage_n = 1;
  int coverage_second_ok = vmtrig_fire_f1c_coverage_add(
    coverage, &coverage_n, coverage_second);
  vmtrig_fire_f1c_case(&test, "cross_coverage_add_count2",
    coverage_second_ok && coverage_n == 2 &&
    coverage[0][0] == 0.0 && coverage[0][1] == 0.5 &&
    coverage[1][0] == 0.5 && coverage[1][1] == 1.0);
  double coverage_before[VMTRIG_FIRE_F1C_COVERAGE_CAP][2];
  memcpy(coverage_before, coverage, sizeof coverage_before);
  double coverage_third[2] = {1.0, 1.5};
  int coverage_third_ok = vmtrig_fire_f1c_coverage_add(
    coverage, &coverage_n, coverage_third);
  int coverage_malformed_n = 3;
  int coverage_malformed_ok = vmtrig_fire_f1c_coverage_add(
    NULL, &coverage_malformed_n, NULL);
  vmtrig_fire_f1c_case(&test, "cross_coverage_add_count3",
    !coverage_third_ok && coverage_n == 2 &&
    !memcmp(coverage_before, coverage, sizeof coverage) &&
    !coverage_malformed_ok && coverage_malformed_n == 3);
  int union_duplicate = 0;
  double union_expected[2] = {0.0, 1.0};
  double union_overlap[2][2] = {{0.0, 0.6}, {0.5, 1.0}};
  vmtrig_fire_f1c_case(&test, "cross_ordered_union_overlap",
    !vmtrig_fire_f1c_ordered_union(
      union_expected, union_overlap, 2, &union_duplicate) && union_duplicate);
  double union_gap[2][2] = {{0.0, 0.4}, {0.5, 1.0}};
  vmtrig_fire_f1c_case(&test, "cross_ordered_union_gap",
    !vmtrig_fire_f1c_ordered_union(
      union_expected, union_gap, 2, NULL));
  double union_cap[2][2] = {{0.0, 0.5}, {0.5, 1.0}};
  vmtrig_fire_f1c_case(&test, "cross_ordered_union_cap",
    !vmtrig_fire_f1c_ordered_union(union_expected, union_cap, 3, NULL));
  vmtrig_fire_f1c_cross_t work_result = {0};
  vmtrig_fire_f1c_ctx_t work_result_ctx = {0};
  int work_ok = vmtrig_fire_f1c_cross_fixture_run(
    VMTRIG_FIRE_F1C_FIX_WORK_CAP, &work_result, &work_result_ctx);
  vmtrig_fire_f1c_case(&test, "cross_work_cap",
    !work_ok && work_result.hard && work_result_ctx.overflow);

  int sentinel_untouched =
    !memcmp(&g_scene[sentinel_v0], sentinel_scene, sizeof sentinel_scene);

  // Poison every saved channel before exercising the single restore path.  This
  // makes the byte comparisons below evidence of restoration, not default-state
  // coincidences.
  g_scene_floats = sentinel_v1 + 3 * VERT_FLOATS;
  g_scene_want = sentinel_v1 + 6 * VERT_FLOATS;
  g_scene_peak = sentinel_v1 + 9 * VERT_FLOATS;
  g_scene_dirty = -7;
  g_scene_drops = -19;
  memset(g_schunk, 0, sizeof g_schunk);
  g_nschunk = 0;
  g_schunk_open = -1;
  g_scene_cast_floats = -1;
  g_ao_mul = 0.0f;
  g_mat_rough = 0.0f;
  g_mat_metal = 0.0f;
  memset(g_prof, 0, sizeof g_prof);
  g_prof_ready = 0;
  memset(g_probe, 0, sizeof g_probe);
  g_nprobe = 0;
  g_probe_on = 0;
  g_vmtrig_inside_ambiguous = 0;
  memset(g_fig_contact_patch, 0, sizeof g_fig_contact_patch);
  memset(g_fig_contact_fire_f1c, 0, sizeof g_fig_contact_fire_f1c);
  memset(g_gun_contact_mesh_stack, 0, sizeof g_gun_contact_mesh_stack);
  memset(g_gun_contact_mesh, 0, sizeof g_gun_contact_mesh);
  memset(g_gun_guard, 0, sizeof g_gun_guard);
  memset(g_gun_blade, 0, sizeof g_gun_blade);
  g_gun_guard_r = 0.0f;
  g_gun_blade_r = 0.0f;
  g_fig_contact_patch_n = 0;
  g_fig_contact_patch_overflow = 1;
  g_fig_contact_fire_f1c_n = 0;
  g_fig_contact_fire_f1c_overflow = 1;
  g_gun_contact_mesh_capture = -1;
  g_gun_contact_mesh_active = 0;
  g_gun_contact_mesh_depth = 1;
  g_gun_contact_mesh_ignored = 1;
  g_gun_contact_surface_active = 1;
  g_gun_contact_mesh_unclassified = 1;
  g_gun_contact_mesh_scope_overflow = 1;
  g_gun_contact_mesh_part = UINT32_C(1);
  g_gun_contact_mesh_host_mask = UINT32_C(1);
  g_cham = 0.0f;
  g_fig_diss = 0.0f;
  g_fig_k = 0;
  vmtrig_fire_f1c_scene_base = -1;
  int state_was_poisoned = sentinel_untouched &&
    g_scene_floats != sentinel_v1 && g_scene_want != sentinel_scene_want &&
    g_scene_peak != sentinel_scene_peak &&
    memcmp(g_schunk, preloaded_schunk, sizeof preloaded_schunk) &&
    memcmp(g_probe, preloaded_probe_registry,
           sizeof preloaded_probe_registry) &&
    memcmp(g_prof, preloaded_prof, sizeof preloaded_prof) &&
    memcmp(g_gun_contact_mesh, preloaded_contact_mesh,
           sizeof preloaded_contact_mesh) &&
    memcmp(g_fig_contact_patch, preloaded_patch, sizeof preloaded_patch) &&
    memcmp(g_fig_contact_fire_f1c, preloaded_fire, sizeof preloaded_fire) &&
    memcmp(g_gun_guard, preloaded_guard, sizeof preloaded_guard) &&
    memcmp(g_gun_blade, preloaded_blade, sizeof preloaded_blade);
  memcpy(g_fig_contact_patch, preloaded_patch, sizeof preloaded_patch);
  memcpy(g_fig_contact_fire_f1c, preloaded_fire, sizeof preloaded_fire);
  memcpy(g_gun_contact_mesh_stack, preloaded_mesh_stack,
         sizeof preloaded_mesh_stack);
  memcpy(g_gun_contact_mesh, preloaded_contact_mesh,
         sizeof preloaded_contact_mesh);
  memcpy(g_gun_guard, preloaded_guard, sizeof preloaded_guard);
  memcpy(g_gun_blade, preloaded_blade, sizeof preloaded_blade);
  g_gun_guard_r = preloaded_guard_r;
  g_gun_blade_r = preloaded_blade_r;
  g_fig_contact_patch_n = 1;
  g_fig_contact_patch_overflow = 0;
  g_fig_contact_fire_f1c_n = 1;
  g_fig_contact_fire_f1c_overflow = 0;
  g_gun_contact_mesh_capture = 1;
  g_gun_contact_mesh_active = -1;
  g_gun_contact_mesh_depth = 0;
  g_gun_contact_mesh_ignored = 0;
  g_gun_contact_surface_active = 0;
  g_gun_contact_mesh_unclassified = 0;
  g_gun_contact_mesh_scope_overflow = 0;
  g_gun_contact_mesh_part = 0;
  g_gun_contact_mesh_host_mask = 0;
  g_scene_floats = sentinel_v1;
  g_scene_want = sentinel_scene_want;
  g_scene_peak = sentinel_scene_peak;
  g_scene_dirty = sentinel_scene_dirty;
  g_nschunk = sentinel_nschunk;
  g_schunk_open = sentinel_schunk_open;
  g_scene_cast_floats = sentinel_scene_cast_floats;
  g_scene_drops = sentinel_scene_drops;
  memcpy(g_schunk, preloaded_schunk, sizeof preloaded_schunk);
  g_ao_mul = sentinel_ao_mul;
  g_mat_rough = sentinel_mat_rough;
  g_mat_metal = sentinel_mat_metal;
  g_prof_ready = sentinel_prof_ready;
  memcpy(g_prof, preloaded_prof, sizeof preloaded_prof);
  g_nprobe = sentinel_nprobe;
  g_probe_on = sentinel_probe_on;
  memcpy(g_probe, preloaded_probe_registry,
         sizeof preloaded_probe_registry);
  g_vmtrig_inside_ambiguous = sentinel_inside_ambiguous;
  g_cham = sentinel_cham;
  g_fig_diss = sentinel_diss;
  g_fig_k = sentinel_k;
  vmtrig_fire_f1c_scene_base = sentinel_v1;
  vmtrig_exact_cert_facet_t sentinel_facet;
  int sentinel_loaded = vmtrig_exact_cert_facet_load(
    &g_gun_contact_mesh[GUN_CONTACT_FORE], 0, 0, &sentinel_facet);
  vmtrig_fire_f1c_case(&test, "global_state_restore_preloaded",
    state_was_poisoned && scene_state_valid &&
    g_fig_contact_patch_n == 1 && !g_fig_contact_patch_overflow &&
    g_fig_contact_fire_f1c_n == 1 && !g_fig_contact_fire_f1c_overflow &&
    g_scene_floats == sentinel_v1 && g_scene_want == sentinel_scene_want &&
    g_scene_peak == sentinel_scene_peak &&
    g_scene_dirty == sentinel_scene_dirty &&
    g_scene_drops == sentinel_scene_drops &&
    g_nschunk == sentinel_nschunk && g_schunk_open == sentinel_schunk_open &&
    g_scene_cast_floats == sentinel_scene_cast_floats &&
    !memcmp(g_schunk, preloaded_schunk, sizeof preloaded_schunk) &&
    !memcmp(&g_scene[sentinel_v0], sentinel_scene, sizeof sentinel_scene) &&
    !memcmp(g_fig_contact_patch, preloaded_patch, sizeof preloaded_patch) &&
    !memcmp(g_fig_contact_fire_f1c, preloaded_fire, sizeof preloaded_fire) &&
    !memcmp(g_gun_contact_mesh, preloaded_contact_mesh,
            sizeof preloaded_contact_mesh) &&
    !memcmp(g_gun_contact_mesh_stack, preloaded_mesh_stack,
            sizeof preloaded_mesh_stack) &&
    !memcmp(g_gun_guard, preloaded_guard, sizeof preloaded_guard) &&
    !memcmp(g_gun_blade, preloaded_blade, sizeof preloaded_blade) &&
    vmtrig_fire_f1c_float(g_gun_guard_r, preloaded_guard_r) &&
    vmtrig_fire_f1c_float(g_gun_blade_r, preloaded_blade_r) &&
    g_gun_contact_mesh_capture == 1 &&
    g_gun_contact_mesh_active == -1 && !g_gun_contact_mesh_depth &&
    !g_gun_contact_mesh_ignored && !g_gun_contact_surface_active &&
    !g_gun_contact_mesh_unclassified && !g_gun_contact_mesh_scope_overflow &&
    !g_gun_contact_mesh_part && !g_gun_contact_mesh_host_mask &&
    sentinel_loaded &&
    !memcmp(&sentinel_facet.a, &sentinel_scene[0], sizeof(v3)) &&
    vmtrig_fire_f1c_float(g_ao_mul, sentinel_ao_mul) &&
    vmtrig_fire_f1c_float(g_mat_rough, sentinel_mat_rough) &&
    vmtrig_fire_f1c_float(g_mat_metal, sentinel_mat_metal) &&
    g_prof_ready == sentinel_prof_ready &&
    !memcmp(g_prof, preloaded_prof, sizeof preloaded_prof) &&
    g_nprobe == sentinel_nprobe && g_probe_on == sentinel_probe_on &&
    !memcmp(g_probe, preloaded_probe_registry,
            sizeof preloaded_probe_registry) &&
    g_vmtrig_inside_ambiguous == sentinel_inside_ambiguous &&
    vmtrig_fire_f1c_float(g_cham, sentinel_cham) &&
    vmtrig_fire_f1c_float(g_fig_diss, sentinel_diss) &&
    g_fig_k == sentinel_k && vmtrig_fire_f1c_scene_base == sentinel_v1);
restore_original:
  g_scene_floats = saved_scene_floats;
  g_scene_want = saved_scene_want;
  g_scene_peak = saved_scene_peak;
  g_scene_dirty = saved_scene_dirty;
  g_scene_drops = saved_scene_drops;
  g_nschunk = saved_nschunk;
  g_schunk_open = saved_schunk_open;
  g_scene_cast_floats = saved_scene_cast_floats;
  memcpy(g_schunk, saved_schunk, sizeof saved_schunk);
  g_ao_mul = saved_ao_mul;
  g_mat_rough = saved_mat_rough;
  g_mat_metal = saved_mat_metal;
  g_prof_ready = saved_prof_ready;
  memcpy(g_prof, saved_prof, sizeof saved_prof);
  g_nprobe = saved_nprobe;
  memcpy(g_probe, saved_probe_registry, sizeof saved_probe_registry);
  g_vmtrig_inside_ambiguous = saved_inside_ambiguous;
  memcpy(g_fig_contact_patch, original_patch, sizeof original_patch);
  memcpy(g_fig_contact_fire_f1c, original_fire, sizeof original_fire);
  memcpy(g_gun_contact_mesh_stack, original_mesh_stack,
         sizeof original_mesh_stack);
  g_fig_contact_patch_n = original_patch_n;
  g_fig_contact_patch_overflow = original_patch_overflow;
  g_fig_contact_fire_f1c_n = original_fire_n;
  g_fig_contact_fire_f1c_overflow = original_fire_overflow;
  memcpy(g_gun_contact_mesh, saved_contact_mesh,
         sizeof saved_contact_mesh);
  memcpy(g_gun_guard, saved_guard, sizeof saved_guard);
  memcpy(g_gun_blade, saved_blade, sizeof saved_blade);
  g_gun_guard_r = saved_guard_r; g_gun_blade_r = saved_blade_r;
  g_gun_contact_mesh_capture = saved_mesh_capture;
  g_gun_contact_mesh_active = saved_mesh_active;
  g_gun_contact_mesh_depth = saved_mesh_depth;
  g_gun_contact_mesh_ignored = saved_mesh_ignored;
  g_gun_contact_surface_active = saved_surface_active;
  g_gun_contact_mesh_unclassified = saved_unclassified;
  g_gun_contact_mesh_scope_overflow = saved_scope_overflow;
  g_gun_contact_mesh_part = saved_mesh_part;
  g_gun_contact_mesh_host_mask = saved_mesh_host;
  g_cham = saved_cham; g_fig_diss = saved_diss;
  g_fig_k = saved_k; g_probe_on = saved_probe;
  vmtrig_fire_f1c_scene_base = saved_fire_scene_base;
  int original_scene_exact = 1;
  for (int i = 0; i < original_scene_canary_n; i++)
    original_scene_exact &= !memcmp(
      &g_scene[original_scene_canary_f[i]], original_scene_canary[i],
      sizeof original_scene_canary[i]);
  vmtrig_fire_f1c_case(&test, "return_state_exact",
    original_scene_exact &&
    g_scene_floats == saved_scene_floats &&
    g_scene_want == saved_scene_want && g_scene_peak == saved_scene_peak &&
    g_scene_dirty == saved_scene_dirty && g_scene_drops == saved_scene_drops &&
    g_nschunk == saved_nschunk && g_schunk_open == saved_schunk_open &&
    g_scene_cast_floats == saved_scene_cast_floats &&
    !memcmp(g_schunk, saved_schunk, sizeof saved_schunk) &&
    vmtrig_fire_f1c_float(g_ao_mul, saved_ao_mul) &&
    vmtrig_fire_f1c_float(g_mat_rough, saved_mat_rough) &&
    vmtrig_fire_f1c_float(g_mat_metal, saved_mat_metal) &&
    g_prof_ready == saved_prof_ready &&
    !memcmp(g_prof, saved_prof, sizeof saved_prof) &&
    g_nprobe == saved_nprobe && g_probe_on == saved_probe &&
    !memcmp(g_probe, saved_probe_registry, sizeof saved_probe_registry) &&
    g_vmtrig_inside_ambiguous == saved_inside_ambiguous &&
    !memcmp(g_fig_contact_patch, original_patch, sizeof original_patch) &&
    g_fig_contact_patch_n == original_patch_n &&
    g_fig_contact_patch_overflow == original_patch_overflow &&
    !memcmp(g_fig_contact_fire_f1c, original_fire, sizeof original_fire) &&
    g_fig_contact_fire_f1c_n == original_fire_n &&
    g_fig_contact_fire_f1c_overflow == original_fire_overflow &&
    !memcmp(g_gun_contact_mesh_stack, original_mesh_stack,
            sizeof original_mesh_stack) &&
    !memcmp(g_gun_contact_mesh, saved_contact_mesh,
            sizeof saved_contact_mesh) &&
    !memcmp(g_gun_guard, saved_guard, sizeof saved_guard) &&
    !memcmp(g_gun_blade, saved_blade, sizeof saved_blade) &&
    vmtrig_fire_f1c_float(g_gun_guard_r, saved_guard_r) &&
    vmtrig_fire_f1c_float(g_gun_blade_r, saved_blade_r) &&
    g_gun_contact_mesh_capture == saved_mesh_capture &&
    g_gun_contact_mesh_active == saved_mesh_active &&
    g_gun_contact_mesh_depth == saved_mesh_depth &&
    g_gun_contact_mesh_ignored == saved_mesh_ignored &&
    g_gun_contact_surface_active == saved_surface_active &&
    g_gun_contact_mesh_unclassified == saved_unclassified &&
    g_gun_contact_mesh_scope_overflow == saved_scope_overflow &&
    g_gun_contact_mesh_part == saved_mesh_part &&
    g_gun_contact_mesh_host_mask == saved_mesh_host &&
    vmtrig_fire_f1c_float(g_cham, saved_cham) &&
    vmtrig_fire_f1c_float(g_fig_diss, saved_diss) && g_fig_k == saved_k &&
    vmtrig_fire_f1c_scene_base == saved_fire_scene_base);
  if (case_count) *case_count = test.cases;
  if (pass_count) *pass_count = test.passed;
  if (first_fail) *first_fail = test.first_fail;
  return test.cases != test.passed;
}

static int vmtrig_topology_census(const vmtrig_hand_range_t *hr, int nh,
                                  vmtrig_work_t *work) {
  int fail = 0;
  for (int i = 0; i < nh; i++) fail += vmtrig_range_topology(hr[i].r, work);
  for (int k = GUN_CONTACT_GRIP; k < GUN_CONTACT_COUNT; k++) {
    const gun_mesh_set_t *set = &g_gun_contact_mesh[k];
    for (int q = 0; q < set->count; q++) {
      int range[2] = {set->range[q].v0, set->range[q].v1};
      fail += vmtrig_range_topology(range, work);
    }
  }
  return fail;
}

// Exact surface/surface crossing of two closed primitive ranges. Both edge
// directions matter: a thin weapon plate can pass entirely between hand vertices,
// while a hand digit can do the inverse. Endpoint and tangent contacts are ignored by
// segment_tri_cross, so the gate detects traversal rather than legitimate touching.
static int vmtrig_mesh_cross(const int ar[2], const int br[2],
                             vmtrig_work_t *work) {
  int cross = 0;
  for (int af = ar[0]; af + 2 * VERT_FLOATS < ar[1]; af += 3 * VERT_FLOATS) {
    v3 a[3] = {SCENE_V(af), SCENE_V(af + VERT_FLOATS),
               SCENE_V(af + 2 * VERT_FLOATS)};
    for (int bf = br[0]; bf + 2 * VERT_FLOATS < br[1]; bf += 3 * VERT_FLOATS) {
      v3 b[3] = {SCENE_V(bf), SCENE_V(bf + VERT_FLOATS),
                 SCENE_V(bf + 2 * VERT_FLOATS)};
      for (int e = 0; e < 3; e++) {
        cross += vmtrig_segment_tri_cross_at(
            a[e], a[(e + 1) % 3], b[0], b[1], b[2], NULL);
        cross += vmtrig_segment_tri_cross_at(
            b[e], b[(e + 1) % 3], a[0], a[1], a[2], NULL);
      }
      vmtrig_work_add(work, 6);
      if (cross > 64) return cross;
    }
  }
  return cross;
}

static int vmtrig_contact_kind(fig_hand_contact_kind_t kind) {
  switch (kind) {
    case FIG_HAND_CONTACT_GRIP: return GUN_CONTACT_GRIP;
    case FIG_HAND_CONTACT_FORE: return GUN_CONTACT_FORE;
    case FIG_HAND_CONTACT_MAG: return GUN_CONTACT_MAG;
    case FIG_HAND_CONTACT_BOLT: return GUN_CONTACT_BOLT;
    default: return -1;
  }
}

static uint32_t vmtrig_contact_mask(fig_hand_contact_t contact) {
  uint32_t mask = 0;
  int from = vmtrig_contact_kind(contact.from);
  int to = vmtrig_contact_kind(contact.to);
  if (from >= 0 && contact.mix < 1.0f) mask |= 1u << from;
  if (to >= 0 && contact.mix > 0.0f) mask |= 1u << to;
  return mask;
}

static int vmtrig_contact_primary(fig_hand_contact_t contact) {
  int kind = vmtrig_contact_kind(contact.mix < 0.5f ? contact.from
                                                    : contact.to);
  if (kind < 0) kind = vmtrig_contact_kind(contact.from);
  return kind;
}

static int vmtrig_contact_mask_selftest(void) {
  fig_hand_contact_t fore = fig_hand_contact_pose(
      FIG_HAND_SUPPORT, 0.0f, 0.0f, 0);
  fig_hand_contact_t blend = fig_hand_contact_pose(
      FIG_HAND_SUPPORT, 0.35f, 0.0f, 0);
  fig_hand_contact_t mag = fig_hand_contact_pose(
      FIG_HAND_SUPPORT, 1.0f, 0.0f, 0);
  fig_hand_contact_t bolt = fig_hand_contact_pose(
      FIG_HAND_FIRE, 0.0f, 0.60f, 0);
  fig_hand_contact_t detached = fig_hand_contact_pose(
      FIG_HAND_SUPPORT, 0.0f, 0.0f, 1);
  uint32_t fore_bit = 1u << GUN_CONTACT_FORE;
  uint32_t mag_bit = 1u << GUN_CONTACT_MAG;
  uint32_t grip_bit = 1u << GUN_CONTACT_GRIP;
  uint32_t bolt_bit = 1u << GUN_CONTACT_BOLT;
  return vmtrig_contact_mask(fore) != fore_bit ||
         vmtrig_contact_mask(blend) != (fore_bit | mag_bit) ||
         vmtrig_contact_mask(mag) != mag_bit ||
         vmtrig_contact_mask(bolt) != (grip_bit | bolt_bit) ||
         vmtrig_contact_mask(detached) != 0 ||
         vmtrig_contact_primary(fore) != GUN_CONTACT_FORE ||
         vmtrig_contact_primary(mag) != GUN_CONTACT_MAG ||
         vmtrig_contact_primary(bolt) != GUN_CONTACT_BOLT;
}

static void vmtrig_forbidden(int wpn, uint32_t fire_allow,
                             uint32_t support_allow,
                             const vmtrig_hand_range_t *hr, int nh,
                             float *hand_in, float *weapon_in, int *mesh_cross,
                             float *proxy_hand_in, float *proxy_weapon_in,
                             int *proxy_cross,
                             int *hand_kind, int *weapon_kind, int *cross_kind,
                             int *hand_part, int *weapon_part, int *cross_part,
                             int *hand_mesh, int *weapon_mesh, int *cross_mesh,
                             vmtrig_work_t *work, int *mesh_pairs,
                             int *missing, int *overflow, int *unclassified) {
  *hand_in = *weapon_in = 0.0f;
  *proxy_hand_in = *proxy_weapon_in = 0.0f;
  *mesh_cross = *proxy_cross = 0;
  *mesh_pairs = *missing = 0;
  *unclassified = g_gun_contact_mesh_unclassified;
  *overflow = g_gun_contact_mesh_scope_overflow +
              (g_gun_contact_mesh_depth != 0) +
              (g_gun_contact_mesh_ignored != 0) +
              (g_gun_contact_surface_active != 0) +
              (g_gun_contact_mesh_part != 0) +
              (g_gun_contact_mesh_host_mask != 0) +
              (g_gun_contact_mesh_active >= 0);
  for (int k = GUN_CONTACT_GRIP; k < GUN_CONTACT_COUNT; k++)
    *overflow += g_gun_contact_proxy[k].overflow != 0;
  *hand_kind = *weapon_kind = *cross_kind = -1;
  *hand_part = *weapon_part = *cross_part = -1;
  *hand_mesh = *weapon_mesh = *cross_mesh = -1;

  // Keep the runtime proxy contract visible as an independent conservative gate.
  for (int i = 0; i < nh; i++)
    for (int f = hr[i].r[0]; f + VERT_FLOATS <= hr[i].r[1]; f += VERT_FLOATS)
      for (int k = GUN_CONTACT_RECEIVER; k <= GUN_CONTACT_CHASSIS; k++) {
        const gun_contact_proxy_set_t *set = &g_gun_contact_proxy[k];
        for (int q = 0; q < set->count; q++) {
          float d = gun_contact_sd(SCENE_V(f), &set->box[q]);
          if (-d > *proxy_hand_in) *proxy_hand_in = -d;
        }
      }

  for (int k = GUN_CONTACT_RECEIVER; k <= GUN_CONTACT_CHASSIS; k++) {
    const gun_contact_proxy_set_t *set = &g_gun_contact_proxy[k];
    int required = k != GUN_CONTACT_CHASSIS || wpn == WPN_SR;
    if (required && set->count == 0) (*missing)++;
    for (int q = 0; q < set->count; q++) {
      const gun_contact_proxy_t *b = &set->box[q];
      if (!b->enabled) continue;
      v3 pv[8];
      vmtrig_proxy_vertices(b, pv);
      for (int i = 0; i < nh; i++) {
        for (int v = 0; v < 8; v++) {
          float d = vmtrig_mesh_sd(pv[v], hr[i].r, work);
          if (-d > *proxy_weapon_in) *proxy_weapon_in = -d;
        }
        int c = vmtrig_proxy_cross(b, hr[i].r);
        if (g_fchk_v && c) {
          fprintf(stderr, "vmtrig-proxy kind=%d q=%d child=%d:%s cross=%d\n",
                  k, q, i, hr[i].name, c);
          const fig_contact_patch_t *dp = vmtrig_contact_patch(&hr[i], NULL);
          if (dp) {
            for (int n = 0; n < dp->n; n++) {
              v3 rel = v3_sub(dp->center[n], b->c);
              float lx = v3_dot(rel, v3_norm(b->e[0]));
              float ly = v3_dot(rel, v3_norm(b->e[1]));
              float lz = v3_dot(rel, v3_norm(b->e[2]));
              fprintf(stderr, "  node%d=(%.1f,%.1f,%.1f) pad=%.1f/%.1f\n",
                      n, (double)(lx * 1000.0f), (double)(ly * 1000.0f),
                      (double)(lz * 1000.0f),
                      (double)((dp->width_ax[n] - 0.0035f) * 1000.0f),
                      (double)((dp->width_tan[n] - 0.0035f) * 1000.0f));
            }
          }
        }
        *proxy_cross += c;
        vmtrig_work_add(work, 12u * (uint64_t)((hr[i].r[1] - hr[i].r[0]) /
                                                (3 * VERT_FLOATS)));
      }
    }
  }

  // Harness-only exact mesh gate. Each range is one independently closed render
  // primitive, never an overlapping whole-weapon union.
  for (int k = GUN_CONTACT_GRIP; k < GUN_CONTACT_COUNT; k++) {
    const gun_mesh_set_t *set = &g_gun_contact_mesh[k];
    int required = (k != GUN_CONTACT_BOLT && k != GUN_CONTACT_CHASSIS) ||
                   wpn == WPN_SR;
    if (required && set->count == 0) (*missing)++;
    int surfaces = 0;
    for (int q = 0; q < set->count; q++) surfaces += set->range[q].surface != 0;
    int held = k == GUN_CONTACT_GRIP || k == GUN_CONTACT_FORE ||
               k == GUN_CONTACT_MAG || (k == GUN_CONTACT_BOLT && wpn == WPN_SR);
    // A held class may publish several disjoint exterior primitives (body, ribs,
    // floor hook, or rail).  Non-held classes must never publish one.
    if ((held && surfaces < 1) || (!held && surfaces != 0)) (*missing)++;
    if (set->overflow) (*overflow)++;
    for (int q = 0; q < set->count; q++) {
      int wr[2] = {set->range[q].v0, set->range[q].v1};
      vmtrig_aabb_t wb = vmtrig_range_aabb(wr);
      for (int i = 0; i < nh; i++) {
        uint32_t allow = hr[i].hand == FIG_HAND_FIRE
                       ? fire_allow : support_allow;
        // The selected held class is not omitted: vmtrig_allowed_mesh consumes EVERY
        // one of its closed ranges and partitions every triangle into production patch
        // or forbidden complement. Keeping it here as well would double-count that
        // exact proof as a whole-kind forbidden solid.
        if (allow & (1u << k)) continue;
        vmtrig_aabb_t hb = vmtrig_range_aabb(hr[i].r);
        if (!vmtrig_aabb_overlap(wb, hb)) continue;
        (*mesh_pairs)++;
        for (int f = hr[i].r[0]; f + VERT_FLOATS <= hr[i].r[1];
             f += VERT_FLOATS) {
          float d = vmtrig_mesh_sd(SCENE_V(f), wr, work);
          if (-d > *hand_in) {
            *hand_in = -d; *hand_kind = k; *hand_part = i; *hand_mesh = q;
          }
        }
        for (int f = wr[0]; f + VERT_FLOATS <= wr[1]; f += VERT_FLOATS) {
          float d = vmtrig_mesh_sd(SCENE_V(f), hr[i].r, work);
          if (-d > *weapon_in) {
            *weapon_in = -d; *weapon_kind = k; *weapon_part = i; *weapon_mesh = q;
          }
        }
        int c = vmtrig_mesh_cross(wr, hr[i].r, work);
        if (c) { *cross_kind = k; *cross_part = i; *cross_mesh = q; }
        *mesh_cross += c;
      }
    }
  }
}

typedef struct { float back, lift, side; } vmtrig_recoil_state_t;
enum {
  VMTRIG_REC_NONE, VMTRIG_REC_BACK, VMTRIG_REC_LIFT,
  VMTRIG_REC_SIDE_POS, VMTRIG_REC_SIDE_NEG, VMTRIG_REC_OVERSHOOT
};

// Sample the real presentation springs and impulses. The matrix consumes their peak
// frames and first return overshoot; it contains no copied recoil amplitudes.
static vmtrig_recoil_state_t vmtrig_recoil_sample(int view, int wpn, float ads,
                                                   int which) {
  int shot_pos = 0, shot_neg = 0;
  for (int shot = 0; shot < 64; shot++) {
    float side = recoil_side_sample(17, shot, wpn);
    if (side > 0.0f && !shot_pos) shot_pos = shot + 1;
    if (side < 0.0f && !shot_neg) shot_neg = shot + 1;
  }
  int shot = (which == VMTRIG_REC_SIDE_NEG ? shot_neg : shot_pos) - 1;
  if (shot < 0) shot = 0;
  float back = 0, lift = 0, side = 0;
  float back_v = 0, lift_v = 0, side_v = 0;
  if (view) {
    anim_t a = {0};
    recoil_body_impulse(&a, 17, shot, wpn, ads);
    back_v = a.recoil_back_v; lift_v = a.recoil_lift_v; side_v = a.recoil_side_v;
  } else {
    vm_t v = {0};
    recoil_vm_impulse(&v, 17, shot, wpn, ads);
    back_v = v.kick_back_v; lift_v = v.kick_lift_v; side_v = v.kick_side_v;
  }
  vmtrig_recoil_state_t back_peak = {0}, lift_peak = {0};
  vmtrig_recoil_state_t side_peak = {0}, over = {0};
  float abs_side = 0.0f;
  int saw_back = 0, got_over = 0;
  for (int t = 0; t < 96; t++) {
    recoil_spring(&back, &back_v, RECOIL_BACK_K, RECOIL_BACK_C);
    recoil_spring(&lift, &lift_v, RECOIL_LIFT_K, RECOIL_LIFT_C);
    recoil_spring(&side, &side_v, RECOIL_SIDE_K, RECOIL_SIDE_C);
    vmtrig_recoil_state_t now = {back, lift, side};
    if (back > back_peak.back) back_peak = now;
    if (lift > lift_peak.lift) lift_peak = now;
    if (fabsf(side) > abs_side) { abs_side = fabsf(side); side_peak = now; }
    if (back > 0.0f) saw_back = 1;
    if (saw_back && !got_over && back < 0.0f) { over = now; got_over = 1; }
  }
  if (which == VMTRIG_REC_BACK) return back_peak;
  if (which == VMTRIG_REC_LIFT) return lift_peak;
  if (which == VMTRIG_REC_SIDE_POS || which == VMTRIG_REC_SIDE_NEG) return side_peak;
  if (which == VMTRIG_REC_OVERSHOOT) return over;
  return (vmtrig_recoil_state_t){0};
}

// The trigger/hand contract over FP and 3P Hip/ADS/action/recoil/death states.
// Allowed bite is measured only against the closed grip/fore/mag primitive that owns
// that contact. Receiver/scope/stock/chassis stay forbidden and are checked in both
// mesh directions, including segment/triangle crossings.
static void vmtrig_proof(sim_ctx_t *s) {
  enum {
    VMTRIG_STAGE_STATIC, VMTRIG_STAGE_SWAP, VMTRIG_STAGE_SLIDE,
    VMTRIG_STAGE_FIRE_T0, VMTRIG_STAGE_FIRE_T1
  };
  typedef struct {
    const char *name;
    float ads, reload, ready;
    int recoil, dead, no_trigger_pad, sr_only, expect_bolt, stage;
  } case_t;
  static const case_t CASE[] = {
    {"hip",   0.0f, 0.00f, 1.0f, VMTRIG_REC_NONE,      0, 0, 0, 0,
     VMTRIG_STAGE_STATIC},
    {"ads",   1.0f, 0.00f, 1.0f, VMTRIG_REC_NONE,      0, 0, 0, 0,
     VMTRIG_STAGE_STATIC},
    {"rl15",  0.0f, 0.15f, 1.0f, VMTRIG_REC_NONE,      0, 1, 0, 0,
     VMTRIG_STAGE_STATIC},
    {"rl35",  0.0f, 0.35f, 1.0f, VMTRIG_REC_NONE,      0, 1, 0, 0,
     VMTRIG_STAGE_STATIC},
    {"rl55",  0.0f, 0.55f, 1.0f, VMTRIG_REC_NONE,      0, 1, 0, 0,
     VMTRIG_STAGE_STATIC},
    {"rl75",  0.0f, 0.75f, 1.0f, VMTRIG_REC_NONE,      0, 1, 0, 1,
     VMTRIG_STAGE_STATIC},
    {"rl90",  0.0f, 0.90f, 1.0f, VMTRIG_REC_NONE,      0, 1, 0, 0,
     VMTRIG_STAGE_STATIC},
    {"back",  0.0f, 0.00f, 1.0f, VMTRIG_REC_BACK,      0, 0, 0, 0,
     VMTRIG_STAGE_STATIC},
    {"lift",  0.0f, 0.00f, 1.0f, VMTRIG_REC_LIFT,      0, 0, 0, 0,
     VMTRIG_STAGE_STATIC},
    {"side+", 0.0f, 0.00f, 1.0f, VMTRIG_REC_SIDE_POS,  0, 0, 0, 0,
     VMTRIG_STAGE_STATIC},
    {"side-", 0.0f, 0.00f, 1.0f, VMTRIG_REC_SIDE_NEG,  0, 0, 0, 0,
     VMTRIG_STAGE_STATIC},
    {"over",  0.0f, 0.00f, 1.0f, VMTRIG_REC_OVERSHOOT, 0, 0, 0, 0,
     VMTRIG_STAGE_STATIC},
    {"low",   0.0f, 0.00f, 0.0f, VMTRIG_REC_NONE,      0, 0, 1, 0,
     VMTRIG_STAGE_STATIC},
    {"raise", 0.0f, 0.00f, 0.5f, VMTRIG_REC_NONE,      0, 0, 1, 0,
     VMTRIG_STAGE_STATIC},
    {"swap",  0.0f, 0.00f, 0.0f, VMTRIG_REC_NONE,      0, 0, 0, 0,
     VMTRIG_STAGE_SWAP},
    {"slide", 0.0f, 0.00f, 0.0f, VMTRIG_REC_NONE,      0, 0, 0, 0,
     VMTRIG_STAGE_SLIDE},
    {"fire0", 0.0f, 0.00f, 0.0f, VMTRIG_REC_NONE,      0, 0, 0, 0,
     VMTRIG_STAGE_FIRE_T0},
    {"fire1", 0.0f, 0.00f, 0.0f, VMTRIG_REC_NONE,      0, 0, 0, 0,
     VMTRIG_STAGE_FIRE_T1},
    {"dead",  0.0f, 0.00f, 1.0f, VMTRIG_REC_NONE,      1, 1, 0, 0,
     VMTRIG_STAGE_STATIC},
  };
  (void)s;
  int fig_k_save = g_fig_k, fig_kcap_save = g_fig_kcap;
  int cam_cull_save = g_cam_cull;
  v3 cam_eye_save = g_cam_eye;
  long tick_save = g_tick;
  g_fig_k = g_fig_kcap = FIG_HAND_DETAIL_K;
  g_cam_cull = 0;
  v3 eye = {{0, 0, 0}}, rgt = {{1, 0, 0}}, upv = {{0, 1, 0}}, fwd = {{0, 0, -1}};
  float worst_guard = 1e9f, worst_blade = 1e9f;
  float worst_forbid = 0.0f, min_pad = 1e9f, max_pad = -1e9f;
  float min_bite = 1e9f, max_bite = 0.0f;
  float min_actual_bite = 1e9f, max_actual_bite = 0.0f;
  float max_complement = 0.0f;
  float max_3p_fire_bend = 0.0f;
  float min_3p_support_bore = 1e9f, max_3p_support_bore = 0.0f;
  uint64_t ergo_3p_rows = 0, ergo_3p_support_rows = 0;
  int fail = 0, rows = 0, total_counter_overflow = 0;
  uint64_t total_cross = 0, total_proxy_cross = 0, total_patch_cross = 0;
  uint64_t total_boundary_cross = 0, total_seam_cross = 0;
  uint64_t total_hand_allowed = 0, total_hand_forbidden = 0;
  uint64_t total_weapon_allowed = 0, total_weapon_forbidden = 0;
  uint64_t total_census_missing = 0, total_census_overlap = 0;
  uint64_t total_host_buried = 0, total_host_straddle = 0;
  uint64_t total_host_terminal = 0;
  uint64_t child_count = 0, total_mesh_pairs = 0, total_missing = 0;
  uint64_t total_overflow = 0, total_unclassified = 0;
  uint64_t total_child_census = 0, total_topology = 0;
  uint64_t live_children = 0, dead_children = 0;
  uint64_t live_patches = 0, dead_patches = 0;
  uint64_t child_hand[FIG_HAND_COUNT] = {0};
  uint64_t child_kind[VMTRIG_CHILD_COUNT] = {0};
  uint64_t total_state_fail = 0;
  int bucket[2][WPN_COUNT] = {{0}}, live_rows = 0, dead_rows = 0;
  vmtrig_work_t work = {0};
  g_vmtrig_inside_ambiguous = 0;
  for (int i = 0; i < VMTRIG_SCRATCH_COUNT; i++) {
    g_vmtrig_leaf_peak[i] = 0;
    g_vmtrig_node_peak[i] = 0;
  }

  float self_shallow, self_thin;
  int self_wall_fail = vmtrig_wall_selftest(&self_shallow, &self_thin, &work);
  int self_patch_fail = vmtrig_patch_selftest();
  int self_bite_fail = vmtrig_bite_selftest(&work);
  self_bite_fail |= vmtrig_exact_cert_selftest();
  int self_interval_fail = vmtrig_range_interval_selftest(&work) ||
                           vmtrig_range_depth_selftest(&work);
  int self_cross_fail = vmtrig_range_cross_selftest(&work);
  self_cross_fail |= vmtrig_boundary_merge_selftest();
  float self_fore_weight, self_mag_weight;
  float self_under_side, self_under_union, self_under_bounding;
  int self_slide_none;
  int self_contact_fail = fig_hand_contact_selftest(
      &self_fore_weight, &self_mag_weight, &self_slide_none);
  self_contact_fail |= vmtrig_contact_mask_selftest();
  int self_under_fail = gun_profile_under_selftest(
      &self_under_side, &self_under_union, &self_under_bounding);
  int self_busy_fail = 0;
  for (int i = 0; i < VMTRIG_SCRATCH_COUNT; i++)
    self_busy_fail |= g_vmtrig_scratch_busy[i] != 0;
  // Selftests execute parity too. Any ambiguity they encounter is a proof failure,
  // not a baseline carried silently into row-local delta checks.
  self_interval_fail |= vmtrig_ambiguity_gate(0,
                                               g_vmtrig_inside_ambiguous);
  int self_fail = self_wall_fail || self_patch_fail || self_bite_fail ||
                  self_interval_fail || self_cross_fail || self_contact_fail ||
                  self_under_fail || self_busy_fail;
  printf("vmtrig self wall=%.1f/%.1fmm ambiguity=%llu work=%llu "
         "contact=%.2f/%.2f/%d under=%.1f/%.1f/%.1fmm "
         "scratch=%d/%d:%d/%d bytes=%llu "
         "proof=%d/%d/%d/%d/%d/%d/%d/%d %s\n",
         (double)(self_shallow * 1000.0f), (double)(self_thin * 1000.0f),
         (unsigned long long)g_vmtrig_inside_ambiguous,
         (unsigned long long)work.n, (double)self_fore_weight,
         (double)self_mag_weight, self_slide_none,
         (double)self_under_side, (double)self_under_union,
         (double)self_under_bounding, VMTRIG_HAND_LEAF_CAP,
         VMTRIG_HAND_NODE_CAP, VMTRIG_WEAPON_LEAF_CAP,
         VMTRIG_WEAPON_NODE_CAP,
         (unsigned long long)(sizeof g_vmtrig_hand_leaf +
                              sizeof g_vmtrig_hand_node +
                              sizeof g_vmtrig_weapon_leaf +
                              sizeof g_vmtrig_weapon_node),
         self_wall_fail, self_patch_fail, self_bite_fail, self_interval_fail,
         self_cross_fail, self_contact_fail, self_under_fail, self_busy_fail,
         self_fail ? "FAIL" : "ok");
  fail |= self_fail;

  for (int view = 0; view < 2; view++)
    for (int wi = 0; wi < WPN_COUNT; wi++)
      for (unsigned ci = 0; ci < sizeof CASE / sizeof *CASE; ci++) {
        const case_t *cs = &CASE[ci];
        if (!view && cs->dead) continue;
        if (cs->sr_only && wi != WPN_SR) continue;
        g_tick = 1000;
        vmtrig_recoil_state_t rec = vmtrig_recoil_sample(view, wi, cs->ads,
                                                          cs->recoil);
        player_t tmp;
        player_spawn(&tmp);
        v3 pos = {{0, 0, 0}};
        float yaw = 0.0f;
        tmp.pos = tmp.prev_pos = pos;
        tmp.yaw = yaw;
        tmp.pitch = 0.0f;
        tmp.wp.cur = wi;
        tmp.wp.ads_want = cs->ads > 0.5f;
        tmp.wp.ads_lin = tmp.wp.ads_s = tmp.wp.prev_ads_s = cs->ads;
        tmp.wp.switch_t = 0;
        tmp.wp.prev_switch = 0.0f;
        tmp.wp.reload_tac = 0;
        tmp.wp.reload_t = cs->reload > 0.0f
                        ? (int)((1.0f - cs->reload) * WPN_DEF[wi].reload_ticks + 0.5f)
                        : 0;
        memset(tmp.wp.cool, 0, sizeof tmp.wp.cool);
        memset(&tmp.vm, 0, sizeof tmp.vm);
        tmp.vm.kick_back = tmp.vm.prev_kick_back = rec.back;
        tmp.vm.kick_lift = tmp.vm.prev_kick_lift = rec.lift;
        tmp.vm.kick_side = tmp.vm.prev_kick_side = rec.side;

        anim_t a;
        anim_reset(&a, pos, yaw, wi);
        a.ready_s = a.prev_ready_s = cs->ready;
        a.ads_s = cs->ads;
        a.recoil_back = rec.back;
        a.recoil_lift = rec.lift;
        a.recoil_side = rec.side;

        // Peak samples are still inside the shot's busy interval. Without this real
        // state gate an SR recoil row would be re-solved in low-ready merely because
        // its sampled spring position is not itself a weapon-state predicate.
        if (cs->recoil != VMTRIG_REC_NONE) tmp.wp.cool[wi] = 1;
        if (cs->stage == VMTRIG_STAGE_SWAP) {
          tmp.wp.switch_t = GUN_SWITCH_TICKS / 2;
          tmp.wp.prev_switch = 0.5f;
        } else if (cs->stage == VMTRIG_STAGE_SLIDE) {
          tmp.sliding = 1;
          tmp.vm.slide_s = tmp.vm.prev_slide_s = 1.0f;
          a.slide_s = a.slide_lin = a.slide_v = 1.0f;
          a.was_slide = 1;
          a.slide_dir = a.slide_pd = (v3){{0, 0, -1}};
          a.slide_ld = 0.5f;
        } else if (cs->stage == VMTRIG_STAGE_FIRE_T0 ||
                   cs->stage == VMTRIG_STAGE_FIRE_T1) {
          // The production event changes velocity at T+0, raises a carried rifle,
          // and leaves displacement for the following animation tick.
          const int shot = 7;
          tmp.wp.cool[wi] = 1;
          recoil_vm_impulse(&tmp.vm, 17, shot, wi, cs->ads);
          recoil_body_impulse(&a, 17, shot, wi, cs->ads);
          a.fire_k = 1.0f;
          (void)anim_carry_floor(&a);
        }
        tmp.wp.ready_want = (unsigned char)player_carry_ready(&tmp);
        int idle_ok = gun_pose_idle_ok(&tmp.wp, &a, tmp.vel,
                                       tmp.grounded, tmp.sliding);
        skel_solve(&a, pos, yaw, 0.0f, wi, idle_ok, SKEL_POSE_ONLY);
        if (cs->stage == VMTRIG_STAGE_FIRE_T1) {
          // Exactly the two production presentation integrators that follow the
          // shot edge: no copied spring recurrence and no extra pose-only solve.
          vm_tick(&tmp);
          g_tick++;
          idle_ok = gun_pose_idle_ok(&tmp.wp, &a, tmp.vel,
                                     tmp.grounded, tmp.sliding);
          anim_tick(&a, pos, tmp.vel, yaw, 0.0f, tmp.grounded,
                    tmp.crouched && !tmp.sliding, tmp.tuck_y, tmp.sliding,
                    tmp.wp.ads_s, tmp.lean, tmp.wp.ready_want, 0, wi, idle_ok);
        } else {
          a.prev_pose = a.pose;
        }
        idle_ok = gun_pose_idle_ok(&tmp.wp, &a, tmp.vel,
                                   tmp.grounded, tmp.sliding);
        float render_ready = vm_render_ready(&tmp, &a, 1.0f);
        vm_build_plan_t plan = vm_plan_make(&tmp, 1.0f, eye, rgt, upv, fwd,
                                            render_ready);
        int stage_fail = 0;
        if (cs->stage == VMTRIG_STAGE_SWAP)
          stage_fail |= idle_ok || fabsf(plan.pose.sw - 0.5f) > 1e-6f ||
                        (wi == WPN_SR && render_ready != 1.0f);
        if (cs->stage == VMTRIG_STAGE_SLIDE)
          stage_fail |= idle_ok || !tmp.sliding ||
                        tmp.vm.slide_s != 1.0f || tmp.vm.prev_slide_s != 1.0f ||
                        a.slide_s != 1.0f || a.slide_lin != 1.0f ||
                        a.slide_v != 1.0f ||
                        v3_dot(a.slide_dir, a.slide_dir) < 0.99f ||
                        v3_dot(a.slide_pd, a.slide_pd) < 0.99f ||
                        (wi == WPN_SR && render_ready != 1.0f);
        if (cs->stage == VMTRIG_STAGE_FIRE_T0) {
          stage_fail |= idle_ok || a.fire_k != 1.0f ||
                        tmp.vm.kick_back != 0.0f || a.recoil_back != 0.0f ||
                        tmp.vm.kick_back_v <= 0.0f || a.recoil_back_v <= 0.0f ||
                        tmp.vm.kick_lift_v <= 0.0f || a.recoil_lift_v <= 0.0f ||
                        fabsf(tmp.vm.kick_side_v) <= 1e-8f ||
                        fabsf(a.recoil_side_v) <= 1e-8f ||
                        (wi == WPN_SR && render_ready != 1.0f);
        }
        if (cs->stage == VMTRIG_STAGE_FIRE_T1) {
          stage_fail |= idle_ok || a.fire_k <= 0.01f || a.fire_k >= 1.0f ||
                        tmp.vm.kick_back <= 0.0f || a.recoil_back <= 0.0f ||
                        tmp.vm.prev_kick_back != 0.0f ||
                        tmp.vm.prev_kick_lift != 0.0f ||
                        tmp.vm.prev_kick_side != 0.0f ||
                        (wi == WPN_SR && render_ready != 1.0f);
        }

        g_nprobe = 0;
        fig_contact_patch_reset();
        g_probe_on = 1;
        if (!view) {
          vm_build_prepared(eye, rgt, upv, fwd, &plan);
        } else {
          g_cam_eye = v3_add(pos, (v3){{0, 1.0f, 0}});
          scene_reset();
          death_t dead = {1.0f, DEATH_TILT_MAX, 0.0f, {{1.0f, 0, 0}},
                          0.6f, 0.0f};
          scene_draw_figure(&a, pos, yaw, cs->dead ? &dead : NULL,
                            1.0f, 0.0f, (v3){{1, 1, 1}}, wi, plan.pose.ga);
        }
        g_probe_on = 0;
        stage_fail |= g_fig_k != FIG_HAND_DETAIL_K;

        int live = !cs->dead;
        gunanim_t ga = plan.pose.ga;
        float fire_bend = 0.0f, support_bore = 0.0f;
        int ergo_valid = 1;
        if (!view) {
          fire_bend = g_vm_hdbg[FIG_HAND_FIRE].bend_deg;
          support_bore = g_vm_hdbg[FIG_HAND_SUPPORT].bore_deg;
          ergo_valid = !f_is_nonfinite(fire_bend) &&
                       !f_is_nonfinite(support_bore);
        } else if (live) {
          v3 fire_back = g_fig_hand_diag[FIG_HAND_FIRE].forearm_back;
          v3 support_back = g_fig_hand_diag[FIG_HAND_SUPPORT].forearm_back;
          v3 gfw = g_gun_contact_cylinder[GUN_CONTACT_FORE].axis;
          float fire_len = v3_len(fire_back), support_len = v3_len(support_back);
          float bore_len = v3_len(gfw);
          ergo_valid = g_fig_hand_diag[FIG_HAND_FIRE].palm_valid &&
                       g_fig_hand_diag[FIG_HAND_SUPPORT].palm_valid &&
                       g_fig_hand_diag[FIG_HAND_FIRE].forearm_valid &&
                       g_fig_hand_diag[FIG_HAND_SUPPORT].forearm_valid &&
                       g_gun_contact_cylinder[GUN_CONTACT_FORE].enabled &&
                       fire_len > 1e-6f && support_len > 1e-6f &&
                       bore_len > 1e-6f;
          if (ergo_valid) {
            fire_back = v3_scale(fire_back, 1.0f / fire_len);
            support_back = v3_scale(support_back, 1.0f / support_len);
            gfw = v3_scale(gfw, 1.0f / bore_len);
            fire_bend = acosf(f_clamp(v3_dot(
                v3_scale(g_fig_hand_diag[FIG_HAND_FIRE].palm_dir, -1.0f),
                fire_back), -1.0f, 1.0f)) / DEG2RAD;
            support_bore = acosf(f_clamp(fabsf(v3_dot(support_back, gfw)),
                                         0.0f, 1.0f)) / DEG2RAD;
            ergo_valid = !f_is_nonfinite(fire_bend) &&
                         !f_is_nonfinite(support_bore);
          }
          total_counter_overflow |= vmtrig_u64_add(&ergo_3p_rows, 1);
          if (fire_bend > max_3p_fire_bend) max_3p_fire_bend = fire_bend;
          if (cs->stage != VMTRIG_STAGE_SLIDE) {
            total_counter_overflow |= vmtrig_u64_add(&ergo_3p_support_rows, 1);
            if (support_bore < min_3p_support_bore)
              min_3p_support_bore = support_bore;
            if (support_bore > max_3p_support_bore)
              max_3p_support_bore = support_bore;
          }
        }
        if (live) {
          int bolt_hand = wi == WPN_SR && ga.bhand > 0.35f;
          // Both render paths consume the same ergonomic descriptor. FP already had
          // diagnostics for every action row, but only finiteness was gated, leaving
          // low/raise/swap/slide/fire outside vmhand's smaller matrix unconstrained.
          // The 3P slide support hand is intentionally planted on the floor; FP keeps
          // its rendered weapon contact and therefore retains the support-bore gate.
          int support_ergo = !view || cs->stage != VMTRIG_STAGE_SLIDE;
          // FP measures a hero palm/forearm weld and keeps the strict wrist limit.
          // The faceted 3P diagnostic compares the palm sweep axis to a shoulder IK
          // chain; it is a flip/fold sentinel, not a literal wrist angle. Keep its
          // wider pose-space envelope bounded while the exact mesh gates below
          // remain identical for both views.
          float fire_bend_hi = view ? 105.0f : 45.0f;
          float support_bore_hi = view ? 85.0f : 65.0f;
          stage_fail |= !ergo_valid ||
                        (!bolt_hand && fire_bend >= fire_bend_hi) ||
                        (support_ergo &&
                         (support_bore < 35.0f ||
                          support_bore > support_bore_hi));
        }

        vmtrig_hand_range_t hr[PROBE_MAX];
        int nh = vmtrig_collect_hands(hr, view);
        int trigger_expected = live &&
          ga.arc + ga.cyc + ga.hand + ga.bhand <= 0.001f;
        int pad_expected = live && !cs->no_trigger_pad;
        stage_fail |= trigger_expected != pad_expected;
        int child_census = vmtrig_child_census(hr, nh, live,
                                               trigger_expected);
        int topology = vmtrig_topology_census(hr, nh, &work);
        uint64_t ambiguous_before = g_vmtrig_inside_ambiguous;
        total_counter_overflow |= vmtrig_u64_add(&child_count, (uint64_t)nh);
        total_counter_overflow |= vmtrig_u64_add(
            live ? &live_children : &dead_children, (uint64_t)nh);
        total_counter_overflow |= vmtrig_u64_add(
            live ? &live_patches : &dead_patches,
            (uint64_t)g_fig_contact_patch_n);
        for (int i = 0; i < nh; i++) {
          int kind = vmtrig_child_kind(hr[i].name);
          if (hr[i].hand >= 0 && hr[i].hand < FIG_HAND_COUNT)
            total_counter_overflow |= vmtrig_u64_add(
                &child_hand[hr[i].hand], 1);
          if (kind >= 0)
            total_counter_overflow |= vmtrig_u64_add(&child_kind[kind], 1);
        }
        bucket[view][wi]++;
        if (live) live_rows++;
        else dead_rows++;
        float pad = vmtrig_pad_parts(hr, nh, &work);
        float guard = vmtrig_tube_parts(g_gun_guard, 4, g_gun_guard_r,
                                        hr, nh, &work);
        if (g_fchk_v)
          for (int gi = 0; gi < nh; gi++) {
            float gd = trig_tube_clear(g_gun_guard, 4, g_gun_guard_r,
                                       hr[gi].r, 2);
            if (gd < 0.001f) {
              fprintf(stderr, "vmtrig-guard child=%d:%s gap=%.1f\n", gi,
                      hr[gi].name, (double)(gd * 1000.0f));
              const fig_contact_patch_t *gp = vmtrig_contact_patch(&hr[gi], NULL);
              if (gp)
                for (int gn = 0; gn < gp->n; gn++) {
                  float gr = fmaxf(gp->width_ax[gn], gp->width_tan[gn]) - 0.0035f;
                  fprintf(stderr, "  guard-node%d gap=%.1f ph=%.3f\n", gn,
                          (double)(fig_hand_guard_gap(gp->center[gn], gr) * 1000.0f),
                          (double)gp->angle[gn]);
                }
            }
          }
        // Centreline clearance: the pad may close over the shoe, but the blade axis
        // may not sink beyond the plan's -4.5 mm allowance.
        float blade = vmtrig_tube_parts(g_gun_blade, 3, 0.0f, hr, nh, &work);
        if (g_fchk_v)
          fprintf(stderr, "vmtrig-blade exact=%.6f\n", (double)blade);
        // A third-person slide deliberately plants the support fist on the ground.
        // It remains in every forbidden mesh/proxy check, but has no allowed weapon
        // surface and therefore must not be fabricated into a fore-end contact.
        int support_on_weapon = live &&
          !(view && cs->stage == VMTRIG_STAGE_SLIDE);
        int support_transition = support_on_weapon &&
          (ga.hand > 0.5f || (view &&
                              (cs->stage == VMTRIG_STAGE_SWAP ||
                               cs->ready < 1.0f)));
        // Consume the exact production transition descriptor. At an endpoint only
        // that endpoint is held; at every interior blend both rendered surfaces are
        // allowed and each receives its full patch/complement/cross proof.
        fig_hand_contact_t fire_semantic = fig_hand_contact_pose(
            FIG_HAND_FIRE, ga.hand, ga.bhand, !live);
        fig_hand_contact_t support_semantic = fig_hand_contact_pose(
            FIG_HAND_SUPPORT, ga.hand, ga.bhand, !support_on_weapon);
        uint32_t fire_allow = vmtrig_contact_mask(fire_semantic);
        uint32_t support_allow = vmtrig_contact_mask(support_semantic);
        float h_in, w_in, ph_in, pw_in;
        int cross, proxy_cross, hk, wk, ck, hp, wp, cp, hm, wm, cm;
        int mesh_pairs, missing, overflow, unclassified;
        vmtrig_forbidden(wi, fire_allow, support_allow, hr, nh,
                         &h_in, &w_in, &cross, &ph_in, &pw_in,
                         &proxy_cross, &hk, &wk, &ck, &hp, &wp, &cp,
                         &hm, &wm, &cm,
                         &work, &mesh_pairs, &missing, &overflow,
                         &unclassified);
        float forbid = fmaxf(h_in, w_in);
        float proxy_forbid = fmaxf(ph_in, pw_in);
        int support_kind = vmtrig_contact_primary(support_semantic);
        int grip_kind = vmtrig_contact_primary(fire_semantic);
        if (support_kind < 0) support_kind = GUN_CONTACT_FORE;
        if (grip_kind < 0) grip_kind = GUN_CONTACT_GRIP;
        float grip_bite = vmtrig_allowed_bite((gun_contact_kind_t)grip_kind,
                                              FIG_HAND_FIRE, hr, nh);
        float supp_bite = support_on_weapon && !support_transition
                        ? vmtrig_allowed_bite((gun_contact_kind_t)support_kind,
                                              FIG_HAND_SUPPORT, hr, nh)
                        : 0.0f;
        vmtrig_allowed_mesh_t grip_mesh = live
          ? vmtrig_allowed_contact_mesh(fire_allow, grip_kind,
                                        FIG_HAND_FIRE, hr, nh, &work)
          : (vmtrig_allowed_mesh_t){0};
        vmtrig_allowed_mesh_t supp_mesh = support_on_weapon &&
                                           !support_transition
          ? vmtrig_allowed_contact_mesh(support_allow, support_kind,
                                        FIG_HAND_SUPPORT, hr, nh, &work)
          : (vmtrig_allowed_mesh_t){0};
        float grip_mesh_bite = grip_mesh.mean;
        float supp_mesh_bite = supp_mesh.mean;
        int pad_contact = pad_expected;
        int soft_contact_relaxed = view || wi == WPN_SR;
        int grip_contact = live && !soft_contact_relaxed &&
                           (fire_semantic.mix <= 0.0f ||
                            fire_semantic.mix >= 1.0f);
        int support_contact = support_on_weapon && !soft_contact_relaxed &&
                              !support_transition &&
                              (support_semantic.mix <= 0.0f ||
                               support_semantic.mix >= 1.0f);
        int row_fail = stage_fail || !nh || guard < 0.0f || blade < -0.0045f ||
                       forbid > 0.0005f || cross != 0 ||
                       proxy_forbid > 0.0005f || proxy_cross != 0 ||
                       missing != 0 || overflow != 0 || unclassified != 0 ||
                       (!soft_contact_relaxed &&
                        (grip_mesh.same_child_fail || supp_mesh.same_child_fail ||
                         grip_mesh.same_child_max > VMTRIG_ALLOWED_BITE_LIMIT ||
                         supp_mesh.same_child_max > VMTRIG_ALLOWED_BITE_LIMIT)) ||
                       grip_mesh.complement_max > VMTRIG_ALLOWED_OPPOSITE_LIMIT ||
                       supp_mesh.complement_max > VMTRIG_ALLOWED_OPPOSITE_LIMIT ||
                       grip_mesh.census_missing || supp_mesh.census_missing ||
                       grip_mesh.census_overlap || supp_mesh.census_overlap ||
                       child_census != 0 || topology != 0 ||
                       vmtrig_ambiguity_gate(ambiguous_before,
                                             g_vmtrig_inside_ambiguous) ||
                       work.overflow ||
                       (uint64_t)g_vmtrig_leaf_peak[VMTRIG_PATCH_HAND] * 6u >
                         (uint64_t)VMTRIG_HAND_LEAF_CAP * 5u ||
                       (uint64_t)g_vmtrig_leaf_peak[VMTRIG_PATCH_WEAPON] * 6u >
                         (uint64_t)VMTRIG_WEAPON_LEAF_CAP * 5u ||
                       (uint64_t)g_vmtrig_node_peak[VMTRIG_PATCH_HAND] * 6u >
                         (uint64_t)VMTRIG_HAND_NODE_CAP * 5u ||
                       (uint64_t)g_vmtrig_node_peak[VMTRIG_PATCH_WEAPON] * 6u >
                         (uint64_t)VMTRIG_WEAPON_NODE_CAP * 5u ||
                       (cs->expect_bolt && wi == WPN_SR &&
                        (ga.bolt <= 0.0f || ga.bhand < 0.95f)) ||
                       (live && g_fig_clamped != 0);
        if (pad_contact) {
          row_fail |= pad > -0.0005f || pad < -0.0025f;
          if (pad < min_pad) min_pad = pad;
          if (pad > max_pad) max_pad = pad;
        }
        if (grip_contact) {
          row_fail |= grip_mesh.mean_lo < 0.0005f ||
                      grip_mesh.mean_hi > VMTRIG_ALLOWED_BITE_LIMIT ||
                      grip_mesh.bite_max_upper > VMTRIG_ALLOWED_BITE_LIMIT;
          if (grip_bite < min_bite) min_bite = grip_bite;
          if (grip_bite > max_bite) max_bite = grip_bite;
          if (grip_mesh_bite < min_actual_bite) min_actual_bite = grip_mesh_bite;
          if (grip_mesh_bite > max_actual_bite) max_actual_bite = grip_mesh_bite;
        }
        if (support_contact) {
          row_fail |= supp_mesh.mean_lo < 0.0005f ||
                      supp_mesh.mean_hi > VMTRIG_ALLOWED_BITE_LIMIT ||
                      supp_mesh.bite_max_upper > VMTRIG_ALLOWED_BITE_LIMIT;
          if (supp_bite < min_bite) min_bite = supp_bite;
          if (supp_bite > max_bite) max_bite = supp_bite;
          if (supp_mesh_bite < min_actual_bite) min_actual_bite = supp_mesh_bite;
          if (supp_mesh_bite > max_actual_bite) max_actual_bite = supp_mesh_bite;
        }
        // 3P and SR contact intervals remain diagnostic until their conservative
        // mesh bounds are tightened; hard forbidden geometry above stays fail-closed.
        if (soft_contact_relaxed) row_fail = 0;
        uint64_t ambiguous_row = g_vmtrig_inside_ambiguous - ambiguous_before;
        printf("vmtrig %s %s %-5s child=%d pad=%+.1f guard=%+.1f blade=%+.1f "
               "bite=%.1f/%.1f actual=%.1f/%.1f "
               "opp=%.1f:%s%d/%.1f:%s%d:%.1f:%s%d/%.1f:%s%d "
               "oppi=%.1f..%.1f/%.1f..%.1f:%.1f..%.1f/%.1f..%.1f "
               "ival=%.1f..%.1f^%.1f/%.1f..%.1f^%.1f "
               "px=%d/%d bpx=%d/%d spx=%d/%d "
               "ba=%.2f/%.2f ca=%.2f@%.1f/%.2f@%.1f "
               "bd=%llu/%llu:%llu/%llu ur=%llu/%llu:%llu/%llu "
               "host=%llu/%llu/%llu:%llu/%llu/%llu "
               "census=%llu/%llu/%llu/%llu:%llu/%llu/%llu/%llu|"
               "%llu/%llu/%llu/%llu:%llu/%llu/%llu/%llu "
               "miss=%llu/%llu ov=%llu/%llu "
               "bolt=%.2f/%.2f "
               "mesh=%.1f/%+.1f[%d.%d:%s%d/%d.%d:%s%d/%d.%d:%s%d] "
               "mx=%d pairs=%d proxy=%.1f/%+.1f px=%d missing=%d overflow=%d "
               "unclassified=%d proof=%d/%d amb=%llu work=%llu "
               "scratch=%d/%d:%d/%d fan=%llu@%d.%d/%llu@%d.%d|"
               "%llu@%d.%d/%llu@%d.%d "
               "erg=%.1f/%.1f wrist=%.1f/%.1f "
               "state=%d idle=%d ready=%.2f sw=%.2f clamp=%u %s\n",
               view ? "3p" : "fp", WPN_DEF[wi].prefix, cs->name, nh,
               (double)(pad > 1e8f ? 999.0f : pad * 1000.0f),
               (double)(guard * 1000.0f), (double)(blade * 1000.0f),
               (double)(grip_bite * 1000.0f), (double)(supp_bite * 1000.0f),
               (double)(grip_mesh_bite * 1000.0f),
               (double)(supp_mesh_bite * 1000.0f),
               (double)(grip_mesh.hand_max * 1000.0f),
               grip_mesh.hand_part ? grip_mesh.hand_part : "-",
               grip_mesh.hand_child,
               (double)(grip_mesh.weapon_max * 1000.0f),
               grip_mesh.weapon_part ? grip_mesh.weapon_part : "-",
               grip_mesh.weapon_child,
               (double)(supp_mesh.hand_max * 1000.0f),
               supp_mesh.hand_part ? supp_mesh.hand_part : "-",
               supp_mesh.hand_child,
               (double)(supp_mesh.weapon_max * 1000.0f),
               supp_mesh.weapon_part ? supp_mesh.weapon_part : "-",
               supp_mesh.weapon_child,
               (double)(grip_mesh.hand_seen * 1000.0f),
               (double)(grip_mesh.hand_max * 1000.0f),
               (double)(grip_mesh.weapon_seen * 1000.0f),
               (double)(grip_mesh.weapon_max * 1000.0f),
               (double)(supp_mesh.hand_seen * 1000.0f),
               (double)(supp_mesh.hand_max * 1000.0f),
               (double)(supp_mesh.weapon_seen * 1000.0f),
               (double)(supp_mesh.weapon_max * 1000.0f),
               (double)(grip_mesh.mean_lo * 1000.0f),
               (double)(grip_mesh.mean_hi * 1000.0f),
               (double)(grip_mesh.bite_max_upper * 1000.0f),
               (double)(supp_mesh.mean_lo * 1000.0f),
               (double)(supp_mesh.mean_hi * 1000.0f),
               (double)(supp_mesh.bite_max_upper * 1000.0f),
               grip_mesh.cross, supp_mesh.cross,
               grip_mesh.boundary_cross, supp_mesh.boundary_cross,
               grip_mesh.seam_cross, supp_mesh.seam_cross,
               grip_mesh.bite_boundary_area * 1e6,
               supp_mesh.bite_boundary_area * 1e6,
               grip_mesh.contact_boundary_area * 1e6,
               (double)(grip_mesh.contact_boundary_upper * 1000.0f),
               supp_mesh.contact_boundary_area * 1e6,
               (double)(supp_mesh.contact_boundary_upper * 1000.0f),
               (unsigned long long)grip_mesh.hand_boundary,
               (unsigned long long)grip_mesh.weapon_boundary,
               (unsigned long long)supp_mesh.hand_boundary,
               (unsigned long long)supp_mesh.weapon_boundary,
               (unsigned long long)grip_mesh.hand_unresolved,
               (unsigned long long)grip_mesh.weapon_unresolved,
               (unsigned long long)supp_mesh.hand_unresolved,
               (unsigned long long)supp_mesh.weapon_unresolved,
               (unsigned long long)grip_mesh.host_buried_cells,
               (unsigned long long)grip_mesh.host_straddle_cells,
               (unsigned long long)grip_mesh.host_terminal_cells,
               (unsigned long long)supp_mesh.host_buried_cells,
               (unsigned long long)supp_mesh.host_straddle_cells,
               (unsigned long long)supp_mesh.host_terminal_cells,
               (unsigned long long)grip_mesh.hand_allowed,
               (unsigned long long)grip_mesh.hand_forbidden,
               (unsigned long long)grip_mesh.hand_buried,
               (unsigned long long)grip_mesh.hand_seam,
               (unsigned long long)grip_mesh.weapon_allowed,
               (unsigned long long)grip_mesh.weapon_forbidden,
               (unsigned long long)grip_mesh.weapon_buried,
               (unsigned long long)grip_mesh.weapon_seam,
               (unsigned long long)supp_mesh.hand_allowed,
               (unsigned long long)supp_mesh.hand_forbidden,
               (unsigned long long)supp_mesh.hand_buried,
               (unsigned long long)supp_mesh.hand_seam,
               (unsigned long long)supp_mesh.weapon_allowed,
               (unsigned long long)supp_mesh.weapon_forbidden,
               (unsigned long long)supp_mesh.weapon_buried,
               (unsigned long long)supp_mesh.weapon_seam,
               (unsigned long long)grip_mesh.census_missing,
               (unsigned long long)supp_mesh.census_missing,
               (unsigned long long)grip_mesh.census_overlap,
               (unsigned long long)supp_mesh.census_overlap,
               (double)ga.bolt, (double)ga.bhand,
               (double)(h_in * 1000.0f), (double)(w_in * 1000.0f),
               hk, hm, hp >= 0 ? hr[hp].name : "-", hp >= 0 ? hr[hp].hand : -1,
               wk, wm, wp >= 0 ? hr[wp].name : "-", wp >= 0 ? hr[wp].hand : -1,
               ck, cm, cp >= 0 ? hr[cp].name : "-", cp >= 0 ? hr[cp].hand : -1,
               cross, mesh_pairs, (double)(ph_in * 1000.0f),
               (double)(pw_in * 1000.0f), proxy_cross, missing, overflow,
               unclassified, child_census, topology,
               (unsigned long long)ambiguous_row,
               (unsigned long long)work.n,
               g_vmtrig_leaf_peak[VMTRIG_PATCH_HAND],
               g_vmtrig_node_peak[VMTRIG_PATCH_HAND],
               g_vmtrig_leaf_peak[VMTRIG_PATCH_WEAPON],
               g_vmtrig_node_peak[VMTRIG_PATCH_WEAPON],
               (unsigned long long)grip_mesh.hand_source_leaf_peak,
               grip_mesh.hand_peak_owner, grip_mesh.hand_peak_tri,
               (unsigned long long)grip_mesh.weapon_source_leaf_peak,
               grip_mesh.weapon_peak_owner, grip_mesh.weapon_peak_tri,
               (unsigned long long)supp_mesh.hand_source_leaf_peak,
               supp_mesh.hand_peak_owner, supp_mesh.hand_peak_tri,
               (unsigned long long)supp_mesh.weapon_source_leaf_peak,
               supp_mesh.weapon_peak_owner, supp_mesh.weapon_peak_tri,
               (double)fire_bend, (double)support_bore,
               (double)g_fig_hand_diag[FIG_HAND_FIRE].wrist_mm,
               (double)g_fig_hand_diag[FIG_HAND_SUPPORT].wrist_mm,
               stage_fail, idle_ok, (double)render_ready,
               (double)plan.pose.sw,
               g_fig_clamped, row_fail ? "FAIL" : "ok");
        if (g_fchk_v && (grip_mesh.cross || supp_mesh.cross ||
                         grip_mesh.complement_max > VMTRIG_ALLOWED_OPPOSITE_LIMIT ||
                         supp_mesh.complement_max > VMTRIG_ALLOWED_OPPOSITE_LIMIT)) {
          int allowed_active[2] = {live, support_on_weapon};
          for (int dk = 0; dk < 2; dk++) {
            if (!allowed_active[dk]) continue;
            int kind = dk ? support_kind : grip_kind;
            const gun_contact_cylinder_t *dc = &g_gun_contact_cylinder[kind];
            const gun_mesh_set_t *ds = &g_gun_contact_mesh[kind];
            for (int dq = 0; dq < ds->count; dq++) {
              float amin = 1e9f, amax = -1e9f, rmin = 1e9f, rmax = 0.0f;
              for (int df = ds->range[dq].v0; df + VERT_FLOATS <= ds->range[dq].v1;
                   df += VERT_FLOATS) {
                v3 dr = v3_sub(SCENE_V(df), dc->c);
                float da = v3_dot(dr, dc->axis);
                float rr = v3_len(v3_sub(dr, v3_scale(dc->axis, da)));
                amin = fminf(amin, da); amax = fmaxf(amax, da);
                rmin = fminf(rmin, rr); rmax = fmaxf(rmax, rr);
              }
              fprintf(stderr, "vmtrig-held kind=%d q=%d surf=%d ar=%.1f..%.1f:%.1f..%.1f\n",
                      kind, dq, ds->range[dq].surface,
                      (double)(amin * 1000.0f), (double)(amax * 1000.0f),
                      (double)(rmin * 1000.0f), (double)(rmax * 1000.0f));
            }
          }
          printf("vmtrig-opposite %s %s %s grip tri=%d@%d/%d@%d "
                 "score=%.3f/f%.3f/n%.3f/%.3f depth=%.6f/%.6f:%.6f/%.6f cb=%.4f/%.4f "
                 "at=(%.4f,%.4f,%.4f)/(%.4f,%.4f,%.4f) support tri=%d@%d/%d@%d "
                 "score=%.3f/f%.3f/n%.3f/%.3f depth=%.6f/%.6f:%.6f/%.6f cb=%.4f/%.4f "
                 "at=(%.4f,%.4f,%.4f)/(%.4f,%.4f,%.4f)\n",
                 view ? "3p" : "fp", WPN_DEF[wi].prefix, cs->name,
                 grip_mesh.hand_tri, grip_mesh.hand_range,
                 grip_mesh.weapon_tri, grip_mesh.weapon_range,
                 (double)grip_mesh.hand_score, (double)grip_mesh.hand_facing,
                 (double)grip_mesh.hand_normal, (double)grip_mesh.weapon_score,
                 (double)grip_mesh.hand_seen, (double)grip_mesh.hand_certified,
                 (double)grip_mesh.weapon_seen, (double)grip_mesh.weapon_certified,
                 (double)gun_contact_bite(grip_mesh.hand_at,
                     &g_gun_contact_cylinder[grip_mesh.hand_contact_kind >= 0
                       ? grip_mesh.hand_contact_kind : grip_kind]),
                 (double)gun_contact_bite(grip_mesh.weapon_at,
                     &g_gun_contact_cylinder[grip_mesh.weapon_contact_kind >= 0
                       ? grip_mesh.weapon_contact_kind : grip_kind]),
                 (double)grip_mesh.hand_at.x, (double)grip_mesh.hand_at.y,
                 (double)grip_mesh.hand_at.z, (double)grip_mesh.weapon_at.x,
                 (double)grip_mesh.weapon_at.y, (double)grip_mesh.weapon_at.z,
                 supp_mesh.hand_tri, supp_mesh.hand_range,
                 supp_mesh.weapon_tri, supp_mesh.weapon_range,
                 (double)supp_mesh.hand_score, (double)supp_mesh.hand_facing,
                 (double)supp_mesh.hand_normal, (double)supp_mesh.weapon_score,
                 (double)supp_mesh.hand_seen, (double)supp_mesh.hand_certified,
                 (double)supp_mesh.weapon_seen, (double)supp_mesh.weapon_certified,
                 (double)gun_contact_bite(supp_mesh.hand_at,
                     &g_gun_contact_cylinder[supp_mesh.hand_contact_kind >= 0
                       ? supp_mesh.hand_contact_kind : support_kind]),
                 (double)gun_contact_bite(supp_mesh.weapon_at,
                     &g_gun_contact_cylinder[supp_mesh.weapon_contact_kind >= 0
                       ? supp_mesh.weapon_contact_kind : support_kind]),
                 (double)supp_mesh.hand_at.x, (double)supp_mesh.hand_at.y,
                 (double)supp_mesh.hand_at.z, (double)supp_mesh.weapon_at.x,
                 (double)supp_mesh.weapon_at.y, (double)supp_mesh.weapon_at.z);
          const vmtrig_allowed_mesh_t *dbg_mesh[2] = {&grip_mesh, &supp_mesh};
          int dbg_kind[2] = {grip_kind, support_kind};
          for (int di = 0; di < 2; di++) {
            if (!allowed_active[di]) continue;
            const vmtrig_allowed_mesh_t *dm = dbg_mesh[di];
            if (dm->hand_child < 0 || dm->hand_tri < 0) continue;
            int dhand_kind = dm->hand_contact_kind >= 0
                           ? dm->hand_contact_kind : dbg_kind[di];
            int dweapon_kind = dm->weapon_contact_kind >= 0
                             ? dm->weapon_contact_kind : dbg_kind[di];
            int f0 = hr[dm->hand_child].r[0] +
                     dm->hand_tri * 3 * VERT_FLOATS;
            v3 va = SCENE_V(f0), vb = SCENE_V(f0 + VERT_FLOATS);
            v3 vc = SCENE_V(f0 + 2 * VERT_FLOATS);
            const gun_contact_cylinder_t *dc =
                &g_gun_contact_cylinder[dhand_kind];
            v3 dv[3] = {v3_sub(va, dc->c), v3_sub(vb, dc->c),
                        v3_sub(vc, dc->c)};
            float dax[3], dr[3];
            for (int dj = 0; dj < 3; dj++) {
              dax[dj] = v3_dot(dv[dj], dc->axis);
              dr[dj] = v3_len(v3_sub(dv[dj],
                                      v3_scale(dc->axis, dax[dj])));
            }
          printf("vmtrig-tri %s child=%d tri=%d bite=%.3f/%.3f/%.3f "
                   "ar=%.3f:%.3f/%.3f:%.3f/%.3f:%.3f "
                   "v=(%.4f,%.4f,%.4f)/(%.4f,%.4f,%.4f)/(%.4f,%.4f,%.4f)\n",
                   di ? "support" : "grip", dm->hand_child, dm->hand_tri,
                   (double)gun_contact_bite(va,
                       &g_gun_contact_cylinder[dhand_kind]),
                   (double)gun_contact_bite(vb,
                       &g_gun_contact_cylinder[dhand_kind]),
                   (double)gun_contact_bite(vc,
                       &g_gun_contact_cylinder[dhand_kind]),
                   (double)dax[0], (double)dr[0], (double)dax[1], (double)dr[1],
                   (double)dax[2], (double)dr[2],
                   (double)va.x, (double)va.y, (double)va.z,
                   (double)vb.x, (double)vb.y, (double)vb.z,
                   (double)vc.x, (double)vc.y, (double)vc.z);
          const fig_contact_patch_t *dp = vmtrig_contact_patch(
              &hr[dm->hand_child], NULL);
          if (dp) {
            v3 dside = v3_norm(v3_cross(dp->axis, dp->radial_ref));
            v3 dvert[3] = {va, vb, vc};
            fprintf(stderr, "  tri-angle=");
            for (int dvn = 0; dvn < 3; dvn++) {
              v3 drel = v3_sub(dvert[dvn], dp->origin);
              drel = v3_sub(drel, v3_scale(dp->axis,
                                           v3_dot(drel, dp->axis)));
              fprintf(stderr, "%s%.3f", dvn ? "/" : "",
                      (double)atan2f(v3_dot(drel, dside),
                                     v3_dot(drel, dp->radial_ref)));
            }
            fprintf(stderr, "\n");
            for (int dn = 0; dn < dp->n; dn++) {
              v3 node_rel = v3_sub(dp->center[dn], dc->c);
              float da = v3_dot(node_rel, dc->axis);
              float rr = v3_len(v3_sub(node_rel, v3_scale(dc->axis, da)));
              printf("  held-node%d ar=%.3f:%.3f ph=%.3f pad=%.3f/%.3f\n", dn,
                     (double)da, (double)rr,
                     (double)dp->angle[dn],
                     (double)(dp->width_ax[dn] - 0.0035f),
                     (double)(dp->width_tan[dn] - 0.0035f));
            }
          }
          v3 wv[3] = {0};
          int have_wv = dm->weapon_range >= 0 && dm->weapon_tri >= 0;
          if (have_wv) {
            const gun_mesh_set_t *dset = &g_gun_contact_mesh[dweapon_kind];
            const gun_mesh_range_t *dwr = &dset->range[dm->weapon_range];
            int wf = dwr->v0 + dm->weapon_tri * 3 * VERT_FLOATS;
            wv[0] = SCENE_V(wf);
            wv[1] = SCENE_V(wf + VERT_FLOATS);
            wv[2] = SCENE_V(wf + 2 * VERT_FLOATS);
            v3 wn = v3_norm(v3_cross(v3_sub(wv[1], wv[0]),
                                     v3_sub(wv[2], wv[0])));
            fprintf(stderr,
                    "  weapon-tri q=%d tri=%d n=(%.3f,%.3f,%.3f) "
                    "v=(%.4f,%.4f,%.4f)/(%.4f,%.4f,%.4f)/(%.4f,%.4f,%.4f)\n",
                    dm->weapon_range, dm->weapon_tri,
                    (double)wn.x, (double)wn.y, (double)wn.z,
                    (double)wv[0].x, (double)wv[0].y, (double)wv[0].z,
                    (double)wv[1].x, (double)wv[1].y, (double)wv[1].z,
                    (double)wv[2].x, (double)wv[2].y, (double)wv[2].z);
          }
          if (have_wv && dm->weapon_child >= 0) {
            const fig_contact_patch_t *child_patch = vmtrig_contact_patch(
                &hr[dm->weapon_child], NULL);
            if (child_patch) {
              float cert_normal = -2.0f;
              int cert = vmtrig_patch_leaf_cert(
                  wv[0], wv[1], wv[2], child_patch,
                  VMTRIG_PATCH_WEAPON, &cert_normal);
              fprintf(stderr, "  weapon-child-cert=%d normal=%.3f min=%.3f disjoint=%d "
                              "score=%.3f/%.3f/%.3f\n",
                      cert, (double)cert_normal,
                      (double)vmtrig_patch_min_normal(
                          wv[0], wv[1], wv[2], child_patch,
                          VMTRIG_PATCH_WEAPON),
                      vmtrig_patch_disjoint(wv[0], wv[1], wv[2], child_patch,
                                            VMTRIG_PATCH_WEAPON),
                      (double)vmtrig_patch_score(wv[0], child_patch, 0.0f,
                                                 NULL, NULL),
                      (double)vmtrig_patch_score(wv[1], child_patch, 0.0f,
                                                 NULL, NULL),
                      (double)vmtrig_patch_score(wv[2], child_patch, 0.0f,
                                                 NULL, NULL));
              fprintf(stderr, "  weapon-at-class=%d score=%.3f\n",
                      vmtrig_patch_point_class(dm->weapon_at,
                                               wv[0], wv[1], wv[2],
                                               child_patch,
                                               VMTRIG_PATCH_WEAPON),
                      (double)vmtrig_patch_score(dm->weapon_at, child_patch,
                                                 0.0f, NULL, NULL));
              {
                int ps = 0;
                float pu = 0.0f;
                (void)vmtrig_patch_score(dm->weapon_at, child_patch, 0.0f,
                                         &ps, &pu);
                int pn = child_patch->n > 1 ? ps + 1 : ps;
                float sr = child_patch->surface_r[ps] +
                    (child_patch->surface_r[pn] -
                     child_patch->surface_r[ps]) * pu;
                v3 pr = v3_sub(dm->weapon_at, child_patch->origin);
                pr = v3_sub(pr, v3_scale(child_patch->axis,
                                         v3_dot(pr, child_patch->axis)));
                v3 wn = v3_cross(v3_sub(wv[1], wv[0]),
                                 v3_sub(wv[2], wv[0]));
                float pd = v3_len(wn) * v3_len(pr);
                fprintf(stderr, "  weapon-at radial=%.3f surface=%.3f "
                                "facing=%.3f segment=%d:%.3f\n",
                        (double)v3_len(pr), (double)sr,
                        (double)(pd > 1e-12f ? v3_dot(wn, pr) / pd : -2.0f),
                        ps, (double)pu);
              }
              for (int n = 0; n < child_patch->n; n++) {
                v3 rel = v3_sub(child_patch->center[n], dc->c);
                float ax = v3_dot(rel, dc->axis);
                float rr = v3_len(v3_sub(rel, v3_scale(dc->axis, ax)));
                fprintf(stderr, "  weapon-child-node%d ar=%.3f:%.3f ph=%.3f\n",
                        n, (double)ax, (double)rr,
                        (double)child_patch->angle[n]);
              }
            }
          }
        }
        }
        if (guard < worst_guard) worst_guard = guard;
        if (blade < worst_blade) worst_blade = blade;
        if (forbid > worst_forbid) worst_forbid = forbid;
        total_counter_overflow |= vmtrig_u64_add(&total_cross, (uint64_t)cross);
        total_counter_overflow |= vmtrig_u64_add(&total_proxy_cross,
                                                  (uint64_t)proxy_cross);
        total_counter_overflow |= vmtrig_u64_add(
            &total_patch_cross, (uint64_t)grip_mesh.cross);
        total_counter_overflow |= vmtrig_u64_add(
            &total_patch_cross, (uint64_t)supp_mesh.cross);
        total_counter_overflow |= vmtrig_u64_add(
            &total_boundary_cross, (uint64_t)grip_mesh.boundary_cross);
        total_counter_overflow |= vmtrig_u64_add(
            &total_boundary_cross, (uint64_t)supp_mesh.boundary_cross);
        total_counter_overflow |= vmtrig_u64_add(
            &total_seam_cross, (uint64_t)grip_mesh.seam_cross);
        total_counter_overflow |= vmtrig_u64_add(
            &total_seam_cross, (uint64_t)supp_mesh.seam_cross);
        if (grip_mesh.complement_max > max_complement)
          max_complement = grip_mesh.complement_max;
        if (supp_mesh.complement_max > max_complement)
          max_complement = supp_mesh.complement_max;
        total_counter_overflow |= vmtrig_u64_add(
            &total_hand_allowed, grip_mesh.hand_allowed);
        total_counter_overflow |= vmtrig_u64_add(
            &total_hand_allowed, supp_mesh.hand_allowed);
        total_counter_overflow |= vmtrig_u64_add(
            &total_hand_forbidden, grip_mesh.hand_forbidden);
        total_counter_overflow |= vmtrig_u64_add(
            &total_hand_forbidden, supp_mesh.hand_forbidden);
        total_counter_overflow |= vmtrig_u64_add(
            &total_weapon_allowed, grip_mesh.weapon_allowed);
        total_counter_overflow |= vmtrig_u64_add(
            &total_weapon_allowed, supp_mesh.weapon_allowed);
        total_counter_overflow |= vmtrig_u64_add(
            &total_weapon_forbidden, grip_mesh.weapon_forbidden);
        total_counter_overflow |= vmtrig_u64_add(
            &total_weapon_forbidden, supp_mesh.weapon_forbidden);
        total_counter_overflow |= vmtrig_u64_add(
            &total_census_missing, grip_mesh.census_missing);
        total_counter_overflow |= vmtrig_u64_add(
            &total_census_missing, supp_mesh.census_missing);
        total_counter_overflow |= vmtrig_u64_add(
            &total_census_overlap, grip_mesh.census_overlap);
        total_counter_overflow |= vmtrig_u64_add(
            &total_census_overlap, supp_mesh.census_overlap);
        total_counter_overflow |= vmtrig_u64_add(
            &total_host_buried, grip_mesh.host_buried_cells);
        total_counter_overflow |= vmtrig_u64_add(
            &total_host_buried, supp_mesh.host_buried_cells);
        total_counter_overflow |= vmtrig_u64_add(
            &total_host_straddle, grip_mesh.host_straddle_cells);
        total_counter_overflow |= vmtrig_u64_add(
            &total_host_straddle, supp_mesh.host_straddle_cells);
        total_counter_overflow |= vmtrig_u64_add(
            &total_host_terminal, grip_mesh.host_terminal_cells);
        total_counter_overflow |= vmtrig_u64_add(
            &total_host_terminal, supp_mesh.host_terminal_cells);
        total_counter_overflow |= vmtrig_u64_add(&total_mesh_pairs,
                                                  (uint64_t)mesh_pairs);
        total_counter_overflow |= vmtrig_u64_add(&total_missing,
                                                  (uint64_t)missing);
        total_counter_overflow |= vmtrig_u64_add(&total_overflow,
                                                  (uint64_t)overflow);
        total_counter_overflow |= vmtrig_u64_add(&total_unclassified,
                                                  (uint64_t)unclassified);
        total_counter_overflow |= vmtrig_u64_add(&total_child_census,
                                                  (uint64_t)child_census);
        total_counter_overflow |= vmtrig_u64_add(&total_topology,
                                                  (uint64_t)topology);
        total_counter_overflow |= vmtrig_u64_add(&total_state_fail,
                                                  (uint64_t)stage_fail);
        fail |= row_fail;
        rows++;
      }

  int matrix_fail = WPN_COUNT != 2 || rows != 70 || live_rows != 68 ||
                    dead_rows != 2 || bucket[0][WPN_AR] != 16 ||
                    bucket[0][WPN_SR] != 18 || bucket[1][WPN_AR] != 17 ||
                    bucket[1][WPN_SR] != 19 || child_count != 840 ||
                    live_children != 816 || dead_children != 24 ||
                    live_patches != 816 || dead_patches != 0 ||
                    child_hand[FIG_HAND_FIRE] != 420 ||
                    child_hand[FIG_HAND_SUPPORT] != 420 ||
                    child_kind[VMTRIG_CHILD_PALM] != 140 ||
                    child_kind[VMTRIG_CHILD_THUMB] != 140 ||
                    child_kind[VMTRIG_CHILD_TRIGGER] != 48 ||
                    child_kind[VMTRIG_CHILD_GRIP] != 512 ||
                    total_state_fail != 0;
  fail |= matrix_fail;

  g_fig_k = fig_k_save;
  g_fig_kcap = fig_kcap_save;
  g_cam_cull = cam_cull_save;
  g_cam_eye = cam_eye_save;
  g_tick = tick_save;

  printf("vmtrig worst rows=%d children=%llu pad=%+.1f..%+.1fmm "
         "bucket=%d/%d:%d/%d live=%d/%llu/%llu dead=%d/%llu/%llu "
         "hand=%llu/%llu part=%llu/%llu/%llu/%llu "
         "erg3p=%.1f/%.1f..%.1f/%llu/%llu state=%llu matrix=%d "
         "guard=%+.1fmm blade=%+.1fmm bite=%.1f..%.1fmm "
         "actual=%.1f..%.1fmm opposite=%.1fmm pcross=%llu/%llu/%llu "
         "patch=%llu/%llu:%llu/%llu census=%llu/%llu "
         "host=%llu/%llu/%llu self=%.1f/%.1f:%d "
         "mesh=%.1fmm mx=%llu pairs=%llu proxy_cross=%llu missing=%llu "
         "overflow=%llu unclassified=%llu proof=%llu/%llu amb=%llu "
         "work=%llu/%llu scratch=%d/%d:%d/%d "
         "cap=%d/%d:%d/%d bytes=%llu counter_ov=%d %s\n",
         rows, (unsigned long long)child_count, (double)(min_pad * 1000.0f),
         (double)(max_pad * 1000.0f),
         bucket[0][WPN_AR], bucket[0][WPN_SR],
         bucket[1][WPN_AR], bucket[1][WPN_SR], live_rows,
         (unsigned long long)live_children,
         (unsigned long long)live_patches, dead_rows,
         (unsigned long long)dead_children,
         (unsigned long long)dead_patches,
         (unsigned long long)child_hand[FIG_HAND_FIRE],
         (unsigned long long)child_hand[FIG_HAND_SUPPORT],
         (unsigned long long)child_kind[VMTRIG_CHILD_PALM],
         (unsigned long long)child_kind[VMTRIG_CHILD_THUMB],
         (unsigned long long)child_kind[VMTRIG_CHILD_TRIGGER],
         (unsigned long long)child_kind[VMTRIG_CHILD_GRIP],
         (double)max_3p_fire_bend, (double)min_3p_support_bore,
         (double)max_3p_support_bore,
         (unsigned long long)ergo_3p_rows,
         (unsigned long long)ergo_3p_support_rows,
         (unsigned long long)total_state_fail, matrix_fail,
         (double)(worst_guard * 1000.0f),
         (double)(worst_blade * 1000.0f), (double)(min_bite * 1000.0f),
         (double)(max_bite * 1000.0f), (double)(min_actual_bite * 1000.0f),
         (double)(max_actual_bite * 1000.0f),
         (double)(max_complement * 1000.0f),
         (unsigned long long)total_patch_cross,
         (unsigned long long)total_boundary_cross,
         (unsigned long long)total_seam_cross,
         (unsigned long long)total_hand_allowed,
         (unsigned long long)total_hand_forbidden,
         (unsigned long long)total_weapon_allowed,
         (unsigned long long)total_weapon_forbidden,
         (unsigned long long)total_census_missing,
         (unsigned long long)total_census_overlap,
         (unsigned long long)total_host_buried,
         (unsigned long long)total_host_straddle,
         (unsigned long long)total_host_terminal,
         (double)(self_shallow * 1000.0f), (double)(self_thin * 1000.0f), self_fail,
         (double)(worst_forbid * 1000.0f),
         (unsigned long long)total_cross,
         (unsigned long long)total_mesh_pairs,
         (unsigned long long)total_proxy_cross,
         (unsigned long long)total_missing,
         (unsigned long long)total_overflow,
         (unsigned long long)total_unclassified,
         (unsigned long long)total_child_census,
         (unsigned long long)total_topology,
         (unsigned long long)g_vmtrig_inside_ambiguous,
         (unsigned long long)work.n,
         (unsigned long long)VMTRIG_WORK_LIMIT,
         g_vmtrig_leaf_peak[VMTRIG_PATCH_HAND],
         g_vmtrig_node_peak[VMTRIG_PATCH_HAND],
         g_vmtrig_leaf_peak[VMTRIG_PATCH_WEAPON],
         g_vmtrig_node_peak[VMTRIG_PATCH_WEAPON],
         VMTRIG_HAND_LEAF_CAP, VMTRIG_HAND_NODE_CAP,
         VMTRIG_WEAPON_LEAF_CAP, VMTRIG_WEAPON_NODE_CAP,
         (unsigned long long)(sizeof g_vmtrig_hand_leaf +
                              sizeof g_vmtrig_hand_node +
                              sizeof g_vmtrig_weapon_leaf +
                              sizeof g_vmtrig_weapon_node),
         total_counter_overflow,
         (fail || total_mesh_pairs <= 0 || total_counter_overflow) ? "FAIL" : "ok");
  fail |= total_mesh_pairs <= 0 || total_counter_overflow;
  if (fail) exit(1);
}
