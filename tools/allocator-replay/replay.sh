#!/usr/bin/env bash
# Allocator replay: drive a containerized sipi with a fixed JP2 decode load and
# record container RSS over time. The per-minute RSS *floor* (lowest sample per
# minute) tracks what the allocator refuses to give back; peaks are legitimate
# in-flight decodes. A ratcheting floor under a load whose live data returns to
# zero between requests is allocator retention (or a leak — vary ONLY the
# allocator to tell them apart). Method, baselines, and interpretation:
# docs/src/development/allocator-replay.md.
#
# Usage: replay.sh <name> <duration-s> [docker args...]
#   name         run label; writes <name>.csv, <name>.errors into $PWD
#   duration-s   e.g. 900 (15 min is enough to show or rule out a ratchet)
#   docker args  appended to `docker run` — allocator/env variants, e.g.
#                -e MALLOC_ARENA_MAX=2  or  -e LD_PRELOAD=/opt/lib/libx.so
# Env:
#   IMAGE   image under test        (default daschswiss/sipi:latest)
#   CORPUS  corpus dir              (default: generated via make-corpus.sh
#                                    into <repo>/bazel-bin/allocator-corpus)
#   WORKERS concurrent clients      (default 6)
#   MEMLIM  container memory limit  (default 6g)
set -euo pipefail

NAME="${1:?usage: replay.sh <name> <duration-s> [docker args...]}"
DURATION="${2:?usage: replay.sh <name> <duration-s> [docker args...]}"
shift 2

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
IMAGE="${IMAGE:-daschswiss/sipi:latest}"
WORKERS="${WORKERS:-6}"
MEMLIM="${MEMLIM:-6g}"
CORPUS="${CORPUS:-$REPO/bazel-bin/allocator-corpus}"

[ -f "$CORPUS/big12k.jp2" ] || "$REPO/tools/allocator-replay/make-corpus.sh" "$CORPUS"

CID=$(docker run -d \
  -m "$MEMLIM" --cpus 6 \
  -e SIPI_NTHREADS=8 \
  "$@" \
  -v "$REPO/config:/sipi/config" \
  -v "$REPO/test/_test_data/images:/sipi/images" \
  -v "$CORPUS:/sipi/images/big" \
  -v "$REPO/scripts:/sipi/scripts" \
  -v "$REPO/server:/sipi/server" \
  -p 0:1024 \
  "$IMAGE")
trap 'docker rm -f "$CID" >/dev/null 2>&1 || true' EXIT

PORT=$(docker port "$CID" 1024/tcp | grep '0.0.0.0' | head -1 | rev | cut -d: -f1 | rev)
BASE="http://127.0.0.1:$PORT"

for i in $(seq 1 60); do
  curl -sf -o /dev/null "$BASE/health" 2>/dev/null && break
  sleep 1
  [ "$i" = 60 ] && { echo "server never became ready" >&2; docker logs "$CID" | tail -20 >&2; exit 1; }
done
echo "[$NAME] $IMAGE ready on :$PORT (mem limit $MEMLIM, $WORKERS workers)"

# JP2-only mix, big images weighted: prod serves exclusively J2K, and the
# per-request Kakadu worker-thread pool is the churn under test. The worker
# index arithmetic is deterministic, so every run replays the same sequence.
IMAGES=(
  "big/big8k.jp2" "big/big4k.jp2" "big/big12k.jp2" "big/big6k.jp2"
  "big/big8k.jp2" "unit/cmyk_lossy.jp2" "big/big4k.jp2" "unit/lena512.jp2"
)
SIZES=("max" "pct:75" "pct:50" "pct:25" "!1024,1024" "!3000,3000")
ROTS=("0" "90" "180" "270")
QUALS=("default.jpg" "default.png" "gray.jpg")

REQDIR=$(mktemp -d)
worker() {
  local w="$1" n=0 e=0
  while [ -f "$REQDIR/run" ]; do
    local img=${IMAGES[$(( (n + w) % ${#IMAGES[@]} ))]}
    local size=${SIZES[$(( (n / 3 + w) % ${#SIZES[@]} ))]}
    local rot=${ROTS[$(( (n / 7 + w) % ${#ROTS[@]} ))]}
    local qual=${QUALS[$(( (n / 5 + w) % ${#QUALS[@]} ))]}
    local url="$BASE/$img/full/$size/$rot/$qual"
    local code
    code=$(curl -s -o /dev/null -w '%{http_code}' --max-time 180 "$url" || echo "000")
    if [ "$code" != "200" ]; then
      e=$((e+1))
      echo "$code $url" >> "$NAME.errors"
    fi
    n=$((n+1))
    echo "$n $e" > "$REQDIR/w$w"
  done
}

touch "$REQDIR/run"
for w in $(seq 1 "$WORKERS"); do worker "$w" & done

CSV="$NAME.csv"
echo "elapsed_s,mem,req_total,err_total" > "$CSV"
START=$(date +%s)
while :; do
  NOW=$(date +%s); EL=$((NOW-START))
  [ "$EL" -ge "$DURATION" ] && break
  MEM=$(docker inspect --format '{{.State.Status}}' "$CID" 2>/dev/null | grep -q running \
    && docker stats --no-stream --format '{{.MemUsage}}' "$CID" | awk -F/ '{print $1}' | sed 's/ //g' \
    || echo "DEAD")
  REQ=$(cat "$REQDIR"/w* 2>/dev/null | awk '{r+=$1; e+=$2} END {print r","e}')
  echo "$EL,$MEM,$REQ" >> "$CSV"
  if [ "$MEM" = "DEAD" ]; then
    echo "[$NAME] container died at ${EL}s:" >&2
    docker inspect --format 'oom={{.State.OOMKilled}} exit={{.State.ExitCode}}' "$CID" >&2
    break
  fi
done

rm -f "$REQDIR/run"; sleep 2
wait 2>/dev/null || true
rm -rf "$REQDIR"

# Per-minute floors + least-squares slope (skipping warm-up minute 0). A slope
# within ±0.5 GiB/h of zero over 15 min is flat; the 2026-07-29 glibc failure
# baseline is +2.5 GiB/h at this load.
awk -F, 'NR>1 && $2!="DEAD" {
  v=$2
  if (v ~ /GiB/) { sub(/GiB/,"",v); v*=1024 } else { sub(/MiB/,"",v) }
  m=int($1/60)
  if (!(m in min) || v+0<min[m]) min[m]=v+0
  last=m; req[m]=$3; err[m]=$4
} END {
  n=0; sx=0; sy=0; sxx=0; sxy=0
  for (m=0;m<=last;m++) if (m in min) {
    printf "  min %2d: floor=%5.0f MiB  reqs=%s errs=%s\n", m, min[m], req[m], err[m]
    if (m>0) { n++; sx+=m; sy+=min[m]; sxx+=m*m; sxy+=m*min[m] }
  }
  if (n>1) {
    slope=(n*sxy-sx*sy)/(n*sxx-sx*sx)
    printf "[%s] floor slope: %+.1f MiB/min (%+.2f GiB/h) over %d min\n", n_, slope, slope*60/1024, n
  }
}' n_="$NAME" "$CSV"
