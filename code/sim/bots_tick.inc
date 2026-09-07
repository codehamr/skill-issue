// =============================================================================
// sim/bots_tick.inc  —  bot_tick + the match_* family.
// Proof: tacstat, parity. One TU: included once by game.c, in definition
// order — moving a definition out is a semantic change.
// Contract: BOT_SKILL[sp_difficulty()].rec_comp is the only way difficulty touches
// gun handling — no weapon numbers cross skill otherwise. The stance
// (crouch) has one owner, the hold-low block gated on !sliding, and eight
// interrupters outside it; a slide IS a crouch for the bot as for the player.
// =============================================================================
// Tactical decisions use sight, remembered bearings and local impacts. A plan
// commits for a short encounter, then yields: holding cover cannot become camping.
static int bot_tactical_plan(bot_t *b, v3 threat, float distance) {
  int pressure = b->hurt_t > 0 || b->suppress.intensity >= BOT_SUPPRESS_THRESHOLD;
  int retreat = b->wp.reload_t || b->hp <= 40;
  bot_tactic_t prior = b->tactic;
  b->tactic = retreat ? BOT_RETREAT : pressure && (b->hp < 70 || b->courage < 0.55f)
                                    ? BOT_FLANK : BOT_ADVANCE;
  // There is no passive healing. A wounded bot changes angle after one retreat,
  // rather than repeatedly selecting the same safe patch until somebody finds it.
  if (retreat && prior == BOT_RETREAT && !b->wp.reload_t) b->tactic = BOT_FLANK;
  b->tactical_threat = threat;
  float best = -1e9f;
  int found = 0;
  v3 candidates[48];
  int count = 0;
  for (int i = 0; i < 16; i++) {
    float angle = (float)(i & 7) * F_PI_4;
    float radius = i < 8 ? 3.0f : 6.0f;
    candidates[count++] = v3_add(b->pos, (v3){{sinf(angle) * radius, 0, cosf(angle) * radius}});
  }
  // Four nearest obstacles, with both corners and face centres. Unlike rings
  // alone these find narrow head-cover and the actual shoulders of a corner.
  int selected[4] = {-1, -1, -1, -1};
  for (int rank = 0; rank < 4; rank++) {
    int pick = -1; float nearest = 100.0f;
    for (int i = 0; i < g_num_solids; i++) {
      const solid_t *solid = &g_solids[i];
      if (solid->max.y < b->pos.y + 0.7f || solid->min.y > b->pos.y + EYE_STAND) continue;
      int used = 0;
      for (int j = 0; j < rank; j++) used |= selected[j] == i;
      if (used) continue;
      float x = fmaxf(fmaxf(solid->min.x - b->pos.x, b->pos.x - solid->max.x), 0);
      float z = fmaxf(fmaxf(solid->min.z - b->pos.z, b->pos.z - solid->max.z), 0);
      if (x * x + z * z < nearest) { nearest = x * x + z * z; pick = i; }
    }
    if (pick < 0) break;
    selected[rank] = pick;
    const solid_t *solid = &g_solids[pick];
    float margin = PLAYER_RADIUS + 0.45f;
    float xs[3] = {solid->min.x - margin, (solid->min.x + solid->max.x) * 0.5f, solid->max.x + margin};
    float zs[3] = {solid->min.z - margin, (solid->min.z + solid->max.z) * 0.5f, solid->max.z + margin};
    for (int z = 0; z < 3; z++)
      for (int x = 0; x < 3; x++)
        if (x != 1 || z != 1) candidates[count++] = (v3){{xs[x], b->pos.y, zs[z]}};
  }
  v3 toward = v3_sub(threat, b->pos); toward.y = 0;
  float range = fmaxf(v3_len(toward), 0.01f);
  toward = v3_scale(toward, 1.0f / range);
  for (int i = 0; i < count; i++) {
    v3 point = candidates[i];
    if (fabsf(point.x) > ARENA_HALF - 1 || fabsf(point.z) > ARENA_HALF - 1) continue;
    v3 travel = v3_sub(point, b->pos);
    float length = v3_len(travel);
    if (length < 1.2f || length > 9.0f || !bot_walk_clear(b->pos, point)) continue;
    v3 chest = v3_add(point, (v3){{0, 0.95f, 0}});
    v3 eye = v3_add(point, (v3){{0, EYE_STAND, 0}});
    int protected = !los_clear(chest, threat);
    int exposed_eye = los_clear(eye, threat);
    if (!protected) continue;
    float new_distance = v3_len(v3_sub(eye, threat));
    float lateral = fabsf(travel.x * toward.z - travel.z * toward.x);
    float score = (protected ? 10.0f : -4.0f) - length * 0.65f;
    if (protected && exposed_eye) score += b->tactic == BOT_RETREAT ? -3.0f : 7.0f;
    if (b->tactic == BOT_RETREAT) score += (new_distance - distance) * 1.8f;
    else if (b->tactic == BOT_FLANK) score += lateral * 1.6f;
    else score += (distance - new_distance) * (distance > 12.0f ? 1.6f : -0.6f);
    v3 middle = v3_add(v3_lerp(b->pos, point, 0.5f), (v3){{0, 0.95f, 0}});
    if (los_clear(middle, threat)) score -= pressure || retreat ? 4.0f : 2.0f;
    if (score <= best) continue;
    best = score; found = 1;
    b->tactical_goal = point;
    b->tactical_low = protected && exposed_eye;
  }
  b->tactical_cd = 72 + (int)(b->courage * 36);
  if (found) {
    b->tactical_t = 210 + (int)(b->courage * 90);
    b->tactical_hold = b->lean_t = 0;
    b->nav_count = b->nav_at = 0; b->nav_retry = 0;
  }
  return found;
}
static void bot_tactical_lean(bot_t *b, v3 threat) {
  if (--b->lean_t > 0) { b->lean_want = b->lean_side; return; }
  b->lean_t = 12;
  b->lean_want = b->lean_side = 0;
  v3 eye = {{b->pos.x, b->pos.y + bot_eye(b), b->pos.z}};
  v3 chest = v3_add(b->pos, (v3){{0, 0.95f, 0}});
  int covered = !los_clear(chest, threat);
  int visible = los_clear(eye, threat);
  if (!covered && visible) return;
  for (int side = -1; side <= 1; side += 2) {
    v3 leaned = v3_add(eye, lean_offset(b->yaw, (float)side));
    if (!los_clear(eye, leaned) || !los_clear(leaned, threat)) continue;
    if (gun_wall_blocked(leaned, b->yaw, b->pitch, &b->wp)) continue;
    b->lean_want = b->lean_side = (float)side;
    break;
  }
}
static void bot_tactical_steer(bot_t *b, int engaging, float distance,
                                 float *wx, float *wz, float *pace) {
  if (b->tactical_cd > 0) b->tactical_cd--;
  if (b->tactical_t > 0) b->tactical_t--;
  b->lean_want = 0;
  // A sound never grants visual knowledge or a trigger. Its immediate escape is
  // owned by suppression; target tactics require an actual recent sighting.
  if (b->tgt < 0 || (!engaging && b->lose_t > 240)) {
    b->tactical_t = 0; return;
  }
  v3 threat = engaging ? ent_aimpoint(b->tgt) : bot_belief_aimpoint(b);
  if (!engaging) distance = v3_len(v3_sub(threat, bot_eye_pos(b)));
  if (!b->tactical_t && !b->tactical_cd && b->grounded && !b->sliding &&
      (engaging || b->belief != BOT_BELIEF_OCCLUDED_HOLD))
    bot_tactical_plan(b, threat, distance);
  if (!b->tactical_t) return;
  float length = v3_len(v3_sub(b->tactical_goal, b->pos));
  if (length > 0.5f && !b->tactical_hold) {
    bot_nav_steer(b, b->tactical_goal, wx, wz);
    *pace = 1.0f; b->crouch = 0;
    b->in_cover = b->tactic == BOT_RETREAT;
    return;
  }
  *wx = *wz = 0;
  b->tactical_hold++;
  // Short, varied shoulder peeks. Low cover alternates crouched concealment and
  // a standing shot with only the upper body showing; both hitboxes remain real.
  int cycle = 42 + (int)(b->courage * 24);
  int hidden = b->tactical_hold % cycle < cycle / 3;
  b->crouch = b->tactical_low && hidden;
  if (!hidden && !b->wp.reload_t) {
    bot_tactical_lean(b, threat);

  }
  if (b->tactical_hold > 96 + (int)(b->courage * 48) || b->pin > 24) {
    b->tactical_t = 0; b->tactical_cd = 24;
    b->tactical_hold = 0;
  }
}

