#!/bin/sh
# Try MiSTer Tasty: install it, then play a random TAS for one of your own ROMs. It never downloads a ROM.
# Usage: curl -fsSL https://raw.githubusercontent.com/mcfbytes/Tasty_MiSTer/main/test-tasty.sh | sh
# For testing: TASTY_INSTALL_URL overrides the installer download (file:// works when TASTY_ALLOW_FILE=1); install.sh's
# own overrides (TASTY_ROOT, TASTY_URL, ...) and TASTY_PLAY_ARGS pass through.
set -eu

main() {
    ROOT=${TASTY_ROOT:-/media/fat}
    TASTY_INSTALL_URL=${TASTY_INSTALL_URL:-https://raw.githubusercontent.com/mcfbytes/Tasty_MiSTer/main/install.sh}
    PROTO=https
    [ "${TASTY_ALLOW_FILE:-}" = 1 ] && PROTO=https,file
    die() { printf '\033[1;31mtasty:\033[0m %s\n' "$*" >&2; exit 1; }

    command -v curl >/dev/null 2>&1 || die "curl is missing"
    W=$(mktemp -d "${TMPDIR:-/tmp}/tasty-try.XXXXXX") || exit 1
    trap 'rm -rf "$W"' EXIT
    curl -fsSL --proto "=$PROTO" --proto-redir "=$PROTO" --connect-timeout 15 --max-time 120 --retry 3 \
        -o "$W/install.sh" "$TASTY_INSTALL_URL" || die "could not download the installer"
    sh "$W/install.sh" || die "the install failed"

    export TASTY_UI=text TASTY_DIR="$ROOT/tasty"
    sh "$ROOT/Scripts/Tasty.sh" scan >"$W/scan" || die "the ROM scan failed"
    awk -F'\t' '$1 == "ready"' "$W/scan" >"$W/ready"
    [ -s "$W/ready" ] || die "no ROM Tasty knows was found. Put your own dumps under games/ and run this again, or open Scripts, Tasty to see which ROMs are wanted"
    awk -F'\t' '$3 !~ /\[(untested|reported)\]$/' "$W/ready" >"$W/best"
    [ -s "$W/best" ] || cp "$W/ready" "$W/best"
    pick=$(awk 'BEGIN { srand() } { l[NR] = $0 } END { print l[int(rand() * NR) + 1] }' "$W/best")
    id=$(printf '%s\n' "$pick" | cut -f2)
    printf 'tasty: playing %s\n' "$(printf '%s\n' "$pick" | cut -f3)"
    rm -rf "$W"
    trap - EXIT
    exec sh "$ROOT/Scripts/Tasty.sh" play "$id"
}

main "$@"
