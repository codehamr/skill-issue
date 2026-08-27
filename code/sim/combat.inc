// =============================================================================
// sim/combat.inc  —  player_respawn, ent_damage, match_on_kill, gun_shoot/
// gun_anim, mv_locomote (the bot+puppet locomotion dispatch), tactical
// steering.
// Proof: parity, botflank, botweapon. One TU: included once by game.c, in
// definition order — moving a definition out is a semantic change.
// Contract: gun_shoot/gun_build/gun_anim are each the ONE copy for every
// view and every shooter; PEN_PATH (1.20 m, core/hitscan.inc) is a path
// length, not a per-weapon budget.
// =============================================================================
static void player_respawn(player_t *p, int ent) {
  int h = human_of_ent(ent);
  // A live-player restart (menu RESTART, harness `restart`) resets the weapon
  // below; the reload SOUND has to stop with it. (Death already cancelled.)
  if (p->wp.reload_t > 0)
    ev_push((event_t){.type = EV_RELOAD_CANCEL, .src = ent, .a = p->pos,
                      .wpn = p->wp.cur});
  int shots = p->shots;  // lifetime counter, part of the trace contract
  int cur = p->wp.cur;   // the weapon SELECTION is a player preference, not
                         // per-life state: dying on the sniper and coming
                         // back holding the AR overrides a choice the player
                         // already made (ammo, recoil and reload state are
                         // still fresh from player_spawn's reset)
  int ph[IN_COUNT];      // press-edge history crosses the spawn too: zeroed,
                         // a button held through death is a fresh press on
                         // the respawn tick (a held trigger fired the SR)
  memcpy(ph, p->prev_held, sizeof ph);
  player_spawn(p);
  p->shots = shots;
  p->wp.cur = cur;
  memcpy(p->prev_held, ph, sizeof ph);
  v3 s = spawn_spot(ent);
  p->pos = p->prev_pos = s;
  p->yaw = atan2f(-s.x, s.z);  // face the arena center
  g_spawn_seq[ent]++;          // snapshot readers see a teleport, not motion
  anim_reset(&g_player_anim[h], p->pos, p->yaw);
  ev_push((event_t){.type = EV_RESPAWN, .dst = ent});
}

static void match_on_kill(int src, int dst, int hs) {
  g_match.kills[src]++;
  g_match.deaths[dst]++;
  if (dst == g_local_ent) g_match.last_killer = src;
  ev_push((event_t){.type = EV_KILL, .src = src, .dst = dst, .k = hs});
  if (!g_match.over && g_match.kills[src] >= sp_frag_limit()) {
    g_match.over = 1;
    g_match.winner = src;
    ev_push((event_t){.type = EV_END, .k = src});
  }
}

// One damage sink for every entity: flash/hp/score bookkeeping, kill and match-end
// detection. `from` is the shooter's origin (it drives the damage direction indicator
// and tells a shot-in-the-back bot where to turn); `at` is the world point the round
// landed on, which decides the death throw and where the sparks come off.
static void rag_launch(rag_t *r, v3 pos, v3 vel, int grounded, v3 from, v3 at,
                       int dmg, int hs) {
  // Momentum from the shot: thrown along the BULLET — the real one, from the muzzle to
  // the point it landed on, so a round fired down from a crate drives the body into the
  // floor instead of sideways.
  v3 bd = v3_sub(at, from);
  float bl = v3_len(bd);
  bd = bl > 1e-4f ? v3_scale(bd, 1.0f / bl) : (v3){{0, 0, 1}};
  v3 d = {{bd.x, 0, bd.z}};
  float dl = sqrtf(d.x * d.x + d.z * d.z);
  // cos(shot pitch): how much of the round's momentum is HORIZONTAL at all.
  float hk = dl;
  if (dl < 1e-3f) { d = (v3){{0, 0, 1}}; dl = 1.0f; }
  d.x /= dl;
  d.z /= dl;
  // Fall into open space: probe the shot direction for a body length of room and swing
  // to the freest side if it is up against a wall.
  v3 eo = {{pos.x, pos.y + 0.9f, pos.z}};
  v3 nn;
  v3 fd = d;
  float swing = 0.0f;   // how far the probe had to turn the fall, for the axis
  float room = trace_world(eo, d, &nn, NULL);
  if (room < 1.1f) {
    // Almost against the wall: swing the fall to the freest side, otherwise the whole
    // skeleton would end up inside geometry and the joint clamp would crush it into a
    // ball.
    float best = room;
    for (int k = 1; k < 9; k++) {
      float ang = (float)((k + 1) / 2) * 0.62f * ((k & 1) ? 1.0f : -1.0f);
      v3 c = {{d.x * cosf(ang) + d.z * sinf(ang), 0,
               -d.x * sinf(ang) + d.z * cosf(ang)}};
      float t = trace_world(eo, c, &nn, NULL);
      if (t > best) { best = t; fd = c; swing = ang; }
      if (best > 3.3f) break;
    }
    room = best;
  }
  // Only a body with nowhere to go gets held back (`room` here is already the freest
  // direction found above).
  float turned = f_clamp(fd.x * d.x + fd.z * d.z, 0.35f, 1.0f);
  // The cap goes OUTSIDE the headshot factor or it is not a cap: inside it, it
  // binds only above 227 damage and the ceiling in its comment is fiction.
  float imp = fminf((DEATH_IMP_BASE + (float)dmg * DEATH_IMP_K) *
                        (hs ? 1.20f : 1.0f), DEATH_IMP_MAX) *
              turned * f_clamp(room / 1.5f, 0.60f, 1.0f);
  // WHICH WAY A BODY GOES DOWN IS TORQUE, and torque is the one quantity the old code
  // threw away. r is the lever from the centre of mass to the point the round landed
  // on; F is the bullet.
  v3 lev = v3_sub(at, (v3){{pos.x, pos.y + DEATH_PIV_Y, pos.z}});
  v3 tau = v3_cross(lev, bd);          // per unit force: metres of lever
  float rw = DEATH_ROT_W(imp);         // ...and how hard the round pushed
  // The wall probe may have swung the FALL away from the bullet to find room; the
  // torque axis has to swing with it, or a corpse tips about an axis its own travel is
  // no longer perpendicular to.
  float cs = cosf(swing), sn = sinf(swing);
  v3 ta = {{tau.x * cs + tau.z * sn, 0.0f, -tau.x * sn + tau.z * cs}};
  float tl = sqrtf(ta.x * ta.x + ta.z * ta.z);
  if (tl < 1e-4f) {                     // straight through the pivot:
    ta = (v3){{fd.z, 0, -fd.x}};        // fall away from the shooter on the
    tl = 0.0f;                          // base spin alone
  } else {
    ta.x /= tl; ta.z /= tl;             // v3_rot_axis needs a UNIT axis
  }
  // The sign of the fall lives in the AXIS and never in the spin: the grounded topple
  // keeps ACCELERATING toward the nearest lying attitude, and a negative rate would
  // fight that integrator instead of feeding it.
  float impk = imp / DEATH_IMP_MAX;
  *r = (rag_t){0};
  // THE VICTIM'S OWN MOMENTUM IS PART OF THE THROW. Everything above is the bullet; a
  // man running at 8 m/s who is shot does not stop and then get pushed, he keeps going
  // and tumbles.
  r->vel = (v3){{vel.x * DEATH_KEEP,
                  DEATH_LIFT + DEATH_LIFT_K * impk +
                      f_clamp(bd.y, -0.5f, 0.5f) * imp * DEATH_PITCH_K +
                      fmaxf(vel.y, 0.0f) * DEATH_KEEP,
                  vel.z * DEATH_KEEP}};
  r->push = (v3){{fd.x * imp * hk, 0.0f, fd.z * imp * hk}};
  r->push_t = DEATH_PUSH_T;
  // A hash of the impact point, mapped to [0, DEATH_REST_VAR) — NON-NEGATIVE by design:
  // allowed to go negative the offset pushes `rest` past the body's own attitude,
  // rag_rest answers with the other side and the corpse rocks between the two forever.
  uint32_t h = (uint32_t)(int32_t)(at.x * 977.0f) * 2654435761u ^
               (uint32_t)(int32_t)(at.y * 991.0f) * 40503u ^
               (uint32_t)(int32_t)(at.z * 1009.0f) * 2246822519u;
  r->rest_off = (float)(h >> 8) / 16777216.0f * DEATH_REST_VAR;
  r->axis = ta;
  // Base + lever + LAUNCH. The launch term is what makes a flung body
  // cartwheel rather than sail: it is the one component that does not care
  // where the round landed, because being thrown is itself a rotation.
  r->spin = fminf(DEATH_SPIN_MAX,
                      (DEATH_SPIN_BASE + DEATH_SPIN_K * tl) * rw +
                          DEATH_AIR_SPIN * impk);
  r->yawv = f_clamp(tau.y * DEATH_YAW_K * rw, -DEATH_YAW_MAX, DEATH_YAW_MAX);
  r->grounded = grounded;
}