// The ENGAGING dispatch arm shares tactical state with bot_tick through its
// explicit steering outputs. Suppression reactions keep the shared weapon and
// locomotion rules unchanged.
static void bot_arm_engage(bot_t *b, const bot_skill_t *sk, float tdist, v3 sight_point,
                            float *want_yaw_p, float *want_pitch_p,
                            float *wx_p, float *wz_p, float *pace_p,
                            int *want_slide_p, int *want_jump_p) {
  float want_yaw = *want_yaw_p, want_pitch = *want_pitch_p;
  float wx = *wx_p, wz = *wz_p, pace = *pace_p;
  int want_slide = *want_slide_p, want_jump = *want_jump_p;
  {
    // The error magnitude settles while tracking; its direction drifts a few
    // times a second and the offset eases toward the goal — imperfect like a
    // human, never jittery like a broken servo.
    b->err += (sk->err1 - b->err) * mv_alpha(sk->settle_ms);
    if (--b->drift_t <= 0) {
      b->drift_t = (int)rng_rangef(&g_rng_bot, 24, 42);
      b->goal_y = rng_rangef(&g_rng_bot, -b->err, b->err);
      b->goal_p = rng_rangef(&g_rng_bot, -b->err, b->err) * 0.6f;
    }
    float ek = ease_k(5.0f);
    b->off_y += (b->goal_y - b->off_y) * ek;
    b->off_p += (b->goal_p - b->off_p) * ek;

    v3 o = bot_eye_pos(b);
    v3 tp = sight_point;
    // Aim at where the target IS. These weapons are hitscan, so there is nothing to
    // lead: a forward offset is a systematic miss, and once bots moved at the player's
    // speed instead of a third of it, the 0.13 s "human anticipation" lead threw the
    // crosshair 1.5 m off and cost the hard tier a third of its hit rate (37.1% ->
    // 24.6% measured).
    float dx = tp.x - o.x, dy = tp.y - o.y, dz = tp.z - o.z;
    float hl = sqrtf(dx * dx + dz * dz);
    // Error grows with range: the same angular sloppiness costs more metres far
    // away, and a bot that is pinpoint at 30 m does not read as a person.
    float rng_k = 1.0f + f_clamp(tdist * 0.020f, 0.0f, 0.55f);
    // Fight the recoil, as far as this skill can.
    float cy = b->wp.rec_y * sk->rec_comp * DEG2RAD;
    float cp = b->wp.rec_p * sk->rec_comp * DEG2RAD;
    want_yaw = atan2f(dx, -dz) + (b->off_y * rng_k + b->fl_y) * DEG2RAD - cy;
    want_pitch = f_clamp(atan2f(dy, hl) + (b->off_p * rng_k + b->fl_p) * DEG2RAD - cp,
                         -1.2f, 1.2f);

    // Combat footwork: strafe with occasional direction changes (sometimes
    // planting), drift toward/away to hold a 7..16 m engagement range.
    if (--b->strafe_t <= 0) {
      int pressure = b->hurt_t > 0 || b->suppress.intensity >= BOT_SUPPRESS_THRESHOLD;
      float quick = pressure ? sk->tac : sk->tac * 0.5f;
      b->strafe_t = (int)rng_rangef(&g_rng_bot, 65 - 43 * quick, 150 - 100 * quick);
      uint32_t rr = rng_next(&g_rng_bot) % 10;
      b->strafe_dir = pressure && b->strafe_dir ? -b->strafe_dir : rr < 4 ? 1 : rr < 8 ? -1 : 0;
    }
    // TAKE COVER when there is a reason to be out of the line: an empty mag, or hurt
    // and low on health.
    int deadlocked = b->tactical_t > 0 || bot_deadlock_steer(b, tdist, &wx, &wz, &pace);
    if (deadlocked) {
      b->crouch = 0;
      b->in_cover = 0;   // the cover leg did not run this tick; do not go stale
    }
    if (!deadlocked) bot_cover_steer(b, NULL, &wx, &wz, &pace);
    // Stand up while walking to cover — kept at the CALL SITE because the belief
    // branch's copy of this leg never did it.
    if (b->in_cover) b->crouch = 0;
    // FLANK, after cover and before range-keeping: needing to be out of the line beats
    // wanting a better angle, and a bot on the swing is not also holding a range band.
    int flanking = !deadlocked && !b->in_cover &&
                   bot_flank_steer(b, sk, tdist, &wx, &wz, &pace);
    if (flanking) b->crouch = 0;
    if (!deadlocked && !b->in_cover && !flanking && hl > 0.01f) {
      float tx = dx / hl, tz = dz / hl;
      // Range keeping needs a DEAD BAND now that bots move at the player's 12 m/s: the
      // whole 8..16 m window is crossed in well under a second, so a hard in/out flip
      // at the edges makes a bot jitter on the spot.
      float adv = 0.0f;
      if (tdist > 16.0f) adv = f_clamp((tdist - 16.0f) / 7.0f, 0.0f, 1.0f) * 0.9f;
      else if (tdist < 8.0f) adv = -f_clamp((8.0f - tdist) / 4.0f, 0.0f, 1.0f) * 0.9f;
      wx = -tz * (float)b->strafe_dir * 0.9f + tx * adv;
      wz =  tx * (float)b->strafe_dir * 0.9f + tz * adv;
      // Tactical slide: close the gap fast, or break off sideways when being shot at.
      // Gated by a cooldown and by skill so EASY bots rarely do it.
      if (!b->sliding && b->grounded && b->slide_cd == 0 &&
          rng_rangef(&g_rng_bot, 0.0f, 1.0f) < sk->tac * 0.008f) {
        int dodge = b->hurt_t > 0 && tdist < 13.0f;
        if (dodge || tdist > 14.0f) {
          // Steer into the slide and ask for it; the shared entry gate decides
          // whether the bot is actually moving fast enough to have one.
          wx = dodge ? -tz * (float)(b->strafe_dir ? b->strafe_dir : 1) : tx;
          wz = dodge ?  tx * (float)(b->strafe_dir ? b->strafe_dir : 1) : tz;
          want_slide = 1;
        }
      }
    }
    // Hold low at range: crouching steadies the aim and genuinely shrinks the hitbox
    // (bot_trace_one follows the stance), so it is a real trade and not a pose.
    if (!b->sliding) {
      int want_cr = tdist > 13.0f && b->strafe_dir == 0 && b->hurt_t == 0 &&
                    b->wp.reload_t == 0 && b->flank_t == 0 && b->dl_t == 0;
      if (b->crouch_t < 0) b->crouch_t++;  // standing again, briefly
      // THE TIMER'S ONLY DRAIN IS HERE, and `>= 0` rather than `== 0` is what makes
      // that true. Seven branches outside this block (cover, flank, deadlock, dodge
      // jump, startle, belief, alert) stand the bot by writing crouch = 0 and leave
      // the positive hold timer behind; with `== 0` the bot then never crouched again
      // until it died or fell back to patrol — 9-49 % of live bot-ticks. An
      // interrupted hold owes no cooldown.
      if (b->crouch) {
        // A hold is a BURST, not a posture.
        if (--b->crouch_t <= 0 || !want_cr) {
          b->crouch = 0;
          b->crouch_t = -(int)rng_rangef(&g_rng_bot, 240.0f, 600.0f);
        }
      } else if (want_cr && b->crouch_t >= 0 &&
                 rng_rangef(&g_rng_bot, 0.0f, 1.0f) < 0.06f) {
        b->crouch = 1;
        b->crouch_t = (int)rng_rangef(&g_rng_bot, 130.0f, 320.0f);
      }
    }

    // Dodge jump: breaks a tracking crosshair and is the other half of "pops out
    // unexpectedly". Only while being shot at, and rate-limited hard.
    if (!b->sliding && b->grounded && b->jump_cd == 0 && b->hurt_t > 0 &&
        tdist > 5.0f && tdist < 20.0f &&
        rng_rangef(&g_rng_bot, 0.0f, 1.0f) < sk->tac * 0.008f) {
      want_jump = 1;
      b->jump_cd = (int)rng_rangef(&g_rng_bot, 300, 660);
      b->crouch = 0;
    }
    pace = 1.0f;
  }
  *want_yaw_p = want_yaw; *want_pitch_p = want_pitch;
  *wx_p = wx; *wz_p = wz; *pace_p = pace;
  *want_slide_p = want_slide; *want_jump_p = want_jump;
}

