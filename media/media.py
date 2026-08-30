#!/usr/bin/env python3
"""media.py — THE media kit, one file: engine AND button. Regenerates every
asset in media/ from the current build, reproducibly (same binary + same
seeds + same scripts = the same media, byte for byte; the PNGs modulo their
lossless recompression pass). Sound in hero.mp4 is the GAME'S OWN audio from
the clip captures — there is no music synthesis.

    ./media/media.py            # everything: stills + clip re-render + gif + mp4
    ./media/media.py stills     # fight.png + scope.png only            (~2 min)
    ./media/media.py gif        # re-render the scene clips + hero.gif  (~10 min)
    ./media/media.py mp4        # re-render + hero.mp4 (game sound);
                                #   `mp4 --skip-render` right after `gif`
    ./media/media.py check      # tooling + staleness report, renders nothing
    ./media/media.py list       # every shot, frames, seconds, note
    ./media/media.py render K.. # (re-)render named clips at full quality
    ./media/media.py probe K N  # N framing stills for shot K, no video —
                                #   THE authoring loop; then read
                                #   .cache/clips/K/probe.log (the event log)
    --skip-render               # gif/mp4: assemble from cached clips (ONLY for
                                #   iterating on the EDIT — after a look change
                                #   the banner would mix two builds)

The folder is three files plus the four assets, and the split is by WHO edits:
  shots.py     the SCENES (gold_* Takes, GOLD stage = seed 59) — prompt here
  scene.json   the SCREENPLAY (cuts in beats @126 BPM, speeds, zooms, palette)
  media.py     this file — the Take builder, renderer, beat assembler and the
               still recipes; touched only when the MACHINERY changes
The clip list for hero.gif/hero.mp4 is read FROM scene.json, so a shot added
to the screenplay is automatically in the re-render set. All caches live in
media/.cache/ (gitignored, safe to delete).
"""
import sys
import tempfile

sys.dont_write_bytecode = True   # keep media/ clean: no __pycache__

import json, math, os, re, shutil, subprocess, random

ROOT = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.join(os.path.dirname(ROOT), "build", "game")
# Everything regenerable hides in .cache/ — the media folder itself stays the
# four assets plus the kit (media.py, scene.json, shots.py).
CACHE = os.path.join(ROOT, ".cache")
CLIPS = os.path.join(CACHE, "clips")
STAGES = os.path.join(CACHE, "stages")
TICK = 120.0

# ---------------------------------------------------------------------------
# camera math — the harness `cam` command takes RADIANS. yaw = atan2(dx, -dz),
# pitch positive = up. -z is north, +x east, floor y = 0, standing eye 1.62.
# SUN_DIR (0.72, 0.62, 0.34): east-south-east, 38 deg up. Shoot roughly WEST
# and the subject is lit; shoot into the sun and it is a silhouette.
# ---------------------------------------------------------------------------
SUN_AZ = 115.0        # degrees; a camera near this azimuth has the sun behind it


def look(eye, at):
    dx, dy, dz = at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]
    return math.atan2(dx, -dz), math.atan2(dy, math.hypot(dx, dz))


def lerp(a, b, t):
    return a + (b - a) * t


def vlerp(a, b, t):
    return tuple(lerp(x, y, t) for x, y in zip(a, b))


def ease(t, kind="inout"):
    t = max(0.0, min(1.0, t))
    if kind == "linear":
        return t
    if kind == "in":
        return t * t
    if kind == "out":
        return 1.0 - (1.0 - t) ** 2
    if kind == "inout":
        return t * t * (3.0 - 2.0 * t)
    if kind == "inout5":
        return t * t * t * (t * (t * 6 - 15) + 10)
    raise ValueError(kind)


def orbit_eye(center, radius, height, deg):
    a = math.radians(deg)
    return (center[0] + radius * math.sin(a), center[1] + height,
            center[2] + radius * math.cos(a))


# ---------------------------------------------------------------------------
# the stage, as data — dumped from the game itself (`--do map`) and cached,
# so a shot's collision/LOS checks can never drift from the sim's own set.
# ---------------------------------------------------------------------------
class Stage:
    def __init__(self, seed):
        self.seed = seed
        os.makedirs(STAGES, exist_ok=True)
        p = os.path.join(STAGES, "s%d.json" % seed)
        if not os.path.exists(p):
            r = subprocess.run([GAME, "--seed", str(seed), "--config",
                                os.path.join(STAGES, ".m.cfg"), "--do", "map"],
                               capture_output=True, text=True, check=True).stdout
            theme = re.search(r"theme=(\w+)", r).group(1)
            solids = [tuple(float(x) for x in m.groups())
                      for m in re.finditer(r"solid \d+ min=\((\S+) (\S+) (\S+)\)"
                                           r" max=\((\S+) (\S+) (\S+)\)", r)]
            json.dump({"theme": theme, "solids": solids}, open(p, "w"))
        d = json.load(open(p))
        self.theme, self.solids = d["theme"], [tuple(s) for s in d["solids"]]

    def blocker(self, a, b):
        """Index of the first solid the segment a->b enters, or None."""
        d = tuple(y - x for x, y in zip(a, b))
        for i, s in enumerate(self.solids):
            lo, hi = s[0:3], s[3:6]
            t0, t1 = 0.0, 1.0
            for k in range(3):
                if abs(d[k]) < 1e-9:
                    if a[k] < lo[k] or a[k] > hi[k]:
                        t0 = 2.0
                        break
                    continue
                u, v = (lo[k] - a[k]) / d[k], (hi[k] - a[k]) / d[k]
                t0, t1 = max(t0, min(u, v)), min(t1, max(u, v))
            if t0 <= t1:
                return i
        return None

    def inside(self, p, pad=0.0):
        """The solid p sits in — a camera inside geometry films its inside."""
        for i, s in enumerate(self.solids):
            if (s[0] - pad <= p[0] <= s[3] + pad and s[1] - pad <= p[1] <= s[4] + pad
                    and s[2] - pad <= p[2] <= s[5] + pad):
                return i
        return None


