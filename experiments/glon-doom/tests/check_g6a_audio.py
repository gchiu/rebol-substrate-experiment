#!/usr/bin/env python3
"""Verify the G6a ambience assets are the supplied PCM loops, unchanged."""
import hashlib
import pathlib
import sys
import wave

ROOT = pathlib.Path(__file__).resolve().parent.parent
AUDIO = ROOT / "assets" / "audio"
EXPECTED = {
    "ship_ambience_loop.wav": {
        "sha256": "63107c5d14759bdebb66c402481d0b1017a650165d6649dd6c0ac03f22e588e4",
        "frames": 1058400,
        "duration": 24.0,
    },
    "ventilation_loop.wav": {
        "sha256": "158ba4c6c096af9ff28d9f4af10907f3f1c7aa20807f67aaa2ce62ef40c3ba6d",
        "frames": 529200,
        "duration": 12.0,
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
print("g6a audio: OK: two unchanged 44100 Hz mono s16le ambience loops")
