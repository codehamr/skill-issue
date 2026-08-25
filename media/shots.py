#!/usr/bin/env python3
"""The scene library for media/scene.json — one function per shot, registered
in SHOTS. All six shots live on the GOLD stage:

    SEED 59 (GOLD)  sand at the LOWEST sun the draw allows (elev 6.50,
    azi 29.6) — amber sky, 20 m shadows, ripple glitter. Scouted 2026-08-25
    over 60 seeds for the hero banner. Geography (from .cache/stages/s59.json,
    auto-dumped): the monolith x -3.16..3.16, z -2.56..2.56, h 2.57; the NORTH
    lane at z -4.5 (its z -2.56 face TAKES the low sun — the backdrop); the
    SOUTH perimeter wall z 24.0 h 2.75, whose north face glows (the
    wall-slide stage); low walls/crates at ~1.0-1.3 m elsewhere.

Staging facts that each cost a measured round (details at the shots):
  - Spray cannot connect by script: rec_p rides ON TOP of the aim
    (~1.2 deg/round). Aimed single taps with recovery between them hit.
  - A `normal` live bot WANDERS; `hard` answers an aggro shot in ~35 ticks
    and kills a STANDING man in 0.5 s — movement is the survival mechanic.
  - An eye advancing on a fixed-angle ground target overshoots it more every
    frame: strafe while firing, never push.
  - The authoring loop: ./media/media.py probe <KEY> N, then read
    .cache/clips/<KEY>/probe.log — the EVENT LOG is what twice exposed a
    "fight" with zero incoming fire.
"""
import math
from media import Take, lerp, vlerp, ease, TICK

GOLD = 59
SHOTS = {}


def shot(fn):
    SHOTS[fn.__name__] = fn
    return fn


# Measured motion table (traced with `trace`, not extrapolated): +forward held
# from a standing warp; x offset from start, +crouch at tick 42.
RUN_SLIDE = [(0, 1.19), (4, 1.42), (14, 2.00), (24, 2.59), (34, 3.17),
             (42, 3.63), (44, 3.75), (54, 5.25), (64, 6.59), (74, 7.71),
             (84, 8.63), (94, 9.40), (104, 10.05), (114, 10.58), (124, 11.02),
             (134, 11.39), (144, 11.70), (154, 11.96), (164, 12.21),
             (184, 12.71), (204, 13.21), (260, 14.6)]


def table(tab, f, k=1):
    for a, b in zip(tab, tab[1:]):
        if f <= b[0]:
            return lerp(a[k], b[k], (f - a[0]) / (b[0] - a[0]))
    return tab[-1][k]


@shot
def gold_run():
    """The establish: low camera north of the lane looking south, so the
    backdrop is the monolith's own SUNSTRUCK north face. The runner crosses
    left-to-right, front-lit, his shadow streaming 15 m toward the wall."""
    t = Take("gold_run", GOLD, fov=62, notes="golden-hour establish, runner crossing")
    t.open()
    t.puppet(7.0, -4.5, 90, speed=3.1, move=(-1, 0))
    t.setup("warp -13 0 -4.5", "aim 2 1.45 -4.5", "wait 20")
    t.mark("run")
    t.dolly(2.4, (-0.5, 0.85, -7.6), (-1.9, 1.00, -7.2),
            (3.0, 1.40, -4.4), (-0.8, 1.35, -4.5), kind="linear")
    t.mark("pass")
    t.dolly(1.2, (-1.9, 1.00, -7.2), (-2.1, 1.08, -7.0),
            (-0.8, 1.35, -4.5), (-3.8, 1.32, -4.5), kind="out")
    return t


