// =============================================================================
// render/figure.inc  —  fig_hand and the hand/grip helpers.
// Proof: vmhand, vmtrig, figcheck. One TU: included once by game.c, in
// definition order — moving a definition out is a semantic change.
// =============================================================================
// --- palm: wrist -> knuckle row, and the palm's own frame for the digits --
// THE BACK OF A HAND IS FLAT — only the fingers curl — so the palm is a straight
// tapered slab from the wrist to the knuckle row with a shallow cup in it, and the
// whole wrap lives in the digits. Every digit below used to be placed purely in the
// GRIP CYLINDER's polar frame — hand_pt(FAX[f], phi, rad) — while the palm is a chord
// from the CALLER's wrist to pk, so this also builds the palm's OWN frame (pbx/pu/
// root_c/root_x) the digits root into. Outputs: pk/rk (the knuckle-row point and its
// radius — also feeds the far-tier one-mass hand), p3/p4/prx3/prx4 (pm[3]/pm[4] and
// their authored half-widths — also feeds the far-tier hand's own root), pbx/pu/
// root_c/root_x (the digit root frame), and out_palm_dir (the caller's own out-param,
// written straight through, per the palm's own axis, so the arm can enter ALONG it).
static void fig_hand_palm(v3 wrist, v3 gc, v3 ga, float gr, v3 col,
                          v3 e1, v3 e2, float KN_PH, v3 *out_palm_dir,
                          v3 *out_pk, float *out_rk, v3 *out_p3, v3 *out_p4,
                          float *out_prx3, float *out_prx4,
                          float *out_prz3, float *out_prz4,
                          v3 *out_pbx, v3 *out_pu, v3 *out_root_c,
                          float *out_root_x) {
  // The bite is EXPLICIT, so the palm's inner face stays 3 mm inside the tube whatever
  // its thickness is set to.
  const float HAND_BITE = 0.0030f;
  static const float PRX[5] = {0.0267f, 0.0336f, 0.0399f, 0.0484f, 0.0452f};
  static const float PRZ[5] = {0.0168f, 0.0208f, 0.0206f, 0.0164f, 0.0113f};
  float rk = gr + PRZ[4] - HAND_BITE;
  v3 pk = hand_pt(gc, ga, e1, e2, 0.0f, KN_PH, rk);
  // The cup: 3 mm of sag toward the tube across the middle of the slab, which is
  // enough to stop the back of the hand reading as a plank and far too little to
  // trigger the bevel clamp above.
  v3 mid = v3_lerp(wrist, pk, 0.5f);
  v3 md = v3_sub(mid, gc);
  md = v3_sub(md, v3_scale(ga, v3_dot(md, ga)));
  v3 inw = v3_dot(md, md) > 1e-9f ? v3_scale(v3_norm(md), -0.0038f)
                                  : (v3){{0, 0, 0}};
  // A glove is not one piece of leather: it has a padded knuckle panel and a cuff, and
  // those are the only two landmarks the BACK of a hand has — which is the side the
  // player looks at all match.
  v3 knuck = v3_scale(col, 0.865f);
  v3 heel  = v3_scale(col, 0.925f);
  fnode_t pm[5];
  const float PT[5] = {0.0f, 0.0f, 0.42f, 0.78f, 1.0f};
  for (int i = 1; i < 5; i++) {
    pm[i].p = v3_lerp(wrist, pk, PT[i]);
    // The cup grows with distance from the wrist.
    pm[i].p = v3_add(pm[i].p, v3_scale(inw, PT[i] * PT[i]));
    // The panel covers the last TWO nodes, not just the knuckle row: coloured only at
    // the distal node it lands where the fingers already are and the back of the hand
    // stays one featureless dome, which at 60 x 40 px is the whole hand.
    pm[i].col = pm[i].bcol = i >= 3 ? knuck : col;
    pm[i].rx = PRX[i]; pm[i].rz = PRZ[i]; pm[i].roll = 0; pm[i].ao = 0.0f;
  }
  // Node 0 reaches BACK past the wrist along the palm's own direction, so whatever the
  // arm is — a sleeve chain on the figure, a forearm chain on the viewmodel — swallows
  // this end cap.
  v3 bk = v3_norm(v3_sub(pm[1].p, pm[2].p));
  pm[0].p = v3_add(pm[1].p, v3_scale(bk, 0.012f));
  pm[0].col = pm[0].bcol = heel;
  pm[0].rx = PRX[0]; pm[0].rz = PRZ[0]; pm[0].roll = 0; pm[0].ao = 0.0f;
  // The palm's own axis, handed back so the arm can enter ALONG it.
  if (out_palm_dir) *out_palm_dir = v3_scale(bk, -1.0f);
  g_fig_palm_mm = v3_len(v3_sub(pk, wrist)) * 1000.0f;
  fig_chain(pm, 5, 12, 1, 1, ga);   // hint = ga, so rx is the BREADTH

  // --- THE PALM'S OWN FRAME, AND WHY THE DIGITS HAVE TO BE ROOTED IN IT -----
  v3 pu  = v3_norm(v3_sub(pm[4].p, pm[3].p));
  v3 pbx = v3_sub(ga, v3_scale(pu, v3_dot(ga, pu)));
  pbx = v3_dot(pbx, pbx) > 1e-9f ? v3_norm(pbx) : e1;
  // Where a digit may leave the hand: on the palm's CENTRE LINE, so the palm's
  // whole local thickness is available to swallow the root ring, ROOT_IN of the
  // last palm bone back from the knuckle row — where the section is 14.1 x 46.9
  // mm against 11.3 x 45.2 at the row itself.
  const float ROOT_IN = 0.55f;
  v3    root_c = v3_lerp(pm[4].p, pm[3].p, ROOT_IN);
  float root_x = PRX[4] + (PRX[3] - PRX[4]) * ROOT_IN;

  *out_pk = pk; *out_rk = rk;
  *out_p3 = pm[3].p; *out_p4 = pm[4].p;
  *out_prx3 = PRX[3]; *out_prx4 = PRX[4];
  *out_prz3 = PRZ[3]; *out_prz4 = PRZ[4];
  *out_pbx = pbx; *out_pu = pu; *out_root_c = root_c; *out_root_x = root_x;
}