// TAKING A ROUND, for whoever took it.
static void anim_hit(anim_t *a, v3 pos, float yaw, v3 from, v3 at, int hs) {
  v3 hd = v3_sub(at, from);
  hd.y = 0.0f;
  if (v3_dot(hd, hd) > 1e-6f) a->hit_d = v3_norm(hd);
  // Saturating rather than summing: a burst SUPPRESSES, it does not fold a man in half.
  a->hit_k = fminf(1.0f, a->hit_k + (hs ? 1.15f : 0.75f));
  // Normalised on the victim's CURRENT height, not HEIGHT_STAND: on a crouched
  // body (crown ~1.2 m) a helmet hit divided by 1.8 read as mid-chest and a
  // chest hit fell below skel_solve's 0.55 waist pivot — the hip-buckle branch
  // fired for a hit that should fold the trunk, i.e. the round landed on the
  // wrong half of the body, on the stance bots hold through every ranged duel.
  float eff_h = HEIGHT_STAND + (HEIGHT_CROUCH - HEIGHT_STAND) * a->crouch_s;
  a->hit_y = f_clamp((at.y - pos.y) / eff_h, 0.0f, 1.0f);
  // Lateral offset in the body's OWN right axis, normalised on a shoulder half-breadth:
  // that is the widest a hitbox admits, so +-1 is a hit on the point of a shoulder and
  // 0 is dead centre line.
  v3 rt = {{cosf(a->leg_yaw), 0.0f, sinf(a->leg_yaw)}};
  (void)yaw;
  v3 rel = v3_sub(at, pos);
  a->hit_x = f_clamp((rel.x * rt.x + rel.z * rt.z) / 0.26f, -1.0f, 1.0f);
  a->hit_w = 0.0f;
}

static void ent_damage(int ent, int dmg, int part, int src, v3 from, v3 at) {
  // A MENU IS NOT A MATCH — FOR THE PLAYER. The boot screen fires the weapon on purpose
  // and those rounds go wherever the arena happens to be, so nothing may put the player
  // on the floor of his own main menu.
  if (g_home && human_of_ent(ent) >= 0) return;
  int hs = part == 3;
  g_match.hits[src]++;
  // The HUMAN branch (three-range rule). Without it a human id above MAX_BOTS
  // falls through to the bot branch below and indexes g_bots[ent - 1] — an
  // out-of-bounds WRITE, before any EV_HIT is ever pushed.
  int vh = human_of_ent(ent);
  if (vh >= 0) {
    player_t *pl = &g_players[vh];
    pl->hp -= dmg;
    if (pl->hp < 0) pl->hp = 0;
    // `a` is the IMPACT POINT, which is what the event header has always documented and
    // what the VFX layer needs to put sparks on the wound.
    ev_push((event_t){.type = hs ? EV_HIT_HEAD : EV_HIT, .a = at, .b = from,
                      .i = dmg, .k = pl->hp, .src = src, .dst = ent});
    // The player's third-person body flinches on the same helper a bot's does.
    pl->flash = BOT_FLASH_TICKS;
    pl->flash_head = hs;
    if (pl->hp > 0) anim_hit(&g_player_anim[vh], pl->pos, pl->yaw, from, at, hs);
    if (pl->hp == 0 && pl->alive) {
      pl->alive = 0;
      pl->respawn_t = PLAYER_RESPAWN_TICKS;
      // Same launch a bot gets, from the same helper and the same round.
      rag_launch(&g_player_rag[vh], pl->pos, pl->vel, pl->grounded, from, at, dmg, hs);
      g_player_corpse_q[vh] = 1;
      // Death abandons a running reload the same way a swap does, and for the
      // same reason: the buffer would announce A LIVE WEAPON over a corpse.
      if (pl->wp.reload_t > 0) {
        ev_push((event_t){.type = EV_RELOAD_CANCEL, .src = ent, .a = pl->pos,
                          .wpn = pl->wp.cur});
        pl->wp.reload_t = 0;
      }
      match_on_kill(src, ent, hs);   // ent, not literal 0: the victim's id
    }
    return;
  }
  bot_t *b = &g_bots[ent - 1];
  b->hp -= dmg;
  if (b->hp < 0) b->hp = 0;
  b->flash = BOT_FLASH_TICKS;
  b->flash_head = hs;
  ev_push((event_t){.type = hs ? EV_HIT_HEAD : EV_HIT, .a = at, .b = from,
                    .i = dmg, .k = b->hp, .src = src, .dst = ent});
  if (b->hp > 0) {
    // The body flinches AWAY from the shooter, so the round's own direction of travel
    // is the pose input — available here for free and nowhere downstream, which is why
    // it is latched rather than derived.
    anim_hit(&b->anim, b->pos, b->yaw, from, at, hs);
    // Flinch: being shot at throws the aim off and briefly makes the bot want to
    // break the line rather than stand and trade. Suppression is a real tactic
    // against a human and it should be one against a bot.
    b->hurt_t = 150;
    b->err += 3.0f;
    b->fl_y += rng_rangef(&g_rng_bot, -2.6f, 2.6f);
    b->fl_p += rng_rangef(&g_rng_bot, -1.4f, 2.2f);
  }
  if (b->hp > 0) {  // getting shot reveals the shooter
    // The shooter's FEET, not `from` (the shot's eye-height origin): last_seen
    // is a feet position everywhere else, and the pre-aim adds chest height on
    // top of it — storing the eye point made a bot revealed by an unseen
    // shooter pre-aim ~1.6 m over their head until first LOS corrected it.
    int score = dmg * 16 + (hs ? 8 : 0);
    if (bot_threat_accept(b, src, score))
      bot_damage_reveal(b, src, score);  // accepted evidence owns focus + priority
    else if (b->tgt == src)
      bot_damage_remember(b, src);       // refresh location, never weaker evidence
  }
  // ... && alive, like the human branch above: lag comp legitimately delivers
  // rewound rounds to a bot that died this same window, and an unguarded death
  // branch re-launched the ragdoll and credited a SECOND kill per corpse hit.
  if (b->hp == 0 && b->alive) {
    b->alive = 0;
    b->topple = 0;
    b->respawn_t = PLAYER_RESPAWN_TICKS;
    // Tactical state does not survive the body.
    b->sliding = 0;
    b->crouch = 0;
    b->crouch_t = 0;
    rag_launch(&b->rag, b->pos, b->vel, b->grounded, from, at, dmg, hs);
    match_on_kill(src, ent, hs);
  }
}

// Spread for WHOEVER is shooting. Keyed on the shooter's weapon state and
// motion, never on which kind of entity it is: a running bot's shots bloom the
// same way a running player's do, and one config slider moves both.
static float gun_spread(const weapon_t *w, v3 vel, int grounded) {
  float hs = sqrtf(vel.x * vel.x + vel.z * vel.z);
  // ADS spread is the tuned base; the structural hip penalty fades out as
  // the sight comes up. Movement and air penalties on top.
  float s = g_sim_wp[w->cur][WP_SPREAD]
          + g_sim_wp[w->cur][WP_HIP_SPREAD] * (1.0f - w->ads_s)
          + w->bloom + GUN_MOVE_SPREAD * f_clamp(hs / g_sim_mv[MV_RUN], 0.0f, 1.5f);
  if (!grounded) s *= GUN_AIR_SPREAD;
  return s < 6.0f ? s : 6.0f;
}

