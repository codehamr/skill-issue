// =============================================================================
// harness/script.inc  —  run_script, the harness command dispatch. Decomposed
// into the SCRIPT_CMDS dispatch table; one measured arm (vmcheck) retained in
// the chain — see the comment at that arm.
// Proof: the whole script battery. One TU: included once by game.c, in
// definition order — moving a definition out is a semantic change.
// Contract: a fixed --config path is a contaminated measurement across
// concurrent sessions — always pass a fresh `mktemp -u` path.
// =============================================================================

// The script vocabulary is a TABLE, not a strcmp chain. The chain was 2016
// lines in one function; each arm is independent (measured: no local flows
// between arms — the shared state is exactly `s` plus globals), and the table
// makes that structural instead of merely true. Order in the table is
// irrelevant — lookup is by exact name — with two exceptions kept in the loop,
// because they are not name matches: the +A/-A prefix form, and the final
// unknown-command error path.
typedef void (*script_fn_t)(sim_ctx_t *s);
typedef struct { const char *name; script_fn_t fn; } script_cmd_t;

static void cmd_decorcheck(sim_ctx_t *s) { (void)s; decorcheck_proof(); }
static void cmd_framepace(sim_ctx_t *s) { (void)s; framepace_proof(); }
static void cmd_botsuppress(sim_ctx_t *s) { (void)s; botsuppress_proof(); }
static void cmd_bottactics(sim_ctx_t *s) { (void)s; bottactics_proof(); }
static void cmd_netloss(sim_ctx_t *s) { (void)s; netloss_proof(); }
static void cmd_bothear(sim_ctx_t *s)          { (void)s; bothear_proof(); }
static void cmd_botweapon(sim_ctx_t *s)        { (void)s; botweapon_proof(); }
static void cmd_botflank(sim_ctx_t *s)         { (void)s; botflank_proof(); }
static void cmd_botmemory(sim_ctx_t *s)        { (void)s; botmemory_proof(); }
static void cmd_botmemoryobserve(sim_ctx_t *s) { (void)s; botmemoryobserve_proof(); }
static void cmd_leanstep(sim_ctx_t *s)         { (void)s; leanstep_proof(); }
static void cmd_padbackend(sim_ctx_t *s)       { (void)s; padbackend_proof(); }
static void cmd_padsync(sim_ctx_t *s)          { (void)s; pad_sync(TICK_DT); }
static void cmd_netfuzz(sim_ctx_t *s)          { (void)s; netfuzz_proof(); }
static void cmd_netdeath(sim_ctx_t *s)         { (void)s; netdeath_proof(); }
static void cmd_netclient(sim_ctx_t *s)        { (void)s; netclient_proof(); }
static void cmd_nethandshake(sim_ctx_t *s)     { (void)s; nethandshake_proof(); }
static void cmd_recoil(sim_ctx_t *s)           { (void)s; recoil_proof(); }
static void cmd_gunwall(sim_ctx_t *s)          { (void)s; gunwall_proof(); }
static void cmd_boresight(sim_ctx_t *s)        { (void)s; boresight_proof(); }
static void cmd_naturalcheck(sim_ctx_t *s) { (void)s; naturalcheck_proof(); }
static void cmd_gaitcheck(sim_ctx_t *s) { (void)s; gaitcheck_proof(); }
static void cmd_animbench(sim_ctx_t *s) { (void)s; animbench_proof(next_n()); }
static void cmd_figmotion(sim_ctx_t *s) { (void)s; figmotion_proof(); }

static void cmd_fighit(sim_ctx_t *s) { (void)s; fighit_proof(); }

static void cmd_defaultsproof(sim_ctx_t *s)    { (void)s; defaults_proof(); }
static void cmd_netrecoil(sim_ctx_t *s)        { (void)s; netrecoil_proof(); }
static void cmd_vmframe(sim_ctx_t *s)           { (void)s; vmframe_proof(); }
static void cmd_figbury(sim_ctx_t *s)          { (void)s; figbury_proof(); }
static void cmd_figm(sim_ctx_t *s)             { (void)s; figm_proof(); }
static void cmd_pos(sim_ctx_t *s)              { sim_print(s); }
static void cmd_vmtrig(sim_ctx_t *s)           { vmtrig_proof(s); }
static void cmd_vmtrigfacet(sim_ctx_t *s) {
  (void)s;
  int fail = vmtrig_exact_cert_selftest();
  printf("vmtrigfacet proof=%d %s\n", fail, fail ? "FAIL" : "ok");
  if (fail) exit(1);
}
static void cmd_vmfiref1c(sim_ctx_t *s) {
  (void)s;
  int cases = 0, passed = 0;
  const char *first = NULL;
  int fail = vmtrig_fire_f1c_selftest(&cases, &passed, &first);
  printf("vmfiref1c cases=%d pass=%d first=%s proof=%d %s\n",
         cases, passed, first ? first : "none", fail, fail ? "FAIL" : "ok");
  if (fail) exit(1);
}
static void cmd_flinch(sim_ctx_t *s)           { flinch_proof(s); }

static void cmd_wait(sim_ctx_t *s) {
  long n = next_n();
  while (n-- > 0) sim_advance(s);
}

static void cmd_tap(sim_ctx_t *s) {
  int a = action_from_name(next_tok());
  s->held[a] = 1;
  sim_advance(s);
  s->held[a] = 0;
}

static void cmd_look(sim_ctx_t *s) {
  float dx = next_f(), dy = next_f();
  player_look(s->p, dx, dy, g_cfg.sense);
}

static void cmd_wheel(sim_ctx_t *s) {
  (void)s;
  // One notch of the mouse wheel, consumed by the next `appframe`.
  char *d = next_tok();
  g_pressed[strcmp(d, "down") ? KEY_MWUP : KEY_MWDN] = 1;
}

static void cmd_key(sim_ctx_t *s) {
  (void)s;
  char *tok = next_tok();
  if (g_ui_name_edit) ui_name_key(tok);  // name field has key focus
  else ui_assign_bind(tok);
}

static void cmd_weapon(sim_ctx_t *s) {
  const char *n = next_tok();
  int found = 0;
  for (int i = 0; i < WPN_COUNT; i++)
    if (!strcmp(n, WPN_DEF[i].prefix)) {
      s->p->wp.cur = i;       // instant, no raise anim: test convenience
      s->p->wp.switch_t = 0;
      s->p->wp.reload_t = 0;
      memset(s->p->wp.cool, 0, sizeof s->p->wp.cool);
      // ...and bot 0 with it while the puppet drives it.
      if (g_bots[0].puppet) {
        g_bots[0].wp.cur = i;
        g_bots[0].wp.switch_t = 0;
        g_bots[0].wp.reload_t = 0;
        memset(g_bots[0].wp.cool, 0, sizeof g_bots[0].wp.cool);
      }
      found = 1;
    }
  if (!found) { fprintf(stderr, "script: unknown weapon '%s'\n", n); exit(1); }
}

static void cmd_aim(sim_ctx_t *s) {
  v3 tgt;
  tgt.x = next_f();
  tgt.y = next_f();
  tgt.z = next_f();
  aim_at(s->p, tgt);
}

static void cmd_warp(sim_ctx_t *s) {
  // Teleport the player.
  s->p->pos.x = next_f();
  s->p->pos.y = next_f();
  s->p->pos.z = next_f();
  s->p->prev_pos = s->p->pos;
  s->p->vel = (v3){{0, 0, 0}};
  s->p->grounded = 1;
  s->p->tuck_y = 0.0f;   // a teleport is a clean slate; anim_reset zeroes
                         // the figure's half of this on the next line
  anim_reset(&g_player_anim[0], s->p->pos, s->p->yaw, s->p->wp.cur);
}

static void cmd_filmcueproof(sim_ctx_t *s) {
  (void)s; if (audiocheck_lifecycle(filmcue_samples)) exit(1);
}

static void cmd_filmtrackproof(sim_ctx_t *s) { (void)s; filmtrack_proof(); }

static void cmd_filmtrack(sim_ctx_t *s) {
  const char *arg = next_tok();
  if (!strcmp(arg, "off")) { s->film.enabled = 0; return; }
  if (!strcmp(arg, "log")) { s->film.log = next_n() != 0; return; }
  if (!strcmp(arg, "stat")) {
    printf("filmtrack summary ticks=%ld target=%d life=%d lost=%d max_speed=%.5f max_accel=%.5f max_error=%.7f shots=%d face_shots=%d head_hits=%d violations=%d %s\n",
        s->film.ticks, s->film.target, s->film.life, s->film.lost,
        (double)s->film.max_speed, (double)s->film.max_accel, (double)s->film.max_error,
        s->film.shots, s->film.face_shots, s->film.head_hits, s->film.violations,
        s->film.violations ? "FAIL" : "ok");
    if (s->film.violations) exit(1);
    return;
  }
  char *end;
  long target = strtol(arg, &end, 10);
  float yaw_amp = next_f(), pitch_amp = next_f();
  if (*end || target < 0 || target >= MAX_ENTS || target == g_local_ent ||
      !ent_alive((int)target) || f_is_nonfinite(yaw_amp) || f_is_nonfinite(pitch_amp) ||
      yaw_amp < 0 || yaw_amp > 0.5f || pitch_amp < 0 || pitch_amp > 0.5f) {
    fprintf(stderr, "filmtrack: need a live foreign entity and breathing amplitudes 0..0.5 degrees\n");
    exit(1);
  }
  film_track_begin(&s->film, s->p, (int)target, yaw_amp, pitch_amp);
}

static void cmd_aimbot(sim_ctx_t *s) {
  int bi = -1;
  float bd = 1e18f;
  for (int i = 0; i < g_match.nbots; i++) {
    if (!g_bots[i].active || !g_bots[i].alive) continue;
    float dx = g_bots[i].pos.x - s->p->pos.x, dz = g_bots[i].pos.z - s->p->pos.z;
    float dd = dx * dx + dz * dz;
    if (dd < bd) { bd = dd; bi = i; }
  }
  if (bi >= 0) {
    v3 h = {{g_bots[bi].pos.x,
             g_bots[bi].pos.y + bot_eye(&g_bots[bi]) + 0.03f,  // head center
             g_bots[bi].pos.z}};
    aim_at(s->p, h);
  }
}

static void cmd_botfreeze(sim_ctx_t *s) {
  (void)s;
  char *arg = take_tok();
  if (!arg) g_bots_frozen = !g_bots_frozen;
  else if (!strcmp(arg, "on")) g_bots_frozen = 1;
  else if (!strcmp(arg, "off")) g_bots_frozen = 0;
  else {
    g_bots_frozen = !g_bots_frozen;
    g_tok_pushback = arg;
  }
}

static void cmd_puppet(sim_ctx_t *s) {
  (void)s;
  char *sub = next_tok();
  bot_t *b = &g_bots[0];
  if (!strcmp(sub, "on")) {
    b->puppet = 1;
    b->pup_speed = 5.0f;
    b->pup_face = b->yaw;
    b->pitch = 0.0f;
    b->pup_mx = b->pup_mz = 0;
    b->pup_crouch = 0;
    b->pup_ready = 1;
    b->pup_lean_t = b->pup_lean = 0;
  } else if (!strcmp(sub, "off")) {
    b->puppet = 0;
  } else if (!strcmp(sub, "move")) {
    b->pup_mx = next_f();
    b->pup_mz = next_f();
  } else if (!strcmp(sub, "speed")) {
    b->pup_speed = next_f();
  } else if (!strcmp(sub, "face")) {
    b->pup_face = next_f() * DEG2RAD;
  } else if (!strcmp(sub, "pitch")) {
    b->pitch = next_f() * DEG2RAD;
  } else if (!strcmp(sub, "warp")) {  // teleport for camera framing
    b->pos.x = next_f();
    b->pos.z = next_f();
    b->pos.y = 0;
    // never warp INTO geometry: step up until the body fits (platforms)
    while (b->pos.y < 3.0f && !aabb_fits(b->pos, PLAYER_RADIUS, HEIGHT_STAND))
      b->pos.y += 0.05f;
    b->prev_pos = b->pos;
    b->vel = (v3){{0, 0, 0}};
    anim_reset(&b->anim, b->pos, b->yaw, b->wp.cur);
  } else if (!strcmp(sub, "crouch")) {
    b->pup_crouch = (int)next_n();
  } else if (!strcmp(sub, "ready")) {
    b->pup_ready = (int)next_n();
  } else if (!strcmp(sub, "ads")) {
    b->pup_ads = (int)next_n();
  } else if (!strcmp(sub, "lean")) {
    b->pup_lean_t = f_clamp(next_f(), -1.0f, 1.0f);
  } else if (!strcmp(sub, "jump")) {
    b->pup_jump = 1;
  } else if (!strcmp(sub, "slide")) {
    b->pup_slide = 1;
  } else {
    fprintf(stderr, "script: unknown puppet sub-command '%s'\n", sub);
    exit(1);
  }
}

static void cmd_uistat(sim_ctx_t *s) {
  (void)s;
  // qarm/nedit/kb: the MODAL states (armed QUIT confirm, name capture, the
  // on-screen keyboard) — without them the harness could only assert any of
  // them by pixel-diffing. kb prints the CURSOR as well as the flag: the
  // grid's whole state is that pair, so a nav proof needs no screenshot.
  printf("ui menu=%d tab=%d page=%s control=%s padsub=%d devsub=%d wpn=%d focus=%d/%d padmode=%d "
         "rebind=%d rebindpad=%d arm=%d nedit=%d kb=%d kbcur=%d,%d name=%s dev=%d curve=%s "
         "dz=(%.2f %.2f) sens=%.0f "
         "adss=%.2f assist=%d inv=%d conn=%d\n",
         g_menu_open, g_ui_tab, ui_page_name(g_ui_page),
         g_ui_control ? "gamepad" : "keyboard",
         g_ui_pad_sub, g_ui_dev_sub, g_ui_wpn,
         g_ui_focus, g_ui_nfocus_last, g_ui_pad_mode, g_ui_rebind,
         g_ui_rebind_pad, (int)g_ui_arm, g_ui_name_edit,
         g_ui_kb, g_ui_kb_r, g_ui_kb_c, g_cfg.name,
         DEV_MENU && g_cfg.devmode, PAD_CURVE_OPTS[g_cfg.pad_curve],
         (double)g_cfg.pad[PAD_DZ_L], (double)g_cfg.pad[PAD_DZ_R],
         (double)g_cfg.pad[PAD_SENS],
         (double)g_cfg.pad[PAD_ADS_SENS], g_cfg.pad_assist,
         g_cfg.pad_inv_y, g_pad.connected);
}

static void cmd_netstat(sim_ctx_t *s) {
  (void)s;
  // The SAME function the server's 10 s digest prints — that is
  // the one-copy rule on observability, checked rather than asserted.
  char dg[160];
  net_digest(dg, sizeof dg);
  printf("netstat %s\n", dg);
}

static void cmd_updinfo(sim_ctx_t *s) {
  (void)s;
  // The naming CONTRACT's proof half: prints the
  // WHOLE client asset table for the cross-check against the workflow,
  // plus the DIAGNOSTIC status line, which is on no screen (see upd_status_line).
  char own[64], line[96], sha[65];
  int have = update_asset_name(own, sizeof own);
  upd_status_line(line, sizeof line);
  printf("updinfo version=%s commit=%.7s own=%s writable=%d\n",
         BUILD_VERSION, BUILD_COMMIT, have ? own : "none",
         g_upd_writable);
  for (int i = 0; i < UPD_NASSETS; i++) {
    char nm[64];
    upd_asset_name(nm, sizeof nm, i);
    printf("asset %s\n", nm);
  }
  if (upd_own_sha(sha)) printf("sha %s\n", sha);
  printf("line %s\n", line);
  // The MENU-OPEN RE-CHECK's policy, tabulated the way padcurve tabulates its
  // curves: the predicate is pure, and it is the only part of that path a harness
  // can reach at all (the call itself lives in each native backend, past its own
  // not-the-harness gate).
  {
    static const struct { int st; const char *n; } RS[] = {
      {UPD_CURRENT, "current"}, {UPD_FAILED, "failed"},
      {UPD_RDONLY, "rdonly"},   {UPD_CHECKING, "checking"},
      {UPD_UPDATE, "update"},   {UPD_RECUT, "recut"},
    };
    long now = 1000000;
    printf("recheck window=%ds", UPD_RECHECK_S);
    for (int i = 0; i < (int)(sizeof RS / sizeof RS[0]); i++)
      printf(" %s=%d/%d/%d", RS[i].n,
             upd_recheck_due(now, 0, RS[i].st),
             upd_recheck_due(now, now - (UPD_RECHECK_S - 1), RS[i].st),
             upd_recheck_due(now, now - UPD_RECHECK_S, RS[i].st));
    printf("\n");
  }
}

static void cmd_updcheck(sim_ctx_t *s) {
  (void)s;
  // The update comparison against a LOCAL file — the whole rule set is provable
  // without the network, and the output can move (truncated file, missing line, bad
  // date, downgrade all print differently).
  const char *path = next_tok();
  char *ldate = take_tok();
  char local[16];
  snprintf(local, sizeof local, "%s", ldate ? ldate : BUILD_VERSION);
  char buf[8192];
  size_t len = 0;
  FILE *uf = fopen(path, "rb");
  if (uf) { len = fread(buf, 1, sizeof buf, uf); fclose(uf); }
  char own[64], own_sha[65], rdate[11] = "", rsha[65] = "";
  const char *why;
  // The DEV CONTAINER IS aarch64 and has no release asset, so update_asset_name()
  // writes the "none" sentinel here — and a comparison against an asset that
  // appears in no SHA256SUMS can only ever print MALFORMED, i.e. the proof of the
  // naming contract would be inert on the one machine it is run from.
  int have = update_asset_name(own, sizeof own);
  if (!have) upd_asset_name(own, sizeof own, 0);
  if (!upd_own_sha(own_sha)) own_sha[0] = '\0';
  upd_verdict_t v = uf ? upd_compare(buf, len, own, own_sha, local,
                                     rdate, rsha, &why)
                       : (why = "NO FILE", UPDV_MALFORMED);
  static const char *VN[] = {"MALFORMED", "CURRENT", "UPDATE", "RECUT",
                             "DOWNGRADE"};
  printf("updcheck verdict=%s local=%s remote=%s why=%s offer=%d asset=%s%s\n",
         VN[v], local, rdate[0] ? rdate : "-", why[0] ? why : "-",
         v == UPDV_UPDATE || v == UPDV_RECUT, own, have ? "" : " (stand-in)");
}

static void cmd_cfg(sim_ctx_t *s) {
  (void)s;
  printf("cfg sense=%.2f ads_sense=%.2f fov=%d vsync=%d fps_cap=%d "
         "show_fps=%d msaa=%d ssao=%d ssr=%d fullscreen=%d shadows=%d bloom=%d "
         "dust=%d grain=%d brightness=%.2f binds=",
         (double)g_cfg.sense, (double)g_cfg.ads_sense, (int)g_cfg.fov, g_cfg.vsync,
         g_cfg.fps_cap, g_cfg.show_fps, g_cfg.msaa, g_cfg.ssao, g_cfg.ssr,
         g_cfg.fullscreen, g_cfg.shadows, g_cfg.bloom, g_cfg.dust, g_cfg.grain,
         (double)g_cfg.brightness);
  for (int i = 0; i < IN_COUNT; i++) printf("%s%s", i ? "," : "", g_cfg.bind[i]);
  printf(" padbinds=");
  for (int i = 0; i < IN_COUNT; i++) printf("%s%s", i ? "," : "", g_cfg.padbind[i]);
  printf(" menu=%d rebind=%d tab=%d\n", g_menu_open, g_ui_rebind, g_ui_tab);
  printf("mv");
  for (int i = 0; i < MV_COUNT; i++)
    printf(" %s=%.2f", MV_DEF[i].key, (double)g_cfg.mv[i]);
  printf("\ngun");
  for (int i = 0; i < GP_COUNT; i++)
    printf(" %s=%.2f", GP_DEF[i].key, (double)g_cfg.gun[i]);
  printf("\nwp vol=%.2f", (double)g_cfg.vol);
  for (int w = 0; w < WPN_COUNT; w++)
    for (int i = 0; i < WP_COUNT; i++) {
      char key[32];
      wp_key(key, sizeof key, w, WP_DEF[i].key);
      printf(" %s=%.2f", key, (double)g_cfg.wp[w][i]);
    }
  printf("\npad");
  for (int i = 0; i < PAD_COUNT; i++)
    printf(" %s=%.2f", PAD_DEF[i].key, (double)g_cfg.pad[i]);
  printf("\n");
}

static void cmd_lean(sim_ctx_t *s) {
  // The lean's proof.
  const player_t *pl = s->p;
  anim_t tmp = g_player_anim[0];
  v3 hl = tmp.pose.head;              // head as solved WITH the lean
  tmp.lean_s = 0.0f;
  skel_solve(&tmp, pl->pos, pl->yaw, pl->pitch, pl->wp.cur, 0,
             SKEL_POSE_ONLY);
  v3 h0 = tmp.pose.head;              // ...and the same pose standing up
  v3 Ra = {{cosf(pl->yaw), 0, sinf(pl->yaw)}};
  float head_lat = (hl.x - h0.x) * Ra.x + (hl.z - h0.z) * Ra.z;
  v3 eb = player_eye_base(pl), ey = player_eye_pos(pl);
  float eye_lat = (ey.x - eb.x) * Ra.x + (ey.z - eb.z) * Ra.z;
  int see = 0;
  float sd;
  if (g_match.nbots > 0 && g_bots[0].active && g_bots[0].alive)
    see = bot_can_see(&g_bots[0], 0, &sd);
  printf("lean t=%ld cmd=%.3f clr=%.3f lean=%.3f body=%.3f roll=%.1f "
         "eye=(%.3f %.3f %.3f) off=%.3f drop=%.3f head_off=%.3f head_drop=%.3f "
         "gap=%.3f hbox=%.3f hbox_drop=%.3f see=%d\n",
         s->tick, (double)pl->lean_in, (double)pl->lean_clr, (double)pl->lean,
         (double)g_player_anim[0].lean_s,
         (double)(pl->lean * LEAN_ANGLE * LEAN_VIEW_K / DEG2RAD),
         (double)ey.x, (double)ey.y, (double)ey.z,
         (double)eye_lat, (double)(ey.y - eb.y),
         (double)head_lat, (double)(hl.y - h0.y),
         (double)fabsf(eye_lat - head_lat),
         // BOTH hitbox components, beside the head's own: the box used to take
         // only the lateral one, so head_drop and hbox_drop disagreed by 91 mm
         // and nothing printed the pair.
         (double)lean_eye_off(pl->lean), (double)-lean_eye_drop(pl->lean), see);
}

static void cmd_wpn(sim_ctx_t *s) {
  const weapon_t *w = &s->p->wp;
  const vm_t *v = &s->p->vm;
  printf("wpn t=%ld cur=%s ammo=%d bore=%.3f rec=(%.3f %.3f) ads=%.3f "
         "fire=%d reload=%d switch=%d vm sway=(%.4f %.4f) lag=(%.4f %.4f %.4f) "
         "kick=(%.4f %.3fdeg %.4f) roll=%.4f scope=(%.1f %.1f %.1f)\n",
         s->tick, WPN_DEF[w->cur].prefix, w->ammo[w->cur],
         (double)gun_bore_offset(w),
         (double)w->rec_p, (double)w->rec_y,
         (double)w->ads_s, w->fire_want, w->reload_t, w->switch_t,
         (double)v->sway_p, (double)v->sway_y,
         (double)v->lag_x, (double)v->lag_y, (double)v->lag_z,
         (double)v->kick_back, (double)(v->kick_lift / DEG2RAD),
         (double)v->kick_side, (double)v->roll,
         (double)g_scope_cx, (double)g_scope_cy, (double)g_scope_r);
}

static void cmd_bot(sim_ctx_t *s) {
  for (int i = 0; i < g_match.nbots; i++) {
    const bot_t *b = &g_bots[i];
    printf("bot %d %s t=%ld pos=(%.3f %.3f %.3f) yaw=%.2f hp=%d state=%s "
           "st=%s cov=%d tgt=%s respawn=%d",
           i, ent_name(i + 1), s->tick,
           (double)b->pos.x, (double)b->pos.y, (double)b->pos.z,
           (double)b->yaw, b->hp,
           !b->alive ? "dead" : g_bots_frozen ? "frozen"
                              : b->tgt >= 0 ? "combat" : "patrol",
           b->sliding ? "slide" : b->crouch ? "crouch" : "stand", b->in_cover,
           b->alive && b->tgt >= 0 ? ent_name(b->tgt) : "none",
           b->alive ? 0 : b->respawn_t);
    printf(" belief_state=%s lose=%d belief=(%.3f %.3f %.3f) rad=%.2f "
           "search=(%.3f %.3f %.3f) reacq=%d",
           bot_belief_label(b), b->lose_t,
           (double)b->belief_pos.x, (double)b->belief_pos.y,
           (double)b->belief_pos.z, (double)b->belief_radius,
           (double)b->search_pos.x, (double)b->search_pos.y,
           (double)b->search_pos.z, b->reacquire_count);
    // The FALL, which no screenshot answers: how far it has toppled, about which
    // horizontal axis, how far it has spun about its own vertical and what the
    // round threw it with.
    if (!b->alive)
      printf(" tilt=%.3f spin=%.2f dyaw=%.3f ax=(%.2f %.2f) "
             "dv=(%.2f %.2f %.2f) slam=%.2f gnd=%d",
             (double)b->rag.tilt, (double)b->rag.spin, (double)b->rag.yaw,
             (double)b->rag.axis.x, (double)b->rag.axis.z,
             (double)b->rag.vel.x, (double)b->rag.vel.y, (double)b->rag.vel.z,
             (double)b->rag.slam, b->rag.grounded);
    putchar('\n');
  }
}

static void cmd_match(sim_ctx_t *s) {
  // humans= is THE cheapest observable of the whole entity model:
  // the only output that would show a missing human at all — the byte
  // gate stays green while half a roster is invisible.
  int nh = 0;
  for (int h = 0; h < MAX_HUMANS; h++)
    if (g_humans_on[h]) nh++;
  printf("match t=%ld over=%d winner=%s bots=%d humans=%d frag=%d diff=%d "
         "php=%d palive=%d\n",
         s->tick, g_match.over, g_match.over ? ent_name(g_match.winner) : "none",
         g_match.nbots, nh, sp_rules()->fraglimit, sp_difficulty(), s->p->hp, s->p->alive);
  ENT_FOREACH(e)
    printf("score %s k=%d d=%d shots=%d hits=%d acc=%d%%\n", ent_name(e),
           g_match.kills[e], g_match.deaths[e], g_match.shots[e], g_match.hits[e],
           g_match.shots[e] ? g_match.hits[e] * 100 / g_match.shots[e] : 0);
}

static void cmd_budget(sim_ctx_t *s) {
  (void)s;
  // A dropped triangle looks exactly like geometry that was never authored,
  // so the peak has to be printable rather than inferred from a screenshot.
  // `ev_peak` is the other silent ceiling in the frame: MAX_EVENTS is a
  // fixed ring and a dropped EV_HIT is a hitmarker that never appeared.
  // ui_peak/ui_drops: the ONE ceiling that had no proof — an
  // overflowing HUD silently stops drawing scoreboard rows.
  printf("budget scene_verts=%d peak=%d max=%d drops=%ld world_verts=%d "
         "ev_peak=%d ev_max=%d ev_drops=%ld part_peak=%d/%d res=%d/%d "
         "world_decor=%d/%d world_drops=%ld ui_peak=%d/%d ui_drops=%ld "
         "world_cast_verts=%d world_pre_verts=%d decor_invalid=%ld "
         "acoustic_updates=%ld acoustic_rays=%ld per_update=%d interval_ticks=%d "
         "whizz_rays=%ld whizz_solid_tests=%ld wet_bytes=%zu\n",
         g_scene_floats / VERT_FLOATS, g_scene_peak / VERT_FLOATS,
         SCENE_MAX_VERTS, g_scene_drops, R.world_verts,
         g_ev_peak, MAX_EVENTS, g_ev_drops, g_part_peak, MAX_PARTICLES,
         g_part_res_peak, PARTS_SKID_LO,
         R.world_decor_verts, DECOR_MAX_VERTS, g_mesh_drops,
         g_ui_peak, UI_MAX_VERTS, g_ui_drops,
         R.world_cast_verts, R.world_pre_verts, g_decor_invalid,
         g_acoustic_updates, g_acoustic_queries, ACOUSTIC_PROBES, ACOUSTIC_INTERVAL,
         g_acoustic_whizz_rays, g_acoustic_whizz_solids, sizeof g_audio_wet);
  printf("suppress_budget segments=%llu bounds=%llu near=%llu impacts=%llu rays=%llu "
         "plans=%llu candidates=%llu candidate_limit=%d\n",
         (unsigned long long)g_match.suppress_segments,
         (unsigned long long)g_match.suppress_bounds,
         (unsigned long long)g_match.suppress_near,
         (unsigned long long)g_match.suppress_impacts,
         (unsigned long long)g_match.suppress_rays,
         (unsigned long long)g_match.suppress_plans,
         (unsigned long long)g_match.suppress_candidates, BOT_SUPPRESS_COVER_CANDIDATES);
}

static void cmd_sun(sim_ctx_t *s) {
  (void)s;
  // THE LIGHT IS MAP STATE, so without this the only way to see a given
  // time of day is to hunt seeds for it — and the whole range 6.5..54
  // degrees has to be reviewable, not just the four elevations the seeds
  // in the batteries happen to roll. With no argument it prints; with two
  // it drives sun_set, which is the same entry map generation uses.
  { char *a = take_tok();      // the opt_n idiom: take, test, push back
    if (!a) { /* bare `sun` prints */ }
    else {
      char *e;
      float ed = strtof(a, &e);
      if (e == a || *e || f_is_nonfinite(ed)) g_tok_pushback = a;
      else {
        sun_set(ed, next_f() * (F_PI / 180.0f));
        // The ground bake reads the sun (its shadow smears), so a moved
        // sun re-bakes: an instrument that disagrees with the world it
        // measures is the shimmer-rig trap in a different hat.
        g_world_dirty = 1;
      }
    } }
  { const sun_t *S = &g_sun;
    printf("sun elev=%.2f azi=%.2f dir=(%.3f %.3f %.3f) "
           "col=(%.3f %.3f %.3f) expo=%.3f sh_half=%.1f\n",
           (double)(asinf(f_clamp(S->dir.y, -1.0f, 1.0f)) * (180.0f / F_PI)),
           (double)(atan2f(S->dir.x, -S->dir.z) * (180.0f / F_PI)),
           (double)S->dir.x, (double)S->dir.y, (double)S->dir.z,
           (double)S->col.x, (double)S->col.y, (double)S->col.z,
           (double)S->exposure, (double)S->sh_half); }
}

// Find ordinary map seeds for fixture coverage without introducing overrides.
static uint32_t weatherproof_seed(weather_kind_t kind, uint32_t after) {
  for (uint32_t n = 1; n <= 10000; n++)
    if (weather_kind_from_seed(after + n) == kind) return after + n;
  fatal("weatherproof missing weather kind");
  return 0;
}

// Observe the production scheduler through the real audio admission trace.
// The mixer lifecycle wrapper owns/restores its lanes and cancellation state.
static int weatherproof_audio(void) {
  int fail = 0, flashes = 0, thunders = 0;
  uint32_t seed0 = g_map_seed;
  long tick0 = g_tick, flash_tick[2] = {-1, -1};
  typeof(g_weather_audio) state0 = g_weather_audio;
  g_weather_audio = (typeof(g_weather_audio)){.mode = -1, .tick = -1};
  g_map_seed = weatherproof_seed(WEATHER_STORM, 0);
  audiocheck_reset();
  fail |= !audio_trace_begin();
  float previous = 0.0f;
  for (g_tick = 0; g_tick < WEATHER_PERIOD * 2; g_tick++) {
    weather_t w = weather_sample();
    if (w.flash > 0.99f && previous <= 0.99f && flashes < 2)
      flash_tick[flashes++] = g_tick;
    previous = w.flash;
    weather_audio_tick();
    int count = g_audio_trace_n;
    weather_audio_tick();
    fail |= count != g_audio_trace_n;
    audio_mix_discard_pending();
  }
  for (int i = 0; i < g_audio_trace_n; i++) {
    const audio_trace_t *t = &g_audio_trace[i];
    if (t->id != SFX_THUNDER) continue;
    fail |= thunders >= flashes || (thunders < flashes &&
        t->source_tick - flash_tick[thunders] != 90);
    thunders++;
  }
  fail |= flashes != 2 || thunders != 2 || g_audio_invalid || g_audio_trace_overflow;
  // A new map with the same weather at the same tick must restart both
  // ambient lanes. A repeated call within that map must admit nothing twice.
  weather_audio_tick();
  int count = g_audio_trace_n;
  g_map_seed = weatherproof_seed(WEATHER_STORM, g_map_seed);
  weather_audio_tick();
  fail |= g_audio_trace_n != count + 2 || g_weather_audio.seed != g_map_seed;
  weather_audio_tick();
  fail |= g_audio_trace_n != count + 2;
  audio_trace_end();
  g_tick = tick0; g_map_seed = seed0; g_weather_audio = state0;
  printf("weatherproof audio flashes=%d thunder=%d delay_ticks=90 dedup=1 map_reset=1 %s\n",
         flashes, thunders, fail ? "FAIL" : "ok");
  return fail;
}