// One finger of the close-range four — the loop body `fig_hand` used to run for
// f = 0..3 in place, now the callee it dispatches to. `f` selects which finger
// (little->index); everything else is what that loop pulled from its enclosing
// scope: the grip frame, the palm's own frame the root is anchored in (pbx/pu/
// root_c/root_x, and prx4 = PRX[4], the palm's own knuckle-row half-width the
// root's spread is measured against), and rk (the palm's knuckle radius, for the
// root-node dome).
static void fig_hand_digit(v3 gc, v3 ga, v3 e1, v3 e2, float gr, float rk,
                           float side, int style, v3 col,
                           const v3 *trig_pt, int slid,
                           v3 pbx, v3 pu, v3 root_c, float root_x, float prx4,
                           int f) {
  // Each is an ARC about the grip axis: root buried in the palm's knuckle end, then
  // two thirds of a turn round the tube.
  static const float FAX[4] = {-0.0300f, -0.0100f, 0.0100f, 0.0295f}; // little->index
  // THE MIDDLE FINGER IS THE THICKEST, not the index.
  static const float FGR[4] = {0.0098f, 0.0110f, 0.0118f, 0.0111f};  // girth, x1.13
  // 6-8 degrees PAST THE CREST of the tube, measured on the player's own frame rather
  // than judged. Stopping at 12 o'clock put the whole finger row on the FAR side of
  // the fore-end, so the only digit the player ever saw was the thumb; going 13
  // degrees past laid all four tips FLAT across the top, which reads as "laid over
  // it" instead of "wrapped around it". The distal node also pitches further in,
  // because the last phalanx of a curled finger points at the palm.
  // Index-most-closed, not middle-most-closed: on a fore-end the index is the highest
  // and most wrapped finger. FKN also gave index and ring the same knuckle height, so
  // the arc across the knuckles was flat exactly where it should peak.
  // WRAP is x1.12 over the authored values — a closed hand on a 26 mm pistol grip
  // carries its tips ~200 degrees round, and at 139-170 the four digits read as lumps
  // stacked against the grip. Bounded by vmtrig: past x1.24 the index's own sweep
  // starts crowding the guard's front strap.
  static const float FSW[4] = {2.710f, 3.069f, 3.203f, 3.315f};      // wrap, rad
  static const float FLF[4] = {0.0015f, 0.0006f, 0.0000f, 0.0009f};  // radial lift
  static const float FKN[4] = {-0.22f, -0.08f, 0.01f, 0.06f};        // knuckle arc
  // Recomputed here from `side` rather than threaded from the wrapper: same
  // closed-form as fig_hand's own copy, bit-identical by construction — kept
  // local to spare a parameter rather than piped through as one.
  const float KN_PH = side * 1.52f;
  float r = FGR[f], rr = gr + r - 0.0034f + FLF[f];
  float ph0 = KN_PH + side * FKN[f];
  // THE TRIGGER FINGER. Four identical curled digits clamped on a pistol grip is a
  // clamp, not a firing grip: what says "about to shoot" is one finger doing
  // something different from the other three.
  float sweep = FSW[f], reach = 0.0f;
  // HEEL grip: the fingers hold the FAR FLANK, they do not crest the top.
  if (style == 2) sweep *= 0.66f;
  int aimed = style == 1 && f == 3 && trig_pt != NULL && !slid;
  // The whole hand squeezes, not just the index.
  if (style == 1 && !aimed) sweep += TRIG_GRIP * g_gun_trig_s;
  if (style == 1 && f == 3 && !aimed) { sweep = 1.42f; reach = 0.0125f; }
  fnode_t fg[6];   // 5 authored + one DOME ring, below
  // Node 0 sits BEHIND the knuckle row and inside the palm, so the root cap is
  // buried whatever the wrap angle; node 1 is the METACARPAL HEAD and is
  // deliberately fat enough to stand ~3 mm proud of the palm's distal end, which is
  // the only landmark the back of a hand has and the thing whose absence made this
  // read as a spade from the one angle the player looks at it from.
  const float FT[5]  = {-0.16f, 0.08f, 0.46f, 0.76f, 1.00f};
  // 0.86 / 0.72 on the ROOT, not 1.16 / 0.90.
  const float FRX[5] = { 0.86f, 1.16f, 1.04f, 0.94f, 0.76f};
  const float FRZ[5] = { 0.72f, 1.11f, 0.94f, 0.84f, 0.64f};
  // The aimed index runs a Bezier instead of an arc: A = the metacarpal head, where
  // the digit leaves the palm; B = the blade, pulled back along the guard's own
  // axis by the finger's own radius so the PAD touches it rather than the tip's
  // centre landing inside the steel; C = out in front of the knuckle, which is what
  // bows the proximal phalanx away from the grip and takes the middle of the finger
  // clear of the guard's rear upright.
  v3 tA = {{0, 0, 0}}, tB = tA, tC = tA;
  if (aimed) {
    // The METACARPAL joint flexes with the press, which swings the whole digit
    // rather than only its last two nodes.
    float aph = ph0 + side * (0.30f + TRIG_MCP * g_gun_trig_s);
    float arad = rr + 0.0018f;
    tA = hand_pt(gc, ga, e1, e2, FAX[f] - 0.0050f, aph, arad);
    // The tip's own half-thickness minus the squash: the pad's SURFACE
    // lands on the steel and deforms into it by TRIG_PAD_SQUASH, which is
    // what vmtrig's `pad` reads back (-0.8 mm at rest, -1.9 pressed).
    tB = v3_add(*trig_pt,
                v3_scale(g_gun_trig_n, r * FTIP_RZ - TRIG_PAD_SQUASH));
    // THE CONTROL POINT IS THE ARC'S OWN TANGENT, and that is a manifold
    // requirement rather than a nicety.
    v3 tg = v3_sub(hand_pt(gc, ga, e1, e2, FAX[f] - 0.0050f,
                           aph + side * 0.08f, arad), tA);
    tg = v3_dot(tg, tg) > 1e-12f ? v3_norm(tg) : v3_norm(v3_sub(tB, tA));
            // 0.35, not 0.62, and the departure 0.30 rad rather than 0.60: with the
            // Bezier leaving a third of a radian further round the grip and bowing
            // out by two thirds of its own chord, the index left the knuckle, ran
            // AROUND the grip and then turned back to the trigger — a hard V that
            // read as a folded sausage from every angle behind the weapon.
    tC = v3_add(tA, v3_scale(tg, 0.35f * v3_len(v3_sub(tB, tA))));
  }
  for (int i = 0; i < 5; i++) {
    float t = FT[i];
    // -0.0074, not -0.0054, on the distal pull-in.
    float rad = i == 0 ? rk - 0.0022f
              : i == 1 ? rr + 0.0016f : rr - 0.0074f * t * t;
    rad += reach * t * t;              // straightens off the grip's front
    fg[i].p = hand_pt(gc, ga, e1, e2, FAX[f], ph0 + side * sweep * t, rad);
    if (aimed && i >= 2) {
      // THE PRESS SLIDES THE NODES ALONG THE CURVE, IT DOES NOT BOW IT. The tip is
      // pinned to the blade and the two proximal nodes sit on the grip arc, so the
      // only thing a press can shape is what happens between them — and shaping it
      // with the CONTROL POINT was wrong: measured, pushing tC out by 0.30 took
      // node 3's turn from 71.5 to 92.8 degrees on the SR, and fig_chain answers a
      // corner it cannot fillet by collapsing the cross-section, so the fingertip
      // pinched and then flared into a trumpet exactly when the trigger was pulled.
      float u = (t - FT[2]) / (1.0f - FT[2]);
      u += TRIG_CURL * g_gun_trig_s * u * (1.0f - u);
      fg[i].p = fig_trig_pt(tA, tC, tB, u);
    }
    fg[i].col = fg[i].bcol = col;
    fg[i].rx = r * (aimed && i == 4 ? FTIP_RX : reach > 0.0f && i == 4 ? 0.80f : FRX[i]);
    fg[i].rz = r * (aimed && i == 4 ? FTIP_RZ : reach > 0.0f && i == 4 ? 0.74f : FRZ[i]);
    // THE GUARD IS THE ONE PART OF THE WEAPON A HAND MAY NOT SHARE SPACE WITH.
    // Everywhere else in this model neighbours interpenetrate on purpose and the
    // burial is hidden inside a solid; nothing hides a 6.8 mm wire crossing a 24 mm
    // finger.
    fg[i].p = fig_guard_clear(fg[i].p, fmaxf(fg[i].rx, fg[i].rz));
    // The root node lives inside the palm and the palm's own corner is
    // legitimately under the receiver, so only the nodes that are OUT on the
    // weapon are cleared of it. 0.72 of the girth rather than all of it: a
    // digit resting against the receiver's flank should touch it, which is
    // this file's contact rule, not stand off it.
    if (i) fg[i].p = fig_body_clear(fg[i].p, fmaxf(fg[i].rx, fg[i].rz) * 0.72f);
    // The pad ROLLS on the shoe as the shoe turns under it. Only the tip:
    // rolling the whole digit would twist the wrap off the grip.
    fg[i].roll = aimed && i == 4 ? side * TRIG_ROLL * g_gun_trig_s : 0.0f;
    // AO 0.88..0.96, not 0.58..0.66.
    fg[i].ao = 0.88f + 0.020f * (float)i;
  }
  // A KNUCKLE IS A POINT ON THE PALM, NOT A BEARING ON THE TUBE. Node 0 is the only
  // node of a digit that is never seen and its only job is ATTACHMENT, so it
  // belongs in the palm's frame while nodes 1-4 stay on the wrap.
  const float ROOT_SPREAD = 0.92f, ROOT_BONE = 0.026f;
  {
    v3 a = v3_add(root_c,
                  v3_scale(pbx, FAX[f] * (root_x / prx4) * ROOT_SPREAD));
    float bl = v3_len(v3_sub(fg[1].p, a));
    // Sliding further back is always safe: pu runs down the palm's centre
    // line into thicker section, so extra bone buys extra burial.
    if (bl < ROOT_BONE) a = v3_sub(a, v3_scale(pu, ROOT_BONE - bl));
    fg[0].p = a;
  }
  // The aimed index gets its own probe row so `vmtrig` can ask about the ONE digit
  // that reaches into the weapon.
  {
    v3 d = v3_sub(fg[4].p, fg[3].p);
    float dl = v3_len(d);
    v3 dir = dl > 1e-6f ? v3_scale(d, 1.0f / dl) : ga;
    fg[5] = fg[4];
    fg[5].rx = fg[4].rx * 0.34f;
    fg[5].rz = fg[4].rz * 0.34f;
    fg[5].p = v3_add(fg[4].p, v3_scale(dir, fmaxf(fg[4].rx, fg[4].rz) * 0.58f));
    fg[5].p = fig_guard_clear(fg[5].p, fmaxf(fg[5].rx, fg[5].rz));
    fg[5].p = fig_body_clear(fg[5].p, fmaxf(fg[5].rx, fg[5].rz) * 0.72f);
  }
  const char *pv = g_probe_on && g_nprobe ? g_probe[g_nprobe - 1].name : NULL;
  if (aimed) probe("trigfinger");
  fig_chain(fg, 6, FIG_DIGIT_K, 1, 1, ga);
  if (aimed) probe(pv);
}

