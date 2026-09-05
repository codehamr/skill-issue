#!/usr/bin/env python3
"""Reproducible game capture and trailer assembly, with verified clip caches.
Seeds, commands, capture settings and binary hashes identify every take.
Exact pixels also depend on the graphics driver and encoder versions.
Sound in hero.mp4 comes from the game itself.

    ./media/media.py            # everything: stills + clip re-render + gif + mp4
    ./media/media.py stills     # fight.png + scope.png only            (~2 min)
    ./media/media.py gif        # re-render the scene clips + hero.gif  (~10 min)
    ./media/media.py mp4        # re-render + hero.mp4 (game sound);
                                #   `mp4 --skip-render` right after `gif`
    ./media/media.py check      # tooling + staleness report, renders nothing
    ./media/media.py list       # every shot and the screenplay's actual cuts
    ./media/media.py review     # final GIF contact sheet and timing/size report
    ./media/media.py render K.. # (re-)render named clips at full quality
    ./media/media.py probe K N  # N framing stills for shot K, no video —
                                #   THE authoring loop; then read
                                #   .cache/clips/K/probe.log (the event log)
    --skip-render               # gif/mp4: assemble from cached clips (ONLY for
                                #   iterating on the EDIT — after a look change
                                #   the banner would mix two builds)

The responsibilities are:
  shots.py     the scenes and camera choreography
  scene.json   the screenplay (beat timing, speeds, zooms, palette)
  media.py     this file — the Take builder, renderer, beat assembler and the
               still recipes; touched only when the MACHINERY changes
The clip list for hero.gif/hero.mp4 is read FROM scene.json, so a shot added
to the screenplay is automatically in the re-render set. All caches live in
media/.cache/ (gitignored, safe to delete).
"""
import sys
import tempfile

sys.dont_write_bytecode = True   # keep media/ clean: no __pycache__

import hashlib, json, math, os, re, shutil, subprocess, random, time

ROOT = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.join(os.path.dirname(ROOT), "build", "game")
# Everything regenerable hides in .cache/ — the media folder itself stays the
# four assets plus the kit (media.py, scene.json, shots.py).
CACHE = os.path.join(ROOT, ".cache")
CLIPS = os.path.join(CACHE, "clips")
STAGES = os.path.join(CACHE, "stages")
TICK = 120.0
ENCODER_THREADS = "8"
FILTER_THREADS = "2"