static float weapon_spread(const player_t *p) {
  return gun_spread(&p->wp, p->vel, p->grounded);
}

// ONE shot, for whoever pulled the trigger.
static void gun_shoot(int ent, weapon_t *w, v3 o, float yaw,
                      float pitch, v3 vel, int grounded, uint64_t *rng,
                      anim_t *anim) {
  const wpn_def_t *def = &WPN_DEF[w->cur];
  const float *wp = g_sim_wp[w->cur];
  w->ammo[w->cur]--;
  g_match.shots[ent]++;
  bot_noise_emit(ent, BOT_NOISE_SHOT, o, 34.0f);
  w->cool[w->cur] = (int)((float)TICK_HZ * 60.0f / wp[WP_RPM] + 0.5f);
  if (w->cool[w->cur] < 1) w->cool[w->cur] = 1;

  // Spread is sampled BEFORE this shot's bloom is added: the first shot of a
  // burst fires at base accuracy, the penalty always trails by one shot.
  float spread = gun_spread(w, vel, grounded);
  w->bloom += wp[WP_BLOOM];
  if (w->bloom > GUN_BLOOM_CAP) w->bloom = GUN_BLOOM_CAP;

  float ya = yaw + w->rec_y * DEG2RAD;
  float pa = f_clamp(pitch + w->rec_p * DEG2RAD, -PITCH_MAX, PITCH_MAX);
  v3 dir = {{cosf(pa) * sinf(ya), sinf(pa), -cosf(pa) * cosf(ya)}};
  float r = spread * DEG2RAD * sqrtf(rng_rangef(rng, 0.0f, 1.0f));
  float phi = rng_rangef(rng, 0.0f, F_TAU);
  v3 right = v3_norm(v3_cross(dir, (v3){{0, 1, 0}}));
  v3 upv = v3_cross(right, dir);
  dir = v3_norm(v3_add(dir, v3_add(v3_scale(right, r * cosf(phi)),
                                   v3_scale(upv, r * sinf(phi)))));

  // The kick lands AFTER the shot left the barrel.
  w->rec_p += wp[WP_KICK_UP] * rng_rangef(rng, 0.9f, 1.1f);
  w->rec_y += wp[WP_KICK_SIDE] *
              (GUN_PATTERN[w->idx & 31] + rng_rangef(rng, -0.2f, 0.2f));
  w->rec_p = f_clamp(w->rec_p, -GUN_REC_CAP, GUN_REC_CAP);
  w->rec_y = f_clamp(w->rec_y, -GUN_REC_CAP, GUN_REC_CAP);
  w->punch_v += GUN_PUNCH_SHOT * wp[WP_PUNCH] * GUN_PUNCH_V;
  // The roll's SIGN comes from the pattern step this shot just took, so the view rolls
  // into the direction the muzzle is being pulled.
  {
    float s = GUN_PATTERN[w->idx & 31];
    w->punch_roll_v = (s > 0.001f ? 1.0f : s < -0.001f ? -1.0f
                                   : (w->idx & 1) ? 1.0f : -1.0f) *
                      GUN_PUNCH_SHOT * wp[WP_PUNCH] * GUN_PUNCH_V * GUN_ROLL_K;
  }
  // SPLIT, not converted (the one line that fused both meanings): shots++ is INDEX —
  // every human's lifetime counter, the trace contract — while the viewmodel kick is
  // LOKAL, per-client.
  int sh = human_of_ent(ent);
  if (sh >= 0) g_players[sh].shots++;
  if (ent == g_local_ent && sh >= 0)
    g_players[sh].vm.kick_v += 0.8f + 0.5f * wp[WP_PUNCH];  // gun slams back
  if (anim) anim->fire_k = 1.0f;                  // third-person body: gun kick

  // THE ROUND'S FLIGHT, one segment per piece of material it crosses.
  v3 nrm = {{0, 1, 0}};                 // the face the round finally stopped on
  v3 so = o;                            // this segment's origin
  float base = 0.0f, used = 0.0f;       // travelled so far / spent in material
  float dist = 0.0f;
  int part = 0, hit = -1, wsol = -1, crossings = 0;
  for (;;) {
    v3 wn = {{0, 1, 0}};
    int ws = -1;
    float wall = trace_world(so, dir, &wn, &ws);
    float d = wall;
    hit = ent_trace(ent, so, dir, &d, &part);       // nearer than the wall
    if (hit >= 0) { dist = base + d; break; }
    if (wall >= GUN_RANGE) { dist = base + wall; nrm = wn; wsol = ws; break; }
    v3 entry = v3_add(so, v3_scale(dir, wall));
    float left = PEN_PATH - used;
    // The arena floor plane is not a solid and has no far side, so `ws < 0` stops the
    // round by construction — a round may not tunnel through the ground the way it may
    // through a crate.
    float span = (ws >= 0 && g_solids[ws].pen && crossings < PEN_MAX &&
                  left > 0.0f) ? pen_span(entry, dir) : -1.0f;
    // A round that leaves the far side UNDERGROUND has not gone through
    // anything, it has buried itself: the arena floor is a plane rather than a
    // solid, so nothing below y = 0 would ever stop it and it would fly under
    // the map to GUN_RANGE. Reachable on any box standing on the ground — the
    // 0.55 m centre platform is the obvious one — shot at from above.
    if (span >= 0.0f && v3_add(entry, v3_scale(dir, span)).y <= PEN_EPS)
      span = -1.0f;
    if (span < 0.0f || span > left) {               // it stops in the material
      dist = base + wall; nrm = wn; wsol = ws;
      break;
    }
    // THROUGH. The entry hole is the one the shooter sees and the only extra event this
    // costs; the exit is on the far face, inside cover, and not worth a second event
    // plus a backward trace for a decal nobody is looking at. `k = 1` marks a THROUGH
    // hole.
    ev_push((event_t){.type = EV_IMPACT, .a = entry, .b = wn, .i = ws, .k = 1,
                      .f = v3_len(v3_sub(entry, o)), .src = ent});
    used += span;
    base += wall + span;
    so = v3_add(entry, v3_scale(dir, span));
    // ...AND THE SIDE THE ENEMY IS STANDING ON. Measured through a 0.60 m cover wall
    // with the camera moved to the far side, the ENTRY face changed 109 pixels and the
    // EXIT face exactly ZERO — so the one mechanic this file calls "mostly a PLAYER
    // power" left no read at all where it matters, for the shooter watching in third
    // person and for the man it is being shot through.
    if (ws >= 0 && ws < g_num_solids) {
      // pen_span WELDS abutting solids, so the exit face can belong to a LATER box in
      // the run than the one the round entered — measure the normal and clip the decal
      // against the box the round actually LEAVES: the one containing a probe point
      // just short of the exit.
      int xs = ws;
      v3 pp = v3_sub(so, v3_scale(dir, PEN_EPS));
      for (int si = 0; si < g_num_solids; si++) {
        const solid_t *sb = &g_solids[si];
        if (pp.x >= sb->min.x && pp.x <= sb->max.x && pp.y >= sb->min.y &&
            pp.y <= sb->max.y && pp.z >= sb->min.z && pp.z <= sb->max.z) {
          xs = si;
          break;
        }
      }
      v3 xn = {{0, 0, 0}};
      float bestd = 1e9f;
      for (int ax = 0; ax < 3; ax++) {
        float dlo = fabsf(so.e[ax] - g_solids[xs].min.e[ax]);
        float dhi = fabsf(so.e[ax] - g_solids[xs].max.e[ax]);
        if (dlo < bestd) { bestd = dlo; xn = (v3){{0, 0, 0}}; xn.e[ax] = -1.0f; }
        if (dhi < bestd) { bestd = dhi; xn = (v3){{0, 0, 0}}; xn.e[ax] =  1.0f; }
      }
      ev_push((event_t){.type = EV_IMPACT, .a = so, .b = xn, .i = xs, .k = 1,
                        .f = v3_len(v3_sub(so, o)), .src = ent});
    }
    crossings++;
  }
  int kind = hit >= 0 ? part : dist < GUN_RANGE ? 1 : 0;
  v3 at = v3_add(o, v3_scale(dir, dist));
  ev_push((event_t){.type = EV_FIRE, .a = at, .b = dir, .f = dist,
                    .g = w->ads_s, .pen = used, .i = w->idx, .k = kind,
                    .wpn = w->cur, .src = ent, .dst = hit});
  if (hit >= 0 && !g_predicting) {   // damage is authoritative — server only
    // A ROUND THAT ARRIVES ARRIVES WHOLE. `used` is not read here at all any more — the
    // wall was already paid for by the round either fitting inside PEN_PATH or being
    // left in the material, and there is nothing between those two states.
    ent_damage(hit, def->dmg * (part == 3 ? def->head_x : 1), part, ent, o, at);
  } else if (kind == 1) {
    // `i` carries the solid the round entered — EV_IMPACT leaves it unused, so
    // the mark's clip window costs nothing in the event ring.
    ev_push((event_t){.type = EV_IMPACT, .a = at, .b = nrm, .i = wsol,
                      .f = dist, .src = ent});
  }
  w->idx++;
}

