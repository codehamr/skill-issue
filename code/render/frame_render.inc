// =============================================================================
// render/frame_render.inc  —  render_frame, the pass order of the frame.
// Proof: shot battery, budget, vmframe. One TU: included once by game.c, in
// definition order — moving a definition out is a semantic change.
// Contract: ONE scene buffer, three frusta (shadow, main, scope) plus the
// glare chain read it. msaa_resolve's per-frame latch and frame_fbo() are
// the one predicate for where the frame is being built right now.
// =============================================================================

// PER-PASS GPU TIMERS for the harness's `benchpass`: one GL_TIME_ELAPSED
// query per pass, armed only while benching (rf_mark is a no-op otherwise),
// so the frame's pass order below doubles as its cost table. On llvmpipe the
// "GPU" is the rasterizer threads, which is the proxy this repo tunes on.
#define RF_PASSES 12
static const char *const RF_PASS_NAME[RF_PASSES] = {
  "build", "shadow", "prepass", "ao", "world+sky", "figures", "ssr", "scope",
  "glow", "dust", "viewmodel", "post+hud"};
static int    g_rf_bench;
static GLuint g_rf_q[RF_PASSES];
static int    g_rf_cur = -1;
static void rf_mark(int idx) {
  if (!g_rf_bench) return;
  if (!g_rf_q[0]) glGenQueries(RF_PASSES, g_rf_q);
  if (g_rf_cur >= 0) glEndQuery(GL_TIME_ELAPSED);
  g_rf_cur = idx;
  if (idx >= 0) glBeginQuery(GL_TIME_ELAPSED, g_rf_q[idx]);
}

// The camera pass: everything that turns (player, alpha, override) into one
// frame's eye/basis/projection. Field list is exactly the locals that cross
// into a later pass or into render_frame's own inter-pass glue: `feet` crosses into
// the HUD's damage-arc calc and `tp_dist` into scene_build. The camera basis is solved
// later by rf_vm_prepare and crosses glow -> viewmodel in rf_vm_frame_t, because both
// passes must consume one exact T0/T1 plan; it does not belong in this general camera
// value.
typedef struct {
  v3 eye, feet, bore;
  float yaw, pitch, bore_yaw, bore_pitch, roll, fovy, aspect, ads, tp_dist;
  int cam_free, third;
  mat4 vp;
} frame_cam_t;

static frame_cam_t rf_camera(const player_t *p, float alpha,
                              const cam_override_t *ov) {
  v3 feet = v3_lerp(p->prev_pos, p->pos, alpha);
  float eye_h = p->prev_eye + (p->eye - p->prev_eye) * alpha
              - (p->prev_land_dip + (p->land_dip - p->prev_land_dip) * alpha);
  v3 eye = {{feet.x, feet.y + eye_h, feet.z}};
  // Lean: the eye travels out and sinks, and the view rolls by half the body's bend.
  float leanv = p->prev_lean + (p->lean - p->prev_lean) * alpha;
  eye = v3_add(eye, lean_offset(p->yaw, leanv));
  eye.y -= lean_eye_drop(leanv);
  // The camera follows the full physical recoil axis. The reticle, scope, rendered
  // barrel and next hitscan therefore always agree in hipfire and ADS.
  const weapon_t *w = &p->wp;
  float ads = w->prev_ads_s + (w->ads_s - w->prev_ads_s) * alpha;
  float rp = w->prev_rec_p + (w->rec_p - w->prev_rec_p) * alpha;
  float ry = w->prev_rec_y + (w->rec_y - w->prev_rec_y) * alpha;
  float yaw = p->yaw + ry * DEG2RAD;
  float pitch = f_clamp(p->pitch + rp * DEG2RAD, -PITCH_MAX, PITCH_MAX);
  // The optics and the viewmodel ride the WHOLE barrel, followed or not: the scope
  // picture is what the rifle sees, and the rendered weapon is where the rifle is.
  float bore_yaw, bore_pitch;
  gun_bore_angles_lerp(w, p->yaw, p->pitch, alpha, &bore_yaw, &bore_pitch);
  v3 bore = gun_bore_dir_lerp(w, p->yaw, p->pitch, alpha);
  // The boot screen owns the camera while it is up.
  cam_override_t home_ov;
  if (home_camera(&home_ov, p, alpha,
                  ov && (ov->active || ov->bot || ov->orbit)))
    ov = &home_ov;
  int cam_free = ov && (ov->active || ov->bot || ov->orbit);
  int third = g_cfg.third_person && !cam_free;   // shoulder cam, body visible
  // Lean is the only camera roll; recoil itself is the visible barrel axis.
  float roll = (third || cam_free) ? 0.0f
             : leanv * LEAN_ANGLE * LEAN_VIEW_K;
  float tp_dist = 2.7f;  // final camera pull-back; short = body would clip
  if (third) {
    // Shoulder camera: pivot slightly right of and above the head, pulled back along
    // the view; any wall behind pushes the camera in so it never clips into geometry.
    v3 fwd = {{cosf(pitch) * sinf(yaw), sinf(pitch), -cosf(pitch) * cosf(yaw)}};
    v3 rgt = v3_norm(v3_cross(fwd, (v3){{0, 1, 0}}));
    v3 side = v3_add(v3_scale(rgt, 0.34f), (v3){{0, 0.10f, 0}});
    float slen = v3_len(side);
    v3 sdir = v3_scale(side, 1.0f / slen);
    for (int i = 0; i < g_num_solids; i++) {
      v3 n;
      float t = ray_aabb(eye, sdir, g_solids[i].min, g_solids[i].max, &n);
      if (t > 0 && t - 0.06f < slen) slen = t - 0.06f;
    }
    v3 pivot = v3_add(eye, v3_scale(sdir, fmaxf(slen, 0.0f)));
    v3 back = v3_scale(fwd, -1.0f);
    for (int i = 0; i < g_num_solids; i++) {
      v3 n;
      float t = ray_aabb(pivot, back, g_solids[i].min, g_solids[i].max, &n);
      if (t > 0 && t - 0.12f < tp_dist) tp_dist = t - 0.12f;
    }
    tp_dist = f_clamp(tp_dist, 0.30f, 2.7f);
    eye = v3_add(pivot, v3_scale(back, tp_dist));
    if (eye.y < 0.10f) eye.y = 0.10f;
  }
  if (ov && ov->orbit) {
    eye = ov->eye; yaw = ov->yaw; pitch = ov->pitch;
  } else if (ov && ov->bot) {
    v3 bp = v3_lerp(g_bots[0].prev_pos, g_bots[0].pos, alpha);
    eye = v3_add(bp, ov->boff);
    v3 d = v3_add(v3_add(bp, (v3){{0, ov->blook > 0 ? ov->blook : 0.9f, 0}}),
                  v3_scale(eye, -1.0f));
    yaw = atan2f(d.x, -d.z);
    pitch = f_clamp(atan2f(d.y, sqrtf(d.x * d.x + d.z * d.z)), -PITCH_MAX, PITCH_MAX);
  } else if (ov && ov->active) {
    eye = ov->eye; yaw = ov->yaw; pitch = ov->pitch;
  }
  if (cam_free)
    bore = (v3){{cosf(pitch) * sinf(yaw), sinf(pitch),
                 -cosf(pitch) * cosf(yaw)}};

  float aspect = (float)R.width / (float)R.height;
  float fov_deg = g_cfg.fov;
  float ads_fov = fminf(fov_deg, g_cfg.wp[p->wp.cur][WP_ADS_FOV]);
  fov_deg += (ads_fov - fov_deg) * ads;
  float fovy = 2.0f * atanf(tanf(fov_deg * (F_PI / 180.0f) * 0.5f) / aspect);
  // ZNEAR is the single biggest lever on depth precision: resolution goes as
  // z^2*(f-n)/(n*f*2^24), so it is set by 1/n.
  mat4 vp = m4_mul(m4_perspective(fovy, aspect, ZNEAR, ZFAR),
                   m4_look(eye, yaw, pitch, roll));

  return (frame_cam_t){.eye = eye, .feet = feet, .bore = bore,
                        .yaw = yaw, .pitch = pitch,
                        .bore_yaw = cam_free ? yaw : bore_yaw,
                        .bore_pitch = cam_free ? pitch : bore_pitch,
                        .roll = roll, .fovy = fovy, .aspect = aspect,
                        .ads = ads, .tp_dist = tp_dist,
                        .cam_free = cam_free, .third = third, .vp = vp};
}

