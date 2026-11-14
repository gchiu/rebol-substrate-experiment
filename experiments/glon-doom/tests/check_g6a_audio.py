#!/usr/bin/env python3
"""Verify the G6a ambience assets are the supplied PCM loops, unchanged."""
import hashlib
import pathlib
import sys
import wave

ROOT = pathlib.Path(__file__).resolve().parent.parent
AUDIO = ROOT / "assets" / "audio"
EXPECTED = {
    "ambient_loop.wav": {
        "sha256": "679a60117ab498983a70afecabc1e38c5081c0678ee4a58cb05dd7496e9aa811",
        "frames": 1499403,
        "duration": 1499403 / 44100,
    },
}
for name, expected in EXPECTED.items():
    path = AUDIO / name
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != expected["sha256"]:
        sys.exit(f"{path}: source audio changed ({digest})")
    with wave.open(str(path), "rb") as w:
        if w.getnchannels() != 1 or w.getsampwidth() != 2 or w.getframerate() != 44100:
            sys.exit(f"{path}: expected mono 16-bit 44100 Hz, got "
                     f"{w.getnchannels()} ch / {w.getsampwidth() * 8} bit / "
                     f"{w.getframerate()} Hz")
        if w.getnframes() != expected["frames"]:
            sys.exit(f"{path}: expected {expected['frames']} frames, got {w.getnframes()}")
        duration = w.getnframes() / w.getframerate()
        if abs(duration - expected["duration"]) > 1e-9:
            sys.exit(f"{path}: expected {expected['duration']} s, got {duration}")
print("g6a audio: OK: Freesound 873687 44100 Hz mono s16le ambience loop")
