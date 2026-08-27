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
static const bot_t *probe_capture_figure(int wpn) {
  g_nprobe = 0;
  g_probe_on = 1;
  scene_reset();
  const bot_t *b = &g_bots[0];
  g_cam_eye = b->pos;
  scene_draw_figure(&b->anim, b->pos, b->yaw, NULL, 1.0f, 0.0f,
                    (v3){{1, 1, 1}}, 0, wpn, gun_anim(&b->wp, 1.0f));
  g_probe_on = 0;
  return b;
}

// Every figure part's measured extent in mm, the bone lengths, the elbow angles
// with the reach each one solves, and the cheek-weld rows.
static void figm_proof(void) {
  // Measure the figure the player actually sees, part by part, in the figure's own
  // frame (x right, y up from the ground, z forward), in mm.
  const bot_t *b = probe_capture_figure(WPN_AR);
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
    {
      v3 hc, ha, org2, rt2, up2, fw2; float hr;
      pose_gun_frame(EP, WPN_AR, &org2, &rt2, &up2, &fw2);
      float gsave = g_gun_girth;
      g_gun_girth = GUN_GIRTH_3P;
      gun_hold(WPN_AR, GH_FORE3, org2, rt2, up2, fw2, &hc, &ha, &hr);
      g_gun_girth = gsave;
      v3 d = v3_sub(EP->wri[0], hc);
      float along = v3_dot(d, ha);
      v3 rad = v3_sub(d, v3_scale(ha, along));
      printf("figm palm  support_gap=%.0fmm (along=%.0f radial=%.0f)\n",
             (double)(v3_len(d) * 1000.0f), (double)(along * 1000.0f),
             (double)((v3_len(rad) - hr) * 1000.0f));
    }
    // THE WELD, and it is the one thing about an aimed pose a screenshot argues about
    // and cannot settle: is the EYE on the sight line, or is the head merely near the
    // rifle? The sight line is the bore raised by the weapon's own sight height
    // (scaled, because the third-person model draws at GUN_GIRTH_3P), and the eye is
    // measured off it in the WEAPON'S frame — up/side, plus the eye relief along the
    // bore.
    for (int wi = 0; wi < WPN_COUNT; wi++) {
      v3 e, hr_, hu_, hf_;
      pose_head_frame(EP, anim_head_cant(&b->anim), &e, &hr_, &hu_, &hf_);
      v3 org3, rt3, up3, fw3;
      pose_gun_frame(EP, wi, &org3, &rt3, &up3, &fw3);
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
             (double)(anim_head_cant(&b->anim) * (180.0f / F_PI)),
             (double)(v3_dot(lat, up3) * 1000.0f),
             (double)(v3_dot(lat, rt3) * 1000.0f),
             (double)(-along * 1000.0f), (double)(v3_len(lat) * 1000.0f));
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
                                      "deltoid", "sho_ball", "dropleg", "neck"};
  // ...and the SAME question about the HEAD, because it is the same defect class and no
  // other proof can see it either: crouching, the head tucks onto the shoulders on
  // purpose, and three separate drops (the crouch tuck, the ADS cheek weld and the
  // spine's hunch) stack into it.
  // Every other name here is emitted by a builder (grep count 2: this list and the
  // scene_part call). "nvg" was the seventh and had a count of 1 — the shroud went
  // when the face became a dark mass with one hard visor, and the list kept its name,
  // so a part that cannot be found silently contributed nothing to the ray parity.
  static const char *const HEADP[] = {"skull", "helm_shell", "helm_ear",
                                      "goggle", "ear", "chinstrap"};
  static const char *const HBODY[] = {"torso", "carrier", "pack", "packstrap",
                                      "trap", "strap", "deltoid", "sho_ball"};
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
    v3 buttp = {{0, 0, 0}}, deepp = {{0, 0, 0}};
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
    anim_reset(&b->anim, b->pos, b->yaw);
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
    anim_hit(&b->anim, b->pos, b->yaw, from, at, c == 0);
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
  anim_reset(&b->anim, b->pos, b->yaw);
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
// and vmtrig's siblings cannot see, per weapon x reload station x press x view.
static void vmtrig_proof(sim_ctx_t *s) {
  // TRIGGER PROOF — A TUBE AGAINST A MESH, not a vertex against a tube. The defect it
  // catches is a THIN weapon part swallowed by a FAT finger, and a point-in-solid test
  // structurally cannot see it: the hand's vertex lattice is ~5 mm by ~8 mm, so a
  // 3.4 mm guard wire passes clean through the holes. Its other number was worse —
  // `reach` was the index chain's end-cap centre against the blade's contact point, and
  // that cap centre IS the contact point plus a constant, so the printed value was one
  // literal in fig_hand read back to itself. A proof whose output cannot move is not a
  // proof.
  //
  // Here the WEAPON's own published centrelines are sampled and each sample is asked
  // how far it is from the HAND's surface, signed by ray parity against the hand's
  // closed manifold — which figcheck and vmcheck are what guarantee. Negative = steel
  // inside flesh.
  //   pad   = the blade's contact point against the hand's surface. A pad presses and
  //           deforms, so a millimetre or two negative is right.
  //   guard = the trigger guard's bow. Must be >= 0: it is a wire in open air.
  //   blade = the trigger blade. Same rule, minus the pad's own squash.
  //   wrist = how far the wrist sits from the grip axis, and `clamp` whether fig_hand
  //           had to slide the weapon out from under it.
  // ADS is not swept: the hand and the weapon are built in the same gun-local frame, so
  // every ads row was byte-identical to its hip row. The RELOAD stations are, because
  // they move the trigger hand, and so is the THIRD-PERSON figure, which this command
  // never ran at all and where the defect is worst.
  player_t tmp = *s->p;
  v3 eye = {{0, 0, 0}}, rgt = {{1, 0, 0}}, upv = {{0, 1, 0}}, fwd = {{0, 0, -1}};
  static const float ST[] = {0.0f, 0.15f, 0.35f, 0.55f, 0.75f, 0.90f};
  static const char *STN[] = {"hip ", "rl15", "rl35", "rl55", "rl75", "rl90"};
  static const char *STN3[] = {"live", "dead"};
  float worst_guard = 1e9f, worst_blade = 1e9f, worst_pad = 1e9f;
  float worst_body = 0.0f, worst_inwp = 0.0f;
  for (int view = 0; view < 2; view++) {
    for (int wi = 0; wi < WPN_COUNT; wi++) {
      for (int st = 0; st < (int)(sizeof ST / sizeof ST[0]); st++) {
        // The figure gets two stations, and the second is the one that matters: a
        // CORPSE. fig_hand's reach clamp exists for the poses the viewmodel can never
        // produce — "on death sprawls both wrists to the chest while the rifle leaves
        // with the trigger hand" — and passing a NULL death_t here would have left
        // exactly that pose untested.
        if (view == 1 && st > 1) continue;
        for (int pr = 0; pr <= 1; pr++) {
          tmp.wp.cur = wi;
          tmp.wp.ads_lin = tmp.wp.ads_s = tmp.wp.prev_ads_s = 0.0f;
          tmp.wp.trig_s = tmp.wp.prev_trig_s = (float)pr;
          tmp.wp.switch_t = 0; tmp.wp.prev_switch = 0.0f;
          tmp.vm.kick = tmp.vm.prev_kick = 0.0f;
          int rt = WPN_DEF[wi].reload_ticks;
          tmp.wp.reload_t = st ? (int)((1.0f - ST[st]) * (float)rt) : 0;

          g_nprobe = 0; g_probe_on = 1;
          scene_reset();
          if (view == 0) {
            vm_build(&tmp, 1.0f, eye, rgt, upv, fwd);
          } else {
            // The same hand on the same rifle, built by the figure: a
            // girth-scaled weapon, an IK wrist and a coarser digit profile.
            g_cam_eye = g_bots[0].pos;
            // A fully toppled, fully slack body about a horizontal axis,
            // with the vertical spin the new death kinematics can leave on
            // it — the state the sprawl and the wall clamp both act in.
            death_t dth = {1.0f, DEATH_TILT_MAX, 0.0f, {{1.0f, 0, 0}}, 0.6f, 0.0f};
            scene_draw_figure(&g_bots[0].anim, g_bots[0].pos, g_bots[0].yaw,
                              st ? &dth : NULL, 1.0f, 0.0f, (v3){{1, 1, 1}},
                              0, wi, gun_anim(&tmp.wp, 1.0f));
          }
          g_probe_on = 0;
          // The hand's triangles: every probe range whose name is a hand part.
          int rng[16], nrng = 0;
          for (int i = 0; i < g_nprobe && nrng < 14; i++) {
            if (!g_probe[i].name) continue;
            if (strcmp(g_probe[i].name, "vm_hands") &&
                strcmp(g_probe[i].name, "trigfinger") &&
                strcmp(g_probe[i].name, "hand")) continue;
            rng[nrng++] = g_probe[i].v0;
            rng[nrng++] = g_probe[i].v1;
          }
          // ...and the TRIGGER FINGER against the receiver box.
          float body = 0.0f;
          for (int i = 0; i < g_nprobe; i++) {
            if (!g_probe[i].name || strcmp(g_probe[i].name, "trigfinger"))
              continue;
            for (int f = g_probe[i].v0; f + 2 < g_probe[i].v1; f += VERT_FLOATS) {
              v3 rel = v3_sub(SCENE_V(f), g_gun_body_c);
              float deep = 1e9f;
              for (int k2 = 0; k2 < 3; k2++) {
                float h = v3_len(g_gun_body_e[k2]);
                if (h < 1e-6f) { deep = 0.0f; break; }
                float o = h - fabsf(v3_dot(rel, v3_scale(g_gun_body_e[k2], 1.0f / h)));
                if (o < deep) deep = o;
              }
              if (deep > body) body = deep;   // deepest vertex inside the box
            }
          }
          // ...and the finger against the WHOLE WEAPON, which is the only test that
          // cannot miss a part nobody thought to publish.
          int grn[4], ngrn = 0;
          for (int i = 0; i < g_nprobe && ngrn < 2; i++)
            if (g_probe[i].name && (!strcmp(g_probe[i].name, "vm_gun") ||
                                    !strcmp(g_probe[i].name, "gun"))) {
              grn[ngrn++] = g_probe[i].v0; grn[ngrn++] = g_probe[i].v1;
            }
          float inwp = 0.0f;
          // THE WHOLE HAND, not just the aimed digit: the other three fingers wrap the
          // pistol grip a few millimetres from the magwell and the magazine, and they
          // are what the player is actually looking at in first person.
          for (int q = 0; q < nrng; q += 2)
            for (int f = rng[q]; f + 2 < rng[q + 1]; f += VERT_FLOATS) {
              float d = -trig_mesh_sd(SCENE_V(f), grn, ngrn);
              if (d > inwp) inwp = d;
            }
          float pad = trig_mesh_sd(g_gun_trig_p, rng, nrng);
          float guard = trig_tube_clear(g_gun_guard, 4, g_gun_guard_r, rng, nrng);
          // The blade is measured on its CENTRELINE (radius 0), not on its surface: a
          // pad legitimately closes OVER a trigger shoe, so the steel's surface sinking
          // a few millimetres into the flesh is the contact rather than a defect — what
          // must not happen is the blade's own axis going deep, which is the shoe being
          // swallowed whole.
          float blade = trig_tube_clear(g_gun_blade, 3, 0.0f, rng, nrng);
          printf("vmtrig %s %s %s trig=%d pad=%+.1f guard=%+.1f blade=%+.1f "
                 "body=%.1f inwpn=%.1f wrist=%.1f clamp=%d\n",
                 view ? "3p" : "vm", WPN_DEF[wi].prefix,
                 view ? STN3[st] : STN[st], pr,
                 (double)(pad * 1000.0f), (double)(guard * 1000.0f),
                 (double)(blade * 1000.0f), (double)(body * 1000.0f),
                 (double)(inwp * 1000.0f), (double)(g_fig_wrist_mm),
                 g_fig_clamped);
          if (guard < worst_guard) worst_guard = guard;
          if (blade < worst_blade) worst_blade = blade;
          if (pad < worst_pad) worst_pad = pad;
          if (body > worst_body) worst_body = body;
          if (inwp > worst_inwp) worst_inwp = inwp;
        }
      }
    }
  }
  // The guard is a wire in open air and may never be inside the hand at all. The blade
  // may be inside it by the pad's own squash and no more.
  printf("vmtrig worst pad=%+.1f mm guard=%+.1f mm (must be >= 0) "
         "blade=%+.1f mm (must be >= %.1f) body=%.1f inwpn=%.1f mm\n",
         (double)(worst_pad * 1000.0f), (double)(worst_guard * 1000.0f),
         (double)(worst_blade * 1000.0f), -4.5, (double)(worst_body * 1000.0f),
         (double)(worst_inwp * 1000.0f));
}