// Sun shadows. One ortho map over the whole arena, centred on it, far enough
// back along the sun that nothing in the level is behind the near plane.
// Reads no camera state at all (the light frustum is derived from g_sun,
// which is map state, not view state) — verified against the pre-split body,
// which never mentions eye/yaw/pitch/vp inside this block.
static void rf_shadow_pass(void) {
  // -g_sun.dir, and g_sun is the ONE copy: the shader is handed the same
  // vector through rig_upload, so a shadow can no longer be cast from a sun
  // the picture does not have.
  v3 ld = v3_scale(g_sun.dir, -1.0f);
  v3 lc = v3_add(g_sun.sh_centre, v3_scale(ld, -g_sun.sh_depth * 0.5f));
  R.light_vp = m4_mul(m4_ortho(g_sun.sh_half, g_sun.sh_half, 0.5f, g_sun.sh_depth),
                      m4_look_dir(lc, ld));
  glBindFramebuffer(GL_FRAMEBUFFER, R.sm_fbo);
  glViewport(0, 0, R.sm_built, R.sm_built);
  glClear(GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  // Front faces cast: with back faces the ground's own shadow floats a texel above
  // it.
  glEnable(GL_CULL_FACE);
  glUseProgram(R.prog_depth);
  glUniformMatrix4fv(R.u_dvp, 1, GL_FALSE, R.light_vp.m);
  glBindVertexArray(R.vao_world);
  glDrawArrays(GL_TRIANGLES, 0, R.world_cast_verts);
  scene_draw_cast();   // the same buffer minus the FP body — casters only
  rt_bind(frame_fbo(), R.width, R.height);
  glBindTexture(GL_TEXTURE_2D, R.sm_tex);   // stays bound for every lit pass
}

// THE GEOMETRY PREPASS: solids, floor, large horizon scenery and figures once
// more, into
// the prepass target (depth + world normal), single-sample whatever the MSAA
// setting. Fine decor is left out on purpose — a grass blade is not an
// occluder, it is a texel of noise the AO would smear under every figure
// standing in it — and so is everything drawn later with its own depth clear
// (the viewmodel). Distant architecture is inside world_pre_verts because god
// rays must stop at its silhouette even though it casts no arena shadow.
// Reads no state the lit pass does not: same VBOs, same camera, same cull.
static void rf_pre_pass(const frame_cam_t *c) {
  rt_bind(R.pre_fbo, R.width, R.height);
  glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glDisable(GL_BLEND);
  glUseProgram(R.prog_pre);
  glUniformMatrix4fv(R.u_pre_vp, 1, GL_FALSE, c->vp.m);
  glUniform1f(R.u_pre_pool, surf_params().z);
  glBindVertexArray(R.vao_world);
  glDrawArrays(GL_TRIANGLES, 0, R.world_pre_verts);
  scene_draw_vp(&c->vp);
  glClearColor(0.71f, 0.73f, 0.75f, 0.0f);   // the lit pass's own clear, restored
}

// SCREEN-SPACE AMBIENT OCCLUSION over the prepass, half resolution: the
// estimator, then one depth-aware blur pass per axis, leaving the result on
// texture unit 1 for the lit passes. Everything here is a fullscreen triangle
// with the depth test off; nothing writes depth. The projected-radius scale is
// the half-res target's height over the vertical tangent, so the same world
// radius lands in the same number of texels at any window size.
static void rf_ao_pass(const frame_cam_t *c) {
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glBindVertexArray(R.vao_sky);
  rt_bind(R.ao_fbo[0], R.ao_w, R.ao_h);
  glUseProgram(R.prog_ssao);
  glUniform1i(R.u_ao_depth, 0);
  glUniform1i(R.u_ao_nrm, 1);
  { mat4 inv = m4_inverse(&c->vp);
    glUniformMatrix4fv(R.u_ao_invvp, 1, GL_FALSE, inv.m); }
  glUniform3f(R.u_ao_eye, c->eye.x, c->eye.y, c->eye.z);
  glUniform4f(R.u_ao_scr, (float)R.ao_w, (float)R.ao_h,
              1.0f / (float)R.ao_w, 1.0f / (float)R.ao_h);
  // 0.32 m: contact, not a broad grime band. The restrained 1.15 intensity
  // complements the authored wall-foot ramp instead of multiplying it into a
  // black corner; both numbers were audited on the marked close-wall rig.
  glUniform4f(R.u_ao_z, ZNEAR, ZFAR,
              (float)R.ao_h * 0.5f / tanf(c->fovy * 0.5f), 0.32f);
  glUniform1f(R.u_ao_i, 0.92f);
  gl_active_texture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, R.nrm_tex);
  gl_active_texture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, R.depth_tex);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glUseProgram(R.prog_aoblur);
  glUniform1i(R.u_aob_tex, 0);
  for (int i = 0; i < 2; i++) {
    rt_bind(R.ao_fbo[1 - i], R.ao_w, R.ao_h);
    glBindTexture(GL_TEXTURE_2D, R.ao_tex[i]);
    glUniform2f(R.u_aob_px, i == 0 ? 1.0f / (float)R.ao_w : 0.0f,
                i == 0 ? 0.0f : 1.0f / (float)R.ao_h);
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  // The blurred result (ao_tex[0], two passes: 0 -> 1 -> 0) stays on unit 1
  // for every lit pass of this frame; unit 0 goes back to the shadow map.
  gl_active_texture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, R.ao_tex[0]);
  gl_active_texture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, R.sm_tex);
  glEnable(GL_DEPTH_TEST);
  rt_bind(frame_fbo(), R.width, R.height);
}

// SCREEN-SPACE REFLECTIONS in the standing water, after the opaque passes
// and before anything that glows: the opaque picture is copied out (the
// multisample target resolves into it, a plain target blits), the march
// reads it, and the apply pass blends the hits into the frame target under
// the water's Fresnel. Nothing here happens on a dry arena — the water level
// is the gate — and the copy is the whole cost on a wet one with MSAA on.
// Dst alpha is kept: it is the bloom weight the lit pass wrote. The march
// itself is half resolution (the AO's target size); the apply pass is full.
static void rf_ssr_pass(const frame_cam_t *c) {
  glBindFramebuffer(GL_READ_FRAMEBUFFER, frame_fbo());
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, R.pic_fbo);
  glBlitFramebuffer(0, 0, R.width, R.height, 0, 0, R.width, R.height,
                    GL_COLOR_BUFFER_BIT, GL_NEAREST);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glBindVertexArray(R.vao_sky);
  rt_bind(R.ssr_fbo, R.ao_w, R.ao_h);
  glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram(R.prog_ssr);
  glUniform1i(R.u_ssr_depth, 0);
  glUniform1i(R.u_ssr_nrm, 1);
  glUniform1i(R.u_ssr_pic, 3);
  { mat4 inv = m4_inverse(&c->vp);
    glUniformMatrix4fv(R.u_ssr_invvp, 1, GL_FALSE, inv.m);
    glUniformMatrix4fv(R.u_ssr_vp, 1, GL_FALSE, c->vp.m);
    glUniform3f(R.u_ssr_eye, c->eye.x, c->eye.y, c->eye.z);
    glUniform2f(R.u_ssr_z, ZNEAR, ZFAR);
    gl_active_texture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, R.pic_tex);
    gl_active_texture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, R.nrm_tex);
    gl_active_texture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, R.depth_tex);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    rt_bind(frame_fbo(), R.width, R.height);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    glUseProgram(R.prog_ssr_apply);
    glUniform1i(R.u_sa_ssr, 0);
    glUniform1i(R.u_sa_nrm, 1);
    glUniform1i(R.u_sa_depth, 3);
    glUniformMatrix4fv(R.u_sa_invvp, 1, GL_FALSE, inv.m);
    glUniform3f(R.u_sa_eye, c->eye.x, c->eye.y, c->eye.z);
    glUniform2f(R.u_sa_px, 1.0f / (float)R.ao_w, 1.0f / (float)R.ao_h);
    gl_active_texture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, R.depth_tex);
    gl_active_texture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, R.ssr_tex);
    glDrawArrays(GL_TRIANGLES, 0, 3); }
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
  // The lit passes that follow (scope, viewmodel) expect the shadow map on
  // unit 0 and the occlusion on unit 1.
  gl_active_texture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, R.ao_tex[0]);
  gl_active_texture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, R.sm_tex);
  glUseProgram(R.prog_world);
}

