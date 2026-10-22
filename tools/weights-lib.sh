# Sourced by get-weights.sh and get-more-weights.sh: download from a pinned Hugging Face revision, check size and sha256.
# tools/weights.sha256 lists "sha256 size repo revision path"; a file that fails the check is deleted and fetched again once.
MANIFEST="$(dirname "${BASH_SOURCE[0]}")/weights.sha256"
sha_of() { if command -v sha256sum > /dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }
# fetch <repo> <path-in-repo> <destination>
fetch() {
  local repo=$1 path=$2 dest=$3 line sha size rev try
  line=$(tr -d '\015' < "$MANIFEST" | awk -v r="$repo" -v p="$path" '$1 !~ /^#/ && $3 == r && $5 == p {print $1, $2, $4}')
  [ -n "$line" ] || { echo "no pinned entry for $repo/$path in $MANIFEST" >&2; return 1; }
  read -r sha size rev <<< "$line"
  for try in 1 2; do
    if [ -f "$dest" ] && [ "$(stat -c %s "$dest" 2> /dev/null || stat -f %z "$dest")" = "$size" ] && [ "$(sha_of "$dest")" = "$sha" ]; then
      echo "ok $dest"; return 0
    fi
    [ "$try" = 2 ] && rm -f "$dest"   # second pass: a bad file is not resumed
    until curl -sS -L --fail -C - -o "$dest" "https://huggingface.co/$repo/resolve/$rev/$path"; do echo "retry $dest"; sleep 5; done
  done
  [ "$(sha_of "$dest")" = "$sha" ] && { echo "ok $dest"; return 0; }
  echo "CHECKSUM MISMATCH: $dest" >&2; return 1
}