static void cmd_weatherproof(sim_ctx_t *s) {
  (void)s;
  proof_world_t *saved = malloc(sizeof *saved);
  float *scene0 = malloc((size_t)(g_scene_floats ? g_scene_floats : 1) * sizeof(float));
  if (!saved || !scene0) fatal("weatherproof allocation failed");
  proof_world_save(saved);
  memcpy(scene0, g_scene, (size_t)g_scene_floats * sizeof(float));
  int scene_state[] = {g_scene_floats, g_scene_want, g_scene_peak, g_scene_dirty,
                      g_nschunk, g_schunk_open, g_scene_cast_floats,
                      g_weather_rain_drops, g_weather_rain_sheltered};
  schunk_t chunks[SCENE_MAX_CHUNKS]; memcpy(chunks, g_schunk, sizeof chunks);
  long drops0 = g_scene_drops;
  uint32_t seed0 = g_map_seed;
  uint64_t rng0 = g_rng, bot0 = g_rng_bot, audio0 = g_rng_audio, vfx0 = g_rng_vfx;
  typeof(g_rng_gun) gun0; memcpy(gun0, g_rng_gun, sizeof gun0);
  int fail = 0, samples = 0, roof_cases = 0, vertices = 0;
  uint32_t seeds[WEATHER_KIND_COUNT];
  int counts[WEATHER_KIND_COUNT] = {0};
  uint32_t chain = 0;
  for (int i = 0; i < 10000; i++) {
    weather_kind_t kind = weather_kind_from_seed(chain);
    if ((unsigned)kind >= WEATHER_KIND_COUNT) fatal("weatherproof invalid kind");
    counts[kind]++;
    chain = map_seed_next(chain);
  }
  const int count_minimum[] = {4500, 1300, 1500, 400, 150};
  const int count_maximum[] = {5500, 2300, 2500, 1200, 700};
  for (int mode = 0; mode < WEATHER_KIND_COUNT; mode++) {
    fail |= counts[mode] < count_minimum[mode] || counts[mode] > count_maximum[mode];
    seeds[mode] = weatherproof_seed((weather_kind_t)mode, 0);
    g_map_seed = seeds[mode];
    for (g_tick = 0; g_tick < WEATHER_PERIOD * 2; g_tick++) {
      weather_t a = weather_sample(), b = weather_sample();
      float values[] = {a.rain, a.fog, a.overcast, a.wind, a.flash, a.rainbow};
      const float maximum[] = {1.0f, 1.2f, 1.0f, 5.6f, 1.0f, 1.0f};
      fail |= memcmp(&a, &b, sizeof a) != 0;
      for (size_t j = 0; j < sizeof values / sizeof values[0]; j++)
        fail |= f_is_nonfinite(values[j]) || values[j] < 0.0f || values[j] > maximum[j];
      fail |= mode < 2 && a.rain != 0.0f;
      fail |= mode != 3 && a.flash != 0.0f;
      fail |= mode != 4 && a.rainbow != 0.0f;
      samples++;
    }
  }
  printf("weatherproof seeds=10000 counts=%d/%d/%d/%d/%d deterministic=1 %s\n",
         counts[0], counts[1], counts[2], counts[3], counts[4], fail ? "FAIL" : "ok");
  // A large roof intercepts every rain column before it can enter the
  // shelter. The open control must emit near-ground drops at identical ticks.
  const long ticks[] = {0, 1, 17, 120, 543, 550, 633, 1199, 2399};
  for (int mode = 2; mode <= 4; mode++)
    for (size_t t = 0; t < sizeof ticks / sizeof ticks[0]; t++) {
      g_map_seed = seeds[mode]; g_tick = ticks[t]; g_num_solids = 0;
      scene_reset(); fx_weather_build((v3){{0.0f, 1.62f, 0.0f}});
      int open_low = 0;
      for (int i = 0; i < g_scene_floats; i += VERT_FLOATS) {
        open_low += g_scene[i + 1] < 3.0f;
        fail |= g_scene[i + 1] < -0.001f;
      }
      fail |= !open_low;
      g_solids[0] = (solid_t){.min = {{-80.0f, 3.5f, -80.0f}},
                              .max = {{80.0f, 3.8f, 80.0f}}};
      g_num_solids = 1;
      scene_reset(); fx_weather_build((v3){{0.0f, 1.62f, 0.0f}});
      fail |= !g_weather_rain_sheltered || !g_weather_rain_drops;
      for (int i = 0; i < g_scene_floats; i += VERT_FLOATS) {
        fail |= g_scene[i + 1] < 3.79f;
        for (int axis = 0; axis < 3; axis++) fail |= f_is_nonfinite(g_scene[i + axis]);
        vertices++;
      }
      roof_cases++;
    }
  memcpy(g_solids, saved->solids, sizeof saved->solids);
  g_num_solids = saved->num_solids;
  fail |= audiocheck_lifecycle(weatherproof_audio);
  int neutral = g_rng == rng0 && g_rng_bot == bot0 && g_rng_audio == audio0 && g_rng_vfx == vfx0
             && !memcmp(g_rng_gun, gun0, sizeof gun0)
             && !memcmp(g_players, saved->players, sizeof saved->players)
             && !memcmp(g_bots, saved->bots, sizeof saved->bots)
             && !memcmp(&g_match, &saved->match, sizeof g_match)
             && !memcmp(g_events, saved->events, sizeof saved->events);
  fail |= !neutral || g_scene_drops != drops0;
  proof_world_restore(saved); free(saved);
  g_map_seed = seed0;
  memcpy(g_scene, scene0, (size_t)scene_state[0] * sizeof(float)); free(scene0);
  g_scene_floats = scene_state[0]; g_scene_want = scene_state[1]; g_scene_peak = scene_state[2];
  g_scene_dirty = scene_state[3]; g_nschunk = scene_state[4]; g_schunk_open = scene_state[5];
  g_scene_cast_floats = scene_state[6]; g_weather_rain_drops = scene_state[7];
  g_weather_rain_sheltered = scene_state[8]; g_scene_drops = drops0;
  memcpy(g_schunk, chunks, sizeof chunks);
  printf("weatherproof modes=5 samples=%d roof_cases=%d checked_vertices=%d neutral=%d %s\n",
         samples, roof_cases, vertices, neutral, fail ? "FAIL" : "ok");
  if (fail) exit(1);
}

static void cmd_biome(sim_ctx_t *s) {
  (void)s;
  char *a = take_tok();
  int change = 0;
  if (a && !strcmp(a, "off")) { g_biome_force = -1; change = 1; }
  else if (a) {
    for (int i = 0; i < 7; i++)
      if (!strcmp(a, BIOME_NAMES[i])) { g_biome_force = i; change = 1; break; }
    if (!change) g_tok_pushback = a;
  }
  if (change) { map_generate(g_map_seed); g_world_dirty = 1; }
  printf("biome kind=%s night=%.0f snow=%.0f forest=%.0f aurora=%.0f lights=%d\n",
         BIOME_NAMES[g_biome.kind], (double)g_biome.night, (double)g_biome.snow,
         (double)g_biome.forest, (double)g_biome.aurora, g_map_light_n);
}

static void cmd_map(sim_ctx_t *s) {
  (void)s;
  printf("map seed=%u weather=%d\n", g_map_seed, weather_kind());
  // The theme owns the whole surface treatment (loose grain, glitter,
  // standing water) and it was previously unprintable — you could only find
  // out which one a seed rolled by photographing it.
  printf("biome kind=%s night=%.0f snow=%.0f forest=%.0f aurora=%.0f lights=%d\n",
         BIOME_NAMES[g_biome.kind], (double)g_biome.night, (double)g_biome.snow,
         (double)g_biome.forest, (double)g_biome.aurora, g_map_light_n);
  { v3 sp = surf_params();
    static const char *SN[] = {"grit", "wet", "sand"};
    printf("map theme=%s mood=%s sig=%s wind=%.3f grain=%.2f glint=%.2f "
           "water=%.2f haze=%.2f sun_elev=%.2f damage=%u/%u/%u/%u/%u\n",
           SN[g_theme.surf], MOOD_DEF[g_theme.surf][g_mood_preset].name,
           SIG_NAME[g_meta.sig], (double)g_wind_ang,
           (double)sp.x, (double)sp.y, (double)sp.z, (double)atmo_haze(),
           (double)(asinf(f_clamp(g_sun.dir.y, -1.0f, 1.0f))
                    * (180.0f / F_PI)),
           (unsigned)g_meta.damage[0], (unsigned)g_meta.damage[1],
           (unsigned)g_meta.damage[2], (unsigned)g_meta.damage[3],
           (unsigned)g_meta.damage[4]); }
  for (int i = 0; i < g_num_solids; i++)
    printf("solid %d min=(%.2f %.2f %.2f) max=(%.2f %.2f %.2f)\n", i,
           (double)g_solids[i].min.x, (double)g_solids[i].min.y, (double)g_solids[i].min.z,
           (double)g_solids[i].max.x, (double)g_solids[i].max.y, (double)g_solids[i].max.z);
  for (int i = 0; i < g_meta.nedge; i++) {
    const medge_t *e = &g_meta.edge[i];
    if (e->kind != ME_FRACTURE) continue;
    printf("fracture ax=%u a=(%.2f %.2f %.2f) b=(%.2f %.2f %.2f)\n",
           (unsigned)e->ax, (double)e->a.x, (double)e->a.y, (double)e->a.z,
           (double)e->b.x, (double)e->b.y, (double)e->b.z);
  }
}

static void cmd_mood(sim_ctx_t *s) {
  (void)s;
  // The mood is seed-hashed like the sun, so without this command a
  // specific latent setting can only be reached by hunting seeds — and the
  // whole latent cube has to be reviewable (nine presets x two sun
  // extremes is the acceptance battery for any of them). With three
  // arguments it FORCES the latents and regenerates; `mood off` hands the
  // seed its own values back.
  { char *a = take_tok();
    if (!a) { /* bare `mood` prints */ }
    else if (!strcmp(a, "off")) {
      g_mood_force = 0;
      g_mood_force_preset = -1;
      map_generate(g_map_seed);
      g_world_dirty = 1;
    } else {
      char *e;
      float ru = strtof(a, &e);
      if (e == a || *e || f_is_nonfinite(ru)) g_tok_pushback = a;
      else {
        // ONE numeric argument is a PRESET index (0..2 within the
        // arena's own theme, exact latents, no jitter — the reviewable
        // form the 9-mood sweep drives); THREE are raw latents. The
        // second token is parse-TESTED before it is committed, or the
        // next command's own name gets eaten as a float.
        char *b2 = take_tok();
        float g2 = 0.0f;
        int has2 = 0;
        if (b2) {
          char *e2;
          g2 = strtof(b2, &e2);
          if (e2 != b2 && !*e2 && !f_is_nonfinite(g2)) has2 = 1;
          else g_tok_pushback = b2;
        }
        if (!has2) {
          int pi = (int)ru;
          g_mood_force_preset = pi >= 0 && pi <= 2 ? pi : -1;
          g_mood_force = 0;
        } else {
          g_mood_forced = (mood_t){f_clamp(ru, 0.0f, 1.0f),
                                   f_clamp(g2, 0.0f, 1.0f),
                                   f_clamp(next_f(), 0.0f, 1.0f)};
          g_mood_force = 1;
          g_mood_force_preset = -1;   // raw latents, the seed's own preset
        }
        map_generate(g_map_seed);
        g_world_dirty = 1;
      }
    } }
  printf("mood ruin=%.2f growth=%.2f strew=%.2f preset=%s forced=%d\n",
         (double)g_mood.ruin, (double)g_mood.growth, (double)g_mood.strew,
         MOOD_DEF[g_theme.surf][g_mood_preset].name,
         g_mood_force || g_mood_force_preset >= 0);
}

static void cmd_scat(sim_ctx_t *s) {
  (void)s;
  // The scatter plan's census: what the placement pass decided, per
  // family, and what deciding cost — a printed number, because "startup
  // feels fine" is not a measurement and the plan runs on every map.
  // Re-planned here so the print always reflects the LOADED arena — the
  // upload's own run happens at the next rendered frame, which a script
  // that just moved the mood has not produced yet. Pure, so it is free.
  scat_plan();
  printf("scat plan_ms=%.3f sites=%d/%d max_top=%.3f", (double)g_scat_ms,
         g_scat_n, SCAT_MAX, (double)g_scat_top);
  for (int i = 0; i < SCAT_FAM_COUNT; i++)
    printf(" %s=%d", SCAT_FAM_NAME[i], g_scat_fam[i]);
  printf("\n");
}

static void cmd_vmsight(sim_ctx_t *s) {
  // SIGHT-CLEARANCE PROOF. No part of the shooter's own hands or forearms may enter
  // the cone the eye looks through.
  static const char *const PART[] = {
    "vm_hands", "palm", "thumb", "trigger_digit", "grip_digits"
  };
  player_t tmp = *s->p;
  v3 eye = {{0, 0, 0}}, rgt = {{1, 0, 0}}, upv = {{0, 1, 0}}, fwd = {{0, 0, -1}};
  int total_bad = 0;
  for (int wi = 0; wi < WPN_COUNT; wi++) {
    // A scoped weapon is immune BY CONSTRUCTION and saying so beats skipping it:
    // the sniper's sight picture is a HUD disc composited after every world pass,
    // so no geometry — hand, tube or otherwise — can land on top of it.
    if (wi == WPN_SR) {   // the only weapon with a PiP scope
      printf("vmsight %s n/a — sight picture is a composited disc, "
             "occlusion impossible\n", WPN_DEF[wi].prefix);
      continue;
    }
    tmp.wp.cur = wi;
    int wbad = 0;
    int part_bad[5] = {0};
    float part_margin[5] = {1e9f, 1e9f, 1e9f, 1e9f, 1e9f};
    int range_bad[PROBE_MAX] = {0};
    float range_margin[PROBE_MAX];
    for (int i = 0; i < PROBE_MAX; i++) range_margin[i] = 1e9f;
    float wmargin = 1e9f;
    float wads = 0.0f;
    for (int a2 = 4; a2 <= 10; a2++) {   // only aimed states have a sight picture
      tmp.wp.ads_lin = (float)a2 / 10.0f;
      tmp.wp.ads_s = tmp.wp.prev_ads_s = smooth01(tmp.wp.ads_lin);
      tmp.wp.reload_t = 0; tmp.wp.switch_t = 0; tmp.wp.prev_switch = 0.0f;
      tmp.vm.kick_back = tmp.vm.prev_kick_back = 0.0f;
      tmp.vm.kick_lift = tmp.vm.prev_kick_lift = 0.0f;
      tmp.vm.kick_side = tmp.vm.prev_kick_side = 0.0f;
      g_nprobe = 0; g_probe_on = 1;
      vm_build(&tmp, 1.0f, eye, rgt, upv, fwd, 1.0f);
      g_probe_on = 0;
      v3 ap = v3_sub(g_gun_sight_p, eye);
      float apd = v3_dot(ap, fwd);
      if (apd < 0.01f) continue;
      float tan_ap = g_gun_sight_r / apd;   // the aperture's angular radius
      for (int i = 0; i < g_nprobe; i++) {
        int part = -1;
        if (g_probe[i].name)
          for (int k = 0; k < 5; k++)
            if (!strcmp(g_probe[i].name, PART[k])) { part = k; break; }
        if (part < 0) continue;
        for (int f = g_probe[i].v0; f < g_probe[i].v1; f += VERT_FLOATS) {
          v3 d = {{g_scene[f] - eye.x, g_scene[f + 1] - eye.y,
                   g_scene[f + 2] - eye.z}};
          float z = v3_dot(d, fwd);
          if (z <= apd) continue;          // nearer than the aperture: harmless
          v3 lat = v3_sub(d, v3_scale(fwd, z));
          float tan_v = sqrtf(v3_dot(lat, lat)) / z;
          // Positive margin = outside the cone. Reported in milliradians so
          // the number means something at any screen size.
          float m = (tan_v - tan_ap) * 1000.0f;
          if (m < 0.0f) { wbad++; part_bad[part]++; range_bad[i]++; }
          if (m < part_margin[part]) part_margin[part] = m;
          if (m < range_margin[i]) range_margin[i] = m;
          if (m < wmargin) { wmargin = m; wads = tmp.wp.ads_lin; }
        }
      }
    }
    printf("vmsight %s occluding_verts=%d worst_margin=%.1f mrad at ads=%.1f\n",
           WPN_DEF[wi].prefix, wbad, (double)wmargin, (double)wads);
    if (wbad) {
      printf("vmsight %s parts", WPN_DEF[wi].prefix);
      for (int k = 0; k < 5; k++)
        if (part_bad[k]) printf(" %s=%d/%.1f", PART[k], part_bad[k],
                                (double)part_margin[k]);
      printf("\n");
      printf("vmsight %s ranges", WPN_DEF[wi].prefix);
      for (int i = 0; i < g_nprobe; i++)
        if (range_bad[i]) printf(" %d:%s=%d/%.1f", i, g_probe[i].name,
                                 range_bad[i], (double)range_margin[i]);
      printf("\n");
    }
    total_bad += wbad;
  }
  printf("vmsight total=%d (must be 0)\n", total_bad);
  if (total_bad) exit(1);
}

static int vmscope_crop_proof(void) {
  const float apertures[] = {0.51f, 2.5f, 4, 10, 30, 60};
  int bad = 0, vertices = 0;
  for (unsigned i = 0; i < sizeof apertures / sizeof *apertures; i++) {
    float rad = apertures[i], lens = 60, cx = 140, cy = 120;
    ui_begin();
    rf_scope_glass(cx, cy, rad, lens, 1);
    if (g_ui_nverts != SCOPE_SEGS * 3) bad++;
    for (int v = 0; v < g_ui_nverts; v++) {
      const float *p = g_ui_vb + v * 9;
      float x = p[0] / g_uis - cx, y = p[1] / g_uis - cy;
      // One UV texel must cover the same physical lens distance at every opening.
      if (fabsf(x - (p[2] - 0.5f) * 2 * lens) > 0.0001f ||
          fabsf(y + (p[3] - 0.5f) * 2 * lens) > 0.0001f ||
          x * x + y * y > rad * rad + 0.002f) bad++;
      for (int field = 0; field < 9; field++) if (f_is_nonfinite(p[field])) bad++;
      vertices++;
    }
    ui_begin();
    rf_scope_reticle(cx, cy, rad, lens);
    if (rad < 3.3f && g_ui_nverts) bad++;
    if (rad == 60 && g_ui_nverts != 72) bad++;
    if (g_ui_nverts && fabsf(fabsf(g_ui_vb[1] / g_uis - cy) - 0.6f) > 0.0001f) bad++;
    for (int v = 0; v < g_ui_nverts; v++) {
      const float *p = g_ui_vb + v * 9;
      float x = p[0] / g_uis - cx, y = p[1] / g_uis - cy;
      if (x * x + y * y > rad * rad + 0.002f ||
          (fabsf(x) < 3.2999f && fabsf(y) < 3.2999f)) bad++;
      vertices++;
    }
  }
  float saved[3] = {g_scope_r, g_scope_cx, g_scope_cy};
  g_scope_r = 60; g_scope_cx = 320; g_scope_cy = 180;
  if (!scope_aperture_visible(1, 640, 360)) bad++;
  const float outside[][2] = {{-61,180}, {701,180}, {320,-61}, {320,421},
                              {INFINITY,180}, {320,NAN}};
  for (unsigned i = 0; i < sizeof outside / sizeof *outside; i++) {
    g_scope_cx = outside[i][0]; g_scope_cy = outside[i][1];
    if (scope_aperture_visible(1, 640, 360)) bad++;
  }
  g_scope_r = saved[0]; g_scope_cx = saved[1]; g_scope_cy = saved[2];
  printf("vmscope crop apertures=6 vertices=%d offscreen=6 %s\n", vertices, bad ? "FAIL" : "ok");
  return bad;
}

// Read the actual shared shader UV field into a temporary float target. Pixel
// centres cover both the exact aiming centre and the four exact rim endpoints;
// display quantization must not hide the deliberately small optical curvature.
static int vmscope_shader_proof(void) {
  enum { SIDE = 257 };
  static float pixels[SIDE * SIDE * 4];
  GLuint target = 0, texture = 0;
  GLint old_fbo = 0, old_view[4], old_program = 0;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_fbo);
  glGetIntegerv(GL_VIEWPORT, old_view);
  glGetIntegerv(GL_CURRENT_PROGRAM, &old_program);
  GLboolean depth = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND);
  GLboolean cull = glIsEnabled(GL_CULL_FACE);
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, SIDE, SIDE, 0, GL_RGBA, GL_FLOAT, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glGenFramebuffers(1, &target);
  rt_bind(target, SIDE, SIDE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
  int bad = glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE;
  glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
  glUseProgram(R.prog_hud);
  glUniform2f(R.u_screen, (float)R.width, (float)R.height);
  glUniform1i(R.u_tex, 0);
  glUniform1i(R.u_img, 3);
  glBindTexture(GL_TEXTURE_2D, R.font_tex);  // never sample the output attachment
  glBindVertexArray(R.vao_hud);
  ui_begin();
  const float white[4] = {1, 1, 1, 1};
  float edge = 0.5f / (SIDE - 1);
  ui_quad(0, 0, g_ui_vw, g_ui_vh, -edge, 1 + edge, 1 + edge, -edge, white);
  ui_flush();
  glReadPixels(0, 0, SIDE, SIDE, GL_RGBA, GL_FLOAT, pixels);
  float max_warp = 0.0f, max_symmetry = 0.0f, center_error = 0.0f;
  int samples = 0;
  for (int y = 0; y < SIDE; y++) {
    float v = (float)y / (SIDE - 1);
    for (int x = 0; x < SIDE; x++) {
      float u = (float)x / (SIDE - 1);
      float qx = u * 2.0f - 1.0f, qy = v * 2.0f - 1.0f;
      if (qx * qx + qy * qy > 1.0f) continue;
      const float *pixel = pixels + (y * SIDE + x) * 4;
      float got_u = (pixel[0] - 0.25f) * 2.0f;
      float got_v = (pixel[1] - 0.25f) * 2.0f;
      float warp = fmaxf(fabsf(got_u - u), fabsf(got_v - v));
      if (warp > max_warp) max_warp = warp;
      if (got_u < -0.000002f || got_u > 1.000002f || got_v < -0.000002f || got_v > 1.000002f ||
          f_is_nonfinite(got_u) || f_is_nonfinite(got_v) || warp > 0.0031f) bad++;
      const float *opposite = pixels + ((SIDE - 1 - y) * SIDE + SIDE - 1 - x) * 4;
      float symmetry = fmaxf(fabsf(pixel[0] + opposite[0] - 1.0f),
                              fabsf(pixel[1] + opposite[1] - 1.0f));
      if (symmetry > max_symmetry) max_symmetry = symmetry;
      if (symmetry > 0.000002f) bad++;
      if (y == SIDE / 2 && x > 0 && pixel[0] < pixel[-4]) bad++;
      if (x == SIDE / 2 && y > 0 && pixel[1] < pixel[1 - SIDE * 4]) bad++;
      if (x == SIDE / 2 && y == SIDE / 2) {
        center_error = fmaxf(fabsf(got_u - 0.5f), fabsf(got_v - 0.5f));
        if (center_error > 0.000002f) bad++;
      }
      if ((x == 0 || x == SIDE - 1 || y == 0 || y == SIDE - 1) && warp > 0.000002f) bad++;
      samples++;
    }
  }
  // Analytic off-centre witness: u=7/8, v=1/2 gives displacement
  // (1/2)*(3/4)*(1/50)*(9/16)*(7/16) = 0.001845703125.
  // This rejects a straight copy independently of the upper distortion bound.
  float witness = (pixels[((SIDE / 2) * SIDE + 7 * (SIDE - 1) / 8) * 4] - 0.25f) * 2.0f;
  if (fabsf(witness - 0.876845703125f) > 0.000002f) bad++;
  if (glGetError() != GL_NO_ERROR) bad++;
  glUniform1i(R.u_img, 0);
  glDeleteFramebuffers(1, &target);
  glDeleteTextures(1, &texture);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)old_fbo);
  glViewport(old_view[0], old_view[1], old_view[2], old_view[3]);
  glUseProgram((GLuint)old_program);
  if (depth) glEnable(GL_DEPTH_TEST);
  if (blend) glEnable(GL_BLEND);
  if (cull) glEnable(GL_CULL_FACE);
  printf("vmscope shader samples=%d warp=%.6f center=%.6f symmetry=%.6f %s\n",
         samples, (double)max_warp, (double)center_error, (double)max_symmetry,
         bad ? "FAIL" : "ok");
  return bad;
}

static int vmscope_pass_proof(sim_ctx_t *s) {
  player_t p = *s->p;
  int old_home = g_home, old_menu = g_menu_open, old_over = g_match.over;
  int old_orbit = g_vmorb.on, old_third = g_cfg.third_person;
  GLuint pool[3] = {R.scope_fbo, R.scope_tex, R.scope_depth};
  int size = R.scope_size, width = R.width, height = R.height;
  int bad = !pool[0] || !pool[1] || !pool[2] || size < 768 || size > 1536;
  p.alive = 1; p.wp.cur = WPN_SR;
  p.wp.reload_t = p.wp.switch_t = 0; p.wp.prev_switch = 0;
  p.lean = p.prev_lean = p.land_dip = p.prev_land_dip = 0;
  memset(&p.vm, 0, sizeof p.vm);
  const char *names[] = {"open", "closed", "off_axis", "dead", "third", "free",
    "orbit", "home", "menu", "result", "missing_fbo", "missing_texture",
    "missing_depth", "ar", "open_after_stale", "closed_after_stale",
    "small", "wide", "restored", "close_again", "reopen"};
  for (unsigned i = 0; i < sizeof names / sizeof *names; i++) {
    g_home = g_menu_open = g_match.over = g_vmorb.on = g_cfg.third_person = 0;
    R.scope_fbo = pool[0]; R.scope_tex = pool[1]; R.scope_depth = pool[2];
    R.width = width; R.height = height;
    p.alive = 1; p.wp.cur = WPN_SR;
    p.wp.ads_lin = p.wp.ads_s = p.wp.prev_ads_s = 1;
    cam_override_t override = {0};
    int want = i == 0 || i == 14 || (i >= 16 && i <= 18) || i == 20;
    if (i == 1 || i == 19) p.wp.ads_lin = p.wp.ads_s = p.wp.prev_ads_s = 0;
    if (i == 2 || i == 15) p.wp.ads_s = p.wp.prev_ads_s = smooth01(0.7f);
    if (i == 3) p.alive = 0;
    if (i == 4) g_cfg.third_person = 1;
    if (i == 5) override.active = 1;
    if (i == 6) g_vmorb.on = 1;
    if (i == 7) g_home = 1;
    if (i == 8) g_menu_open = 1;
    if (i == 9) g_match.over = 1;
    if (i == 10) R.scope_fbo = 0;
    if (i == 11) R.scope_tex = 0;
    if (i == 12) R.scope_depth = 0;
    if (i == 13) p.wp.cur = WPN_AR;
    if (i == 14) { g_scope_r = 90; g_scope_cx = g_scope_cy = 100000; }
    if (i == 15) { g_scope_r = 90; g_scope_cx = width * 0.5f; g_scope_cy = height * 0.5f; }
    if (i == 16) { R.width = 320; R.height = 240; }
    if (i == 17) { R.width = 900; R.height = 360; }
    uint64_t before = g_scope_scene_serial;
    render_frame(&p, 1, 60, &override);
    uint64_t work = g_scope_scene_serial - before;
    int pass = work == (uint64_t)want && R.scope_size == size;
    if (i != 10 && R.scope_fbo != pool[0]) pass = 0;
    if (i != 11 && R.scope_tex != pool[1]) pass = 0;
    if (i != 12 && R.scope_depth != pool[2]) pass = 0;
    if (glGetError() != GL_NO_ERROR) pass = 0;
    if (!pass) bad++;
    printf("vmscope pass=%s world_submissions=%llu expected=%d pool=%d %s\n",
           names[i], (unsigned long long)work, want, size, pass ? "ok" : "FAIL");
  }
  R.width = width; R.height = height;
  R.scope_fbo = pool[0]; R.scope_tex = pool[1]; R.scope_depth = pool[2];
  // Exercise the real scope pass with an empty dynamic cast and inspect the GL
  // state its next main-world consumer receives, including the actual uniforms.
  frame_cam_t camera = rf_camera(&p, 1, NULL);
  rf_vm_frame_t vmf;
  v3 anchor;
  rf_vm_publish_anchor(&p, 1, &camera, &vmf, &anchor);
  scene_reset(); g_scene_cast_floats = 0;
  rt_bind(frame_fbo(), width, height);
  glUseProgram(R.prog_world);
  glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE); glDisable(GL_BLEND);
  glDepthMask(GL_TRUE); glDepthFunc(GL_LESS);
  gl_active_texture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, R.sm_tex);
  scope_result_t submitted = rf_scope_pass(&p, &camera);
  GLint bound = 0, view[4], program = 0, depth_func = 0;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &bound);
  glGetIntegerv(GL_VIEWPORT, view);
  glGetIntegerv(GL_CURRENT_PROGRAM, &program);
  glGetIntegerv(GL_DEPTH_FUNC, &depth_func);
  GLboolean depth_write = 0;
  glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_write);
  typedef void (APIENTRY *get_uniform_t)(GLuint, GLint, GLfloat *);
  get_uniform_t get_uniform = (get_uniform_t)eglGetProcAddress("glGetUniformfv");
  float matrix[16] = {0}, ao[4] = {0};
  if (get_uniform) {
    get_uniform(R.prog_world, R.u_vp, matrix);
    get_uniform(R.prog_world, R.u_aop, ao);
  }
  int restored = submitted.scope_on && (GLuint)bound == frame_fbo() &&
    view[0] == 0 && view[1] == 0 && view[2] == width && view[3] == height &&
    (GLuint)program == R.prog_world && glIsEnabled(GL_DEPTH_TEST) &&
    glIsEnabled(GL_CULL_FACE) && !glIsEnabled(GL_BLEND) && depth_write &&
    depth_func == GL_LESS && get_uniform && ao[0] == (float)R.ao_w &&
    ao[1] == (float)R.ao_h && ao[2] == (g_cfg.ssao ? 1.0f : 0.0f) && ao[3] == 0;
  for (int i = 0; i < 16; i++)
    if (fabsf(matrix[i] - camera.vp.m[i]) > 0.000001f) restored = 0;
  if (glGetError() != GL_NO_ERROR) restored = 0;
  if (!restored) bad++;
  printf("vmscope state viewport=%dx%d program=%u restored=%d %s\n",
         view[2], view[3], (unsigned)program, restored, restored ? "ok" : "FAIL");
  // The real optional-target failure owner must release actual incomplete objects.
  glGenTextures(1, &R.scope_tex);
  glBindTexture(GL_TEXTURE_2D, R.scope_tex);
  glGenRenderbuffers(1, &R.scope_depth);
  glBindRenderbuffer(GL_RENDERBUFFER, R.scope_depth);
  glGenFramebuffers(1, &R.scope_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, R.scope_fbo);
  GLuint failed_tex = R.scope_tex, failed_fbo = R.scope_fbo, failed_depth = R.scope_depth;
  typedef GLboolean (APIENTRY *is_object_t)(GLuint);
  is_object_t is_fbo = (is_object_t)eglGetProcAddress("glIsFramebuffer");
  is_object_t is_depth = (is_object_t)eglGetProcAddress("glIsRenderbuffer");
  GLenum incomplete = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  int released = incomplete != GL_FRAMEBUFFER_COMPLETE && !scope_target_finish(incomplete) &&
    !R.scope_tex && !R.scope_fbo && !R.scope_depth && !glIsTexture(failed_tex) &&
    is_fbo && is_depth && !is_fbo(failed_fbo) && !is_depth(failed_depth);
  if (!released) bad++;
  R.scope_fbo = pool[0]; R.scope_tex = pool[1]; R.scope_depth = pool[2];
  g_home = old_home; g_menu_open = old_menu; g_match.over = old_over;
  g_vmorb.on = old_orbit; g_cfg.third_person = old_third;
  rt_bind(R.present_fbo, width, height);
  glBindTexture(GL_TEXTURE_2D, R.font_tex);
  printf("vmscope failure_cleanup=%s %s\n", released ? "released" : "leaked", bad ? "FAIL" : "ok");
  return bad;
}