// World + sky. `shadow_on` is threaded as a plain second parameter rather than
// folded into frame_cam_t: it is not camera state, it is "was the shadow map
// this frame valid", computed once by the caller from R.sm_fbo and shared with
// rf_shadow_pass's own gate.
static void rf_world_pass(const frame_cam_t *c, int shadow_on) {
  mat4 vp = c->vp;
  v3 eye = c->eye;
  float yaw = c->yaw, pitch = c->pitch, roll = c->roll;
  float fovy = c->fovy, aspect = c->aspect;

  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glDisable(GL_BLEND);
  glUseProgram(R.prog_world);
  glUniformMatrix4fv(R.u_vp, 1, GL_FALSE, vp.m);
  glUniform3f(R.u_eye, eye.x, eye.y, eye.z);
  glUniform1f(R.u_detail, 1.0f);  // static world gets the surface detail
  // The floor's treatment.
  { v3 sp = surf_params();
    glUniform4f(R.u_surf, sp.x, sp.y, sp.z, g_wind_ang); }
  rig_upload(&R.rig_world);
  glUniform3f(R.u_foliage, cosf(g_wind_ang), sinf(g_wind_ang), (float)g_tick / 120.0f);
  glUniform1i(R.u_lamp_n, g_map_light_n);
  glUniform1i(R.u_lamp_tex, 4);
  gl_active_texture(GL_TEXTURE0 + 4);
  glBindTexture(GL_TEXTURE_2D, R.lamp_tex);
  gl_active_texture(GL_TEXTURE0);
  for (int i = 0; i < g_map_light_n; i++) {
    const map_light_t *l = &g_map_lights[i];
    glUniform4f(R.u_lamp_pos[i], l->pos.x, l->pos.y, l->pos.z, l->radius);
    glUniform3f(R.u_lamp_col[i], l->col.x, l->col.y, l->col.z);
  }
  glUniform1f(R.u_emis, 0.0f);
  glUniform1i(R.u_sm, 0);
  glUniform1f(R.u_shadow, shadow_on ? 1.0f : 0.0f);
  glUniform1f(R.u_vm, 0.0f);
  glUniform1i(R.u_ao, 1);
  glUniform4f(R.u_aop, (float)R.ao_w, (float)R.ao_h, g_cfg.ssao ? 1.0f : 0.0f, 0.0f);
  glUniform1f(R.u_skycut, 0.0f);
  // The world's own muzzle light.
  g_world_muz_c = 0.0f;
  for (int i = 0; i < MAX_FLASHES; i++)
    if (g_flashes[i].src == g_local_ent && g_tick - g_flashes[i].t0 <= 2 &&
        (g_flashes[i].life > 0 || g_flashes[i].fresh)) {
      g_world_muz_c = g_flashes[i].wpn == WPN_SR ? 12.0f : 8.0f;
      g_world_muz_p = g_flashes[i].pos;
    }
  if (g_world_muz_c > 0.001f) {
    glUniform3f(R.u_muzc, 1.00f * g_world_muz_c, 0.70f * g_world_muz_c,
                0.36f * g_world_muz_c);
    glUniform3f(R.u_muzp, g_world_muz_p.x, g_world_muz_p.y, g_world_muz_p.z);
  } else {
    glUniform3f(R.u_muzc, 0.0f, 0.0f, 0.0f);
  }
  // The map's size is asked, never assumed: at LOW the same penumbra is half as
  // many texels wide, and a filter stepping in the other map's texels is a
  // shadow filtered for a map that is not on the card.
  { float sm = (float)R.sm_built;
    glUniform2f(R.u_smstep, 1.0f / sm, 1.0f / sm);
    glUniform1f(R.u_smtexel, 2.0f * g_sun.sh_half / sm); }
  // The penumbra: the light-space depth runs 0..1 over (sh_depth - 0.5) metres
  // (m4_ortho's near is 0.5), so a depth difference times that is metres of
  // caster-to-receiver gap, times the sun's tangent is metres of penumbra,
  // over the texel is texels. The sun is drawn at 1.5x its 0.53 degrees: the
  // dust aureole around it is part of the source as far as a shadow is
  // concerned. The bias is two texels of world in the same units.
  { float texel_m = 2.0f * g_sun.sh_half / (float)R.sm_built;
    float rng = g_sun.sh_depth - 0.5f;
    glUniform1i(R.u_smraw, 2);
    glUniform2f(R.u_smpcss, rng * (g_biome.night > 0.5f ? 0.022f : 0.0139f) / texel_m, 2.0f * texel_m / rng); }
  glUniformMatrix4fv(R.u_lightvp, 1, GL_FALSE, R.light_vp.m);
  glBindVertexArray(R.vao_world);
  glDrawArrays(GL_TRIANGLES, 0, R.world_verts);

  sky_pass(yaw, pitch, roll, fovy, aspect);
}

// Scope picture-in-picture, stage 1: the SOLID scene. Returns what the later
// glow pass and the HUD need (`scope`, `svp`, `scope_on`); `szoom` never
// crosses out of this pass (nothing downstream reads it), so it stays a
// pass-local the way it always was.
typedef struct { float scope; mat4 svp; int scope_on; } scope_result_t;

static scope_result_t rf_scope_pass(const player_t *p, const frame_cam_t *c) {
  v3 eye = c->eye;
  float yaw = c->bore_yaw, pitch = c->bore_pitch, roll = c->roll, ads = c->ads;
  int cam_free = c->cam_free, third = c->third;
  mat4 vp = c->vp;

  float scope = 0.0f;
  mat4 svp = vp;
  // ...AND ONLY ONCE THE DISC CAN ACTUALLY OPEN. The picture is gated on the ramp at
  // SCOPE_ADS_OPEN below (`rad` is identically zero under it), so every frame of the
  // first half of a raise rendered the world, the sky and every figure a second time
  // into a 768-square target that was then not drawn.
  int scope_on = !cam_free && !third && p->alive && R.scope_fbo &&
                 p->wp.cur == WPN_SR && ads > SCOPE_ADS_OPEN;
  if (scope_on) {
    scope = ads;
    float szoom = SCOPE_FOV_DEG * (F_PI / 180.0f);
    svp = m4_mul(m4_perspective(szoom, 1.0f, ZNEAR, ZFAR),
                 m4_look(eye, yaw, pitch, roll));
    rt_bind(R.scope_fbo, R.scope_size, R.scope_size);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(R.prog_world);
    glUniformMatrix4fv(R.u_vp, 1, GL_FALSE, svp.m);
    glUniform1f(R.u_detail, 1.0f);
    // The occlusion was measured through the main lens; the scope's is another
    // screen, so it goes without.
    glUniform4f(R.u_aop, (float)R.ao_w, (float)R.ao_h, 0.0f, 0.0f);
    glBindVertexArray(R.vao_world);
    glDrawArrays(GL_TRIANGLES, 0, R.world_verts);
    sky_pass(yaw, pitch, roll, szoom, 1.0f);
    // The figures, still in the buffer — capped at the cast mark, so the 9°
    // lens can never magnify the shooter's own trousers at a steep down
    // pitch: the FP body is appended after the mark, same contract as the
    // shadow pass. scene_bind() first, so a pending upload happens at FULL
    // size before the clamp.
    scene_bind();
    { int fl_save = g_scene_floats;
      if (g_scene_cast_floats >= 0) g_scene_floats = g_scene_cast_floats;
      scene_draw_vp(&svp);
      g_scene_floats = fl_save; }
    rt_bind(frame_fbo(), R.width, R.height);
    glUniformMatrix4fv(R.u_vp, 1, GL_FALSE, vp.m);
    glUniform4f(R.u_aop, (float)R.ao_w, (float)R.ao_h, g_cfg.ssao ? 1.0f : 0.0f, 0.0f);
  }
  return (scope_result_t){.scope = scope, .svp = svp, .scope_on = scope_on};
}

// A viewmodel point is authored and drawn through VM_FOV_DEG, while its tracer is
// drawn in the world projection. Preserve its camera-space depth and scale only the
// lateral coordinates by the tangent ratio: both projections then land on exactly the
// same pixel. Pure and shared with vmframe, so the proof measures the transform the
// renderer actually consumes rather than a parallel approximation of it.
static v3 vm_muzzle_world_remap(v3 eye, v3 fwd, v3 rgt, v3 upv,
                                float world_fovy, v3 vm_cam) {
  float k = tanf(world_fovy * 0.5f) /
            tanf(VM_FOV_DEG * DEG2RAD * 0.5f);
  return v3_add(eye, v3_add(v3_scale(rgt, vm_cam.x * k),
         v3_add(v3_scale(upv, vm_cam.y * k),
                v3_scale(fwd, vm_cam.z))));
}

static float vm_render_ready(const player_t *p, const anim_t *a, float alpha);

// Mesh-free state shared by the glow and opaque-viewmodel passes.  The glow pass is
// intentionally earlier, so it prepares and publishes this frame once; the later VM
// pass consumes the same solve when it emits the one mesh and one draw call.
typedef struct {
  vm_build_plan_t plan;
  v3 fwd, rgt, upv;
  int active;
} rf_vm_frame_t;

static rf_vm_frame_t rf_vm_prepare(const player_t *p, float alpha,
                                   const frame_cam_t *c) {
  rf_vm_frame_t out = {0};
  out.active = ((!c->cam_free && !c->third && p->alive) || g_vmorb.on);
  if (!out.active) return out;

  cam_basis(c->yaw, c->pitch, c->roll, &out.fwd, &out.rgt, &out.upv);
  int lh = human_of_ent(g_local_ent);
  const anim_t *a = lh >= 0 && lh < MAX_HUMANS ? &g_player_anim[lh] : NULL;
  float ready = vm_render_ready(p, a, alpha);
  v3 bore = g_vmorb.on ? out.fwd : c->bore;
  out.plan = vm_plan_make(p, alpha, c->eye, out.rgt, out.upv, out.fwd,
                          bore, ready);

  // Only the ordinary live first-person camera may publish.  Orbit/probe poses still
  // build through the plan above, but can never donate their muzzle to a later tracer.
  if (!c->cam_free && !c->third && p->alive && !g_vmorb.on) {
    v3 mc = v3_sub(out.plan.muzzle, c->eye);
    g_vm_muz_cam = (v3){{v3_dot(mc, out.rgt), v3_dot(mc, out.upv),
                         v3_dot(mc, out.fwd)}};
    g_vm_muz_tick = g_tick;
  }
  return out;
}

