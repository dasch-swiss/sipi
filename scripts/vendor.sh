#!/usr/bin/env bash
# Download, verify, and manage vendored dependency archives.
# Reads metadata from cmake/dependencies.cmake.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VENDOR_DIR="$REPO_ROOT/vendor"
MANIFEST="$REPO_ROOT/cmake/dependencies.cmake"

# Parse DEP_*_URL, DEP_*_FILENAME, and DEP_*_SHA256 from cmake manifest.
# Outputs lines of: NAME URL FILENAME SHA256
parse_deps() {
    local names=()
    local -A urls filenames sha256s

    while IFS= read -r line; do
        if [[ "$line" =~ ^set\(DEP_([A-Z0-9_]+)_URL[[:space:]]+\"([^\"]+)\"\) ]]; then
            local name="${BASH_REMATCH[1]}"
            urls["$name"]="${BASH_REMATCH[2]}"
            # Track unique names in order
            if [[ ! " ${names[*]:-} " =~ " $name " ]]; then
                names+=("$name")
            fi
        elif [[ "$line" =~ ^set\(DEP_([A-Z0-9_]+)_FILENAME[[:space:]]+\"([^\"]+)\"\) ]]; then
            filenames["${BASH_REMATCH[1]}"]="${BASH_REMATCH[2]}"
        elif [[ "$line" =~ ^set\(DEP_([A-Z0-9_]+)_SHA256[[:space:]]+\"([^\"]+)\"\) ]]; then
            sha256s["${BASH_REMATCH[1]}"]="${BASH_REMATCH[2]}"
        fi
    done < "$MANIFEST"

    for name in "${names[@]}"; do
        echo "$name ${urls[$name]:-} ${filenames[$name]:-} ${sha256s[$name]:-}"
    done
}

cmd_download() {
    local filter="${1:-}"
    mkdir -p "$VENDOR_DIR"

    while read -r name url filename sha256; do
        if [[ -n "$filter" && "$name" != "$filter" ]]; then
            continue
        fi
        local dest="$VENDOR_DIR/$filename"
        if [[ -f "$dest" ]]; then
            echo "  skip  $filename (exists)"
            continue
        fi
        echo "  fetch $filename"
        curl -fL --retry 3 --retry-delay 2 -o "$dest" "$url"
    done < <(parse_deps)
}

cmd_verify() {
    local rc=0
    while read -r name url filename sha256; do
        local dest="$VENDOR_DIR/$filename"
        if [[ ! -f "$dest" ]]; then
            echo "  MISS  $filename (not found)"
            rc=1
            continue
        fi
        if [[ "$sha256" == "PLACEHOLDER" ]]; then
            echo "  SKIP  $filename (no hash in manifest)"
            continue
        fi
        local actual
        actual="$(shasum -a 256 "$dest" | awk '{print $1}')"
        if [[ "$actual" == "$sha256" ]]; then
            echo "  OK    $filename"
        else
            echo "  FAIL  $filename"
            echo "        expected: $sha256"
            echo "        actual:   $actual"
            rc=1
        fi
    done < <(parse_deps)
    return $rc
}

cmd_checksums() {
    while read -r name url filename sha256; do
        local dest="$VENDOR_DIR/$filename"
        if [[ ! -f "$dest" ]]; then
            echo "# $name: NOT FOUND ($filename)"
            continue
        fi
        local actual
        actual="$(shasum -a 256 "$dest" | awk '{print $1}')"
        echo "set(DEP_${name}_SHA256 \"$actual\")"
    done < <(parse_deps)
}

cmd_update() {
    local target="${1:-}"
    if [[ -z "$target" ]]; then
        echo "Usage: $0 update <DEP_NAME>" >&2
        echo "Example: $0 update ZSTD" >&2
        exit 1
    fi
    target="$(echo "$target" | tr '[:lower:]' '[:upper:]')"

    # Remove existing archive so download is forced
    while read -r name url filename sha256; do
        if [[ "$name" == "$target" ]]; then
            rm -f "$VENDOR_DIR/$filename"
            break
        fi
    done < <(parse_deps)

    cmd_download "$target"

    echo ""
    echo "New checksum:"
    while read -r name url filename sha256; do
        if [[ "$name" == "$target" ]]; then
            local dest="$VENDOR_DIR/$filename"
            if [[ -f "$dest" ]]; then
                local actual
                actual="$(shasum -a 256 "$dest" | awk '{print $1}')"
                echo "set(DEP_${name}_SHA256 \"$actual\")"
            fi
        fi
    done < <(parse_deps)
}

usage() {
    cat <<EOF
Usage: $0 <command> [args]

Commands:
  download [NAME]   Download missing archives to vendor/ (optionally filter by NAME)
  verify            Verify SHA-256 checksums of all vendor archives
  checksums         Print SHA-256 checksums for all archives (for updating manifest)
  update <NAME>     Re-download one dep and print its new checksum

Examples:
  $0 download          # fetch all missing archives
  $0 download ZSTD     # fetch only zstd
  $0 verify            # check all checksums
  $0 checksums         # print all checksums
  $0 update OPENSSL    # re-download openssl, print new hash
EOF
}

case "${1:-}" in
    download)  cmd_download "${2:-}" ;;
    verify)    cmd_verify ;;
    checksums) cmd_checksums ;;
    update)    cmd_update "${2:-}" ;;
    *)         usage; exit 1 ;;
esac
