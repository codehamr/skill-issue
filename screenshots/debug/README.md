# Player-model debug captures

Run the complete deterministic screenshot set from the checkout root:

```sh
python3 screenshots/debug/capture.py
```

The script writes 21 PNGs directly beside this README and replaces captures with the
same name. It uses seed `1337`, a fresh config for every image, a frozen match,
settled pose transitions, software rendering, and a fixed open filming point.
`manifest.json` records the exact harness script and SHA-256 hashes for each
result. Generated PNGs and the manifest remain ignored by Git.

Names follow `pose_view_detail.png`. The set is intentionally compact:

- `stand_*` and `crouch_*`: neutral exterior views and views down the local body
- `ads_*`: AR/sniper first person plus standing/crouched exterior alignment
- `lean_*`: hip/ADS and left/right comparisons from one fixed camera
- `slide_*`: profile, three-quarter, and first-person viewmodel pose
- `detail_*`: close hand/weapon views and a lit side view of stock contact

Capture only one area while iterating:

```sh
python3 screenshots/debug/capture.py --group lean
python3 screenshots/debug/capture.py --group ads --group slide
python3 screenshots/debug/capture.py --group detail
python3 screenshots/debug/capture.py --list
```

Use `--game`, `--seed`, `--width`, or `--height` only when deliberately making
a separate comparison. Keep those values identical for before/after reviews.