_stages = {}


def stage_of(seed):
    if seed not in _stages:
        _stages[seed] = Stage(seed)
    return _stages[seed]


# ---------------------------------------------------------------------------
# the take builder
# ---------------------------------------------------------------------------
class Take:
    def __init__(self, key, seed, hud=False, notes="", res=None, fov=None):
        # `fov` narrows the LENS for one shot (the game plays at 100, which is
        # right for playing and wrong for a camera). It is a config key, so the
        # renderer writes a scratch config. First-person shots keep 100 unless
        # the subject needs the reach — that IS the game.
        # `res` overrides capture size for ONE shot (the scope picture is HUD-
        # pass, so that shot captures wide and the edit crops the readouts off).
        self.key, self.seed, self.notes = key, seed, notes
        self.res, self.fov = res, fov
        self.pre = ["showfps off", "hud on" if hud else "hud off"]
        self.timeline = []           # list[list[str]], one entry per frame
        self._pending = []
        self.marks = []              # (frame, label)
        self.rng = random.Random(0xBEA7 ^ seed)

    # -- primitives ----------------------------------------------------------
    def setup(self, *cmds):
        for c in cmds:
            self.pre += [x.strip() for x in c.split(";") if x.strip()]
        return self

    def cue(self, *cmds):
        for c in cmds:
            self._pending += [x.strip() for x in c.split(";") if x.strip()]
        return self

    def run(self, n=1):
        for i in range(int(n)):
            self.timeline.append(self._pending if i == 0 else [])
            self._pending = []
        return self

    def mark(self, label):
        self.marks.append((len(self.timeline), label))
        return self

    @property
    def f(self):
        return len(self.timeline)

    def sec(self, s):
        return int(round(s * TICK))

    # -- staging sugar -------------------------------------------------------
    def open(self, bots=1, freeze=True, skill=None):
        """Common opening: controllable roster, match that cannot end."""
        self.setup("bots %d" % bots)
        if skill:
            self.setup("skill " + skill)
        self.setup("fraglimit 1000", "wait 3")
        if freeze:
            self.setup("botfreeze on")
        return self

    def puppet(self, x, z, face, speed=None, move=None, ready=True):
        """A staged enemy — and it MOVES. A target standing still while it is
        shot reads as a range dummy and makes the aim look free; `aimbot`
        re-snaps onto the head on the frame it fires, so a mover costs nothing.
        Prefer STRAFING (facing the player, travelling across the line):
        lateral motion is what reads as motion, closing motion just changes
        the subject's size."""
        self.setup("puppet on", "puppet warp %.2f %.2f" % (x, z),
                   "puppet face %.1f" % face)
        if ready:
            self.setup("puppet ready 1")
        if speed is not None:
            self.setup("puppet speed %.2f" % speed)
        if move is not None:
            self.setup("puppet move %.2f %.2f" % move)
        return self

    # -- free camera ----------------------------------------------------------
    def cam(self, eye, at=None, yaw=None, pitch=None):
        if at is not None:
            yaw, pitch = look(eye, at)
        return self.cue("cam %.4f %.4f %.4f %.5f %.5f"
                        % (eye[0], eye[1], eye[2], yaw, pitch))

    def hold(self, s, eye=None, at=None):
        if eye is not None:
            self.cam(eye, at)
        return self.run(self.sec(s))

    def dolly(self, s, eye0, eye1, at0, at1, kind="inout", shake=0.0):
        n = self.sec(s)
        for i in range(n):
            t = ease(i / max(1, n - 1), kind)
            e, a = vlerp(eye0, eye1, t), vlerp(at0, at1, t)
            if shake:
                e = self._shake(e, shake, i)
                a = self._shake(a, shake * 0.35, i + 977)
            self.cam(e, a).run(1)
        return self

    def arc(self, s, center, r0, r1, h0, h1, d0, d1, look_h=1.0,
            look_off=(0, 0), kind="inout", shake=0.0):
        n = self.sec(s)
        for i in range(n):
            t = ease(i / max(1, n - 1), kind)
            e = orbit_eye(center, lerp(r0, r1, t), lerp(h0, h1, t), lerp(d0, d1, t))
            a = (center[0] + look_off[0], center[1] + look_h, center[2] + look_off[1])
            if shake:
                e = self._shake(e, shake, i)
            self.cam(e, a).run(1)
        return self

    def track(self, s, path, kind="linear", shake=0.0):
        """path(t) -> (eye, at), t in 0..1 after easing. The workhorse."""
        n = self.sec(s)
        for i in range(n):
            t = ease(i / max(1, n - 1), kind)
            e, a = path(t)
            if shake:
                e = self._shake(e, shake, i)
            self.cam(e, a).run(1)
        return self

    def _shake(self, p, amp, i):
        r = self.rng
        w = math.sin(i * 0.11) * 0.6 + math.sin(i * 0.031 + 1.7) * 0.4
        return (p[0] + amp * (w + r.uniform(-0.3, 0.3)),
                p[1] + amp * 0.7 * (math.sin(i * 0.083 + 2.2) + r.uniform(-0.3, 0.3)),
                p[2] + amp * (math.cos(i * 0.097 + 0.4) + r.uniform(-0.3, 0.3)))

    # -- bot-relative camera (the only rig that tracks a LIVE actor) ----------
    def cambot(self, off, look_h=1.05):
        return self.cue("camaim %.2f" % look_h,
                        "cambot %.3f %.3f %.3f" % off)

    def botarc(self, s, r0, r1, h0, h1, d0, d1, look_h=1.05, kind="inout"):
        n = self.sec(s)
        for i in range(n):
            t = ease(i / max(1, n - 1), kind)
            a = math.radians(lerp(d0, d1, t))
            r = lerp(r0, r1, t)
            self.cambot((r * math.sin(a), lerp(h0, h1, t), r * math.cos(a)),
                        look_h).run(1)
        return self

    # -- viewmodel macro lens --------------------------------------------------
    # `vmorbit` photographs the FIRST-PERSON model — the one with the 0.7 mm
    # chamfers and the brushed phosphate — but it is a weapon and two gloves
    # with NO BODY, so the lens must stay close enough that no frame shows
    # where an arm should be. Small yaw swings + a distance/pitch ramp read as
    # an overfly; a big yaw sweep reads as the weapon spinning on a turntable.
    # Angles here honour the sun: azimuth ~115 lights the top of the receiver.
    def vm(self, yaw, pit, dist, which=1):
        return self.cue("vmorbit %.3f %.3f %.4f %d" % (yaw, pit, dist, which))

    def vmtravel(self, s, a, b, which=1, kind="inout"):
        """a/b = (yaw_deg, pitch_deg, dist_m)."""
        n = self.sec(s)
        for i in range(n):
            t = ease(i / max(1, n - 1), kind)
            self.vm(lerp(a[0], b[0], t), lerp(a[1], b[1], t),
                    lerp(a[2], b[2], t), which).run(1)
        return self

    # -- first person ----------------------------------------------------------
    # Methods take DEGREES; the engine's `look` takes mouse counts at 0.022
    # deg/count. +yaw right, +pitch up.
    COUNT_PER_DEG = 1.0 / 0.022

    def look_deg(self, dyaw, dpitch):
        return self.cue("look %.2f %.2f" % (dyaw * self.COUNT_PER_DEG,
                                            -dpitch * self.COUNT_PER_DEG))

    def pan(self, s, yaw_deg, pitch_deg, kind="inout"):
        n = self.sec(s)
        prev = 0.0, 0.0
        for i in range(n):
            t = ease((i + 1) / n, kind)
            cx, cy = yaw_deg * t, pitch_deg * t
            self.look_deg(cx - prev[0], cy - prev[1]).run(1)
            prev = cx, cy
        return self

    def sway(self, s, amp_yaw=1.1, amp_pitch=0.5, period=150):
        """Idle breathing. No sway reads as a freeze-frame; over ~1 degree
        reads as a drunk."""
        n = self.sec(s)
        for i in range(n):
            sx = amp_yaw * (math.sin((i + 1) * 2 * math.pi / period) -
                            math.sin(i * 2 * math.pi / period))
            sy = amp_pitch * (math.sin((i + 1) * 2 * math.pi / (period * 1.7)) -
                              math.sin(i * 2 * math.pi / (period * 1.7)))
            self.look_deg(sx, sy).run(1)
        return self

    def hold_aim(self, s, every=12, amp_yaw=0.7, amp_pitch=0.3, period=110):
        """TRACK a moving target: re-snap onto its head every `every` frames,
        breathe between snaps. One `aimbot` at burst start is a lead that goes
        stale the moment the target walks; a snap every frame is an aimbot demo."""
        n = self.sec(s)
        for i in range(n):
            if i % every == 0:
                self.cue("aimbot")
            sx = amp_yaw * (math.sin((i + 1) * 2 * math.pi / period) -
                            math.sin(i * 2 * math.pi / period))
            sy = amp_pitch * (math.sin((i + 1) * 2 * math.pi / (period * 1.7)) -
                              math.sin(i * 2 * math.pi / (period * 1.7)))
            self.look_deg(sx, sy).run(1)
        return self

    # -- script emission --------------------------------------------------------
    def script(self, seg_dir="seg"):
        out = list(self.pre)
        segs, i, n = [], 0, len(self.timeline)
        while i < n:
            cmds, k = self.timeline[i], 1
            while i + k < n and not self.timeline[i + k]:
                k += 1
            segs.append((cmds, k))
            i += k
        for j, (cmds, k) in enumerate(segs):
            out += cmds
            out.append("capture %s/s%04d.rgb %s/s%04d.wav 120 %d"
                       % (seg_dir, j, seg_dir, j, k))
        return "\n".join(out) + "\n"

    def script_probe(self, want):
        """Same sim, `wait` instead of `capture`, a `shot` at each wanted
        frame — a framing question answered in seconds instead of minutes."""
        out = list(self.pre)
        want = sorted(set(min(max(0, int(f)), max(0, len(self.timeline) - 1))
                          for f in want))
        wi = 0
        for i, cmds in enumerate(self.timeline):
            out += cmds
            if wi < len(want) and want[wi] == i:
                out.append("shot probe/p%04d.png" % i)
                wi += 1
            out.append("wait 1")
        return "\n".join(out) + "\n"