// The thumb: the OPPOSITION, and the strongest grip cue there is — a hand whose
// digits all wrap the same way is a clamp. Self-contained given the palm's own
// knuckle point `pk` (the root has to be BURIED in the palm, not sitting on its
// surface) and the grip frame; it needs none of the other digits' root frame.
static void fig_hand_thumb(v3 wrist, v3 pk, v3 gc, v3 ga, v3 e1, v3 e2,
                           float gr, float side, int style, v3 col) {
  // The root has to be BURIED, and it was not: 25.0 mm along ga plus its own 12.8 mm
  // of half-breadth put its outer extent 3.8 mm outside a palm that is 34.0 mm there,
  // so the thumb's proximal end sat ON the palm surface with a dark seam under it and
  // read as a rod glued to the hand.
  v3 tb0 = v3_add(v3_lerp(wrist, pk, 0.42f), v3_scale(ga, 0.0150f));
  v3 tb1 = v3_add(v3_lerp(wrist, pk, 0.62f), v3_scale(ga, 0.0382f));
  // Tip at 100 degrees round the near side; the index tip comes over the top at about
  // 74 degrees the other way, so the two end ~26 mm apart on a 28 mm tube. That gap
  // IS the grip. Each node rides so its INNER face is 3 mm inside the surface — the
  // same HAND_BITE the palm and the fingers obey — because the thumb is the one digit
  // the player actually sees and it was the only one that never touched the weapon.
  // A THUMB ON A PISTOL GRIP DOES NOT WRAP IT: it lies ALONG the frame, because above
  // a pistol grip there is a RECEIVER and a thumb that keeps climbing runs into it —
  // the tip came out the FAR side of the AR's receiver as a severed blob, invisible to
  // every distance-based proof because the thumb is not inside anything, it is
  // THROUGH it. The support hand keeps the full wrap: a handguard has nothing above
  // it, and there the C the thumb makes with the fingers is the whole grip cue.
  // ...and it TIGHTENS with the press: the thumb is 3627 px of the AR hip frame
  // against the index finger's 12, so a squeeze that does not move it is a squeeze the
  // player cannot see.
  const float TWRAP = (style == 1 ? 0.68f : 1.0f) *
                      (1.0f + (style == 1 ? 0.06f * g_gun_trig_s : 0.0f));
  v3 tb2, tb3, tb4;
  if (style == 2) {
    // FORWARD THUMB. The heel grip trades the C's opposition cue for the drive-
    // forward cue: phalanges at INCREASING axial stations along ga (little-finger ->
    // index -> muzzle) at a nearly constant bearing on the near-upper flank, radius
    // easing down so the pad RIDES the handguard surface (same HAND_BITE philosophy
    // as the wrap).
    tb2 = hand_pt(gc, ga, e1, e2, 0.0420f, -side * 0.84f, gr + 0.0070f);
    tb3 = hand_pt(gc, ga, e1, e2, 0.0560f, -side * 0.76f, gr + 0.0052f);
    tb4 = hand_pt(gc, ga, e1, e2, 0.0660f, -side * 0.70f, gr + 0.0033f);
  } else {
    tb2 = hand_pt(gc, ga, e1, e2, 0.0455f, -side * 0.62f * TWRAP, gr + 0.0092f);
    tb3 = hand_pt(gc, ga, e1, e2, 0.0430f, -side * 1.28f * TWRAP, gr + 0.0079f);
    tb4 = hand_pt(gc, ga, e1, e2, 0.0395f, -side * 1.76f * TWRAP, gr + 0.0040f);
  }
  fnode_t th[5] = {
    {tb0, col, col, 0.0119f, 0.0132f, 0, 0.86f},
    {tb1, col, col, 0.0134f, 0.0150f, 0, 0.92f},
    {tb2, col, col, 0.0123f, 0.0137f, 0, 0.96f},
    {tb3, col, col, 0.0110f, 0.0122f, 0, 0.96f},
    // The last node domes the tip instead of leaving a flat octagon facing
    // whatever the camera happens to be. Same trick on the fingers.
    {tb4, col, col, 0.0071f, 0.0078f, 0, 0.96f}};
  // The thumb sweeps 1.9 rad over four bones, so every interior node earns a bevel
  // PAIR — five nodes is eight rings, which is more geometry than the whole far-tier
  // hand it sits on.
  for (int i = 0; i < 5; i++) {
    th[i].p = fig_guard_clear(th[i].p, fmaxf(th[i].rx, th[i].rz));
    th[i].p = fig_body_clear(th[i].p, fmaxf(th[i].rx, th[i].rz) * 0.72f);
  }
  g_fig_jit = 0.0f;
  if (g_fig_k >= 16) {
    fig_chain(th, 5, FIG_DIGIT_K, 1, 1, ga);
  } else {
    fnode_t t3[3] = {th[0], th[2], th[4]};
    t3[2].rx = 0.0082f; t3[2].rz = 0.0092f;
    fig_chain(t3, 3, 8, 1, 1, ga);
  }
  g_fig_jit = 1.0f;
}