static void cmd_vmscope(sim_ctx_t *s) {
  // THE SR'S OPTICAL CONTRACT. Unlike vmsight, this measures the projected ocular
  // and the PiP aperture that is composited after geometry. The renderer's own
  // scope_pip_radius() supplies the threshold/alignment result. These eleven rows
  // are the secured physical-barrel pass at 640x360 virtual pixels. Keeping the
  // reference here makes later composition changes deliberate rather than an
  // accidental return to a viewmodel axis that differs from hitscan.
  typedef struct { float ocular, axis, cx, cy, radius, pip; } scope_before_t;
  static const scope_before_t BEFORE[11] = {
    {0.226000f, 0.154711f, 519.666000f, 226.785000f, 25.053000f,  0.000000f},
    {0.222360f, 0.150370f, 517.243000f, 226.208000f, 25.463000f,  0.000000f},
    {0.212480f, 0.138590f, 510.248000f, 224.546000f, 26.647000f,  0.000000f},
    {0.197920f, 0.121236f, 498.677000f, 221.804000f, 28.607000f,  0.000000f},
    {0.180240f, 0.100175f, 482.128000f, 217.894000f, 31.413000f,  0.000000f},
    {0.161000f, 0.077270f, 460.010000f, 212.688000f, 35.167000f,  0.000000f},
    {0.141760f, 0.054379f, 431.913000f, 206.096000f, 39.940000f,  0.000000f},
    {0.124080f, 0.033358f, 398.439000f, 198.267000f, 45.631000f,  0.000000f},
    {0.109520f, 0.016057f, 362.779000f, 189.946000f, 51.697000f,  0.038000f},
    {0.099640f, 0.004321f, 332.658000f, 182.929000f, 56.823000f, 56.823000f},
    {0.096000f, 0.000006f, 320.000000f, 179.983000f, 58.978000f, 58.978000f},
  };
  // The secured pose is at tick zero. Ambient breathing and an earlier command's
  // landing/lean state must not contaminate this reproducible optical fixture.
  uint32_t tick_save = g_tick;
  g_tick = 0;
  player_t tmp = {0};
  v3 eye = {{0, 0, 0}}, rgt = {{1, 0, 0}}, upv = {{0, 1, 0}}, fwd = {{0, 0, -1}};
  tmp.wp.cur = WPN_SR;
  tmp.wp.reload_t = tmp.wp.switch_t = 0;
  tmp.wp.prev_switch = 0.0f;
  memset(&tmp.vm, 0, sizeof tmp.vm);
  float sc = ui_scale();
  float vw = (float)R.width / sc, vh = (float)R.height / sc;
  int bad = 0, scope_tris = 0;
  float max_base_m = 0.0f, max_base_px = 0.0f;
  for (int a = 0; a <= 10; a++) {
    tmp.wp.ads_lin = (float)a / 10.0f;
    tmp.wp.ads_s = tmp.wp.prev_ads_s = smooth01(tmp.wp.ads_lin);
    vm_build(&tmp, 1.0f, eye, rgt, upv, fwd, 1.0f);
    v3 rel = v3_sub(g_gun_sight_p, eye);
    float ocular = v3_dot(rel, fwd);
    v3 axis = v3_norm(g_vm_mdl_z);
    float along = v3_dot(rel, axis);
    float axis_off = v3_len(v3_sub(rel, v3_scale(axis, along)));
    float center_off = sqrtf((g_scope_cx - vw * 0.5f) * (g_scope_cx - vw * 0.5f) +
                             (g_scope_cy - vh * 0.5f) * (g_scope_cy - vh * 0.5f));
    float pip = scope_pip_radius(tmp.wp.ads_s, vw, vh);
    if (g_gun_sr_scope_v0 >= 0 && g_gun_sr_scope_v1 > g_gun_sr_scope_v0)
      scope_tris = (g_gun_sr_scope_v1 - g_gun_sr_scope_v0) /
                   (3 * VERT_FLOATS);
    float values[] = {ocular, axis_off, center_off, g_scope_cx, g_scope_cy,
                      g_scope_r, g_gun_sight_r, pip};
    for (unsigned i = 0; i < sizeof values / sizeof values[0]; i++)
      if (f_is_nonfinite(values[i])) bad++;
    if (fabsf(g_gun_sight_r - SCOPE_OCU_R) > 1e-6f || !(g_scope_r > 0.0f)) bad++;
    if (tmp.wp.ads_s <= SCOPE_ADS_OPEN && pip > 0.001f) bad++;
    // The old values were captured at virtual 640x360. Projection offsets and radii
    // scale with virtual height, while the viewport centre itself follows vw/vh.
    float vk = vh / 360.0f;
    const scope_before_t *b = &BEFORE[a];
    float want_cx = vw * 0.5f + (b->cx - 320.0f) * vk;
    float want_cy = vh * 0.5f + (b->cy - 180.0f) * vk;
    float em = fmaxf(fabsf(ocular - b->ocular), fabsf(axis_off - b->axis));
    float ep = fabsf(g_scope_cx - want_cx);
    ep = fmaxf(ep, fabsf(g_scope_cy - want_cy));
    ep = fmaxf(ep, fabsf(g_scope_r - b->radius * vk));
    if (a == 0 || a == 10) ep = fmaxf(ep, fabsf(pip - b->pip * vk));
    if (em > max_base_m) max_base_m = em;
    if (ep > max_base_px) max_base_px = ep;
    int base_ok = em <= 0.00020f && ep <= 0.08f * vk;
    if (!base_ok) bad++;
    printf("vmscope ads=%.1f eased=%.3f ocular=%.3fmm axis=%.3fmm "
           "center=(%.3f %.3f) off=%.3f radius=%.3f pip=%.3f base=%s\n",
           (double)tmp.wp.ads_lin, (double)tmp.wp.ads_s,
           (double)(ocular * 1000.0f), (double)(axis_off * 1000.0f),
           (double)g_scope_cx, (double)g_scope_cy, (double)center_off,
           (double)g_scope_r, (double)pip, base_ok ? "ok" : "FAIL");
  }

  float opening[2049], previous = 0.0f, max_step = 0.0f;
  int dense_bad = 0;
  for (int direction = 0; direction < 2; direction++) {
    for (int n = 0; n <= 2048; n++) {
      int sample = direction ? 2048 - n : n;
      tmp.wp.ads_lin = (float)sample / 2048.0f;
      tmp.wp.ads_s = tmp.wp.prev_ads_s = smooth01(tmp.wp.ads_lin);
      vm_build_plan_t plan = vm_plan_make(&tmp, 1, eye, rgt, upv, fwd, fwd, 1);
      scope_projection_publish(&plan.scope);
      float pip = scope_pip_radius(tmp.wp.ads_s, vw, vh);
      if (f_is_nonfinite(pip) || pip < 0 || pip > g_scope_r + 0.0001f) dense_bad++;
      if (tmp.wp.ads_s <= SCOPE_ADS_OPEN && pip != 0) dense_bad++;
      if (direction) {
        if (fabsf(pip - opening[sample]) > 0.00001f || (n && pip > previous + 0.00001f)) dense_bad++;
      } else {
        opening[sample] = pip;
        if (n && pip < previous - 0.00001f) dense_bad++;
      }
      if (n) {
        float step = fabsf(pip - previous) / fmaxf(g_scope_r, 1.0f);
        if (step > max_step) max_step = step;
        if (step > 0.02f) dense_bad++;
      }
      previous = pip;
      if (!direction && sample % 8 == 0)
        printf("vmscope timeline ads=%.6f ocular=%.6f center=(%.6f %.6f) radius=%.6f pip=%.6f\n",
               (double)tmp.wp.ads_lin, (double)v3_dot(plan.scope.ocular, fwd),
               (double)g_scope_cx, (double)g_scope_cy, (double)g_scope_r, (double)pip);
    }
  }
  bad += dense_bad;
  printf("vmscope aperture samples=4098 max_step=%.6fr %s\n", (double)max_step, dense_bad ? "FAIL" : "ok");

  // The same full-ADS ocular across the real SR recoil springs. Radius may breathe
  // slightly as the rifle compresses, but the eye must stay behind the glass.
  tmp.wp.ads_lin = tmp.wp.ads_s = tmp.wp.prev_ads_s = 1.0f;
  memset(&tmp.vm, 0, sizeof tmp.vm);
  vm_build(&tmp, 1.0f, eye, rgt, upv, fwd, 1.0f);
  float calm_r = g_scope_r, min_ocular = 1e9f, max_rdelta = 0.0f;
  float max_axis = 0.0f, max_center_ratio = 0.0f, min_pip_ratio = 1e9f;
  recoil_vm_impulse(&tmp.vm, 0, 0, WPN_SR, 1.0f);
  for (int t = 0; t <= 24; t++) {
    tmp.vm.prev_kick_back = tmp.vm.kick_back;
    tmp.vm.prev_kick_lift = tmp.vm.kick_lift;
    tmp.vm.prev_kick_side = tmp.vm.kick_side;
    vm_build(&tmp, 1.0f, eye, rgt, upv, fwd, 1.0f);
    v3 rel = v3_sub(g_gun_sight_p, eye);
    float ocular = v3_dot(rel, fwd);
    v3 axis = v3_norm(g_vm_mdl_z);
    float along = v3_dot(rel, axis);
    float axis_off = v3_len(v3_sub(rel, v3_scale(axis, along)));
    float center_off = sqrtf((g_scope_cx - vw * 0.5f) * (g_scope_cx - vw * 0.5f) +
                             (g_scope_cy - vh * 0.5f) * (g_scope_cy - vh * 0.5f));
    float pip = scope_pip_radius(1.0f, vw, vh);
    float rd = calm_r > 0.0f ? fabsf(g_scope_r - calm_r) / calm_r : 1.0f;
    float cr = g_scope_r > 0.0f ? center_off / g_scope_r : 1e9f;
    float pr = g_scope_r > 0.0f ? pip / g_scope_r : 0.0f;
    if (ocular < min_ocular) min_ocular = ocular;
    if (rd > max_rdelta) max_rdelta = rd;
    if (axis_off > max_axis) max_axis = axis_off;
    if (cr > max_center_ratio) max_center_ratio = cr;
    if (pr < min_pip_ratio) min_pip_ratio = pr;
    float rvalues[] = {ocular, axis_off, center_off, g_scope_cx, g_scope_cy,
                       g_scope_r, pip, rd, cr, pr};
    for (unsigned i = 0; i < sizeof rvalues / sizeof rvalues[0]; i++)
      if (f_is_nonfinite(rvalues[i])) bad++;
    recoil_spring(&tmp.vm.kick_back, &tmp.vm.kick_back_v,
                  RECOIL_BACK_K, RECOIL_BACK_C);
    recoil_spring(&tmp.vm.kick_lift, &tmp.vm.kick_lift_v,
                  RECOIL_LIFT_K, RECOIL_LIFT_C);
    recoil_spring(&tmp.vm.kick_side, &tmp.vm.kick_side_v,
                  RECOIL_SIDE_K, RECOIL_SIDE_C);
  }
  // Recoil is intentionally new, so its motion cannot equal the old one. Its limits
  // come from the approved plan: >=78 mm ocular and <=20% radius breathing. The two
  // alignment limits are renderer contracts: the eye remains inside the ocular and
  // the PiP stays in scope_pip_radius's full-alignment plateau (off <= 0.30 radius).
  if (scope_tris <= 0 || fabsf(SCOPE_ADS_OPEN - 0.55f) > 1e-6f ||
      min_ocular < 0.078f || max_rdelta > 0.2005f ||
      max_axis > SCOPE_OCU_R + 1e-5f || max_center_ratio > 0.302f ||
      min_pip_ratio < 0.998f) bad++;
  printf("vmscope scope_tris=%d open_at=%.2f baseline_err=%.3fmm/%.3fpx "
         "recoil_min_ocular=%.1fmm recoil_axis=%.2fmm "
         "recoil_center=%.3fr recoil_pip=%.3fr recoil_radius_delta=%.1f%% %s\n",
         scope_tris, (double)SCOPE_ADS_OPEN, (double)(max_base_m * 1000.0f),
         (double)max_base_px, (double)(min_ocular * 1000.0f),
         (double)(max_axis * 1000.0f), (double)max_center_ratio,
         (double)min_pip_ratio, (double)(max_rdelta * 100.0f),
         bad ? "FAIL" : "ok");
  bad += vmscope_crop_proof();
  bad += vmscope_shader_proof();
  bad += vmscope_pass_proof(s);
  g_tick = tick_save;
  printf("vmscope proof=%d %s\n", bad, bad ? "FAIL" : "ok");
  if (bad) exit(1);
}

static void cmd_parity(sim_ctx_t *s) {
  // Every gameplay quantity that must be IDENTICAL for the player and for a bot,
  // computed by calling the shared code with each one's state.
  const player_t *p = s->p;
  bot_t *b = &g_bots[0];
  const float *mv = g_cfg.mv;
  weapon_t pw = p->wp, bw = b->wp;
  pw.cur = bw.cur = WPN_AR;
  pw.ads_s = bw.ads_s = 0.0f;
  pw.rec_p = bw.rec_p = pw.rec_y = bw.rec_y = 0.0f;
  int ppart = 0, bpart = 0;
  v3 dn = {{0, -1, 0}};
  v3 pabove = {{p->pos.x, p->pos.y + 4.0f, p->pos.z}};
  v3 babove = {{b->pos.x, b->pos.y + 4.0f, b->pos.z}};
  // hitbox parity is measured upright (no lean), each at its own yaw
  float ph = ent_trace_box(p->pos, EYE_STAND, p->yaw, 0.0f, pabove, dn, &ppart);
  float bh = ent_trace_box(b->pos, EYE_STAND, b->yaw, 0.0f, babove, dn, &bpart);
  float pc = ent_trace_box(p->pos, EYE_CROUCH, p->yaw, 0.0f, pabove, dn, &ppart);
  float bc = ent_trace_box(b->pos, EYE_CROUCH, b->yaw, 0.0f, babove, dn, &bpart);
  // Two kinds of row, DECLARED in the output. `shared` rows put the same expression
  // in both columns because the one-copy refactor removed the bot-side twin they
  // originally compared — their MISMATCH branch is unreachable, and printing "ok"
  // implied a check that cannot fail ("a proof whose output cannot move is not a
  // proof").
  struct { const char *n; double pv, bv; int shared; } row[] = {
    {"run_speed",     mv[MV_RUN],                       mv[MV_RUN], 1},
    {"crouch_speed",  mv[MV_CROUCH_SP],                 mv[MV_CROUCH_SP], 1},
    {"response_ms",   mv[MV_RESPONSE_MS],               mv[MV_RESPONSE_MS], 1},
    {"jump_speed",    mv_jump_speed(),                  mv_jump_speed(), 1},
    {"air_ctl",       mv[MV_AIR_CTL],                   mv[MV_AIR_CTL], 1},
    {"slide_boost",   mv_slide_boost(),                 mv_slide_boost(), 1},
    {"slide_gate",    mv[MV_RUN] * SLIDE_START_SPEED,   mv[MV_RUN] * SLIDE_START_SPEED, 1},
    {"fire_cool",     (int)(TICK_HZ * 60.0f / g_cfg.wp[WPN_AR][WP_RPM] + 0.5f),
                      (int)(TICK_HZ * 60.0f / g_cfg.wp[WPN_AR][WP_RPM] + 0.5f), 1},
    {"mag",           WPN_DEF[WPN_AR].mag,              WPN_DEF[WPN_AR].mag, 1},
    {"reload_ticks",  WPN_DEF[WPN_AR].reload_ticks,     WPN_DEF[WPN_AR].reload_ticks, 1},
    {"dmg",           WPN_DEF[WPN_AR].dmg,              WPN_DEF[WPN_AR].dmg, 1},
    {"cone_ground",   gun_spread(&pw, 1, 0),            gun_spread(&bw, 1, 0), 0},
    {"cone_air",      gun_spread(&pw, 0, 0),            gun_spread(&bw, 0, 0), 0},
    {"cone_slide",    gun_spread(&pw, 1, 1),            gun_spread(&bw, 1, 1), 0},
    {"recoil_up",     g_cfg.wp[WPN_AR][WP_RECOIL_UP],   g_cfg.wp[WPN_AR][WP_RECOIL_UP], 1},
    {"recenter",      g_cfg.gun[GP_RECENTER],           g_cfg.gun[GP_RECENTER], 1},
    {"eye_stand",     EYE_STAND,                        EYE_STAND, 1},
    {"eye_crouch",    EYE_CROUCH,                       EYE_CROUCH, 1},
    {"height_stand",  HEIGHT_STAND,                     HEIGHT_STAND, 1},
    {"height_crouch", HEIGHT_CROUCH,                    HEIGHT_CROUCH, 1},
    {"radius",        PLAYER_RADIUS,                    PLAYER_RADIUS, 1},
    {"hp",            PLAYER_HP,                        PLAYER_HP, 1},
    {"hitbox_stand",  4.0f - ph,                        4.0f - bh, 0},
    {"hitbox_crouch", 4.0f - pc,                        4.0f - bc, 0},
  };
  int bad = 0, live = 0;
  for (unsigned i = 0; i < sizeof row / sizeof *row; i++) {
    int eq = fabs(row[i].pv - row[i].bv) < 1e-5;
    if (!eq) bad++;
    if (!row[i].shared) live++;
    printf("parity %-14s player=%10.4f bot=%10.4f %s\n", row[i].n,
           row[i].pv, row[i].bv,
           !eq ? "MISMATCH" : row[i].shared ? "shared" : "ok");
  }
  printf("parity total rows=%u live=%d mismatches=%d\n",
         (unsigned)(sizeof row / sizeof *row), live, bad);
  if (bad) exit(1);
}

static void cmd_rag(sim_ctx_t *s) {
  // THE FLIGHT, TICK BY TICK, ACROSS THE HANDOVER. A ragdoll is the one thing in
  // this game a screenshot is worst at: peak height, air time, bounces, wall
  // rebounds and total travel are all quantities, and the handover to the corpse
  // pool happens in the middle of them — which is exactly where the two integrators
  // used to disagree.
  long n = next_n();
  int bi = (int)opt_n(0);             // which BOT INDEX, default 0
  if (bi < 0) bi = 0;
  if (bi >= MAX_BOTS) bi = MAX_BOTS - 1;
  v3 p0 = {{0, 0, 0}};
  float apex = 0.0f, travel = 0.0f, spin_max = 0.0f, slam_max = 0.0f;
  int air = 0, bounces = 0, first = 1, settle = -1;
  float pvy = 0.0f, tilt = 0.0f, yaw = 0.0f;
  v3 prev = {{0, 0, 0}};
  for (long i = 0; i < n; i++) {
    sim_advance(s);
    const bot_t *b = &g_bots[bi];
    const rag_t *r = NULL;
    v3 p = {{0, 0, 0}};
    int life = -1;
    if (!b->alive && b->active) { r = &b->rag; p = b->pos; }
    else {   // handed over: follow ITS corpse (harness_corpse_of)
      const corpse_t *cp = harness_corpse_of(bi);
      if (cp) { life = cp->life; r = &cp->rag; p = cp->pos; }
    }
    if (!r) continue;
    if (first) { p0 = p; prev = p; first = 0; }
    v3 d = v3_sub(p, prev);
    travel += sqrtf(d.x * d.x + d.z * d.z);
    prev = p;
    if (p.y > apex) apex = p.y;
    if (fabsf(r->spin) > spin_max) spin_max = fabsf(r->spin);
    if (r->slam > slam_max) slam_max = r->slam;
    if (!r->grounded) air++;
    // A bounce is the frame the ground hands velocity BACK — that is the
    // only unambiguous signature, and it is why the counter reads dv rather
    // than the grounded flag (which the bounce itself clears).
    if (pvy < -0.05f && r->vel.y > 0.05f) bounces++;
    pvy = r->vel.y;
    tilt = r->tilt;
    yaw = r->yaw;
    if (settle < 0 && r->grounded && r->spin == 0.0f &&
        r->vel.x == 0.0f && r->vel.z == 0.0f) settle = (int)i;
    printf("rag t=%ld p=(%.2f %.2f %.2f) tilt=%.3f spin=%.2f yaw=%.3f "
           "v=(%.2f %.2f %.2f) slam=%.2f gnd=%d %s\n",
           (long)s->tick, (double)p.x, (double)p.y, (double)p.z,
           (double)r->tilt, (double)r->spin, (double)r->yaw,
           (double)r->vel.x, (double)r->vel.y, (double)r->vel.z,
           (double)r->slam, r->grounded, life >= 0 ? "corpse" : "entity");
  }
  v3 dt = v3_sub(prev, p0);
  printf("rag apex=%.3f air=%d/%ld bounces=%d slam_max=%.2f spin_max=%.2f "
         "travel=%.2f net=%.2f tilt=%.3f yaw=%.3f settle=%d\n",
         (double)apex, air, n, bounces, (double)slam_max, (double)spin_max,
         (double)travel, (double)sqrtf(dt.x * dt.x + dt.z * dt.z),
         (double)tilt, (double)yaw, settle);
}

static void cmd_ragsoak(sim_ctx_t *s) {
  // `ragx` follows ONE body; a match has six corpses on the floor at once, in
  // cover, on platforms and against the perimeter, and the worst of them is the one
  // the player is looking at.
  long n = next_n();
  float wfloat = 0.0f, wsink = 0.0f, wpen = 0.0f, whang = 0.0f;
  long nin = 0, bodies = 0, samples = 0, ptick = -1;
  float wbpen = 0.0f, wrpen = 0.0f, wrsink = 0.0f;
  const char *pnam = "-", *snam = "-", *rnam = "-";
  int pgnd = 0, pset = 0;
  for (long i = 0; i < n; i++) {
    sim_advance(s);
    // e < 0 scans the corpse pool, e >= 0 dead-but-unrespawned BOTS.
    // Humans need no third range: a human hands his body to the
    // pool ON the killing tick (corpse_add with his own anim and real
    // wp.cur), so every human corpse is already covered by the e < 0
    // branch — there is no human-as-dead-entity window to scan.
    for (int e = -MAX_CORPSES; e < MAX_BOTS; e++) {
      const rag_t *r;
      ragx_t x;
      scene_reset();
      g_nprobe = 0;
      if (e < 0) {
        const corpse_t *cp = &g_corpses[-e - 1];
        if (!cp->used) continue;
        g_probe_on = 1;
        g_cam_eye = cp->pos;
        harness_draw_corpse(cp);
        r = &cp->rag;
      } else {
        const bot_t *b = &g_bots[e];
        if (b->alive || !b->active) continue;
        g_probe_on = 1;
        g_cam_eye = b->pos;
        scene_draw_bot(b, 1.0f);
        r = &b->rag;
      }
      samples++;
      probe(NULL);
      g_probe_on = 0;
      x = ragx_scan(0);
      if (r->grounded && x.clr > wfloat) wfloat = x.clr;
      if (r->grounded && r->spin == 0.0f && r->vel.x == 0.0f &&
          r->vel.z == 0.0f) {
        bodies++;
        if (x.hang > whang) whang = x.hang;
        // SETTLED bodies are the ones a player walks up to and looks at.
        if (x.bpen > wrpen) { wrpen = x.bpen; rnam = ragx_part(x.fbpen); }
        if (x.clr < wrsink) wrsink = x.clr;
      }
      if (x.clr < wsink) { wsink = x.clr; snam = ragx_part(x.fclr); }
      if (x.pen > wpen) wpen = x.pen;
      if (x.bpen > wbpen) {
        wbpen = x.bpen; pnam = ragx_part(x.fbpen); ptick = (long)s->tick;
        pgnd = r->grounded;
        // The COMPLETE settled predicate (the truncated x-only copy could
        // label a body still skidding along z — per-axis collision zeroes
        // only the axis that hit — as at rest).
        pset = r->spin == 0.0f && r->vel.x == 0.0f && r->vel.z == 0.0f;
      }
      if (x.nin) nin++;
    }
  }
  printf("ragsoak ticks=%ld samples=%ld settled=%ld float=%.3f hang=%.3f "
         "sink=%.3f<%s> pen=%.3f body_pen=%.3f<%s>@t%ld gnd=%d rest=%d "
         "in=%ld rest_pen=%.3f<%s> rest_sink=%.3f\n", n, samples, bodies,
         (double)wfloat, (double)whang, (double)wsink, snam, (double)wpen,
         (double)wbpen, pnam, ptick, pgnd, pset, nin, (double)wrpen, rnam,
         (double)wrsink);
}

static void cmd_ragx(sim_ctx_t *s) {
  // WHERE THE BODY ACTUALLY IS, measured on the geometry rather than on the
  // origin `rag` prints. `rag` can report a body at rest on y = 0 while the
  // rendered corpse sits 30 cm in the air or half inside a wall, because the
  // origin is not the mesh: the corpse solve tips, drapes, clamps and settles
  // the pose AFTER the integrator has finished with the origin. This is the
  // proof for the two complaints the integrator cannot see.
  //   float  worst clearance of the lowest rendered vertex, GROUNDED ticks
  //          only (a body in flight is legitimately clear of everything)
  //   sink   worst NEGATIVE clearance — geometry under the surface
  //   pen    deepest vertex inside a solid, `in_ticks` how many ticks had any
  long n = next_n();
  // `bi` is a BOT INDEX — see the identical note in `rag`.
  int bi = (int)opt_n(0);
  if (bi < 0) bi = 0;
  if (bi >= MAX_BOTS) bi = MAX_BOTS - 1;
  float wfloat = 0.0f, wsink = 0.0f, wpen = 0.0f, wbfloat = 0.0f, whang = 0.0f;
  long nin_ticks = 0, gticks = 0;
  for (long i = 0; i < n; i++) {
    sim_advance(s);
    const bot_t *b = &g_bots[bi];
    const corpse_t *cp = NULL;
    if (b->alive || !b->active) {
      cp = harness_corpse_of(bi);   // one staged body stays the followed one
      if (!cp) continue;
    }
    scene_reset();
    g_nprobe = 0;
    g_probe_on = 1;
    // Camera ON the body: the invariants have to hold on the mesh the player
    // sees when he walks up to the corpse, not on whatever tier the harness
    // camera happened to select.
    g_cam_eye = cp ? cp->pos : b->pos;
    const rag_t *r;
    if (cp) {
      harness_draw_corpse(cp);
      r = &cp->rag;
    } else {
      scene_draw_bot(b, 1.0f);
      r = &b->rag;
    }
    probe(NULL);
    g_probe_on = 0;
    // Under `figv`, the solved joints themselves. `span`/`hgt` say a corpse
    // is folded; only the joint table says WHERE it is folded.
    if (g_fchk_v) {
      static const char *JN[POSE_NPOS] = {
        "hipL","hipR","kneeL","kneeR","ankL","ankR","toeL","toeR",
        "shoL","shoR","elbL","elbR","wriL","wriR",
        "pelvis","chest","neck","head","gun"};
      const pose_t *QP = cp ? &cp->anim.pose : &b->anim.pose;
      pose_t Q = *QP;
      death_t dq = rag_death(r, cp ? cp->slack : b->topple,
                             cp ? cp->slack : b->topple, 1.0f);
      v3 qp = cp ? cp->pos : b->pos;
      fig_death_pose(&Q, &dq, qp, cp ? cp->yaw : b->yaw,
                     cp ? cp->wpn : WPN_AR);
      const v3 *qj = (const v3 *)&Q;
      printf("  joints");
      for (int k = 0; k < POSE_NPOS; k++)
        printf(" %s=%.2f", JN[k], (double)qj[k].y);
      printf("\n");
    }
    ragx_t x = ragx_scan(0);
    if (r->grounded) {
      gticks++;
      if (x.clr > wfloat) wfloat = x.clr;
      if (x.bclr > wbfloat) wbfloat = x.bclr;
    }
    // `hang` only means anything on a body that has STOPPED. A body still
    // toppling is standing on its boots with its chest legitimately a metre
    // in the air, and counting those ticks makes every scenario read 0.9.
    if (r->grounded && r->spin == 0.0f && r->vel.x == 0.0f &&
        r->vel.z == 0.0f && x.hang > whang) whang = x.hang;
    if (x.clr < wsink) wsink = x.clr;
    if (x.pen > wpen) wpen = x.pen;
    if (x.nin) nin_ticks++;
    v3 bp = cp ? cp->pos : b->pos;
    printf("ragx t=%ld p=(%.2f %.2f %.2f) tilt=%.3f clr=%+.3f<%s> "
           "body=%+.3f<%s> pen=%.3f<%s> in=%d hang=%.3f span=%.2f hgt=%.2f verts=%d "
           "gnd=%d %s\n",
           (long)s->tick, (double)bp.x, (double)bp.y, (double)bp.z,
           (double)r->tilt, (double)x.clr, ragx_part(x.fclr), (double)x.bclr,
           ragx_part(x.fbclr), (double)x.pen, ragx_part(x.fpen), x.nin,
           (double)x.hang, (double)x.span, (double)x.hgt, x.nv,
           r->grounded, cp ? "corpse" : "entity");
  }
  printf("ragx ticks=%ld grounded=%ld float=%.3f bodyfloat=%.3f hang=%.3f "
         "sink=%.3f pen=%.3f in_ticks=%ld\n", n, gticks, (double)wfloat,
         (double)wbfloat, (double)whang, (double)wsink, (double)wpen,
         nin_ticks);
}

static void cmd_figv(sim_ctx_t *s) {
  (void)s;
  g_fchk_v = !g_fchk_v;  // locate every offending triangle, not just count
}

static void cmd_figcheck(sim_ctx_t *s) {
  long n = next_n();
  // Optional camera DISTANCE, because the invariants have to hold on every
  // profile tier and nothing reached the far ones. `g_fig_k` is chosen from
  // how far the figure is from `g_cam_eye`, and every command that could
  // have checked the coarse tiers pinned it to the finest: figcheck put the
  // camera ON the bot, vmtrig's third-person branch does the same, and
  // vm_build hard-sets FIG_KMAX. So the two-node merged finger mass — the
  // shape that covers everything past 6 m, i.e. most of a 48 m arena — had
  // no gate at all. 0 keeps the old behaviour exactly.
  float fdist = (float)opt_n(0);
  int wpn = WPN_AR;
  {
    char *wtok = take_tok();
    if (wtok && !strcmp(wtok, "ar")) wpn = WPN_AR;
    else if (wtok && !strcmp(wtok, "sr")) wpn = WPN_SR;
    else if (wtok) g_tok_pushback = wtok;
  }

  fchk_res_t worst = {0};
  long wt = s->tick, tris = 0;
  int bad = 0, wcross = 0, pose_wpn = 1, structural_bad = 0;
  // cross has its own worst; see vmcheck's note. Near/cross are intentional contact
  // census values and are baselined by ci-proofs; the five topology classes are hard.
  float pose_delta_min = 1e9f;
  // The whole figure is now body surface: the ground blob disk it used to emit
  // first (an open fan, which had to be skipped) is gone, replaced by a real cast
  // shadow that lives in the shadow map and not in this buffer.
  for (long i = 0; i < (n > 0 ? n : 1); i++) {
    if (n > 0) sim_advance(s);
    const bot_t *subject = &g_bots[0];
    // Stage both sides of the selector locally. Merely asking the renderer for an
    // SR mesh around the bot's live AR-authored wrists is a plausible-looking but
    // false SR proof; mutating the live bot here would make this diagnostic alter
    // the simulation it is measuring.
    weapon_t draw_wp = subject->wp;
    anim_t draw_anim = subject->anim;
    draw_wp.cur = wpn;
    int idle_ok = gun_pose_state(&draw_wp, &draw_anim, subject->vel,
                                   subject->grounded, subject->sliding);
    skel_solve(&draw_anim, subject->pos, subject->yaw, subject->pitch,
               wpn, idle_ok, SKEL_POSE_ONLY);
    // A selector echo is not a pose witness. Solve the same immutable animation
    // state with the other production weapon and require the actually drawn gun,
    // wrists and weapon frame to differ. This counterfactual fails if `wpn` stops
    // reaching skel_solve even though the renderer still receives the right enum.
    int other_wpn = wpn == WPN_AR ? WPN_SR : WPN_AR;
    weapon_t other_wp = subject->wp;
    anim_t other_anim = subject->anim;
    other_wp.cur = other_wpn;
    int other_idle_ok = gun_pose_state(&other_wp, &other_anim, subject->vel,
                                         subject->grounded, subject->sliding);
    skel_solve(&other_anim, subject->pos, subject->yaw, subject->pitch,
               other_wpn, other_idle_ok, SKEL_POSE_ONLY);
    const pose_t *pa = &draw_anim.pose, *pb = &other_anim.pose;
    float pose_delta = v3_len(v3_sub(pa->gun_o, pb->gun_o))
                     + v3_len(v3_sub(pa->wri[0], pb->wri[0]))
                     + v3_len(v3_sub(pa->wri[1], pb->wri[1]))
                     + 0.25f * v3_len(v3_sub(pa->gun_f, pb->gun_f))
                     + 0.25f * v3_len(v3_sub(pa->gun_u, pb->gun_u));
    if (pose_delta < pose_delta_min) pose_delta_min = pose_delta;
    if (draw_wp.cur != wpn || other_wp.cur != other_wpn ||
        f_is_nonfinite(pose_delta) || pose_delta < 0.010f)
      pose_wpn = 0;
    scene_reset();
    // Check the mesh the player sees up close: scene_draw_figure picks its
    // profile resolution from the camera distance, and the invariants have to
    // hold on the most detailed variant.
    g_cam_eye = subject->pos;
    if (fdist > 0.0f) g_cam_eye.z += fdist;
    if (g_fchk_v) { g_nprobe = 0; g_probe_on = 1; }
    scene_draw_figure(&draw_anim, subject->pos, subject->yaw, NULL,
                      1.0f, 0.0f, (v3){{1, 1, 1}}, wpn,
                      gun_anim(&draw_wp));
    g_probe_on = 0;
    fchk_res_t r = fchk_scene();
    tris = r.tris;
    if (r.zfight && g_fchk_v) {
      gunanim_t action = gun_anim(&draw_wp);
      printf("figcheck topology tick=%ld hand=%.6f bolt=%.6f bhand=%.6f slide=%.6f\n",
             s->tick, (double)action.hand, (double)action.bolt,
             (double)action.bhand, (double)draw_anim.slide_s);
      for (int q = 0; q < g_nprobe; q++)
        if (g_probe[q].name && !strcmp(g_probe[q].name, "grip_digits"))
          printf("figcheck digit_range q=%d triangles=%d/%d\n", q,
                 g_probe[q].v0 / (VERT_FLOATS * 3), g_probe[q].v1 / (VERT_FLOATS * 3));
    }
    structural_bad |= r.open || r.flip || r.dup || r.zfight || r.degen;
    int sc = 100 * (r.open + r.flip + r.dup + r.degen) + 10 * r.zfight + r.near;
    if (r.cross > wcross) wcross = r.cross;   // its own worst; see vmcheck
    if (sc > bad) { bad = sc; worst = r; wt = s->tick; }
  }
  printf("figcheck ticks=%ld dist=%.0f wpn=%s pose_wpn=%d pose_delta=%.1fmm "
         "k=%d tris=%d "
         "worst_t=%ld open=%d flip=%d dup=%d zfight=%d near=%d cross=%d degen=%d\n", n,
         (double)fdist, WPN_DEF[wpn].prefix, pose_wpn,
         (double)(pose_delta_min * 1000.0f), g_fig_k, (int)tris, wt,
         worst.open, worst.flip, worst.dup, worst.zfight, worst.near, wcross,
         worst.degen);
  if (!pose_wpn || structural_bad) exit(1);
}

static void cmd_kine(sim_ctx_t *s) {
  // Per-tick kinematics of bot 0's SOLVED pose: what the eye actually sees.
  // `skel` prints the foot controller's plant anchors; this prints where the
  // rendered boot soles and gloves really are, which is the only way to hold
  // a contact claim ("the heel is on the ground at this tick") to a number.
  long n = next_n();
  while (n-- > 0) {
    sim_advance(s);
    const bot_t *b = &g_bots[0];
    const anim_t *A = &b->anim;
    const pose_t *P = &A->pose;
    printf("kine t=%ld pos=(%.3f %.3f %.3f) air=%.2f cr=%.2f kn=%.2f sl=%.2f "
           "hd=%.2f ly=%.4f "
           "pel=(%.3f %.3f %.3f) head=%.4f",
           s->tick, (double)b->pos.x, (double)b->pos.y, (double)b->pos.z,
           (double)A->air_s, (double)A->crouch_s, (double)A->kneel_s,
           (double)A->slide_s, (double)A->hdown,
           (double)A->leg_yaw, (double)P->pelvis.x, (double)P->pelvis.y,
           (double)P->pelvis.z, (double)P->head.y);
    for (int i = 0; i < 2; i++) {
      v3 ank = P->ankle[i], toe = P->toe[i];
      v3 fF = v3_sub(toe, ank);
      fF = v3_dot(fF, fF) > 1e-8f ? v3_norm(fF) : (v3){{0, 0, 1}};
      // Mirror the renderer's boot chain exactly (see scene_draw_figure):
      // av = ankle - 0.028, every ring's centre sits at (rx - H) along the
      // spine so centre - rx is ONE plane per piece, and the lowest of those
      // is the OUTSOLE's at av - SOLE_H. If this drifts from the renderer
      // the proof silently validates a boot nobody can see.
      v3 heel = v3_add(v3_add(ank, (v3){{0, -0.028f, 0}}), v3_scale(fF, -0.064f));
      // The renderer lifts the boot by its own worst dip (see `sole_dip` in
      // scene_draw_figure), so the lowest sole vertex is ON the plane through av at
      // every foot pitch by construction.
      float sole = ank.y - b->pos.y - ANIM_ANKLE_Y;
      printf(" %c=(%.4f %.4f %.4f)%c K=(%.4f %.4f %.4f) "
             "sp=%.2f toe=%.4f heel=%.4f sole=%.4f",
             i ? 'R' : 'L', (double)ank.x, (double)ank.y, (double)ank.z,
             A->foot[i].t >= 1.0f ? '*' : '~', (double)P->knee[i].x,
             (double)P->knee[i].y, (double)P->knee[i].z,
             (double)A->stance[i],
             (double)toe.y, (double)heel.y, (double)sole);
    }
    for (int i = 0; i < 2; i++)
      printf(" %cs=(%.4f %.4f %.4f) %ce=(%.4f %.4f %.4f) %cw=(%.4f %.4f %.4f)",
             i ? 'R' : 'L', (double)P->sho[i].x, (double)P->sho[i].y,
             (double)P->sho[i].z, i ? 'R' : 'L', (double)P->elb[i].x,
             (double)P->elb[i].y, (double)P->elb[i].z, i ? 'R' : 'L',
             (double)P->wri[i].x, (double)P->wri[i].y, (double)P->wri[i].z);
    {
      v3 body_right = v3_norm(P->right);
      v3 body_fwd = {{body_right.z, 0.0f, -body_right.x}};
      float leg_clear = slide_leg_clearance(P, P->knee[0], P->knee[1]);
      float left_step = v3_len(v3_sub(v3_sub(P->elb[0], P->sho[0]),
                                      v3_sub(A->prev_pose.elb[0],
                                             A->prev_pose.sho[0])));
      float right_step = v3_len(v3_sub(v3_sub(P->elb[1], P->sho[1]),
                                       v3_sub(A->prev_pose.elb[1],
                                              A->prev_pose.sho[1])));
      printf(" knee_sep=%.3f leg_clear=%.3f elbow_step=%.4f/%.4f "
             "arm_clear=%.3f/%.3f",
             (double)v3_len(v3_sub(P->knee[1], P->knee[0])),
             (double)leg_clear, (double)left_step, (double)right_step,
             (double)arm_torso_segment_clearance(P->sho[0], P->elb[0],
                                                  P->wri[0], P->chest,
                                                  body_right, body_fwd),
             (double)arm_torso_segment_clearance(P->sho[1], P->elb[1],
                                                  P->wri[1], P->chest,
                                                  body_right, body_fwd));
    }
    printf("\n");
  }
}