# ---------------------------------------------------------------------------
# render / probe
# ---------------------------------------------------------------------------
def _cfg(d, fov):
    """Fresh scratch config per run — the game writes its config back on exit,
    so a stale one is a silent state leak between takes."""
    p = os.path.join(d, "t.cfg")
    if os.path.exists(p):
        os.remove(p)
    if fov:
        open(p, "w").write("fov %d\n" % int(fov))


def sh(cmd, **kw):
    return subprocess.run(cmd, shell=isinstance(cmd, str), check=True, **kw)


def render(take, w=1920, h=1080, quiet=False):
    """Run the take, encode clips/<key>/<key>.mp4 (120 fps) + .wav + sheet."""
    d = os.path.join(CLIPS, take.key)
    seg = os.path.join(d, "seg")
    shutil.rmtree(seg, ignore_errors=True)
    os.makedirs(seg, exist_ok=True)
    open(os.path.join(d, "script.txt"), "w").write(take.script())
    open(os.path.join(d, "frames.txt"), "w").write(
        "".join("%5d  %s\n" % (f, l) for f, l in take.marks))
    _cfg(d, take.fov)
    if not quiet:
        print("  render %-13s %dx%d  %d frames" % (take.key, w, h, len(take.timeline)))
    with open(os.path.join(d, "events.log"), "w") as log:
        subprocess.run([GAME, "--seed", str(take.seed), "--w", str(w), "--h", str(h),
                        "--config", "t.cfg", "--script", "script.txt"],
                       cwd=d, env=dict(os.environ, LP_NUM_THREADS="8"),
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    mp4 = os.path.join(d, take.key + ".mp4")
    wav = os.path.join(d, take.key + ".wav")
    allrgb = os.path.join(d, "all.rgb")
    sh("cat %s/s*.rgb > %s" % (seg, allrgb))
    with open(os.path.join(d, "wlist.txt"), "w") as f:
        for p in sorted(os.listdir(seg)):
            if p.endswith(".wav"):
                f.write("file 'seg/%s'\n" % p)
    sh(["ffmpeg", "-y", "-loglevel", "error", "-f", "concat", "-safe", "0",
        "-i", os.path.join(d, "wlist.txt"), "-c", "copy", wav])
    sh(["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24",
        "-s", "%dx%d" % (w, h), "-framerate", "120", "-i", allrgb,
        "-c:v", "libx264", "-preset", "veryfast", "-crf", "16",
        "-pix_fmt", "yuv420p", mp4])
    os.remove(allrgb)
    shutil.rmtree(seg, ignore_errors=True)
    os.remove(os.path.join(d, "wlist.txt"))
    n = contact(take.key)
    # Mean luma, printed because a contact sheet hides it: a shot fired from
    # the shaded side of a wall comes in 2.7x darker than the reel and nothing
    # else in the loop says so out loud.
    r = subprocess.run(["ffmpeg", "-hide_banner", "-i", mp4, "-vf",
                        "signalstats,metadata=print:key=lavfi.signalstats.YAVG",
                        "-f", "null", "-"], capture_output=True, text=True).stderr
    ys = [float(x.split("=")[1]) for x in r.split()
          if x.startswith("lavfi.signalstats.YAVG=")]
    if ys and not quiet:
        print("    %-13s %d frames, mean luma %.0f (min %.0f)"
              % (take.key, n, sum(ys) / len(ys), min(ys)))
    return mp4


def probe(take, frames, w=960, h=540):
    """Render only the wanted frames, tiled into clips/<key>/probe.png."""
    d = os.path.join(CLIPS, take.key)
    pd = os.path.join(d, "probe")
    shutil.rmtree(pd, ignore_errors=True)
    os.makedirs(pd, exist_ok=True)
    open(os.path.join(d, "probe.txt"), "w").write(take.script_probe(frames))
    _cfg(d, take.fov)
    subprocess.run([GAME, "--seed", str(take.seed), "--w", str(w), "--h", str(h),
                    "--config", "t.cfg", "--script", "probe.txt"], cwd=d,
                   env=dict(os.environ, LP_NUM_THREADS="8"),
                   stdout=open(os.path.join(d, "probe.log"), "w"),
                   stderr=subprocess.STDOUT, check=True)
    shots = sorted(os.listdir(pd))
    cols = min(4, max(1, len(shots)))
    rows = (len(shots) + cols - 1) // cols
    out = os.path.join(d, "probe.png")
    sh(["ffmpeg", "-y", "-loglevel", "error", "-pattern_type", "glob",
        "-i", os.path.join(pd, "*.png"), "-vf",
        "scale=640:-1,tile=%dx%d" % (cols, rows), "-frames:v", "1", out])
    print("probe -> %s  (%s)" % (out, " ".join(s[1:5] for s in shots)))
    return out


def contact(key, cols=6, rows=4):
    """Contact sheet + a still at every mark — the review surface."""
    d = os.path.join(CLIPS, key)
    mp4 = os.path.join(d, key + ".mp4")
    n = int(subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0",
                            "-count_frames", "-show_entries", "stream=nb_read_frames",
                            "-of", "csv=p=0", mp4], capture_output=True, text=True
                           ).stdout.strip() or 0)
    step = max(1, n // (cols * rows))
    sh(["ffmpeg", "-y", "-loglevel", "error", "-i", mp4, "-vf",
        "select='not(mod(n,%d))',scale=420:-1,tile=%dx%d" % (step, cols, rows),
        "-frames:v", "1", os.path.join(d, "sheet.png")])
    st = os.path.join(d, "stills")
    shutil.rmtree(st, ignore_errors=True)
    os.makedirs(st, exist_ok=True)
    marks = []
    fp = os.path.join(d, "frames.txt")
    if os.path.exists(fp):
        for line in open(fp):
            p = line.split(None, 1)
            if len(p) == 2:
                marks.append((int(p[0]), p[1].strip()))
    if not marks:
        marks = [(int(n * x), "t%02d" % int(x * 100)) for x in (0.05, 0.3, 0.5, 0.7, 0.95)]
    marks = [(min(max(0, f), max(0, n - 1)), l) for f, l in marks]
    sel = "+".join("eq(n\\,%d)" % f for f, _ in marks)      # ONE decode pass
    sh(["ffmpeg", "-y", "-loglevel", "error", "-i", mp4, "-vf",
        "select='%s'" % sel, "-vsync", "vfr", os.path.join(st, "s%03d.png")])
    got = sorted(os.listdir(st))
    for (f, label), src in zip(marks, got):
        os.rename(os.path.join(st, src),
                  os.path.join(st, "%04d_%s.png" % (f, re.sub(r"\W+", "_", label))))
    return n


# ---------------------------------------------------------------------------
# the assembler — beats in, trailer out
# ---------------------------------------------------------------------------
# Loudness lessons, measured on this material, kept verbatim:
#   - the montage's LEVEL SPREAD is the problem (19.5 LU across one cut): a
#     look-ahead rider on the concatenated game bus closes it, hand trims do not
#   - loudness is a TWO-PASS job: render audio, measure, apply makeup
#   - the AAC encoder overshoots the limiter by over a dB on this transient-
#     heavy mix; ceiling 0.60 or the decoded file clips
LEVELLER = "dynaudnorm=f=250:g=15:p=0.55:m=18:s=6:r=0.0"
LUFS_TARGET = -13.5
TRUE_PEAK = 0.60


def clip_paths(key):
    d = os.path.join(CLIPS, key)
    return os.path.join(d, key + ".mp4"), os.path.join(d, key + ".wav")


def clip_frames(key):
    return int(subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "v:0", "-count_frames",
         "-show_entries", "stream=nb_read_frames", "-of", "csv=p=0",
         clip_paths(key)[0]], capture_output=True, text=True).stdout.strip() or 0)


