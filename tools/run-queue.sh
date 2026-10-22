#!/usr/bin/env bash
# Runs a list of jobs on the console one after the other and fetches each result.
#   tools/run-queue.sh jobs.txt            lines: <name>|<trellis-cli arguments>   (# and blank lines skipped)
# The title starts over after every job (mg_main.cpp), so the next job meets a fresh process. When the web
# interface does not answer after a job, the launch payload starts the title again.
set -u
here=$(cd "$(dirname "$0")/.." && pwd)
H=${PS5_HOST:?set PS5_HOST to the console address}
F=ftp://$H:2121/data/PS5MG
mkdir -p "$here/out" "$here/compare"
results=$here/out/queue_results.tsv

alive() { curl -s -m 4 "http://$H:7870/api/status" > /dev/null; }

ensure_title() {
  local tries=0
  until alive; do
    tries=$((tries + 1))
    [ $tries -gt 4 ] && { echo "the title does not come up"; return 1; }
    echo "  (starting the title, try $tries)"
    "$here/tools/mg.sh" launch > /dev/null 2>&1
    for _ in $(seq 1 12); do sleep 5; alive && return 0; done
  done
}

while IFS='|' read -r name args; do
  case $name in ''|\#*) continue ;; esac
  echo "== $name  $(date +%H:%M:%S)"
  ensure_title || { echo "$name	skipped (no title)" >> "$results"; continue; }
  "$here/tools/mg.sh" job "$name" "$args" > /dev/null
  s=queued
  while [ "$s" != done ] && [ "$s" != failed ]; do
    sleep 15
    s=$("$here/tools/mg.sh" state "$name")
    # a title that went away (crash, console) is started again; the job file stays .running then
    if [ "$s" = running ] && ! alive; then ensure_title > /dev/null 2>&1; fi
  done
  secs=$(curl -sS "$F/jobs/$name.result" 2>/dev/null | awk '{print $2}')
  curl -sS "$F/logs/$name.log" -o "$here/out/ps5_$name.log" 2>/dev/null
  [ "$s" = done ] && curl -sS "$F/out/$name.glb" -o "$here/out/ps5_$name.glb" 2>/dev/null
  echo "$name	$s	${secs:-?}" >> "$results"
  echo "   $s in ${secs:-?} s"
done < "$1"
echo QUEUE-DONE
