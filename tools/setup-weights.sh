#!/usr/bin/env bash
# One command: download the model weights (pinned, checked) and copy them to the console over FTP.
#   ./tools/setup-weights.sh <console-ip> [--set q4|q8] [--pixal3d] [--no-birefnet] [--download-only] [--upload-only] [--only <file.gguf>] [--accept-licenses]
# Safe to re-run after any interruption: finished downloads and files already on the console (same size) are skipped.
# Writes only into models/ on this PC and /data/PS5MG/models/<set>/ on the console; never deletes anything on the console.
set -u
cd "$(dirname "$0")/.." || exit 1
. tools/weights-lib.sh
die() { echo "ERROR: $*" >&2; exit 1; }
usage() { sed -n '2,3p' "$0" | sed 's/^# *//'; exit 2; }

ip= set=q4 pixal=0 birefnet=1 dl=1 ul=1 only= acc=
while [ $# -gt 0 ]; do
  case $1 in
    --set) set=${2:-}; shift ;;
    --pixal3d) pixal=1 ;;
    --no-birefnet) birefnet=0 ;;
    --download-only) ul=0 ;;
    --upload-only) dl=0 ;;
    --only) only=${2:-}; shift ;;
    --accept-licenses) acc=--accept-licenses ;;
    -h|--help) usage ;;
    -*) echo "Unknown option $1" >&2; usage ;;
    *) ip=$1 ;;
  esac
  shift
done
[ -n "$ip" ] || usage
[ "$dl$ul" = 00 ] && die "--download-only and --upload-only together do nothing"
case $set in q4|q8) ;; *) die "--set must be q4 or q8 (got '$set')" ;; esac
case $ip in *[!A-Za-z0-9.:-]*|"") die "that does not look like a console address: $ip" ;; esac

# The file list comes only from the manifest: "repo|path-in-repo|name|sha|size|rev".
list=$(tr -d '\015' < "$MANIFEST" | awk -v s="$set" -v px=$pixal -v bi=$birefnet '
  $1 ~ /^#/ || NF < 5 {next}
  $3 == "ilintar/trellis2-gguf" && index($5, s "/") == 1 { n = substr($5, length(s) + 2); if (n == "birefnet.gguf" && !bi) next }
  $3 == "vegax87/Pixal3D" { if (!px) next; n = $5 }
  $3 != "ilintar/trellis2-gguf" && $3 != "vegax87/Pixal3D" {next}
  n != "" { print $3 "|" $5 "|" n "|" $1 "|" $2 "|" $4; n = "" }')
if [ -n "$only" ]; then
  list=$(printf '%s\n' "$list" | awk -F'|' -v o="$only" '$3 == o')
  [ -n "$list" ] || die "'$only' is not a file of this set (add --pixal3d for Pixal3D files, and check the spelling)"
fi
total=$(printf '%s\n' "$list" | wc -l | tr -d ' ')
dir=models/$set
mb() { echo $(( ($1 + 500000) / 1000000 )); }   # decimal MB, like Hugging Face and the README
fsize() { stat -c %s "$1" 2> /dev/null || stat -f %z "$1" 2> /dev/null || echo -1; }

if [ $dl = 1 ]; then
  accept_licenses $acc || exit 1
  mkdir -p "$dir" || die "cannot create $dir"
  # Check what is already correct first, so we know how much space is needed.
  need=0
  while IFS='|' read -r repo path name sha size rev; do
    [ "$(fsize "$dir/$name")" = "$size" ] && continue
    have=$(fsize "$dir/$name"); [ "$have" -gt 0 ] 2> /dev/null && [ "$have" -lt "$size" ] && size=$((size - have))
    need=$((need + size))
  done <<< "$list"
  free=$(df -Pk "$dir" | awk 'NR==2 {print int($4 / 1024)}')
  [ "$free" -ge $(( need / 1048576 + 100 )) ] 2> /dev/null || die "not enough disk space on this PC: need about $(mb $need) MB more, only ${free:-?} MB free"
  n=0
  while IFS='|' read -r repo path name sha size rev; do
    n=$((n + 1)); f=$dir/$name; tag="[$n/$total] $name ($(mb "$size") MB)"
    for try in 1 2; do
      if [ "$(fsize "$f")" = "$size" ]; then
        echo "$tag: checking ..."
        [ "$(sha_of "$f")" = "$sha" ] && { echo "$tag: already there and correct"; continue 2; }
        echo "$tag: damaged, downloading it again"; rm -f "$f"
      fi
      [ "$(fsize "$f")" -gt "$size" ] && rm -f "$f"
      echo "$tag: downloading (you can stop with Ctrl+C and run the same command again to continue)"
      for k in 1 2 3; do
        curl -L --fail -C - -# --connect-timeout 15 -o "$f" "https://huggingface.co/$repo/resolve/$rev/$path" && break
        [ "$k" = 3 ] && die "cannot download from Hugging Face. Is the internet connection working? Run the command again to continue."
        sleep 3
      done
    done
    die "$name still does not match its SHA-256 after a second download - not using it"
  done <<< "$list"
fi

if [ $ul = 1 ]; then
  base=ftp://$ip:2121/data/PS5MG/models/$set
  curl -s --connect-timeout 5 -o /dev/null "ftp://$ip:2121/" || die "cannot reach the console at $ip port 2121. Is the FTP server running on the console? Is the address right?"
  echo "Copying to the console. A file that was only half copied is replaced when you run this again."
  n=0
  while IFS='|' read -r repo path name sha size rev; do
    n=$((n + 1)); f=$dir/$name; tag="[$n/$total] $name ($(mb "$size") MB)"
    [ "$(fsize "$f")" = "$size" ] || die "$f is missing or incomplete on this PC - run without --upload-only first"
    remote=$(curl -sI --connect-timeout 5 "$base/$name" | tr -d '\015' | awk -F': ' 'tolower($1) == "content-length" {print $2}')
    [ "$remote" = "$size" ] && { echo "$tag: already on the console with the same size, skipped"; continue; }
    echo "$tag: copying to the console ..."
    curl -# --connect-timeout 5 --ftp-create-dirs -T "$f" "$base/$name" -o /dev/null || die "copy of $name failed. Is the FTP server running on the console? Run the command again to continue."
    remote=$(curl -sI --connect-timeout 5 "$base/$name" | tr -d '\015' | awk -F': ' 'tolower($1) == "content-length" {print $2}')
    [ "$remote" = "$size" ] || die "$name on the console has size ${remote:-?}, expected $size - run the command again"
    echo "$tag: copied"
  done <<< "$list"
fi
if [ "$ul" = 0 ]; then echo "Done. The files are in $dir on this PC. Run the command again without --download-only to copy them to the console."
else echo "Done. Open http://$ip:7870 in a browser and drop a picture."; fi