// bot_tick's STARTLED dispatch arm — extracted verbatim (Task 16).
static void bot_arm_startled(bot_t *b, float *want_yaw_p, float *want_pitch_p,
                              float *wx_p, float *wz_p, float *pace_p) {
  float want_yaw = *want_yaw_p, want_pitch = *want_pitch_p;
  float wx = *wx_p, wz = *wz_p, pace = *pace_p;
  {
    // An audible position is not a target: no aimpoint, no target assignment, no combat
    // trigger.
    float dx = b->hear_pos.x - b->pos.x, dz = b->hear_pos.z - b->pos.z;
    float hl = sqrtf(dx * dx + dz * dz);
    // The STORE flattens y (a bearing along the floor is what is heard), so the
    // consumer has to put a person back on it — the same chest height
    // bot_belief_aimpoint uses.
    float dy = (b->hear_pos.y + EYE_STAND - ENT_CHEST_OFF) -
               (b->pos.y + bot_eye(b));
    if (hl > 0.01f) {
      want_yaw = atan2f(dx, -dz);
      want_pitch = f_clamp(atan2f(dy, hl), -1.2f, 1.2f);
      if (hl > 1.8f && fabsf(wrap_pi(want_yaw - b->yaw)) <= 1.0471976f) {
        bot_nav_steer(b, b->hear_pos, &wx, &wz);
        pace = BOT_PATROL_PACE;
      }
    }
    b->crouch = 0;
  }
  *want_yaw_p = want_yaw; *want_pitch_p = want_pitch;
  *wx_p = wx; *wz_p = wz; *pace_p = pace;
}

// bot_tick's BELIEF dispatch arm (b->tgt >= 0, not engaging, not startled) —
// extracted verbatim (Task 16).
static void bot_arm_belief(bot_t *b, const bot_skill_t *sk,
                            float *want_yaw_p, float *want_pitch_p,
                            float *wx_p, float *wz_p, float *pace_p,
                            int *want_slide_p, int *want_jump_p) {
  float want_yaw = *want_yaw_p, want_pitch = *want_pitch_p;
  float wx = *wx_p, wz = *wz_p, pace = *pace_p;
  int want_slide = *want_slide_p, want_jump = *want_jump_p;
  {
    // Lost sight: aim and movement use only the stored belief/search memory.
    v3 tp = bot_belief_aimpoint(b);
    v3 eo = bot_eye_pos(b);
    float adx = tp.x - b->pos.x, adz = tp.z - b->pos.z;
    float ahl = sqrtf(adx * adx + adz * adz);
    if (ahl > 0.01f) {
      float aimy = tp.y - eo.y;
      want_yaw = atan2f(adx, -adz) + b->fl_y * DEG2RAD;
      want_pitch = f_clamp(atan2f(aimy, ahl), -1.2f, 1.2f);
    }
    b->crouch = 0;

    // PRE-FIRE: a human who is CONFIDENT the corner is about to break holds the trigger
    // through it — rounds land on the angle as it opens, before visual confirmation.
    if ((b->belief == BOT_BELIEF_OCCLUDED_HOLD ||
         b->belief == BOT_BELIEF_INVESTIGATE) &&
        b->belief_radius < 1.3f && ahl > 2.0f && ahl < 20.0f &&
        (int)(g_tick - b->last_seen_tick) < 84) {
      if (b->prefire_t > 0) b->prefiring = 1;
      else if (b->prefire_cd == 0 &&
               rng_rangef(&g_rng_bot, 0.0f, 1.0f) < sk->tac * 0.010f) {
        b->prefire_t = (int)rng_rangef(&g_rng_bot, 24.0f, 54.0f);
        b->prefire_cd = (int)rng_rangef(&g_rng_bot, 300.0f, 700.0f);
        b->prefiring = 1;
      }
    }

    // Same cover leg, probed against the BELIEVED position instead of a seen one.
    if (b->tactical_t > 0) {
      // The committed tactical plan below owns translation.
    } else if (bot_cover_steer(b, &tp, &wx, &wz, &pace)) {
      // steering (or arrival) fully handled by the shared leg
    } else if (b->belief == BOT_BELIEF_OCCLUDED_HOLD &&
               bot_flank_walk(b, &wx, &wz, &pace)) {
      // FINISH THE SWING. Only while the bot still believes in the target it
      // planned around (OCCLUDED_HOLD, not INVESTIGATE/SEARCH — once it has
      // started sweeping for a lost man the old anchor means nothing), and it
      // is the one thing worth doing with a hold, which otherwise stands still.
      b->crouch = 0;
    } else if (b->belief == BOT_BELIEF_OCCLUDED_HOLD) {
      wx = wz = 0.0f;
    } else {
      float sdx = b->search_pos.x - b->pos.x;
      float sdz = b->search_pos.z - b->pos.z;
      float sl = sqrtf(sdx * sdx + sdz * sdz);
      if (sl > 0.01f) want_yaw = atan2f(sdx, -sdz);
      if (b->belief == BOT_BELIEF_INVESTIGATE) {
        if (b->search_t > 0) b->search_t--;
        if (sl < 0.9f || b->search_t <= 0) {
          b->belief = BOT_BELIEF_SEARCH_SWEEP;
          bot_search_plan_next(b, sk);
        } else {
          bot_nav_steer(b, b->search_pos, &wx, &wz);
          // A human RUNS the approach and only slows for the last angle — the constant
          // 0.75 shuffle read as a bot creeping everywhere.
          pace = sl > 8.0f ? 1.0f : 0.75f;
          float spd2 = sqrtf(b->vel.x * b->vel.x + b->vel.z * b->vel.z);
          // SLIDE-PEEK / traversal slide: break into the believed angle low and fast,
          // or eat distance on a long approach.
          if (!b->sliding && b->grounded && b->slide_cd == 0 && sl > 3.0f &&
              spd2 > g_sim_mv[MV_RUN] * SLIDE_START_SPEED &&
              rng_rangef(&g_rng_bot, 0.0f, 1.0f) < sk->tac * 0.008f) {
            want_slide = 1;
            b->peek = sl < 12.0f;
          }
          // CORNER-JUMP: closing on a tight belief, occasionally break the sightline
          // vertically as the angle opens — the human peek-jump.
          if (!b->sliding && b->grounded && b->jump_cd == 0 && sl > 2.5f &&
              sl < 13.0f && b->belief_radius < 3.2f &&
              rng_rangef(&g_rng_bot, 0.0f, 1.0f) < sk->tac * 0.022f) {
            want_jump = 1;
            b->peek = 1;
            b->jump_cd = (int)rng_rangef(&g_rng_bot, 360.0f, 780.0f);
          }
        }
      } else if (b->belief == BOT_BELIEF_SEARCH_SWEEP) {
        if (sl < 0.9f) {
          wx = wz = 0.0f;
          if (b->search_t > 0) b->search_t--;
          if (b->search_t <= 0) bot_search_plan_next(b, sk);
        } else {
          bot_nav_steer(b, b->search_pos, &wx, &wz);
          pace = 0.75f;
        }
      }
    }
  }
  *want_yaw_p = want_yaw; *want_pitch_p = want_pitch;
  *wx_p = wx; *wz_p = wz; *pace_p = pace;
  *want_slide_p = want_slide; *want_jump_p = want_jump;
}