def file_hash(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()

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
        binary_hash = file_hash(GAME)
        try:
            with open(p) as stream:
                d = json.load(stream)
        except (OSError, ValueError):
            d = {}
        if d.get("binary_sha256") != binary_hash:
            with tempfile.TemporaryDirectory(prefix="map-", dir=STAGES) as temporary:
                r = subprocess.run([GAME, "--seed", str(seed), "--config",
                                    os.path.join(temporary, "fresh.cfg"), "--do", "map"],
                                   capture_output=True, text=True, check=True).stdout
            theme = re.search(r"theme=(\w+)", r).group(1)
            solids = [tuple(float(x) for x in m.groups())
                      for m in re.finditer(r"solid \d+ min=\((\S+) (\S+) (\S+)\)"
                                           r" max=\((\S+) (\S+) (\S+)\)", r)]
            d = dict(theme=theme, solids=solids, binary_sha256=binary_hash)
            with open(p, "w") as stream:
                json.dump(d, stream)
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
            # capture_run advances one simulation tick before rendering.
            out.append("wait 1")
            if wi < len(want) and want[wi] == i:
                out.append("shot probe/p%04d.png" % i)
                wi += 1
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


def ffmpeg_threads(cmd):
    """Bound both decoder pools and filters when an edit opens many inputs."""
    if not isinstance(cmd, list) or cmd[0] != "ffmpeg":
        return cmd
    out = [cmd[0], "-filter_threads", FILTER_THREADS,
           "-filter_complex_threads", FILTER_THREADS]
    for arg in cmd[1:]:
        if arg == "-i":
            out += ["-threads", ENCODER_THREADS]
        out.append(arg)
    return out[:-1] + ["-threads", ENCODER_THREADS, out[-1]]


def sh(cmd, **kw):
    return subprocess.run(ffmpeg_threads(cmd), shell=isinstance(cmd, str), check=True, **kw)


def replace_media(source, destination):
    """Publish atomically, allowing shared-host media indexers to release files."""
    for attempt in range(20):
        try:
            os.replace(source, destination)
            return
        except PermissionError:
            if attempt == 19:
                raise
            time.sleep(0.25)


def capture_settings(take):
    w, h = take.res or (1280, 720)
    return w, h


def provenance(take, w=None, h=None):
    if w is None or h is None:
        w, h = capture_settings(take)
    return dict(format_version=1, binary_sha256=file_hash(GAME),
                script_sha256=hashlib.sha256(take.script().encode()).hexdigest(),
                seed=take.seed, width=w, height=h, fov=take.fov, frames=take.f)


def take_of(key):
    import shots as library
    if key not in library.SHOTS:
        raise ValueError("unknown shot %r — see `list`" % key)
    return library.SHOTS[key]()


def cache_status(key):
    mp4, wav = clip_paths(key)
    path = os.path.join(CLIPS, key, "manifest.json")
    if not all(os.path.isfile(p) for p in (mp4, wav, path)):
        return "missing clip, audio or provenance; render this take"
    try:
        with open(path) as stream:
            manifest = json.load(stream)
        if manifest.get("capture") != provenance(take_of(key)):
            return "binary or take settings changed; render this take"
        for label, asset in (("video_sha256", mp4), ("audio_sha256", wav)):
            if manifest.get(label) != file_hash(asset):
                return "cached media changed or is incomplete; render this take"
    except (OSError, ValueError) as error:
        return "invalid provenance: %s" % error
    return None


def require_clips(keys):
    errors = [(key, cache_status(key)) for key in keys]
    errors = [(key, error) for key, error in errors if error]
    if errors:
        raise ValueError("unusable clip cache:\n" + "\n".join(
            "  %s: %s" % item for item in errors))


def render(take, w=None, h=None, quiet=False):
    """Run the take, encode clips/<key>/<key>.mp4 (120 fps) + .wav + sheet."""
    if w is None or h is None:
        w, h = capture_settings(take)
    expected = provenance(take, w, h)
    d = os.path.join(CLIPS, take.key)
    seg = os.path.join(d, "seg")
    shutil.rmtree(seg, ignore_errors=True)
    os.makedirs(seg, exist_ok=True)
    manifest_path = os.path.join(d, "manifest.json")
    if os.path.exists(manifest_path):
        os.remove(manifest_path)
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
    rgb_paths = [os.path.join(seg, p) for p in sorted(os.listdir(seg))
                 if p.endswith(".rgb")]
    expected_bytes = take.f * w * h * 3
    if sum(os.path.getsize(p) for p in rgb_paths) != expected_bytes:
        raise RuntimeError("%s: incomplete raw capture" % take.key)
    with open(os.path.join(d, "wlist.txt"), "w") as f:
        for p in sorted(os.listdir(seg)):
            if p.endswith(".wav"):
                f.write("file 'seg/%s'\n" % p)
    sh(["ffmpeg", "-y", "-loglevel", "error", "-f", "concat", "-safe", "0",
        "-i", os.path.join(d, "wlist.txt"), "-c", "copy", wav])
    command = ["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24",
        "-s", "%dx%d" % (w, h), "-framerate", "120", "-i", "pipe:0",
        "-c:v", "libx264", "-preset", "veryfast", "-crf", "16",
        "-threads", ENCODER_THREADS, "-pix_fmt", "yuv420p", mp4]
    with subprocess.Popen(command, stdin=subprocess.PIPE) as encoder:
        try:
            for path in rgb_paths:
                with open(path, "rb") as raw:
                    shutil.copyfileobj(raw, encoder.stdin, length=1024 * 1024)
        finally:
            encoder.stdin.close()
        if encoder.wait():
            raise RuntimeError("%s: video encoding failed" % take.key)
    shutil.rmtree(seg, ignore_errors=True)
    os.remove(os.path.join(d, "wlist.txt"))
    n = contact(take.key)
    if n != take.f:
        raise RuntimeError("%s: encoded %d frames, expected %d" % (take.key, n, take.f))
    if expected != provenance(take, w, h):
        raise RuntimeError("The game binary changed during capture; rerender this take.")
    with open(manifest_path, "w") as stream:
        json.dump(dict(capture=expected, video_sha256=file_hash(mp4),
                       audio_sha256=file_hash(wav)), stream, indent=2)
    # Mean luma, printed because a contact sheet hides it: a shot fired from
    # the shaded side of a wall comes in 2.7x darker than the reel and nothing
    # else in the loop says so out loud.
    r = subprocess.run(["ffmpeg", "-hide_banner", "-threads", ENCODER_THREADS,
                        "-filter_threads", FILTER_THREADS, "-i", mp4, "-vf",
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
    result = subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "v:0", "-count_frames",
         "-show_entries", "stream=nb_read_frames", "-of", "csv=p=0",
         clip_paths(key)[0]], capture_output=True, text=True, check=True)
    return int(result.stdout.strip() or 0)


def plan_edit(spec, fps, require_media=True):
    """Turn the beat-authored shot list into an absolute timeline.

    Each entry: {clip, in_f, beats, speed} — `beats` is the cut's length on
    the soundtrack grid, `speed` the playback stretch (1 realtime, 2 slow2x,
    0.5 fast2x). Cut boundaries are quantized to the OUTPUT frame grid with
    the rounding error carried forward, so the last cut of the reel is as
    on-beat as the first. `in_f` omitted = continue where the previous entry
    of the same clip left off (a speed RAMP is two entries).
    """
    bpm = float(spec["bpm"])
    if not math.isfinite(bpm) or bpm <= 0 or not isinstance(fps, int) or fps <= 0:
        raise ValueError("BPM and output FPS must be positive")
    spb = 60.0 / bpm
    shots = [s for s in spec["shots"] if not s.get("skip")]
    if not shots:
        raise ValueError("screenplay has no active shots")
    have, last_out = {}, {}
    plan, t, target = [], 0.0, 0.0
    for s in shots:
        key = s["clip"]
        if key not in have:
            have[key] = clip_frames(key) if require_media else take_of(key).f
        n = have[key]
        fac = float(s.get("speed", 1))
        beats = float(s["beats"])
        if not math.isfinite(fac) or fac <= 0 or not math.isfinite(beats) or beats <= 0:
            raise ValueError("%s: speed and beats must be positive finite numbers" % key)
        target += beats * spb
        dur = round((target - t) * fps) / fps          # cumulative quantize
        in_f = s.get("in_f", last_out.get(s["clip"], 0))
        if not isinstance(in_f, int) or in_f < 0 or dur <= 0:
            raise ValueError("%s: invalid source frame or sub-frame cut duration" % key)
        out_f = in_f + int(round(dur * TICK / fac))
        if out_f <= in_f or out_f > n:
            raise ValueError("%s: source range %d..%d exceeds %d frames; extend the take or shorten the cut"
                             % (key, in_f, out_f, n))
        last_out[s["clip"]] = out_f
        plan.append(dict(s, in_f=in_f, out_f=out_f, t0=t, dur=dur, factor=fac))
        t += dur
    return plan, t


def cut(specpath, out=None, fps=60, preview=False):
    spec = json.load(open(specpath))
    out = out or os.path.join(ROOT, spec.get("out", "trailer.mp4"))
    w, h = spec.get("w", 1280), spec.get("h", 720)
    if any(not isinstance(value, int) or value <= 0 or value % 2 for value in (w, h)):
        raise ValueError("screenplay width and height must be positive even integers")
    if preview:
        w, h, out = w // 2, h // 2, os.path.join(ROOT, "preview.mp4")
    require_clips(list(dict.fromkeys(s["clip"] for s in spec["shots"] if not s.get("skip"))))
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
                     "apad,atrim=duration=%.6f,"
                     "afade=t=in:st=0:d=0.02,afade=t=out:st=%.3f:d=0.03[a%d]"
                     % (ai, p["in_f"] / TICK, p["out_f"] / TICK, asp,
                        "volume=%.1fdB," % g if g else "",
                        p["dur"], max(0.0, p["dur"] - 0.03), i))
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
        r = subprocess.run(ffmpeg_threads(args), capture_output=True, text=True, check=True).stderr
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
        # Source rounding and tblend may remove one endpoint frame. Every cut
        # still occupies its exact authored number of output frames.
        vf += "tpad=stop_mode=clone:stop_duration=%.6f,trim=end_frame=%d,setpts=N/(%d*TB)," \
              % (2.0 / fps, round(p["dur"] * fps), fps)
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
             "-r", str(fps), "-fps_mode", "cfr",
             "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart",
             "-shortest", out]
    sh(args)
    print("cut -> %s  (%d cuts, %.2fs, %d bpm)" % (out, n, total, spec["bpm"]))
    for p in plan:
        print("  %6.2f  %-13s %5d..%-5d x%-4.3g %5.2fs %s"
              % (p["t0"], p["clip"], p["in_f"], p["out_f"], p["factor"],
                 p["dur"], p.get("hit", "")))
    return out