def plan_edit(spec, fps):
    """Turn the beat-authored shot list into an absolute timeline.

    Each entry: {clip, in_f, beats, speed} — `beats` is the cut's length on
    the soundtrack grid, `speed` the playback stretch (1 realtime, 2 slow2x,
    0.5 fast2x). Cut boundaries are quantized to the OUTPUT frame grid with
    the rounding error carried forward, so the last cut of the reel is as
    on-beat as the first. `in_f` omitted = continue where the previous entry
    of the same clip left off (a speed RAMP is two entries).
    """
    spb = 60.0 / spec["bpm"]
    shots = [s for s in spec["shots"] if not s.get("skip")]
    have, last_out = {}, {}
    plan, t, target = [], 0.0, 0.0
    for s in shots:
        n = have.setdefault(s["clip"], clip_frames(s["clip"]))
        fac = float(s.get("speed", 1))
        target += s["beats"] * spb
        dur = round((target - t) * fps) / fps          # cumulative quantize
        in_f = s.get("in_f", last_out.get(s["clip"], 0))
        out_f = in_f + int(round(dur * TICK / fac))
        if n and out_f > n:
            print("  ! %s out_f %d > %d frames — clamped" % (s["clip"], out_f, n))
            out_f = n
            dur = (out_f - in_f) / TICK * fac
        last_out[s["clip"]] = out_f
        plan.append(dict(s, in_f=in_f, out_f=out_f, t0=t, dur=dur, factor=fac))
        t += dur
    return plan, t