// bot_tick's ALERT dispatch arm (no target, not startled, clearing a noise
// spot) — extracted verbatim (Task 16).
static void bot_arm_alert(bot_t *b, float *want_yaw_p, float *wx_p,
                           float *wz_p, float *pace_p) {
  float want_yaw = *want_yaw_p, wx = *wx_p, wz = *wz_p, pace = *pace_p;
  {
    // CLEARING THE SPOT. Not a target and not a noise — a place the bot has unfinished
    // business with.
    b->crouch = 0;
    float adx = b->alert_p.x - b->pos.x, adz = b->alert_p.z - b->pos.z;
    float al = sqrtf(adx * adx + adz * adz);
    if (al < BOT_ALERT_ARRIVE) {
      b->alert_t = 0;                    // checked it; stand down
    } else {
      want_yaw = atan2f(adx, -adz);
      bot_nav_steer(b, b->alert_p, &wx, &wz);
      pace = BOT_ALERT_PACE;
    }
  }
  *want_yaw_p = want_yaw; *wx_p = wx; *wz_p = wz; *pace_p = pace;
}

// bot_tick's PATROL dispatch arm (no target, not startled, no alert spot) —
// extracted verbatim (Task 16). `pace` is not read or written here in the
// original body (patrol runs at the default BOT_PATROL_PACE already set
// before the dispatch), so it is not threaded through.
static void bot_arm_patrol(bot_t *b, float *want_yaw_p, float *wx_p,
                            float *wz_p) {
  float want_yaw = *want_yaw_p, wx = *wx_p, wz = *wz_p;
  {
    // Patrol: wander between random reachable waypoints.
    b->crouch = 0;
    b->crouch_t = 0;
    float dx = b->target.x - b->pos.x, dz = b->target.z - b->pos.z;
    float dist = sqrtf(dx * dx + dz * dz);
    if (dist < 0.8f || b->stuck > 60) {
      for (int i = 0; i < 16; i++) {
        v3 c = {{rng_rangef(&g_rng_bot, -ARENA_HALF + 2, ARENA_HALF - 2), 0,
                 rng_rangef(&g_rng_bot, -ARENA_HALF + 2, ARENA_HALF - 2)}};
        float tx = c.x - b->pos.x, tz = c.z - b->pos.z;
        if (tx * tx + tz * tz < 64.0f) continue;  // wander legs are >=8 m
        if (!aabb_fits(c, PLAYER_RADIUS, HEIGHT_STAND)) continue;
        b->target = c;
        break;
      }
      b->stuck = 0;
      dx = b->target.x - b->pos.x;
      dz = b->target.z - b->pos.z;
    }
    bot_nav_steer(b, b->target, &wx, &wz);
    if (wx * wx + wz * wz > 0.01f) want_yaw = atan2f(wx, -wz);
    if (fabsf(wrap_pi(want_yaw - b->yaw)) > 1.2f) wx = wz = 0;

  }
  *want_yaw_p = want_yaw; *wx_p = wx; *wz_p = wz;
}

