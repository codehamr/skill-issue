"""Decoded media and cue timing gates. All limits live with their measurement."""
import array
import json
import math
import os
import subprocess
import wave

SOURCE_HZ = 120
AUDIO_HZ = 48000
DELIVERED_HZ = 60
SOURCE_CUE_LIMIT = 1 / SOURCE_HZ
ENCODE_CUE_LIMIT = 1 / DELIVERED_HZ
CORRELATION_MIN = 0.90
SYNC_HZ = 1500


def probe(path):
    result = subprocess.run(["ffprobe", "-v", "error", "-count_frames", "-show_streams",
                             "-show_format", "-of", "json", path],
                            capture_output=True, text=True, check=True)
    return json.loads(result.stdout)


def audio(path, hz=AUDIO_HZ):
    result = subprocess.run(["ffmpeg", "-v", "error", "-threads", "2", "-i", path,
                             "-map", "0:a:0", "-ac", "2", "-ar", str(hz),
                             "-f", "f32le", "pipe:1"], capture_output=True, check=True)
    values = array.array("f")
    values.frombytes(result.stdout)
    if not values or len(values) % 2 or any(not math.isfinite(x) for x in values):
        raise ValueError("missing or invalid decoded stereo audio")
    return values


def source_audio(path, expected_frames):
    with wave.open(path, "rb") as stream:
        channels, rate, width, frames = (stream.getnchannels(), stream.getframerate(),
                                         stream.getsampwidth(), stream.getnframes())
        data = stream.readframes(frames)
    if (channels, rate, width, frames) != (2, AUDIO_HZ, 2, expected_frames) or len(data) != frames * 4:
        raise ValueError("source stereo PCM is incomplete or differs from its continuous trace clock")
    return frames



