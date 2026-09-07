#!/usr/bin/env python3
"""Real-game action takes with authored mouse input and readable cause/effect.

Every frame is one 120 Hz tick. No target-lock controller runs in these takes:
acquisition, overshoot and recoil correction are finite mouse gestures. Actors
and effects use the actual locomotion, collision, weapon and damage rules.
"""
import math
from media import Take, lerp, look

SHOTS = {}


def shot(fn):
    SHOTS[fn.__name__] = fn
    return fn


def fire(t):
    """One captured trigger tick; the take owns the following mouse gesture."""
    t.cue("+fire").run(1).cue("-fire")


@shot
def dunes_slide():
    """A low camera sweeps across the slide; consecutive frames close the loop."""
    t = Take("dunes_slide", 59, fov=76,
             notes="DUNES / low sand slide with a passing camera; loop at frame 60")
    t.open()
    t.setup("environment dunes golden haze", "sun 18 80")
    t.puppet(-4, 19, 90, speed=5.8, move=(1, 0), ready=False)
    t.setup("warp -20 0 -20", "wait 30")
    t.mark("approach")
    t.botarc(0.5, 4.0, 3.6, 1.3, 0.9, -12, 0, look_h=0.9, kind="linear")
    t.mark("slide")
    t.cue("puppet slide")
    for frame in range(156):
        a = frame / 155
        radius = lerp(3.6, 4.1, a)
        angle = math.radians(36 * a)
        t.cambot((radius * math.sin(angle), lerp(0.9, 0.65, a),
                  radius * math.cos(angle)),
                 look_h=lerp(0.9, 0.58, min(1.0, frame / 24))).run(1)
    return t


@shot
def forest_break():
    """A diagonal jump crosses a planted lens and incoming rounds."""
    t = Take("forest_break", 4, fov=82,
             notes="FOREST / diagonal leap across incoming fire, close landing")
    t.open()
    t.setup("environment forest day clear", "sun 34 145")
    t.puppet(-4, 17, 135, speed=5.7, move=(0.8, 0.6))
    t.setup("warp -8 0 22", "weapon ar", "aim -0.5 0.4 20", "wait 20")
    t.mark("break")
    for frame in range(168):
        if frame == 18:
            t.cue("puppet jump")
            t.mark("jump")
        if frame in (28, 40, 52):
            t.cue("+fire")
        if frame in (29, 41, 53):
            t.cue("-fire")
        # The actor crosses and grows in frame; the camera does not chase him.
        a = frame / 167
        t.cam((1.8 + 0.5 * a, 0.85, 23.1),
              (-2.0 + 2.3 * a, 1.4 - 0.25 * a, 18.6 + 1.6 * a)).run(1)
    return t


@shot
def marsh_duel():
    """A hurried body-level burst misses, corrects and follows a reversal."""
    t = Take("marsh_duel", 13, fov=88,
             notes="MARSH / late acquisition, short bursts, strafe reversal, recoil recovery")
    t.open()
    t.setup("environment marsh day clear", "sun 32 145")
    t.puppet(7, 6, 135, speed=1.6, move=(-1, 0))
    t.setup("puppet ads 1", "warp 11.5 0 10", "weapon ar", "wait 36",
            "aim 6.9 1.05 6")
    t.mark("acquire")
    t.pan(0.17, 3.0, 0.8, kind="out")
    t.cue("+ads")
    fire(t)
    t.pan(0.18, -7.8, -0.5, kind="out")
    t.mark("correct")
    t.cue("+fire")
    t.pan(0.16, -1.55, -0.8, kind="linear")
    t.cue("-fire", "puppet move 1 0", "+right")
    t.pan(0.19, -5.5, 1.1, kind="out")
    t.mark("reverse")
    t.cue("+fire")
    t.pan(0.3, -13.0, 0.2, kind="linear")
    t.cue("-fire", "-right", "-ads")
    t.pan(0.24, 4.0, 0.5, kind="out")
    t.sway(0.25, 0.3, 0.13)
    return t