@shot
def gold_fight():
    """The CALM ADS kill. The duel version read as jitter at GIF scale (the
    strafe-dodge, the snap to the old position, the tracking snaps), so this
    is the quiet version: the dot WAITS on the crossing bot's path, breathing
    only, and two aimed taps drop him at 13 m. The puppet crosses slowly so
    the one aimbot correction per tap is under a degree — no visible snap."""
    t = Take("gold_fight", GOLD, fov=85, notes="calm ADS kill on a crossing bot")
    t.open()
    t.puppet(-4.6, -11.5, 90, speed=1.8, move=(1, 0))
    t.setup("warp -7 0 -1.0", "aim -2.6 1.42 -11.7", "wait 12")
    t.mark("raise")
    t.cue("+ads")
    t.sway(0.50, 0.18, 0.09)
    t.mark("hold")
    t.sway(0.45, 0.14, 0.07)
    t.mark("tap")
    t.cue("aimbot", "tap fire")
    t.sway(0.30, 0.12, 0.06)
    t.cue("aimbot", "tap fire")
    t.sway(0.35, 0.12, 0.06)
    t.mark("drop")
    t.sway(0.65, 0.15, 0.08)
    t.cue("-ads")
    t.sway(0.35, 0.30, 0.15)
    return t


@shot
def gold_wallslide():
    """The GOLD wall-slide: same construction as wall_slide, moved to seed
    59's SOUTH perimeter wall (h 2.75) whose north face takes the low sun
    full-on — the slide runs along a GLOWING wall and the enemy rounds spark
    off lit stone. Lane z = 23.5, 0.5 m off the wall; live bots, and the
    first-order aim follower cannot hold a 20 m/s slider, so the incoming
    lands behind him by construction. He does not fire (a slide is a
    one-handed muzzle-up carry). Realtime run, slow2x from the slide."""
    t = Take("gold_wallslide", GOLD, fov=85, notes="lit-wall slide under fire")
    # ONE bot, HARD, hand-placed: with 6+ live bots on this seed everybody
    # locked onto each other mid-field and the runner at the south edge drew
    # ZERO fire through the whole take (probe.log read one bang — the
    # player's own; the sparks in the first probe were slide friction, not
    # incoming). A single bot has no other target. Placed at (10, 14) via a
    # puppet warp released back to the AI — off the north edge of the frame,
    # so only his tracers enter it — and on HARD he answers the aggro shot
    # inside ~35 ticks, i.e. exactly as the take opens. The run is what keeps
    # the runner alive: measured, hard kills a STANDING man here in 0.5 s.
    t.open(bots=1, freeze=False, skill="hard")
    t.setup("puppet on", "puppet warp -14 20.5", "puppet off",
            "warp -1 0 23.5", "aim 20 1.5 23.5", "tap fire",
            "+forward", "wait 44")

    def px(f):
        return table(RUN_SLIDE, f)

    def rig_(f, back=3.0, lead=1.3, ch=1.20, lh=1.15):
        return ((px(f) - 0.4, ch, 23.5 - back), (px(f) + lead, lh, 23.5))

    # Camera a metre and a half further back than the first cut, so the WALL
    # BEHIND the slider owns the frame: the shooter is released WEST, nearly
    # ON the lane axis (~10 deg to the wall), so his aim-lag misses smear
    # along the wall trailing the slide instead of vanishing past it.
    t.mark("run")
    t.track(0.35, lambda a: rig_(a * 42, 4.6, 1.6, 1.30, 1.18), kind="linear")
    t.mark("slide")
    t.cue("+crouch")
    t.track(0.55, lambda a: rig_(42 + a * 66, lerp(4.6, 4.2, a), 1.7,
                                 lerp(1.30, 0.98, a), lerp(1.18, 0.82, a)),
            kind="linear")
    t.mark("low")
    t.track(0.40, lambda a: rig_(108 + a * 48, lerp(4.2, 4.4, a), 1.5,
                                 lerp(0.98, 1.02, a), 0.82), kind="linear")
    t.cue("-crouch")
    t.mark("out")
    t.cue("-forward")
    t.track(0.35, lambda a: rig_(156 + a * 42, lerp(2.9, 3.4, a), 0.6,
                                 lerp(0.96, 1.30, a), lerp(0.80, 1.12, a)),
            kind="out")
    return t