def cut(specpath, out=None, fps=60, preview=False):
    spec = json.load(open(specpath))
    out = out or os.path.join(ROOT, spec.get("out", "trailer.mp4"))
    w, h = spec.get("w", 1920), spec.get("h", 1080)
    if preview:
        w, h, out = w // 2, h // 2, os.path.join(ROOT, "preview.mp4")
    plan, total = plan_edit(spec, fps)
    n = len(plan)

    # -- audio: the GAME SOUND the clips captured (shots, impacts, steps) —
    # no synthesized bed. Slow-mo cuts pitch down WITH the picture.

    def audio_chain(base):
        f, al = [], []
        for i, p in enumerate(plan):
            ai, fac = base(i), p["factor"]
            if fac == 1.0:
                asp = ""
            elif 1.0 < fac <= 4.0:              # pitch down WITH the picture
                asp = "asetrate=%d,aresample=48000," % int(48000 / fac)
            elif fac > 4.0:
                asp = "asetrate=12000,aresample=48000,atempo=%.4f," % (4.0 / fac)
            else:                                # fast: stretch up, keep pitch
                asp = "atempo=%.4f," % (1.0 / fac)
            g = p.get("gain_db", 0.0)
            f.append("[%d:a]atrim=start=%.6f:end=%.6f,asetpts=PTS-STARTPTS,%s%s"
                     "afade=t=in:st=0:d=0.02,afade=t=out:st=%.3f:d=0.03[a%d]"
                     % (ai, p["in_f"] / TICK, p["out_f"] / TICK, asp,
                        "volume=%.1fdB," % g if g else "",
                        max(0.0, p["dur"] - 0.03), i))
            al.append("[a%d]" % i)
        return f, al

    def measure():
        f, al = audio_chain(lambda i: i)
        f.append("%sconcat=n=%d:v=0:a=1,%s,"
                 "loudnorm=I=%.1f:print_format=json[afin]"
                 % ("".join(al), n, LEVELLER, LUFS_TARGET))
        args = ["ffmpeg", "-y", "-hide_banner", "-loglevel", "info"]
        for p in plan:
            args += ["-i", clip_paths(p["clip"])[1]]
        args += ["-filter_complex", ";".join(f), "-map", "[afin]",
                 "-f", "null", "-"]
        r = subprocess.run(args, capture_output=True, text=True).stderr
        j = json.loads(r[r.rindex("{"):r.rindex("}") + 1])
        return float(j["input_i"])

    lufs = measure()
    makeup = max(-12.0, min(12.0, LUFS_TARGET - lufs))
    print("mix measured %.1f LUFS -> makeup %+.1f dB" % (lufs, makeup))

    # -- video ---------------------------------------------------------------
    f, vl = [], []
    for i, p in enumerate(plan):
        vi = 2 * i
        vf = "[%d:v]trim=start_frame=%d:end_frame=%d,setpts=PTS-STARTPTS," \
             % (vi, p["in_f"], p["out_f"])
        if p["factor"] != 1.0:
            vf += "setpts=%.4f*PTS," % p["factor"]
        # 180-degree shutter: a realtime cut blends 120 fps frame pairs down to
        # 60, which is real motion blur for free. Slow-mo keeps every source
        # frame and stays sharp — which is what slow motion looks like.
        if p["factor"] == 1.0 and fps == 60 and p.get("shutter", True):
            vf += "tblend=all_mode=average,framestep=2,"
        else:
            vf += "fps=%d," % fps
        # `zoom` crops BEFORE it scales: the same number frames the same crop
        # whatever the source resolution. `pos` [cx, cy] moves the crop's
        # centre (fractions, default centre) — how a macro shot loses the
        # viewmodel's arm without re-rendering.
        if p.get("zoom"):
            cx, cy = p.get("pos", (0.5, 0.5))
            vf += "crop=iw/%.5f:ih/%.5f:(iw-ow)*%.4f:(ih-oh)*%.4f," \
                  % (p["zoom"], p["zoom"], cx, cy)
        vf += "scale=%d:%d," % (w, h)
        for g in (spec.get("grade"), p.get("grade")):
            if g:
                vf += g + ","
        if p.get("fade"):
            vf += "fade=t=in:st=0:d=%.3f," % p["fade"]
        f.append(vf + "setsar=1,format=yuv420p[v%d]" % i)
        vl.append("[v%d]" % i)
    af, al = audio_chain(lambda i: 2 * i + 1)
    f += af
    f.append("%sconcat=n=%d:v=1:a=1[vcat][acat0]"
             % ("".join(v + a for v, a in zip(vl, al)), n))
    f.append("[acat0]%s[acat]" % LEVELLER)
    fo = spec.get("fade_out", 0.35)
    if fo > 0:
        f.append("[vcat]fade=t=out:st=%.3f:d=%.3f[vfin]" % (max(0.0, total - fo), fo))
    else:
        f.append("[vcat]null[vfin]")
    f.append("[acat]volume=%.2fdB,alimiter=level=0:limit=%.3f,"
             "afade=t=in:st=0:d=0.05,afade=t=out:st=%.3f:d=0.4[afin]"
             % (makeup, TRUE_PEAK, max(0.0, total - 0.4)))
    args = ["ffmpeg", "-y", "-loglevel", "error"]
    for p in plan:
        mp4, wav = clip_paths(p["clip"])
        args += ["-i", mp4, "-i", wav]
    args += ["-filter_complex", ";".join(f),
             "-map", "[vfin]", "-map", "[afin]",
             "-c:v", "libx264", "-preset", "slow", "-crf", "18",
             "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart",
             "-shortest", out]
    sh(args)
    print("cut -> %s  (%d cuts, %.2fs, %d bpm)" % (out, n, total, spec["bpm"]))
    for p in plan:
        print("  %6.2f  %-13s %5d..%-5d x%-4.3g %5.2fs %s"
              % (p["t0"], p["clip"], p["in_f"], p["out_f"], p["factor"],
                 p["dur"], p.get("hit", "")))
    return out