// Per-tick weapon housekeeping every shooter needs: cooldown, the reload
// countdown, recoil recovery once the trigger is off, and the bloom/punch decay.
static void gun_cool(weapon_t *w) {
  for (int i = 0; i < WPN_COUNT; i++)
    if (w->cool[i] > 0) w->cool[i]--;
  if (w->switch_t > 0) w->switch_t--;
  // THE VIEW PUNCH STEPS HERE, at the top of the tick, for both shooters — gun_cool is
  // the one hook that already runs before gun_shoot in the player path AND in bot_tick.
  w->punch_v += (-GUN_PUNCH_K * w->punch - GUN_PUNCH_C * w->punch_v) * TICK_DT;
  w->punch += w->punch_v * TICK_DT;
  w->punch_roll_v += (-GUN_PUNCH_K * w->punch_roll
                      - GUN_PUNCH_C * w->punch_roll_v) * TICK_DT;
  w->punch_roll += w->punch_roll_v * TICK_DT;
}

static void gun_reload_tick(weapon_t *w) {
  if (w->reload_t > 0 && --w->reload_t == 0) {
    w->ammo[w->cur] = WPN_DEF[w->cur].mag;
    w->idx = 0;
  }
}

// ADS ramps linearly over the weapon's own ADS TIME; presentation reads the smoothstep.
static void gun_ads(weapon_t *w, int want_ads) {
  w->ads_lin = f_clamp(w->ads_lin + (want_ads ? 1.0f : -1.0f) *
                       TICK_DT * 1000.0f / g_sim_wp[w->cur][WP_ADS_MS],
                       0.0f, 1.0f);
  w->ads_s = smooth01(w->ads_lin);
}

static void gun_recover(weapon_t *w, int trigger_held) {
  const float *wp = g_sim_wp[w->cur];
  // TRIGGER TRAVEL, and it belongs here for the same reason `mv_walk` is shared: the
  // player and every bot pull the same trigger, and a finger that presses only in first
  // person does not exist on the twenty models the player is shooting at.
  w->prev_trig_s = w->trig_s;
  {
    // The FULL-AUTO subtlety: a held trigger stays back, and a semi-auto's does too
    // until the shooter lets go — so this follows the button, not the shot.
    float want = trigger_held && w->reload_t == 0 && w->switch_t == 0 ? 1.0f : 0.0f;
    float rate = want > w->trig_s ? 36.0f : 60.0f;
    w->trig_s += (want - w->trig_s) * fminf(1.0f, rate * TICK_DT);
  }
  // Recovery only once the trigger is off (or the mag ran dry): during a burst
  // the climb is the shooter's to fight, afterwards the view returns.
  if (!trigger_held || w->ammo[w->cur] == 0 || w->switch_t > 0) {
    float a = mv_alpha(wp[WP_RECOVER_MS]);
    w->rec_p -= w->rec_p * a;
    w->rec_y -= w->rec_y * a;
    if (!trigger_held && w->rec_p > -0.05f && w->rec_p < 0.05f) w->idx = 0;
  }
  w->bloom *= expf(-3.0f * TICK_DT / 0.4f);   // bursts cool off in ~400 ms
  // The punch is NOT stepped here any more — it is a spring and it runs in gun_cool, at
  // the top of the tick, so this tick's impulse survives to the render.
}

// ---------------------------------------------------------------------------
// What the weapon in a pair of hands is DOING this frame: the presentation
// state the MODEL needs and the pose does not.
typedef struct {
  float arc;    // the reload's body arc: the gun comes off the shoulder and back
  float cyc;    // bolt / charging-handle cycle, one lobe late in the reload
  float punch;  // the two beat impulses (magazine SEATS, bolt goes home), summed
  float mag;    // magazine travel out of the well
  float hand;   // support hand off the fore-end and onto the magazine
  float trig;   // trigger travel, 0 = at rest on the blade
  // The BOLT's own 0..1, and it is a monotone RAMP where `cyc` is a hump.
  float bolt;
  float bhand;  // trigger hand off the grip and onto the bolt knob, 0..1
  // MICRO-MOTION, viewmodel only. `jolt` is the signed ring-down of a struck
  // mass — a ~15 Hz decaying oscillation after each beat, zero by the last
  // tick. `heft` is the mass transfer of the magazine itself: positive while
  // it rips OUT (the gun pops up, lighter), negative while the fresh one is
  // driven IN (the gun dips under the hand). Both are pure functions of prog,
  // so they stay on the sim clock like everything else in here.
  float jolt, heft;
} gunanim_t;