def gif_shot_palettes(spec, source, out, temporary, w, fps, colors, bayer):
    """One stable palette per clip, including all of its final edit ranges.

    Repeated clips share their palette so the dunes loop and scope speed
    ramp cannot change color at a cut. Gifsicle merges local color tables
    losslessly; passing GIFs through a generic pal8 conversion would replace
    them with a fixed low-quality palette.
    """
    if not shutil.which("gifsicle"):
        raise RuntimeError("gif_palette: shot requires gifsicle; install it or select global")
    plan, duration = plan_edit(spec, fps, require_media=False)
    groups, ranges = {}, []
    for p in plan:
        start = round(p["t0"] * fps)
        stop = start + round(p["dur"] * fps)
        ranges.append((p["clip"], start, stop))
        groups.setdefault(p["clip"], []).append((start, stop))
    palettes = {}
    for index, (key, spans) in enumerate(groups.items()):
        palette = os.path.join(temporary, "palette-%02d.png" % index)
        selected = "+".join("between(n,%d,%d)" % (start, stop - 1)
                            for start, stop in spans)
        sh(["ffmpeg", "-y", "-loglevel", "error", "-i", source,
            "-vf", "select='%s',scale=%d:-1:flags=lanczos,"
                   "palettegen=max_colors=%d:stats_mode=full" % (selected, w, colors),
            "-frames:v", "1", palette])
        palettes[key] = palette
    segments = []
    for index, (key, start, stop) in enumerate(ranges):
        segment = os.path.join(temporary, "cut-%02d.gif" % index)
        vf = "trim=start_frame=%d:end_frame=%d,setpts=PTS-STARTPTS,scale=%d:-1:flags=lanczos" \
             % (start, stop, w)
        sh(["ffmpeg", "-y", "-loglevel", "error", "-i", source,
            "-i", palettes[key], "-filter_complex", vf + "[v];[v][1:v]"
            "paletteuse=dither=bayer:bayer_scale=%d:diff_mode=rectangle" % bayer,
            "-r", str(fps), "-fps_mode", "cfr", "-loop", "0", segment])
        segments.append(segment)
    sh(["gifsicle", "--merge", "--loopcount=0", "-O3", "--optimize=keep-empty",
        "--output", out] + segments)
    if spec.get("gif_lossy", 0):
        optimized = os.path.join(temporary, "optimized.gif")
        sh(["gifsicle", "-O3", "--optimize=keep-empty",
            "--lossy=%d" % spec["gif_lossy"], out, "--output", optimized])
        out = optimized
    # Optimization may change rectangle sizes and local tables, but every
    # authored frame and delay must survive into the delivered animation.
    result = subprocess.run([
        "ffprobe", "-v", "error", "-count_frames", "-select_streams", "v:0",
        "-show_entries", "stream=nb_read_frames:format=duration", "-of", "json", out],
        capture_output=True, text=True, check=True)
    measured = json.loads(result.stdout)
    frames = int(measured["streams"][0]["nb_read_frames"])
    actual_duration = float(measured["format"]["duration"])
    if frames != round(duration * fps) or abs(actual_duration - duration) > 1.0 / fps + 1e-6:
        raise RuntimeError("merged GIF timing differs from the screenplay; run review")
    print("  %d stable clip palettes, %d cuts, %d verified frames" %
          (len(palettes), len(segments), frames))
    return out