static void cmd_slidecheck(sim_ctx_t *s) {
  (void)s;
  static const float DIR[8][2] = {
    {0, -1}, {0.70710678f, -0.70710678f}, {1, 0},
    {0.70710678f, 0.70710678f}, {0, 1},
    {-0.70710678f, 0.70710678f}, {-1, 0},
    {-0.70710678f, -0.70710678f},
  };
  float min_clear = 9.0f, min_side = 9.0f, max_step = 0.0f, bone_err = 0.0f;
  float exit_step = 0.0f, exit_side = 0.0f;
  float max_pole = 0.0f, exit_pole = 0.0f;
  float steady_step = 0.0f, steady_pole = 0.0f;
  float knee_contact = 9.0f, back_knee = 9.0f;
  int clear_dir = -1, clear_tick = -1, step_dir = -1, step_tick = -1;
  int step_leg = -1, pole_dir = -1, pole_tick = -1, pole_leg = -1;
  int exit_step_dir = -1, exit_step_tick = -1;
  int exit_step_leg = -1, exit_pole_dir = -1, exit_pole_tick = -1;
  int exit_pole_leg = -1;
  int contact_dir = -1, contact_tick = -1, back_dir = -1, back_tick = -1;
  int handoff_min = 999, handoff_max = -1, swing_lag_max = -1;
  int finite = 1;
  for (int d = 0; d < 8; d++) {
    anim_t a;
    v3 pos = {{0.0f, 5.0f, 0.0f}};
    v3 vel = {{DIR[d][0] * 7.0f, 0.0f, DIR[d][1] * 7.0f}};
    anim_reset(&a, pos, 0.0f, WPN_AR);
    for (int tick = 0; tick < 60; tick++) {
      pos = v3_add(pos, v3_scale(vel, TICK_DT));
      anim_tick(&a, pos, vel, 0.0f, 0.0f, 1, 1, 0.0f, 1, 0.0f,
                0.0f, 1, 1, WPN_AR, GUN_POSE_BLOCKED);
      if (tick < 6) continue;
      const pose_t *P = &a.pose;
      float clear = slide_leg_clearance(P, P->knee[0], P->knee[1]);
      v3 right = {{cosf(a.leg_yaw), 0.0f, sinf(a.leg_yaw)}};
      float side = v3_dot(v3_sub(P->knee[1], P->knee[0]), right);
      if (clear < min_clear) { min_clear = clear; clear_dir = d; clear_tick = tick; }
      min_side = fminf(min_side, side);
      for (int i = 0; i < 2; i++) {
        v3 bend = v3_sub(P->knee[i], P->hip[i]);
        v3 old_bend = v3_sub(a.prev_pose.knee[i], a.prev_pose.hip[i]);
        float step = v3_len(v3_sub(bend, old_bend));
        if (step > max_step) {
          max_step = step; step_dir = d; step_tick = tick; step_leg = i;
        }
        if (tick >= 20) steady_step = fmaxf(steady_step, step);
        v3 axis = v3_norm(v3_sub(P->ankle[i], P->hip[i]));
        v3 p0 = v3_sub(bend, v3_scale(axis, v3_dot(bend, axis)));
        v3 p1 = v3_sub(old_bend, v3_scale(axis, v3_dot(old_bend, axis)));
        if (v3_dot(p0, p0) > 1e-7f && v3_dot(p1, p1) > 1e-7f) {
          float turn = acosf(f_clamp(v3_dot(v3_norm(p0), v3_norm(p1)),
                                     -1.0f, 1.0f));
          if (turn > max_pole) {
            max_pole = turn; pole_dir = d; pole_tick = tick; pole_leg = i;
          }
          if (tick >= 20) steady_pole = fmaxf(steady_pole, turn);
        }
        bone_err = fmaxf(bone_err,
            fabsf(v3_len(v3_sub(P->knee[i], P->hip[i])) - ANIM_THIGH));
        bone_err = fmaxf(bone_err,
            fabsf(v3_len(v3_sub(P->ankle[i], P->knee[i])) - ANIM_SHIN));
        finite &= !f_is_nonfinite(P->knee[i].x) &&
                  !f_is_nonfinite(P->knee[i].y) &&
                  !f_is_nonfinite(P->knee[i].z);
      }
      if (tick >= 24) {
        int folded = a.slide_ld > 0.5f ? 0 : 1;
        float ky = P->knee[folded].y - pos.y;
        if (a.slide_bk < 0.18f && ky < knee_contact) {
          knee_contact = ky; contact_dir = d; contact_tick = tick;
        }
        if (a.slide_bk > 0.82f && ky < back_knee) {
          back_knee = ky; back_dir = d; back_tick = tick;
        }
      }
    }
    // Release the raw slide while velocity remains high. This is the actual failure
    // window: pelvis and feet are still owned by the eased get-up, but the special
    // collision poles are returning control to ordinary leg IK.
    int dir_handoff = -1, dir_swing = -1;
    for (int tick = 0; tick < 90; tick++) {
      pos = v3_add(pos, v3_scale(vel, TICK_DT));
      anim_tick(&a, pos, vel, 0.0f, 0.0f, 1, 0, 0.0f, 0, 0.0f,
                0.0f, 1, 1, WPN_AR, GUN_POSE_BLOCKED);
      const pose_t *P = &a.pose;
      if (!a.was_slide_pose && dir_handoff < 0) dir_handoff = tick;
      if (dir_handoff >= 0 && dir_swing < 0 &&
          (a.foot[0].t < 1.0f || a.foot[1].t < 1.0f))
        dir_swing = tick;
      v3 right = {{cosf(a.leg_yaw), 0.0f, sinf(a.leg_yaw)}};
      if (a.was_slide_pose && a.slide_s < 0.40f)
        exit_side = fmaxf(exit_side,
                          v3_dot(v3_sub(P->knee[1], P->knee[0]), right));
      for (int i = 0; i < 2; i++) {
        v3 bend = v3_sub(P->knee[i], P->hip[i]);
        v3 old_bend = v3_sub(a.prev_pose.knee[i], a.prev_pose.hip[i]);
        float step = v3_len(v3_sub(bend, old_bend));
        if (step > exit_step) {
          exit_step = step;
          exit_step_dir = d;
          exit_step_tick = tick;
          exit_step_leg = i;
        }
        v3 axis = v3_norm(v3_sub(P->ankle[i], P->hip[i]));
        v3 p0 = v3_sub(bend, v3_scale(axis, v3_dot(bend, axis)));
        v3 p1 = v3_sub(old_bend, v3_scale(axis, v3_dot(old_bend, axis)));
        if (v3_dot(p0, p0) > 1e-7f && v3_dot(p1, p1) > 1e-7f) {
          float turn = acosf(f_clamp(v3_dot(v3_norm(p0), v3_norm(p1)),
                                     -1.0f, 1.0f));
          if (turn > exit_pole) {
            exit_pole = turn; exit_pole_dir = d; exit_pole_tick = tick;
            exit_pole_leg = i;
          }
        }
        bone_err = fmaxf(bone_err,
            fabsf(v3_len(v3_sub(P->knee[i], P->hip[i])) - ANIM_THIGH));
        bone_err = fmaxf(bone_err,
            fabsf(v3_len(v3_sub(P->ankle[i], P->knee[i])) - ANIM_SHIN));
        finite &= !f_is_nonfinite(P->knee[i].x) &&
                  !f_is_nonfinite(P->knee[i].y) &&
                  !f_is_nonfinite(P->knee[i].z);
      }
    }
    if (dir_handoff >= 0) {
      handoff_min = handoff_min < dir_handoff ? handoff_min : dir_handoff;
      handoff_max = handoff_max > dir_handoff ? handoff_max : dir_handoff;
    }
    if (dir_handoff >= 0 && dir_swing >= dir_handoff)
      swing_lag_max = swing_lag_max > dir_swing - dir_handoff
                    ? swing_lag_max : dir_swing - dir_handoff;
  }
  int ok = finite && min_clear >= 0.120f && min_side >= 0.090f &&
           max_step <= 0.090f && steady_step <= 0.035f &&
           max_pole <= 15.0f * DEG2RAD && steady_pole <= 7.0f * DEG2RAD &&
           // The recovered running plant is deliberately inside this window now.
           // Keep its IK pole bounded just above the measured ordinary-gait peak;
           // the old 10.0 degree cap excluded it by 0.04 degrees.
           exit_step <= 0.080f && exit_pole <= 10.25f * DEG2RAD &&
           exit_side <= 0.520f && knee_contact >= 0.044f &&
           knee_contact <= 0.090f &&
           back_knee >= 0.180f && bone_err <= 0.007f &&
           handoff_min >= 12 && handoff_max <= 36 && swing_lag_max <= 2 &&
           swing_lag_max >= 0;
  printf("slidecheck dirs=8 clear=%.1fmm@%d/%d side=%.1fmm "
         "step=%.1fmm@%d/%d/%d pole=%.2fdeg@%d/%d/%d "
         "steady=%.1fmm/%.2fdeg contact=%.1f@%d/%d/%.1f@%d/%dmm "
         "exit_step=%.1fmm@%d/%d/%d exit_pole=%.2fdeg@%d/%d/%d "
         "exit_side=%.1fmm handoff=%d..%d swing_lag=%d "
         "bone=%.3fmm finite=%d %s\n",
         (double)(min_clear * 1000.0f), clear_dir, clear_tick,
         (double)(min_side * 1000.0f), (double)(max_step * 1000.0f),
         step_dir, step_tick, step_leg, (double)(max_pole / DEG2RAD),
         pole_dir, pole_tick, pole_leg, (double)(steady_step * 1000.0f),
         (double)(steady_pole / DEG2RAD), (double)(knee_contact * 1000.0f),
         contact_dir, contact_tick, (double)(back_knee * 1000.0f),
         back_dir, back_tick, (double)(exit_step * 1000.0f),
         exit_step_dir, exit_step_tick, exit_step_leg,
         (double)(exit_pole / DEG2RAD), exit_pole_dir, exit_pole_tick,
         exit_pole_leg,
         (double)(exit_side * 1000.0f), handoff_min, handoff_max, swing_lag_max,
         (double)(bone_err * 1000.0f),
         finite, ok ? "ok" : "FAIL");
  if (!ok) exit(1);
}

static void armcheck_sample(const anim_t *a, float step[2], float clear[2]) {
  const pose_t *P = &a->pose;
  v3 body_right = v3_norm(P->right);
  v3 body_fwd = {{body_right.z, 0.0f, -body_right.x}};
  for (int i = 0; i < 2; i++) {
    v3 bend = v3_sub(P->elb[i], P->sho[i]);
    v3 old_bend = v3_sub(a->prev_pose.elb[i], a->prev_pose.sho[i]);
    step[i] = fmaxf(step[i], v3_len(v3_sub(bend, old_bend)));
    clear[i] = fminf(clear[i],
        arm_torso_segment_clearance(P->sho[i], P->elb[i], P->wri[i],
                                    P->chest, body_right, body_fwd));
  }
}

static void cmd_armcheck(sim_ctx_t *s) {
  (void)s;
  float worst_step = 0.0f, worst_trigger = 0.0f, worst_clear = 9.0f;
  float worst_gun = 0.0f, worst_bore = 0.0f;
  int ok = 1;
  for (int wpn = WPN_AR; wpn <= WPN_SR; wpn++) {
    float start_step[2] = {0, 0}, run_step[2] = {0, 0};
    float aim_step[2] = {0, 0}, ads_step[2] = {0, 0};
    float start_clear[2] = {9, 9}, run_clear[2] = {9, 9};
    float aim_clear[2] = {9, 9}, ads_clear[2] = {9, 9};
    float gun_step = 0.0f, bore_step = 0.0f;
    anim_t a;
    v3 pos = {{0, 5, 0}}, vel = {{0, 0, 0}};
    anim_reset(&a, pos, 0.0f, wpn);
    // Settle the sniper into its real patrol carry, then start locomotion. This is the
    // transition that the old AR-only proof skipped and the reported SR elbow twitch
    // lives in.
    for (int tick = 0; tick < 120; tick++)
      anim_tick(&a, pos, vel, 0.0f, 0.0f, 1, 0, 0.0f, 0, 0.0f,
                0.0f, 0, 1, wpn, GUN_POSE_IDLE);
    vel = (v3){{0, 0, -5.5f}};
    for (int tick = 0; tick < 300; tick++) {
      pos = v3_add(pos, v3_scale(vel, TICK_DT));
      anim_tick(&a, pos, vel, 0.0f, 0.0f, 1, 0, 0.0f, 0, 0.0f,
                0.0f, 0, 1, wpn, GUN_POSE_LOCOMOTION);
      if (tick < 40) armcheck_sample(&a, start_step, start_clear);
      else armcheck_sample(&a, run_step, run_clear);
    }

    for (int ads = 0; ads <= 1; ads++) {
      float *step = ads ? ads_step : aim_step;
      float *clear = ads ? ads_clear : aim_clear;
      pos = (v3){{0, 5, 0}};
      vel = (v3){{0, 0, 0}};
      anim_reset(&a, pos, 0.0f, wpn);
      // Down to -PITCH_MAX, hold, then the same slow rate up through zero to
      // +PITCH_MAX and hold — the up half exists because the up-aim elbow hints
      // (see aim_up in skel_solve) were added after the floor-only sweep shipped.
      for (int tick = 0; tick < 740; tick++) {
        float pitch = tick < 120 ? 0.0f
                    : tick < 300 ? -PITCH_MAX * (tick - 119) / 180.0f
                    : tick < 340 ? -PITCH_MAX
                    : tick < 700 ? -PITCH_MAX + PITCH_MAX * (tick - 339) / 180.0f
                                 : PITCH_MAX;
        anim_tick(&a, pos, vel, 0.0f, pitch, 1, 0, 0.0f, 0, (float)ads,
                  0.0f, 1, 1, wpn, GUN_POSE_BLOCKED);
        if (tick >= 120) {
          armcheck_sample(&a, step, clear);
          gun_step = fmaxf(gun_step,
                           v3_len(v3_sub(a.pose.gun_o, a.prev_pose.gun_o)));
          bore_step = fmaxf(bore_step,
              acosf(f_clamp(v3_dot(a.pose.gun_f, a.prev_pose.gun_f),
                             -1.0f, 1.0f)));
        }
      }
    }
    float local_step = 0.0f, local_clear = 9.0f;
    for (int i = 0; i < 2; i++) {
      local_step = fmaxf(local_step, fmaxf(fmaxf(start_step[i], run_step[i]),
                                           fmaxf(aim_step[i], ads_step[i])));
      local_clear = fminf(local_clear, fminf(fminf(start_clear[i], run_clear[i]),
                                             fminf(aim_clear[i], ads_clear[i])));
    }
    worst_step = fmaxf(worst_step, local_step);
    worst_trigger = fmaxf(worst_trigger,
        fmaxf(fmaxf(start_step[1], run_step[1]),
              fmaxf(aim_step[1], ads_step[1])));
    worst_clear = fminf(worst_clear, local_clear);
    worst_gun = fmaxf(worst_gun, gun_step);
    worst_bore = fmaxf(worst_bore, bore_step);
    ok &= local_step <= 0.035f && local_clear >= 0.020f &&
          gun_step <= 0.012f && bore_step <= 0.70f * DEG2RAD;
    printf("armcheck %s start=%.1f/%.1fmm run=%.1f/%.1fmm "
           "aim=%.1f/%.1fmm ads=%.1f/%.1fmm gun=%.1fmm/%.3fdeg "
           "clear=%.3f %s\n",
           wpn == WPN_AR ? "ar" : "sr",
           (double)(start_step[0] * 1000.0f), (double)(start_step[1] * 1000.0f),
           (double)(run_step[0] * 1000.0f), (double)(run_step[1] * 1000.0f),
           (double)(aim_step[0] * 1000.0f), (double)(aim_step[1] * 1000.0f),
           (double)(ads_step[0] * 1000.0f), (double)(ads_step[1] * 1000.0f),
           (double)(gun_step * 1000.0f), (double)(bore_step / DEG2RAD),
           (double)local_clear, local_step <= 0.035f && local_clear >= 0.020f &&
                                gun_step <= 0.012f && bore_step <= 0.70f * DEG2RAD
                                  ? "ok" : "FAIL");
  }
  // A real model muzzle, not an eye-reach proxy, must stay above the support plane
  // at every downward aim through standing/crouching and low-ready/hip/ADS.
  float floor_min = 9.0f, floor_fp_min = 9.0f;
  int floor_cases = 0;
  int floor_solids_saved = g_num_solids;
  g_num_solids = 0;
  for (int plane = 0; plane < 2; plane++) {
    for (int wpn = WPN_AR; wpn <= WPN_SR; wpn++) {
      for (int crouch = 0; crouch <= 1; crouch++) {
        for (int mode = 0; mode < 3; mode++) {
          v3 floor_pos = {{0.0f, plane ? 0.0f : 5.0f, 0.0f}};
          v3 zero = {{0, 0, 0}};
          anim_t floor_anim;
          anim_reset(&floor_anim, floor_pos, 0, wpn);
          for (int tick = 0; tick < 280; tick++) {
            float pitch = tick < 100 ? 0.0f
                          : -PITCH_MAX * (float)(tick - 99) / 180.0f;
            float ads = mode == 2 ? 1.0f : 0.0f;
            anim_tick(&floor_anim, floor_pos, zero, 0, pitch, 1, crouch,
                      0, 0, ads, 0, mode > 0, 1, wpn,
                      mode > 0 ? GUN_POSE_BLOCKED : GUN_POSE_IDLE);
            if (tick < 100) continue;
            v3 muzzle = pose_muzzle(&floor_anim.pose, wpn);
            floor_min = fminf(floor_min, muzzle.y - floor_pos.y);
            player_t floor_player = {0};
            floor_player.pos = floor_player.prev_pos = floor_pos;
            floor_player.eye = floor_player.prev_eye = crouch ? EYE_CROUCH : EYE_STAND;
            floor_player.grounded = floor_player.alive = 1;
            floor_player.wp.cur = wpn;
            floor_player.wp.ads_s = floor_player.wp.prev_ads_s = ads;
            v3 eye = v3_add(floor_pos, (v3){{0, floor_player.eye, 0}});
            v3 forward = {{0, sinf(pitch), -cosf(pitch)}}, right = {{1, 0, 0}};
            v3 up = v3_norm(v3_cross(right, forward));
            vm_build_plan_t plan = vm_plan_make(&floor_player, 1, eye, right, up,
                                                 forward, forward, mode > 0);
            floor_fp_min = fminf(floor_fp_min, plan.muzzle.y - floor_pos.y);
            floor_cases++;
          }
        }
      }
    }
  }
  g_num_solids = floor_solids_saved;
  int floor_ok = !f_is_nonfinite(floor_min) && !f_is_nonfinite(floor_fp_min) &&
                 floor_min >= 0.0348f && floor_fp_min >= 0.0348f;
  ok &= floor_ok;
  printf("armcheck floor cases=%d body=%.3fmm fp=%.3fmm %s\n", floor_cases,
         (double)(floor_min * 1000), (double)(floor_fp_min * 1000),
         floor_ok ? "ok" : "FAIL");
  ok &= worst_trigger <= 0.015f;
  printf("armcheck weapons=2 max_step=%.1fmm trigger=%.1fmm "
         "gun=%.1fmm/%.3fdeg min_clear=%.3f %s\n",
         (double)(worst_step * 1000.0f), (double)(worst_trigger * 1000.0f),
         (double)(worst_gun * 1000.0f), (double)(worst_bore / DEG2RAD),
         (double)worst_clear,
         ok ? "ok" : "FAIL");
  if (!ok) exit(1);
}

static void cmd_skel(sim_ctx_t *s) {
  long n = next_n();
  while (n-- > 0) {
    sim_advance(s);
    const bot_t *b = &g_bots[0];
    const anim_t *A = &b->anim;
    printf("skel t=%ld spd=%.2f phase=%.2f air=%.2f crouch=%.2f slide=%.2f "
           "lean=%.2f yaw=%.2f leg=%.2f L=(%.3f %.3f %.3f)%s R=(%.3f %.3f %.3f)%s "
           "pelvis=(%.3f %.3f %.3f)\n",
           s->tick, (double)A->spd, (double)A->phase, (double)A->air_s,
           (double)A->crouch_s, (double)A->slide_s, (double)A->lean_s,
           (double)b->yaw, (double)A->leg_yaw,
           (double)A->foot[0].pos.x, (double)A->foot[0].pos.y,
           (double)A->foot[0].pos.z, A->foot[0].t >= 1.0f ? "*" : "~",
           (double)A->foot[1].pos.x, (double)A->foot[1].pos.y,
           (double)A->foot[1].pos.z, A->foot[1].t >= 1.0f ? "*" : "~",
           (double)A->pose.pelvis.x, (double)A->pose.pelvis.y,
           (double)A->pose.pelvis.z);
  }
}

static void cmd_vmhand(sim_ctx_t *s) {
  // THE FOUR NUMBERS THAT DECIDE WHETHER A WRIST LOOKS KINKED. A render cannot
  // answer how far a wrist bends — foreshortening hides it, and three rounds of
  // this were judged from screenshots and kept coming back. `bend` is the angle
  // between the palm's axis and the forearm's (a firing grip runs 20-30 degrees; 0
  // is a splint, 45+ is the original kink), `bore` is how the forearm meets the
  // weapon's axis (a real support arm sits 40-55 degrees off it, 90 is the
  // perpendicular clamp), `palm` is the wrist-to-knuckle chord against ANSUR's 116
  // mm p50, and `off` is how far the wrist stands off the grip SURFACE. THE RELOAD
  // IS COVERED TOO, and it was the hole in this proof.
  player_t tmp = *s->p;
  v3 eye = {{0, 0, 0}}, rgt = {{1, 0, 0}}, upv = {{0, 1, 0}}, fwd = {{0, 0, -1}};
  static const float RSTATION[5] = {0.15f, 0.35f, 0.55f, 0.75f, 0.90f};
  static const int RWHICH[5] = {
    VMTRIG_REC_BACK, VMTRIG_REC_LIFT, VMTRIG_REC_SIDE_POS,
    VMTRIG_REC_SIDE_NEG, VMTRIG_REC_OVERSHOOT,
  };
  static const char *const RNAME[5] = {
    "back", "lift", "side+", "side-", "over",
  };
  float max_fire_bend = 0.0f, min_support_bore = 1e9f, max_support_bore = 0.0f;
  int rows = 0, live_rows = 0, clamp_rows = 0, fail = 0;
  for (int wi = 0; wi < WPN_COUNT; wi++) {
    tmp.wp.cur = wi;
    // Hip/ADS, five reload stations, then the five real recoil landmarks at both
    // Hip and ADS. Recoil amplitudes come from the same production sampler vmtrig
    // uses; this matrix contains no kick literals.
    for (int a = 0; a < 17; a++) {
      char tag[20];
      int recoil = a >= 7;
      int recoil_ads = a >= 12;
      // Every row in this matrix is a live held pose. Reload deliberately moves the
      // support hand, but it is not the SR bolt exception from the descriptor contract;
      // both wrist/bore limits therefore remain hard throughout the five stations.
      tmp.wp.ads_lin = (a == 1 || recoil_ads) ? 1.0f : 0.0f;
      tmp.wp.ads_s = tmp.wp.prev_ads_s = tmp.wp.ads_lin;
      tmp.wp.switch_t = 0; tmp.wp.prev_switch = 0.0f;
      tmp.vm.kick_back = tmp.vm.prev_kick_back = 0.0f;
      tmp.vm.kick_lift = tmp.vm.prev_kick_lift = 0.0f;
      tmp.vm.kick_side = tmp.vm.prev_kick_side = 0.0f;
      if (a < 2) {
        tmp.wp.reload_t = 0;
        snprintf(tag, sizeof tag, "%s", a ? "ads" : "hip");
      } else if (!recoil) {
        float pr = RSTATION[a - 2];
        tmp.wp.reload_t = (int)((1.0f - pr) * WPN_DEF[wi].reload_ticks + 0.5f);
        snprintf(tag, sizeof tag, "rl%02d", (int)(pr * 100.0f));
      } else {
        int ri = (a - 7) % 5;
        tmp.wp.reload_t = 0;
        vmtrig_recoil_state_t rs = vmtrig_recoil_sample(
            0, wi, tmp.wp.ads_s, RWHICH[ri]);
        tmp.vm.kick_back = tmp.vm.prev_kick_back = rs.back;
        tmp.vm.kick_lift = tmp.vm.prev_kick_lift = rs.lift;
        tmp.vm.kick_side = tmp.vm.prev_kick_side = rs.side;
        snprintf(tag, sizeof tag, "%s_%s", recoil_ads ? "ads" : "hip", RNAME[ri]);
      }
      vm_build(&tmp, 1.0f, eye, rgt, upv, fwd, 1.0f);
      unsigned clamp = g_fig_clamped;
      if (clamp) clamp_rows += 2;
      for (int h = 0; h < 2; h++) {
        float bend = g_vm_hdbg[h].bend_deg, bore = g_vm_hdbg[h].bore_deg;
        int finite = !f_is_nonfinite(bend) && !f_is_nonfinite(bore) &&
                     !f_is_nonfinite(g_vm_hdbg[h].palm_mm) &&
                     !f_is_nonfinite(g_vm_hdbg[h].off_mm);
        int row_fail = !finite || clamp != 0;
        if (!h) {
          row_fail |= !(bend >= 0.0f && bend < 45.0f);
          if (bend > max_fire_bend) max_fire_bend = bend;
        }
        if (h) {
          row_fail |= !(bore >= 35.0f && bore <= 65.0f);
          if (bore < min_support_bore) min_support_bore = bore;
          if (bore > max_support_bore) max_support_bore = bore;
        }
        printf("vmhand %s %-4s %-7s bend=%5.1f deg  bore=%5.1f deg  "
               "palm=%5.1f mm  off=%5.1f mm clamp=%u %s\n", WPN_DEF[wi].prefix,
               tag, h ? "support" : "trigger",
               (double)bend, (double)bore, (double)g_vm_hdbg[h].palm_mm,
               (double)g_vm_hdbg[h].off_mm, clamp, row_fail ? "FAIL" : "ok");
        fail |= row_fail;
        rows++;
        live_rows++;
      }
    }
  }
  printf("vmhand summary rows=%d live=%d firing_bend_max=%.1fdeg "
         "support_bore=%.1f..%.1fdeg clamp_rows=%d %s\n",
         rows, live_rows, (double)max_fire_bend, (double)min_support_bore,
         (double)max_support_bore, clamp_rows, fail ? "FAIL" : "ok");
  if (fail) exit(1);
}

static void cmd_vmorbit(sim_ctx_t *s) {
  (void)s;
  // Turntable on the VIEWMODEL's hands: the pose is the real one (springs, ADS
  // blend, reload dip), only the camera moves.
  char *a = next_tok();
  if (!strcmp(a, "off")) { g_vmorb.on = 0; return; }
  g_vmorb.on = 1;
  g_vmorb.yaw = parse_f(a) * DEG2RAD;
  g_vmorb.pit = next_f() * DEG2RAD;
  g_vmorb.dist = next_f();
  // Indexes g_vm_hand_p[2] directly, so a negative value read out of bounds
  // (ASan: global-buffer-overflow) and anything above 2 quietly meant "both".
  g_vmorb.fov = 30.0f;        // the harness turntable keeps its wide lens
  g_vmorb.piv_on = 0;
  g_vmorb.rise = 0.0f;
  g_vmorb.comp = 0.0f;        // ...and its centred subject
  int wsel = (int)next_n();   // 0 trigger hand, 1 support, 2 both, 3 trigger
  if (wsel < 0 || wsel > 3) {
    fprintf(stderr, "script: vmorbit WHICH must be 0..3\n");
    exit(1);
  }
  g_vmorb.which = wsel;
}

static void cmd_vmbore(sim_ctx_t *s) {
  (void)s;
  // THE SAME TURNTABLE, PIVOTED ON THE BORE instead of on a wrist — the macro
  // lens HOME's close-ups already use (`home_shot_t`'s piv_on/bore/rise/fov),
  // exposed to a script. `vmorbit` pivots on a HAND, so nothing forward of the
  // support hand can be framed: the AR's muzzle is 190 mm past that wrist and
  // leaves the frame before the lens is close enough to read a chamfer. Every
  // detail argument in this pass was settled through this command.
  g_vmorb.on = 1;
  g_vmorb.which = 2;
  g_vmorb.piv_on = 1;
  g_vmorb.comp = 0.0f;
  g_vmorb.yaw = next_f() * DEG2RAD;
  g_vmorb.pit = next_f() * DEG2RAD;
  g_vmorb.dist = next_f();
  g_vmorb.bore = next_f();    // station ALONG the bore, gun-local metres
  g_vmorb.rise = next_f();    // ...and off it, along the weapon's own up
  float bfov = next_f();
  g_vmorb.fov = bfov > 1.0f ? bfov : 24.0f;
}

static void cmd_hud(sim_ctx_t *s) {
  (void)s;
  g_hud_visible = next_on_off("hud");
}

static void cmd_showfps(sim_ctx_t *s) {
  (void)s;
  g_cfg.show_fps = next_on_off("showfps");
}

// `dbgview ao|off`: the composite shows a screen-space buffer instead of the
// picture, so a pass can be judged on its own output rather than through
// the lighting it feeds.
static void cmd_dbgview(sim_ctx_t *s) {
  (void)s;
  const char *a = next_tok();
  g_dbg_view = !strcmp(a, "ao") ? 1 : !strcmp(a, "bloom") ? 2
             : !strcmp(a, "alpha") ? 3 : 0;
}

// `grade CURVE CONTRAST SAT`: the display look for A/B frames (CURVE 0 = the
// ACES fit, 1 = AgX). Every tonemapping program reads it through rig_upload.
// THREE, not four: uLook.w is the player's brightness and comes from the config
// key, so a graded A/B frame still renders at the exposure that is configured.
static void cmd_grade(sim_ctx_t *s) {
  (void)s;
  g_look[0] = next_f();
  g_look[1] = next_f();
  g_look[2] = next_f();
}

static void cmd_trace(sim_ctx_t *s) {
  long n = next_n();
  while (n-- > 0) { sim_advance(s); sim_print(s); }
}

// `bench N`: the render cost of the CURRENT view — N frames of render_frame
// back to back, glFinish'd, wall-clock ms per frame. No sim ticks, so the
// number is the renderer's alone; the harness's llvmpipe makes it a proxy
// for fragment cost, not a frame rate.
static void cmd_bench(sim_ctx_t *s) {
  long n = next_n();
  if (n < 1) n = 1;
  render_frame(s->p, 1.0f, 120, &s->cam);   // warm: programs, targets, uploads
  glFinish();
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  for (long i = 0; i < n; i++) render_frame(s->p, 1.0f, 120, &s->cam);
  glFinish();
  clock_gettime(CLOCK_MONOTONIC, &t1);
  double ms = ((double)(t1.tv_sec - t0.tv_sec) * 1e3 +
               (double)(t1.tv_nsec - t0.tv_nsec) * 1e-6) / (double)n;
  printf("bench frames=%ld ms/frame=%.2f\n", n, ms);
}

// `benchpass N`: `bench` broken down by pass — N frames, the per-pass GPU
// timers averaged, plus the wall clock of the whole frame for the CPU side
// (scene_build, vm_build, the HUD batch) that no GPU timer sees.
static void cmd_benchpass(sim_ctx_t *s) {
  long n = next_n();
  if (n < 1) n = 1;
  render_frame(s->p, 1.0f, 120, &s->cam);
  glFinish();
  double acc[RF_PASSES] = {0};
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  g_rf_bench = 1;
  for (long i = 0; i < n; i++) {
    render_frame(s->p, 1.0f, 120, &s->cam);
    glFinish();
    for (int k = 0; k < RF_PASSES; k++) {
      GLuint64 ns = 0;
      glGetQueryObjectui64v(g_rf_q[k], GL_QUERY_RESULT, &ns);
      acc[k] += (double)ns * 1e-6;
    }
  }
  g_rf_bench = 0;
  clock_gettime(CLOCK_MONOTONIC, &t1);
  double wall = ((double)(t1.tv_sec - t0.tv_sec) * 1e3 +
                 (double)(t1.tv_nsec - t0.tv_nsec) * 1e-6) / (double)n;
  double sum = 0.0;
  printf("benchpass frames=%ld wall=%.2f ms/frame\n", n, wall);
  for (int k = 0; k < RF_PASSES; k++) {
    printf("  %-10s %7.2f ms\n", RF_PASS_NAME[k], acc[k] / (double)n);
    sum += acc[k] / (double)n;
  }
  printf("  %-10s %7.2f ms\n", "gpu-sum", sum);
}

static void cmd_shot(sim_ctx_t *s) {
  const char *path = next_tok();
  uint64_t scope_before = g_scope_scene_serial;
  render_frame(s->p, 1.0f, 120, &s->cam);
  film_frame(s, scope_before);
  write_png(path, R.width, R.height);
}

static void cmd_capture(sim_ctx_t *s) {
  const char *rgb_path = next_tok();
  const char *wav_path = next_tok();
  long fps = next_n();
  long frames = next_n();
  capture_run(s, rgb_path, wav_path,
              fps == 60 || fps == 120 ? (int)fps : 0, frames);
}

static void cmd_cam(sim_ctx_t *s) {
  char *a = next_tok();
  if (!strcmp(a, "off")) {
    s->cam.active = s->cam.bot = s->cam.orbit = 0;
    return;
  }
  s->cam.active = 1;
  s->cam.bot = s->cam.orbit = 0;
  s->cam.eye.x = parse_f(a);
  s->cam.eye.y = next_f();
  s->cam.eye.z = next_f();
  s->cam.yaw = next_f();
  s->cam.pitch = next_f();
}

static void cmd_camaim(sim_ctx_t *s) {
  s->cam.blook = next_f();  // cambot look height (default .9)
}

static void cmd_cambot(sim_ctx_t *s) {
  char *a = next_tok();
  if (!strcmp(a, "off")) { s->cam.bot = 0; return; }
  s->cam.bot = 1;
  s->cam.active = s->cam.orbit = 0;
  s->cam.boff.x = parse_f(a);
  s->cam.boff.y = next_f();
  s->cam.boff.z = next_f();
}