def gif(specpath, out, w=640, fps=18, colors=96, bayer=5):
    """Assemble a spec exactly as `cut` does, then encode a README GIF.
    96 colours / 18 fps / coarse bayer dither are SIZE decisions measured on
    this footage (error-diffusion re-noises every pixel every frame and
    doubles the file). `fade_out: 0` in a GIF spec is load-bearing: a looping
    GIF that fades flashes black once a loop."""
    # ...and the SPEC may pin them, because the palette is part of the edit rather than
    # of the command: a banner whose cut changes changes how hard it compresses, and the
    # number that keeps it under the README's weight budget belongs next to the cut it
    # belongs to, not in a shell history. 2026-08-25: the weapon-pass re-cut needed 72
    # to land where the old cut sat at 96.
    with open(specpath) as fh:
        _sp = json.load(fh)
    fps = _sp.get("gif_fps", fps)
    colors = _sp.get("gif_colors", colors)
    tmp = os.path.join(CACHE, ".gif_src.mp4")
    cut(specpath, out=tmp, fps=fps)
    pal = os.path.join(CACHE, ".gif_pal.png")
    vf = "fps=%d,scale=%d:-1:flags=lanczos" % (fps, w)
    sh(["ffmpeg", "-y", "-loglevel", "error", "-i", tmp, "-vf",
        vf + ",palettegen=max_colors=%d:stats_mode=diff" % colors, pal])
    sh(["ffmpeg", "-y", "-loglevel", "error", "-i", tmp, "-i", pal,
        "-lavfi", vf + "[x];[x][1:v]paletteuse=dither=bayer:bayer_scale=%d:"
        "diff_mode=rectangle" % bayer, "-loop", "0", out])
    os.remove(tmp)
    os.remove(pal)
    print("gif -> %s  %.1f MB" % (out, os.path.getsize(out) / 1e6))
    return out


