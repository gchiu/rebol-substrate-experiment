#!/bin/sh
# Usage: LLM_CMD='model-command' ./run_trial.sh TASK A|B|C OUTPUT
# LLM_CMD reads a prompt on stdin and writes one artifact to stdout.
# The harness allows one initial artifact and up to three repair attempts.
set -eu
if [ "$#" -ne 3 ]; then
    echo "usage: LLM_CMD='...' $0 TASK A|B|C OUTPUT" >&2
    exit 2
fi
task=$1; rep=$2; out=$3
if [ -z "${LLM_CMD:-}" ]; then
    echo "UNPERFORMED: no LLM_CMD set; controlled trials were not run" >&2
    exit 2
fi
case "$rep" in A|B|C) ;; *) echo "representation must be A, B or C" >&2; exit 2;; esac

prompt=$(mktemp)
trap 'rm -f "$prompt"' EXIT
cat "../tasks/$task.md" "prompts/$rep.md" > "$prompt"

attempt=0
while [ "$attempt" -le 3 ]; do
    artifact="${out}.attempt${attempt}"
    # shellcheck disable=SC2086
    $LLM_CMD < "$prompt" > "$artifact"
    if ../build/b4 --verify "$task" "$rep" "$artifact" > "${artifact}.test" 2>&1; then
        cp "$artifact" "$out"
        echo "PASS after $attempt repair attempt(s): $out"
        exit 0
    fi
    echo "attempt $attempt failed:" >&2
    cat "${artifact}.test" >&2
    {
        echo
        echo "The previous artifact failed:"
        cat "${artifact}.test"
        echo "The previous artifact was:"
        cat "$artifact"
        echo
        echo "Return a corrected artifact only."
    } >> "$prompt"
    attempt=$((attempt + 1))
done
echo "FAIL after three repair attempts; last artifact: ${out}.attempt3" >&2
exit 1
