#!/bin/sh
# Tests for test-tasty.sh, the wrapper that installs Tasty and plays a random TAS: it plays one ready id (a real ELF stands in for tasty), prefers
# verified entries, says what to bring when nothing is ready, and plays nothing when the install fails.
# Usage: sh tests/try/run.sh [shell]   (the shell that runs test-tasty.sh, default sh)
HERE=$(cd "$(dirname "$0")" && pwd)
WRAP=$HERE/../../test-tasty.sh
REAL_SCRIPT=$HERE/../../scripts/Tasty.sh
INSTALL=$HERE/../../install.sh
SHELL_UNDER_TEST=${1:-sh}
T=$(mktemp -d "${TMPDIR:-/tmp}/tasty-try-test.XXXXXX") || exit 1
trap 'rm -rf "$T"' EXIT
fails=0
checks=0
ok() { checks=$((checks + 1)); }
bad() { checks=$((checks + 1)); fails=$((fails + 1)); printf 'FAIL: %s\n' "$*"; }
expect() { d=$1; shift; if "$@"; then ok; else bad "$d"; fi; }
not() { ! "$@"; }
has() { grep -q -F -- "$2" "$1"; }

command -v curl >/dev/null 2>&1 || { echo "SKIP: curl is not installed"; exit 0; }

DL=$T/dl
mkdir -p "$DL"
cp "$REAL_SCRIPT" "$DL/Tasty.sh"
cp "$INSTALL" "$DL/install.sh"
# any real ELF stands in for tasty (it exits 0 for every call); what was chosen shows in the output
cp /bin/true "$DL/tasty"
printf 'x' >"$T/rom.bin"
H=$(sha1sum <"$T/rom.bin" | cut -d' ' -f1)
printf 'movie\n' >"$T/movie.fm2"
M=$(sha1sum <"$T/movie.fm2" | cut -d' ' -f1)
cat_row() { printf '%s\tNES\t%s\t\tUSA\t%s.nes\tsha1\t%s\tines_file\thttps://x/%s\thttps://x/%s?handler=Download\t%s\tNES\t\t%s\t\n' \
    "$1" "$1" "$1" "$H" "$1" "$1" "$M" "$2"; }
cat_head() { printf '# t\nid\tplatform\ttitle\taliases\tregion\trom_name\thash_kind\trom_hash\tlayout\tmovie_page\tmovie_url\tmovie_sha1\tcore\tplay_args\tstatus\tnote\n'; }

try() { # try <root> [VAR=value...]: stdout/stderr to $T/out
    r=$1; shift
    env TASTY_ALLOW_FILE=1 TASTY_ROOT="$r" TMPDIR="$T" TASTY_GAMES="$r/games" \
        TASTY_INSTALL_URL="file://$DL/install.sh" TASTY_SCRIPT_URL="file://$DL/Tasty.sh" TASTY_URL="file://$DL/tasty" \
        TASTY_SHA_URL="file://$DL/none.sha256" TASTY_CATALOG_URL="file://$DL/tas-catalog.tsv" "$@" \
        "$SHELL_UNDER_TEST" "$WRAP" >"$T/out" 2>&1
}
card() { rm -rf "$T/card"; mkdir -p "$T/card/games/NES" "$T/card/tasty/movies"
    for i in only untestedone verifiedone reportedone untestedtwo verifiedtwo; do cp "$T/movie.fm2" "$T/card/tasty/movies/$i-m.fm2"; done; }

# nothing ready: a hint and no play
{ cat_head; cat_row only verified; } >"$DL/tas-catalog.tsv"
card
try "$T/card"; rc=$?
expect "no ROM: exits non-zero" test $rc -ne 0
expect "no ROM: says where to look" has "$T/out" "Scripts, Tasty"
expect "no ROM: plays nothing" not has "$T/out" "playing"

# one ready id: played with TASTY_PLAY_ARGS passed through
card; cp "$T/rom.bin" "$T/card/games/NES/only.nes"
try "$T/card" TASTY_PLAY_ARGS=""; rc=$?
expect "one ROM: exits 0" test $rc -eq 0
expect "one ROM: says what it chose" has "$T/out" "playing"
expect "one ROM: chose it" has "$T/out" "playing NES: only"

# verified is preferred over untested and reported
{ cat_head; cat_row untestedone untested; cat_row verifiedone verified; cat_row reportedone reported; cat_row untestedtwo untested; cat_row verifiedtwo verified; } >"$DL/tas-catalog.tsv"
card; for i in untestedone verifiedone reportedone untestedtwo verifiedtwo; do cp "$T/rom.bin" "$T/card/games/NES/$i.nes"; done
for n in 1 2 3 4 5 6 7 8 9 10 11 12; do
    try "$T/card"; rc=$?
    expect "mixed: exits 0 ($n)" test $rc -eq 0
    expect "mixed: only a verified entry is chosen ($n)" has "$T/out" "playing"
    if grep -q "playing NES: \(untestedone\|reportedone\|untestedtwo\)" "$T/out"; then bad "mixed: an unverified entry was chosen ($n)"; else ok; fi
done
{ cat_head; cat_row untestedone untested; } >"$DL/tas-catalog.tsv"
card; cp "$T/rom.bin" "$T/card/games/NES/untestedone.nes"
try "$T/card"; rc=$?
expect "only unverified entries: still plays one" test $rc -eq 0

# a failing scan: the scan-failed message and no play
cat >"$DL/stub.sh" <<'STUB'
#!/bin/sh
# MiSTer Tasty menu: stub whose scan fails
case $1 in
    update) cp "$TASTY_URL_SRC" "$TASTY_BIN"; printf 'id\tplatform\nx\tNES\n' >"$TASTY_DIR/tas-catalog.tsv" ;;
    scan) exit 1 ;;
    play) echo "playing" ;;
esac
STUB
card
try "$T/card" TASTY_SCRIPT_URL="file://$DL/stub.sh" TASTY_URL_SRC="$DL/tasty"; rc=$?
expect "failing scan: exits non-zero" test $rc -ne 0
expect "failing scan: says so" has "$T/out" "the ROM scan failed"
expect "failing scan: plays nothing" not has "$T/out" "playing"

# install failure: nothing played
card
try "$T/card" TASTY_URL="file://$DL/missing"; rc=$?
expect "install failure: exits non-zero" test $rc -ne 0
expect "install failure: plays nothing" not has "$T/out" "playing"
try "$T/card" TASTY_INSTALL_URL="file://$DL/missing"; rc=$?
expect "installer unavailable: exits non-zero" test $rc -ne 0

if [ "$fails" -ne 0 ]; then echo "$fails of $checks checks failed"; exit 1; fi
echo "all $checks checks passed"