// Accept only the current tick's live first-person publish. Frame preparation precedes
// the glow read even at T0; a missing or stale publish is an error, never a fallback.
static int rf_vm_muzzle_anchor(const player_t *p, const frame_cam_t *c,
                               const rf_vm_frame_t *vmf, v3 *out) {
  if (c->cam_free || c->third || !p->alive || g_vmorb.on ||
      g_vm_muz_tick != g_tick || !(g_vm_muz_cam.z > 0.05f))
    return 0;
  *out = vm_muzzle_world_remap(c->eye, vmf->fwd, vmf->rgt, vmf->upv,
                               c->fovy, g_vm_muz_cam);
  return !f_is_nonfinite(out->x) && !f_is_nonfinite(out->y) &&
         !f_is_nonfinite(out->z);
}

// This is the non-GL production sequence vmframe exercises directly: solve/publish
// first, then consume the published T0/T1 anchor.  rf_glow_pass adds only buffer build
// and draws around it.
static int rf_vm_publish_anchor(const player_t *p, float alpha,
                                const frame_cam_t *c, rf_vm_frame_t *vmf,
                                v3 *anchor) {
  *vmf = rf_vm_prepare(p, alpha, c);
  return rf_vm_muzzle_anchor(p, c, vmf, anchor);
}

// Re-anchor the player's own streak onto the viewmodel's published muzzle
// (glow), plus the scope's own stage 2 (the same glow, seen through the
// glass) when the scope is up. `svp_in` is aliased to a local `svp` copy: the
// pass only ever READS the matrix (glUniformMatrix4fv, scene_draw_vp(&svp)),
// so a value copy behaves identically to the original local.
static void rf_glow_pass(const player_t *p, float alpha, const frame_cam_t *c,
                         int scope_on, const mat4 *svp_in,
                         rf_vm_frame_t *vmf) {
  v3 eye = c->eye;
  int cam_free = c->cam_free, third = c->third;
  mat4 vp = c->vp;
  mat4 svp = *svp_in;

  // FOV-remapped: the vm pass renders at VM_FOV_DEG while the world renders at fovy, so the
  // world-space point that lands on the same screen pixel as the drawn barrel tip has
  // its lateral camera-space offsets scaled by the tangent ratio.
  g_fx_muz_live = 0;
  // The LOCAL alive status: this is one half of the flash-ownership handshake with the
  // world-star suppression above — with g_local_ent < 0 (dedicated server) it must be
  // hard 0, or the two halves disagree and a shot draws zero or two stars.
  g_fx_vm_flash = g_local_ent >= 0 && !cam_free && !third && p->alive && !g_vmorb.on;
  g_fx_muz_live = rf_vm_publish_anchor(p, alpha, c, vmf, &g_fx_muzzle);
  fx_build(eye);
  if (g_scene_floats) {
    fx_pass_begin();
    scene_draw_vp(&vp);
    if (scope_on) {  // stage 2: the same glow, seen through the glass
      rt_bind(R.scope_fbo, R.scope_size, R.scope_size);
      glUniformMatrix4fv(R.u_vp, 1, GL_FALSE, svp.m);
      scene_draw_vp(&svp);
      rt_bind(frame_fbo(), R.width, R.height);
      glUniformMatrix4fv(R.u_vp, 1, GL_FALSE, vp.m);
    }
    fx_pass_end();
  }
}

// AIRBORNE DUST, MAIN VIEW ONLY, and that is why it is its own submission
// rather than another builder inside fx_build. A mote is sized to hold about
// a pixel and a half at the world's FOV; the scope renders the same buffer
// through a NINE degree lens, which magnifies every one of them into a blob
// sitting between the crosshair and the target. A scope is not where you
// look at the air.
static void rf_dust_pass(const frame_cam_t *c) {
  v3 eye = c->eye;
  int third = c->third;
  mat4 vp = c->vp;

  // DUST OFF is decided before the motes are built, not after: the pass costs a
  // scene batch and a full blend over the frame, and its whole result is the
  // motes themselves.
  if (!third && g_cfg.dust) {
    scene_reset();
    fx_dust(eye);
    if (g_scene_floats) {
      // The prepass depth on unit 3 for the sky test — see uSkyCut in WORLD_FS.
      gl_active_texture(GL_TEXTURE3);
      glBindTexture(GL_TEXTURE_2D, R.depth_tex);
      gl_active_texture(GL_TEXTURE0);
      glUniform1i(R.u_predepth, 3);
      glUniform1f(R.u_skycut, 1.0f);
      fx_pass_begin();
      scene_draw_vp(&vp);
      fx_pass_end();
      glUniform1f(R.u_skycut, 0.0f);
    }
  }
}

// Viewmodel pass: isolated depth when MSAA is on, near depth slice otherwise,
// so the gun never clips into walls; its fixed-FOV projection keeps the world
// FOV slider from distorting it.
static float vm_render_ready(const player_t *p, const anim_t *a, float alpha) {
  if (!a) return 1.0f;
  float ready = a->prev_ready_s + (a->ready_s - a->prev_ready_s) * alpha;
  // Render interpolation is cosmetic and may never interpolate across a real action
  // gate. In particular alpha=0 on the shot tick still observes the current cooldown
  // / fire latch and therefore cannot draw the SR's previous low-ready frame.
  if (p->wp.cur == WPN_SR &&
      gun_pose_state(&p->wp, a, p->vel, p->grounded, p->sliding) ==
        GUN_POSE_BLOCKED)
    ready = 1.0f;
  return f_clamp(ready, 0.0f, 1.0f);
}