static void cmd_camorbit(sim_ctx_t *s) {
  char *a = next_tok();
  if (!strcmp(a, "off")) { s->cam.orbit = 0; return; }
  s->cam.orbit = 1;
  s->cam.active = s->cam.bot = 0;
  s->cam.orbit_radius = parse_f(a);
  s->cam.orbit_height = next_f();
  s->cam.orbit_rate = next_f();
  s->cam.orbit_look = next_f();
  s->cam.orbit_phase = 0.0f;
}

typedef struct {
  int menu, home, tab, page, control, padsub, devsub, wpn;
  int focus, nfocus, padmode, rebind, rebindpad, arm, nedit;
  int kb, kb_r, kb_c, dev, curve, assist, inv, connected;
  int quit, online, client, root_focus, nav_n, nav_act;
  float dz_l, dz_r, sens, ads_sens;
  char name[sizeof g_cfg.name];
} homeui_state_t;

// Large script-proof buffers are mutually exclusive: the dispatcher finishes one
// command before entering the next. Keep that lifetime contract explicit instead of
// giving every proof a permanent BSS allocation. HOMEUI currently sets the arena's
// size: one retained 1280x800 RGB frame plus one scanline. The next framebuffer is
// compared a row at a time and retained only when it becomes the next baseline.
#define HOMEUI_SCRATCH_W 1280
#define HOMEUI_SCRATCH_H 800
#define HOMEUI_FRAME_BYTES ((size_t)HOMEUI_SCRATCH_W * HOMEUI_SCRATCH_H * 3u)
#define HOMEUI_ROW_BYTES ((size_t)HOMEUI_SCRATCH_W * 3u)
#define HARNESS_SCRATCH_BYTES (HOMEUI_FRAME_BYTES + HOMEUI_ROW_BYTES)
typedef union {
  uint8_t bytes[HARNESS_SCRATCH_BYTES];
  long double align_ld;
  void *align_ptr;
  uint64_t align_u64;
} harness_scratch_t;
enum { HARNESS_SCRATCH_NONE, HARNESS_SCRATCH_HOMEUI };
static harness_scratch_t g_harness_scratch;
static int g_harness_scratch_owner;

_Static_assert(sizeof g_harness_scratch == HARNESS_SCRATCH_BYTES,
               "harness scratch gained hidden padding");
_Static_assert(HARNESS_SCRATCH_BYTES <= 13000000u,
               "harness scratch exceeds the 13 MB hard budget");

static int harness_scratch_acquire(int owner) {
  if (owner <= HARNESS_SCRATCH_NONE || g_harness_scratch_owner) return 0;
  g_harness_scratch_owner = owner;
  return 1;
}

static uint8_t *harness_scratch_bytes(int owner) {
  return g_harness_scratch_owner == owner ? g_harness_scratch.bytes : NULL;
}

static int harness_scratch_release(int owner) {
  if (owner <= HARNESS_SCRATCH_NONE || g_harness_scratch_owner != owner)
    return 0;
  g_harness_scratch_owner = HARNESS_SCRATCH_NONE;
  return 1;
}

// One UI-only frame step, shared by the public `uiframe` command and HOME's pixel/
// interaction proof. Keeping the operation here makes the proof exercise the exact
// command path required by the HOME acceptance recipe.
static void harness_uiframe(sim_ctx_t *s) {
  render_frame(s->p, 1.0f, 120, &s->cam);
}

static homeui_state_t homeui_state(void) {
  homeui_state_t q = {0};
  q.menu = g_menu_open; q.home = g_home;
  q.tab = g_ui_tab; q.page = g_ui_page; q.control = g_ui_control;
  q.padsub = g_ui_pad_sub; q.devsub = g_ui_dev_sub; q.wpn = g_ui_wpn;
  q.focus = g_ui_focus; q.nfocus = g_ui_nfocus_last;
  q.padmode = g_ui_pad_mode; q.rebind = g_ui_rebind;
  q.rebindpad = g_ui_rebind_pad; q.arm = g_ui_arm;
  q.nedit = g_ui_name_edit; q.kb = g_ui_kb;
  q.kb_r = g_ui_kb_r; q.kb_c = g_ui_kb_c;
  q.dev = DEV_MENU && g_cfg.devmode; q.curve = g_cfg.pad_curve;
  q.dz_l = g_cfg.pad[PAD_DZ_L]; q.dz_r = g_cfg.pad[PAD_DZ_R];
  q.sens = g_cfg.pad[PAD_SENS]; q.ads_sens = g_cfg.pad[PAD_ADS_SENS];
  q.assist = g_cfg.pad_assist; q.inv = g_cfg.pad_inv_y;
  q.connected = g_pad.connected;
  q.quit = g_ui_quit; q.online = g_online; q.client = g_cl.state;
  q.root_focus = g_ui_root_focus; q.nav_n = g_nav_n; q.nav_act = g_nav_act;
  memcpy(q.name, g_cfg.name, sizeof q.name);
  return q;
}

static int homeui_state_same(const homeui_state_t *a, const homeui_state_t *b) {
  return a->menu == b->menu && a->home == b->home &&
         a->tab == b->tab && a->page == b->page &&
         a->control == b->control && a->padsub == b->padsub &&
         a->devsub == b->devsub && a->wpn == b->wpn &&
         a->focus == b->focus && a->nfocus == b->nfocus &&
         a->padmode == b->padmode && a->rebind == b->rebind &&
         a->rebindpad == b->rebindpad && a->arm == b->arm &&
         a->nedit == b->nedit && a->kb == b->kb &&
         a->kb_r == b->kb_r && a->kb_c == b->kb_c &&
         a->dev == b->dev && a->curve == b->curve &&
         a->dz_l == b->dz_l && a->dz_r == b->dz_r &&
         a->sens == b->sens && a->ads_sens == b->ads_sens &&
         a->assist == b->assist && a->inv == b->inv &&
         a->connected == b->connected && a->quit == b->quit &&
         a->online == b->online && a->client == b->client &&
         a->root_focus == b->root_focus && a->nav_n == b->nav_n &&
         a->nav_act == b->nav_act &&
         !memcmp(a->name, b->name, sizeof a->name);
}

static int homeui_capture_pixels(void) {
  uint8_t *frame = harness_scratch_bytes(HARNESS_SCRATCH_HOMEUI);
  size_t bytes = (size_t)R.width * (size_t)R.height * 3;
  if (!frame || R.width <= 0 || R.height <= 0 ||
      R.width > HOMEUI_SCRATCH_W || R.height > HOMEUI_SCRATCH_H ||
      bytes > HOMEUI_FRAME_BYTES)
    return 0;
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  while (glGetError() != GL_NO_ERROR) {}
  glReadPixels(0, 0, R.width, R.height, GL_RGB, GL_UNSIGNED_BYTE,
               frame);
  return glGetError() == GL_NO_ERROR;
}

static int homeui_pixel_delta_current(float ax, float ay, float bx, float by,
                                      int *changed) {
  const uint8_t *frame = harness_scratch_bytes(HARNESS_SCRATCH_HOMEUI);
  uint8_t *row = harness_scratch_bytes(HARNESS_SCRATCH_HOMEUI);
  if (!frame || !row || !changed || R.width <= 0 || R.height <= 0 ||
      R.width > HOMEUI_SCRATCH_W || R.height > HOMEUI_SCRATCH_H)
    return 0;
  row += HOMEUI_FRAME_BYTES;
  // ui_menu's graticule reaches 7.1 virtual units from its centre. Mask its old
  // and new bounding squares only; any readout-wide hover wash remains measurable.
  int rad = (int)ceilf(8.0f * g_uis) + 1;
  *changed = 0;
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  while (glGetError() != GL_NO_ERROR) {}
  for (int gl_y = 0; gl_y < R.height; gl_y++) {
    glReadPixels(0, gl_y, R.width, 1, GL_RGB, GL_UNSIGNED_BYTE, row);
    if (glGetError() != GL_NO_ERROR) return 0;
    int y = R.height - 1 - gl_y;
    for (int x = 0; x < R.width; x++) {
      int cursor_a = abs(x - (int)ax) <= rad && abs(y - (int)ay) <= rad;
      int cursor_b = abs(x - (int)bx) <= rad && abs(y - (int)by) <= rad;
      if (cursor_a || cursor_b) continue;
      size_t at = ((size_t)gl_y * (size_t)R.width + (size_t)x) * 3u;
      size_t rt = (size_t)x * 3u;
      if (frame[at] != row[rt] || frame[at + 1] != row[rt + 1] ||
          frame[at + 2] != row[rt + 2])
        (*changed)++;
    }
  }
  return 1;
}

static void cmd_homeui(sim_ctx_t *s) {
  // One current-resolution HOME acceptance run. CI launches four fresh processes at
  // the required sizes; this command owns the fixed time, cursor mask and state diff.
  if (!harness_scratch_acquire(HARNESS_SCRATCH_HOMEUI)) {
    printf("homeui scratch=%zu busy=%d FAIL\n", HARNESS_SCRATCH_BYTES,
           g_harness_scratch_owner);
    exit(1);
  }
  const float proof_time = 36.0f;     // loop 0, middle of UPPER RECEIVER
  float dt_save = g_ui_dt;
  g_ui_dt = 0.0f;                    // pixel pairs must photograph one director instant
  g_ui_mdown = 0;
  if (!g_menu_open) ui_open_menu();
  ui_route(UI_PAGE_ROOT);
  home_enter(s->p);
  g_home_t0 = g_ui_time - proof_time;
  home_stage(s->p);

  // Warm lazy render state and establish the root focus census before sampling.
  g_ui_cx = g_ui_cy = -40.0f;
  harness_uiframe(s);

  home_readout_box_t box;
  int visible = ui_home_readout_box(g_ui_vw, g_ui_vh, &box);
  float rail_right = HOME_RAIL_X + HOME_RAIL_W;
  float gap = box.x - rail_right;
  int layout_ok = visible && gap >= 20.0f - 0.01f &&
                  box.x + box.w <= g_ui_vw - 34.0f + 0.01f &&
                  box.y - 13.0f >= 0.0f && box.y + box.h <= g_ui_vh;

  // Exercise the exact fit helper on both weapon strings and every authored caption,
  // including the long UPPER RECEIVER and the sniper-only substitute shot.
  float worst_over = 0.0f;
  for (int wi = 0; wi < WPN_COUNT; wi++) {
    float ns = 1.35f, nt = 1.5f;
    float over = ui_home_fit_width(HOME_WNAME[wi], box.w, &ns, &nt) - box.w;
    if (over > worst_over) worst_over = over;
    float ss = 0.72f, st = 1.3f;
    over = ui_home_fit_width(HOME_WSPEC[wi], box.w, &ss, &st) - box.w;
    if (over > worst_over) worst_over = over;
  }
  for (int i = 0; i <= HOME_NSHOT; i++) if (HOME_SHOT[i].cap) {
    float cs = 0.8f, ct = 2.4f;
    float over = ui_home_fit_width(HOME_SHOT[i].cap, box.w, &cs, &ct) - box.w;
    if (over > worst_over) worst_over = over;
  }
  int fit_ok = worst_over <= 0.01f;

  float fade = 0.0f;
  const char *cap = home_caption(&fade);
  int caption_ok = cap && !strcmp(cap, "UPPER RECEIVER") && fade > 0.99f;
  float cs = 0.8f, ct = 2.4f;
  float capw = cap ? ui_home_fit_width(cap, box.w, &cs, &ct) : 0.0f;

  float out_x = (rail_right + box.x) * 0.5f * g_uis;
  float main_x = (box.x + box.w * 0.5f) * g_uis;
  float main_y = (box.y + box.h * 0.5f) * g_uis;
  float cap_x = (box.x + box.w - capw * 0.5f) * g_uis;
  float cap_y = (box.y - 9.5f) * g_uis;

  // Outside readout -> inside readout: only the two graticule squares may move.
  g_ui_cx = out_x; g_ui_cy = main_y;
  harness_uiframe(s);
  int read_ok = homeui_capture_pixels();
  homeui_state_t before = homeui_state();
  g_ui_cx = main_x; g_ui_cy = main_y;
  harness_uiframe(s);
  homeui_state_t hover = homeui_state();
  int hover_delta = 0;
  read_ok &= homeui_pixel_delta_current(out_x, main_y, main_x, main_y,
                                         &hover_delta);
  // The hovered frame is the baseline for the following press.
  read_ok &= homeui_capture_pixels();

  // Press/release in the body, then repeat on the caption. Both pixels and every field
  // printed by uistat must remain unchanged; page/root/menu extras are included too.
  g_ui_mdown = 1;
  harness_uiframe(s);
  homeui_state_t main_down = homeui_state();
  int click_delta = 0;
  read_ok &= homeui_pixel_delta_current(main_x, main_y, main_x, main_y,
                                         &click_delta);
  g_ui_mdown = 0;
  harness_uiframe(s);
  homeui_state_t main_up = homeui_state();

  g_ui_cx = cap_x; g_ui_cy = cap_y;
  harness_uiframe(s);
  read_ok &= homeui_capture_pixels();
  homeui_state_t cap_hover = homeui_state();
  g_ui_mdown = 1;
  harness_uiframe(s);
  homeui_state_t cap_down = homeui_state();
  int cap_delta = 0;
  read_ok &= homeui_pixel_delta_current(cap_x, cap_y, cap_x, cap_y,
                                         &cap_delta);
  click_delta += cap_delta;
  g_ui_mdown = 0;
  harness_uiframe(s);
  homeui_state_t after = homeui_state();

  int state_ok = homeui_state_same(&before, &hover) &&
                 homeui_state_same(&before, &main_down) &&
                 homeui_state_same(&before, &main_up) &&
                 homeui_state_same(&before, &cap_hover) &&
                 homeui_state_same(&before, &cap_down) &&
                 homeui_state_same(&before, &after);
  int focus_ok = before.page == UI_PAGE_ROOT && before.menu && before.home &&
                 before.focus == 0 && before.nfocus == 6 && !before.padmode;
  // The shipped binary is stamped with YYYY-MM-DD plus a seven-character commit. Its
  // footer and the shadowed 120 FPS counter peak at 1560 vertices; `dev/unknown`
  // uses 1518. The separate match-settings row adds exactly 114 vertices. The counter contributes 72 vertices to both measured builds.
  const int vertex_budget = 1560;
  int budget_ok = g_ui_peak <= vertex_budget && g_ui_drops == 0;
  int scratch_ok = harness_scratch_release(HARNESS_SCRATCH_HOMEUI);
  int ok = read_ok && scratch_ok && layout_ok && fit_ok && caption_ok && hover_delta == 0 &&
           click_delta == 0 && state_ok && focus_ok && budget_ok;

  printf("homeui res=%dx%d virtual=%.0fx%.0f visible=%d gap=%.1f "
         "fit_over=%.2fpx caption=%d hover_delta=%d click_delta=%d "
         "state=%s focus=%d/%d ui_peak=%d/%d ui_drops=%ld %s\n",
         R.width, R.height, (double)g_ui_vw, (double)g_ui_vh, visible,
         (double)gap, (double)fmaxf(worst_over, 0.0f), caption_ok,
         hover_delta, click_delta, state_ok ? "stable" : "CHANGED",
         before.focus, before.nfocus, g_ui_peak, vertex_budget, g_ui_drops,
         ok ? "ok" : "FAIL");

  g_ui_dt = dt_save;
  if (!ok) exit(1);
}

static void cmd_home(sim_ctx_t *s) {
  // The BOOT SCREEN, on demand.
  char *ha = next_tok();
  if (!strcmp(ha, "off")) {
    g_home = 0;
  } else {
    if (!g_menu_open) { g_ui_cx = g_ui_cy = -40.0f; ui_open_menu(); }
    ui_route(UI_PAGE_ROOT);
    home_enter(s->p);
    if (strcmp(ha, "on")) {
      // ...AND STAGE AGAIN AFTERWARDS. home_enter stages for whatever loop the
      // clock is on, which is loop 0, and the jump then lands the director in a
      // different one — so `home 52` framed loop 1's shots against loop 0's corner
      // and photographed a wall with the soldier somewhere off the side of it.
      g_home_t0 = g_ui_time - (float)atof(ha);
      home_stage(s->p);
    }
  }
}

static void cmd_menu(sim_ctx_t *s) {
  (void)s;
  // The native backends seed the software cursor from the real pointer on menu
  // open; headless has no pointer, and the unseeded (0,0) default painted a clipped
  // white cursor fragment into the corner of every menu screenshot.
  if (!g_menu_open) g_ui_cx = g_ui_cy = -40.0f;
  ui_on_escape();  // same semantics as ESC in the game
}

static void cmd_cursor(sim_ctx_t *s) {
  (void)s;
  g_ui_cx = next_f();
  g_ui_cy = next_f();
}

static void cmd_mdown(sim_ctx_t *s) {
  (void)s;
  g_ui_mdown = 1;
}

static void cmd_mup(sim_ctx_t *s) {
  (void)s;
  g_ui_mdown = 0;
}

static void cmd_uiframe(sim_ctx_t *s) {
  harness_uiframe(s);
  if (g_ui_quit) puts("ui: quit");
}

static void cmd_appframe(sim_ctx_t *s) {
  // N frames through the REAL shared frame step — the only headless caller of
  // app_frame, including event admission across multiple ticks per rendered frame.
  // The default is one tick; `menu; appframe 3` still tests pause without catch-up.
  long n = next_n();
  long batch = opt_n(1);
  if (batch < 1 || batch > 12) {
    fprintf(stderr, "appframe: ticks per frame must be 1..12\n");
    exit(1);
  }
  static app_t app;
  // The script's own held state feeds through, exactly like wait/trace — an empty
  // input here would silently RELEASE every +action for the duration and re-arm its
  // press edge after, i.e. the battery would measure an input sequence the game
  // never ran. ...and the BOOT SCREEN's director is the second producer of input in
  // this file (frame_input_keys is the first).
  int ran = 0;
  while (n-- > 0) {
    frame_input_t in = {0};
    for (int i = 0; i < IN_COUNT; i++) {
      in.physical_held[i] = s->held[i];
      in.held[i] = in.held_first[i] = s->held[i] || home_input(i);
    }
    if (input_wheel_swap()) in.held_first[IN_SWAP] = 1;
    uint64_t serial = g_sp_serial;
    int r = app_frame(&app, s->p, (double)batch * TICK_DT, &in);
    s->p = local_player();
    if (serial != g_sp_serial) memset(&s->cam, 0, sizeof s->cam);
    if (r) g_pressed[KEY_MWUP] = g_pressed[KEY_MWDN] = 0;
    ran += r;
  }
  s->tick += ran;
  printf("appframe ticks=%d acc=%.4f menu=%d\n", ran, app.acc, g_menu_open);
}

static void cmd_pad(sim_ctx_t *s) {
  (void)s;
  // Raw pad state, exactly what a platform pump would have written — so
  // every consumer (deadzone, curve, triggers, menu nav, stick viz) runs
  // the real shared-core path, deterministically.
  char *k = next_tok();
  if      (!strcmp(k, "on"))  g_pad.connected = 1;
  else if (!strcmp(k, "off")) memset(&g_pad, 0, sizeof g_pad);
  else if (!strcmp(k, "btn")) {
    char *b = next_tok();
    long v = next_n();
    char tok[16];
    snprintf(tok, sizeof tok, "pad_%s", b);
    int found = 0;
    for (int i = 0; i < PB_COUNT; i++)
      if (!strcmp(tok, PAD_BTN_DEF[i].tok)) { g_pad.btn[i] = v != 0; found = 1; }
    if (!found) { fprintf(stderr, "script: unknown pad button '%s'\n", b); exit(1); }
    // lt/rt as buttons drive the analog value too — pad_btn_sync would
    // otherwise recompute the level from the axis and silently revert.
    if (!strcmp(b, "lt")) g_pad.lt = v ? 1.0f : 0.0f;
    if (!strcmp(b, "rt")) g_pad.rt = v ? 1.0f : 0.0f;
    g_pad.connected = 1;
  } else {
    float v = next_f();
    g_pad.connected = 1;
    if      (!strcmp(k, "lx")) g_pad.lx = v;
    else if (!strcmp(k, "ly")) g_pad.ly = v;
    else if (!strcmp(k, "rx")) g_pad.rx = v;
    else if (!strcmp(k, "ry")) g_pad.ry = v;
    else if (!strcmp(k, "lt")) g_pad.lt = v;
    else if (!strcmp(k, "rt")) g_pad.rt = v;
    else { fprintf(stderr, "script: unknown pad field '%s'\n", k); exit(1); }
  }
}

static void cmd_padscan(sim_ctx_t *s) {
  (void)s;
  // THE ON-DEVICE PROOF FOR THE MULTI-NODE PUMP, and the one pad proof that cannot
  // be faked.
#ifndef _WIN32
  nat_pad_report((int)opt_n(600));
#else
  printf("padscan unavailable on this backend\n");
#endif
}

static void cmd_padlook(sim_ctx_t *s) {
  // N ticks of right-stick look. 120 ticks at full deflection must print dyaw ==
  // pad_sens_x — the sens unit IS this proof.
  long n = next_n();
  float y0 = s->p->yaw, p0 = s->p->pitch;
  while (n-- > 0) pad_look(s->p, TICK_DT);
  printf("padlook dyaw=%.3f dpitch=%.3f yaw=%.4f pitch=%.4f\n",
         (double)((s->p->yaw - y0) / DEG2RAD),
         (double)((s->p->pitch - p0) / DEG2RAD),
         (double)s->p->yaw, (double)s->p->pitch);
}

static void cmd_padstat(sim_ctx_t *s) {
  // The pad pipeline's proof: deadzoned move/aim vectors, the curve's
  // output, the ADS look scale, trigger hysteresis levels, and — through
  // the REAL resolve_binds/frame_input_keys — which actions the pad holds.
  float mx, my, ax, ay;
  float mm = pad_deadzone(g_pad.lx, g_pad.ly, g_cfg.pad[PAD_DZ_L], &mx, &my);
  float am = pad_deadzone(g_pad.rx, g_pad.ry, g_cfg.pad[PAD_DZ_R], &ax, &ay);
  printf("pad conn=%d move=(%.3f %.3f) m=%.3f aim=(%.3f %.3f) m=%.3f "
         "curve=%s out=%.3f adsk=%.3f lt=%.2f rt=%.2f ltb=%d rtb=%d btn=",
         g_pad.connected, (double)mx, (double)my, (double)mm,
         (double)ax, (double)ay, (double)am,
         PAD_CURVE_OPTS[g_cfg.pad_curve],
         (double)pad_curve_apply(am, g_cfg.pad_curve),
         (double)pad_ads_look_scale(s->p),
         (double)g_pad.lt, (double)g_pad.rt,
         g_pad.btn[PB_LT], g_pad.btn[PB_RT]);
  int any = 0;
  for (int i = 0; i < PB_COUNT; i++)
    if (g_pad.btn[i]) printf("%s%s", any++ ? "," : "", PAD_BTN_DEF[i].tok + 4);
  if (!any) printf("-");
  {
    // Aim-assist probe, independent of stick state: which ENTITY the bubble would
    // engage (id, not bot index), the angular offset vs the cone, the
    // target's distance, and the friction the look rate would take.
    v3 atp;
    float aang = 0, aeng = 0, adist = 0;
    int at = pad_assist_target(s->p, &atp, &aang, &aeng, &adist);
    float fric = at >= 0 ? pad_assist_friction(aang, aeng, adist) : 1.0f;
    printf(" assist=%d tgt=%d ang=%.2f eng=%.2f dist=%.1f fric=%.2f",
           g_cfg.pad_assist, at,
           (double)(aang / DEG2RAD), (double)(aeng / DEG2RAD),
           (double)adist, (double)fric);
  }
  int kb[IN_COUNT], pb[IN_COUNT];
  resolve_binds(kb, pb);
  frame_input_t fin = {0};
  frame_input_keys(&fin, kb, pb);
  printf(" held=");
  any = 0;
  for (int i = 0; i < IN_COUNT; i++)
    if (fin.held[i]) printf("%s%s", any++ ? "," : "", ACTION_DEF[i].script);
  if (!any) printf("-");
  printf("\n");
}

static void cmd_padcurve(sim_ctx_t *s) {
  (void)s;
  // Every curve, tabulated: endpoints must be 0 and 1 and each row must be
  // monotone — the proof that no curve changes top speed, only the travel.
  for (int c = 0; c < 3; c++) {
    printf("padcurve %-8s", PAD_CURVE_OPTS[c]);
    for (int i = 0; i <= 10; i++)
      printf(" %.3f", (double)pad_curve_apply((float)i / 10.0f, c));
    printf("\n");
  }
}

static void cmd_nav(sim_ctx_t *s) {
  (void)s;
  char *k = next_tok();
  static const char *const NAVN[8] = {"up", "down", "left", "right",
                                      "ok", "back", "tabl", "tabr"};
  int hit = -1;
  for (int i = 0; i < 8; i++)
    if (!strcmp(k, NAVN[i])) hit = i;
  if (hit < 0) { fprintf(stderr, "script: unknown nav '%s'\n", k); exit(1); }
  nav_push(hit);
}

// Existing entity fields remain byte-compared. New internal threat records use
// their field serializer because C struct padding is not simulation state.
static uint32_t netloop_state_hash(void) {
  uint32_t hash = 2166136261u;
  const unsigned char *bytes = (const unsigned char *)g_players;
  for (size_t i = 0; i < sizeof g_players; i++) hash = (hash ^ bytes[i]) * 16777619u;
  for (int bi = 0; bi < MAX_BOTS; bi++) {
    bytes = (const unsigned char *)&g_bots[bi];
    size_t prefix = (const unsigned char *)&g_bots[bi].suppress - bytes;
    size_t suffix = (const unsigned char *)&g_bots[bi].hurt_t - bytes;
    for (size_t i = 0; i < prefix; i++) hash = (hash ^ bytes[i]) * 16777619u;
    for (size_t i = suffix; i < sizeof(bot_t); i++) hash = (hash ^ bytes[i]) * 16777619u;
  }
  bytes = (const unsigned char *)&g_match;
  size_t prefix = (const unsigned char *)g_match.bodies - bytes;
  size_t suffix = (const unsigned char *)&g_match.suppress_segments - bytes;
  for (size_t i = 0; i < prefix; i++) hash = (hash ^ bytes[i]) * 16777619u;
  for (size_t i = suffix; i < sizeof g_match; i++) hash = (hash ^ bytes[i]) * 16777619u;
  sha256_t sha;
  sha256_init(&sha);
  bot_suppress_state_hash(&sha);
  char digest[65];
  sha256_hex(&sha, digest);
  for (int i = 0; i < 64; i++) hash = (hash ^ (unsigned char)digest[i]) * 16777619u;
  return hash;
}

static void cmd_netloop(sim_ctx_t *s) {
  // THE Phase-2 proof: the same N ticks once through sim_advance and once through
  // the server loop's own tick body, from an identical saved sim state, per-tick
  // hash compared — MISMATCH prints the first divergent tick. "36 000 ticks ran"
  // was no proof: an output that cannot move is not a proof (parity's own rule).
  long n = next_n();
  if (n > 4096) n = 4096;
  struct {
    player_t players[MAX_HUMANS];
    bot_t bots[MAX_BOTS];
    match_t match;
    long tick;
    uint64_t rng, rng_gun[MAX_HUMANS], rng_bot;
    uint32_t map_seed;
    unsigned char on[MAX_HUMANS], spawn_seq[MAX_ENTS];
  } sv;
  memcpy(sv.players, g_players, sizeof g_players);
  memcpy(sv.bots, g_bots, sizeof g_bots);
  sv.match = g_match;
  sv.tick = g_tick;
  sv.rng = g_rng; sv.rng_bot = g_rng_bot;
  memcpy(sv.rng_gun, g_rng_gun, sizeof g_rng_gun);
  sv.map_seed = g_map_seed;
  memcpy(sv.on, g_humans_on, sizeof g_humans_on);
  memcpy(sv.spawn_seq, g_spawn_seq, sizeof g_spawn_seq);
  static uint32_t ha[4096], hb[4096];
  for (long i = 0; i < n; i++) { sim_advance(s); ha[i] = netloop_state_hash(); }
  // restore — map first (a fraglimit restart advances the seed and
  // regenerates solids; map_generate also reseeds every RNG stream),
  // then the exact RNG/entity/match state over it.
  map_generate(sv.map_seed);
  g_map_seed = sv.map_seed;
  memcpy(g_players, sv.players, sizeof g_players);
  memcpy(g_bots, sv.bots, sizeof g_bots);
  g_match = sv.match;
  g_tick = sv.tick;
  g_rng = sv.rng; g_rng_bot = sv.rng_bot;
  memcpy(g_rng_gun, sv.rng_gun, sizeof g_rng_gun);
  memcpy(g_humans_on, sv.on, sizeof g_humans_on);
  memcpy(g_spawn_seq, sv.spawn_seq, sizeof g_spawn_seq);
  g_num_events = 0;
  for (long i = 0; i < n; i++) { server_tick_once(s->held); hb[i] = netloop_state_hash(); }
  long bad = -1;
  for (long i = 0; i < n && bad < 0; i++)
    if (ha[i] != hb[i]) bad = i;
  if (bad >= 0)
    printf("netloop n=%ld MISMATCH at tick %ld (sim %08x vs server %08x)\n",
           n, bad, ha[bad], hb[bad]);
  else
    printf("netloop n=%ld ok hash=%08x\n", n, n ? netloop_report_hash() : 0);
  if (bad >= 0) exit(1);
  s->tick += n;   // the second pass advanced the world again
}

static void cmd_netpack(sim_ctx_t *s) {
  (void)s;
  // Encode/decode round-trip over random states, worst error PER FIELD. A
  // quantization error is invisible until someone's aim is 0.2 deg off, so the
  // number has to be printed, not asserted away.
  long n = next_n();
  uint64_t rs = 0x1234abcd ^ (uint64_t)n * 2654435761u;
  float e_yaw = 0, e_pitch = 0, e_wish = 0, e_pos = 0, e_vel = 0;
  int roundtrip_fail = 0;
  for (long i = 0; i < n; i++) {
    float fy = (rng_next(&rs) / 4294967296.0f - 0.5f) * F_TAU;
    float fp = (rng_next(&rs) / 4294967296.0f - 0.5f) * 2.0f * PITCH_MAX;
    float fw = rng_next(&rs) / 4294967296.0f * 2.0f - 1.0f;
    float fx = (rng_next(&rs) / 4294967296.0f - 0.5f) * 640.0f;
    float fv = (rng_next(&rs) / 4294967296.0f - 0.5f) * 640.0f;
    net_input_t in = {.tick = (uint32_t)i, .view = rng_next(&rs),
                      .buttons = (uint16_t)rng_next(&rs),
                      .yaw = net_q_yaw(fy), .pitch = net_q_pitch(fp),
                      .wish_x = net_q_wish(fw), .wish_y = net_q_wish(-fw)};
    unsigned char buf[64];
    np_wr w = {buf, sizeof buf, 0, 0};
    net_input_encode(&w, &in);
    np_rd r = {buf, w.at, 0, 0};   // record = fields only, cursor at 0
    net_input_t out;
    if (!net_input_decode(&r, &out) || out.tick != in.tick ||
        out.view != in.view ||
        out.buttons != (in.buttons & ((1u << IN_COUNT) - 1u)) ||
        out.yaw != in.yaw) roundtrip_fail = 1;
    float dy = fabsf(net_dq_yaw(out.yaw) - wrap_pi(fy));
    float dp = fabsf(net_dq_pitch(out.pitch) - f_clamp(fp, -PITCH_MAX, PITCH_MAX));
    float dw = fabsf(net_dq_wish(out.wish_x) - f_clamp(fw, -1.0f, 1.0f));
    if (dy > e_yaw) e_yaw = dy;
    if (dp > e_pitch) e_pitch = dp;
    if (dw > e_wish) e_wish = dw;
    net_ent_t ne = {.id = (uint8_t)(rng_next(&rs) % MAX_ENTS), .flags = 0xf,
                    .hp = 100, .weapon = 1, .spawn_seq = 3,
                    .x = net_q_pos(fx), .y = net_q_pos(fx * 0.5f), .z = net_q_pos(-fx),
                    .vx = net_q_vel(fv), .vy = net_q_vel(0), .vz = net_q_vel(-fv),
                    .yaw = net_q_yaw(fy), .pitch = net_q_pitch(fp),
                    .lean = net_q_wish(fw)};
    np_wr w2 = {buf, sizeof buf, 0, 0};
    net_ent_encode(&w2, &ne);
    np_rd r2 = {buf, w2.at, 0, 0};
    net_ent_t neo;
    if (!net_ent_decode(&r2, &neo) || neo.id != ne.id || neo.hp != ne.hp)
      roundtrip_fail = 1;
    float dpos = fabsf(net_dq_pos(neo.x) - f_clamp(fx, -327.0f, 327.0f));
    float dvel = fabsf(net_dq_vel(neo.vx) - f_clamp(fv, -327.0f, 327.0f));
    if (dpos > e_pos) e_pos = dpos;
    if (dvel > e_vel) e_vel = dvel;
  }
  printf("netpack n=%ld roundtrip=%s worst_yaw=%.5frad worst_pitch=%.5frad "
         "worst_wish=%.5f worst_pos=%.4fm worst_vel=%.4fm/s\n",
         n, roundtrip_fail ? "FAIL" : "ok",
         (double)e_yaw, (double)e_pitch, (double)e_wish,
         (double)e_pos, (double)e_vel);
}

static void cmd_netpredict(sim_ctx_t *s) {
  // The whole snapshot path, in one process: build a snapshot from live sim state,
  // encode, decode, and check the reconstructed entity positions match the live
  // ones within quantization (1 cm).
  long n = next_n();
  // Capture INSIDE the loop: the harness drains its event list every
  // advance, so capturing once at the end sees one tick's worth and the
  // section round-trips empty — a proof that passes without proving.
  for (long i = 0; i < n; i++) sim_advance(s);
  static unsigned char snap[2048];
  // ev_ack 0 = "this client has acknowledged nothing", so every event still
  // inside the resend window rides along and the section is exercised.
  size_t len = net_snapshot_build(snap, sizeof snap, (uint32_t)g_tick, 0, 2);
  net_ent_t a[MAX_ENTS], b[MAX_ENTS];
  net_ev_t ea[NET_EV_SNAP_MAX], eb[NET_EV_SNAP_MAX];
  uint32_t st, li, wh;
  int nea = 0, neb = 0;
  int na = net_snapshot_read(snap, len, &st, &li, &wh, NULL, NULL, a, MAX_ENTS,
                             NULL, ea, NET_EV_SNAP_MAX, &nea);
  int nb = net_snapshot_read(snap, len, NULL, NULL, NULL, NULL, NULL, b, MAX_ENTS,
                             NULL, eb, NET_EV_SNAP_MAX, &neb);
  float worst_mm = 0.0f;
  int mism = na != nb, live = 0;
  ENT_FOREACH(e) {
    v3 lp = ent_pos(e);
    int found = -1;
    for (int j = 0; j < na; j++) if (a[j].id == e) { found = j; break; }
    if (found < 0) { mism = 1; continue; }
    float dx = net_dq_pos(a[found].x) - lp.x;
    float dz = net_dq_pos(a[found].z) - lp.z;
    float mm = sqrtf(dx * dx + dz * dz) * 1000.0f;
    if (mm > worst_mm) worst_mm = mm;
    live++;
  }
  for (int j = 0; j < na && !mism; j++)
    if (a[j].id != b[j].id || a[j].x != b[j].x || a[j].hp != b[j].hp) mism = 1;
  // The event section round-trips too, and identically on the second read —
  // the ids are what the client dedups on, so an unstable id is a resend
  // that plays its sound twice.
  if (nea != neb) mism = 1;
  for (int j = 0; j < nea && !mism; j++)
    if (ea[j].id != eb[j].id || ea[j].type != eb[j].type ||
        ea[j].ax != eb[j].ax || ea[j].src != eb[j].src) mism = 1;
  printf("netpredict n=%ld ents=%d evs=%d bytes=%zu server_tick=%u world=%08x "
         "dedup=%s worst_pos=%.2fmm\n",
         n, live, nea, len, st, wh, mism ? "MISMATCH" : "stable", (double)worst_mm);
}

