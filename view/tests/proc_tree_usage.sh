#!/bin/sh
# proc_tree_usage.sh PID [SECONDS] -- the processes of a running program and
# all its descendants (e.g. the browser demo and WebKit's helper
# processes): each one's resident memory, the totals, and the CPU they used
# over SECONDS (default 10).  Reads /proc only; sends nothing to the program.
#
#   view/tests/proc_tree_usage.sh "$(pgrep -f 'intent_browser.glon' | head -1)"
set -u
root=${1:?usage: proc_tree_usage.sh PID [SECONDS]}
secs=${2:-10}
tree() {
    echo "$root"
    todo=$root
    while [ -n "$todo" ]; do
        next=""
        for p in $todo; do
            for c in $(cat /proc/"$p"/task/*/children 2>/dev/null); do echo "$c"; next="$next $c"; done
        done
        todo=$next
    done
}
ticks() {
    t=0
    for p in $(tree); do
        s=$(sed 's/^.*) //' /proc/"$p"/stat 2>/dev/null) || continue
        set -- $s
        t=$((t + ${12} + ${13}))
    done
    echo $t
}
rss_total=0; pss_total=0; n=0
printf '%-8s %-24s %10s %10s\n' PID NAME RSS_kB PSS_kB
for p in $(tree); do
    name=$(cat /proc/"$p"/comm 2>/dev/null) || continue
    rss=$(awk '/^VmRSS:/ {print $2}' /proc/"$p"/status 2>/dev/null); rss=${rss:-0}
    pss=$(awk '/^Pss:/ {print $2}' /proc/"$p"/smaps_rollup 2>/dev/null); pss=${pss:-0}
    printf '%-8s %-24s %10s %10s\n' "$p" "$name" "$rss" "$pss"
    rss_total=$((rss_total + rss)); pss_total=$((pss_total + pss)); n=$((n + 1))
done
echo "processes: $n   RSS total: ${rss_total} kB   PSS total: ${pss_total} kB (PSS shares common pages fairly)"
t0=$(ticks); sleep "$secs"; t1=$(ticks)
hz=$(getconf CLK_TCK)
echo "CPU over ${secs} s: $(( (t1 - t0) * 1000 / (secs * hz) / 10 )).$(( (t1 - t0) * 1000 / (secs * hz) % 10 ))% of one core"
