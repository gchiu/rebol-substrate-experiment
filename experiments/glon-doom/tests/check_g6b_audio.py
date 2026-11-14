#!/usr/bin/env python3
"""Verify G6b event assets and the retained G6a ambience."""
import hashlib
import pathlib
import sys
import wave

ROOT = pathlib.Path(__file__).resolve().parent.parent
AUDIO = ROOT / "assets" / "audio"
AMBIENT = {
    "ambient_loop.wav": {
        "sha256": "679a60117ab498983a70afecabc1e38c5081c0678ee4a58cb05dd7496e9aa811",
        "frames": 1499403,
    }
}
SFX = {
    "door_open.wav": {
        "sha256": "970db0d94206122a2fa7e53bd8d4f16988e17eb4e60aaa2646d5e156e8444b41",
        "frames": 97020,
    },
    "door_close.wav": {
        "sha256": "0c237a177b1090eacd92b78f84cc8548d33f24e032f6066202b706d9e50ad255",
        "frames": 52920,
    },
    "alien_shriek.wav": {
        "sha256": "e91e1360ad94121382926bcb1be09c8513527cc0b519fc0a4ff5ab828a5fb7c2",
        "frames": 93347,
    },
    "rifle_shot.wav": {
        "sha256": "b40e41f7abe3bfbe016f5117bc64a6fc20482b0b77c67387c8ab279a8ba74d8f",
        "frames": 28665,
    },
    "wall_thud.wav": {
        "sha256": "a4ca3b8f0eca6503ff6c7b99f044ec7eca9fc6cfe0be091252ec488247e8321e",
        "frames": 52920,
    },
    "cargo_impact.wav": {
        "sha256": "2edafcdcab43834c4c5a341f8b8762c9b33c1932106689f43718912a8de2f274",
        "frames": 37044,
    },
}

def check(path, expected):
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != expected["sha256"]:
        sys.exit(f"{path}: expected asset changed ({digest})")
    with wave.open(str(path), "rb") as w:
        if w.getnchannels() != 1 or w.getsampwidth() != 2 or w.getframerate() != 44100:
            sys.exit(f"{path}: expected mono 16-bit 44100 Hz")
        if w.getnframes() != expected["frames"]:
            sys.exit(f"{path}: expected {expected['frames']} frames, got {w.getnframes()}")

for name, expected in AMBIENT.items():
    check(AUDIO / name, expected)
for name, expected in SFX.items():
    check(AUDIO / "sfx" / name, expected)

licence = (AUDIO / "sfx" / "SOURCE.txt").read_text()
for required in ("CC BY 4.0", "F.M.Audio", "Tommaso Motteran", "873687", "620830"):
    if required not in licence:
        sys.exit(f"missing licence/provenance text: {required}")
print("g6b audio: OK: ambience retained, six converted SFX assets, licences recorded")