static void cmd_netlagcomp(sim_ctx_t *s) {
  // Rewind the target N ticks into the past and prove the shot HITS with
  // compensation and MISSES without — both halves, plus the residual distance in mm
  // so the number can move.
  long rewind = opt_n(20);
  // stage: bot 0 as a fast puppet crossing in front, freeze the player
  g_bots_frozen = 0;
  // run enough ticks that the history ring is full of motion
  for (int i = 0; i < 40; i++) { sim_advance(s); }
  // aim the player at where bot 0 WAS `rewind` ticks ago
  long past = g_tick - rewind;
  long slot = past % NET_HIST;
  v3 tpos = g_hist[1][slot].pos;   // entity 1 = bot 0
  v3 eye = player_eye_pos(s->p);
  v3 dir = v3_sub((v3){{tpos.x, tpos.y + g_hist[1][slot].eye - ENT_CHEST_OFF, tpos.z}}, eye);
  float dl = v3_len(dir);
  if (dl > 1e-4f) dir = v3_scale(dir, 1.0f / dl);
  int part_c = -1, part_n = -1;
  float d_c = GUN_RANGE, d_n = GUN_RANGE;
  float hc = net_hist_trace(1, past, 0.0f, eye, dir, &part_c);   // compensated
  // uncompensated: trace against the CURRENT box
  float hn = ent_trace_box(g_bots[0].pos, bot_eye(&g_bots[0]),
                           g_bots[0].yaw, 0.0f, eye, dir, &part_n);
  d_c = hc > 0 ? hc : -1; d_n = hn > 0 ? hn : -1;
  // residual: how far the aimed ray passes from the CURRENT target centre
  v3 cur = {{g_bots[0].pos.x, g_bots[0].pos.y + bot_eye(&g_bots[0]) - ENT_CHEST_OFF,
             g_bots[0].pos.z}};
  v3 rel = v3_sub(cur, eye);
  float along = v3_dot(rel, dir);
  v3 closest = v3_add(eye, v3_scale(dir, along));
  float resid_mm = v3_len(v3_sub(cur, closest)) * 1000.0f;
  printf("netlagcomp rewind=%ld comp_hit=%s uncomp_hit=%s resid=%.1fmm "
         "(past tick %ld, now %ld)\n",
         rewind, d_c > 0 ? "HIT" : "miss", d_n > 0 ? "HIT" : "miss",
         (double)resid_mm, past, g_tick);
}

static void cmd_netstall(sim_ctx_t *s) {
  (void)s;
  // The input-queue bounds, proven against the LIVE queue machinery (the real
  // net_srv_queue_input + net_srv_consume_inputs on a scratch connection), not
  // against a copy of the rules.
  long gap = opt_n(60);
  net_conn_t csave = g_conns[0];
  int bsave = g_srv_in_budget[1];
  unsigned dsave = g_srv_dropped_inputs;
  int hsave[IN_COUNT]; memcpy(hsave, g_human_held[1], sizeof hsave);
  float ysave = g_players[1].yaw, psave = g_players[1].pitch;
  memset(&g_conns[0], 0, sizeof g_conns[0]);
  g_conns[0].used = 1; g_conns[0].human = 1;
  g_srv_in_budget[1] = 0;
  net_input_t in = {.buttons = (uint16_t)(1u << IN_FIRE)};
  int fresh = 0;                       // A: ticks 0..3, sent twice
  for (int rep = 0; rep < 2; rep++)
    for (uint32_t tk = 0; tk < 4; tk++) {
      in.tick = tk;
      fresh += net_srv_queue_input(&g_conns[0], 1, &in);
    }
  for (int k = 0; k < 6; k++)          // B: 6 world ticks vs 4 inputs
    net_srv_consume_inputs();
  int hold = g_human_held[1][IN_FIRE]; // ...the 2-tick gap held the state
  uint32_t applied = g_conns[0].last_input_tick;
  unsigned drop0 = g_srv_dropped_inputs;  // C: 200-tick same-second burst
  int acc = 0;
  in.buttons = 0;
  for (uint32_t tk = 4; tk < 204; tk++) {
    in.tick = tk;
    acc += net_srv_queue_input(&g_conns[0], 1, &in);
  }
  int rate_drop = (int)(g_srv_dropped_inputs - drop0);
  memset(&g_conns[0], 0, sizeof g_conns[0]);   // D: far-ahead jump
  g_conns[0].used = 1; g_conns[0].human = 1;
  g_srv_in_budget[1] = 0;
  in.tick = 0; net_srv_queue_input(&g_conns[0], 1, &in);
  drop0 = g_srv_dropped_inputs;
  in.tick = (uint32_t)gap + NET_IN_MAX;
  net_srv_queue_input(&g_conns[0], 1, &in);
  int skip = (int)(g_srv_dropped_inputs - drop0);
  // E: the backlog TRIM — the path the live WAN rubber-banding rode, and the one
  // the old netstall never touched.
  memset(&g_conns[0], 0, sizeof g_conns[0]);
  g_conns[0].used = 1; g_conns[0].human = 1;
  g_srv_in_budget[1] = 0;
  memset(g_human_held[1], 0, sizeof g_human_held[1]);
  unsigned trim0 = g_srv_trim_drops;
  for (uint32_t tk = 0; tk < (uint32_t)NET_IN_BACKLOG + 4; tk++) {
    in.tick = tk;
    in.buttons = tk == 1 ? (uint16_t)(1u << IN_FIRE) : 0;  // press inside
    net_srv_queue_input(&g_conns[0], 1, &in);              // the trim zone
  }
  net_srv_consume_inputs();
  int trimmed_n = (int)(g_srv_trim_drops - trim0);
  int carry_ok = g_human_held[1][IN_FIRE];   // edge survived the trim
  uint32_t trim_applied = g_conns[0].last_input_tick;
  printf("netstall dup_free=%d applied_tick=%u gap_hold=%s | burst "
         "accepted=%d dropped=%d | jump_skipped=%d | trim trimmed=%d "
         "applied=%u carry=%s (cap=%d backlog=%d rate=%d/s redun=%d)\n",
         fresh, applied, hold ? "ok" : "LOST", acc, rate_drop, skip,
         trimmed_n, trim_applied, carry_ok ? "ok" : "LOST",
         NET_IN_MAX, NET_IN_BACKLOG,
         TICK_HZ * NET_IN_RATE_NUM / NET_IN_RATE_DEN, NET_IN_REDUN);
  g_conns[0] = csave;                  // leave no trace in the session
  g_srv_in_budget[1] = bsave; g_srv_dropped_inputs = dsave;
  memcpy(g_human_held[1], hsave, sizeof hsave);
  g_players[1].yaw = ysave; g_players[1].pitch = psave;
  g_human_move[1] = 1;                 // back to the always-move default
  g_human_view[1] = -1;                // and no stale lag-comp anchor
  g_srv_trim_drops = trim0;            // segment E leaves no trace either
}

static void cmd_netanim(sim_ctx_t *s) {
  // Foreign anim_tick from a DECIMATED snapshot stream: run one puppet at 120 Hz
  // counting plants, then re-drive a fresh anim from position samples taken every K
  // ticks (a 60 Hz snapshot with loss) and count plants there.
  long n = next_n();
  int stride = (int)opt_n(2);      // 2 = 60 Hz from 120 Hz
  if (stride < 1) stride = 1;
  // reference: the puppet already running (bot 0), count its EV_STEP via a
  // plant-edge detector on foot t crossing.
  int plants_full = 0, plants_dec = 0;
  anim_t ra; anim_reset(&ra, g_bots[0].pos, g_bots[0].yaw, g_bots[0].wp.cur);
  v3 pprev = g_bots[0].pos;   // yaw comes live from the puppet, not sampled
  int last_stance = -1, last_stance_d = -1;
  for (long i = 0; i < n; i++) {
    sim_advance(s);
    // full-rate re-solve from the live puppet's own state
    int st = g_bots[0].anim.foot[0].t < g_bots[0].anim.foot[1].t ? 0 : 1;
    if (last_stance >= 0 && st != last_stance) plants_full++;
    last_stance = st;
    // decimated: only every `stride` ticks feed a fresh solver, with
    // velocity from the position delta (what a client reconstructs)
    if (i % stride == 0) {
      v3 pos = g_bots[0].pos;
      v3 vel = v3_scale(v3_sub(pos, pprev), (float)TICK_HZ / stride);
      anim_tick(&ra, pos, vel, g_bots[0].yaw, g_bots[0].pitch,
                g_bots[0].grounded, 0, 0.0f, 0, 0.0f, 0.0f, 0, 1,
                g_bots[0].wp.cur, 0);
      int sd = ra.foot[0].t < ra.foot[1].t ? 0 : 1;
      if (last_stance_d >= 0 && sd != last_stance_d) plants_dec++;
      last_stance_d = sd;
      pprev = pos;
    }
  }

  // A snapshot does not carry the presentation-only lean foot. Re-solve that
  // absolute target from the existing quantized lean level, once with a full-rate
  // stream and once with a level held for `stride` ticks. This is the production
  // LEAN_IN/OUT ramp including a direct sign change; slowing it down or inserting a
  // neutral dwell would hide the exact transition a remote figure has to survive.
  const int lean_n = 540;
  anim_t af, ad;
  anim_reset(&af, SPAWN_POS, 0.0f, WPN_AR);
  anim_reset(&ad, SPAWN_POS, 0.0f, WPN_AR);
  afoot_t foot_f[2], foot_d[2];
  memcpy(foot_f, af.foot, sizeof foot_f);
  memcpy(foot_d, ad.foot, sizeof foot_d);
  float phase_f = af.phase, phase_d = ad.phase;
  float source_lean = 0.0f, dec_lean = 0.0f, max_ankle = 0.0f;
  float max_total_ankle = 0.0f;
  float max_full_lean = 0.0f, max_dec_lean = 0.0f;
  float max_full_step = 0.0f, max_dec_step = 0.0f;
  int max_tick = -1, max_total_tick = -1, max_side = -1;
  int max_active_full = -1, max_active_dec = -1;
  int side_mismatch = 0, plant_flicker = 0, seen_f = 0, seen_d = 0;
  int lean_scan_ok = 1;

  event_t ev_save[MAX_EVENTS];
  long ev_tick_save[MAX_EVENTS];
  unsigned char ev_feedback_save[MAX_EVENTS];
  memcpy(ev_save, g_events, sizeof ev_save);
  memcpy(ev_tick_save, g_event_tick, sizeof ev_tick_save);
  memcpy(ev_feedback_save, g_event_feedback, sizeof ev_feedback_save);
  int nev_save = g_num_events, peak_save = g_ev_peak, predicting_save = g_predicting;
  int replay_save = g_prediction_replay;
  int online_save = g_online, local_save = g_local_ent;
  long tick_save = g_tick, drops_save = g_ev_drops;
  bot_noise_t noise_save[BOT_NOISE_COUNT][MAX_ENTS];
  memcpy(noise_save, g_match.noise, sizeof noise_save);
  long scans_save = g_lean_step_scans;
  v3 want_save = g_lean_step_want_off, got_save = g_lean_step_got_off;
  float ratio_save = g_lean_step_ratio;
  int diag_side_save = g_lean_step_side;
  g_num_events = 0;
  g_ev_drops = 0;
  g_predicting = 0;
  g_prediction_replay = 0;
  // Both solvers model foreign figures. The production path applies its short
  // snapshot-hold presentation follower only to this online/non-local case.
  g_online = 1;
  g_local_ent = -1;

  for (int i = 0; i < lean_n; i++) {
    float target = i < 120 ? 1.0f : i < 360 ? -1.0f : 0.0f;
    float rate = fabsf(target) > fabsf(source_lean) ? LEAN_IN_RATE : LEAN_OUT_RATE;
    source_lean += (target - source_lean) * ease_k(rate);
    float lean = net_dq_wish(net_q_wish(source_lean));
    // The decimated side sees exactly what the live wire publishes: signed 16-bit
    // wish quantization followed by a held level between snapshots.
    if (i % stride == 0) dec_lean = net_dq_wish(net_q_wish(lean));
    g_tick++;
    long scan_f0 = g_lean_step_scans;
    anim_tick(&af, SPAWN_POS, (v3){{0, 0, 0}}, 0.0f, 0.0f,
              1, 0, 0.0f, 0, 0.0f, lean, 0, 0, WPN_AR, 1);
    if (g_lean_step_scans - scan_f0 > 1) lean_scan_ok = 0;
    long scan_d0 = g_lean_step_scans;
    anim_tick(&ad, SPAWN_POS, (v3){{0, 0, 0}}, 0.0f, 0.0f,
              1, 0, 0.0f, 0, 0.0f, dec_lean, 0, 1, WPN_AR, 1);
    if (g_lean_step_scans - scan_d0 > 1) lean_scan_ok = 0;
    int sf = af.lean_step_k > 1e-4f ? 1 : af.lean_step_k < -1e-4f ? -1 : 0;
    int sd = ad.lean_step_k > 1e-4f ? 1 : ad.lean_step_k < -1e-4f ? -1 : 0;
    // Control solve: remove only the new presentation step from otherwise identical
    // states. The isolated delta identifies this feature's share, while the plan's
    // 5 mm contract remains hard on the complete solved ankle as well.
    anim_t zf = af, zd = ad;
    zf.lean_step_off = zd.lean_step_off = (v3){{0, 0, 0}};
    zf.lean_step_k = zd.lean_step_k = 0.0f;
    skel_solve(&zf, SPAWN_POS, 0.0f, 0.0f, WPN_AR, 1, SKEL_POSE_ONLY);
    skel_solve(&zd, SPAWN_POS, 0.0f, 0.0f, WPN_AR, 1, SKEL_POSE_ONLY);
    for (int side = 0; side < 2; side++) {
      float total = v3_len(v3_sub(af.pose.ankle[side], ad.pose.ankle[side]));
      if (total > max_total_ankle) {
        max_total_ankle = total;
        max_total_tick = i;
      }
      v3 add_f = v3_sub(af.pose.ankle[side], zf.pose.ankle[side]);
      v3 add_d = v3_sub(ad.pose.ankle[side], zd.pose.ankle[side]);
      float added = v3_len(v3_sub(add_f, add_d));
      if (added > max_ankle) {
        max_ankle = added;
        max_tick = i;
        max_side = side;
        max_active_full = sf;
        max_active_dec = sd;
        max_full_lean = lean;
        max_dec_lean = dec_lean;
        max_full_step = af.lean_step_k;
        max_dec_step = ad.lean_step_k;
      }
    }
    if (sf > 0) seen_f |= 1; else if (sf < 0) seen_f |= 2;
    if (sd > 0) seen_d |= 1; else if (sd < 0) seen_d |= 2;
    if (sf && sd && sf != sd &&
        fminf(v3_len(af.lean_step_off), v3_len(ad.lean_step_off)) >= 0.005f)
      side_mismatch++;
    if (memcmp(af.foot, foot_f, sizeof foot_f) || af.phase != phase_f ||
        memcmp(ad.foot, foot_d, sizeof foot_d) || ad.phase != phase_d)
      plant_flicker++;
  }
  int extra_events = g_num_events;
  int step_events = 0;
  for (int i = 0; i < g_num_events; i++)
    if (g_events[i].type == EV_STEP) step_events++;
  int lean_ok = max_ankle <= 0.005f && max_total_ankle <= 0.005f &&
                side_mismatch == 0 &&
                seen_f == 3 && seen_d == 3 && plant_flicker == 0 &&
                extra_events == 0 && step_events == 0 && g_ev_drops == 0 &&
                lean_scan_ok;

  memcpy(g_events, ev_save, sizeof ev_save);
  memcpy(g_event_tick, ev_tick_save, sizeof ev_tick_save);
  memcpy(g_event_feedback, ev_feedback_save, sizeof ev_feedback_save);
  g_num_events = nev_save;
  g_ev_peak = peak_save;
  g_ev_drops = drops_save;
  g_predicting = predicting_save;
  g_prediction_replay = replay_save;
  g_online = online_save;
  g_local_ent = local_save;
  memcpy(g_match.noise, noise_save, sizeof noise_save);
  g_tick = tick_save;
  g_lean_step_scans = scans_save;
  g_lean_step_want_off = want_save;
  g_lean_step_got_off = got_save;
  g_lean_step_ratio = ratio_save;
  g_lean_step_side = diag_side_save;

  printf("netanim n=%ld stride=%d plants_full=%d plants_decimated=%d "
         "lean_ticks=%d ankle_add=%.2fmm@T%d/%s active_at_worst=%d/%d "
         "ankle_total=%.2fmm@T%d lean=%.3f/%.3f step=%+.3f/%+.3f "
         "active=%d/%d side_mismatch=%d "
         "plant_flicker=%d events=%d steps=%d scan=%d %s\n",
         n, stride, plants_full, plants_dec, lean_n,
         (double)(max_ankle * 1000.0f), max_tick, max_side == 1 ? "R" : "L",
         max_active_full, max_active_dec,
         (double)(max_total_ankle * 1000.0f), max_total_tick,
         (double)max_full_lean, (double)max_dec_lean,
         (double)max_full_step, (double)max_dec_step,
         seen_f, seen_d, side_mismatch,
         plant_flicker, extra_events, step_events, lean_scan_ok,
         lean_ok ? "ok" : "FAIL");
  if (!lean_ok) exit(1);
}

static void cmd_netfill(sim_ctx_t *s) {
  (void)s;
  // The fill and cap rules over a join/leave timeline: a join never passes cap_public,
  // bots keep a lonely human company up to bot_fill and never past cap_bots, an empty
  // arena holds NONE, and — the half a fixed-count model cannot express — the company
  // bot COMES BACK when the second human goes.
  int cb = g_srv.cap_bots, cp = g_srv.cap_public, bf = g_srv.bot_fill;
  int humans = 0, bots = net_bot_target(0, bf, cb);   // empty server
  int bots_empty = bots;                              // must be 0: nobody to keep company
  int bots_lonely = -1;                               // bots seen at exactly 1 human
  int max_public = 0, max_arena = bots;
  int over_public = 0, bots_over = 0, arena_over = 0;
  const int seq[] = {0,0,0,0,0,0,0,0,0,0,0, -1,-1,-1,-1,-1,-1,-1};
  for (int i = 0; i < (int)(sizeof seq / sizeof seq[0]); i++) {
    if (seq[i] < 0) { if (humans > 0) humans--; }
    else if (net_join_ok(humans, cp)) humans++;
    bots = net_bot_target(humans, bf, cb);   // the one rule, every step
    if (humans > cp) over_public++;
    if (bots > cb || bots < 0) bots_over++;
    if (humans + bots > cp) arena_over++;
    if (humans == 1) bots_lonely = bots;
    if (humans > max_public) max_public = humans;
    if (humans + bots > max_arena) max_arena = humans + bots;
  }
  int inv = net_join_ok(0, cp) && bf >= 0 && bf <= cp;
  printf("netfill cap_bots=%d cap_public=%d bot_fill=%d inv(caps)=%s "
         "public_max=%d arena_max=%d over_public=%d bots_over=%d "
         "arena_over=%d bots_empty=%d bots_lonely=%d bots_end=%d\n",
         cb, cp, bf, inv ? "ok" : "FAIL", max_public, max_arena,
         over_public, bots_over, arena_over, bots_empty, bots_lonely, bots);
}

// Stage a server's roster into the local world: three foreign humans with names,
// bodies, score rows, pings and spawn seqs, and one of them is us.
static void netleave_stage(void) {
  int me = 2;                       // the slot a server would have given us
  for (int h = 1; h <= 3; h++) {
    g_humans_on[h] = 1;
    // %c, not %d: h is 1..3 here, and the -O1 build (the ASan one) cannot
    // prove that — it reported a -Wformat-truncation the -O2 gate does not.
    snprintf(g_human_name[h], sizeof g_human_name[h], "GHOST%c", (char)('0' + h));
    g_players[h].alive = 1;
    g_players[h].hp = 100;
    int e = ent_of_human(h);
    g_match.kills[e] = 7; g_match.deaths[e] = 3;
    g_match.shots[e] = 50; g_match.hits[e] = 9;
    g_net_ping[e] = 42;
    g_spawn_seq[e] = 5;
    g_human_move[h] = 0;
  }
  snprintf(g_human_name[me], sizeof g_human_name[me], "%s", g_cfg.name);
  g_online = 1;
  g_local_ent = ent_of_human(me);
  ev_push((event_t){.type = EV_FIRE, .wpn = WPN_AR, .src = ent_of_human(me),
                    .a = {{0, 1, 0}}, .b = {{0, 0, -1}}, .f = 1});
}

// ...and count what survived it. Every number here must be 0, and the DESTINATION is
// part of the contract: the teardown ends on the entry screen — menu up, boot screen
// running, ROOT page — never in a local round the player never asked to start.
static void netleave_report(const char *via, int want_reject) {
  int ghosts = 0, names = 0, rows = 0, pings = 0, seqs = 0, moved = 0;
  for (int h = 1; h < MAX_HUMANS; h++) {
    if (g_humans_on[h]) ghosts++;
    if (g_human_name[h][0]) names++;
    if (!g_human_move[h]) moved++;
    int e = ent_of_human(h);
    if (g_match.kills[e] || g_match.deaths[e] ||
        g_match.shots[e] || g_match.hits[e]) rows++;
    if (g_net_ping[e]) pings++;
    if (g_spawn_seq[e]) seqs++;
  }
  // A ghost is walkable by ent_next, so the roster walk is the honest
  // check: it is what makes one shootable, rendered and scored.
  int walked = 0;
  for (int e = ent_next(-1); e >= 0; e = ent_next(e)) if (e > MAX_BOTS) walked++;
  int rail = g_menu_open && g_home && g_ui_page == UI_PAGE_ROOT;
  int stale_events = 0;
  for (int i = 0; i < g_num_events; i++)
    stale_events += g_events[i].src > MAX_BOTS;
  if (stale_events) printf("netleave stale_events=%d FAIL\n", stale_events);
  printf("netleave via=%s ghosts=%d walked=%d names=%d rows=%d pings=%d seq=%d "
         "move_stuck=%d online=%d local_ent=%d self=%d rail=%d reject=%d\n",
         via, ghosts, walked, names, rows, pings, seqs, moved, g_online,
         g_local_ent, g_humans_on[0], rail, g_cl.reject);
  // THE EXIT CODE IS THE READING. Every count above is a leak of the server's world
  // into the local one, and `rail` is where the player ended up — a red line that
  // still exits 0 is a gate that passes a broken teardown.
  if (stale_events || ghosts || walked || names || rows || pings || seqs || moved || g_online ||
      g_local_ent || !g_humans_on[0] || !rail || g_cl.reject != want_reject)
    exit(1);
}

static void cmd_netcarry(sim_ctx_t *s) { (void)s; netcarry_proof(); }
static void cmd_netfigure(sim_ctx_t *s) { (void)s; netfigure_proof(); }

static void cmd_netleave(sim_ctx_t *s) {
  // THE ONLINE->LOCAL TRANSITION, as a gate — and BOTH ways into it, because they
  // share one teardown and differ only in what the player is told afterwards.
  //   netleave        the player pressed LEAVE MATCH: a clean exit, nothing to report
  //   netleave drop   the server went quiet: app_frame's NC_FAILED branch runs the
  //                   same teardown and re-latches the verdict so the rail's
  //                   MULTIPLAYER tile can say why the arena vanished
  char *how = take_tok();
  int drop = how && !strcmp(how, "drop");
  if (how && !drop) g_tok_pushback = how;
  netleave_stage();
  if (drop) {
    // The state a 5 s silence leaves behind, minus the socket: net_cl_poll returns at
    // its own `fd < 0` guard, exactly as it does once the descriptor is gone.
    g_cl.state = NC_FAILED;
    g_cl.fd = -1;
    g_cl.reject = 0;
    app_t app = {0};
    frame_input_t in = {0};
    s->tick += app_frame(&app, s->p, TICK_DT, &in);
    netleave_report("drop", NR_LOST);
  } else {
    leave_match();
    netleave_report("leave", 0);
  }
}

// Drive actual menu activation and app_frame. This deliberately exercises a
// destructive new-arena action; it restores preferences, not the previous match.
static int spstart_check(int condition, const char *what, int state, int device) {
  if (!condition) printf("spstart check=%s state=%d device=%d FAIL\n", what, state, device);
  return condition;
}
static int spstart_activate(sim_ctx_t *s, int device, int row) {
  g_ui_mdown = 0;
  g_ui_cx = g_ui_cy = -40;
  memset(&g_pad, 0, sizeof g_pad);
  pad_sync(0);
  harness_uiframe(s);
  if (row < 0 || row >= g_focus_rn) return 0;
  g_ui_focus = row;
  if (device == 0) {
    focus_rect_t box = g_focus_r[row];
    g_ui_cx = (box.x + box.w * 0.5f) * g_uis;
    g_ui_cy = (box.y + box.h * 0.5f) * g_uis;
    g_ui_mdown = 1;
  } else if (device == 1) {
    g_nav_kbd = 1;
    nav_push(NAV_OK);
  } else {
    g_pad.connected = 1;
    g_pad.btn[PB_A] = 1;
    pad_sync(TICK_DT);
  }
  harness_uiframe(s);
  // A second render pass, or a held controller button, cannot accept twice.
  if (device == 2) pad_sync(TICK_DT);
  harness_uiframe(s);
  return 1;
}
// A real queued ACCEPT would replace the map before the next snapshot. It is
// intentionally a different valid map so moving the transition below poll fails.
static int spstart_late_accept(uint32_t seed, int *sender) {
  int receiver = net_udp_open(0);
  *sender = net_udp_open(0);
  if (receiver < 0 || *sender < 0) return -1;
  struct sockaddr_in a = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
  if (bind(receiver, (struct sockaddr *)&a, sizeof a) ||
      bind(*sender, (struct sockaddr *)&a, sizeof a)) return -1;
  socklen_t length = sizeof a;
  if (getsockname(receiver, (struct sockaddr *)&a, &length)) return -1;
  uint16_t receiver_port = ntohs(a.sin_port);
  if (getsockname(*sender, (struct sockaddr *)&a, &length)) return -1;
  g_cl.fd = receiver; g_cl.ip = htonl(INADDR_LOOPBACK); g_cl.port = ntohs(a.sin_port);
  g_cl.state = NC_JOINING;
  uint32_t late_seed = seed ^ 0xDEADBEEFu;
  map_generate(late_seed); g_map_seed = late_seed;
  uint32_t world = net_world_hash();
  map_generate(seed); g_map_seed = seed;
  unsigned char packet[512]; np_wr w = {packet, sizeof packet, 0, 0};
  np_w16(&w, NET_PROTO); np_w8(&w, NP_ACCEPT);
  np_w8(&w, (uint8_t)ent_of_human(2)); np_w8(&w, 0); np_w8(&w, 1);
  np_w32(&w, late_seed); np_w32(&w, world); np_w32(&w, 17);
  for (int i = 0; i < MV_COUNT; i++) np_wf(&w, g_cfg.mv[i]);
  for (int i = 0; i < GP_COUNT; i++) np_wf(&w, g_cfg.gun[i]);
  for (int wp = 0; wp < WPN_COUNT; wp++)
    for (int i = 0; i < WP_COUNT; i++) np_wf(&w, g_cfg.wp[wp][i]);
  if (w.bad || net_udp_send(*sender, g_cl.ip, receiver_port, packet, (int)w.at) != (int)w.at) return -1;
  return receiver;
}
static int spstart_audio_proof(void) {
  int ok = 1;
  float pcm[1024];
  audio_session_reset();
  audio_mix(pcm, 512, 2);
  audio_play_tag(SFX_RELOAD_AR, 0.8f, 1, 0, 0, 1);
  audio_play_tag(SFX_RELOAD_AR, 0.8f, 1, 0, 1, 1);
  audio_mix(pcm, 512, 2);
  int old = 0;
  for (int i = 0; i < MAX_VOICES; i++) old += g_voices[i].active != 0;
  ok &= old == 2;
  // Fill the producer ring with stale delayed cues. Reset must survive overflow.
  for (int i = 0; i < TRIG_RING; i++) audio_play_tag(SFX_RELOAD_AR, 0.8f, 1, 0, 1, 1);
  uint32_t old_w = atomic_load(&g_trig_w);
  audio_session_reset();
  ok &= atomic_load(&g_trig_r) != old_w;
  audio_mix(pcm, 512, 2);
  for (int i = 0; i < MAX_VOICES; i++) ok &= !g_voices[i].active;
  for (int i = 0; i < 1024; i++) ok &= pcm[i] == 0;
  // The callback captured the old epoch, then the producer reset and published
  // a new cue before its ring snapshot. Per-trigger epoch observation owns it.
  audio_mix_sync_epoch();
  audio_session_reset();
  audio_play(SFX_HIT, 0.8f, 1, 0, 0);
  audio_mix_admit_until(audio_mix_snapshot());
  int fresh = 0;
  for (int i = 0; i < MAX_VOICES; i++) fresh += g_voices[i].active != 0;
  ok &= fresh == 1;
  audio_mix(pcm, 512, 2);
  double energy = 0;
  for (int i = 0; i < 1024; i++) energy += (double)pcm[i] * pcm[i];
  ok &= energy > 0.000001;
  // A reset after the callback's write-index snapshot must not skip a newly
  // published trigger. Consumer progress stops at precisely its own snapshot.
  audio_snapshot_t old_end = audio_mix_snapshot();
  audio_session_reset();
  audio_play(SFX_HIT_HEAD, 0.8f, 1, 0, 0);
  audio_mix_admit_until(old_end);
  ok &= atomic_load(&g_trig_r) == old_end.normal &&
        atomic_load(&g_protected_r) == old_end.protected &&
        atomic_load(&g_protected_w) == old_end.protected + 1;
  audio_mix(pcm, 512, 2);
  fresh = 0;
  for (int i = 0; i < MAX_VOICES; i++) {
    fresh += g_voices[i].active != 0;
    if (g_voices[i].active) ok &= g_voices[i].buf == g_sfx_buf[SFX_HIT_HEAD];
  }
  ok &= fresh == 1;
  audio_session_reset();
  audio_mix(pcm, 512, 2);
  printf("spstart audio old=%d reset_full_ring=1 fresh=%d energy=%.6f races=2 %s\n",
         old, fresh, energy, ok ? "ok" : "FAIL");
  return ok;
}
static int spstart_stick_proof(void) {
  pad_state_t saved = g_pad;
  float dz_l = g_cfg.pad[PAD_DZ_L], dz_r = g_cfg.pad[PAD_DZ_R];
  int saved_l = g_sp_eat_l, saved_r = g_sp_eat_r;
  int saved_eat[IN_COUNT]; memcpy(saved_eat, g_sp_eat, sizeof saved_eat);
  int ok = 1, cases = 0;
  const float zones[] = {0.0f, 0.01f, 0.04f, 0.20f};
  for (int z = 0; z < 4; z++) {
    g_cfg.pad[PAD_DZ_L] = g_cfg.pad[PAD_DZ_R] = zones[z];
    frame_input_t input = {0};
    memset(&g_pad, 0, sizeof g_pad); g_pad.connected = 1;
    g_sp_eat_l = g_sp_eat_r = 1;
    g_pad.lx = .8f; g_pad.ry = -.7f;
    sp_filter_input(&input);
    ok &= g_sp_eat_l && g_sp_eat_r && !g_pad.lx && !g_pad.ry;
    // Rest noise must release both menu gestures, including deadzone-off.
    g_pad.lx = .032f; g_pad.ly = -.026f; g_pad.rx = -.045f; g_pad.ry = .018f;
    sp_filter_input(&input);
    ok &= !g_sp_eat_l && !g_sp_eat_r;
    g_pad.lx = .65f; g_pad.ry = -.62f;
    sp_filter_input(&input);
    ok &= g_pad.lx == .65f && g_pad.ry == -.62f;
    // A newly neutral left stick cannot unlock a still-held right stick.
    g_sp_eat_l = g_sp_eat_r = 1;
    g_pad.lx = .02f; g_pad.ry = -.8f;
    sp_filter_input(&input);
    ok &= !g_sp_eat_l && g_sp_eat_r && !g_pad.ry;
    cases++;
  }
  g_pad = saved; g_cfg.pad[PAD_DZ_L] = dz_l; g_cfg.pad[PAD_DZ_R] = dz_r;
  g_sp_eat_l = saved_l; g_sp_eat_r = saved_r;
  memcpy(g_sp_eat, saved_eat, sizeof saved_eat);
  printf("spstart sticks cases=%d rest_noise=1 independent=1 %s\n", cases, ok ? "ok" : "FAIL");
  return ok;
}