static void bot_tick(int bi) {
  bot_t *b = &g_bots[bi];
  const bot_skill_t *sk = &BOT_SKILL[sp_difficulty()];
  b->prev_pos = b->pos;
  b->prev_yaw = b->yaw;
  b->prev_topple = b->topple;
  b->startled = 0;   // census flag: honest on the dead/puppet/frozen returns too
  bot_suppress_tick(b, bi);
  if (b->flash > 0) b->flash--;

  if (!b->alive) {
    rag_advance(&b->rag, &b->pos, b->prev_pos, &b->topple, &b->anim);
    b->grounded = b->rag.grounded;
    if (!g_match.over && --b->respawn_t <= 0) {
      // The BODY stays behind.
      corpse_add(&b->anim, b->pos, b->prev_pos, b->rag, b->yaw,
                 b->topple, b->prev_topple,
                 gun_anim(&b->wp), b->wp.cur, bi + 1);
      bot_spawn(bi);
      ev_push((event_t){.type = EV_RESPAWN, .dst = bi + 1});
    }
    return;
  }
  if (b->puppet) {
    // ADS is driven HERE and not down at the AI's own gun_ads: the puppet tick returns
    // on the next line, so an override placed there is unreachable.
    gun_ads(&b->wp, b->pup_ads);
    bot_puppet_tick(b, bi);
    return;
  }
  // The BOOT SCREEN used to freeze them too, on the reasoning that a ticking world with
  // live bots is a main menu in which the player is hunted down and killed while
  // reading the tiles.
  if (g_bots_frozen || g_match.over || home_calm()) return;

  gun_cool(&b->wp);
  gun_reload_tick(&b->wp);
  if (b->pause_t > 0) b->pause_t--;
  if (b->react_t > 0) b->react_t--;
  if (b->hurt_t > 0) b->hurt_t--;
  if (b->threat_t > 0 && --b->threat_t == 0) {
    b->threat_src = -1;
    b->threat_score = 0;
  }
  if (b->nav_retry > 0) b->nav_retry--;
  if (b->slide_cd > 0) b->slide_cd--;
  if (b->jump_cd > 0) b->jump_cd--;
  bot_flank_tick(b);
  bot_deadlock_tick(b);
  if (b->alert_t > 0) b->alert_t--;
  if (b->prefire_t > 0) b->prefire_t--;
  if (b->prefire_cd > 0) b->prefire_cd--;
  b->prefiring = 0;
  b->peek = 0;
  if (b->cov_retry > 0) b->cov_retry--;
  if (b->swap_cd > 0) b->swap_cd--;
  bot_hear_update(b, bi);
  float flk = expf(-6.0f * TICK_DT);
  b->fl_y *= flk;
  b->fl_p *= flk;

  // Perception keeps a visible target and rescans every ten ticks. Spawn phases
  // spread full-roster LOS scans across the server ticks.
  float tdist = 1e9f;
  v3 sight_point = {{0, 0, 0}};
  int see = b->tgt >= 0 && ent_alive(b->tgt) &&
            bot_in_arc(b, b->tgt, BOT_KEEP_COS) &&
            bot_visible_point(b, b->tgt, &sight_point, &tdist);
  if (see) {
    int reacquired = b->tgt >= 0 && b->belief != BOT_BELIEF_VISIBLE;
    int hidden = b->lose_t;          // observe zeroes it; the RE-ARM needs it
    bot_belief_observe(b, b->tgt);
    b->belief = BOT_BELIEF_VISIBLE;
    if (reacquired) {
      b->reacquire_count++;
      // A RE-PEEK IS NOT A FREE SHOT. react_t and the settling aim error were armed
      // only on a target SWITCH or a damage reveal, so both survived an occlusion
      // intact: first contact cost 37 ticks to the first round, re-exposing the same
      // angle 60 ticks later cost two.
      float re = f_clamp((float)hidden / 45.0f, 0.0f, 1.0f);
      int t = (int)(sk->react_ms * 0.5f * re * (float)TICK_HZ / 1000.0f);
      if (b->react_t < t) b->react_t = t;
      if (b->err < sk->err0 * 0.5f * re) b->err = sk->err0 * 0.5f * re;
    }
  } else if (b->tgt >= 0) {
    if (!ent_alive(b->tgt)) {
      b->tgt = -1;
      b->lose_t = 0;
      bot_belief_clear(b);
    } else {
      if (b->belief == BOT_BELIEF_VISIBLE) {
        b->belief = BOT_BELIEF_OCCLUDED_HOLD;
        b->search_t = sk->hold_ticks;
        b->search_index = 0;
        b->search_visited = 0;
      }
      b->lose_t++;
      bot_belief_predict(b);
      if (b->belief == BOT_BELIEF_OCCLUDED_HOLD) {
        if (b->search_t > 0) b->search_t--;
        if (b->search_t <= 0) {
          b->belief = BOT_BELIEF_INVESTIGATE;
          bot_search_plan_next(b, sk);
        }
      }
      if (b->lose_t >= sk->memory_ticks) {
        b->tgt = -1;
        b->lose_t = 0;
        bot_belief_clear(b);
      }
    }
  }
  if (--b->scan_t <= 0) {
    b->scan_t = 10;
    float fx = sinf(b->yaw), fz = -cosf(b->yaw);
    v3 eo = bot_eye_pos(b);
    bot_target_choice_t best = {.ent = -1, .score = -1.0e30f, .dist = 1.0e9f};
    ENT_FOREACH(e) {   // without the human range, humans are invisible to the whole AI
      if (e == bi + 1 || !ent_alive(e)) continue;
      // Cheap rejects FIRST: most candidates fail the cone/heard gate, and the LOS
      // trace is the expensive test.
      v3 tp = ent_aimpoint(e);
      float d = v3_len(v3_sub(tp, eo));
      if (d < 0.01f || d > GUN_RANGE) continue;
      float dx = tp.x - b->pos.x, dz = tp.z - b->pos.z;
      float hl = sqrtf(dx * dx + dz * dz);
      if (hl <= 0.01f) continue;
      float align = (dx * fx + dz * fz) / hl;
      if (e != b->tgt && align <= BOT_FOV_COS) continue;
      // The current target's LOS was already traced by the perception step
      // this tick, and bot_can_see is pure — reuse that verdict.
      if (e == b->tgt) { if (!see) continue; }
      else if (!bot_can_see(b, e, &d)) continue;
      bot_target_choice_t cand = {
        .ent = e,
        .score = bot_target_score(b, e, d, align),
        .dist = d,
      };
      if (bot_target_better(cand, best)) best = cand;
    }
    // Visible candidates use one deterministic score with an explicit hold bonus and
    // epsilon ties.
    int hidden_current = b->tgt >= 0 && !see && ent_alive(b->tgt);
    int can_switch = best.ent >= 0 && best.ent != b->tgt &&
                     (!hidden_current || bot_target_breaks_hidden(b, best));
    if (can_switch) {
      bot_focus_invalidate(b);
      b->tgt = best.ent;
      b->lose_t = 0;
      b->err = sk->err0;
      b->react_t = (int)(sk->react_ms * rng_rangef(&g_rng_bot, 0.85f, 1.25f) *
                         (float)TICK_HZ / 1000.0f);
      see = bot_in_arc(b, b->tgt, BOT_KEEP_COS) &&
            bot_visible_point(b, b->tgt, &sight_point, &tdist);
      if (see) bot_belief_observe(b, b->tgt);
    }
  }

  int engaging = b->tgt >= 0 && see;
  // STARTLE. A fresh noise is NEWER information than a stale memory: a bot acts on what
  // it just heard whenever it is not currently looking at somebody.
  int startled = b->hear_tick && !engaging && b->hear_tick > b->last_seen_tick;
  b->startled = startled;   // census only (tacstat) — nothing branches on it
  float horizontal_speed = sqrtf(b->vel.x * b->vel.x + b->vel.z * b->vel.z);
  int other_weapon = (b->wp.cur + 1) % WPN_COUNT;
  int emergency_swap = engaging && b->wp.ammo[b->wp.cur] <= 0 &&
                       b->wp.ammo[other_weapon] > 0;
  int desired = bot_desired_weapon(b, engaging, tdist, horizontal_speed);
  if (b->wp.switch_t == 0 && (b->swap_cd == 0 || emergency_swap) &&
      bot_request_weapon(b, bi, desired))
    b->swap_cd = BOT_SWAP_COOLDOWN_TICKS;

  // An available alternate weapon wins before the automatic empty-mag reload.
  // When neither weapon can fire, retain the existing reload/cover behavior.
  if (b->wp.ammo[b->wp.cur] <= 0 && b->wp.reload_t == 0 && b->wp.switch_t == 0) {
    b->wp.reload_tac = 0;    // a bot only ever reloads when the mag is dry
    b->wp.reload_t = wpn_reload_ticks(b->wp.cur, 0);
    // .k is the TACTICAL bit, and it is on the push for the same reason the player's
    // is: a viewer re-starts this reload off the event and animates it against the
    // duration that bit selects.
    ev_push((event_t){.type = EV_RELOAD, .a = b->pos, .wpn = b->wp.cur,
                      .src = bi + 1, .k = 0});
  }

  // Decide where to look and where to move.
  float want_yaw = b->yaw, want_pitch = 0;
  // Intent, not physics: what a human would be holding on the keyboard this tick.
  // `pace` is the one scale on it — a bot that has not seen anybody walks its patrol,
  // and everything from the moment it engages runs at the player's full RUN SPEED.
  float wx = 0, wz = 0, pace = BOT_PATROL_PACE;
  int want_slide = 0, want_jump = 0;
  if (engaging) {
    bot_arm_engage(b, sk, tdist, sight_point, &want_yaw, &want_pitch, &wx, &wz, &pace,
                   &want_slide, &want_jump);
  } else if (startled) {
    bot_arm_startled(b, &want_yaw, &want_pitch, &wx, &wz, &pace);
  } else if (b->tgt >= 0) {
    bot_arm_belief(b, sk, &want_yaw, &want_pitch, &wx, &wz, &pace,
                   &want_slide, &want_jump);
  }
  if (b->tgt < 0 && !startled && b->alert_t > 0) {
    bot_arm_alert(b, &want_yaw, &wx, &wz, &pace);
  }
  if (b->tgt < 0 && !startled && b->alert_t == 0) {
    bot_arm_patrol(b, &want_yaw, &wx, &wz);
  }

  bot_tactical_steer(b, engaging, tdist, &wx, &wz, &pace);
  if (bot_suppress_steer(b, bi, engaging, &want_yaw, &wx, &wz, &pace)) {
    want_jump = 0;
    b->lean_want = 0;
  }
  // Traversal slides also serve retreats and cover transfers. A clear landing
  // lane and real launch speed prevent decorative slides straight into a wall.
  float intent = sqrtf(wx * wx + wz * wz);
  if (intent > 0.5f && b->grounded && !b->sliding && !b->slide_cd &&
      horizontal_speed > g_sim_mv[MV_RUN] * SLIDE_START_SPEED &&
      (b->tgt >= 0 || b->alert_t > 0) &&
      rng_rangef(&g_rng_bot, 0, 1) < sk->tac * fmaxf(b->agility, 0.75f) * 0.005f) {
    v3 lane = v3_add(b->pos, (v3){{wx / intent * 2.5f, 0, wz / intent * 2.5f}});
    if (bot_walk_clear(b->pos, lane)) want_slide = 1;
  }
  if (b->tgt >= 0 && !engaging && b->lose_t > 0 && b->lose_t < 120 &&
      b->grounded && !b->sliding && !b->jump_cd && b->hp >= 45 && intent > 0.5f &&
      (g_tick + bi) % 12 == 0 && rng_rangef(&g_rng_bot, 0, 1) < sk->tac * 0.4f) {
    v3 next = v3_add(b->pos, (v3){{wx / intent * 2.0f, 0, wz / intent * 2.0f}});
    v3 angle = bot_belief_aimpoint(b);
    if (bot_walk_clear(b->pos, next) && !los_clear(bot_eye_pos(b), angle) &&
        los_clear(v3_add(next, (v3){{0, EYE_STAND, 0}}), angle)) {
      want_jump = 1; want_slide = 0; b->peek = 1;
      b->jump_cd = (int)rng_rangef(&g_rng_bot, 300, 600);
    }
  }
  if (want_slide) want_jump = 0;
  if (want_jump && (b->hp < 40 || !aabb_fits(b->pos, PLAYER_RADIUS,
                                           HEIGHT_STAND + 0.8f))) want_jump = 0;
  int wanted_motion = intent > 0.1f;

  // Turn: proportional approach capped at the skill's rate — decisive on big angles,
  // gentle over the last degrees.
  float diff = wrap_pi(want_yaw - b->yaw);
  float mt = (startled ? BOT_ALERT_TURN : sk->turn) * TICK_DT;
  float tracking = 8.0f + 6.0f * sk->tac;
  b->yaw += f_clamp(diff * tracking * TICK_DT, -mt, mt);
  float pdiff = want_pitch - b->pitch;
  float mp = sk->turn * 0.7f * TICK_DT;   // pitch stays on the tracking rate
  b->pitch += f_clamp(pdiff * tracking * TICK_DT, -mp, mp);

  // Standing up needs headroom, or a bot uncrouches inside low cover — the same test
  // the player makes at its own decision point (player_fits, before player_col_height).
  // It has to land HERE, ahead of every consumer of the stance below: the wall-follow
  // height, the move speed, the hitbox eye and the collider height. Down past ent_move
  // it was inert — all four had already read the unguarded stance, and the next tick's
  // steering zeroed it before anything else could.
  if (b->crouch == 0 && !aabb_fits(b->pos, PLAYER_RADIUS, HEIGHT_STAND)) b->crouch = 1;

  // Local wall clearance complements the planned route and combat strafes.
  // Preserve the original movement intent for the stall detector below.
  {
    float bh_ = b->crouch ? HEIGHT_CROUCH : HEIGHT_STAND;
    for (int i = 0; i < g_num_solids; i++) {
      const solid_t *sd = &g_solids[i];
      // Only solids this body actually shares height with — a bot standing ON a
      // platform is inside it in plan view and must not be shoved off its own
      // footing. 0.2 m of lift is the step the locomotion already swallows.
      if (sd->max.y <= b->pos.y + 0.20f || sd->min.y >= b->pos.y + bh_) continue;
      // Signed distance in XZ to the face, through the box grown by the
      // collision radius — the same expansion move_axis resolves against.
      float ex = fmaxf(sd->min.x - PLAYER_RADIUS - b->pos.x,
                       b->pos.x - (sd->max.x + PLAYER_RADIUS));
      float ez = fmaxf(sd->min.z - PLAYER_RADIUS - b->pos.z,
                       b->pos.z - (sd->max.z + PLAYER_RADIUS));
      float d = fmaxf(ex, ez);
      if (d >= BOT_WALL_BAND) continue;
      // Outward normal = the axis the bot is least buried along, which for a body
      // beside a long wall is the wall's own face and for a body at a corner is the
      // nearer of the two.
      float nx = 0.0f, nz = 0.0f;
      if (ex >= ez) nx = b->pos.x > sd->max.x ? 1.0f : -1.0f;
      else          nz = b->pos.z > sd->max.z ? 1.0f : -1.0f;
      float into = wx * nx + wz * nz;
      if (into < 0.0f) { wx -= nx * into; wz -= nz * into; }  // slide, don't press
      // The elbow band: the last 50 mm, where the figure is wider than the box it
      // collides with.
      if (d < BOT_WALL_ELBOW) {
        float k = (BOT_WALL_ELBOW - d) * (1.0f / BOT_WALL_ELBOW) * 0.35f;
        wx += nx * k; wz += nz * k;
      }
    }
  }

  if (b->pin > 30 && wx * wx + wz * wz > 0.01f) {
    float s = b->strafe_dir >= 0 ? 1.0f : -1.0f;
    float ox = wx, oz = wz;
    switch (((b->pin - 1) / 30) % 3) {
      case 0: wx = -oz * s; wz = ox * s; break;   // slide along it
      case 1: wx = -ox;     wz = -oz;    break;   // back out
      default: wx = oz * s; wz = -ox * s; break;  // slide the other way
    }
  }

  // Move on the PLAYER's locomotion — same helpers, same numbers, same order.
  float wlen = sqrtf(wx * wx + wz * wz);
  if (wlen > 1.0f) { wx /= wlen; wz /= wlen; wlen = 1.0f; }
  float sp2d = sqrtf(b->vel.x * b->vel.x + b->vel.z * b->vel.z);
  // slide_cd is decremented ONCE per tick, up in the tactical-cooldown block beside
  // pause_t/react_t/jump_cd, the same shape player_tick uses.
  if (!b->sliding && b->grounded && want_slide && b->slide_cd == 0 &&
      sp2d > g_sim_mv[MV_RUN] * SLIDE_START_SPEED) {
    sp2d = mv_slide_launch(&b->vel, sp2d);
    b->sliding = 1;
    b->slide_cd = SLIDE_COOLDOWN_TICKS;
  }
  // A SLIDE IS A CROUCH — the player cannot slide without holding IN_CROUCH, since the
  // slide ends the tick it is released, so p->crouched is 1 for every sliding tick.
  // DERIVED, NOT STORED, and that is the whole point: only the stance machine is gated
  // on !sliding, while `in_cover`, the startle branch and the lost-sight branch write
  // crouch = 0 unconditionally on LATER ticks of the same slide. A 1 written at the
  // launch was dropped once and never restored, so about half of all slides finished
  // on a standing collider and a standing HITBOX over a body drawn with its hip at
  // 190 mm. Same shape as bot_puppet_tick's, which is why the puppet path was already
  // right. It ends the way a player's does: on leaving the ground or bleeding down to
  // crouch-walk speed.
  b->crouch = b->crouch || b->sliding;
  float ms = (b->crouch ? g_sim_mv[MV_CROUCH_SP] : g_sim_mv[MV_RUN]) * pace;
  sp2d = mv_locomote(b, wx, wz, wlen, sp2d, ms);
  // The jump reads grounded AFTER the locomotion step, which cannot change it.
  if (b->grounded && want_jump) {
    b->vel.y = mv_jump_speed();
    b->grounded = 0;
    b->sliding = 0;
  }
  // The hitbox eye follows the stance on the player's own ease (see bot_eye).
  float bot_eye_tgt = b->crouch ? EYE_CROUCH : EYE_STAND;
  b->eye += (bot_eye_tgt - b->eye) * f_clamp(EYE_LERP_RATE * TICK_DT, 0, 1);
  float bh = b->crouch ? HEIGHT_CROUCH : HEIGHT_STAND;
  b->vel.y -= GRAVITY * TICK_DT;
  b->grounded = 0;
  ent_move(&b->pos, &b->vel, &b->grounded, bh);
  arena_clamp(&b->pos);

  v3 moved = v3_sub(b->pos, b->prev_pos);
  float hs = sqrtf(moved.x * moved.x + moved.z * moved.z) / TICK_DT;
  if (wanted_motion && ++b->nav_progress_t >= 90) {
    v3 progress = v3_sub(b->pos, b->nav_progress);
    if (progress.x * progress.x + progress.z * progress.z < 0.36f) {
      b->nav_count = b->nav_at = b->nav_retry = 0;
      b->tactical_t = b->tactical_hold = 0;
      b->cov_valid = 0;
      b->target = b->pos;
      if (b->flank_t) bot_flank_release(b);
      if (b->alert_t) b->alert_t = 0;
    }
    b->nav_progress = b->pos; b->nav_progress_t = 0;
  }
  if (wanted_motion && hs < 0.3f) b->stuck++;
  else b->stuck = 0;
  // THE PIN, censused separately from `stuck` because the escape below RESETS stuck —
  // so stuck can never show how long a bot actually spent leaning on a wall, which is
  // the half of "stuck at the corner in a loop" that the escape exists to bound.
  if (wanted_motion && hs < 0.3f) b->pin++;
  else b->pin = 0;

  if (!engaging && b->tgt >= 0 && b->belief != BOT_BELIEF_VISIBLE &&
      b->stuck > 30) {
    bot_search_plan_next(b, sk);
    b->belief = BOT_BELIEF_SEARCH_SWEEP;
    b->stuck = 0;
  } else if (engaging && b->stuck > 30) {
    // PRESSED INTO GEOMETRY. Flipping the strafe was the only escape, and it is an
    // escape for the STRAFE leg alone: a bot walking to a cover spot or a flank anchor
    // ignores strafe_dir entirely, so it leaned on the wall until the commitment
    // expired.
    if (b->flank_t) bot_flank_release(b);
    b->cov_valid = 0;
    b->cov_retry = 10;
    b->cov_phase++;
    b->strafe_dir = -b->strafe_dir;
    b->stuck = 0;
  }

  b->lean = mv_lean((v3){{b->pos.x, b->pos.y + bot_eye(b), b->pos.z}},
                    b->vel, b->yaw, b->alive,
                    b->lean_want, &b->lean_in, &b->lean_clr);

  // Trigger discipline: shots leave only once the imperfect aim has settled onto the
  // target, after the reaction delay, in bursts with pauses. A PRE-FIRE window goes
  // through the same discipline; only what the settle is measured against differs.
  //
  // The settle gate is an allowed MISS DISTANCE, not a fixed angle. The aim follower
  // is a first-order lag (diff * 8 * dt), so its steady-state error against a target
  // sweeping w rad/s is w/8 — at 9 m against a 12 m/s strafe the crosshair trails by
  // 1.5 m permanently, and the old flat 0.05 rad gate (0.6 m at 12 m) could never
  // open. That is the "it stares at me and does not shoot" report.
  //
  // It opens ONLY after the bot has been unable to take the shot for a while.
  // Widening it permanently also fixes the stare and was measured and REJECTED: it
  // roughly doubles the volume of fire, drops accuracy 30% -> 18% and saturates the
  // voice pool at the 20-bot ceiling. The ordinary case is not broken and must not be
  // re-tuned to fix the broken one.
  //
  // b->stare counts consecutive engaging-and-ready ticks with no shot, so half a
  // second of it means the settle will not arrive on its own; a person then takes the
  // shot at where the target roughly is. Firing resets the counter, so this is one
  // trailing shot every half second while the tracking is hopeless, not a stream. The
  // 0.05 rad floor is the old number, so a distant duel keeps its discipline and so
  // does blind pre-fire (tdist is 1e9 there).
  float aim_gate = b->stare > 60 ? f_clamp(1.60f / fmaxf(tdist, 1.0f), 0.05f, 0.40f)
                                 : 0.05f;
  int trigger = (engaging || b->prefiring) && b->react_t == 0 &&
                b->pause_t == 0 && !b->sliding && b->wp.switch_t == 0 &&
                diff > -aim_gate && diff < aim_gate &&
                pdiff > -aim_gate * 1.2f && pdiff < aim_gate * 1.2f;
  // A walled rifle answers the bot exactly as it answers the player: dead trigger,
  // ADS denied. Gated here and not only by gun_shoot's silent return, because that
  // return neither drains ammo nor sets cool — a covered bot re-fired every tick
  // forever, and each attempt zeroed `stare`, blinding the very diagnostic built
  // to catch that stall. Gated BEFORE the stare bookkeeping, a walled bot counts
  // as staring and tacstat reports it.
  int walled = (trigger || engaging) &&
               gun_wall_blocked(bot_eye_pos(b),
                                b->yaw, b->pitch, &b->wp);
  trigger = trigger && !walled;
  gun_recoil_step(&b->wp, trigger);
  // THE STARE, counted because it is the one failure a player reports in words and no
  // other number here can see: the bot can shoot, has someone to shoot at, and the
  // trigger never comes. tacstat reports the longest run.
  if (trigger)
    b->stare = 0;
  else if (engaging && b->wp.reload_t == 0 && b->wp.switch_t == 0 &&
           b->wp.ammo[b->wp.cur] > 0)
    b->stare++;
  else if (b->stare > 0)
    b->stare--;
  if (trigger && b->wp.cool[b->wp.cur] == 0 && b->wp.reload_t == 0 &&
      b->wp.ammo[b->wp.cur] > 0) {
    bot_fire(bi);
    if (--b->burst <= 0) {
      b->burst = (int)rng_rangef(&g_rng_bot, (float)sk->burst_lo,
                                 (float)sk->burst_hi + 0.99f);
      b->pause_t = (int)(rng_rangef(&g_rng_bot, sk->pause_lo, sk->pause_hi) *
                         (float)TICK_HZ / 1000.0f);
    }
  }

  // ADS on the same ramp and the same ADS TIME as the player's: a bot shoulders the
  // weapon for a distant target, which removes hip spread without changing its speed.
  int want_ads = engaging && b->wp.reload_t == 0 && b->wp.switch_t == 0 &&
                 !b->sliding && !walled && tdist > 9.0f;
  // ...and the PUPPET can ask for it directly.
  gun_ads(&b->wp, want_ads);
  b->wp.ready_want = (unsigned char)(b->tgt >= 0 || startled || b->alert_t > 0);
  int idle_ok = gun_pose_state(&b->wp, &b->anim, b->vel,
                                 b->grounded, b->sliding);
  weapon_anim_tick(&b->anim, b->pos,
                   bot_eye_pos(b),
                   b->vel, b->yaw, b->pitch, b->grounded,
                   b->crouch, 0.0f, b->sliding, b->wp.ads_s, b->lean,
                   b->wp.ready_want, bi + 1, &b->wp, idle_ok);
  // (`startled` is in there because a bot that turns to a sound with the rifle
  // still slung is the single most "it did not really notice me" frame in the
  // game — the carry state is the only tell the player gets that hearing
  // happened at all, and the raise is ~110 ms, so it lands with the turn.)
  // (The 0.0f is the baked TUCK, and it is 0 by construction rather than by
  // omission: a bot's crouch only picks a shorter collider — `bh` above — and
  // never moves b->pos.y, so nothing is folded into its origin.)
}

