# Player-model debug captures

Run the complete deterministic screenshot set from the checkout root:

```sh
python3 screenshots/debug/capture.py
```

The script writes the complete PNG set directly beside this README and replaces captures with the
same name. It uses seed `1337`, a fresh config for every image, a frozen match,
settled pose transitions, software rendering, and a fixed open filming point.
`manifest.json` records the exact harness script and SHA-256 hashes for each
result. Generated PNGs and the manifest remain ignored by Git.

Names follow `pose_view_detail.png`. The set is intentionally compact:

- `stand_*` and `crouch_*`: neutral exterior views and views down the local body
- `ads_*`: AR/sniper first person plus standing/crouched exterior alignment
- `aim_sr_*_seq_*`: slow sniper ready/ADS sweeps through the 88.8-degree pitch limit
- `run_sr_seq_*`: dense low-ready sniper run start and settled gait
- `lean_*`: hip/ADS and left/right comparisons from one fixed camera
- `slide_*`: profile, three-quarter, first-person, six-frame get-up, and eight-frame
  front/back/left/right entry sequences
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