def gif(specpath, out, w=640, fps=18, colors=96, bayer=5):
    """Assemble a spec exactly as `cut` does, then encode a README GIF.
    The screenplay owns width, frame rate, palette size and Bayer strength.
    `gif_palette: shot` uses stable clip palettes and requires gifsicle;
    `global` uses one palette for the entire edit. `fade_out: 0` avoids a
    black flash at the loop boundary."""
    with open(specpath) as fh:
        _sp = json.load(fh)
    fps = _sp.get("gif_fps", fps)
    colors = _sp.get("gif_colors", colors)
    w = _sp.get("gif_width", w)
    bayer = _sp.get("gif_bayer", bayer)
    mode = _sp.get("gif_palette", "global")
    if not isinstance(w, int) or w <= 0 or not isinstance(colors, int) or not 4 <= colors <= 256:
        raise ValueError("GIF width must be positive and palette size must be 4..256")
    if not isinstance(bayer, int) or not 0 <= bayer <= 5:
        raise ValueError("GIF Bayer strength must be an integer in 0..5")
    if mode not in ("shot", "global"):
        raise ValueError("gif_palette must be shot or global")
    lossiness = _sp.get("gif_lossy", 0)
    if not isinstance(lossiness, int) or lossiness < 0:
        raise ValueError("gif_lossy must be a nonnegative integer")
    if lossiness and mode != "shot":
        raise ValueError("gif_lossy requires gif_palette: shot")
    if mode == "shot" and not shutil.which("gifsicle"):
        raise RuntimeError("gif_palette: shot requires gifsicle; install it or select global")
    os.makedirs(CACHE, exist_ok=True)
    # Separate scratch paths let preview edits coexist. Shared-host media
    # indexers can briefly retain a file; cleanup must not discard a good GIF.
    with tempfile.TemporaryDirectory(prefix="gif-", dir=CACHE,
                                     ignore_cleanup_errors=True) as temporary:
        tmp, pal = (os.path.join(temporary, name) for name in ("source.mp4", "palette.png"))
        encoded = os.path.join(temporary, "finished.gif")
        cut(specpath, out=tmp, fps=fps)
        if mode == "shot":
            encoded = gif_shot_palettes(_sp, tmp, encoded, temporary, w, fps, colors, bayer)
        else:
            vf = "fps=%d,scale=%d:-1:flags=lanczos" % (fps, w)
            sh(["ffmpeg", "-y", "-loglevel", "error", "-i", tmp, "-vf",
                vf + ",palettegen=max_colors=%d:stats_mode=full" % colors, pal])
            sh(["ffmpeg", "-y", "-loglevel", "error", "-i", tmp, "-i", pal,
                "-lavfi", vf + "[x];[x][1:v]paletteuse=dither=bayer:bayer_scale=%d:"
                "diff_mode=rectangle" % bayer, "-loop", "0", encoded])
        replace_media(encoded, out)
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