// The map seed chain, in ONE place: match_restart advances it per restart and
// the harness's mapcheck walks (and restores) the same chain, so a mapcheck
// `worst_seed` stays reproducible via `restart`.
static uint32_t map_seed_next(uint32_t s) { return s * 747796405u + 2891336453u; }

// THE SCOREBOARD'S OWN ROWS, cleared as one unit. BOTH edges of a networked
// session need exactly this: the join (the pre-join local arena scored into
// rows whose ids collide with server entities, and a stale over=1 pinned the
// VICTORY banner and blocked predicted fire) and the leave (nothing of the
// server's board may survive into the local match). Two copies is how one of
// the two ends up missing the field the other clears.
//
// The four ENTITY-ID-INDEXED counter arrays are their own unit: the client's
// NP_SCORES adoption needs exactly those and must NOT touch over/winner, and a
// fifth counter has to land in both places or it does not land at all.
static void match_counters_clear(void) {
  memset(g_match.kills, 0, sizeof g_match.kills);
  memset(g_match.deaths, 0, sizeof g_match.deaths);
  memset(g_match.shots, 0, sizeof g_match.shots);
  memset(g_match.hits, 0, sizeof g_match.hits);
}
static void match_scores_clear(void) {
  match_counters_clear();
  g_match.over = 0; g_match.winner = 0; g_match.last_killer = 0;
}

