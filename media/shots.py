#!/usr/bin/env python3
"""Deterministic trailer takes: six places, one movement-driven arena FPS.

Cameras leave room for the biome skyline. Actions use the game's locomotion,
collision, weapons and effects; the edit supplies only cuts and time stretching.
Every timeline frame is one 120 Hz sim tick. Fire edges live inside capture so
video, impact and the game's own sound remain aligned. Probe before rendering.
"""
import math
from media import Take, lerp, look

SHOTS = {}


def shot(fn):
    SHOTS[fn.__name__] = fn
    return fn


def fire(t):
    """One captured trigger tick, with recoil recovery authored by the take."""
    t.cue("+fire").run(1).cue("-fire")


@shot
def dunes_slide():
    """Opening slide and closing approach share consecutive source frames."""
    t = Take("dunes_slide", 59, fov=76,
             notes="DUNES / tracking run into a sand-sparking slide; loop at frame 60")
    t.open()
    t.setup("biome dunes", "sun 28 145")
    t.puppet(-4, 19, 90, speed=5.8, move=(1, 0), ready=False)
    t.setup("warp -20 0 -20", "wait 30")
    t.mark("approach")
    t.botarc(0.5, 4.0, 4.0, 1.3, 1.3, 0, 0, look_h=1.0, kind="linear")
    t.mark("slide")
    t.cue("puppet slide")
    for frame in range(180):
        a = frame / 179
        radius = lerp(4.0, 4.3, a)
        angle = math.radians(12 * a)
        t.cambot((radius * math.sin(angle), lerp(1.3, 1.0, a),
                  radius * math.cos(angle)),
                 look_h=lerp(1.0, 0.65, min(1.0, frame / 24))).run(1)
    return t


@shot
def forest_jump():
    """A low lateral camera keeps ground and treetops in the jump frame."""
    t = Take("forest_jump", 4, fov=80, notes="FOREST / sprint, airborne silhouette, landing")
    t.open()
    t.setup("biome forest", "sun 34 145")
    t.puppet(-7, 18, 90, speed=5.2, move=(1, 0), ready=False)
    t.setup("warp -20 0 -20", "wait 24")
    t.mark("run")
    for frame in range(240):
        if frame == 54:
            t.mark("jump")
            t.cue("puppet jump")
        x = -6.15 + (frame + 1) * 5.2 / 120
        t.cam((x + 1.8, 1.1, 23.0), (x + 0.1, 1.45, 18.0)).run(1)
    return t


@shot
def marsh_duel():
    """Close first-person exchange across a wet, planted lane."""
    t = Take("marsh_duel", 13, fov=88, notes="MARSH / wet ground, close AR tracking and visible hit")
    t.open()
    t.setup("biome marsh", "sun 32 145")
    t.puppet(7, 6, 135, speed=1.1, move=(-1, 0))
    t.setup("puppet ads 1", "warp 11.5 0 10", "weapon ar", "+crouch", "aimbot", "wait 50")
    t.mark("acquire")
    t.cue("+ads")
    t.hold_aim(0.55, every=12, amp_yaw=0.12, amp_pitch=0.05)
    t.mark("hit")
    t.cue("aimbot")
    fire(t)
    t.sway(0.23, 0.08, 0.04)
    t.cue("aimbot")
    fire(t)
    t.sway(0.4, 0.1, 0.05)
    t.mark("release")
    t.cue("-ads")
    t.pan(0.8, 7, -2, kind="out")
    t.sway(0.1, 0.1, 0.05)
    return t


@shot
def frost_run():
    """White terrain and mountains frame the runner before the optic cut."""
    t = Take("frost_run", 22, fov=76, notes="FROST / wide snowy sprint, trees and mountain skyline")
    t.open()
    t.puppet(-7, 18, 90, speed=4.8, move=(1, 0), ready=False)
    t.setup("warp -20 0 -20", "wait 28")
    t.mark("cross")
    t.botarc(1.6, 6.7, 5.6, 1.6, 1.4, 15, 0, look_h=1.25, kind="linear")
    return t


@shot
def frost_scope():
    """One continuous acquire / shot / bolt cycle in the snowy arena."""
    t = Take("frost_scope", 22, hud=True, res=(1600, 900), fov=90,
             notes="FROST / tracked sniper shot; source 120 is impact, zoom 1.38")
    t.open()
    t.puppet(12, 9, 145, speed=1.25, move=(0, -1))
    t.setup("puppet ads 1", "warp 17 0 15", "weapon sr", "aimbot", "+ads", "wait 100")
    t.mark("track")
    t.hold_aim(1.0, every=10, amp_yaw=0.12, amp_pitch=0.05)
    t.mark("shot")
    t.cue("aimbot")
    fire(t)
    # Recoil settles naturally; the bright hit and bolt are held by the edit.
    t.sway(0.8, 0.12, 0.05)
    t.mark("bolt")
    t.cue("-ads")
    t.sway(0.5, 0.16, 0.08)
    return t


@shot
def quarry_crossfire():
    """A live hard bot pursues a strafing player in the industrial yard."""
    t = Take("quarry_crossfire", 2, fov=84,
             notes="QUARRY / live return fire and lateral dodge beside orange masonry")
    t.open(bots=1, freeze=False, skill="hard")
    t.setup("sun 30 145", "puppet on", "puppet warp 9 9", "puppet off",
            "warp 13 0 15", "aim 9.5 1.3 9")
    t.mark("challenge")
    fire(t)
    t.cue("+left", "+ads")
    t.hold_aim(0.6, every=15, amp_yaw=0.1, amp_pitch=0.05)
    t.mark("return")
    t.cue("aimbot")
    fire(t)
    t.hold_aim(0.23, every=15, amp_yaw=0.1, amp_pitch=0.05)
    t.cue("aimbot")
    fire(t)
    t.sway(0.3, 0.1, 0.05)
    t.cue("-left", "-ads")
    t.pan(0.85, -8, 1, kind="out")
    return t


@shot
def aurora_hunt():
    """The last wide reveals aurora, snow and lit outposts around a runner."""
    t = Take("aurora_hunt", 28, fov=82,
             notes="AURORA / emerald sky, blue snow, red outpost; low running approach")
    t.open()
    t.puppet(12, -15, -135, speed=3.8, move=(-0.7, 0.7), ready=True)
    t.setup("warp 20 0 20", "wait 36")
    t.mark("reveal")
    t.dolly(2.5, (16, 2.2, -19.5), (12.5, 2.6, -18),
            (4, 3.6, 0), (0, 4.0, 2), kind="linear")
    return t


@shot
def frost_impact():
    """The scoped kill from outside: identical sim, a fixed close camera."""
    t = frost_scope()
    t.key, t.fov, t.res = "frost_impact", 66, None
    t.notes = "FROST / same sniper impact outside the optic, sparks and falling body"
    t.setup("hud off")
    for frame, commands in enumerate(t.timeline):
        settle = max(0.0, min(1.0, (frame - 126) / 70))
        eye = (15.3, 1.05, 10.2)
        at = (12.0 - 0.4 * settle, 1.22 - 0.6 * settle, 6.75 - 0.5 * settle)
        yaw, pitch = look(eye, at)
        commands.append("cam %.4f %.4f %.4f %.5f %.5f"
                        % (*eye, yaw, pitch))
    return t