# --- stills select frames from the trailer's shared staging -----------------
# These are native game PNGs, losslessly recompressed with no trailer grade.
STILLS = {
    "fight.png": ("quarry_crossfire", 103),
    "scope.png": ("frost_scope", 118),
}


def die(msg):
    print("media.py: %s" % msg, file=sys.stderr)
    sys.exit(1)


def sanity():
    if not os.access(GAME, os.X_OK):
        die("no runnable binary at %s — run `make build/game` first" % GAME)
    if not os.path.exists(SPEC):
        die("missing screenplay %s" % SPEC)
    sources = [os.path.join(directory, name)
               for directory, _, names in os.walk(os.path.join(REPO, "code"))
               for name in names if name.endswith((".c", ".inc"))]
    if any(os.path.getmtime(src) > os.path.getmtime(GAME) for src in sources):
        print("WARNING: game sources are newer than build/game — the media "
              "would capture a STALE build. `touch code/game.c && make "
              "build/game` and check a gcc line actually ran (clock skew).")


def clips_of_spec():
    seen, out = set(), []
    for sh in json.load(open(SPEC))["shots"]:
        if sh.get("skip"):
            continue
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
    os.makedirs(CACHE, exist_ok=True)
    binary_hash = file_hash(GAME)
    for name, (key, frame) in STILLS.items():
        take = take_of(key)
        if not 0 <= frame < take.f:
            raise ValueError("%s: frame %d is outside %s" % (name, frame, key))
        w, h = capture_settings(take)
        take.timeline = take.timeline[:frame + 1]
        with tempfile.TemporaryDirectory(prefix="still-", dir=CACHE,
                                         ignore_cleanup_errors=True) as temporary:
            os.mkdir(os.path.join(temporary, "probe"))
            _cfg(temporary, take.fov)
            with open(os.path.join(temporary, "script.txt"), "w") as stream:
                stream.write(take.script_probe([frame]))
            with open(os.path.join(CACHE, "still-" + key + ".log"), "w") as log:
                subprocess.run([GAME, "--seed", str(take.seed), "--config", "t.cfg",
                                "--w", str(w), "--h", str(h), "--script", "script.txt"],
                               cwd=temporary, env=dict(os.environ, LP_NUM_THREADS="8"),
                               stdout=log, stderr=subprocess.STDOUT, check=True)
            captured = os.path.join(temporary, "probe", "p%04d.png" % frame)
            if not os.path.exists(captured):
                die("%s was not written" % name)
            if file_hash(GAME) != binary_hash:
                raise RuntimeError("The game binary changed during capture; rerun stills.")
            out = os.path.join(MEDIA, name)
            shutil.move(captured, out)
        recompress(out)
        print("  %-12s %dx%d %5.1f kB — %s frame %d" %
              (name, w, h, os.path.getsize(out) / 1024.0, key, frame))