// `style`: 1 = trigger hand (aimed index, TWRAP thumb cut), 0 = support hand
// closing a full C-wrap (magazine, bolt knob — anything gripped ACROSS), 2 =
// the support HEEL grip: palm heel under the handguard, fingers wrapping the
// far flank, THUMB POINTING FORWARD along the near-upper flank — the modern
// C-clamp-family hold both weapons now share on the fore-end.
static void fig_hand(v3 wrist, v3 gc, v3 ga, float gr, float side, v3 col,
                     int style, const v3 *trig_pt, v3 *out_palm_dir) {
  ga = v3_norm(ga);
  v3 w = v3_sub(wrist, gc);
  float aw = v3_dot(w, ga);                     // where along the tube the hand is
  v3 e1 = v3_sub(w, v3_scale(ga, aw));
  float rw = v3_len(e1);
  if (rw < 1e-4f) {                             // wrist on the axis: any bearing
    e1 = v3_cross(ga, (v3){{0, 1, 0}});
    if (v3_dot(e1, e1) < 1e-8f) e1 = v3_cross(ga, (v3){{1, 0, 0}});
    rw = gr + 0.050f;
  }
  e1 = v3_norm(e1);
  v3 e2 = v3_cross(ga, e1);        // (e1, e2, ga) right-handed BY CONSTRUCTION
  // A HAND IS A FIXED SIZE. The palm runs from the wrist, which is the caller's, to the
  // knuckle row, which is the weapon's — and if those two get far apart the palm simply
  // stretches to span the gap.
  const float HAND_REACH_LO = gr + 0.026f, HAND_REACH_HI = gr + 0.088f;
  // A hand is a fixed size in EVERY direction, and this clamp once fixed only one
  // of them: `rw` is the distance from the tube's AXIS, `aw` how far ALONG it the
  // wrist sits, and passing `aw` through untouched let a wrist a metre up the
  // barrel still produce a metre-long palm. 95 mm clears every live pose measured
  // and still catches a corpse's draped wrist and the hard-landing reach.
  const float HAND_AX_MAX = 0.095f;
  float rc = f_clamp(rw, HAND_REACH_LO, HAND_REACH_HI);
  float ac = f_clamp(aw, -HAND_AX_MAX, HAND_AX_MAX);
  // Whether the clamp had to MOVE the weapon out from under the wrist.
  int slid = (rc != rw || ac != aw);
  g_fig_wrist_mm = (rw - gr) * 1000.0f;
  g_fig_clamped = slid;
  if (slid) {
    gc = v3_sub(wrist, v3_add(v3_scale(ga, ac), v3_scale(e1, rc)));
    rw = rc; aw = ac;
  }

  // The knuckle row: 62 degrees round the tube from the wrist's bearing, riding 10.5 mm
  // off the surface so the palm's inner face bites 3 mm INTO it.
  const float KN_PH = side * 1.52f;

  v3 pk, p3, p4, pbx, pu, root_c;
  float rk, prx3, prx4, prz3, prz4, root_x;
  fig_hand_palm(wrist, gc, ga, gr, col, e1, e2, KN_PH, out_palm_dir,
                &pk, &rk, &p3, &p4, &prx3, &prx4, &prz3, &prz4,
                &pbx, &pu, &root_c, &root_x);

  // SEPARATE FINGERS ARE THE MOST EXPENSIVE THING ON THE FIGURE, so the gate is the
  // tightest tier: 16 = inside 6 m.
  if (g_fig_k >= 16) {
    // --- four fingers, close range only ------------------------------------
    g_fig_jit = 0.0f;    // a digit is not a crate — see the note on g_fig_jit
    for (int f = 0; f < 4; f++) {
      fig_hand_digit(gc, ga, e1, e2, gr, rk, side, style, col, trig_pt, slid,
                     pbx, pu, root_c, root_x, prx4, f);
    }
    g_fig_jit = 1.0f;
  } else {
    // Far tier: ONE mass, and it is STRAIGHT — two nodes, no interior node, therefore
    // no bevel and nothing that can be thinned. Not a simplification for looks: it is
    // the only shape this primitive can hold at this width, because fig_chain sizes a
    // bevel off the profile's CIRCUMRADIUS, so a 40 mm slab turning 67 degrees over a
    // 12 mm bone asked for 26.9 mm of fillet, got 7.3, and was thinned to 27%.
    // The root is the PALM's, exactly as the four fingers' roots are. A 2-node chain has
    // ONE ring plane, perpendicular to its own bone, so a full-breadth slab rooted on
    // the tube ARC drives its corners straight out through the back of the hand
    // (measured 17.9 mm, wandering with the pose). Rooted on the palm's centre line the
    // residual is sub-pixel. It has to stay TWO nodes, so sub-pixel rather than zero is
    // the target; the narrower root is also the anatomy — four merged fingers are
    // ~69 mm across, not the whole hand's 90.
    const float FAR_IN = 0.80f;
    fnode_t fg[2];
    fg[0].p = v3_lerp(p4, p3, FAR_IN);
    fg[1].p = hand_pt(gc, ga, e1, e2, -0.0015f, KN_PH + side * 1.34f, gr + 0.0098f);
    fg[0].col = fg[0].bcol = col; fg[1].col = fg[1].bcol = col;
    fg[0].rx = (prx4 + (prx3 - prx4) * FAR_IN) * 0.72f;
    fg[0].rz = (prz4 + (prz3 - prz4) * FAR_IN) * 0.78f;
    fg[1].rx = 0.0340f; fg[1].rz = 0.0096f;
    fg[1].p = fig_guard_clear(fg[1].p, fg[1].rx);
    fg[0].roll = fg[1].roll = 0; fg[0].ao = 0.90f; fg[1].ao = 0.96f;
    fig_chain(fg, 2, 8, 1, 1, ga);
  }

  // --- thumb: the OPPOSITION, and the strongest grip cue there is -----------
  fig_hand_thumb(wrist, pk, gc, ga, e1, e2, gr, side, style, col);
}