@shot
def gold_lean():
    """The corner peek as a real exchange. The player stands at the
    monolith's SW corner where 0.28 m of lean flips the line exactly: from
    (-13, -9) the bot's ray to the STANDING eye crosses the monolith's own
    footprint (blocked), and to the LEANED-LEFT eye it clears the south
    face's west corner (checked against stages/s59.json). So while he holds
    cover the live bot's rounds chip the corner beside the lens, and the
    lean-out is the whole reveal: ADS, two beats of exchange, the bot drops
    at 16 m."""
    t = Take("gold_lean", GOLD, fov=85, notes="live corner peek: cover fire, lean, kill")
    t.open(bots=1, freeze=False, skill="hard")
    # Aggro shot misses 2.5 m right of him; a LONG cover hold so his answer
    # (and his approach) happens on camera — his rounds land on the corner
    # beside the lens while the standing eye is geometrically covered. The
    # burst opens on the held aim (he has moved: sparks, not hits), one
    # aimbot snap ends it.
    t.setup("puppet on", "puppet warp -13 -9", "puppet off",
            "warp -2.4 0 3.4", "aim -10.5 1.20 -8.0", "tap fire", "wait 12")
    t.mark("cover")
    t.sway(0.95, 0.35, 0.18)
    t.mark("lean")
    t.cue("+lean_left", "+ads")
    t.sway(0.50, 0.28, 0.14)
    t.mark("burst")
    # Suppression pair, then aimed taps — see gold_fight's note on why spray
    # cannot connect (rec_p rides on top of any re-snapped aim).
    t.cue("+fire")
    t.sway(0.20, 0.22, 0.10)
    t.cue("-fire")
    for _ in range(3):
        t.cue("aimbot", "tap fire")
        t.sway(0.14, 0.15, 0.08)
    t.mark("down")
    t.sway(0.50, 0.25, 0.12)
    t.cue("-lean_left", "-ads")
    t.sway(0.35, 0.4, 0.2)
    return t


@shot
def gold_scope():
    """THE shot the banner is about: through the glass. FP bolt gun, the
    disc opens, the runner is tracked IN the scope, one round drops him at
    ~15 m, the bolt cycles in the hold. HUD stays on (the disc is drawn in
    the HUD pass) at 2560x1440; the edit crops >=1.4x, which also pushes
    the killfeed line off the frame. Play the bang slow (3-4x) in the edit."""
    t = Take("gold_scope", GOLD, hud=True, res=(2560, 1440),
             notes="scoped tracking headshot; crop >=1.4x; bang slow3x")
    t.open()
    t.puppet(7.0, -4.5, 90, speed=3.1, move=(-1, 0))
    t.setup("weapon sr", "warp -13 0 -4.5", "aim 2.0 1.45 -4.5", "wait 20")
    t.mark("raise")
    t.cue("+ads")
    t.sway(0.55, 0.30, 0.15)
    t.mark("track")
    t.hold_aim(1.90, every=9, amp_yaw=0.35, amp_pitch=0.15)
    t.mark("bang")
    t.cue("aimbot", "tap fire")
    t.sway(0.55, 0.22, 0.10)
    t.mark("bolt")
    t.sway(1.15, 0.30, 0.16)
    t.mark("settle")
    t.cue("-ads")
    t.sway(0.55, 0.40, 0.20)
    return t


@shot
def gold_impact():
    """The same kill from the lane: camera SOUTH of the runner, so he crosses
    BACKLIT against the low sun's haze — silhouette, rim, 20 m of shadow
    running at the lens — and the round arrives from off-frame. The ragdoll
    is thrown along the shot line (EAST, back over his own heels — the round
    comes from the west end of the lane) and the tail tilts down onto the
    body in the glitter. Play the bang slow4x."""
    t = Take("gold_impact", GOLD, fov=58, notes="backlit lane kill, bang slow4x")
    t.open()
    t.puppet(7.0, -4.5, 90, speed=3.1, move=(-1, 0))
    t.setup("weapon sr", "warp -13 0 -4.5", "aim 2.0 1.45 -4.5", "wait 20")
    t.mark("run")
    t.dolly(2.45, (3.4, 1.10, -2.95), (2.7, 1.06, -3.00),
            (5.8, 1.35, -4.5), (-0.3, 1.32, -4.5), kind="linear")
    t.mark("bang")
    t.cue("aimbot", "tap fire")
    t.dolly(0.95, (2.7, 1.06, -3.00), (2.3, 1.10, -3.02),
            (-0.3, 1.32, -4.5), (1.8, 1.10, -4.5), kind="linear")
    t.mark("down")
    t.dolly(1.35, (2.3, 1.10, -3.02), (2.0, 1.18, -3.00),
            (1.8, 1.10, -4.5), (4.5, 0.50, -4.6), kind="out")
    return t