static void rf_vm_pass(const player_t *p, const frame_cam_t *c,
                       const rf_vm_frame_t *vmf) {
  float yaw = c->yaw, pitch = c->pitch, roll = c->roll, aspect = c->aspect;
  float ads = c->ads;
  v3 eye = c->eye;

  if (vmf->active) {
    v3 fwd = vmf->fwd, rgt = vmf->rgt, upv = vmf->upv;
    // With MSAA the viewmodel owns a separate multisample depth buffer and can
    // clear it. Without MSAA, scene_fbo deliberately shares the sampled
    // prepass depth texture: clearing it would erase world occlusion just before
    // the volume/glare chain reads it. Map the weapon into the nearest
    // one percent instead; it remains in front, self-depth still works, and
    // untouched pixels retain the world's atmosphere stop distance.
    int vm_depth_slice = R.msaa_fbo == 0;
    if (vm_depth_slice) glDepthRange(0.0, 0.01);
    else glClear(GL_DEPTH_BUFFER_BIT);
    mat4 vmvp = m4_mul(m4_perspective(VM_FOV_DEG * DEG2RAD, aspect, 0.01f, 5.0f),
                       m4_look(eye, yaw, pitch, roll));
    glUniformMatrix4fv(R.u_vp, 1, GL_FALSE, vmvp.m);
    // Hero rig, in world directions built from the camera basis: fill over the left
    // shoulder, rim from the lower right and behind.
    v3 fl = v3_norm(v3_add(v3_scale(rgt, -0.26f),
                   v3_add(v3_scale(upv, 0.66f), v3_scale(fwd, -0.36f))));
    v3 rm = v3_norm(v3_add(v3_scale(rgt, 0.74f),
                   v3_add(v3_scale(upv, -0.10f), v3_scale(fwd, 0.54f))));
    glUniform1f(R.u_vm, 1.0f);
    glUniform3f(R.u_fill, fl.x, fl.y, fl.z);
    glUniform3f(R.u_rim, rm.x, rm.y, rm.z);
    // The frame the micro-structure is anchored in. vm_build below refreshes it for
    // THIS frame's pose, so the upload has to follow the build, not precede it — see
    // the block after the vmorbit re-aim.
    int mfresh = g_muzzle_fresh && g_tick - g_muzzle_fire_tick <= 12 &&
                 !g_vmorb.on;
    if ((g_muzzle_t > 0 || mfresh) && !g_vmorb.on) {
      // The self-light DECAYS with the flash's new longer life — held at 1.3
      // for all seven ticks the weapon read as lamp-lit long after the flame.
      float k = (mfresh ? 3.4f : 2.0f * (float)g_muzzle_t / (float)FLASH_TICKS) *
                (1.0f - 0.45f * ads);
      v3 ml = v3_norm(v3_add(fwd, v3_scale(upv, 0.16f)));
      glUniform3f(R.u_muzl, ml.x, ml.y, ml.z);
      glUniform3f(R.u_muzc, 1.00f * k, 0.72f * k, 0.38f * k);
    } else {
      glUniform3f(R.u_muzc, 0.0f, 0.0f, 0.0f);
    }
    vm_build_prepared(eye, rgt, upv, fwd, &vmf->plan);
    if (g_vmorb.on) {
      // Re-aim the projection at one hand, keeping the pose that was just built.
      v3 of, orr, ou;
      // ORBIT ANGLES ARE RELATIVE TO THE WEAPON when the boot screen is driving, and
      // that is not a convenience.
      float oyaw, opit;
      if (g_vmorb.piv_on) {
        float cy2 = cosf(g_vmorb.yaw), sy2 = sinf(g_vmorb.yaw);
        float cp2 = cosf(g_vmorb.pit), sp2 = sinf(g_vmorb.pit);
        v3 dir = v3_add(v3_scale(g_vm_mdl_x, cp2 * sy2),
                 v3_add(v3_scale(g_vm_mdl_y, sp2),
                        v3_scale(g_vm_mdl_z, cp2 * cy2)));
        dir = v3_norm(dir);
        oyaw = atan2f(dir.x, -dir.z);
        opit = asinf(f_clamp(dir.y, -1.0f, 1.0f));
      } else {
        oyaw = g_vmorb.yaw;
        opit = g_vmorb.pit;
      }
      cam_basis(oyaw, opit, 0.0f, &of, &orr, &ou);
      // WHICH = 3 pivots on the TRIGGER, not on a wrist.
      v3 piv = g_vmorb.which == 3 ? g_gun_trig_p
             : g_vmorb.which < 2  ? g_vm_hand_p[g_vmorb.which]
                                  : v3_scale(v3_add(g_vm_hand_p[0],
                                                    g_vm_hand_p[1]), 0.5f);
      // A STATION ON THE WEAPON'S OWN AXIS, not a wrist.
      if (g_vmorb.piv_on)
        piv = v3_add(g_vm_mdl_o, v3_add(v3_scale(g_vm_mdl_z, g_vmorb.bore),
                                        v3_scale(g_vm_mdl_y, g_vmorb.rise)));
      float ofov = g_vmorb.fov > 1.0f ? g_vmorb.fov : 30.0f;
      // `comp` is a FRACTION OF THE FRAME, not an angle, and it is turned into one here
      // because here is the only place that knows the lens and the aspect.
      float ohf = 2.0f * atanf(tanf(ofov * DEG2RAD * 0.5f) * aspect);
      mat4 ovp = m4_mul(m4_perspective(ofov * DEG2RAD, aspect, 0.01f, 5.0f),
                        m4_look(v3_sub(piv, v3_scale(of, g_vmorb.dist)),
                                oyaw - g_vmorb.comp * ohf, opit, 0.0f));
      glUniformMatrix4fv(R.u_vp, 1, GL_FALSE, ovp.m);
      v3 ofl = v3_norm(v3_add(v3_scale(orr, -0.52f),
                      v3_add(v3_scale(ou, 0.62f), v3_scale(of, -0.35f))));
      v3 orm = v3_norm(v3_add(v3_scale(orr, 0.70f),
                      v3_add(v3_scale(ou, -0.22f), v3_scale(of, 0.62f))));
      glUniform3f(R.u_fill, ofl.x, ofl.y, ofl.z);
      glUniform3f(R.u_rim, orm.x, orm.y, orm.z);
      // ...AND THE EYE, which the turntable was not moving. `uEye` is what every view-
      // dependent term in the shader is built on — the whole specular lobe, the
      // environment reflection, the Fresnel and the aerial perspective — and it was
      // still the PLAYER's eye while the projection orbited, so the one view that
      // exists to judge a material showed the highlights of a viewpoint it was not
      // looking from.
      v3 oeye = v3_sub(piv, v3_scale(of, g_vmorb.dist));
      glUniform3f(R.u_eye, oeye.x, oeye.y, oeye.z);
    }
    // The weapon's own frame, uploaded AFTER vm_build so it describes the pose
    // that was just emitted rather than the previous frame's.
    glUniform3f(R.u_mdlo, g_vm_mdl_o.x, g_vm_mdl_o.y, g_vm_mdl_o.z);
    glUniform3f(R.u_mdlx, g_vm_mdl_x.x, g_vm_mdl_x.y, g_vm_mdl_x.z);
    glUniform3f(R.u_mdlz, g_vm_mdl_z.x, g_vm_mdl_z.y, g_vm_mdl_z.z);
    scene_draw();
    glUniform1f(R.u_vm, 0.0f);
    // The world pass that follows (the scope FBO, the HUD's own program) must
    // not inherit the orbit's eye; the next world draw sets it from the camera.
    glUniform3f(R.u_eye, eye.x, eye.y, eye.z);
    glUniform3f(R.u_muzc, 0.0f, 0.0f, 0.0f);
    if ((g_muzzle_t > 0 || mfresh) && !g_vmorb.on) {
      // The same star the world pass gives every other shooter, on the drawn barrel tip
      // in the viewmodel's own projection.
      //
      // The reflex dot remains HUD-drawn to avoid a duplicate emissive mark. Its full-ADS
      // centre is honest because the cheek-weld camera follows the physical bore at
      // ads=1; before that point the fading housing itself shows the remaining parallax.
      scene_reset();
      float mf01 = mfresh ? 1.0f : (float)g_muzzle_t / (float)FLASH_TICKS;
      g_muzzle_fresh = 0;
      float fs = 1.0f - 0.55f * ads;
      float ws = p->wp.cur == WPN_SR ? 0.155f : 0.115f;
      fx_flash(g_vm_muzzle, vmf->plan.frame.gz, vmf->plan.frame.gx,
               vmf->plan.frame.gy, eye,
               ws * fs * (0.55f + 0.45f * mf01), mf01,
               (uint32_t)p->wp.idx * 61u + 17u, p->wp.cur);
      fx_pass_begin();
      scene_draw();
      fx_pass_end();
    }
    if (vm_depth_slice) glDepthRange(0.0, 1.0);
  }
}