// ONE FOREARM, FOR BOTH VIEWS. The arm the player sees in first person and the arm on
// his own body in third person were two different objects: nine nodes with a gauntlet,
// a pronation roll and a five-step value ramp at profile 12, against three nodes of
// plain tapered sleeve at profile 8.
static void fig_forearm(v3 wrist, v3 back, v3 pd, v3 lat, float side,
                        v3 sleeve, v3 gnt, const v3 *elbow, int k) {
  // A MONOTONE ramp, and only three steps in it.
  v3 slv1 = v3_scale(sleeve, 0.94f), slv2 = v3_scale(sleeve, 0.86f);
  v3 slv3 = v3_scale(sleeve, 0.76f), slv4 = v3_scale(sleeve, 0.58f);
  float sc = 1.0f;
  if (elbow) {
    float l = v3_len(v3_sub(*elbow, wrist));
    if (l > 0.06f) { sc = l / 0.268f; back = v3_scale(v3_sub(*elbow, wrist), 1.0f / l); }
  }
  // The ellipse ROLLS down the arm, which is pronation and is why a real forearm
  // looks different from every angle while a swept tube looks the same from all
  // of them. `fnode_t.roll` costs nothing — it turns the ring in the transported
  // frame — and 0 at the wrist to ~17 degrees at the elbow is the twist a hand
  // on a fore-end actually puts in. The small lateral drift is the ulnar bow.
  #define FAN(T, RX, RZ, C, AO)                                            \
    {v3_add(v3_add(wrist, v3_scale(back, (T) * sc)),                       \
            v3_scale(lat, side * (T) * sc * 0.075f)), C, C, RX, RZ,        \
     side * (T) * 1.10f, AO}
  fnode_t fa[9] = {
    // Node 0 sits 26 mm INSIDE the palm, along the PALM's own axis rather than the
    // arm's, so the sleeve's end cap is buried in a cross-section it actually fits
    // inside.
    {v3_add(wrist, v3_scale(pd, 0.026f)), sleeve, sleeve, 0.0240f, 0.0122f, 0, 0.0f},
    // THE CUFF IS A FAT NODE OF THIS CHAIN, not a nested one: as its own chain it had
    // to cross the sleeve's surface to bury its far cap, and two 12-gons transported
    // from DIFFERENT first bones are angularly out of phase, so the intersection came
    // out as a zig-zag torn edge round the whole wrist.
    FAN(-0.010f, 0.0316f, 0.0234f, gnt,    0.99f),   // cuff lip, proud in BOTH axes
    FAN( 0.008f, 0.0342f, 0.0260f, gnt,    0.96f),   // cuff crown, tight to the wrist
    FAN( 0.030f, 0.0338f, 0.0254f, gnt,    0.93f),
    FAN( 0.050f, 0.0368f, 0.0278f, sleeve, 0.90f),   // leather -> slate
    FAN( 0.088f, 0.0392f, 0.0302f, slv1,   0.85f),
    FAN( 0.140f, 0.0434f, 0.0338f, slv2,   0.77f),
    FAN( 0.200f, 0.0470f, 0.0370f, slv3,   0.62f),   // the flexor belly
    // -2.9 degrees on the last band, not -4.7: a band that pinches out hard
    // reads as an amputated stub in any debug view, where a gentle one reads as
    // an arm continuing off frame.
    FAN( 0.268f, 0.0426f, 0.0336f, slv4,   0.46f)};
  #undef FAN
  // Past 12 m the cuff's three rings are under two pixels apart and the whole gauntlet
  // is one band, so the far tier keeps only the stations that carry the silhouette: the
  // buried root, the cuff crown, the leather/slate boundary, the flexor belly and the
  // elbow.
  if (g_fig_k >= 12) {
    fig_chain(fa, 9, k, 1, 1, lat);
  } else {
    fnode_t f5[5] = {fa[0], fa[2], fa[4], fa[7], fa[8]};
    fig_chain(f5, 5, k, 1, 1, lat);
  }
}