static gunanim_t gun_anim(const weapon_t *w, float alpha) {
  gunanim_t g = {0};
  int cur = w->cur;
  g.trig = w->prev_trig_s + (w->trig_s - w->prev_trig_s) * alpha;
  if (w->reload_t <= 0) return g;
  // The SAME duration the sim is counting down, or every beat in the animation
  // lands on a different tick from the sound it is supposed to share.
  float ticks = (float)(w->reload_tac ? WPN_DEF[cur].reload_tac_ticks
                                      : WPN_DEF[cur].reload_ticks);
  float prog = 1.0f - (float)w->reload_t / ticks;
  // The AR's TACTICAL reload is a different mechanism with its own seat fraction (see
  // RELOAD_SEAT_TAC): the seat is the LAST event, the charging handle and the bolt
  // release do not happen.
  int tac_ar = w->reload_tac && cur != WPN_SR;
  float seat = tac_ar ? RELOAD_SEAT_TAC : RELOAD_BEAT[cur][0];
  // THE ARC ENDS ON THE SEAT. At 1.25 it held above 0.85 from 30% to 52% of the reload
  // — 275 ms, 22% of the whole thing, with nothing happening — and then sat at exactly
  // zero for 100 ms before the bolt.
  g.arc = sinf(f_clamp(prog * (tac_ar ? 1.0f / RELOAD_SEAT_TAC : 1.79f),
                       0, 1) * F_PI);
  // THE SECOND HALF OF THE RELOAD WAS DEAD: `arc`'s argument clamps, so it reaches zero
  // at prog 0.559 and stays there, and the seat impulse is spent by 0.73 — from there
  // to the bolt at 0.96 nothing drove the pose at all (45 changed pixels of 921600
  // between prog 0.921 and the standing idle frame).
  g.cyc = tac_ar ? 0.0f
        : sinf(f_clamp((prog - 0.60f) / (0.40f - 1.0f / ticks), 0, 1) * F_PI);
  // THE BOLT BEAT HAS TO REACH ZERO ON THE LAST TICK, AND x*e^(1-x) CANNOT. There are
  // only 0.04 of progress left after the beat — 6 ticks on the AR — so that impulse was
  // still at 79% of its peak on the final tick, and one tick later every term became
  // zero at once: 32 mm of translation and 10.5 degrees of roll gone in 8.3 ms.
  // THE SEAT HITS HARDER AND RINGS DOWN. tau 0.017 rather than 0.021 — the
  // impulse peaks 0.6 of a tick sooner and the front of it is what reads as
  // "slam" — and the amplitude is up a tenth. The tactical seat is the LAST
  // event of its reload, so it carries the bigger number: nothing after it
  // gets to say "live weapon" for it.
  float x1 = (prog - seat) / (tac_ar ? 0.0075f : 0.017f);
  // Guarded because the numerator is DATA: a beat set within one tick of 1.0
  // makes this zero or negative, i.e. a NaN offset on the whole viewmodel — an
  // invisible weapon from a plausible one-character edit.
  float bw = fmaxf((1.0f - RELOAD_BEAT[cur][1]) - 1.0f / ticks, 1.0f / ticks);
  float x2 = f_clamp((prog - RELOAD_BEAT[cur][1]) / bw, 0.0f, 1.0f);
  // No second beat on the tactical AR: the 1.45-amplitude bolt-release punch fired at
  // prog 0.96 into audio silence, for a bolt event that does not exist on this reload.
  // The bolt lobe is SKEWED (pow 0.60): a released bolt is an instant hit with a
  // decay, not a symmetric swell — the peak lands a third of the way into the
  // window and the lobe still reaches exactly zero on the last tick, which is
  // the contract the sin form exists for.
  g.punch = (x1 > 0.0f ? (tac_ar ? 1.42f : 1.32f) * x1 * expf(1.0f - x1) : 0.0f)
          + (tac_ar ? 0.0f : 1.52f * sinf(powf(x2, 0.60f) * F_PI));
  // MICRO-SETTLE: a struck mass rings. ~15 Hz on the empty AR's clock, dead by
  // js 0.2 of progress, so the tail of the seat can never reach the bolt beat.
  // The final beat gets a faster shudder that the (1-x2)^2 envelope forces to
  // exactly zero on the last tick.
  float js = prog - seat;
  g.jolt = (js > 0.0f ? sinf(js * 120.0f) * expf(-js * 26.0f) : 0.0f)
         + (!tac_ar && x2 > 0.0f
              ? 0.6f * sinf((prog - RELOAD_BEAT[cur][1]) * 260.0f)
                     * (1.0f - x2) * (1.0f - x2)
              : 0.0f);
  // THE HAND ARRIVES FIRST, THEN THE MAGAZINE MOVES. They used to start
  // together, so at any mid-travel tick the hand was still a fraction of the way
  // from the fore-end while the magazine had already dropped — 24 mm of daylight
  // between a fist and the thing it is supposed to be holding, which is the
  // whole reason the grip did not read.
  // THE MAG-OUT IS A RIP, ON ITS OWN SOUND. The old travel was a smoothstep at
  // 0.22-0.30 — 60 ms AFTER the "falls free" event at 0.175/0.185 — and a
  // symmetric ease reads as placing, not stripping. u*u starts the magazine
  // slow under the hand and has it accelerating as it clears, and the window
  // sits on the audio event it belongs to.
  g.mag = prog < 0.18f ? 0.0f
        : prog < 0.24f ? ((prog - 0.18f) / 0.06f) * ((prog - 0.18f) / 0.06f)
        : prog < 0.40f ? 1.0f
        // The insert is a POWER curve, not a smoothstep: a magazine goes in slowly and
        // then slams the last centimetre, where a symmetric ease spends its speed in
        // the middle. Fourth power now — the last centimetre is faster still.
        : prog < seat
            ? (tac_ar
                 ? 1.0f - GUN_MAG_U_TAC(prog) * GUN_MAG_U_TAC(prog)
                        * GUN_MAG_U_TAC(prog) * GUN_MAG_U_TAC(prog)
                 : 1.0f - GUN_MAG_U(prog) * GUN_MAG_U(prog)
                        * GUN_MAG_U(prog) * GUN_MAG_U(prog))
            : 0.0f;
  // The magazine's own mass, felt: + while it rips out (the gun pops, lighter),
  // - while the fresh one is driven home. mag*(1-mag) is zero except in the two
  // travel windows, and which window it is is a question about prog.
  {
    float mb = g.mag * (1.0f - g.mag) * 4.0f;
    g.heft = prog < 0.34f ? mb : -0.8f * mb;
  }
  // The support hand leaves the fore-end at 7%, is on the magazine by 17% —
  // before the magazine starts moving, and a third faster than it used to be:
  // a reload is judged by how fast the hand COMMITS. It only starts letting go
  // at 72% (tactical AR: the hand rides the magazine to ITS later seat, then
  // makes a quick return — the seat is the event that ends this mechanism).
  g.hand = smooth01(f_clamp((prog - 0.07f) / 0.10f, 0.0f, 1.0f)) *
           (tac_ar ? smooth01(f_clamp((0.995f - prog) / 0.10f, 0.0f, 1.0f))
                   : smooth01(f_clamp((0.90f - prog) / 0.18f, 0.0f, 1.0f)));
  // The AR racks its charging handle on the same window the sniper works its bolt on,
  // and off the same ramp — one mechanism, two shapes. ...and it does not happen at all
  // on a TACTICAL reload of the AR: the bolt never locked back, so there is nothing to
  // release.
  g.bolt = tac_ar
         ? 0.0f
         : f_clamp((prog - 0.660f) / (0.330f - 1.0f / ticks), 0.0f, 1.0f);
  if (cur == WPN_SR) {
    // A bolt gun's reload ENDS with the action being worked, and that is the half of it
    // a player recognises.
    g.bhand = smooth01(f_clamp((prog - 0.620f) / 0.060f, 0.0f, 1.0f)) *
              smooth01(f_clamp((0.995f - prog) / 0.040f, 0.0f, 1.0f));
  }
  return g;
}

// A body handed to the presentation layer at the respawn so it can outlive the
// entity that was it — see the corpse pool with the VFX. Declared here because
// both death paths call it and the pool is defined a long way below.
static void corpse_add(const anim_t *A, v3 pos, v3 prev_pos, rag_t rag,
                       float yaw, float slack, float prev_slack, gunanim_t gs,
                       int wpn, int pal, int owner);

static void bot_fire(int bi) {
  bot_t *b = &g_bots[bi];
  v3 o = {{b->pos.x, b->pos.y + bot_eye(b), b->pos.z}};
  // The player's shot, fired by a bot: same fire rate, spread model, bloom, recoil
  // climb and pattern, damage and hitscan, all off the live config.
  gun_shoot(bi + 1, &b->wp, o, b->yaw, b->pitch, b->vel, b->grounded,
            &g_rng_bot, &b->anim);
}

// Harness puppet: drives one bot with player-style movement intents so every locomotion
// animation (run all directions, turn, crouch, jump, slide) can be exercised
// deterministically from scripts and filmed with cambot.
static float mv_locomote(bot_t *b, float wx, float wz, float wlen,
                         float sp2d, float walk_sp) {
  if (b->sliding && mv_slide_should_end(b->grounded, 1, sp2d)) b->sliding = 0;
  if (!b->grounded) { mv_air(&b->vel, wx, wz, wlen, sp2d); return sp2d; }
  if (b->sliding) return mv_slide_step(&b->vel, sp2d, wx, wz, wlen);
  mv_walk(&b->vel, wx, wz, wlen, walk_sp);
  return sp2d;
}

