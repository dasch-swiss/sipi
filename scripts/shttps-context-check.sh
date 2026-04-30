#!/usr/bin/env bash
# Enforce the shttps → sipi one-way dependency direction.
# See sipi/shttps/CONTEXT.md and docs/adr/0001-shttps-as-strangler-fig-target.md.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

# Allowlist: each entry is "file|symbol" and must carry an inline TODO comment
# naming the follow-up that retires it. Currently empty — the boundary is
# clean. Add new entries only with a tracked Linear issue for retirement.
ALLOWLIST=()

# grep flags shared by both passes. Vendored third-party sources are excluded.
GREP_FLAGS=(
    --include='*.cpp'
    --include='*.cc'
    --include='*.c'
    --include='*.h'
    --include='*.hpp'
    --exclude='sole.hpp'
    --exclude='jwt.c'
    --exclude='jwt.h'
)

# is_allowed FILE CONTENT — returns 0 when an allowlist entry suppresses the line.
# Word-boundary check uses POSIX bracket expressions so the script runs on any
# grep (GNU, BSD, busybox/toybox).
is_allowed() {
    local file="$1" content="$2" entry allow_file allow_symbol
    for entry in "${ALLOWLIST[@]}"; do
        allow_file="${entry%%|*}"
        allow_symbol="${entry#*|}"
        if [ "$file" = "$allow_file" ] \
           && grep -qE "(^|[^A-Za-z0-9_])${allow_symbol}([^A-Za-z0-9_]|$)" <<<"$content"; then
            return 0
        fi
    done
    return 1
}

# run_pass KIND PATTERN — prints "file:line: kind: content" for each unsuppressed match.
run_pass() {
    local kind="$1" pattern="$2"
    local raw entry file rest line content
    raw=$(grep -rEn "${GREP_FLAGS[@]}" "$pattern" shttps/ || true)
    if [ -z "$raw" ]; then
        return 0
    fi
    while IFS= read -r entry; do
        file="${entry%%:*}"
        rest="${entry#*:}"
        line="${rest%%:*}"
        content="${rest#*:}"
        if is_allowed "$file" "$content"; then
            continue
        fi
        printf '%s:%s: %s: %s\n' "$file" "$line" "$kind" "$content"
    done <<<"$raw"
}

violations=$( {
    run_pass include '^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"]Sipi[A-Za-z0-9_]*\.(h|hpp)[>"]'
    run_pass symbol  '(^|[^A-Za-z0-9_])Sipi[A-Z][A-Za-z0-9_]*::'
} )

if [ -n "$violations" ]; then
    printf '%s\n' "$violations" >&2
    exit 1
fi