// A HOLLOW round tube: two concentric walls closed by an annulus at each end. fig_chain
// closes a chain with a fan to its own axis, which is right for a solid body and
// exactly wrong for anything the player has to see THROUGH — the window of a red dot,
// the bore of a muzzle brake.
static void fig_tube(v3 a, v3 b, float rox, float roz, float rix, float riz,
                     int k, v3 col, v3 hint) {
  if (fig_diss_cut(v3_lerp(a, b, 0.5f))) return;
  if (!g_prof_ready) fig_prof_init();
  v3 ax = v3_sub(b, a);
  float l = v3_len(ax);
  if (l < 1e-5f || rix >= rox || riz >= roz || rix <= 0.0f) return;
  ax = v3_scale(ax, 1.0f / l);
  if (k > g_fig_k) k = g_fig_k < 4 ? 4 : g_fig_k;
  v3 nx = fig_perp(ax, hint);
  fring_t ao = fig_ring(a, ax, nx, rox, roz, k), bo = fig_ring(b, ax, nx, rox, roz, k);
  fring_t ai = fig_ring(a, ax, nx, rix, riz, k), bi = fig_ring(b, ax, nx, rix, riz, k);
  int round = fig_round(k);
  v3 rn[FIG_KMAX], fn[FIG_KMAX];
  for (int i = 0; i < k; i++) {
    rn[i] = v3_norm(v3_sub(ao.v[i], a));                       // per vertex
    v3 e = v3_sub(ao.v[(i + 1) % k], ao.v[i]);
    v3 f = v3_cross(e, ax);                                    // per face
    fn[i] = v3_dot(f, f) > 1e-14f ? v3_norm(f) : rn[i];
    if (v3_dot(fn[i], rn[i]) < 0.0f) fn[i] = v3_scale(fn[i], -1.0f);
  }
  for (int i = 0; i < k; i++) {
    int j = (i + 1) % k;
    v3 n0 = round ? rn[i] : fn[i], n1 = round ? rn[j] : fn[i];
    scene_push(ao.v[i], n0, col);  scene_push(bo.v[i], n0, col);
    scene_push(bo.v[j], n1, col);  scene_push(ao.v[i], n0, col);
    scene_push(bo.v[j], n1, col);  scene_push(ao.v[j], n1, col);
    v3 m0 = v3_scale(n0, -1.0f), m1 = v3_scale(n1, -1.0f);
    scene_push(ai.v[j], m1, col);  scene_push(bi.v[j], m1, col);
    scene_push(bi.v[i], m0, col);  scene_push(ai.v[j], m1, col);
    scene_push(bi.v[i], m0, col);  scene_push(ai.v[i], m0, col);
    scene_quad(ai.v[i], ao.v[i], ao.v[j], ai.v[j], v3_scale(ax, -1.0f), col);
    scene_quad(bi.v[j], bo.v[j], bo.v[i], bi.v[i], ax, col);
  }
}