static void cmd_spstart(sim_ctx_t *s) {
  float saved_device_rate = g_dev_rate;
  if (!audio_mix_device_reset(SFX_HZ, SFX_CH)) fatal("invalid harness audio format");
  int saved_bots = g_cfg.sp_bots, saved_diff = g_cfg.sp_diff, saved_frag = g_cfg.sp_frag;
  sp_rules_t active_saved = g_sp_rules_all[g_lobby];
  unsigned char ready_saved = g_sp_rules_ready[g_lobby];
  int ephemeral = g_cfg_ephemeral;
  pad_state_t pad_saved = g_pad;
  g_cfg_ephemeral = 1; // A proof must not persist its deliberately edited preferences.
  int keys_saved[KEY_COUNT];
  memcpy(keys_saved, g_keys, sizeof keys_saved);
  int ok = spstart_check(g_cl.state != NC_OFF || g_cl.fd == -1, "offline_descriptor", -1, -1);
  ok &= spstart_audio_proof();
  ok &= spstart_stick_proof();
  double elapsed;
  uint64_t nan_bits = UINT64_C(0x7FF8000000000001);
  double nan_time; memcpy(&nan_time, &nan_bits, sizeof nan_time);
  ok &= spstart_check(sp_elapsed_ms(1, 1.25, &elapsed) && elapsed == 250 &&
      !sp_elapsed_ms(2, 1, &elapsed) && !sp_elapsed_ms(-1, 1, &elapsed) &&
      !sp_elapsed_ms(nan_time, 1, &elapsed) && !sp_elapsed_ms(1, nan_time, &elapsed),
      "monotonic_timing", -1, -1);
  int cases = 0;
  const char *names[] = {"home", "alive", "dead", "over", "connecting", "online"};
  for (int state = 0; state < 6; state++) for (int device = 0; device < 3; device++) {
    g_online = 0; g_local_ent = 0;
    g_cl.fd = -1; g_cl.state = NC_OFF;
    g_home = state == 0;
    s->p = local_player();
    ui_open_menu();
    if (g_home) {
      home_enter(s->p);
      g_home_t0 = g_ui_time;
      home_stage(s->p);
    }
    g_cfg.sp_bots = 3; g_cfg.sp_diff = device; g_cfg.sp_frag = 71;
    *sp_rules() = (sp_rules_t){1, 1, 99};
    s->p->alive = state != 2;
    s->p->shots = 90;
    s->p->wp.cur = WPN_SR; s->p->wp.reload_t = 50;
    g_match.over = state == 3;
    g_match.kills[0] = 8;
    g_match.shots[0] = 90;
    g_match.restart_req = 1;
    g_hitmark_arm = 20; g_hitmark_pend = HM_KILL;
    g_muzzle_t = g_muzzle_fresh = 5;
    g_hitstop = 10;
    g_bots_frozen = 1;
    if (state >= 4) {
      netleave_stage();
      g_cl.state = state == 4 ? NC_JOINING : NC_PLAYING;
      g_online = state == 5;
      g_cl.fd = -1;
      g_sim_mv = g_cl.srv_mv; g_sim_gun = g_cl.srv_gun; g_sim_wp = g_cl.srv_wp;
      s->p = &g_players[2];
    }
    uint32_t seed = g_map_seed;
    int late_sender = -1, late_receiver = -1;
    if (state == 4) {
      late_receiver = spstart_late_accept(seed, &late_sender);
      ok &= spstart_check(late_receiver >= 0, "late_accept_queued", state, device);
    }
    float stepped_pcm[1024];
    audio_session_reset();
    audio_mix(stepped_pcm, 512, 2);
    audio_play_tag(SFX_RELOAD_AR, 0.8f, 1, 0, 0, 17);
    audio_play_tag(SFX_RELOAD_AR, 0.8f, 1, 0, 1, 17);
    atomic_store(&g_audio_pause,0);
    audio_acoustic_publish((acoustic_params_t){.delay = {.010f,.024f}, .early = {.30f,.30f},
      .late = .22f, .feedback = .65f, .damping = 3500, .kind = ACOUSTIC_SHELTER});
    for (int block = 0; block < 16; block++) audio_mix(stepped_pcm,512,2);
    double old_wet = 0;
    for (unsigned sample = 0; sample < AUDIO_WET_RING; sample++)
      old_wet += (double)g_audio_wet.early[0][sample] * g_audio_wet.early[0][sample];
    ok &= spstart_check(old_wet > 1e-7 && g_audio_wet.late_gain > .05f,
                       "old_room_staged",state,device);
    int old_voices = 0;
    for (int i = 0; i < MAX_VOICES; i++) old_voices += g_voices[i].active && g_voices[i].tag == 17;
    ok &= spstart_check(old_voices == 2, "old_audio_staged", state, device);
    for (int i = 0; i < TRIG_RING; i++) audio_play_tag(SFX_RELOAD_AR, 0.8f, 1, 0, 1, 17);
    // Later authoritative perception joins the same new-arena reset contract.
    g_bots[0].suppress = (bot_suppression_t){.point = {{9, 1, 4}},
        .intensity = 900, .hold = 70, .planned = 1, .cover = 1};
    g_match.pending[0] = (bot_stimulus_t){.score = 900, .tick = g_tick};
    g_match.published[0] = (bot_stimulus_t){.score = 800, .tick = g_tick - 1};
    uint64_t serial = g_sp_serial;
    uint32_t epoch = atomic_load(&g_audio_epoch);
    ok &= spstart_check(spstart_activate(s, device, 0), "activate", state, device);
    ok &= spstart_check(g_sp_pending && g_sp_serial == serial && g_map_seed == seed,
                        "queued_boundary", state, device);
    frame_input_t in = {.mouse_dx = 400, .mouse_dy = -300};
    int binds[IN_COUNT], pbinds[IN_COUNT];
    resolve_binds(binds, pbinds);
    memset(g_keys, 0, sizeof g_keys);
    memset(g_pressed, 0, sizeof g_pressed);
    if (device == 0) g_keys[KEY_MOUSE1] = 1;
    if (device == 1) g_keys[0x0D] = 1;
    if (device == 2) {
      g_keys[KEY_PAD0 + PB_A] = 1;
      g_pad.lx = g_pad.rx = 0.8f;
    }
    if (state == 0) {
      // Select a real authored fire/reload beat, then capture the native input
      // resolver's director substitution. No proof-only synthetic HOME actions.
      int beat = device == 1 ? 2 : 3;
      float time = HOME_BEAT[beat].t0 + HOME_BEAT[beat].hold * 0.4f;
      for (int i = 0; i < HOME_NSHOT; i++) {
        int shot = home_slot(0, i);
        if (shot == HOME_BEAT[beat].shot) break;
        time += HOME_SHOT[shot].len;
      }
      g_home_t0 = g_ui_time - time;
    } else {
      g_keys[binds[IN_FIRE]] = g_keys[binds[IN_RELOAD]] = g_keys[binds[IN_SWAP]] = 1;
    }
    frame_input_keys(&in, binds, pbinds);
    if (state == 0) ok &= spstart_check(in.held[device == 1 ? IN_RELOAD : IN_FIRE],
                                       "real_director_input", state, device);
    app_t app = {.acc = 0.75};
    int ticks = app_frame(&app, s->p, TICK_DT, &in);
    s->p = local_player();
    memset(&s->cam, 0, sizeof s->cam);
    double ready_ms = g_sp_ready_ms;
    if (late_receiver >= 0) {
      struct sockaddr_in closed_address; socklen_t closed_length = sizeof closed_address;
      ok &= spstart_check(getsockname(late_receiver, (struct sockaddr *)&closed_address,
                                     &closed_length) < 0 && errno == EBADF,
                          "late_socket_closed", state, device);
    }
    if (late_sender >= 0) net_udp_close(late_sender);
    audio_mix(stepped_pcm, 512, 2);
    for (int i = 0; i < MAX_VOICES; i++) ok &= spstart_check(
        !(g_voices[i].active && g_voices[i].tag == 17), "old_audio_gone", state, device);
    int old_room = 0;
    for (unsigned sample = 0; sample < AUDIO_WET_RING; sample++) {
      old_room |= g_audio_wet.early[0][sample] != 0 || g_audio_wet.early[1][sample] != 0;
      for (int line = 0; line < 4; line++) old_room |= g_audio_wet.line[line][sample] != 0;
    }
    ok &= spstart_check(!old_room,"old_room_gone",state,device);
    audio_play_tag(SFX_HIT, 0.8f, 1, 0, 0, 18);
    audio_mix(stepped_pcm, 512, 2);
    double new_energy = 0;
    for (int i = 0; i < 1024; i++) new_energy += (double)stepped_pcm[i] * stepped_pcm[i];
    ok &= spstart_check(new_energy > 0.000001, "new_audio_audible", state, device);
    ok &= spstart_check(g_map_seed == map_seed_next(seed) && g_sp_serial == serial + 1 &&
        !g_sp_pending && ticks == 1, "one_seed_tick", state, device);
    ok &= spstart_check(!g_menu_open && !g_home && !g_online && g_local_ent == 0 &&
        g_cl.state == NC_OFF && g_cl.fd == -1 && g_sim_mv == g_cfg.mv &&
        g_sim_gun == g_cfg.gun && g_sim_wp == g_cfg.wp, "local_destination", state, device);
    ok &= spstart_check(v3_len(v3_sub(g_listen, player_eye_pos(local_player()))) < 0.000001f,
                        "fresh_presentation_player", state, device);
    ok &= spstart_check(s->p == &g_players[0] && s->p->alive && s->p->shots == 0 &&
        s->p->wp.cur == WPN_AR && !s->p->wp.reload_t && !s->p->wp.switch_t &&
        s->p->pitch == 0 && s->p->vel.x == 0 && s->p->vel.z == 0 &&
        !g_match.over && !g_match.restart_req && !g_match.kills[0] && !g_match.shots[0] &&
        !g_hitmark_arm && !g_hitmark_t && !g_muzzle_t && !g_muzzle_fresh && !g_hitstop,
        "fresh_first_tick", state, device);
    ok &= spstart_check(g_match.nbots == 3 && sp_bot_count() == 3 && sp_difficulty() == device &&
        sp_frag_limit() == 71 && g_cfg.sp_bots == 3 && g_cfg.sp_diff == device &&
        g_cfg.sp_frag == 71 && atomic_load(&g_audio_epoch) == epoch + 1, "rules_audio", state, device);
    for (int h = 1; h < MAX_HUMANS; h++) ok &= spstart_check(!g_humans_on[h] &&
        !g_human_name[h][0] && !g_players[h].alive && !g_net_ping[ent_of_human(h)],
        "ghost", state, device);
    for (int bi = 0; bi < MAX_BOTS; bi++)
      ok &= spstart_check(!g_bots[bi].suppress.intensity && !g_bots[bi].suppress.hold &&
          !g_bots[bi].suppress.cooldown && !g_match.pending[bi].score &&
          !g_match.published[bi].score, "fresh_authoritative_threats", state, device);
    bot_t bot = g_bots[0];
    frame_input_t released = {0};
    if (in.physical_held[IN_FIRE]) {
      frame_input_keys(&in, binds, pbinds);
      in.mouse_dx = in.mouse_dy = 0;
      app_frame(&app, s->p, TICK_DT, &in);
      ok &= spstart_check(s->p->shots == 0 && !s->p->wp.reload_t && s->p->wp.cur == WPN_AR,
                          "held_consumed", state, device);
      memset(g_keys, 0, sizeof g_keys);
      memset(&g_pad, 0, sizeof g_pad);
      pad_sync(0);
      app_frame(&app, s->p, TICK_DT, &released);
    }
    // The first newly owned input must work, without a countdown or stale latch.
    frame_input_t fire = {0};
    fire.physical_held[IN_FIRE] = fire.held[IN_FIRE] = fire.held_first[IN_FIRE] = 1;
    app_frame(&app, s->p, TICK_DT, &fire);
    ok &= spstart_check(s->p->shots == 1 && memcmp(&bot, &g_bots[0], sizeof bot) &&
        g_map_seed == map_seed_next(seed) && g_sp_serial == serial + 1,
        "ready_input_bots", state, device);
    // ESC and controller Back preserve the match; edits apply to the next arena.
    ui_on_escape();
    pad_sync(TICK_DT);
    harness_uiframe(s);
    ok &= spstart_check(g_ui_focus == 0 && !g_sp_pending,
                        "safe_esc_focus_held", state, device);
    uint32_t keep_seed = g_map_seed;
    sp_rules_t active = *sp_rules();
    ok &= spstart_check(spstart_activate(s, 1, 1), "settings_activate", state, device);
    ok &= spstart_check(g_ui_page == UI_PAGE_MATCH && !g_sp_pending,
                        "settings_page", state, device);
    // All three real widgets edit saved preferences, not active AI.
    for (int field = 0; field < 3; field++) {
      g_ui_focus = field; g_nav_kbd = 1; nav_push(NAV_RIGHT);
      harness_uiframe(s);
    }
    ok &= spstart_check(g_cfg.sp_bots == 4 && g_cfg.sp_frag == 72 &&
                        g_cfg.sp_diff == (device + 1) % 3,
                        "settings_edit_saved", state, device);
    g_cfg.sp_bots = 49;
    g_ui_focus = 0; g_nav_kbd = 1; nav_push(NAV_RIGHT); harness_uiframe(s);
    ok &= spstart_check(g_cfg.sp_bots == 50, "bots-slider-fiftieth-slot", state, device);
    nav_push(NAV_RIGHT); harness_uiframe(s);
    ok &= spstart_check(g_cfg.sp_bots == 50, "bots-slider-upper-stop", state, device);
    g_cfg.sp_bots = 1; nav_push(NAV_LEFT); harness_uiframe(s);
    ok &= spstart_check(g_cfg.sp_bots == 1, "bots-slider-lower-stop", state, device);
    g_ui_focus = 2; g_cfg.sp_frag = 100;
    nav_push(NAV_RIGHT); harness_uiframe(s);
    ok &= spstart_check(g_cfg.sp_frag == 0, "frag-slider-after-100-is-off", state, device);
    nav_push(NAV_RIGHT); harness_uiframe(s);
    ok &= spstart_check(g_cfg.sp_frag == 0, "frag-slider-off-upper-stop", state, device);
    nav_push(NAV_LEFT); harness_uiframe(s);
    ok &= spstart_check(g_cfg.sp_frag == 100, "frag-slider-off-back-to-100", state, device);
    g_cfg.sp_frag = 5; nav_push(NAV_LEFT); harness_uiframe(s);
    ok &= spstart_check(g_cfg.sp_frag == 5, "frag-slider-lower-stop", state, device);
    g_cfg.sp_bots = 4; g_cfg.sp_frag = 72;
    ui_on_back();
    ok &= spstart_check(g_ui_page == UI_PAGE_ROOT && g_ui_focus == 1,
                        "settings_back_focus", state, device);
    if (device == 2) ui_on_back();
    else ui_on_escape();
    ok &= spstart_check(!g_menu_open && g_map_seed == keep_seed &&
        !memcmp(sp_rules(), &active, sizeof active) && sp_bot_count() == active.bots &&
        sp_difficulty() == active.difficulty && sp_frag_limit() == active.fraglimit && !g_sp_pending,
        "resume_preserves", state, device);
    g_ui_mdown = 0;
    memset(&g_pad, 0, sizeof g_pad);
    pad_sync(0);
    app_frame(&app, s->p, TICK_DT, &released);
    printf("spstart via=%s device=%s seed=%u next=%u ticks=%d bots=%d ready_ms=%.3f %s\n",
           names[state], device == 0 ? "mouse" : device == 1 ? "enter" : "pad_a",
           seed, g_map_seed, ticks, g_match.nbots, ready_ms, ok ? "ok" : "FAIL");
    cases++;
    if (!ok) exit(1);
  }
  // The maximum local roster is real, and zero disables the actual end rule.
  g_cfg.sp_bots = 50; g_cfg.sp_frag = 0;
  sp_request_new_arena();
  app_t capacity_app = {0}; frame_input_t neutral = {0};
  app_frame(&capacity_app, local_player(), TICK_DT, &neutral);
  app_frame(&capacity_app, local_player(), TICK_DT, &neutral);
  int roster = 0, unique = 1;
  ENT_FOREACH(e) roster++;
  for (int i = 0; i < 50; i++) {
    unique &= g_bots[i].active && g_bots[i].alive && human_of_ent(i + 1) < 0;
    for (int j = 0; j < i; j++) unique &= strcmp(bot_name(i), bot_name(j)) != 0;
  }
  ok &= spstart_check(g_match.nbots == 50 && roster == 51 && unique &&
      ent_of_human(1) == 51 && human_of_ent(51) == 1,
      "fifty-live-bots-distinct-from-humans", 6, 0);
  g_match.kills[0] = 100000;
  match_on_kill(0, 50, 0);
  ok &= spstart_check(!g_match.over && g_match.kills[0] == 100001 &&
      g_match.deaths[50] == 1, "unlimited-keeps-scoring-past-any-menu-limit", 6, 0);
  sp_rules()->fraglimit = 5; g_match.kills[0] = 3;
  match_on_kill(0, 50, 0);
  ok &= spstart_check(!g_match.over, "frag-limit-before-threshold", 6, 0);
  match_on_kill(0, 50, 0);
  ok &= spstart_check(g_match.over && g_match.winner == 0,
      "frag-limit-at-threshold", 6, 0);
  printf("spstart rules bots=1..50 frags=5..100,off widgets=7 config=roundtrip scoring=unlimited %s\n",
         ok ? "ok" : "FAIL");
  // The existing result-overlay fire action is also an explicit new arena.
  g_match.over = 1;
  local_player()->prev_held[IN_FIRE] = 0;
  uint32_t result_seed = g_map_seed;
  uint64_t result_serial = g_sp_serial;
  frame_input_t result_input = {0};
  result_input.physical_held[IN_FIRE] = result_input.held[IN_FIRE] = result_input.held_first[IN_FIRE] = 1;
  app_t result_app = {0};
  app_frame(&result_app, local_player(), TICK_DT, &result_input);
  app_frame(&result_app, local_player(), TICK_DT, &result_input);
  ok &= spstart_check(g_map_seed == map_seed_next(result_seed) && g_sp_serial == result_serial + 1 &&
      !g_match.over && !local_player()->shots, "result_direct_held", 6, 0);
  // Arena and seed intentionally advance; independent rule preferences are restored.
  g_cfg.sp_bots = saved_bots; g_cfg.sp_diff = saved_diff; g_cfg.sp_frag = saved_frag;
  g_sp_rules_all[g_lobby] = active_saved;
  g_sp_rules_ready[g_lobby] = ready_saved;
  g_cfg_ephemeral = ephemeral;
  g_pad = pad_saved;
  memcpy(g_keys, keys_saved, sizeof keys_saved);
  g_dev_rate = saved_device_rate;
  printf("spstart summary cases=%d result=1 late_accept=3 audio_races=2 %s\n", cases, ok ? "ok" : "FAIL");
  if (!ok) exit(1);
}

static void cmd_tele(sim_ctx_t *s) {
  (void)s;
  // tele HOST PORT boot|ui|sp|mp [DT] — one telemetry packet at a --server, through
  // the SAME encoder the live client uses (one-copy rule).
  const char *host = next_tok();
  // RANGE-CHECKED, fatally, for the same reason the mode token is: the cast to
  // uint16_t is silent, so `tele H 70000 sp` would have gone to port 4464 and still
  // printed a confident "tele sent".
  long portl = next_n();
  if (portl < 1 || portl > 65535) {
    fprintf(stderr, "script: tele port %ld out of range (1..65535)\n", portl);
    exit(1);
  }
  int port = (int)portl;
  const char *mode = next_tok();
  // Enum-valued argument: a typo must not parse as a silent default.
  int kind = -1, m = 0;
  if      (!strcmp(mode, "boot")) { kind = 0; m = 0; }
  else if (!strcmp(mode, "ui"))   { kind = 1; m = 0; }
  else if (!strcmp(mode, "sp"))   { kind = 1; m = 1; }
  else if (!strcmp(mode, "mp"))   { kind = 1; m = 2; }
  else { fprintf(stderr, "script: unknown tele mode '%s' (boot|ui|sp|mp)\n", mode); exit(1); }
  int dt = (int)opt_n(60);
  if (dt < 0) dt = 0;
  if (dt > NET_TELE_DT_MAX) dt = NET_TELE_DT_MAX;   // u8 on the wire, and
                          // the server clamps at the same NET_TELE_DT_MAX;
                          // 300 would wrap to 44 and log a plausible lie
  uint32_t ip = net_resolve(host);
  int fd = ip ? net_udp_open(0) : -1;   // 0 = ephemeral local port
  if (fd < 0) { printf("tele fail resolve/socket\n"); }
  else {
    unsigned char b[NET_TELE_LEN];
    net_tele_encode(b, 0xC0FFEE01u, kind, m, kind ? dt : 0);
    // sendto's result is CHECKED. Discarding it made "tele sent" a claim about
    // intent rather than about the wire — the line printed happily when nothing had
    // left the box (unreachable route, EPERM from a firewall, EMSGSIZE), which is
    // the same class of silent lie as the truncated port above.
    int sent = net_udp_send(fd, ip, (uint16_t)port, b, NET_TELE_LEN);
    net_udp_close(fd);
    if (sent == NET_TELE_LEN) printf("tele sent %s dt=%d\n", mode, kind ? dt : 0);
    else printf("tele SEND FAIL %s dt=%d rc=%d (%s)\n", mode, kind ? dt : 0,
                sent, strerror(errno));
  }
}

static void cmd_humans(sim_ctx_t *s) {
  (void)s;
  // Test-roster control (the Phase-1 gate's two-human test): activate
  // human slots so the total is N. A joining slot gets the bot pattern of
  // a zeroed score row, fresh spawn — and a name; its input stays
  // neutral (g_human_held is zero), so it stands where it spawned.
  long n = next_n();
  if (n < 1) n = 1;
  if (n > MAX_HUMANS) n = MAX_HUMANS;
  for (int h = 1; h < MAX_HUMANS; h++) {
    int on = h < n;
    if (on && !g_humans_on[h]) {
      g_humans_on[h] = 1;
      int e = ent_of_human(h);
      g_match.kills[e] = g_match.deaths[e] = 0;
      g_match.shots[e] = g_match.hits[e] = 0;
      snprintf(g_human_name[h], sizeof g_human_name[h], "HUM%d", h & 7);
      // (& 7: value-range for -Wformat-truncation; MAX_HUMANS is 8)
      player_spawn(&g_players[h]);
      player_respawn(&g_players[h], e);
    } else if (!on && g_humans_on[h]) {
      g_humans_on[h] = 0;
      int e = ent_of_human(h);
      g_match.kills[e] = g_match.deaths[e] = 0;   // leave clears
      g_match.shots[e] = g_match.hits[e] = 0;     // the score row too
    }
  }
  printf("humans %ld\n", n);
}

static void cmd_localent(sim_ctx_t *s) {
  (void)s;
  // The instrument: the byte gate always runs with g_local_ent == 0 and is
  // blind to every g_local_ent site.
  long le = next_n();
  g_local_ent = (le < -1 || le >= MAX_ENTS) ? -1 : (int)le;
  printf("localent %d\n", g_local_ent);
}

static void cmd_tacstat(sim_ctx_t *s) {
  // Census of the tactical layer: how often bots actually slide, crouch, jump and
  // hold a pre-aim.
  long n = next_n();
  long lean_ticks = 0, low_ticks = 0, transfers = 0, retreats = 0, tactical_flanks = 0;
  long strafe_changes = 0;
  uint64_t nav_plans = g_bot_nav_plans, nav_visited = g_bot_nav_visited;
  int was_strafe[MAX_BOTS] = {0};
  long slides = 0, jumps = 0, cr_ticks = 0, sl_ticks = 0, preaim = 0, tot = 0;
  long cov_ticks = 0, hold_ticks = 0, investigate_ticks = 0, search_ticks = 0;
  long visible_ticks = 0, memory_ticks = 0, reacquired = 0, search_anchors = 0;
  long prefires = 0, prefire_ticks = 0, peeks = 0;
  long flanks = 0, flank_ticks = 0, arrivals = 0, stare_max = 0, pin_max = 0;
  long pushes = 0, breaks = 0, alert_ticks = 0;
  int was_dl[MAX_BOTS] = {0};
  long heard_ticks = 0, startles = 0;
  long suppressed_ticks = 0, suppress_cover_ticks = 0, suppress_recovery_ticks = 0;
  int was_pf[MAX_BOTS] = {0}, was_fl[MAX_BOTS] = {0}, was_hr[MAX_BOTS] = {0};
  int was_arr[MAX_BOTS] = {0};
  for (int b = 0; b < MAX_BOTS; b++) was_arr[b] = g_bots[b].flank_arrived;
  int was_sl[MAX_BOTS] = {0}, was_air[MAX_BOTS] = {0};
  int was_memory[MAX_BOTS] = {0}, was_searching[MAX_BOTS] = {0};
  int was_search_index[MAX_BOTS] = {0};
  v3 was_search_pos[MAX_BOTS];
  for (int b = 0; b < MAX_BOTS; b++) {
    const bot_t *bt = &g_bots[b];
    was_memory[b] = bt->active && bt->alive && bt->tgt >= 0 &&
                    bt->belief != BOT_BELIEF_VISIBLE;
    was_searching[b] = bt->active && bt->alive && bt->tgt >= 0 &&
                       (bt->belief == BOT_BELIEF_INVESTIGATE ||
                        bt->belief == BOT_BELIEF_SEARCH_SWEEP);
    was_search_index[b] = bt->search_index;
    was_search_pos[b] = bt->search_pos;
  }
  for (long i = 0; i < n; i++) {
    sim_advance(s);
    for (int b = 0; b < g_match.nbots; b++) {
      const bot_t *bt = &g_bots[b];
      if (!bt->active || !bt->alive) {
        was_sl[b] = 0;
        was_air[b] = 0;
        was_memory[b] = 0;
        was_searching[b] = 0;
        was_search_index[b] = bt->search_index;
        was_search_pos[b] = bt->search_pos;
        continue;
      }
      tot++;
      if (fabsf(bt->lean) > 0.15f) lean_ticks++;
      if (bt->tactical_t > 0) {
        if (bt->tactical_low && bt->tactical_hold > 0) low_ticks++;
        if (bt->tactic == BOT_RETREAT) retreats++;
        if (bt->tactic == BOT_FLANK) tactical_flanks++;
        if (!bt->tactical_hold) transfers++;
      }
      if (bt->strafe_dir && was_strafe[b] && bt->strafe_dir != was_strafe[b]) strafe_changes++;
      was_strafe[b] = bt->strafe_dir;
      if (bt->suppress.hold > 0 && bt->suppress.delay == 0) suppressed_ticks++;
      if (bt->suppress.hold > 0 && bt->suppress.cover) suppress_cover_ticks++;
      if (bt->suppress.cooldown > 0) suppress_recovery_ticks++;
      if (bt->sliding) sl_ticks++;
      if (bt->crouch) cr_ticks++;
      if (bt->sliding && !was_sl[b]) slides++;
      if (!bt->grounded && was_air[b] == 0 && bt->vel.y > 1.0f) jumps++;
      if (bt->tgt >= 0 && bt->lose_t > 0) preaim++;
      if (bt->prefiring) prefire_ticks++;
      if (bt->prefiring && !was_pf[b]) prefires++;
      was_pf[b] = bt->prefiring;
      if (bt->peek) peeks++;
      if (bt->flank_t > 0) flank_ticks++;
      if (bt->flank_t > 0 && !was_fl[b]) flanks++;
      was_fl[b] = bt->flank_t > 0;
      // ARRIVALS are what separate flanking from wandering with a goal, and they
      // are the only census term that catches a commitment that silently stopped
      // completing — it read a flat 0 while every swing that broke the line was
      // left un-walked.
      arrivals += bt->flank_arrived - was_arr[b];
      was_arr[b] = bt->flank_arrived;
      if (bt->stare > stare_max) stare_max = bt->stare;
      // The deadlock DECISION, split by which way it went — a tier that
      // only ever breaks is a tier that reads as cowardice, and one that
      // only ever pushes stops being a decision at all.
      if (bt->dl_t > 0 && !was_dl[b]) { if (bt->dl_push) pushes++; else breaks++; }
      was_dl[b] = bt->dl_t > 0;
      if (bt->alert_t > 0 && bt->tgt < 0) alert_ticks++;
      if (bt->pin > pin_max) pin_max = bt->pin;
      if (bt->startled) heard_ticks++;
      if (bt->startled && !was_hr[b]) startles++;
      was_hr[b] = bt->startled;
      if (bt->in_cover) cov_ticks++;
      int memory = bt->tgt >= 0 && bt->belief != BOT_BELIEF_VISIBLE;
      int searching = bt->tgt >= 0 &&
                      (bt->belief == BOT_BELIEF_INVESTIGATE ||
                       bt->belief == BOT_BELIEF_SEARCH_SWEEP);
      if (bt->tgt >= 0 && bt->belief == BOT_BELIEF_VISIBLE) visible_ticks++;
      if (bt->belief == BOT_BELIEF_OCCLUDED_HOLD) hold_ticks++;
      if (bt->belief == BOT_BELIEF_INVESTIGATE) investigate_ticks++;
      if (bt->belief == BOT_BELIEF_SEARCH_SWEEP) search_ticks++;
      if (memory) memory_ticks++;
      if (was_memory[b] && bt->tgt >= 0 && bt->belief == BOT_BELIEF_VISIBLE)
        reacquired++;
      if (searching) {
        v3 d = v3_sub(bt->search_pos, was_search_pos[b]);
        float moved2 = d.x * d.x + d.y * d.y + d.z * d.z;
        if (!was_searching[b] || bt->search_index != was_search_index[b] ||
            moved2 > BOT_TARGET_EPS * BOT_TARGET_EPS)
          search_anchors++;
      }
      was_sl[b] = bt->sliding;
      was_air[b] = !bt->grounded;
      was_memory[b] = memory;
      was_searching[b] = searching;
      was_search_index[b] = bt->search_index;
      was_search_pos[b] = bt->search_pos;
    }
  }
  printf("tacstat ticks=%ld botticks=%ld slides=%ld slide_pct=%.1f jumps=%ld "
         "crouch_pct=%.1f preaim_pct=%.1f cover_pct=%.1f hold=%ld "
         "investigate=%ld search=%ld visible=%ld "
         "memory_pct=%.1f investigate_pct=%.1f search_pct=%.1f "
         "reacquired=%ld searchanchors=%ld prefires=%ld prefire_pct=%.1f "
         "peeks=%ld flanks=%ld flank_pct=%.1f flank_arrive=%ld "
         "startles=%ld hear_pct=%.1f stare_max=%ld pin_max=%ld "
         "pushes=%ld breaks=%ld alert_pct=%.1f\n",
         n, tot, slides,
         tot ? 100.0 * (double)sl_ticks / (double)tot : 0.0, jumps,
         tot ? 100.0 * (double)cr_ticks / (double)tot : 0.0,
         tot ? 100.0 * (double)preaim / (double)tot : 0.0,
         tot ? 100.0 * (double)cov_ticks / (double)tot : 0.0,
         hold_ticks, investigate_ticks, search_ticks, visible_ticks,
         tot ? 100.0 * (double)memory_ticks / (double)tot : 0.0,
         tot ? 100.0 * (double)investigate_ticks / (double)tot : 0.0,
         tot ? 100.0 * (double)search_ticks / (double)tot : 0.0,
         reacquired, search_anchors, prefires,
         tot ? 100.0 * (double)prefire_ticks / (double)tot : 0.0, peeks,
         flanks, tot ? 100.0 * (double)flank_ticks / (double)tot : 0.0,
         arrivals, startles,
         tot ? 100.0 * (double)heard_ticks / (double)tot : 0.0, stare_max,
         pin_max, pushes, breaks,
         tot ? 100.0 * (double)alert_ticks / (double)tot : 0.0);
  printf("tacstat_tactical lean=%ld headcover=%ld transfers=%ld retreat=%ld flank=%ld "
         "strafe_changes=%ld nav_plans=%llu nav_cells=%llu\n", lean_ticks, low_ticks,
         transfers, retreats, tactical_flanks, strafe_changes,
         (unsigned long long)(g_bot_nav_plans - nav_plans),
         (unsigned long long)(g_bot_nav_visited - nav_visited));
  printf("tacstat_suppress ticks=%ld reacting=%ld cover=%ld recovery=%ld\n",
         tot, suppressed_ticks, suppress_cover_ticks, suppress_recovery_ticks);
}

static void cmd_netdebug(sim_ctx_t *s) {
  (void)s;
  g_net_debug = !g_net_debug;  // per-correction/trim/skip lines, see decl
  printf("netdebug %s\n", g_net_debug ? "on" : "off");
}

static void cmd_dev(sim_ctx_t *s) {
  (void)s;
  // DEV remains a two-level gate: the runtime config exposes the Root
  // action, while DEV_MENU stays the compile-time hard off switch.
  g_cfg.devmode = next_on_off("dev");
}

static void cmd_bots(sim_ctx_t *s) {
  (void)s;
  long n = next_n();
  g_cfg.sp_bots = parse_int_def(n, &SP_BOTS_DEF);
  sp_rules()->bots = g_cfg.sp_bots;
}

static void cmd_skill(sim_ctx_t *s) {
  (void)s;
  // The trailing fallback used to be a bare `: 1`, so `skill hrd` silently
  // measured the NORMAL tier — on the one command the documented difficulty
  // ladder is measured with. Every other enum-valued argument here errors.
  const char *n = next_tok();
  int d = !strcmp(n, "easy") ? 0 : !strcmp(n, "normal") ? 1
        : !strcmp(n, "hard") ? 2 : -1;
  if (d < 0) {
    fprintf(stderr, "script: unknown skill '%s' (easy|normal|hard)\n", n);
    exit(1);
  }
  g_cfg.sp_diff = d;
  sp_rules()->difficulty = d;
}

static void cmd_fraglimit(sim_ctx_t *s) {
  (void)s;
  const char *arg = next_tok();
  long n = !strcmp(arg, "off") || !strcmp(arg, "unlimited") ? 0 : parse_n(arg);
  g_cfg.sp_frag = sp_frag_normalize(n);
  sp_rules()->fraglimit = g_cfg.sp_frag;
}

static void cmd_restart(sim_ctx_t *s) {
  (void)s;
  g_match.restart_req = 1;
}

static void cmd_sfxlog(sim_ctx_t *s) {
  // Every audio decision the game makes, for N ticks, as text.
  long n = next_n();
  g_sfx_log_n = 0;
  g_sfx_log_drops = 0;
  g_sfx_log_on = g_audio_route = 1;
  for (long i = 0; i < n; i++) {
    sim_advance(s);
    // Routing without a mixer: nothing consumes the trigger ring, so the
    // command plays the consumer itself (mixdown's own idiom) — otherwise
    // the ring reads permanently flooded after 64 pushes and every later
    // trigger of the run counts as a bogus flood.
    audio_mix_discard_pending();
  }
  g_sfx_log_on = g_audio_route = 0;
  float peak_t = -1.0f;
  int overlap = 0, worst = 0;
  for (int i = 0; i < g_sfx_log_n; i++) {
    const sfx_log_t *l = &g_sfx_log[i];
    printf("sfxlog t=%ld %-9s gain=%.2f rate=%.2f pan=%+.2f delay=%dms\n",
           l->tick, SFX_NAME[l->id], (double)l->gain, (double)l->rate,
           (double)l->pan, (int)(l->delay * 1000.0f));
    // How many sounds are alive at this trigger's start: the voice budget
    // is 24 and a 20-bot firefight is the case that finds out.
    float t0 = (float)l->tick / TICK_HZ + l->delay;
    overlap = 0;
    for (int j = 0; j < g_sfx_log_n; j++) {
      float tj = (float)g_sfx_log[j].tick / TICK_HZ + g_sfx_log[j].delay;
      float dur = (float)g_sfx_len[g_sfx_log[j].id] / SFX_HZ;
      if (tj <= t0 && tj + dur > t0) overlap++;
    }
    if (overlap > worst) { worst = overlap; peak_t = t0; }
  }
  printf("sfxlog total=%d ticks=%ld max_concurrent=%d at t=%.2fs "
         "(voices=%d)", g_sfx_log_n, n, worst, (double)peak_t, MAX_VOICES);
  // The one state this command exists for (a firefight past the budget) is
  // also the one that overflows the log — say so instead of presenting a
  // truncated prefix as the whole run.
  if (g_sfx_log_drops)
    printf(" SATURATED log_drops=%ld", g_sfx_log_drops);
  putchar('\n');
}