def review():
    """Inspect the delivered GIF, independent of its intermediate clip cache."""
    source = os.path.join(MEDIA, "hero.gif")
    output = os.path.join(REPO, "screenshots", "trailer-review")
    os.makedirs(output, exist_ok=True)
    with open(SPEC) as stream:
        spec = json.load(stream)
    fps = spec.get("gif_fps", 18)
    plan, duration = plan_edit(spec, fps, require_media=False)
    result = subprocess.run([
        "ffprobe", "-v", "error", "-count_frames", "-select_streams", "v:0",
        "-show_entries", "stream=width,height,nb_read_frames,duration:format=duration,size",
        "-of", "json", source], capture_output=True, text=True, check=True)
    measured = json.loads(result.stdout)
    stream, container = measured["streams"][0], measured["format"]
    actual_duration = float(container["duration"])
    planned_frames, actual_frames = round(duration * fps), int(stream["nb_read_frames"])
    samples = max(1, math.ceil(actual_duration * 2))
    cols, rows = min(4, samples), math.ceil(samples / 4)
    sheet = os.path.join(output, "final-sheet.png")
    sh(["ffmpeg", "-y", "-loglevel", "error", "-ignore_loop", "1", "-i", source,
        "-vf", "fps=2,scale=416:-1:flags=lanczos,tile=%dx%d" % (cols, rows),
        "-frames:v", "1", sheet])
    expected_width = spec.get("gif_width", 832)
    expected_height = round(expected_width * spec.get("h", 720) / spec.get("w", 1280))
    mismatches = []
    if planned_frames != actual_frames:
        mismatches.append("frames %d != %d" % (actual_frames, planned_frames))
    if (stream["width"], stream["height"]) != (expected_width, expected_height):
        mismatches.append("dimensions %dx%d != %dx%d" %
                          (stream["width"], stream["height"], expected_width, expected_height))
    if abs(actual_duration - duration) > 1.0 / fps + 1e-6:
        mismatches.append("duration %.3f != %.3f" % (actual_duration, duration))
    report = dict(source="media/hero.gif", sha256=file_hash(source),
                  screenplay_sha256=file_hash(SPEC),
                  planned=dict(frames=planned_frames, duration=duration, fps=fps,
                               width=expected_width, height=expected_height, cuts=plan),
                  actual=dict(frames=actual_frames, duration=actual_duration,
                              width=stream["width"], height=stream["height"],
                              bytes=int(container["size"])),
                  frames_match=planned_frames == actual_frames, mismatches=mismatches,
                  contact_sheet=dict(file="final-sheet.png", samples_per_second=2,
                                     columns=cols, rows=rows))
    with open(os.path.join(output, "final-report.json"), "w") as file:
        json.dump(report, file, indent=2)
        file.write("\n")
    print("review -> %s\n  %dx%d, %.3fs, %d/%d frames, %.2f MB" %
          (sheet, stream["width"], stream["height"], actual_duration,
           actual_frames, planned_frames, int(container["size"]) / 1e6))
    if mismatches:
        raise ValueError("final GIF differs from screenplay: " + "; ".join(mismatches))
    return report


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
        problem = cache_status(c)
        print("  %-16s %s" % (c, problem or "ok (content verified)"))
    for t in ("ffmpeg", "ffprobe", "convert", "gifsicle"):
        print("%-11s %s" % (t, shutil.which(t) or "MISSING"))


def main():
    global shots   # module-wide: render_clips()/list read it too
    import shots   # lazy: shots.py imports the Take API back from this module
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    skip = "--skip-render" in sys.argv
    verb = args[0] if args else "all"
    if verb == "check":
        return check()
    if verb == "review":
        return review()
    if verb == "list":
        for k, fn in shots.SHOTS.items():
            t = fn()
            print("  %-15s %4d f  %5.2f s  %s"
                  % (k, t.f, t.f / TICK, t.notes))
        with open(SPEC) as stream:
            spec = json.load(stream)
        fps = spec.get("gif_fps", 18)
        plan, total = plan_edit(spec, fps, require_media=False)
        print("\nscreenplay: %d cuts, %.3f seconds, %d frames at %d fps" %
              (len(plan), total, round(total * fps), fps))
        for p in plan:
            print("  %6.3f..%-6.3f %-18s source %4d..%-4d stretch %.3g" %
                  (p["t0"], p["t0"] + p["dur"], p["clip"],
                   p["in_f"], p["out_f"], p["factor"]))
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