static void bot_puppet_tick(bot_t *b, int bi) {
  float diff = wrap_pi(b->pup_face - b->yaw);
  float mt = 5.5f * TICK_DT;
  b->yaw += f_clamp(diff * 9.0f * TICK_DT, -mt, mt);
  // Same lean ramp the player's sim runs — the testbed exists to film the gait
  // the game actually plays, and a lean eased at a different rate than the
  // player's is exactly the kind of second copy this file keeps deleting.
  {
    float r = fabsf(b->pup_lean_t) > fabsf(b->pup_lean) ? LEAN_IN_RATE : LEAN_OUT_RATE;
    b->pup_lean += (b->pup_lean_t - b->pup_lean) * ease_k(r);
  }

  if (b->pup_jump) {
    b->pup_jump = 0;
    if (b->grounded) b->vel.y = mv_jump_speed();
  }
  // The puppet drives the PLAYER's locomotion too.
  float ml = sqrtf(b->pup_mx * b->pup_mx + b->pup_mz * b->pup_mz);
  float wx = ml > 0.01f ? b->pup_mx / ml : 0.0f;
  float wz = ml > 0.01f ? b->pup_mz / ml : 0.0f;
  float wlen = ml > 0.01f ? 1.0f : 0.0f;
  float sp2d = sqrtf(b->vel.x * b->vel.x + b->vel.z * b->vel.z);
  if (b->slide_cd > 0) b->slide_cd--;
  if (b->pup_slide) {
    b->pup_slide = 0;
    if (!b->sliding && b->grounded) {
      // Same launch as the player's: steer into the travel, take the shared boost.
      if (wlen < 0.5f) { wx = sinf(b->yaw); wz = -cosf(b->yaw); wlen = 1.0f; }
      float boost = mv_slide_boost();
      // The standstill teleport is testbed-ONLY and stays local: a script may `puppet
      // slide` from a dead stop, which the shared launch (a multiplier on the speed you
      // already had) cannot express, and the shared path must never assign a velocity —
      // that is the teleport a human has no access to.
      if (sp2d < 0.05f) { b->vel.x = wx * boost; b->vel.z = wz * boost; }
      else mv_slide_launch(&b->vel, sp2d);
      sp2d = boost;
      b->sliding = 1;
      b->slide_cd = SLIDE_COOLDOWN_TICKS;
    }
  }
  float pup_sp = b->pup_crouch ? fminf(b->pup_speed, g_sim_mv[MV_CROUCH_SP])
                               : b->pup_speed;
  sp2d = mv_locomote(b, wx, wz, wlen, sp2d, pup_sp);
  b->vel.y -= GRAVITY * TICK_DT;
  b->grounded = 0;
  // The puppet's crouch is REAL, not visual: collider and hitbox eye follow
  // the stance exactly as bot_tick's do, or every proof staged on a crouched
  // puppet validates a standing collider nobody rendered.
  b->crouch = b->pup_crouch || b->sliding;   // a slide is a crouch, as in bot_tick
  float pup_eye_tgt = b->crouch ? EYE_CROUCH : EYE_STAND;
  b->eye += (pup_eye_tgt - b->eye) * f_clamp(EYE_LERP_RATE * TICK_DT, 0, 1);
  ent_move(&b->pos, &b->vel, &b->grounded,
           b->crouch ? HEIGHT_CROUCH : HEIGHT_STAND);
  arena_clamp(&b->pos);
  anim_tick(&b->anim, b->pos, b->vel, b->yaw, b->pitch, b->grounded,
            b->crouch, 0.0f, b->sliding, b->wp.ads_s, b->pup_lean,
            b->pup_ready, bi + 1);  // REAL entity id: hardwired 1, every
                                    // puppet != bot 0 pushed EV_STEP/EV_LAND
                                    // with .src = 1 and stepped from the wrong
                                    // body — no tuck, see bot_tick
}

// Tactical cover: the direction that puts geometry between this bot and its target.
static int bot_cover_spot_point(const bot_t *b, v3 threat_point, v3 *out) {
  for (int r = 0; r < 2; r++) {          // nearest ring first: reachable beats ideal
    float rad = r ? 5.0f : 2.6f;
    for (int k = 0; k < 8; k++) {
      // The ring STARTS at cov_phase, which the stuck escape advances.
      float a = (float)((k + b->cov_phase) & 7) * F_PI_4;
      v3 c = {{b->pos.x + sinf(a) * rad, b->pos.y, b->pos.z + cosf(a) * rad}};
      if (fabsf(c.x) > ARENA_HALF - 1.0f || fabsf(c.z) > ARENA_HALF - 1.0f) continue;
      if (!aabb_fits(c, PLAYER_RADIUS, HEIGHT_STAND)) continue;
      v3 eo = {{c.x, c.y + EYE_STAND, c.z}};
      if (v3_len(v3_sub(threat_point, eo)) <= 0.5f) continue;
      if (los_clear(eo, threat_point)) continue;  // still exposed
      *out = c;
      return 1;
    }
  }
  return 0;
}

static int bot_cover_spot(const bot_t *b, v3 *out) {
  return bot_cover_spot_point(b, ent_aimpoint(b->tgt), out);
}

// The COVER LEG of bot steering, one copy for both branches that can want it (a visible
// target, and a target the bot only believes in).
static int bot_cover_steer(bot_t *b, const v3 *threat,
                           float *wx, float *wz, float *pace) {
  int need_cover = b->wp.reload_t > 0 || (b->hurt_t > 0 && b->hp < 45);
  if (!need_cover) b->cov_valid = 0;
  // COMMIT to one spot.
  if (need_cover && !b->cov_valid && b->cov_retry == 0) {
    b->cov_valid = threat ? bot_cover_spot_point(b, *threat, &b->cov_p)
                          : bot_cover_spot(b, &b->cov_p);
    if (!b->cov_valid) b->cov_retry = 10;    // ~83 ms; the arena barely moved
  }
  b->in_cover = need_cover && b->cov_valid;
  if (!b->in_cover) return 0;
  float cdx = b->cov_p.x - b->pos.x, cdz = b->cov_p.z - b->pos.z;
  float cl = sqrtf(cdx * cdx + cdz * cdz);
  if (cl < 0.9f) {
    // Arrived: hold here, the line is already broken, and hand the bot to the
    // pre-aim/peek branch at the call site.
    b->cov_valid = 0;
    b->in_cover = 0;
    // ...but do not re-arm the probe on the very NEXT tick.
    b->cov_retry = 10;
    return 1;
  }
  *wx = cdx / cl;
  *wz = cdz / cl;
  *pace = 1.0f;
  return 1;
}

// THE SWING ANCHOR. A flank without a navmesh is one decision — "go stand over THERE
// instead of trading from here" — and the only thing that makes it read as clever
// rather than as a wide strafe is WHERE there is.
static int bot_flank_spot(const bot_t *b, v3 tpos, int side, v3 *out) {
  float dx = b->pos.x - tpos.x, dz = b->pos.z - tpos.z;
  float r = sqrtf(dx * dx + dz * dz);
  if (r < 1.0f) return 0;
  float ux = dx / r, uz = dz / r;
  float rad = f_clamp(r, 8.0f, 14.0f);
  v3 te = {{tpos.x, tpos.y + EYE_STAND - ENT_CHEST_OFF, tpos.z}};
  static const float PHI[3] = {0.95f, 1.40f, 1.85f};
  int have = 0;
  float best = -1.0e30f;
  for (int k = 0; k < 3; k++) {
    float cs = cosf(PHI[k]), sn = sinf(PHI[k]) * (float)side;
    v3 c = {{tpos.x + (ux * cs - uz * sn) * rad, 0.0f,
             tpos.z + (ux * sn + uz * cs) * rad}};
    if (fabsf(c.x) > ARENA_HALF - 1.0f || fabsf(c.z) > ARENA_HALF - 1.0f) continue;
    if (!aabb_fits(c, PLAYER_RADIUS, HEIGHT_STAND)) continue;
    v3 ce = {{c.x, c.y + EYE_STAND, c.z}};
    float score = los_clear(ce, te) ? 0.0f : 100.0f;
    score -= (float)k;
    if (!have || score > best) { best = score; *out = c; have = 1; }
  }
  return have;
}

// One release, so every path that ends a swing also pays for the next one.
static void bot_flank_release(bot_t *b) {
  b->flank_t = 0;
  b->flank_cd = (int)rng_rangef(&g_rng_bot, 600.0f, 1400.0f);
}

// The flank's CLOCKS, at the top of the tick beside slide_cd and prefire_cd rather than
// inside the steering leg — the same lesson slide_cd already carries, and for a sharper
// reason.
static void bot_flank_tick(bot_t *b) {
  if (b->flank_cd > 0) b->flank_cd--;
  if (b->flank_t == 0) return;
  int abort = b->wp.reload_t > 0 || (b->hurt_t > 0 && b->hp < 45) || b->sliding;
  if (--b->flank_t == 0 || abort) bot_flank_release(b);
}

