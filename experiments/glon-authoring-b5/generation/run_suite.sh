#!/bin/sh
# Usage: LLM_CMD='model-command' ./run_suite.sh OUTPUT_DIR
# Shuffles all task x representation pairs and records one log per trial.
set -eu
if [ "$#" -ne 1 ]; then
    echo "usage: LLM_CMD='...' $0 OUTPUT_DIR" >&2
    exit 2
fi
if [ -z "${LLM_CMD:-}" ]; then
    echo "UNPERFORMED: no LLM_CMD set; no productivity trial was run" >&2
    exit 2
fi
outdir=$1
mkdir -p "$outdir"
tasks="tri pow2 min3 clamp sign_neg range_sum choose is_even ack quad median3 max_neg"
pairs=$(mktemp)
trap 'rm -f "$pairs"' EXIT
for t in $tasks; do
    for r in A B C; do echo "$t $r"; done
done > "$pairs"
# Randomise order to reduce order effects.
shuf "$pairs" > "${pairs}.shuffled"
while read -r task rep; do
    log="$outdir/${task}.${rep}.log"
    if ./run_trial.sh "$task" "$rep" "$outdir/${task}.${rep}.artifact" > "$log" 2>&1; then
        echo "PASS $task $rep" >> "$outdir/summary.txt"
    else
        echo "FAIL $task $rep" >> "$outdir/summary.txt"
    fi
done < "${pairs}.shuffled"
rm -f "${pairs}.shuffled"
