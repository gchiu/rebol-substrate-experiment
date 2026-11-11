#!/usr/bin/env python3
"""Extract assistant text from OpenCode JSON events and append trial metadata.

Usage: opencode_extract.py RAW_JSON META_JSONL MODEL START END TRIAL_ID RAW_PATH ERR_PATH
Writes the artifact text to stdout. Metadata is a JSON line appended to META_JSONL.
"""
import json
import os
import sys
import time

raw, meta, model, start, end, trial_id, raw_path, err_path = sys.argv[1:]
start = float(start)
end = float(end)
texts = []
sessions = []
steps = []
tokens = {"input": 0, "output": 0, "reasoning": 0, "total": 0,
          "cache_read": 0, "cache_write": 0}
cost = 0.0
errors = []
try:
    with open(raw, "r", errors="replace") as f:
        for line in f:
            try:
                e = json.loads(line)
            except Exception:
                continue
            sid = e.get("sessionID")
            if sid and sid not in sessions:
                sessions.append(sid)
            typ = e.get("type")
            part = e.get("part", {})
            if typ == "text":
                texts.append(part.get("text", ""))
            elif typ == "step_start":
                steps.append(("start", e.get("timestamp")))
            elif typ == "step_finish":
                steps.append(("finish", e.get("timestamp")))
                tk = part.get("tokens", {})
                for k in ("input", "output", "reasoning", "total"):
                    tokens[k] += int(tk.get(k, 0) or 0)
                cache = tk.get("cache", {}) or {}
                tokens["cache_read"] += int(cache.get("read", 0) or 0)
                tokens["cache_write"] += int(cache.get("write", 0) or 0)
                cost += float(part.get("cost", 0) or 0)
            elif typ == "error":
                errors.append(str(e.get("error", e))[:1000])
except FileNotFoundError:
    errors.append("raw JSON log missing")

starts = [t for k, t in steps if k == "start" and isinstance(t, (int, float))]
finishes = [t for k, t in steps if k == "finish" and isinstance(t, (int, float))]
model_step_s = (max(finishes) - min(starts)) / 1000.0 if starts and finishes else 0.0

try:
    err_text = open(err_path, "r", errors="replace").read()[-2000:]
except FileNotFoundError:
    err_text = ""
if err_text.strip():
    errors.append(err_text.strip())
adapter_timeout = "ADAPTER_TIMEOUT" in err_text or any("ADAPTER_TIMEOUT" in e for e in errors)
timeout_s = os.environ.get("B5_OPENCODE_TIMEOUT", "")
try:
    timeout_s = int(timeout_s) if timeout_s else None
except ValueError:
    timeout_s = None

record = {
    "trial_id": trial_id,
    "model": model,
    "sessions": sessions,
    "tokens": tokens,
    "cost": cost,
    "model_step_seconds": round(model_step_s, 6),
    "process_seconds": round(end - start, 6),
    "text_parts": len(texts),
    "adapter_timeout": adapter_timeout,
    "adapter_timeout_seconds": timeout_s,
    "raw_json": raw_path,
    "error_log": err_path,
    "errors": errors,
    "recorded_at": time.time(),
}
with open(meta, "a") as f:
    f.write(json.dumps(record) + "\n")
sys.stdout.write("\n".join(texts))