@shot
def frost_scope():
    """A human scope acquisition: late drag, small overrun, correction, shot."""
    t = Take("frost_scope", 22, hud=True, res=(2880, 1620), fov=90,
             notes="FROST / scope arrives off target, corrects to torso, shot at frame 120")
    t.open()
    t.setup("environment frost day clear")
    t.puppet(12, 9, 145, speed=1.1, move=(0, -1))
    t.setup("puppet ads 1", "warp 17 0 15", "weapon sr", "wait 40",
            "aim 13.0 1.15 8.5")
    t.mark("raise")
    t.cue("+ads")
    t.pan(0.35, -8.0, 0.4, kind="out")
    t.mark("overrun")
    t.pan(0.22, 1.8, 0.18, kind="inout")
    t.pan(0.25, 1.1, 0.12, kind="linear")
    t.pan(0.18, 2.9, -0.08, kind="out")
    t.mark("shot")
    fire(t)
    t.pan(0.18, 0.4, -0.7, kind="out")
    t.sway(0.55, 0.17, 0.08)
    t.mark("bolt")
    t.cue("-ads")
    t.pan(0.35, 2.0, -0.6, kind="out")
    return t


@shot
def quarry_crossfire():
    """Actual bot fire drives a first-person dodge and a loose counter-burst."""
    t = Take("quarry_crossfire", 2, fov=88,
             notes="QUARRY / live incoming fire, A-D dodge, body burst and lean")
    t.open(bots=1, freeze=False, skill="hard")
    t.setup("environment quarry day clear", "sun 30 145", "puppet on", "puppet warp 9 9", "puppet off",
            "warp 13 0 15", "aim 9.5 1.0 9")
    t.mark("challenge")
    fire(t)
    t.cue("+left", "+ads")
    t.pan(0.24, -3.5, 0.7, kind="out")
    t.mark("return")
    t.cue("+fire")
    t.pan(0.28, 2.5, -1.4, kind="linear")
    t.cue("-fire", "-left", "+right")
    t.pan(0.18, -2.0, 0.6, kind="out")
    t.cue("+fire")
    t.pan(0.32, -3.5, 0.4, kind="inout")
    t.cue("-fire", "-right", "+lean_right")
    t.pan(0.25, -8.0, 1.1, kind="out")
    t.cue("+fire")
    t.pan(0.28, -9.0, 0.2, kind="linear")
    t.cue("-fire", "-lean_right", "-ads", "+back", "+reload")
    t.run(1).cue("-reload")
    t.pan(0.5, 7.0, -3.0, kind="inout")
    t.cue("-back")
    t.sway(1.8, 0.3, 0.12)
    return t


@shot
def aurora_push():
    """A low first-person rush brings the night arena into the action."""
    t = Take("aurora_push", 28, fov=94,
             notes="AURORA / sprint into slide, hipfire with the outpost and sky in view")
    t.open()
    t.setup("environment frost night clear")
    t.puppet(10, -5, 155, speed=1.6, move=(-1, 0))
    t.setup("warp 15 0 -17", "weapon ar", "aim 8 1.2 -6", "+forward", "wait 36")
    t.mark("rush")
    t.pan(0.25, 7.0, 1.0, kind="out")
    t.cue("+crouch")
    t.mark("slide")
    t.pan(0.38, -9.0, -1.3, kind="inout")
    t.cue("-forward", "-crouch")
    t.pan(0.12, -14.0, -1.7, kind="out")
    t.cue("+fire")
    t.pan(0.36, 3.5, -2.3, kind="linear")
    t.cue("-fire")
    t.pan(0.3, 5.5, 2.0, kind="out")
    return t


@shot
def frost_impact():
    """The exact scoped shot seen outside, without a second simulated impact."""
    t = frost_scope()
    t.key, t.fov, t.res = "frost_impact", 66, None
    t.notes = "FROST / the same shot, falling figure and impact plume at close range"
    t.setup("hud off")
    for frame, commands in enumerate(t.timeline):
        settle = max(0.0, min(1.0, (frame - 126) / 65))
        eye = (15.3, 1.05, 10.2)
        at = (12.0 - 0.4 * settle, 1.22 - 0.6 * settle, 7.45 - 0.5 * settle)
        yaw, pitch = look(eye, at)
        commands.append("cam %.4f %.4f %.4f %.5f %.5f" % (*eye, yaw, pitch))
    return t