// The WALK half, split out because TWO branches can own a committed swing.
static int bot_flank_walk(bot_t *b, float *wx, float *wz, float *pace) {
  if (b->flank_t == 0) return 0;
  float dx = b->flank_p.x - b->pos.x, dz = b->flank_p.z - b->pos.z;
  float l = sqrtf(dx * dx + dz * dz);
  if (l < 1.4f) {   // arrived: hand the bot back to the duel from the new angle
    b->flank_arrived++;
    bot_flank_release(b);
    return 0;
  }
  *wx = dx / l;
  *wz = dz / l;
  *pace = 1.0f;
  return 1;
}

static void bot_deadlock_release(bot_t *b) {
  b->dl_t = 0;
  b->dl_cd = (int)rng_rangef(&g_rng_bot, 240.0f, 600.0f);
}

// Its clock, at the top of the tick with the others.
static void bot_deadlock_tick(bot_t *b) {
  if (b->dl_cd > 0) b->dl_cd--;
  if (b->dl_t > 0 && --b->dl_t == 0) bot_deadlock_release(b);
}

// THE DEADLOCK LEG. Runs BEFORE cover and the flank, because when a bot is welded to a
// corner the thing that is wrong is usually the destination one of those two chose —
// overriding them is the point.
static int bot_deadlock_steer(bot_t *b, float tdist, float *wx, float *wz,
                              float *pace) {
  if (b->dl_t == 0 && b->dl_cd == 0 && b->pin > BOT_DEADLOCK_PIN &&
      b->tgt >= 0) {
    int can_push = b->hp >= BOT_PUSH_HP && b->wp.reload_t == 0 &&
                   b->wp.ammo[b->wp.cur] > 0 && tdist <= BOT_PUSH_DIST;
    if (can_push) {
      b->dl_p = b->last_seen;
      b->dl_push = 1;
      b->dl_t = (int)rng_rangef(&g_rng_bot, 90.0f, 150.0f);
    } else if (bot_cover_spot_point(b, ent_aimpoint(b->tgt), &b->dl_p)) {
      b->dl_push = 0;
      b->dl_t = (int)rng_rangef(&g_rng_bot, 90.0f, 150.0f);
    } else {
      b->dl_cd = 60;   // nowhere to break to; do not re-probe every tick
    }
  }
  if (b->dl_t == 0) return 0;
  float dx = b->dl_p.x - b->pos.x, dz = b->dl_p.z - b->pos.z;
  float l = sqrtf(dx * dx + dz * dz);
  // A push is done when it is ON him; a break is done when it reaches the spot.
  if (l < (b->dl_push ? 3.0f : 1.2f)) {
    bot_deadlock_release(b);
    return 0;
  }
  *wx = dx / l;
  *wz = dz / l;
  *pace = 1.0f;
  return 1;
}

// The FLANK LEG, shaped exactly like bot_cover_steer above: the trigger, the commitment
// and the release are one quantity each.
static int bot_flank_steer(bot_t *b, const bot_skill_t *sk, float tdist,
                           float *wx, float *wz, float *pace) {
  // Rate, not habit. sk->tac is the one tactic knob (never a gun-handling
  // number), and 0.0007 per tick at tac 0.60 is one attempt per ~20 s of
  // engagement — with the 5..12 s cooldown that lands at a few percent of
  // bot-ticks, the same order the slide sits at.
  if (b->flank_t == 0 && b->flank_cd == 0 && b->grounded &&
      b->wp.reload_t == 0 && !(b->hurt_t > 0 && b->hp < 45) && !b->sliding &&
      tdist > 7.0f && tdist < 28.0f &&
      rng_rangef(&g_rng_bot, 0.0f, 1.0f) < sk->tac * 0.0007f) {
    // The way it was already leaning — a swing the player can read and learn
    // beats a coin flip. strafe_dir is -1/0/+1 and the plant (0) must not fold
    // into one side, or a mirror-symmetric arena gets a 60/40 bias and every
    // bot teaches the player to watch the same shoulder.
    int side = b->strafe_dir ? (b->strafe_dir > 0 ? 1 : -1)
                             : ((rng_next(&g_rng_bot) & 1) ? 1 : -1);
    // last_seen, never ent_pos(tgt): the anchor is planned ONCE, out of what the bot
    // actually observed.
    if (bot_flank_spot(b, b->last_seen, side, &b->flank_p))
      b->flank_t = (int)rng_rangef(&g_rng_bot, 220.0f, 460.0f);
  }
  return bot_flank_walk(b, wx, wz, pace);
}

// The remembered aim point — belief_pos DISPLACED BY THE UNCERTAINTY the dead reckoning
// has been accumulating. belief_radius was computed, grown and capped and then read
// only as a boolean gate, so a player who broke line of sight was pre-aimed and pre-
// fired at with zero positional error.
static v3 bot_belief_aimpoint(const bot_t *b) {
  return (v3){{b->belief_pos.x + sinf(b->belief_err_a) * b->belief_radius,
               b->belief_pos.y + EYE_STAND - ENT_CHEST_OFF,
               b->belief_pos.z + cosf(b->belief_err_a) * b->belief_radius}};
}

static v3 bot_search_clamped(v3 p) {
  p.y = 0.0f;
  p.x = f_clamp(p.x, -ARENA_HALF + PLAYER_RADIUS, ARENA_HALF - PLAYER_RADIUS);
  p.z = f_clamp(p.z, -ARENA_HALF + PLAYER_RADIUS, ARENA_HALF - PLAYER_RADIUS);
  return p;
}

static void bot_search_candidates(const bot_t *b, v3 cand[6]) {
  v3 fwd = {{b->last_seen_vel.x, 0.0f, b->last_seen_vel.z}};
  float sp2 = fwd.x * fwd.x + fwd.z * fwd.z;
  if (sp2 > 0.25f * 0.25f) {
    fwd = v3_scale(fwd, 1.0f / sqrtf(sp2));
  } else {
    fwd = (v3){{b->belief_pos.x - b->pos.x, 0.0f,
                b->belief_pos.z - b->pos.z}};
    float len2 = fwd.x * fwd.x + fwd.z * fwd.z;
    fwd = len2 > 1e-6f ? v3_scale(fwd, 1.0f / sqrtf(len2))
                       : (v3){{sinf(b->yaw), 0.0f, -cosf(b->yaw)}};
  }
  v3 left_vec = {{-fwd.z, 0.0f, fwd.x}};
  v3 right_vec = {{fwd.z, 0.0f, -fwd.x}};
  v3 first_side = b->search_dir >= 0 ? left_vec : right_vec;
  v3 second_side = b->search_dir >= 0 ? right_vec : left_vec;
  const float side_step = 4.0f;
  const float close_step = 2.8f;
  const float long_step = 5.6f;
  cand[0] = b->belief_pos;
  cand[1] = b->last_seen;
  cand[2] = v3_add(b->last_seen, v3_scale(first_side, side_step));
  cand[3] = v3_add(b->last_seen, v3_scale(second_side, side_step));
  cand[4] = v3_add(b->last_seen, v3_scale(fwd, close_step));
  cand[5] = v3_add(b->last_seen, v3_scale(fwd, long_step));
  for (int i = 0; i < 6; i++) cand[i] = bot_search_clamped(cand[i]);
}

static unsigned bot_search_valid_mask(const v3 cand[6], const int fits[6]) {
  unsigned mask = 0;
  for (int i = 0; i < 6; i++) {
    if (!fits[i]) continue;
    int duplicate = 0;
    for (int j = 0; j < i; j++) {
      if (!(mask & (1u << j))) continue;
      v3 d = v3_sub(cand[i], cand[j]);
      if (v3_dot(d, d) <= BOT_SEARCH_EPS * BOT_SEARCH_EPS) {
        duplicate = 1;
        break;
      }
    }
    if (!duplicate) mask |= 1u << i;
  }
  return mask;
}