// HUD + menu: everything batches into one buffer, one draw call.
static void rf_hud_pass(const player_t *p, const frame_cam_t *c, int fps,
                         float scope) {
  v3 feet = c->feet;
  float yaw = c->yaw, fovy = c->fovy, ads = c->ads;
  int cam_free = c->cam_free, third = c->third;

  g_ui_time += g_ui_dt;   // presentation clock (pulses, glides); never sim-read
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(R.prog_hud);
  glUniform2f(R.u_screen, (float)R.width, (float)R.height);
  glUniform1i(R.u_tex, 0);
  glUniform1i(R.u_img, 0);
  glBindTexture(GL_TEXTURE_2D, R.font_tex);   // unit 0 carried the shadow map
  glBindVertexArray(R.vao_hud);

  // The optic is part of the weapon, not the gameplay overlay. Harness captures and
  // players may hide ammo/crosshair furniture while the scope still has to transmit
  // the render target through its glass.
  int hud_on = g_hud_visible && !g_home;
  if (!g_home && scope > 0.02f) {
    ui_begin();
    float cx = g_scope_cx, cy = g_scope_cy;
    float vw_ = g_ui_vw, vh_ = g_ui_vh;
    // Radius comes from the geometry (vm_build projected the ocular).
    float rad = scope_pip_radius(scope, vw_, vh_);
    if (rad > 0.5f) {
      // Glass TAKES light: coated optics transmit ~90% and tint slightly cool.
      const float glass[4] = {0.86f, 0.89f, 0.93f, 1.0f};
      const float grim[4] = {0.52f, 0.55f, 0.58f, 1.0f};   // radial vignette
      for (int i = 0; i < SCOPE_SEGS; i++) {
        float a0 = (float)i * F_TAU / SCOPE_SEGS;
        float a1 = (float)(i + 1) * F_TAU / SCOPE_SEGS;
        ui_push(cx, cy, 0.5f, 0.5f, glass);
        ui_push(cx + cosf(a0) * rad, cy + sinf(a0) * rad,
                0.5f + cosf(a0) * 0.5f, 0.5f - sinf(a0) * 0.5f, grim);
        ui_push(cx + cosf(a1) * rad, cy + sinf(a1) * rad,
                0.5f + cosf(a1) * 0.5f, 0.5f - sinf(a1) * 0.5f, grim);
      }
      glUniform1i(R.u_img, 1);
      glBindTexture(GL_TEXTURE_2D, R.scope_tex);
      ui_flush();
      glUniform1i(R.u_img, 0);
      // Glass furniture, drawn over the picture in font/solid mode: a dark vignette
      // ring that fades the image into the tube wall, then the reticle — a fine cross
      // with a gap at the centre (so the aim point is never covered), tapered thick
      // posts outside it, and mil hashes down the lower post.
      ui_begin();
      glBindTexture(GL_TEXTURE_2D, R.font_tex);
      // THE SOLID TEXEL, like every other primitive. ui_push is the raw batch entry —
      // the ui_* helpers set the mode and the UV, and these calls did neither, so the
      // ring sampled the atlas at (0,0), which font_bake leaves at zero for glyph 0.
      // Alpha 0 everywhere: the annulus has been built and drawn every scoped frame
      // and has never been visible.
      g_ui_mode = UIM_RAW;
      const float su_ = g_hf_solid_u, sv_ = g_hf_solid_v;
      for (int i = 0; i < SCOPE_SEGS; i++) {
        float a0 = (float)i * F_TAU / SCOPE_SEGS;
        float a1 = (float)(i + 1) * F_TAU / SCOPE_SEGS;
        float r0 = rad * 0.86f, r1 = rad + 0.6f;
        const float in_[4] = {0.02f, 0.02f, 0.025f, 0.0f};
        const float out_[4] = {0.02f, 0.02f, 0.025f, 0.96f};
        ui_push(cx + cosf(a0) * r0, cy + sinf(a0) * r0, su_, sv_, in_);
        ui_push(cx + cosf(a1) * r0, cy + sinf(a1) * r0, su_, sv_, in_);
        ui_push(cx + cosf(a1) * r1, cy + sinf(a1) * r1, su_, sv_, out_);
        ui_push(cx + cosf(a0) * r0, cy + sinf(a0) * r0, su_, sv_, in_);
        ui_push(cx + cosf(a1) * r1, cy + sinf(a1) * r1, su_, sv_, out_);
        ui_push(cx + cosf(a0) * r1, cy + sinf(a0) * r1, su_, sv_, out_);
      }
      float gap = rad * 0.055f, thin = rad * 0.010f, thick = rad * 0.021f;
      for (int i = 0; i < 4; i++) {
        float dx = (i == 0) - (i == 1), dy = (i == 2) - (i == 3);
        ui_line(cx + dx * gap, cy + dy * gap, cx + dx * rad * 0.46f,
                cy + dy * rad * 0.46f, thin, 0.04f, 0.04f, 0.05f, 0.95f);
        ui_line(cx + dx * rad * 0.46f, cy + dy * rad * 0.46f,
                cx + dx * rad * 0.92f, cy + dy * rad * 0.92f, thick,
                0.04f, 0.04f, 0.05f, 0.95f);
      }
      for (int i = 1; i <= 4; i++) {  // mil hashes below centre
        float y = cy + rad * 0.12f * (float)i;
        float hlen = rad * (i % 2 ? 0.045f : 0.075f);
        ui_line(cx - hlen, y, cx + hlen, y, thin, 0.04f, 0.04f, 0.05f, 0.9f);
      }
      ui_flush();
    }
  }
  glBindTexture(GL_TEXTURE_2D, R.font_tex);
  if (hud_on) {
  ui_begin();
  if (g_cfg.show_fps) {
    char text[16];
    snprintf(text, sizeof text, "%d FPS", fps);
    float tx = g_ui_vw - ui_text_width_tr(text, 0.7f, 0.6f) - 7;
    ui_text_sh(text, tx, 6, 0.7f, TXT_REG, 0.6f, C4(C_HAZE, 0.7f));
  }
  if (!g_menu_open && !cam_free && !g_vmorb.on) {
    float ccx = g_ui_vw * 0.5f, ccy = g_ui_vh * 0.5f;
    int in_play = p->alive && !g_match.over;
    // Crosshair: the gap IS the spread cone the stance opens. At rest and aimed it is
    // exactly the base gap, because the round is exact; hip carry and jump/slide open
    // it. Recoil is not in it — that is on screen already as the view.
    float ch_hide = p->wp.cur == WPN_SR ? 0.02f : 0.95f;
    if ((ads < ch_hide || third) && in_play) {  // 3rd person: crosshair even in ADS
      // 2.5x on the honest cone projection — pure readability — plus the HIT KICK, so
      // the reticle reacts to LANDING a round and not only to firing one.
      float gap = 3.0f + tanf(weapon_spread(p) * DEG2RAD) / tanf(fovy * 0.5f) * 450.0f
                + g_ch_kick;
      float a = 1.0f - ads / ch_hide;
      const float armlen = 4.6f;
      for (int i = 0; i < 4; i++) {
        float dx = (i == 0) - (i == 1), dy = (i == 2) - (i == 3);
        float x0 = ccx + dx * gap, y0 = ccy + dy * gap;
        float x1 = ccx + dx * (gap + armlen), y1 = ccy + dy * (gap + armlen);
        ui_capsule(x0, y0, x1, y1, 1.15f, 0.02f, 0.03f, 0.04f, 0.50f * a);
        ui_capsule(x0, y0, x1, y1, 0.62f, C_BONE, 0.97f * a);
      }
    }
    // Sight picture once snapped in, exactly point-of-aim — the AR's red dot only.
    float dot_a = f_clamp((ads - 0.78f) / 0.17f, 0.0f, 1.0f);
    if (dot_a > 0.0f && in_play && !third && p->wp.cur != WPN_SR) {
      // A soft radial bloom (the atlas soft cell) under a mid falloff and a saturated
      // core.
      ui_glow(ccx, ccy, 4.6f, 1.0f, 0.12f, 0.05f, 0.38f * dot_a);
      ui_disk(ccx, ccy, 2.0f, 1.0f, 0.14f, 0.05f, 0.45f * dot_a);
      ui_disk(ccx, ccy, 1.25f, 1.0f, 0.22f, 0.10f, 0.98f * dot_a);
    }
    // THE HITMARKER. Four diagonal strokes; the levers are the lessons: it POPS AND
    // SETTLES off g_hitmark_t (a distinct beat per round, even re-armed mid-life); the
    // outline is CONCENTRIC, not a drop shadow (the core is read against its border,
    // not against its own impact spark); a KILL IS BRIGHTER, NOT JUST REDDER (hue moves
    // into the outline, the core stays high-value — "sharper, not louder", same idiom
    // as SFX_HEAD_DUCK); the HEAD axis is a LAYER outside the base strokes.
    if (g_hitmark_t > 0) {
      int kill = g_hitmark_kind >= HM_KILL, head = g_hitmark_kind & 1;
      float span = (float)(kill ? KILLMARK_TICKS : HITMARK_TICKS);
      float e = (float)g_hitmark_t / span;        // 1 at birth -> 0 at death
      float pop = 1.0f + 0.28f * e * e * e * e;
      float a = f_clamp(e * 3.0f, 0.0f, 1.0f) * (kill ? 0.95f : 0.88f);
      float in = 4.4f * pop, out = (kill ? 11.4f : 9.0f) * pop;
      float r  = 1.0f, g = kill ? 0.74f : 0.96f, b = kill ? 0.64f : 0.96f;
      float orr = kill ? 0.78f : 0.0f, og = kill ? 0.05f : 0.0f, ob = kill ? 0.03f : 0.0f;
      float k = 0.7071f;
      for (int pass = 0; pass < (head ? 2 : 1); pass++) {
        float i0 = pass ? (out + 1.5f) : in, o0 = pass ? (out + 4.2f) : out;
        float hwd = pass ? 0.28f : 0.34f;
        for (int sx = -1; sx <= 1; sx += 2)
          for (int sy = -1; sy <= 1; sy += 2) {
            ui_capsule(ccx + (float)sx * i0 * k, ccy + (float)sy * i0 * k,
                       ccx + (float)sx * o0 * k, ccy + (float)sy * o0 * k,
                       hwd + 0.55f, orr, og, ob, 0.72f * a);
            ui_capsule(ccx + (float)sx * i0 * k, ccy + (float)sy * i0 * k,
                       ccx + (float)sx * o0 * k, ccy + (float)sy * o0 * k,
                       hwd, r, g, b, a);
          }
      }
    }
    if (in_play) {
      // Ammo, bottom-right.
      {
        // NUMBERS ONLY. One small quiet figure in each corner — the weapon and magazine
        // state is the count itself; anything more is furniture.
        float rxe = g_ui_vw - 16;
        float by = g_ui_vh - 24;
        int mag = WPN_DEF[p->wp.cur].mag, cur = p->wp.ammo[p->wp.cur];
        float a01 = f_clamp((float)cur / (0.25f * (float)mag), 0.0f, 1.0f);
        char am[16];
        snprintf(am, sizeof am, "%d/%d", cur, mag);
        float cr = 0.941f + (1.000f - 0.941f) * (1 - a01);
        float cg = 0.957f + (0.294f - 0.957f) * (1 - a01);
        float cb2 = 0.973f + (0.227f - 0.973f) * (1 - a01);
        float aa = 0.85f;
        if (p->wp.reload_t > 0) {
          float bl = 0.6f + 0.4f * sinf(g_ui_time * 6.0f);
          cr = 1.000f; cg = 0.549f; cb2 = 0.102f; aa = bl;
        }
        ui_text_sh(am, rxe - ui_text_width(am, 1.0f), by, 1.0f, TXT_REG, 0,
                   C4(cr, cg, cb2, aa));
      }
      // Health, bottom-left: the ammo figure's mirror — the same quiet
      // numeral, bleeding toward THREAT as it falls and pulsing when low.
      {
        float hp01 = f_clamp((float)p->hp / PLAYER_HP, 0.0f, 1.0f);
        float tt = 1.0f - hp01;
        float cr = 0.941f + (1.000f - 0.941f) * tt;
        float cg = 0.957f + (0.294f - 0.957f) * tt;
        float cb2 = 0.973f + (0.227f - 0.973f) * tt;
        char hps[8];
        snprintf(hps, sizeof hps, "%d", p->hp);
        float pulse = p->hp <= 25 ? 0.55f + 0.30f * sinf(g_ui_time * 7.0f) : 0.85f;
        ui_text_sh(hps, 16, g_ui_vh - 24, 1.0f, TXT_REG, 0, C4(cr, cg, cb2, pulse));
      }
      // Low-HP vignette: a breathing THREAT edge, gradient so it never
      // reads as a solid frame over the game.
      if (p->hp <= 25) {
        // A whisper, not a lava filter: the pulsing THREAT number in the
        // panel carries the state; the edge only echoes it.
        float va = (0.5f + 0.5f * sinf(g_ui_time * 7.0f)) * 0.09f + 0.05f;
        ui_vgrad(0, 0, g_ui_vw, 24, C4(C_THREAT, va), C4(C_THREAT, 0));
        ui_vgrad(0, g_ui_vh - 24, g_ui_vw, 24, C4(C_THREAT, 0), C4(C_THREAT, va));
        ui_hgrad(0, 0, 24, g_ui_vh, C4(C_THREAT, va), C4(C_THREAT, 0));
        ui_hgrad(g_ui_vw - 24, 0, 24, g_ui_vh, C4(C_THREAT, 0), C4(C_THREAT, va));
      }
    }

    // Incoming damage: a brief THREAT edge wash (gradients, not slabs) plus
    // one ARC per live attacker — an arc IS a direction at a glance, and the
    // chords fade toward the wedge ends so it has no hard corner.
    if (g_dmg_t > 0 && p->alive) {
      float k = (float)g_dmg_t / DMG_TICKS;
      ui_vgrad(0, 0, g_ui_vw, 22, C4(C_THREAT, 0.22f * k), C4(C_THREAT, 0));
      ui_vgrad(0, g_ui_vh - 22, g_ui_vw, 22, C4(C_THREAT, 0), C4(C_THREAT, 0.22f * k));
      ui_hgrad(0, 0, 22, g_ui_vh, C4(C_THREAT, 0.22f * k), C4(C_THREAT, 0));
      ui_hgrad(g_ui_vw - 22, 0, 22, g_ui_vh, C4(C_THREAT, 0), C4(C_THREAT, 0.22f * k));
    }
    for (int di = 0; di < DMG_SLOTS && p->alive; di++) {
      if (g_dmg[di].life <= 0) continue;
      float dk = (float)g_dmg[di].life / DMG_TICKS;
      float ddx = g_dmg[di].from.x - feet.x, ddz = g_dmg[di].from.z - feet.z;
      float rel = atan2f(ddx, -ddz) - yaw;
      const float R0 = 46.0f, HALF = 0.35f;      // 40 deg of arc
      for (int seg = 0; seg < 8; seg++) {
        float a0 = rel - HALF + (2.0f * HALF) * (float)seg / 8.0f;
        float a1 = rel - HALF + (2.0f * HALF) * (float)(seg + 1) / 8.0f;
        float m = 1.0f - fabsf((float)seg - 3.5f) / 4.6f;
        ui_line(ccx + sinf(a0) * R0, ccy - cosf(a0) * R0,
                ccx + sinf(a1) * R0, ccy - cosf(a1) * R0,
                2.5f, 0.05f, 0.01f, 0.01f, 0.50f * dk * m);
        ui_line(ccx + sinf(a0) * R0, ccy - cosf(a0) * R0,
                ccx + sinf(a1) * R0, ccy - cosf(a1) * R0,
                1.7f, C_THREAT, 0.95f * dk * m);
      }
    }

    // THE SCORELINE, top centre: my frags against my best RIVAL — the whole match state
    // a player needs mid-fight.
    if (!g_match.over) {
      int me = g_local_ent >= 0 ? g_match.kills[g_local_ent] : 0, lead = 0;
      for (int i = 0; i < MAX_ENTS; i++)
        if (i != g_local_ent && g_match.kills[i] > lead) lead = g_match.kills[i];
      char msc[8], lsc[8];
      snprintf(msc, sizeof msc, "%d", me);
      snprintf(lsc, sizeof lsc, "%d", lead);
      float pk = g_score_pop > 0 ? (float)g_score_pop / SCORE_POP_TICKS : 0.0f;
      char sc2[16];
      snprintf(sc2, sizeof sc2, "%s:%s", msc, lsc);
      float ms = 0.9f + 0.25f * pk * pk;
      int ahead = me >= lead && me > 0;
      ui_text_sh(sc2, ccx - ui_text_width_tr(sc2, ms, 0.8f) * 0.5f, 8, ms,
                 TXT_REG, 0.8f,
                 ahead ? C4(C_BLAZE, 0.8f)
                 : lead > me ? C4(C_HAZE, 0.85f) : C4(C_HAZE, 0.8f));
    }
    // ...and the multi-kill banner, under the crosshair rather than over it,
    // with a soft glow and a small entrance pop.
    if (g_streak_t > 0 && g_streak_n >= 2) {
      static const char *NM[5] = {"DOUBLE KILL", "TRIPLE KILL", "QUAD KILL",
                                  "PENTA KILL", "RAMPAGE"};
      const char *nm = NM[g_streak_n - 2 < 4 ? g_streak_n - 2 : 4];
      float ba = f_clamp((float)g_streak_t / 40.0f, 0.0f, 1.0f);
      float pop2 = 1.0f + 0.18f *
                   f_clamp(((float)g_streak_t - (STREAK_TICKS - 8)) / 8.0f, 0.0f, 1.0f);
      float bs = 0.85f * pop2;
      float bw = ui_text_width_tr(nm, bs, 1.8f);
      ui_text_ex(nm, ccx - bw * 0.5f, ccy + 26, bs, TXT_REG, 1.8f,
                 C4(C_BLAZE, 0.85f * ba));
    }

    // Kill feed top-right: chips that slide in from the right edge, newest first.
    {
      float fy = 32;
      for (int i = 0; i < KILLFEED_N; i++) {
        if (g_feed[i].life <= 0) continue;
        float fa = g_feed[i].life > 60 ? 1.0f : (float)g_feed[i].life / 60.0f;
        float tin = ui_easeout(
            f_clamp((float)(KILLFEED_TICKS - g_feed[i].life) / 7.0f, 0.0f, 1.0f));
        float slide = (1.0f - tin) * 24.0f;
        const char *kn = ent_name(g_feed[i].src), *vn = ent_name(g_feed[i].dst);
        int meK = g_feed[i].src == g_local_ent, meD = g_feed[i].dst == g_local_ent;
        int src_h = human_of_ent(g_feed[i].src) > 0;   // >0: a remote human
        int dst_h = human_of_ent(g_feed[i].dst) > 0;
        float knw = ui_text_width(kn, 0.8f), vnw = ui_text_width(vn, 0.8f);
        float chev = ui_text_width("\x01", 0.8f);
        float hsw = g_feed[i].hs ? ui_text_width("\x04", 0.8f) + 3 : 0;
        const float rowh = 9;
        // Pure quiet text at the ONE small HUD size — no plates, no shadows;
        // color alone carries who did what to whom.
        float rw = knw + 4 + chev + 3 + hsw + vnw;
        float tx = g_ui_vw - 10 - rw + slide, ty2 = fy;
        if (meK)        ui_text_sh(kn, tx, ty2, 0.8f, TXT_REG, 0, C4(C_BLAZE, 0.9f * fa));
        else if (src_h) ui_text_sh(kn, tx, ty2, 0.8f, TXT_REG, 0, C4(C_SIGNAL, 0.9f * fa));
        else            ui_text_sh(kn, tx, ty2, 0.8f, TXT_REG, 0, C4(C_BONE, 0.85f * fa));
        tx += knw + 4;
        ui_text_sh("\x01", tx, ty2, 0.8f, TXT_REG, 0, C4(C_HAZE, 0.8f * fa));
        tx += chev + 3;
        if (g_feed[i].hs) {
          ui_text_sh("\x04", tx, ty2, 0.8f, TXT_REG, 0, C4(C_THREAT, 0.9f * fa));
          tx += hsw;
        }
        if (meD)        ui_text_sh(vn, tx, ty2, 0.8f, TXT_REG, 0, C4(C_THREAT, 0.9f * fa));
        else if (dst_h) ui_text_sh(vn, tx, ty2, 0.8f, TXT_REG, 0, C4(C_SIGNAL, 0.9f * fa));
        else            ui_text_sh(vn, tx, ty2, 0.8f, TXT_REG, 0, C4(C_BONE, 0.8f * fa));
        fy += rowh + 2;
      }
    }

    // Death screen: the world stays visible; a scrim pulls the eye to the
    // lower third where the broadcast caption sits, and the respawn is a
    // progress capsule instead of a decimal readout.
    if (!p->alive && !g_match.over) {
      // The death frame is STAGED: an INK scrim from both edges controls whatever the
      // corpse camera happens to face, so the caption never stands on raw mud.
      ui_rect(0, 0, g_ui_vw, g_ui_vh, 0.04f, 0.03f, 0.04f, 0.30f);
      ui_vgrad(0, 0, g_ui_vw, g_ui_vh * 0.40f, C4(0, 0, 0, 0.55f), C4(0, 0, 0, 0));
      ui_vgrad(0, g_ui_vh * 0.42f, g_ui_vw, g_ui_vh * 0.58f,
               C4(0, 0, 0, 0), C4(0, 0, 0, 0.72f));
      float dy0 = g_ui_vh * 0.56f;
      const char *d1 = "ELIMINATED";
      float w1 = ui_text_width_tr(d1, 2.6f, 2.0f);
      ui_text_ex(d1, ccx - w1 * 0.5f + 1, dy0 + 1, 2.6f, TXT_BLACK, 2.0f,
                 C4(0.05f, 0.01f, 0.01f, 0.85f));
      ui_text_ex(d1, ccx - w1 * 0.5f, dy0, 2.6f, TXT_BLACK, 2.0f, C4(C_THREAT, 1));
      char d2[40];
      snprintf(d2, sizeof d2, "BY %s", ent_name(g_match.last_killer));
      ui_text(d2, ccx - ui_text_width(d2, 1) * 0.5f, dy0 + 30, 1, C_BONE, 0.92f);
      float rp = 1.0f - f_clamp((float)p->respawn_t / PLAYER_RESPAWN_TICKS, 0.0f, 1.0f);
      ui_rect(ccx - w1 * 0.5f, dy0 + 46, w1, 3, C_BONE, 0.14f);
      ui_rect(ccx - w1 * 0.5f, dy0 + 46, w1 * rp, 3, C_BLAZE, 0.95f);
      ui_text_ex("REDEPLOY", ccx - ui_text_width_tr("REDEPLOY", 0.8f, 1.5f) * 0.5f,
                 dy0 + 53, 0.8f, TXT_REG, 1.5f, C4(C_HAZE, 0.9f));
    }

    // Match over: winner hero over a top scrim, graticule ruler under it; the
    // scoreboard forces on below.
    if (g_match.over) {
      ui_vgrad(0, 0, g_ui_vw, 96, C4(0, 0, 0, 0.60f), C4(0, 0, 0, 0));
      char msg[32];
      if (g_match.winner == g_local_ent) snprintf(msg, sizeof msg, "VICTORY");
      else snprintf(msg, sizeof msg, "%s WINS", ent_name(g_match.winner));
      float w1 = ui_text_width_tr(msg, 2.6f, 2.2f);
      ui_text_ex(msg, ccx - w1 * 0.5f + 1, 25, 2.6f, TXT_BLACK, 2.2f,
                 C4(0.04f, 0.03f, 0.02f, 0.85f));
      ui_text_ex(msg, ccx - w1 * 0.5f, 24, 2.6f, TXT_BLACK, 2.2f, C4(C_BLAZE, 1));
      ui_rect(ccx - w1 * 0.5f, 47, w1, 1, C_BONE, 0.30f);
      // ONLINE THE REMATCH IS NOT A TRIGGER PULL. The fire press is gated off on a
      // client (it would restart this client's arena only), so the old string was an
      // instruction that did nothing; the server's countdown goes here instead.
      char subbuf[32];
      const char *sub = "PRESS FIRE FOR REMATCH";
      if (g_online) {
        snprintf(subbuf, sizeof subbuf, "NEW ROUND IN %d", match_over_secs());
        sub = subbuf;
      }
      ui_text_ex(sub, ccx - ui_text_width_tr(sub, 0.9f, 1.4f) * 0.5f, 54, 0.9f,
                 TXT_REG, 1.4f, C4(C_BONE, 0.85f));
      // NO VERSION LINE HERE. This used to carry the same VERSION+update status string
      // the entry screen does, on the reasoning that the match end is where the player
      // is already stopped — but what it actually did was put a build hash, a commit
      // and a "CHECKED 4 MIN AGO" under the name of whoever just won.
    }

    if (g_score_held || g_match.over) ui_scoreboard();
  }
  }
  if (!hud_on) {
    glBindTexture(GL_TEXTURE_2D, R.font_tex);
    ui_begin();
  }
  // The frosted backdrop exists to seat a panel over a FROZEN world.
  if (g_menu_open && session_pauses() && !g_home) ui_menu_backdrop();
  if (g_menu_open) ui_menu();
  ui_flush();
}

