#!/usr/bin/env bash
# Drives PS5MG on the console (FTP 2121, elfldr 9021).
#   mg.sh put <local> <remote-under-/data/PS5MG>   upload a file
#   mg.sh job <name> "<trellis-cli args>"          queue a job
#   mg.sh state <name>                             queued|running|done|failed|none
#   mg.sh wait <name> [max-seconds]                wait for it; fetches out/ps5_<name>.glb and .log
#   mg.sh log                                      the console's app.log
#   mg.sh alive                                    title running? (log changed in the last 3 minutes and no 'worker ends')
#   mg.sh launch                                   start the title (launch payload for PPSA99577)
#   mg.sh quit                                     ask the title to end itself
#   mg.sh setkey <key>                             protect the web interface and API with a key (empty: open again)
set -u
H=${PS5_HOST:?set PS5_HOST to the console address}
F=ftp://$H:2121/data/PS5MG
here=$(cd "$(dirname "$0")/.." && pwd)
case ${1:-} in
  put) curl -sS --ftp-create-dirs -T "$2" "$F/$3" ;;
  job) printf '%s\n' "$3" > /tmp/mg-$2.job
       # uploaded under another name and renamed: the title never sees a half-written job file
       curl -sS --ftp-create-dirs -T /tmp/mg-$2.job "$F/jobs/$2.job.tmp" &&
       curl -sS "ftp://$H:2121/" -Q "RNFR /data/PS5MG/jobs/$2.job.tmp" -Q "RNTO /data/PS5MG/jobs/$2.job" -o /dev/null && echo "queued $2" ;;
  state)
    l=$(curl -sS "$F/jobs/" 2>/dev/null | awk '{print $NF}' | grep "^$2\." | grep -v -E "\.(meta|result)$" | head -1)
    case $l in *.job) echo queued ;; *.running) echo running ;; *.done) echo done ;; *.failed) echo failed ;; *.cancelled) echo cancelled ;; *) echo none ;; esac ;;
  wait)
    max=${3:-3600}; t=0
    while [ $t -lt "$max" ]; do
      s=$("$0" state "$2"); [ "$s" = done ] || [ "$s" = failed ] && break
      sleep 15; t=$((t+15))
    done
    echo "$2: $s"
    curl -sS "$F/app.log" -o "$here/out/ps5_$2.log"
    [ "$s" = done ] && curl -sS "$F/out/$2.glb" -o "$here/out/ps5_$2.glb" ;;
  log) curl -sS "$F/app.log" ;;
  alive)
    log=$(curl -sS "$F/app.log"); echo "$log" | tail -1 | cut -c1-120
    echo "$log" | grep -q "worker ends" && echo "ENDED" || echo "no end marker" ;;
  launch) PS5_HOST=$H PS5_PORT=9021 bash "$here/third_party/PS5SD/scripts/send-elf.sh" "${MG_LAUNCH_ELF:-$here/build/tools/launch-mg.elf}" ;;
  quit) : > /tmp/mg-quit; curl -sS -T /tmp/mg-quit "$F/quit" && echo "quit sent" ;;
  setkey) echo "${2:-}" > /tmp/mg-key; curl -sS --ftp-create-dirs -T /tmp/mg-key "$F/api-key.txt" && echo "key set; the title reads it on every request" ;;
  *) sed -n 2,12p "$0" ;;
esac
