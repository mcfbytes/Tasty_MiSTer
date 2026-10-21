#!/bin/sh
# Install MiSTer Tasty on the MiSTer: the Tasty.sh menu script into Scripts/, then tasty and the catalogue.
# Usage: curl -fsSL https://raw.githubusercontent.com/mcfbytes/Tasty_MiSTer/main/install.sh | sh
# Run it again any time to update. It never downloads a ROM, and touches only Scripts/Tasty.sh and tasty/.
# For testing: TASTY_ROOT (default /media/fat), TASTY_SCRIPT_URL, TASTY_URL, TASTY_SHA_URL, TASTY_CATALOG_URL (file:// works
# when TASTY_ALLOW_FILE=1).

ROOT=${TASTY_ROOT:-/media/fat}
SCRIPTS=$ROOT/Scripts
DEST=$SCRIPTS/Tasty.sh
TASTY_SCRIPT_URL=${TASTY_SCRIPT_URL:-https://raw.githubusercontent.com/mcfbytes/Tasty_MiSTer/main/scripts/Tasty.sh}
TASTY_URL=${TASTY_URL:-https://github.com/mcfbytes/Tasty_MiSTer/releases/latest/download/tasty}
TASTY_SHA_URL=${TASTY_SHA_URL:-https://github.com/mcfbytes/Tasty_MiSTer/releases/latest/download/tasty.sha256}
TASTY_CATALOG_URL=${TASTY_CATALOG_URL:-https://raw.githubusercontent.com/mcfbytes/Tasty_MiSTer/main/data/tas-catalog.tsv}
MARKER='# MiSTer Tasty menu:'

main() {
    say() { printf 'tasty: %s\n' "$*"; }
    die() { printf 'tasty: %s\n' "$*" >&2; exit 1; }

    [ -d "$ROOT" ] || die "$ROOT not found: run this on the MiSTer itself (ssh root@<your-mister>)"
    if [ -z "${TASTY_ROOT:-}" ] && [ ! -e "$ROOT/MiSTer" ] && [ ! -d "$ROOT/config" ]; then
        die "$ROOT does not look like a MiSTer card: run this on the MiSTer itself"
    fi
    command -v curl >/dev/null 2>&1 || die "curl is missing"

    PROTO=https
    [ "${TASTY_ALLOW_FILE:-}" = 1 ] && PROTO=https,file

    NEW=$ROOT/.Tasty.sh.new
    rm -f "$NEW"
    trap 'rm -f "$NEW"' EXIT
    trap 'rm -f "$NEW"; exit 130' INT TERM HUP

    say "fetching Tasty.sh"
    curl -fsSL --proto "=$PROTO" --proto-redir "=$PROTO" --connect-timeout 15 --max-time 120 --retry 3 \
        -o "$NEW" "$TASTY_SCRIPT_URL" || die "could not download Tasty.sh (nothing was changed)"
    [ -s "$NEW" ] || die "the Tasty.sh download was empty (nothing was changed)"
    [ "$(head -n 1 "$NEW")" = '#!/bin/sh' ] || die "the Tasty.sh download is not a shell script (nothing was changed)"
    grep -q -F -- "$MARKER" "$NEW" || die "the Tasty.sh download is not the Tasty menu script (nothing was changed)"
    chmod 755 "$NEW" || die "cannot make Tasty.sh executable"
    mkdir -p "$SCRIPTS" || die "cannot create $SCRIPTS"
    mv "$NEW" "$DEST" || die "cannot install $DEST"
    say "installed $DEST"

    mkdir -p "$ROOT/tasty" || die "cannot create $ROOT/tasty"
    FAILED=0
    TASTY_DIR=$ROOT/tasty TASTY_BIN=$ROOT/tasty/tasty TASTY_URL=$TASTY_URL TASTY_SHA_URL=$TASTY_SHA_URL TASTY_CATALOG_URL=$TASTY_CATALOG_URL \
        TASTY_ALLOW_FILE=${TASTY_ALLOW_FILE:-} sh "$DEST" update || FAILED=1

    [ -x "$ROOT/tasty/tasty" ] && [ -s "$ROOT/tasty/tas-catalog.tsv" ] ||
        die "tasty or the catalogue could not be downloaded; run the install again, or: sh $DEST update"

    [ "$FAILED" = 0 ] || die "some downloads failed and the old files were kept; run the install again"

    say "done. Open the Scripts menu on the MiSTer and pick Tasty, or run $DEST over ssh."
}

main "$@"