MEDIA = ROOT
REPO = os.path.dirname(MEDIA)
SPEC = os.path.join(MEDIA, "scene.json")
sys.path.insert(0, MEDIA)
# shots.py imports the Take API back FROM this module; register ourselves so
# `from media import ...` resolves to the running script instead of loading
# this file a second time.
sys.modules.setdefault("media", sys.modules[__name__])


def die(msg):
    print("media.py: %s" % msg, file=sys.stderr)
    sys.exit(1)


# --- the still recipes -----------------------------------------------------
# The two README stills — ordinary gameplay frames from live 12-bot matches on
# hard; only the SUN is staged (a weapon needs a raking light to read; these
# arenas draw it flat overhead).
#
# fight.png  seed 20348, t=1404, dusk. The throwaway pre-shot exists because
#            the local tracer re-anchors to the viewmodel muzzle only once a
#            frame has actually RENDERED — without it the beam leaves the
#            third-person anchor under the camera — and the SECOND throwaway
#            shot mid-flight re-anchors it again, or the recoil moves the
#            muzzle off the beam's frozen start (the "tracer torn off the
#            barrel" frame). Two ticks after the trigger the tracer is a
#            continuous beam from the muzzle into the crosshair starburst,
#            and the killfeed's top line is the player's own headshot.
# scope.png  seed 80468 (GRIT — teal against fight.png's warm sand), t=1354.
#            Through the scope, ONE tick after a 37 m headshot on PIVOT: the
#            tracer still enters the disc diagonally INTO the fresh spark
#            burst, a second bot stands armed in the glass, and the killfeed
#            tops with the player's own headshot. The `look 0 90` is RECOIL
#            COMPENSATION, calibrated by sweep (0/60/90/120/300/600): the
#            round leaves with rec = 0 and lands on the head, but one tick
#            later the kick has already lifted the camera ~30 px off the
#            impact — 90 counts of pull-down puts the crosshair back ON the
#            burst, exactly the discipline a real shooter's hand does. The
#            aim is a fixed `aim` at PIVOT's head: aimbot snaps to the
#            NEAREST bot, the wrong one at long range.
STILLS = {
    # Re-staged 2026-08-25 after the map-gen v2 seed break re-rolled every
    # arena: the old seeds/coords photographed the ELIMINATED screen (fight)
    # and a wall one metre past the muzzle (scope). Both recipes aim by
    # AIMBOT now, never by fixed world coordinates — a layout change then
    # re-frames instead of breaking. Seeds were scanned for "player alive at
    # the freeze + a bot 8-16 m down the muzzle" (php>0 in `match`, bang d=).
    # 20350: grit QUARRY + collapse signature — tracer to a hit-lit bot who
    # is aiming BACK, three more behind, a corpse and brick spill in frame.
    "fight.png": (20350, "bots 12; skill hard; fraglimit 1000; wait 1400; "
                  "botfreeze on; wait 4; weapon ar; sun 7 -75; aimbot; "
                  "shot {tmp}; +fire; wait 1; shot {tmp}; wait 1; "
                  "shot media/fight.png"),
    # 31337: grit SWEPT — the second aimbot AFTER the ADS ramp re-centres
    # the scope on the head; the frame lands two ticks after the trigger,
    # impact star on the target in the glass, the kill already atop the
    # killfeed, casing in the air. The warm shots stay: the tracer needs a
    # RENDERED frame before the trigger or it anchors at the 3P muzzle.
    "scope.png": (31337, "bots 12; skill hard; fraglimit 1000; wait 1350; "
                  "botfreeze on; wait 4; weapon sr; sun 14 -55; aimbot; "
                  "+ads; wait 130; aimbot; shot {tmp}; +fire; wait 1; "
                  "shot {tmp}; wait 1; shot media/scope.png"),
}


