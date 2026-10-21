#!/bin/sh
# Tests for install.sh against a fake card with file:// downloads: a fresh install, a re-run, bad downloads that
# leave the old files, a missing card, and a missing Scripts directory.
# Usage: sh tests/install/run.sh [shell]   (the shell that runs install.sh, default sh)
HERE=$(cd "$(dirname "$0")" && pwd)
INSTALL=$HERE/../../install.sh
REAL_SCRIPT=$HERE/../../scripts/Tasty.sh
SHELL_UNDER_TEST=${1:-sh}
T=$(mktemp -d "${TMPDIR:-/tmp}/tasty-install-test.XXXXXX") || exit 1
trap 'rm -rf "$T"' EXIT
fails=0
checks=0

ok() { checks=$((checks + 1)); }
bad() { checks=$((checks + 1)); fails=$((fails + 1)); printf 'FAIL: %s\n' "$*"; }
expect() { d=$1; shift; if "$@"; then ok; else bad "$d"; fi; }
not() { ! "$@"; }
same() { cmp -s "$1" "$2"; }

command -v curl >/dev/null 2>&1 || { echo "SKIP: curl is not installed"; exit 0; }

DL=$T/dl
mkdir -p "$DL"
cp "$REAL_SCRIPT" "$DL/Tasty.sh"
{ printf '\177ELF'; printf 'fake binary one'; } >"$DL/tasty"
printf 'id\tplatform\tx\nsmb\tnes\ty\n' >"$DL/tas-catalog.tsv"
printf 'echo not the menu script\n' >"$DL/notshell.sh"
printf '#!/bin/sh\necho some other script\n' >"$DL/othershell.sh"
printf 'not an elf' >"$DL/notelf"

run() { # run <root> [VAR=value...]
    r=$1; shift
    env TASTY_ALLOW_FILE=1 TASTY_ROOT="$r" TMPDIR="$T" \
        TASTY_SCRIPT_URL="file://$DL/Tasty.sh" TASTY_URL="file://$DL/tasty" \
        TASTY_CATALOG_URL="file://$DL/tas-catalog.tsv" TASTY_SHA_URL="file://$DL/none.sha256" "$@" \
        "$SHELL_UNDER_TEST" "$INSTALL" >"$T/out" 2>&1
}

# fresh install, Scripts/ missing
R=$T/card1
mkdir -p "$R"
run "$R"; rc=$?
expect "fresh install exits 0" test $rc -eq 0
expect "Scripts dir created and script installed" test -x "$R/Scripts/Tasty.sh"
expect "script is the shipped one" same "$R/Scripts/Tasty.sh" "$DL/Tasty.sh"
expect "tasty installed" test -x "$R/tasty/tasty"
expect "catalogue installed" same "$R/tasty/tas-catalog.tsv" "$DL/tas-catalog.tsv"
expect "no .new left behind" not test -e "$R/Scripts/Tasty.sh.new"
expect "points at the Scripts menu" grep -q -F 'Scripts menu' "$T/out"
expect "nothing else on the card" test "$(ls "$R")" = "Scripts
tasty"
expect "no MiSTer binary created" not test -e "$R/MiSTer"

# re-run is idempotent and updates
{ printf '\177ELF'; printf 'fake binary two'; } >"$DL/tasty"
run "$R"; rc=$?
expect "re-run exits 0" test $rc -eq 0
expect "re-run updates tasty" same "$R/tasty/tasty" "$DL/tasty"

# bad downloads leave the old files
cp "$R/Scripts/Tasty.sh" "$T/old-script"; cp "$R/tasty/tasty" "$T/old-tasty"
for bad_url in "$DL/notshell.sh" "$DL/othershell.sh" "$DL/missing"; do
    run "$R" TASTY_SCRIPT_URL="file://$bad_url"; rc=$?
    expect "bad script ($bad_url) fails" test $rc -ne 0
    expect "bad script leaves the old script" same "$R/Scripts/Tasty.sh" "$T/old-script"
    expect "bad script leaves no .new" not test -e "$R/Scripts/Tasty.sh.new"
    expect "bad script leaves tasty alone" same "$R/tasty/tasty" "$T/old-tasty"
done
run "$R" TASTY_URL="file://$DL/notelf"; rc=$?
expect "non-ELF tasty fails" test $rc -ne 0
expect "non-ELF leaves the old tasty" same "$R/tasty/tasty" "$T/old-tasty"
expect "non-ELF leaves no .new" not test -e "$R/tasty/tasty.new"

# a failed download on a fresh card leaves no Scripts/; an inherited TASTY_BIN does not redirect the binary
mkdir -p "$T/card3"
run "$T/card3" TASTY_SCRIPT_URL="file://$DL/missing"; rc=$?
expect "failed first download fails" test $rc -ne 0
expect "failed first download leaves the card empty" test -z "$(ls -A "$T/card3")"
mkdir -p "$T/card4"
run "$T/card4" TASTY_BIN="$T/elsewhere"; rc=$?
expect "install with an inherited TASTY_BIN exits 0" test $rc -eq 0
expect "binary lands in tasty/ regardless" test -x "$T/card4/tasty/tasty"
expect "nothing written to the inherited TASTY_BIN" not test -e "$T/elsewhere"

# no card
run "$T/nocard"; rc=$?
expect "no card refuses" test $rc -ne 0
expect "no card says so" grep -q -F 'run this on the MiSTer' "$T/out"
expect "no card creates nothing" not test -e "$T/nocard"

# no curl
mkdir -p "$T/nocurl-bin" "$T/card2"
for tool in "$SHELL_UNDER_TEST" sh head grep chmod mv rm mkdir cmp od tr cat dirname mktemp; do
    p=$(command -v "$tool") && ln -sf "$p" "$T/nocurl-bin/$(basename "$tool")"
done
run "$T/card2" PATH="$T/nocurl-bin"; rc=$?
expect "missing curl refuses" test $rc -ne 0
expect "missing curl says so" grep -q -F 'curl is missing' "$T/out"
expect "missing curl installs nothing" not test -e "$T/card2/Scripts/Tasty.sh"

if [ "$fails" -ne 0 ]; then printf '%d of %d checks failed\n' "$fails" "$checks"; exit 1; fi
printf 'all %d checks passed\n' "$checks"