static void render_frame(const player_t *p, float alpha, int fps,
                         const cam_override_t *ov) {
  if (g_world_dirty) { world_upload(); g_world_dirty = 0; }
  msaa_update();
  post_update();
  shadow_update();
  R.frame_resolved = 0;
  glBindFramebuffer(GL_FRAMEBUFFER, frame_fbo());

  frame_cam_t cam = rf_camera(p, alpha, ov);
  v3 eye = cam.eye;
  mat4 vp = cam.vp;
  int cam_free = cam.cam_free, third = cam.third;
  float tp_dist = cam.tp_dist;
  float yaw = cam.yaw, pitch = cam.pitch, roll = cam.roll;
  float fovy = cam.fovy, aspect = cam.aspect;

  // Everything that casts is built BEFORE anything is drawn: the shadow pass draws the
  // same two VBOs the colour passes do, so the caster list cannot drift from the
  // visible geometry.
  rf_mark(0);
  g_cam_eye = eye;
  g_cam_frustum = frustum_of(&vp);
  g_cam_cull = 1;
  // The FP body exists only for the player's own live first-person camera:
  // vmorbit and the HOME macro shots re-aim the lens anywhere, and a torso in
  // their background band is the "author a close-up against BOTH weapons"
  // family of trap.
  scene_build(p, alpha, cam_free || (third && tp_dist > 0.95f),
              !cam_free && !third && !g_vmorb.on);
  g_cam_cull = 0;

  // Sun shadows. One ortho map over the whole arena, centred on it, far enough
  // back along the sun that nothing in the level is behind the near plane.
  int shadow_on = R.sm_fbo != 0 && g_cfg.shadows;
  rf_mark(1);
  if (shadow_on) rf_shadow_pass();
  else {
    // SHADOWS OFF SKIPS THE PASS, NEVER THE BINDING. Unit 0 belongs to the
    // shadow map for every lit pass of this frame (unit 2 to its raw twin), and
    // a unit holding a texture of another type under a declared sampler2DShadow
    // is undefined — so the map stays bound and the shader simply is not asked
    // for the read (uShadow 0, the gate at the top of sun_shadow).
    glBindTexture(GL_TEXTURE_2D, R.sm_tex);
  }

  // The screen-space half: geometry first, then what the screen can say about
  // it, before a single lit pixel is decided.
  rf_mark(2);
  rf_pre_pass(&cam);
  rf_mark(3);
  if (g_cfg.ssao) {
    rf_ao_pass(&cam);
  } else {
    // Unit 1 still carries the (stale) AO texture: the lit passes declare the
    // sampler whether or not they read it, and a unit that flips format
    // between a frame's draws costs llvmpipe a shader variant per flip
    // (measured: the world pass at 103 ms against 60 with the occlusion ON).
    gl_active_texture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, R.ao_tex[0]);
    gl_active_texture(GL_TEXTURE0);
    rt_bind(frame_fbo(), R.width, R.height);
  }

  rf_mark(4);
  rf_world_pass(&cam, shadow_on);

  // Dynamic scene: figures and round impact marks (solid), then the additive
  // light pass — tracers and sparks are glow, not geometry: depth-tested but not
  // depth-written, blended ONE/ONE.
  rf_mark(5);
  scene_draw_vp(&vp);

  rf_mark(6);
  if (g_cfg.ssr && surf_params().z > 0.0f) rf_ssr_pass(&cam);

  rf_mark(7);
  scope_result_t sr = rf_scope_pass(p, &cam);

  rf_mark(8);
  rf_vm_frame_t vmf;
  rf_glow_pass(p, alpha, &cam, sr.scope_on, &sr.svp, &vmf);

  rf_mark(9);
  rf_dust_pass(&cam);

  rf_mark(10);
  rf_vm_pass(p, &cam, &vmf);

  // The picture is finished here; everything after this line is interface.
  rf_mark(11);
  glare_pass(yaw, pitch, roll, fovy, aspect, &vp, eye, shadow_on);

  rf_hud_pass(p, &cam, fps, sr.scope);
  rf_mark(-1);

  // Leave the present FBO bound on both targets so glReadPixels/SwapBuffers
  // see it. glare_pass has normally finished the frame already, in which case
  // this is the latch's no-op and the HUD above was drawn straight into the
  // present buffer.
  msaa_resolve();
  glBindFramebuffer(GL_FRAMEBUFFER, R.present_fbo);
}

#define DEFAULT_SEED 1337u