// No candidate survived the filter: search the believed position itself. Both
// dead ends in bot_search_plan_next take exactly this exit.
static void bot_search_fallback(bot_t *b, const bot_skill_t *sk) {
  b->search_pos = bot_search_clamped(b->belief_pos);
  b->search_t = sk->search_ticks;
  b->search_index = 0;
  b->search_visited = 0;
}

static void bot_search_plan_next(bot_t *b, const bot_skill_t *sk) {
  v3 cand[6];
  bot_search_candidates(b, cand);
  int fits[6];
  for (int i = 0; i < 6; i++)
    fits[i] = aabb_fits(cand[i], PLAYER_RADIUS, HEIGHT_STAND);
  unsigned valid = bot_search_valid_mask(cand, fits);
  if (!valid) {
    bot_search_fallback(b, sk);
    return;
  }
  if (!(valid & ~b->search_visited)) {
    // The finite set is exhausted. Start the next sweep with the side order reversed;
    // within a sweep search_dir stays fixed so mask bits keep their physical meaning.
    b->search_visited = 0;
    b->search_dir = -b->search_dir;
    v3 tmp = cand[2];
    cand[2] = cand[3];
    cand[3] = tmp;
    int tmp_fit = fits[2];
    fits[2] = fits[3];
    fits[3] = tmp_fit;
    valid = bot_search_valid_mask(cand, fits);
  }
  v3 threat_point = bot_belief_aimpoint(b);
  v3 prev_dir = v3_sub(b->search_pos, b->pos);
  prev_dir.y = 0.0f;
  float prev_len2 = prev_dir.x * prev_dir.x + prev_dir.z * prev_dir.z;
  int have_prev = prev_len2 > 0.25f;
  if (have_prev) prev_dir = v3_scale(prev_dir, 1.0f / sqrtf(prev_len2));

  int best_i = -1, los_calls = 0;
  float best_score = -1.0e30f;
  for (int i = 0; i < 6; i++) {
    if (!(valid & (1u << i)) || (b->search_visited & (1u << i))) continue;
    v3 anchor_eye = {{cand[i].x, cand[i].y + EYE_STAND, cand[i].z}};
    v3 to_threat = v3_sub(threat_point, anchor_eye);
    float threat_d2 = v3_dot(to_threat, to_threat);
    int opens_los = 0, has_cover = 0;
    if (threat_d2 > 0.25f && los_calls < 16) {
      opens_los = los_clear(anchor_eye, threat_point);
      los_calls++;
      has_cover = !opens_los;
    }
    v3 to_anchor = v3_sub(cand[i], b->pos);
    float dist2 = to_anchor.x * to_anchor.x + to_anchor.z * to_anchor.z;
    float novelty = 0.0f;
    // dist2 IS this candidate's 2D length; it used to be computed a second
    // time into `anchor_len2` from the identical expression.
    if (have_prev && dist2 > 1e-6f) {
      v3 nd = v3_scale((v3){{to_anchor.x, 0.0f, to_anchor.z}},
                       1.0f / sqrtf(dist2));
      novelty = (1.0f - f_clamp(v3_dot(prev_dir, nd), -1.0f, 1.0f)) * 40.0f;
    }
    float score = (opens_los ? 1000.0f : 0.0f) + (has_cover ? 80.0f : 0.0f) +
                  novelty - dist2 * 0.02f;
    if (best_i < 0 || score > best_score + BOT_SEARCH_EPS ||
        (fabsf(score - best_score) <= BOT_SEARCH_EPS && i < best_i)) {
      best_score = score;
      best_i = i;
    }
  }

  if (best_i < 0) {
    bot_search_fallback(b, sk);
    return;
  }
  b->search_pos = cand[best_i];
  b->search_t = sk->search_ticks;
  b->search_visited |= 1u << best_i;
  b->search_index = (best_i + 1) % 6;
}

static void bot_belief_clear(bot_t *b) {
  // A finished fight leaves something behind.
  if (b->last_seen_tick) {
    b->alert_p = b->last_seen;
    b->alert_t = BOT_ALERT_TICKS;
  }
  b->belief = BOT_BELIEF_VISIBLE;
  b->last_seen_vel = (v3){{0, 0, 0}};
  b->belief_pos = (v3){{0, 0, 0}};
  b->belief_radius = 0.0f;
  b->last_seen_tick = 0;
  bot_focus_invalidate(b);
  b->search_dir = 1;
  b->reacquire_count = 0;
  b->threat_src = -1;
  b->threat_t = 0;
  b->threat_score = 0;
}

static void bot_belief_observe(bot_t *b, int e) {
  v3 pos = ent_pos(e);
  v3 vel = {{0, 0, 0}};
  if (b->belief == BOT_BELIEF_VISIBLE && b->last_seen_tick > 0 &&
      g_tick > b->last_seen_tick) {
    float inv_dt = 1.0f / ((float)(g_tick - b->last_seen_tick) * TICK_DT);
    vel.x = (pos.x - b->last_seen.x) * inv_dt;
    vel.z = (pos.z - b->last_seen.z) * inv_dt;
    float speed = sqrtf(vel.x * vel.x + vel.z * vel.z);
    float max_speed = g_sim_mv[MV_RUN];
    if (speed > max_speed && speed > 1e-6f) {
      float s = max_speed / speed;
      vel.x *= s;
      vel.z *= s;
    }
  }
  // The error angle is rolled inside the seed, on the LAST tick this bot can
  // still see, and then frozen for the whole memory window.
  b->belief = BOT_BELIEF_VISIBLE;
  bot_belief_seed(b, pos, vel);
  b->search_t = 0;
}

static void bot_belief_predict(bot_t *b) {
  float speed = sqrtf(b->last_seen_vel.x * b->last_seen_vel.x +
                      b->last_seen_vel.z * b->last_seen_vel.z);
  b->belief_pos.x += b->last_seen_vel.x * TICK_DT;
  b->belief_pos.z += b->last_seen_vel.z * TICK_DT;
  b->belief_radius += 0.015f + speed * TICK_DT * 0.20f;
  if (b->belief_radius > 4.5f) b->belief_radius = 4.5f;
}

typedef struct {
  int ent;
  float score, dist;
} bot_target_choice_t;

static float bot_target_score(const bot_t *b, int e, float dist, float align) {
  float fov01 = f_clamp((align - BOT_FOV_COS) / (1.0f - BOT_FOV_COS),
                        0.0f, 1.0f);
  float score = 1000.0f / (1.0f + dist) + 350.0f * fov01;
  if (b->threat_t > 0 && b->threat_src == e)
    score += 650.0f + 70.0f * (float)b->threat_score;
  if (b->tgt == e) score += BOT_TARGET_HOLD_BONUS;
  return score;
}

static int bot_target_better(bot_target_choice_t a, bot_target_choice_t b) {
  if (a.ent < 0) return 0;
  if (b.ent < 0) return 1;
  if (a.score > b.score + BOT_TARGET_EPS) return 1;
  if (b.score > a.score + BOT_TARGET_EPS) return 0;
  if (a.dist < b.dist - BOT_TARGET_EPS) return 1;
  if (b.dist < a.dist - BOT_TARGET_EPS) return 0;
  return a.ent < b.ent;
}

static int bot_target_breaks_hidden(const bot_t *b, bot_target_choice_t c) {
  if (c.ent < 0) return 0;
  if (b->threat_t > 0 && b->threat_src == c.ent) return 1;
  return c.score > BOT_TARGET_HOLD_BONUS + BOT_TARGET_HIDDEN_BREAK_MARGIN;
}

[[maybe_unused]] static const char *bot_belief_label(const bot_t *b) {
  if (!b->alive || b->tgt < 0) return "patrol";
  switch (b->belief) {
    case BOT_BELIEF_VISIBLE: return "visible";
    case BOT_BELIEF_OCCLUDED_HOLD: return "hold";
    case BOT_BELIEF_INVESTIGATE: return "investigate";
    case BOT_BELIEF_SEARCH_SWEEP: return "search";
  }
  return "patrol";
}