def tail(path, summary, header, video_frames):
    frames = int(summary["frames"])
    rms = float(summary["rms"])
    if (not 0 < frames <= 10 * AUDIO_HZ or frames % (AUDIO_HZ // SOURCE_HZ) or
            not math.isfinite(rms) or rms >= 1e-5 or float(summary["threshold"]) != 1e-5 or
            int(summary["quiet_ms"]) != 100 or int(summary["timeout"]) != 0):
        raise ValueError("audio tail did not satisfy its measured completion contract")
    source_audio(path, frames)
    with wave.open(path, "rb") as stream:
        stream.setpos(max(0, frames - AUDIO_HZ // 10))
        pcm = array.array("h")
        pcm.frombytes(stream.readframes(AUDIO_HZ // 10))
    end_rms = math.sqrt(sum((x / 32767) ** 2 for x in pcm) / len(pcm))
    # The float mixer owns the threshold; the retained PCM is rounded to Int16.
    if end_rms > 1e-5 + 0.5 / 32767:
        raise ValueError("retained tail PCM did not end below its quantized quiet floor")
    if header["output_frames"] != video_frames * (AUDIO_HZ // SOURCE_HZ) + frames:
        raise ValueError("tail, picture and continuous mixer sample clocks disagree")
    return dict(frames=frames, seconds=frames / AUDIO_HZ, final_rms=rms, pcm_end_rms=end_rms)


def cues(path):
    with open(path) as stream:
        rows = [json.loads(line) for line in stream if line.strip()]
    if not rows or rows[0].get("schema") != "audio-cue-trace-v1":
        raise ValueError("missing audio cue census")
    header, rows = rows[0], rows[1:]
    if (header["sample_rate"] != AUDIO_HZ or header["count"] != len(rows) or
            header["overflow"] or header["bad_rate"] or header["unobserved_active"]):
        raise ValueError("incomplete or invalid audio cue census")
    total = header["output_frames"]
    if type(total) is not int or total <= 0:
        raise ValueError("cue census has no measured output sample extent")
    def sample(value):
        if value is not None and (type(value) is not int or not 0 <= value < total):
            raise ValueError("cue sample is outside the measured output")
        return value
    serials, witnesses = set(), []
    for row in rows:
        serial = row["serial"]
        if serial in serials:
            raise ValueError("duplicate cue identity")
        serials.add(serial)
        first = sample(row["first_source_sample"])
        dry = sample(row["first_nonzero_dry_sample"])
        if dry is not None and (first is None or dry < first):
            raise ValueError("dry cue precedes its measured source cursor")
        delta = row["script_tick"] - header["start_script_tick"]
        if row["source_tick"] - header["start_source_tick"] != delta + 1:
            raise ValueError("source and script cue clocks disagree")
        # Quiet/flood/capped outcomes remain recorded; required event identities
        # are selected by the source event census and cannot disappear here.
        if first is None:
            if any(mark["output_sample"] is not None for mark in row.get("milestones", [])):
                raise ValueError("unstarted cue fabricated a reload milestone")
            continue
        delay = row["device_delay_frames"]
        if delay is None or delta < 0:
            raise ValueError("cue onset has no measurable intended clock")
        expected = delta * (AUDIO_HZ // SOURCE_HZ) + delay
        residual = (first - expected) / AUDIO_HZ
        if abs(residual) > SOURCE_CUE_LIMIT + 1e-9:
            raise ValueError("source cue onset exceeds one simulation tick")
        milestones = []
        for mark in row.get("milestones", []):
            if mark["source_sample"] < 0 or row["rate"] <= 0 or not math.isfinite(row["rate"]):
                raise ValueError("invalid reload source milestone")
            observed = sample(mark["output_sample"])
            if observed is not None and observed < first:
                raise ValueError("reload milestone precedes its source cursor")
            target = first + mark["source_sample"] / row["rate"]
            residual = None if observed is None else (observed - target) / AUDIO_HZ
            if residual is not None and abs(residual) > SOURCE_CUE_LIMIT + 1e-9:
                raise ValueError("reload milestone cursor exceeds one source tick")
            milestones.append(dict(mark, expected_output_sample=target, residual_seconds=residual))
        witnesses.append(dict(row, milestones=milestones, expected_first_sample=expected,
                              source_residual_seconds=(first - expected) / AUDIO_HZ,
                              dry_seconds=None if dry is None else dry / AUDIO_HZ,
                              source_frame=delta))
    return header, rows, witnesses


def delay_witness(reference, decoded, at, width=0.18):
    """Measure the actual encoded waveform against its pre-AAC master.

    Search a wider window than the permitted drift, so an obviously late cue
    fails its measured residual instead of appearing to have zero displacement.
    """
    center = round(at * SYNC_HZ)
    start = max(0, center - round(0.015 * SYNC_HZ))
    stop = min(len(reference) // 2, center + round(width * SYNC_HZ))
    if stop - start < 45:
        raise ValueError("cue has too little decoded context")
    # Preserve stereo; choose the channel with the stronger measured witness.
    channel = max((0, 1), key=lambda ch: sum(reference[i * 2 + ch] ** 2 for i in range(start, stop)))
    pattern = reference[start * 2 + channel:stop * 2 + channel:2]
    mean = sum(pattern) / len(pattern)
    pattern = [x - mean for x in pattern]
    norm = sum(x * x for x in pattern)
    if norm < 1e-9:
        raise ValueError("cue is silent or unmeasurable in the edited master")
    best, best_lag = -1.0, None
    for lag in range(-round(0.10 * SYNC_HZ), round(0.10 * SYNC_HZ) + 1):
        a, b = start + lag, stop + lag
        if a < 0 or b > len(decoded) // 2:
            continue
        actual = decoded[a * 2 + channel:b * 2 + channel:2]
        avg = sum(actual) / len(actual)
        actual = [x - avg for x in actual]
        energy = sum(x * x for x in actual)
        if energy <= 1e-12:
            continue
        score = sum(x * y for x, y in zip(pattern, actual)) / math.sqrt(norm * energy)
        if score > best:
            best, best_lag = score, lag
    if best_lag is None or best < CORRELATION_MIN:
        raise ValueError("missing or unmeasurable encoded cue waveform")
    residual = best_lag / SYNC_HZ
    if abs(residual) > ENCODE_CUE_LIMIT + 1e-9:
        raise ValueError("encoded cue drift exceeds one delivered 60 FPS frame")
    return dict(correlation=best, additional_drift_seconds=residual)


def mp4(path, width, height, frames, fps, reference_path=None, witnesses=None, video_reference_path=None):
    metadata = probe(path)
    video = [s for s in metadata["streams"] if s["codec_type"] == "video"]
    sounds = [s for s in metadata["streams"] if s["codec_type"] == "audio"]
    if len(video) != 1 or len(sounds) != 1:
        raise ValueError("MP4 needs exactly one picture stream and one sound stream")
    video, sound = video[0], sounds[0]
    if (video["codec_name"] != "h264" or sound["codec_name"] != "aac" or
            (video["width"], video["height"]) != (width, height) or
            int(video["nb_read_frames"]) != frames or sound["channels"] != 2 or
            int(sound["sample_rate"]) != AUDIO_HZ):
        raise ValueError("decoded MP4 codec/dimensions/frames/stereo format mismatch")
    if (width, height, fps) != (1920, 1080, 60) or video.get("pix_fmt") != "yuv420p":
        raise ValueError("hero.mp4 must be compatible Full HD at 60 fps")
    if int(video.get("bit_rate", 0)) < 20_000_000 or int(sound.get("bit_rate", 0)) < 256_000:
        raise ValueError("hero.mp4 video or audio bitrate is below the delivery floor")
    result = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0",
                             "-show_frames", "-show_entries", "frame=best_effort_timestamp_time",
                             "-of", "json", path], capture_output=True, text=True, check=True)
    times = [float(f["best_effort_timestamp_time"]) for f in json.loads(result.stdout)["frames"]]
    if len(times) != frames or any(abs(t - i / fps) > 1e-4 for i, t in enumerate(times)):
        raise ValueError("decoded MP4 frame cadence differs from the edit grid")
    pcm = audio(path)
    seconds = len(pcm) / (2 * AUDIO_HZ)
    if abs(seconds - frames / fps) > 1024 / AUDIO_HZ + 1e-6:
        raise ValueError("decoded sound duration differs from picture")
    peak = max(abs(x) for x in pcm)
    rms = math.sqrt(sum(x * x for x in pcm) / len(pcm))
    if peak >= 0.99999 or rms <= 1e-7:
        raise ValueError("decoded sound clips or is empty")
    boxes = []
    with open(path, "rb") as stream:
        while len(boxes) < 32:
            header = stream.read(8)
            if len(header) != 8:
                break
            size, kind = int.from_bytes(header[:4], "big"), header[4:]
            head = 8
            if size == 1:
                size, head = int.from_bytes(stream.read(8), "big"), 16
            if size < head:
                break
            boxes.append(kind)
            if kind == b"mdat":
                break
            stream.seek(size - head, 1)
    if b"moov" not in boxes or b"mdat" not in boxes or boxes.index(b"moov") > boxes.index(b"mdat"):
        raise ValueError("MP4 is missing a leading faststart index")
    measured = []
    if reference_path is not None:
        if not witnesses:
            raise ValueError("final MP4 has no required audiovisual cue witnesses")
        reference, actual = audio(reference_path, SYNC_HZ), audio(path, SYNC_HZ)
        for witness in witnesses:
            measured.append(dict(witness, **delay_witness(reference, actual, witness["edited_seconds"])))
    if video_reference_path is not None:
        with open(video_reference_path, "rb") as stream:
            pictures = stream.read()
        actual_pictures = decode_video(path)
        if len(pictures) != frames * 160 * 90 or len(actual_pictures) != len(pictures):
            raise ValueError("pre-encode and decoded picture lengths differ")
        for row in measured:
            at = row.get("edited_event_seconds")
            if at is not None:
                row.update(visual_delay_witness(pictures, actual_pictures,
                                               min(frames - 1, max(0, round(at * fps)))))
    return dict(width=width, height=height, frames=frames, fps=fps,
                video_bitrate=int(video["bit_rate"]), audio_bitrate=int(sound["bit_rate"]),
                decoded_audio_seconds=seconds, decoded_padding_frames=round((seconds - frames / fps) * AUDIO_HZ),
                peak=peak, rms=rms,
                source_limit_seconds=SOURCE_CUE_LIMIT, encode_limit_seconds=ENCODE_CUE_LIMIT,
                cues=measured, faststart=True)


def decode_video(path, width=160, height=90):
    result = subprocess.run(["ffmpeg", "-v", "error", "-threads", "2", "-i", path,
                             "-map", "0:v:0", "-vf", "scale=%d:%d:flags=area,format=gray" % (width, height),
                             "-fps_mode", "passthrough", "-f", "rawvideo", "pipe:1"],
                            capture_output=True, check=True)
    if not result.stdout or len(result.stdout) % (width * height):
        raise ValueError("missing or truncated decoded picture witness")
    return result.stdout


def visual_delay_witness(reference, decoded, frame, stride=160 * 90):
    """Locate the actual delivered action frame among its nearby decoded frames."""
    count = len(reference) // stride
    if len(reference) != len(decoded) or not 0 <= frame < count:
        raise ValueError("picture witness length or source frame is invalid")
    def distance(left, right):
        return sum((a - b) ** 2 for a, b in zip(left, right)) / stride
    target = reference[frame * stride:(frame + 1) * stride]
    scores, variation = [], 0.0
    for lag in range(-3, 4):
        other = frame + lag
        if not 0 <= other < count:
            continue
        scores.append((distance(target, decoded[other * stride:(other + 1) * stride]), abs(lag), lag))
        variation = max(variation, distance(target, reference[other * stride:(other + 1) * stride]))
    if variation < 0.04:
        raise ValueError("action picture has no measurable temporal witness")
    error, _, lag = min(scores)
    if error > 36 or abs(lag) > 1:
        raise ValueError("action picture changed or drifted beyond one delivered frame")
    return dict(picture_drift_frames=lag, picture_rms_error=math.sqrt(error),
                picture_temporal_variation=variation)