static void match_restart(void) {
  int n = g_match.nbots;
  memset(&g_match, 0, sizeof g_match);   // the human roster lives OUTSIDE
  g_match.nbots = n;                     // g_match and survives this on purpose
  // Fresh procedural arena every match; everyone respawns onto it.
  g_map_seed = map_seed_next(g_map_seed);
  map_generate(g_map_seed);
  g_world_dirty = 1;
  // EVERY human: respawning only one leaves the others on their old
  // positions inside freshly generated geometry.
  for (int h = 0; h < MAX_HUMANS; h++)
    if (g_humans_on[h])
      player_respawn(&g_players[h], ent_of_human(h));
  for (int i = 0; i < n; i++) bot_spawn(i);
  vfx_match_reset();
  bot_suppress_complete();
}

// THE ONE MATCH-OVER COUNTDOWN — networked sessions only.
static void match_over_countdown(void) {
  if (!g_server && !g_online) return;
  if (!g_match.over) { g_over_t = 0; return; }
  if (g_over_t < MATCH_OVER_TICKS) g_over_t++;
  if (g_server && g_over_t >= MATCH_OVER_TICKS) {
    g_over_t = 0;
    g_match.restart_req = 1;
  }
}

// Seconds still on that countdown, for the HUD. Rounded UP, so the first frame reads
// the full 10 and the last second reads 1.
static int match_over_secs(void) {
  int left = MATCH_OVER_TICKS - g_over_t;
  if (left < 1) left = 1;
  return (left + TICK_HZ - 1) / TICK_HZ;
}

