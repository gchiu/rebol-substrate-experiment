#!/bin/sh
# B5 LLM_CMD adapter for OpenCode 1.18.35.
# Reads the prompt on stdin and writes only the model's artifact on stdout.
# Preserves raw JSON events and appends usage/cost metadata.
set -u

BASE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
MODEL=${OPENCODE_MODEL:-deepseek/deepseek-flash}
TRIAL_ID=${B5_TRIAL_ID:-manual}
RAW_DIR=${B5_RAW_DIR:-$(mktemp -d /tmp/opencode/b5-raw.XXXXXX)}
META_LOG=${B5_META_LOG:-$RAW_DIR/meta.jsonl}
mkdir -p "$RAW_DIR"

PROMPT=$(mktemp)
trap 'rm -f "$PROMPT"' EXIT
cat > "$PROMPT"

PROJECT=$(mktemp -d /tmp/opencode/b5-project.XXXXXX)
cat > "$PROJECT/opencode.jsonc" <<'EOF'
{
  "$schema": "https://opencode.ai/config.json",
  "agent": {
    "b5-solver": {
      "description": "Tool-free solver for isolated B5 trials",
      "mode": "primary",
      "prompt": "You are a program-generation assistant. Return only the requested artifact. Do not use tools. Do not include explanations or Markdown fences.",
      "tools": { "*": false },
      "permission": { "*": "deny" }
    }
  }
}
EOF

STAMP=$(date +%s%N)
RAW="$RAW_DIR/raw-$STAMP.json"
ERR="$RAW_DIR/raw-$STAMP.err"
START=$(date +%s.%N)
TIMEOUT=${B5_OPENCODE_TIMEOUT:-900}
timeout "$TIMEOUT" opencode run \
  --dir "$PROJECT" \
  --agent b5-solver \
  --model "$MODEL" \
  --format json \
  --pure \
  "$(cat "$PROMPT")" > "$RAW" 2> "$ERR"
RC=$?
END=$(date +%s.%N)
if [ "$RC" -eq 124 ]; then
  echo "ADAPTER_TIMEOUT timeout=${TIMEOUT}s" >> "$ERR"
fi
export B5_OPENCODE_TIMEOUT="$TIMEOUT"

# Always return 0 so B5's repair loop can consume empty/failed artifacts and
# feed verification diagnostics back to the next fresh session.
python3 "$BASE/generation/opencode_extract.py" \
  "$RAW" "$META_LOG" "$MODEL" "$START" "$END" "$TRIAL_ID" "$RAW" "$ERR"
exit 0