def die(msg):
    print("media.py: %s" % msg, file=sys.stderr)
    sys.exit(1)


def sanity():
    if not os.access(GAME, os.X_OK):
        die("no runnable binary at %s — run `make build/game` first" % GAME)
    if not os.path.exists(SPEC):
        die("missing screenplay %s" % SPEC)
    src = os.path.join(REPO, "code", "game.c")
    if os.path.exists(src) and os.path.getmtime(src) > os.path.getmtime(GAME):
        print("WARNING: code/game.c is newer than build/game — the media "
              "would capture a STALE build. `touch code/game.c && make "
              "build/game` and check a gcc line actually ran (clock skew).")


def clips_of_spec():
    seen, out = set(), []
    for sh in json.load(open(SPEC))["shots"]:
        if sh["clip"] not in seen:
            seen.add(sh["clip"])
            out.append(sh["clip"])
    return out


def recompress(path):
    # The harness writes PNG uncompressed (2.77 MB at 720p); recompress
    # losslessly (ImageMagick, else ffmpeg, else keep).
    tmp = path + ".opt.png"
    if shutil.which("convert"):
        subprocess.run(["convert", path, "-strip",
                        "-define", "png:compression-level=9",
                        "-define", "png:compression-filter=5", tmp], check=True)
    elif shutil.which("ffmpeg"):
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-i", path,
                        "-compression_level", "9", tmp], check=True)
    else:
        return
    os.replace(tmp, path)


def stills():
    for name, (seed, script) in STILLS.items():
        cfg = os.path.join(tempfile.gettempdir(), "media-%d.cfg" % os.getpid())
        script = script.format(tmp=os.path.join(tempfile.gettempdir(),
                                                "media-warm.png"))
        r = subprocess.run([GAME, "--seed", str(seed), "--config", cfg,
                            "--w", "1280", "--h", "720", "--do", script],
                           cwd=REPO)
        out = os.path.join(MEDIA, name)
        if r.returncode != 0 or not os.path.exists(out):
            die("%s was not written" % name)
        recompress(out)
        print("  %-12s %5.1f kB" % (name, os.path.getsize(out) / 1024.0))


def render_clips(keys):
    for k in keys:
        if k not in shots.SHOTS:
            die("unknown shot %r — see `list`" % k)
        render(shots.SHOTS[k]())


def build_gif():
    gif(SPEC, os.path.join(MEDIA, "hero.gif"), 832)


def build_mp4():
    cut(SPEC)
    src = os.path.join(MEDIA, json.load(open(SPEC))["out"])
    if not os.path.exists(src):
        die("cut produced no %s" % src)
    shutil.copyfile(src, os.path.join(MEDIA, "hero.mp4"))
    print("  hero.mp4     %5.1f MB"
          % (os.path.getsize(os.path.join(MEDIA, "hero.mp4")) / 1e6))


def check():
    sanity()
    print("game        %s" % GAME)
    print("screenplay  %s" % SPEC)
    for c in clips_of_spec():
        p = os.path.join(CLIPS, c, c + ".mp4")
        if not os.path.exists(p):
            print("  %-16s NOT RENDERED" % c)
        elif os.path.getmtime(p) < os.path.getmtime(GAME):
            print("  %-16s STALE (older than build/game)" % c)
        else:
            print("  %-16s ok" % c)
    for t in ("ffmpeg", "convert"):
        print("%-11s %s" % (t, shutil.which(t) or "MISSING"))


def main():
    global shots   # module-wide: render_clips()/list read it too
    import shots   # lazy: shots.py imports the Take API back from this module
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    skip = "--skip-render" in sys.argv
    verb = args[0] if args else "all"
    if verb == "check":
        return check()
    if verb == "list":
        for k, fn in shots.SHOTS.items():
            t = fn()
            print("  %-15s %4d f  %5.2f s  %s"
                  % (k, t.f, t.f / TICK, t.notes))
        return
    if verb == "probe":
        if len(args) < 2:
            die("probe needs a shot key")
        t = shots.SHOTS[args[1]]()
        raw = [int(x) for x in args[2:]]
        if len(raw) <= 1:
            count = raw[0] if raw else 8
            if count < 1:
                die("probe frame count must be positive")
            if count == 1:
                want = [max(0, t.f - 1) // 2]
            else:
                want = [round(i * max(0, t.f - 1) / (count - 1))
                        for i in range(count)]
        else:
            want = raw
        return probe(t, want)
    if verb == "render":
        sanity()
        return render_clips(args[1:] or clips_of_spec())
    if verb not in ("all", "stills", "gif", "mp4"):
        die("unknown verb %r — see the docstring" % verb)
    sanity()
    if verb in ("all", "stills"):
        stills()
    if verb in ("all", "gif", "mp4") and not skip:
        print("re-rendering %d clips (the banner must never mix two builds)"
              % len(clips_of_spec()))
        render_clips(clips_of_spec())
    if verb in ("all", "gif"):
        build_gif()
    if verb in ("all", "mp4"):
        build_mp4()
    print("done.")


if __name__ == "__main__":
    main()