// Runs once per sim tick: applies the active roster target (fresh bots spawn,
// extras retire), services a queued restart, then ticks every active bot.
static void match_tick(void) {
  int want = sp_bot_count();
  while (g_match.nbots < want) {
    int i = g_match.nbots++;
    g_match.kills[i + 1] = g_match.deaths[i + 1] = 0;
    g_match.shots[i + 1] = g_match.hits[i + 1] = 0;
    bot_spawn(i);
  }
  while (g_match.nbots > want) {
    bot_t *retired = &g_bots[--g_match.nbots];
    retired->active = 0;
    retired->suppress = (bot_suppression_t){0};
  }

  // The server's half of the over countdown (see match_over_countdown): this
  // is the caller that owns the restart. Offline it returns immediately.
  match_over_countdown();

  if (g_match.restart_req) {
    g_match.restart_req = 0;
    match_restart();
  }

  for (int i = 0; i < g_match.nbots; i++) bot_tick(i);
}

static void match_init(void) {
  memset(&g_match, 0, sizeof g_match);
  memset(g_bots, 0, sizeof g_bots);
  g_match.nbots = sp_bot_count();
  for (int i = 0; i < g_match.nbots; i++) bot_spawn(i);
  for (int h = 0; h < MAX_HUMANS; h++)   // every human starts with a
    if (g_humans_on[h])                  // solved skeleton, not garbage
      anim_reset(&g_player_anim[h], g_players[h].pos, g_players[h].yaw,
                 g_players[h].wp.cur);
  bot_suppress_complete();
}