static void cmd_audiotrace(sim_ctx_t *s) {
  const char *action = next_tok();
  if (!strcmp(action, "begin")) {
    g_sfx_log_tick = s->tick;
    if (!audio_trace_begin()) fatal("cannot begin audio trace");
  } else if (!strcmp(action, "write")) {
    const char *path = next_tok();
    FILE *out = fopen(path, "wx");
    if (!out) fatal("cannot create audio trace");
    int ok = audio_trace_write(out);
    if (fclose(out)) ok = 0;
    if (!ok) fatal("incomplete audio trace");
  } else if (!strcmp(action, "end")) audio_trace_end();
  else fatal("audiotrace expects begin, write PATH, or end");
}

static void cmd_acousticcheck(sim_ctx_t *s) {
  (void)s;
  int fail = audiocheck_lifecycle(acousticcheck_all);
  if (fail) exit(1);
}

static void cmd_audiocheck(sim_ctx_t *s) {
  (void)s;
  int catalogue = 1, mix = 1;
  char *part = take_tok();
  if (part && !strcmp(part, "mix")) catalogue = 0;
  else if (part && !strcmp(part, "catalogue")) mix = 0;
  else if (part) g_tok_pushback = part;
  int fail = catalogue ? audiocheck_catalogue() : 0;
  if (mix) fail |= audiocheck_lifecycle(audiocheck_routes);
  printf("audiocheck summary catalogue=%d lifecycle=%d %s\n", catalogue ? SFX_COUNT : 0,
         mix ? 13 : 0, fail ? "FAIL" : "ok");
  if (fail) exit(1);
}

static void cmd_sfx(sim_ctx_t *s) {
  (void)s;
  const char *dir = next_tok();
  for (int i = 0; i < SFX_COUNT; i++) {
    char path[560];
    snprintf(path, sizeof path, "%s/%s.wav", dir, SFX_NAME[i]);
    write_wav(path, g_sfx_buf[i], g_sfx_len[i]);
    an_report(SFX_NAME[i], g_sfx_buf[i], g_sfx_len[i], 0.30f);
  }
}

// Two bounded takes drive actual input edges through sim_advance and audio_mix.
// Setup is explicit in the script; no appframe can schedule duplicate voices.
static void cmd_audiosteps(sim_ctx_t *s) {
  const char *path = next_tok();
  const char *mode = next_tok();
  int repeated = !strcmp(mode, "sr"), cancel = !strcmp(mode, "cancel");
  if ((!repeated && !cancel) || (repeated && s->p->wp.cur != WPN_SR) ||
      (cancel && s->p->wp.ammo[s->p->wp.cur] == WPN_DEF[s->p->wp.cur].mag))
    fatal("audiosteps expects sr with SR, or cancel with a partially used magazine");
  int held[IN_COUNT]; memcpy(held, s->held, sizeof held);
  int period = gun_round_ticks(WPN_SR) + 2;
  int ticks = repeated ? period * 3 + 1 : TICK_HZ * 2;
  int total = ticks + TICK_HZ * AUDIO_TAIL_SECONDS;
  int frames = total * (SFX_HZ / TICK_HZ);
  audio_tail_t tail_state = {0}; int drained = 0;
  float *mix = calloc((size_t)frames * SFX_CH, sizeof *mix);
  if (!mix || !audio_mix_device_reset(SFX_HZ, SFX_CH)) fatal("cannot initialize audio take");
  int old_weapon = s->p->wp.cur, reload_seen = 0, cancelled = 0, fail = 0;
  long shots = s->p->shots;
  g_voice_peak = 0; g_lim_min = g_duck_min = 1;
  g_trig_dropped = g_trig_capped = g_trig_flooded = 0;
  g_trig_protected_flooded = g_protected_dropped = g_audio_invalid = 0;
  g_audio_route = 1;
  for (int tick = 0; tick < total; tick++) {
    if (tick < ticks) {
      memset(s->held, 0, sizeof s->held);
      s->held[IN_FIRE] = repeated ? tick % period == 0 : tick == TICK_HZ;
      s->held[IN_RELOAD] = cancel && tick == 0;
      s->held[IN_SWAP] = cancel && tick == TICK_HZ / 4;
      sim_advance(s);
      if (cancel && tick == 0) reload_seen = s->p->wp.reload_t > 0;
      if (cancel && tick == TICK_HZ / 4)
        cancelled = !s->p->wp.reload_t && s->p->wp.cur != old_weapon;
    }
    float *at = mix + (size_t)tick * (SFX_HZ / TICK_HZ) * SFX_CH;
    audio_mix(at, SFX_HZ / TICK_HZ, SFX_CH);
    if (cancel && tick == TICK_HZ / 2)
      for (int voice = 0; voice < MAX_VOICES; voice++)
        fail |= g_voices[voice].active && g_voices[voice].tag == 1 &&
          (g_voices[voice].id == SFX_RELOAD_AR || g_voices[voice].id == SFX_RELOAD_AR_TAC ||
           g_voices[voice].id == SFX_RELOAD_SR);
    if (tick >= ticks) {
      drained = audio_tail_step(&tail_state,at,SFX_HZ / TICK_HZ,SFX_CH);
      if (drained) { frames = (tick + 1) * (SFX_HZ / TICK_HZ); break; }
    }
  }
  g_audio_route = 0; memcpy(s->held, held, sizeof held);
  if (drained != 1) {
    free(mix); fatal(drained < 0 ? "audiosteps nonfinite or invalid tail" : "audiosteps tail did not drain within 10 seconds");
  }
  printf("audiosteps tail_frames=%d rms=%.9f threshold=0.000010000 quiet_ms=100 timeout=0\n",
    frames-ticks*(SFX_HZ / TICK_HZ),sqrt(tail_state.power));
  double energy = 0;
  for (int i = 0; i < frames * SFX_CH; i++) {
    fail |= f_is_nonfinite(mix[i]) || fabsf(mix[i]) >= .999f;
    energy += (double)mix[i] * mix[i];
  }
  fail |= s->p->shots - shots != (repeated ? 4 : 1) ||
    (cancel && (!reload_seen || !cancelled)) || energy <= 1e-6 ||
    g_trig_flooded || g_trig_protected_flooded || g_protected_dropped || g_audio_invalid;
  fail |= !write_wav(path, mix, frames); an_report("stepped_bus", mix, frames, 0);
  printf("audiosteps mode=%s ticks=%d shots=%ld reload=%d cancelled=%d voices_peak=%d "
    "normal_drop=%ld normal_cap=%ld protected_drop=%ld energy=%.6f %s\n", mode, ticks,
    s->p->shots - shots, reload_seen, cancelled, g_voice_peak, g_trig_dropped,
    g_trig_capped, g_protected_dropped, energy, fail ? "FAIL" : "ok");
  free(mix);
  if (fail) exit(1);
}

// Finish a contiguous take without advancing gameplay or clearing mixer state.
static void cmd_audiotail(sim_ctx_t *s) {
  (void)s;
  const char *path = next_tok();
  const int block = SFX_HZ / TICK_HZ;
  const int maximum = AUDIO_TAIL_SECONDS * SFX_HZ;
  float *pcm = malloc((size_t)maximum * SFX_CH * sizeof *pcm);
  if (!pcm) fatal("out of memory");
  float saved_rate = g_dev_rate;
  g_dev_rate = 1; // contiguous capture_run segments use this exact format
  audio_tail_t tail = {0};
  int frames = 0, drained = 0;
  while (frames + block <= maximum) {
    float *at = pcm + frames * SFX_CH;
    audio_mix(at, (uint32_t)block, SFX_CH);
    frames += block;
    drained = audio_tail_step(&tail,at,block,SFX_CH);
    if (drained) break;
  }
  g_dev_rate = saved_rate;
  if (drained != 1) {
    free(pcm); fatal(drained < 0 ? "audiotail nonfinite or invalid tail" : "audiotail did not drain within 10 seconds");
  }
  int written = write_wav(path,pcm,frames);
  free(pcm);
  if (!written) fatal("cannot write audio tail");
  printf("audiotail frames=%d rms=%.9f threshold=0.000010000 quiet_ms=100 timeout=0\n",
    frames,sqrt(tail.power));
}

static void cmd_mixdown(sim_ctx_t *s) {
  // The bus, rendered.
  const char *path = next_tok();
  long n = next_n();
  if (n < 1) n = 1;
  const int fpt = SFX_HZ / TICK_HZ;            // exactly 400 frames a tick
  const long tail = AUDIO_TAIL_SECONDS * TICK_HZ;
  if (n > WAV_MAX_FRAMES / fpt - tail) fatal("mixdown duration exceeds WAV frame limit");
  long total = n + tail;
  float *mix = calloc((size_t)total * (size_t)fpt * SFX_CH, sizeof *mix);
  if (!mix) fatal("out of memory");
  if (!audio_mix_device_reset(SFX_HZ, SFX_CH)) fatal("invalid capture audio format");
  g_lim_min = g_duck_min = 1.0f;
  g_voice_peak = 0;
  g_trig_dropped = 0;
  g_trig_capped = 0;
  g_trig_flooded = g_trig_protected_flooded = g_protected_dropped = 0;
  g_trig_stale = g_trig_cancelled = g_audio_invalid = g_audio_bad_snapshot = 0;
  g_audio_route = 1;
  int drained = 0;
  audio_tail_t tail_state = {0};
  for (long i = 0; i < total; i++) {
    if (i < n) sim_advance(s);
    float *block = mix + (size_t)i * (size_t)fpt * SFX_CH;
    audio_mix(block, (uint32_t)fpt, SFX_CH);
    if (i < n) continue;
    drained = audio_tail_step(&tail_state,block,fpt,SFX_CH);
    if (drained < 0) fatal("mixdown nonfinite tail");
    if (drained == 1) { total = i + 1; break; }
  }
  g_audio_route = 0;
  if (!drained) { free(mix); fatal("mixdown tail did not drain within 10 seconds"); }
  printf("mixdown tail_ticks=%ld rms=%.9f threshold=0.000010000 quiet_ms=100 timeout=0\n",
    total - n, sqrt(tail_state.power));
  int frames = (int)(total * fpt);
  if (!write_wav(path, mix, frames)) { free(mix); fatal("cannot write mixdown"); }
  long clip = 0;
  for (long k = 0; k < (long)frames * SFX_CH; k++)
    if (mix[k] >= 0.999f || mix[k] <= -0.999f) clip++;
  an_report("bus", mix, frames, 0.0f);
  printf("mixdown %s ticks=%ld voices_peak=%d/%d dropped=%ld capped=%ld "
         "limiter=%.2f (%+.1fdB) duck=%.2f (%+.1fdB) clipped=%ld "
         "normal_flood=%ld protected_flood=%ld protected_drop=%ld invalid=%ld stale=%ld cancel=%ld\n", path, n,
         g_voice_peak, MAX_VOICES, g_trig_dropped, g_trig_capped,
         (double)g_lim_min,
         20.0 * log10((double)g_lim_min + 1e-9), (double)g_duck_min,
         20.0 * log10((double)g_duck_min + 1e-9), clip,
         g_trig_flooded, g_trig_protected_flooded, g_protected_dropped,
         g_audio_invalid + g_audio_bad_snapshot, g_trig_stale, g_trig_cancelled);
  // Printed only when it happened, so every healthy baseline stays
  // byte-identical — the same idiom as `bang`'s pen= field.
  if (g_trig_flooded)
    printf("mixdown WARNING flooded=%ld (trigger ring refused)\n",
           g_trig_flooded);
  free(mix);
}

// Exercise real collision without arena_clamp: a hidden clamp must not mask
// an opening, a snag at a cut seam, or a boundary which bullets can cross.
static void cmd_boundarycheck(sim_ctx_t *s) {
  (void)s;
  long maps = next_n();
  if (maps < 1) maps = 1;
  int force0 = g_biome_force;
  uint32_t seed0 = g_map_seed;
  uint64_t rng0 = g_rng, bot0 = g_rng_bot, gun0[8];
  memcpy(gun0, g_rng_gun, sizeof gun0);
  float high_mv[MV_COUNT];
  memcpy(high_mv, g_sim_mv, sizeof high_mv);
  high_mv[MV_JUMP_H] = MV_DEF[MV_JUMP_H].hi;
  const float *mv0 = g_sim_mv;
  g_sim_mv = high_mv;
  float high_jump = mv_jump_speed();
  g_sim_mv = mv0;
  long rays = 0, routes = 0, contacts = 0, fail = 0;
  int meshes = 0, mesh_peak = 0;
  g_biome_force = BIOME_FOREST;
  for (long map = 0; map < maps; map++) {
    map_generate(g_map_seed);
    // Build the complete static world, not only its collision boxes. Dense
    // foliage seeds must leave room for the cut, shoulders and ground detail.
    long drops = g_mesh_drops, invalid = g_decor_invalid;
    mesh_build();
    fail += g_mesh_drops != drops || g_decor_invalid != invalid;
    int verts = g_mesh_floats / VERT_FLOATS;
    if (verts > mesh_peak) mesh_peak = verts;
    meshes++;
    float takeoff = 0.0f;
    for (int i = map_perim_count(); i < g_num_solids; i++)
      takeoff = fmaxf(takeoff, g_solids[i].max.y);
    for (int side = 0; side < 4; side++) {
      int axis = side < 2 ? 0 : 2, along = axis == 0 ? 2 : 0;
      float sign = side & 1 ? -1.0f : 1.0f;
      v3 outward = {{0,0,0}}; outward.e[axis] = sign;
      // Sample both ends of every lane and its full width, independently
      // of the generator's segment positions and its number of sections.
      for (int step = -22; step <= 22; step++) {
        v3 eye = {{0,EYE_STAND,0}};
        eye.e[axis] = sign * (ARENA_HALF - 1.0f);
        eye.e[along] = (float)step;
        v3 normal; int solid = -1;
        float d = trace_world(eye, outward, &normal, &solid);
        fail += fabsf(d - 1.0f) > 0.001f || solid < 0 ||
                (solid >= 0 && (g_solids[solid].pen || g_solids[solid].mat != SOLID_ROCK));
        rays++;
        for (int lane = 0; lane <= 6; lane++) {
          v3 p = eye; p.y = 0;
          float inset = PLAYER_RADIUS + 0.01f +
                        (RING_W - 2.0f * PLAYER_RADIUS - 0.02f) * (float)lane / 6.0f;
          p.e[axis] = sign * (ARENA_HALF - inset);
          fail += !aabb_fits(p, PLAYER_RADIUS, HEIGHT_STAND);
        }
      }
      for (int mode = 0; mode < 3; mode++) for (int reverse = 0; reverse < 2; reverse++) {
        float dir = reverse ? -1.0f : 1.0f;
        v3 p = {{0,0,0}}, vel = {{0,0,0}};
        p.e[axis] = sign * (ARENA_HALF - PLAYER_RADIUS - 0.01f);
        p.e[along] = -21.0f * dir;
        vel.e[along] = dir * (mode == 1 ? mv_slide_boost() : g_sim_mv[MV_RUN]);
        if (mode == 2) vel.y = mv_jump_speed();
        int grounded = mode != 2;
        int sliding = mode == 1;
        for (int tick = 0; tick < 1200 && p.e[along] * dir < 21.0f; tick++) {
          v3 wish = {{0,0,0}}; wish.e[along] = dir;
          float speed = fabsf(vel.e[along]);
          if (sliding && mv_slide_should_end(grounded, 1, speed)) sliding = 0;
          if (sliding)
            mv_slide_step(&vel, speed, wish.x, wish.z, 1.0f);
          else mv_walk(&vel, wish.x, wish.z, 1.0f, g_sim_mv[MV_RUN]);
          float expected = p.e[along] + vel.e[along] * TICK_DT;
          vel.y -= GRAVITY * TICK_DT;
          ent_move(&p, &vel, &grounded, mode == 1 ? HEIGHT_CROUCH : HEIGHT_STAND);
          fail += fabsf(p.e[along] - expected) > 0.001f ||
                  fabsf(p.e[axis] - sign * (ARENA_HALF - PLAYER_RADIUS - 0.01f)) > 0.001f;
        }
        fail += p.e[along] * dir < 21.0f;
        routes++;
      }
      // A standing rush, slide and full jump must hit the visible face and
      // settle inside it using collision alone, including at every seam.
      for (int step = -20; step <= 20; step += 4) for (int mode = 0; mode < 4; mode++) {
        v3 p = {{0,0,0}}, vel = v3_scale(outward, mode == 1 ? mv_slide_boost() : g_sim_mv[MV_RUN]);
        p.e[axis] = sign * (ARENA_HALF - 1.0f); p.e[along] = (float)step;
        if (mode == 2) vel.y = mv_jump_speed();
        if (mode == 3) {
          // The most conservative launch: highest actual cover, maximum
          // authored jump, and boosted speed right beside the boundary.
          vel = v3_scale(outward, mv_slide_boost());
          vel.y = high_jump; p.y = takeoff;
        }
        int grounded = mode < 2;
        for (int tick = 0; tick < 180; tick++) {
          mv_walk(&vel, outward.x, outward.z, 1.0f, g_sim_mv[MV_RUN]);
          vel.y -= GRAVITY * TICK_DT;
          ent_move(&p, &vel, &grounded, mode == 1 ? HEIGHT_CROUCH : HEIGHT_STAND);
          fail += p.e[axis] * sign > ARENA_HALF - PLAYER_RADIUS + 0.001f;
        }
        fail += fabsf(p.e[axis] * sign - (ARENA_HALF - PLAYER_RADIUS)) > 0.001f || !grounded;
        contacts++;
      }
    }
    if (fail) { printf("boundarycheck witness seed=%u fail=%ld\n", g_map_seed, fail); break; }
    g_map_seed = map_seed_next(g_map_seed);
  }
  g_biome_force = force0; g_map_seed = seed0; map_generate(seed0);
  g_rng = rng0; g_rng_bot = bot0; memcpy(g_rng_gun, gun0, sizeof gun0);
  g_world_dirty = 1;
  printf("boundarycheck maps=%ld rays=%ld routes=%ld contacts=%ld meshes=%d peak_verts=%d fail=%ld %s\n",
         maps, rays, routes, contacts, meshes, mesh_peak, fail, fail ? "FAIL" : "ok");
  if (fail) exit(1);
}

static void cmd_mapcheck(sim_ctx_t *s) {
  (void)s;
  // The world's version of figcheck, and nothing covered it before: two solids
  // whose SAME-facing faces land on one plane z-fight at every distance, and on 5 m
  // wall faces that is the most visible flicker in the game.
  long n = next_n();
  if (n < 1) n = 1;
  long worst = 0, total = 0, solid_peak = 0, solid_full = 0, edge_peak = 0;
  long open_probes = 0, open_blocked = 0, void_peak = 0, void_full = 0;
  // The check is a property of the GENERATOR, so it walks the seed chain
  // (map_seed_next, the same chain `restart` advances) and must put it back:
  // leaving the seed N steps on desynchronises every later `restart`, and leaving
  // the last probe map loaded without flagging the renderer means every later
  // `shot` photographs an arena the sim is no longer colliding against.
  uint32_t seed0 = g_map_seed;
  uint32_t bad_seed = g_map_seed;
  for (long m = 0; m < n; m++) {
    long hits = 0;
    if (g_num_solids > solid_peak) solid_peak = g_num_solids;
    if (g_num_solids == MAX_SOLIDS) solid_full++;
    if (g_meta.nedge > edge_peak) edge_peak = g_meta.nedge;
    if (g_meta.nvoid > void_peak) void_peak = g_meta.nvoid;
    if (g_meta.nvoid == META_VOID_MAX) void_full++;
    for (int q = 0; q < g_meta.nvoid; q++) {
      const mvoid_t *v = &g_meta.voids[q];
      v3 d = v->ax ? (v3){{0.0f, 0.0f, 1.0f}}
                   : (v3){{1.0f, 0.0f, 0.0f}};
      float reach = v->half_depth + 0.08f;
      // The generator publishes canonical-half samples. Both physical
      // openings must remain clear, including when capacity pressure could
      // otherwise truncate one member of a symmetric wall pair.
      for (int mirror = 0; mirror < 2; mirror++) {
        v3 p = v->p;
        if (mirror) { p.x = -p.x; p.z = -p.z; }
        v3 o = v3_sub(p, v3_scale(d, reach));
        int blocked = 0;
        for (int i = 0; i < g_num_solids && !blocked; i++) {
          v3 nrm;
          float t = ray_aabb(o, d, g_solids[i].min, g_solids[i].max, &nrm);
          if (t > 0.0f && t < reach * 2.0f - 0.001f) blocked = 1;
        }
        open_probes++;
        open_blocked += blocked;
      }
    }
    for (int i = 0; i < g_num_solids; i++)
      for (int j = i + 1; j < g_num_solids; j++) {
        const solid_t *A = &g_solids[i], *B = &g_solids[j];
        const float *amn = A->min.e, *amx = A->max.e;
        const float *bmn = B->min.e, *bmx = B->max.e;
        for (int a = 0; a < 3; a++) {
          int u = (a + 1) % 3, v = (a + 2) % 3;
          // overlap in the two axes the faces span, with real area
          if (fminf(amx[u], bmx[u]) - fmaxf(amn[u], bmn[u]) <= 0.001f) continue;
          if (fminf(amx[v], bmx[v]) - fmaxf(amn[v], bmn[v]) <= 0.001f) continue;
          // A -y face at ground level is buried in the floor and can never
          // be seen, so two boxes standing on the ground always share that
          // plane and it means nothing. Everything else counts.
          int buried = (a == 1 && amn[a] < 0.001f && bmn[a] < 0.001f);
          if (!buried && fabsf(amn[a] - bmn[a]) < 0.001f) hits++;
          if (fabsf(amx[a] - bmx[a]) < 0.001f) hits++;
        }
      }
    total += hits;
    if (hits > worst) { worst = hits; bad_seed = g_map_seed; }
    g_map_seed = map_seed_next(g_map_seed);
    map_generate(g_map_seed);
  }
  g_map_seed = seed0;
  map_generate(seed0);
  g_world_dirty = 1;
  printf("mapcheck maps=%ld coplanar_total=%ld worst=%ld worst_seed=%u "
         "solid_peak=%ld/%d solid_full=%ld edge_peak=%ld/%d "
         "open_probes=%ld blocked=%ld void_peak=%ld/%d void_full=%ld\n",
         n, total, worst, bad_seed, solid_peak, MAX_SOLIDS, solid_full,
         edge_peak, META_EDGE_MAX, open_probes, open_blocked,
         void_peak, META_VOID_MAX, void_full);
  if (total || solid_full || open_blocked || void_full) exit(1);
}

static void cmd_feedbackproof(sim_ctx_t *s) { (void)s; feedbackproof(); }

static const script_cmd_t SCRIPT_CMDS[] = {
  {"boundarycheck", cmd_boundarycheck},
  {"weatherproof", cmd_weatherproof},
  {"feedbackproof", cmd_feedbackproof},
  {"decorcheck", cmd_decorcheck}, {"framepace", cmd_framepace},
  {"bottactics", cmd_bottactics}, {"netloss", cmd_netloss}, {"botsuppress", cmd_botsuppress}, {"bothear", cmd_bothear}, {"botweapon", cmd_botweapon},
  {"botflank", cmd_botflank}, {"botmemory", cmd_botmemory},
  {"botmemoryobserve", cmd_botmemoryobserve}, {"padbackend", cmd_padbackend},
  {"padsync", cmd_padsync}, {"netfuzz", cmd_netfuzz},
  {"netdeath", cmd_netdeath}, {"netclient", cmd_netclient}, {"nethandshake", cmd_nethandshake},
  {"figbury", cmd_figbury}, {"figm", cmd_figm},
  {"pos", cmd_pos}, {"vmtrig", cmd_vmtrig},
  {"vmtrigfacet", cmd_vmtrigfacet}, {"vmfiref1c", cmd_vmfiref1c},
  {"flinch", cmd_flinch},
  {"wait", cmd_wait}, {"tap", cmd_tap}, {"look", cmd_look},
  {"wheel", cmd_wheel}, {"key", cmd_key}, {"weapon", cmd_weapon},
  {"aim", cmd_aim}, {"warp", cmd_warp}, {"aimbot", cmd_aimbot}, {"filmtrack", cmd_filmtrack}, {"filmtrackproof", cmd_filmtrackproof}, {"filmcueproof", cmd_filmcueproof},
  {"botfreeze", cmd_botfreeze}, {"puppet", cmd_puppet},
  {"wpn", cmd_wpn}, {"bot", cmd_bot}, {"match", cmd_match}, {"map", cmd_map},
  {"lean", cmd_lean}, {"cfg", cmd_cfg}, {"budget", cmd_budget},
  {"netstat", cmd_netstat}, {"updinfo", cmd_updinfo}, {"updcheck", cmd_updcheck},
  {"sun", cmd_sun}, {"biome", cmd_biome}, {"mood", cmd_mood}, {"scat", cmd_scat}, {"uistat", cmd_uistat},
  {"vmsight", cmd_vmsight}, {"vmscope", cmd_vmscope}, {"parity", cmd_parity},
  {"rag", cmd_rag}, {"ragsoak", cmd_ragsoak}, {"ragx", cmd_ragx},
  {"figv", cmd_figv}, {"figcheck", cmd_figcheck}, {"kine", cmd_kine},
  {"slidecheck", cmd_slidecheck}, {"armcheck", cmd_armcheck},
  {"naturalcheck", cmd_naturalcheck}, {"gaitcheck", cmd_gaitcheck}, {"animbench", cmd_animbench}, {"skel", cmd_skel}, {"vmhand", cmd_vmhand}, {"vmorbit", cmd_vmorbit},
  {"vmbore", cmd_vmbore},
  {"hud", cmd_hud}, {"showfps", cmd_showfps}, {"trace", cmd_trace},
  {"shot", cmd_shot}, {"capture", cmd_capture}, {"cam", cmd_cam}, {"bench", cmd_bench},
  {"benchpass", cmd_benchpass},
  {"dbgview", cmd_dbgview}, {"grade", cmd_grade},
  {"camaim", cmd_camaim}, {"cambot", cmd_cambot}, {"camorbit", cmd_camorbit},
  {"homeui", cmd_homeui}, {"home", cmd_home}, {"menu", cmd_menu},
  {"cursor", cmd_cursor}, {"mdown", cmd_mdown},
  {"mup", cmd_mup}, {"uiframe", cmd_uiframe}, {"appframe", cmd_appframe}, {"pad", cmd_pad},
  {"padscan", cmd_padscan}, {"padlook", cmd_padlook}, {"padstat", cmd_padstat}, {"padcurve", cmd_padcurve},
  {"nav", cmd_nav}, {"netloop", cmd_netloop}, {"netpack", cmd_netpack}, {"netpredict", cmd_netpredict},
  {"netlagcomp", cmd_netlagcomp}, {"netstall", cmd_netstall}, {"netanim", cmd_netanim}, {"netfill", cmd_netfill},
  {"netcarry", cmd_netcarry}, {"netfigure", cmd_netfigure},
  {"recoil", cmd_recoil}, {"gunwall", cmd_gunwall}, {"boresight", cmd_boresight},
  {"figmotion", cmd_figmotion}, {"fighit", cmd_fighit}, {"defaultsproof", cmd_defaultsproof}, {"netrecoil", cmd_netrecoil}, {"vmframe", cmd_vmframe},
  {"spstart", cmd_spstart}, {"netleave", cmd_netleave}, {"tele", cmd_tele}, {"humans", cmd_humans}, {"localent", cmd_localent},
  {"tacstat", cmd_tacstat}, {"netdebug", cmd_netdebug}, {"dev", cmd_dev}, {"bots", cmd_bots},
  {"skill", cmd_skill}, {"fraglimit", cmd_fraglimit}, {"restart", cmd_restart}, {"sfxlog", cmd_sfxlog},
  {"audiotail", cmd_audiotail}, {"acousticcheck", cmd_acousticcheck}, {"audiosteps", cmd_audiosteps}, {"audiotrace", cmd_audiotrace}, {"audiocheck", cmd_audiocheck}, {"sfx", cmd_sfx}, {"mixdown", cmd_mixdown}, {"mapcheck", cmd_mapcheck},
  {"leanstep", cmd_leanstep},
  {NULL, NULL},
};

static void run_script(char *script, sim_ctx_t *s) {
  g_tok_pushback = NULL;
  for (char *t = strtok(script, " \t\r\n;"); t; t = take_tok()) {
    const script_cmd_t *scmd = SCRIPT_CMDS;
    for (; scmd->name; scmd++) if (!strcmp(t, scmd->name)) { scmd->fn(s); break; }
    if (scmd->name) continue;
    if (t[0] == '+' || t[0] == '-') {
      s->held[action_from_name(t + 1)] = t[0] == '+';
    // VMCHECK STAYS IN THE CHAIN, DELIBERATELY: extracting it into a cmd_
    // function shifts its own printed near/worst fields under the shipping
    // flags (-O3 -ffast-math -flto re-inlines around the split, and FP
    // contraction flips which sample wins a worst-score tie — -O0 stays
    // bit-identical, i.e. non-semantic). Measured in round 6c.
    } else if (!strcmp(t, "vmcheck")) {
      // The viewmodel's geometry proof.
      player_t tmp = *s->p;
      v3 eye = {{0, 0, 0}}, rgt = {{1, 0, 0}}, upv = {{0, 1, 0}}, fwd = {{0, 0, -1}};
      fchk_res_t worst = {0};
      // `cross` is tracked as its own maximum over the sweep and deliberately kept OUT
      // of the score that picks the worst state.
      int worst_cross = 0;
      int worst_score = -1, tris = 0, structural_bad = 0;
      fchk_res_t structural_max = {0};
      const char *worst_state = "";
      char label[64];
      int recoil_states = 0;
      for (int wi = 0; wi < WPN_COUNT; wi++) {
        tmp.wp.cur = wi;
        for (int a = 0; a <= 10; a++) {
          // Base phases 3/4 are a full lean each way, at rest and mid-ramp (the lag term is
          // only nonzero while the lean is MOVING, so a settled lean would never
          // exercise it). At Hip and full ADS, append the five frames sampled by
          // vmtrig from the real presentation impulses and springs: back/lift peaks,
          // both learned side signs and the first return overshoot. No authored recoil
          // amplitude exists in this proof.
          int nstate = 6 + ((a == 0 || a == 10) ? 5 : 0);
          for (int state = 0; state < nstate; state++) {
            int phase = state < 6 ? state : 0;
            int rec = state - 6;
            tmp.wp.ads_lin = (float)a / 10.0f;
            tmp.wp.ads_s = tmp.wp.prev_ads_s = smooth01(tmp.wp.ads_lin);
            tmp.wp.reload_t = phase == 1 ? WPN_DEF[wi].reload_ticks / 2
                            : phase == 5 ? (WPN_DEF[wi].reload_ticks * 73) / 100
                            : 0;
            tmp.wp.switch_t = phase == 2 ? GUN_SWITCH_TICKS / 2 : 0;
            tmp.wp.prev_switch = (float)tmp.wp.switch_t / GUN_SWITCH_TICKS;
            tmp.vm.kick_back = tmp.vm.prev_kick_back = tmp.vm.kick_back_v = 0.0f;
            tmp.vm.kick_lift = tmp.vm.prev_kick_lift = tmp.vm.kick_lift_v = 0.0f;
            tmp.vm.kick_side = tmp.vm.prev_kick_side = tmp.vm.kick_side_v = 0.0f;
            const char *tag = phase == 1 ? "reload" : phase == 2 ? "swap" :
                              phase == 3 ? "lean_r" : phase == 4 ? "lean_l" :
                              phase == 5 ? "magout" : "idle";
            if (state >= 6) {
              static const int WHICH[5] = {
                VMTRIG_REC_BACK, VMTRIG_REC_LIFT, VMTRIG_REC_SIDE_POS,
                VMTRIG_REC_SIDE_NEG, VMTRIG_REC_OVERSHOOT,
              };
              static const char *const NAME[5] = {
                "back_peak", "lift_peak", "side_pos", "side_neg", "overshoot",
              };
              vmtrig_recoil_state_t rs = vmtrig_recoil_sample(
                  0, wi, tmp.wp.ads_s, WHICH[rec]);
              tmp.vm.kick_back = tmp.vm.prev_kick_back = rs.back;
              tmp.vm.kick_lift = tmp.vm.prev_kick_lift = rs.lift;
              tmp.vm.kick_side = tmp.vm.prev_kick_side = rs.side;
              tag = NAME[rec];
              recoil_states++;
            }
            tmp.lean = tmp.prev_lean = phase == 3 ? 1.0f : phase == 4 ? -1.0f : 0.0f;
            tmp.vm.lean_lag = tmp.vm.prev_lean_lag = tmp.lean * 0.45f;
            // Probes on, exactly as figcheck does it: a bare triangle index is not a
            // diagnosis. `vm_gun`/`vm_hands` is the granularity vm_build publishes, and
            // it is the one that answers the question this sweep is usually asked — is
            // the offending pair inside the weapon, or is it a hand closing on it.
            g_nprobe = 0;
            g_probe_on = 1;
            vm_build(&tmp, 1.0f, eye, rgt, upv, fwd, 1.0f);
            probe(NULL);
            g_probe_on = 0;
            fchk_res_t r = fchk_scene();
            tris = r.tris;
            int sc = r.open * 100 + r.flip * 100 + r.dup * 100 + r.degen * 100 +
                     r.zfight * 10 + r.near;
            structural_bad |= r.open || r.flip || r.dup || r.zfight || r.degen;
            if (r.open > structural_max.open) structural_max.open = r.open;
            if (r.flip > structural_max.flip) structural_max.flip = r.flip;
            if (r.dup > structural_max.dup) structural_max.dup = r.dup;
            if (r.zfight > structural_max.zfight) structural_max.zfight = r.zfight;
            if (r.degen > structural_max.degen) structural_max.degen = r.degen;
            if (r.cross > worst_cross) worst_cross = r.cross;
            if (sc > worst_score) {
              worst_score = sc;
              worst = r;
              snprintf(label, sizeof label, "%s ads=%.1f %s", WPN_DEF[wi].prefix,
                       (double)tmp.wp.ads_lin, tag);
              worst_state = label;
            }
          }
        }
      }
      printf("vmcheck tris=%d worst=[%s] open=%d flip=%d dup=%d zfight=%d "
             "near=%d cross=%d degen=%d recoil_states=%d\n", tris, worst_state,
             structural_max.open, structural_max.flip, structural_max.dup,
             structural_max.zfight, worst.near, worst_cross,
             structural_max.degen, recoil_states);
      if (structural_bad || recoil_states != WPN_COUNT * 2 * 5)
        exit(1);
    } else {
      fprintf(stderr, "script: unknown command '%s'\n", t);
      exit(1);
    }
  }
}
