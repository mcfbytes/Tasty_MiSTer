#!/bin/sh
# Tests for scripts/Tasty.sh against a fake MiSTer tree: loose and zipped ROMs, wrong ROMs under the right name,
# header flavours, an unreadable zip, odd file names, the scan cache, and a fake `tasty` that records its calls.
# Usage: sh tests/menu/run.sh [shell]   (the shell that runs Tasty.sh, default sh)
HERE=$(cd "$(dirname "$0")" && pwd)
SCRIPT=$HERE/../../scripts/Tasty.sh
SHELL_UNDER_TEST=${1:-sh}
T=$(mktemp -d "${TMPDIR:-/tmp}/tasty-menu-test.XXXXXX") || exit 1
trap 'rm -rf "$T"' EXIT
fails=0
checks=0

ok() { checks=$((checks + 1)); }
bad() { checks=$((checks + 1)); fails=$((fails + 1)); printf 'FAIL: %s\n' "$*"; }
expect() { # expect <description> <condition...>
    d=$1; shift
    if "$@"; then ok; else bad "$d"; fi
}
has() { grep -q -F -- "$2" "$1"; }

command -v zip >/dev/null 2>&1 || { echo "SKIP: zip is not installed"; exit 0; }

# --- fixtures -------------------------------------------------------------------------------------------------
rep() { head -c "$2" /dev/zero | tr '\0' "$1"; }   # rep <char> <bytes>
ines() { # ines <prg-char> <chr-char> [flags7] [extra-header-byte]: a 16K+8K iNES file on stdout
    printf 'NES\032\001\001\000%b' "\\0$(printf %o "${3:-0}")"
    printf '\000\000\000\000\000\000\000\000'
    rep "$1" 16384; rep "$2" 8192
}
body_md5() { tail -c +17 "$1" | md5sum | cut -d' ' -f1; }
file_sha1() { sha1sum <"$1" | cut -d' ' -f1; }
sfc() { rep "$1" 32768; }

G=$T/mister/games
mkdir -p "$G/NES/sub dir" "$G/SNES" "$G/NES/zips" "$T/tasty/movies" "$T/bin" "$T/dl"

# NES ROM A (the real one) and its hashes; ROM B is the same name with different content
ines P C >"$T/a.nes"
ines X X >"$T/b.nes"
A_BODY=$(body_md5 "$T/a.nes")
A_FILE=$(file_sha1 "$T/a.nes")

cp "$T/a.nes" "$G/NES/Alpha Quest (USA).nes"                                # 1 plain, headered
ines P C 8 >"$T/a-flag.nes"; cp "$T/a-flag.nes" "$G/NES/sub dir/Beta Run [!] (Rev 1).nes"  # 2 other header flavour, brackets
mkdir -p "$T/z1" && cp "$T/a.nes" "$T/z1/Gamma Strike (USA).nes" && (cd "$T/z1" && zip -q "$G/NES/zips/Gamma Strike (USA).zip" "Gamma Strike (USA).nes")  # 3 zipped
mkdir -p "$T/z2" && cp "$T/a.nes" "$T/z2/rom.nes" && (cd "$T/z2" && zip -q "$G/NES/zips/Delta Force.zip" rom.nes)  # 4 zip named right, member not
mkdir -p "$T/z3" && cp "$T/a.nes" "$T/z3/Epsilon [!].nes" && (cd "$T/z3" && zip -q "$G/NES/zips/Epsilon (USA).zip" "Epsilon [!].nes")  # 5 brackets inside a zip
cp "$T/b.nes" "$G/NES/Zeta Zone (USA).nes"                                  # 6 wrong ROM, right name
printf 'this is not a zip' >"$G/NES/zips/Eta Edge.zip"                        # 7 unreadable zip
cp "$T/a.nes" "$G/NES/Theta Tales, The (USA).nes"                           # 8 ", The" form
# iNES file hash with a dirty header (bytes 8-15 set): matches the canonical-header digest
{ printf 'NES\032\001\001\000\000DIRTY!!!'; rep P 16384; rep C 8192; } >"$T/dirty.nes"
cp "$T/dirty.nes" "$G/NES/Iota Isle (USA).nes"
{ printf 'NES\032\001\001\000\000\000\000\000\000\000\000\000\000'; rep P 16384; rep C 8192; } >"$T/clean.nes"
IOTA_SHA=$(file_sha1 "$T/clean.nes")
# SNES: a headerless dump and a copier-headered one of the same body
sfc K >"$T/k.sfc"
K_SHA=$(file_sha1 "$T/k.sfc")
cp "$T/k.sfc" "$G/SNES/Kappa Kart (USA).sfc"
{ rep H 512; sfc L; } >"$T/l.smc"
{ sfc L; } >"$T/l.sfc"
L_SHA=$(file_sha1 "$T/l.sfc")
cp "$T/l.smc" "$G/SNES/Lambda Land (USA).smc"
mkdir -p "$T/z4" && cp "$T/a.nes" "$T/z4/-Dash Dance - Sequel (USA).nes" && (cd "$T/z4" && zip -q "$G/NES/zips/Dash Dance - Sequel (USA).zip" -- "-Dash Dance - Sequel (USA).nes")  # leading dash in a member
cp "$T/z1/Gamma Strike (USA).nes" "$G/NES/Mu Mission.nes"                   # no catalogue row names it

row() { # row id plat title aliases rom kind hash layout [movie_sha1 [args]]
    printf '%s\t%s\t%s\t%s\tUSA\t%s\t%s\t%s\t%s\thttps://tasvideos.org/%s\t%s\t%s\t%s\t%s\tverified\t\n' \
        "$1" "$2" "$3" "$4" "$5" "$6" "$7" "$8" "$9" "${12:-https://tasvideos.org/$9?handler=Download}" "${10:-}" "$2" "${11:-}"
}
{
    printf '# test catalogue\n'
    printf 'id\tplatform\ttitle\taliases\tregion\trom_name\thash_kind\trom_hash\tlayout\tmovie_page\tmovie_url\tmovie_sha1\tcore\tplay_args\tstatus\tnote\n'
    row alpha NES "Alpha Quest" "" "Alpha Quest.nes" md5 "$A_BODY" ines_body 1001M
    row beta NES "Beta Run" "" "Beta Run.nes" md5 "$A_BODY" ines_body 1002M
    row gamma NES "Gamma Strike" "" "Gamma Strike.nes" md5 "$A_BODY" ines_body 1003M
    row delta NES "Delta Force" "" "Delta Force.nes" md5 "$A_BODY" ines_body 1004M
    row epsilon NES "Epsilon" "" "Epsilon.nes" md5 "$A_BODY" ines_body 1005M
    row zeta NES "Zeta Zone" "" "Zeta Zone.nes" md5 "$A_BODY" ines_body 1006M
    row eta NES "Eta Edge" "" "Eta Edge.nes" md5 "$A_BODY" ines_body 1007M
    row theta NES "The Theta Tales" "" "Theta Tales.nes" md5 "$A_BODY" ines_body 1008M
    row iota NES "Iota Isle" "" "Iota Isle.nes" sha1 "$IOTA_SHA" ines_file 1009M
    row alphafile NES "Alpha Quest" "" "Alpha Quest.nes" sha1 "$A_FILE" ines_file 1010M
    row kappa SNES "Kappa Kart" "" "Kappa Kart.sfc" sha1 "$K_SHA" copier 1011M
    row lambda SNES "Lambda Land" "Lambda Alias" "Lambda Land.sfc" sha1 "$L_SHA" copier 1012M
    row dash NES "Dash Dance - Sequel" "" "Dash Dance.nes" md5 "$A_BODY" ines_body 1016M
    row nu NES "Nu Nowhere" "" "Nu Nowhere.nes" md5 "$A_BODY" ines_body 1013M
} >"$T/tasty/tas-catalog.tsv"

run() { TASTY_ALLOW_FILE=1 TASTY_SHA_URL="${TASTY_SHA_URL:-file://$T/dl/none.sha256}" TASTY_DIR=$T/tasty TASTY_GAMES=$T/mister/games TASTY_UI=text TASTY_PLAY_ARGS="${PLAY_ARGS:-}" \
    TASTY_BIN=$T/bin/tasty TASTY_MOVIE_URL_BASE="file://$T/dl" TMPDIR=$T "$SHELL_UNDER_TEST" "$SCRIPT" "$@"; }

# --- scan -----------------------------------------------------------------------------------------------------
run scan >"$T/scan.out" 2>"$T/scan.err"
expect "scan exits 0" test $? -eq 0
ready() { grep -q "^ready	$1	" "$T/scan.out"; }
missing() { grep -q "^missing	$1	" "$T/scan.out"; }
expect "plain headered ROM is ready" ready alpha
expect "other header flavour and brackets in the name is ready (header ignored by body hash)" ready beta
expect "zipped ROM with a matching member name is ready" ready gamma
expect "zip named for the game with an arbitrary member is ready" ready delta
expect "zipped ROM with brackets in the member name is ready" ready epsilon
expect "wrong ROM under the right name is rejected" missing zeta
expect "unreadable zip is just not found" missing eta
expect "', The' file name matches 'The Title'" ready theta
expect "dirty-header iNES matches the canonical-header digest" ready iota
expect "whole-file digest of an untouched iNES file" ready alphafile
expect "headerless SNES dump is ready" ready kappa
expect "512-byte copier header is skipped" ready lambda
expect "a zip member starting with a dash is read" ready dash
expect "a catalogue row with no ROM is missing" missing nu
expect "the missing line names the ROM to look for" has "$T/scan.out" "Nu Nowhere.nes"
expect "an unrelated ROM is not claimed by any row" test "$(grep -c 'Mu Mission' "$T/scan.out")" -eq 0
expect "the unreadable zip warns nothing fatal" test ! -s "$T/scan.err"
expect "zipped ready line names the member" has "$T/scan.out" "Gamma Strike (USA).zip::Gamma Strike (USA).nes"

# --- cache ----------------------------------------------------------------------------------------------------
expect "the scan cache exists" test -s "$T/tasty/scan-cache.tsv"
run scan >"$T/scan2.out" 2>/dev/null
expect "a second scan gives the same answer" cmp -s "$T/scan.out" "$T/scan2.out"
n1=$(wc -l <"$T/tasty/scan-cache.tsv")
run scan >/dev/null 2>&1
n2=$(wc -l <"$T/tasty/scan-cache.tsv")
expect "a rescan adds nothing to the cache" test "$n1" -eq "$n2"
# swap the file for the wrong ROM with a newer mtime: the cache must not keep the old verdict
cp "$T/b.nes" "$G/NES/Alpha Quest (USA).nes"; touch -d '+2 hours' "$G/NES/Alpha Quest (USA).nes"
run scan >"$T/scan3.out" 2>/dev/null
expect "a changed file is hashed again" grep -q "^missing	alpha	" "$T/scan3.out"
rm -f "$G/NES/Alpha Quest (USA).nes"
run scan >"$T/scan4.out" 2>/dev/null
expect "a deleted file is gone from the results" grep -q "^missing	alpha	" "$T/scan4.out"
expect "a deleted file is gone from the cache" test "$(grep -c 'Alpha Quest (USA).nes' "$T/tasty/scan-cache.tsv")" -eq 0
cp "$T/a.nes" "$G/NES/Alpha Quest (USA).nes"

# --- local catalogue ------------------------------------------------------------------------------------------
{ row mine NES "Mu Mission" "" "Mu Mission.nes" md5 "$A_BODY" ines_body 1014M; } >"$T/tasty/catalog.local.tsv"
run scan >"$T/scan5.out" 2>/dev/null
expect "a local catalogue row is added" grep -q "^ready	mine	" "$T/scan5.out"

# --- play -----------------------------------------------------------------------------------------------------
cat >"$T/bin/tasty" <<'FAKE'
#!/bin/sh
echo "$*" >>"$TASTY_TEST_LOG"
case $1 in
    play | check)
        shift
        rom=
        while [ $# -gt 0 ]; do [ "$1" = --rom ] && rom=$2; shift; done
        if [ -f "$rom" ]; then echo "rom-present $rom" >>"$TASTY_TEST_LOG"; else echo "rom-missing $rom" >>"$TASTY_TEST_LOG"; fi
        if [ -n "${TASTY_TUNE_ROOT:-}" ]; then
            R=$TASTY_TUNE_ROOT
            echo "tune $(cat "$R/proc/sys/vm/compact_unevictable_allowed") $(cat "$R/proc/sys/vm/compaction_proactiveness") $(cat "$R/sys/bus/workqueue/devices/writeback/cpumask" 2>/dev/null)" >>"$TASTY_TEST_LOG"
        fi
        ;;
esac
exit 0
FAKE
chmod +x "$T/bin/tasty" && cp "$T/bin/tasty" "$T/fake-tasty"
TASTY_TEST_LOG="$T/tasty.log"; export TASTY_TEST_LOG
# the movie zip a tasvideos download would be
mkdir -p "$T/mv" && printf 'fake movie\n' >"$T/mv/gamma.fm2" && (cd "$T/mv" && zip -q "$T/dl/1003M.zip" gamma.fm2 && mv "$T/dl/1003M.zip" "$T/dl/1003M")
MV_SHA=$(file_sha1 "$T/mv/gamma.fm2")
# replace the gamma row with the movie hash and an argument
grep -v '^gamma	' "$T/tasty/tas-catalog.tsv" >"$T/cat.tmp"
row gamma NES "Gamma Strike" "" "Gamma Strike.nes" md5 "$A_BODY" ines_body 1003M "$MV_SHA" "--lead -1" >>"$T/cat.tmp"
cp "$T/cat.tmp" "$T/tasty/tas-catalog.tsv"
: >"$TASTY_TEST_LOG"
PLAY_ARGS="--stay" run play gamma >"$T/play.out" 2>"$T/play.err"; echo $? >"$T/play.rc"
expect "play exits 0" test "$(cat "$T/play.rc")" -eq 0
expect "the movie landed in the movies folder" test -f "$T/tasty/movies/1003M-gamma.fm2"
expect "tasty check ran first" grep -q "^check .*1003M-gamma.fm2 --rom " "$TASTY_TEST_LOG"
expect "tasty play got the movie and the row's and the user's arguments" grep -q "^play .*1003M-gamma.fm2 --rom .* --lead -1 --stay" "$TASTY_TEST_LOG"
expect "the zipped ROM was unpacked for tasty" has "$TASTY_TEST_LOG" "rom-present"
expect "the unpacked name is the member's" grep -q "rom-present .*/Gamma Strike (USA).nes" "$TASTY_TEST_LOG"
unpacked=$(sed -n 's/^rom-present //p' "$TASTY_TEST_LOG" | head -n 1)
expect "the unpacked ROM is cleaned up afterwards" test ! -e "$unpacked"
# a second play reuses the movie: remove the download and it must still work
rm -f "$T/dl/1003M"; : >"$TASTY_TEST_LOG"
run play gamma >/dev/null 2>&1
expect "a stored movie is not downloaded again" grep -q "^play " "$TASTY_TEST_LOG"
# a movie that does not match its catalogue hash is refused
rm -f "$T/tasty/movies/1003M-gamma.fm2"; printf 'tampered\n' >"$T/mv/gamma.fm2"; rm -f "$T/dl/1003M"; (cd "$T/mv" && zip -q "$T/dl/1003M.zip" gamma.fm2 && mv "$T/dl/1003M.zip" "$T/dl/1003M")
: >"$TASTY_TEST_LOG"
run play gamma >/dev/null 2>"$T/play2.err"
expect "a movie with the wrong hash is refused" test $? -ne 0
expect "and tasty is never run" test ! -s "$TASTY_TEST_LOG"
expect "and the user is told why" has "$T/play2.err" "does not match the catalogue"
# a row without a movie hash is refused, and so is a movie URL off tasvideos.org
: >"$TASTY_TEST_LOG"
run play mine >/dev/null 2>"$T/play4.err"
expect "a row with no movie SHA-1 is refused" test $? -ne 0
expect "and says why" has "$T/play4.err" "no movie SHA-1"
grep -v '^mine	' "$T/tasty/catalog.local.tsv" >"$T/local.tmp"
row mine NES "Mu Mission" "" "Mu Mission.nes" md5 "$A_BODY" ines_body 1014M "$MV_SHA" "" "https://evil.example/1014M" >>"$T/local.tmp"
cp "$T/local.tmp" "$T/tasty/catalog.local.tsv"
run play mine >/dev/null 2>"$T/play5.err"
expect "a movie URL off tasvideos.org is refused" test $? -ne 0
expect "and says so" has "$T/play5.err" "not on tasvideos.org"
expect "and tasty is not run" test ! -s "$TASTY_TEST_LOG"
# movie zips whose members have spaces: the movie is picked by extension and extracted by name
mkdir -p "$T/mv2" && printf 'spacey\n' >"$T/mv2/my movie [1].fm2" && printf 'x' >"$T/mv2/readme.txt"
(cd "$T/mv2" && zip -q "$T/dl/1015M.zip" readme.txt "my movie [1].fm2" && mv "$T/dl/1015M.zip" "$T/dl/1015M")
SP_SHA=$(file_sha1 "$T/mv2/my movie [1].fm2")
row mine NES "Mu Mission" "" "Mu Mission.nes" md5 "$A_BODY" ines_body 1015M "$SP_SHA" >"$T/tasty/catalog.local.tsv"
: >"$TASTY_TEST_LOG"
run play mine >/dev/null 2>"$T/play6.err"
expect "a movie member with spaces and brackets plays" grep -q "^play .*1015M-.* --rom " "$TASTY_TEST_LOG"
expect "the stored movie has the member's contents" cmp -s "$(ls "$T"/tasty/movies/1015M-*)" "$T/mv2/my movie [1].fm2"
# play options from a catalogue row are limited to a whitelist
row mine NES "Mu Mission" "" "Mu Mission.nes" md5 "$A_BODY" ines_body 1015M "$SP_SHA" "--rom /etc/passwd --lead 3" >"$T/tasty/catalog.local.tsv"
: >"$TASTY_TEST_LOG"
run play mine >/dev/null 2>"$T/play7.err"
expect "a row with a non-whitelisted option is refused" test $? -ne 0
expect "and tasty is not run" test ! -s "$TASTY_TEST_LOG"
row mine NES "Mu Mission" "" "Mu Mission.nes" md5 "$A_BODY" ines_body 1015M "$SP_SHA" "--lead 3x" >"$T/tasty/catalog.local.tsv"
run play mine >/dev/null 2>&1
expect "a non-numeric --lead is refused" test $? -ne 0
# a ROM that is not there
: >"$TASTY_TEST_LOG"
run play nu >/dev/null 2>"$T/play3.err"
expect "playing an entry with no ROM fails" test $? -ne 0
expect "and says so" has "$T/play3.err" "no matching ROM"
run stop >/dev/null 2>&1
expect "stop calls tasty stop" grep -q "^stop" "$TASTY_TEST_LOG"

# --- update -----------------------------------------------------------------------------------------------------
printf '\177ELF-new-binary' >"$T/dl/tasty-new"
printf 'old binary' >"$T/bin/tasty-old"
cp "$T/bin/tasty" "$T/bin/tasty.keep"
TASTY_URL="file://$T/dl/tasty-new" run update >/dev/null 2>&1
upd_fail=$?
expect "an update whose catalogue download fails exits non-zero" test $upd_fail -ne 0
cp "$T/tasty/tas-catalog.tsv" "$T/dl/cat-ok"
TASTY_URL="file://$T/dl/tasty-new" TASTY_CATALOG_URL="file://$T/dl/cat-ok" run update >/dev/null 2>&1
upd_ok=$?
expect "a clean update exits 0" test $upd_ok -eq 0
expect "update installs a binary with ELF magic" grep -q 'ELF-new-binary' "$T/bin/tasty"
expect "the update leaves no .new file" test ! -e "$T/bin/tasty.new"
printf 'truncated, not an ELF' >"$T/dl/tasty-bad"
cp "$T/bin/tasty" "$T/bin/tasty.before"
TASTY_URL="file://$T/dl/tasty-bad" run update >/dev/null 2>"$T/upd.err"
upd_bad=$?
expect "a failed binary download makes update exit non-zero" test $upd_bad -ne 0
expect "a download that is not an ELF leaves the old binary untouched" cmp -s "$T/bin/tasty" "$T/bin/tasty.before"
expect "and says so" has "$T/upd.err" "could not update tasty"
: >"$T/dl/tasty-empty"
TASTY_URL="file://$T/dl/tasty-empty" run update >/dev/null 2>&1
expect "an empty download leaves the old binary untouched" cmp -s "$T/bin/tasty" "$T/bin/tasty.before"
TASTY_URL="file://$T/dl/does-not-exist" run update >/dev/null 2>&1
expect "a failed download leaves the old binary untouched" cmp -s "$T/bin/tasty" "$T/bin/tasty.before"
cp "$T/tasty/tas-catalog.tsv" "$T/cat.before"
printf 'id\tplatform\n' >"$T/dl/cat-empty"
TASTY_CATALOG_URL="file://$T/dl/cat-empty" run update >/dev/null 2>&1
upd_cat=$?
expect "a failed catalogue download makes update exit non-zero" test $upd_cat -ne 0
expect "a catalogue with no rows leaves the old one" cmp -s "$T/tasty/tas-catalog.tsv" "$T/cat.before"
TASTY_URL="file://$T/dl/tasty-new" TASTY_CATALOG_URL="file://$T/dl/cat-bad" run update >/dev/null 2>&1
expect "a missing catalogue download leaves the old one" cmp -s "$T/tasty/tas-catalog.tsv" "$T/cat.before"
cp "$T/cat.before" "$T/dl/cat-good"
TASTY_CATALOG_URL="file://$T/dl/cat-good" run update >/dev/null 2>&1
expect "a good catalogue is installed" cmp -s "$T/tasty/tas-catalog.tsv" "$T/cat.before"
# the published sha256: skip the download when the installed binary matches, refuse a download that does not
good=$(sha256sum <"$T/dl/tasty-new" | cut -d' ' -f1)
printf '%s  tasty\n' "$good" >"$T/dl/tasty.sha256"
printf '%s  tasty\n' "$(printf x | sha256sum | cut -d' ' -f1)" >"$T/dl/other.sha256"
printf 'not a hash\n' >"$T/dl/bad.sha256"
printf '%s\n' "$(printf '%s' "$good" | tr a-f A-F)" >"$T/dl/upper.sha256"
cp "$T/dl/tasty-new" "$T/bin/tasty"
SHAENV="TASTY_SHA_URL=file://$T/dl/tasty.sha256 TASTY_CATALOG_URL=file://$T/dl/cat-good"
# shellcheck disable=SC2086
env $SHAENV TASTY_URL="file://$T/dl/unreadable" TASTY_ALLOW_FILE=1 TASTY_DIR=$T/tasty TASTY_BIN=$T/bin/tasty TASTY_UI=text TMPDIR=$T \
    "$SHELL_UNDER_TEST" "$SCRIPT" update >"$T/sha1.out" 2>"$T/sha1.err"
expect "a matching binary is not downloaded (the binary URL is unreadable) and update exits 0" test $? -eq 0
expect "and says it is up to date" has "$T/sha1.out" "tasty is up to date"
printf 'old' >"$T/bin/tasty"; cp "$T/bin/tasty" "$T/bin/tasty.before"
TASTY_SHA_URL="file://$T/dl/tasty.sha256" TASTY_URL="file://$T/dl/tasty-new" TASTY_CATALOG_URL="file://$T/dl/cat-good" run update >/dev/null 2>&1
expect "a differing binary is downloaded" cmp -s "$T/bin/tasty" "$T/dl/tasty-new"
TASTY_SHA_URL="file://$T/dl/other.sha256" TASTY_URL="file://$T/dl/tasty-new" TASTY_CATALOG_URL="file://$T/dl/cat-good" run update >/dev/null 2>"$T/sha2.err"
expect "a download that does not match the published hash exits non-zero" test $? -ne 0
expect "and the installed binary is kept" cmp -s "$T/bin/tasty" "$T/dl/tasty-new"
printf 'old' >"$T/bin/tasty"
TASTY_SHA_URL="file://$T/dl/other.sha256" TASTY_URL="file://$T/dl/tasty-new" TASTY_CATALOG_URL="file://$T/dl/cat-good" run update >/dev/null 2>"$T/sha2.err"
expect "a mismatch leaves the old binary" test "$(cat "$T/bin/tasty")" = old
expect "a mismatch leaves no .new" test ! -e "$T/bin/tasty.new"
expect "a mismatch says so" has "$T/sha2.err" "published sha256"
for hf in none bad upper; do
    printf 'old' >"$T/bin/tasty"
    TASTY_SHA_URL="file://$T/dl/$hf.sha256" TASTY_URL="file://$T/dl/tasty-new" TASTY_CATALOG_URL="file://$T/dl/cat-good" run update >/dev/null 2>&1
    expect "a $hf hash file is treated as missing: update downloads as before" cmp -s "$T/bin/tasty" "$T/dl/tasty-new"
done
cp "$T/dl/tasty-new" "$T/bin/tasty"
# https-only: without the test hook, file:// is refused
TASTY_ALLOW_FILE=0 TASTY_DIR=$T/tasty TASTY_BIN=$T/bin/tasty TASTY_UI=text TASTY_URL="file://$T/dl/tasty-new" TMPDIR=$T \
    "$SHELL_UNDER_TEST" "$SCRIPT" update >/dev/null 2>"$T/upd2.err"
expect "file:// downloads are refused outside the tests" has "$T/upd2.err" "could not update"

# --- busybox unzip: no "--", a different listing footer; a stale cache from an older script is dropped -------
mkdir -p "$T/bb"
cat >"$T/bb/unzip" <<EOS
#!/bin/sh
[ \$# -eq 0 ] && { echo "BusyBox v1.38.0 () multi-call binary."; exit 0; }
for a in "\$@"; do [ "\$a" = "--" ] && { echo "Usage: unzip [-lnojpqK] FILE[.zip]" >&2; exit 1; }; done
exec $(command -v unzip) "\$@"
EOS
chmod +x "$T/bb/unzip"
printf 'H\037stale\n' >"$T/tasty/scan-cache.tsv"
PATH="$T/bb:$PATH" run scan >"$T/scan-bb.out" 2>/dev/null
expect "busybox unzip: a zipped ROM is ready" grep -q "^ready	gamma	" "$T/scan-bb.out"
expect "busybox unzip: brackets inside a zip are ready" grep -q "^ready	epsilon	" "$T/scan-bb.out"
expect "an old cache is replaced by a versioned one" test "$(head -n 1 "$T/tasty/scan-cache.tsv")" = "#tasty-scan-cache 2"
expect "busybox unzip: a member starting with a dash is read" grep -q "^ready	dash	" "$T/scan-bb.out"
expect "the stale cache row is gone" test "$(grep -c stale "$T/tasty/scan-cache.tsv")" -eq 0
mkdir -p "$G/NES/other" && cp "$G/NES/zips/Gamma Strike (USA).zip" "$G/NES/other/unrelated.zip"
PATH="$T/bb:$PATH" run scan >/dev/null 2>&1
expect "a zip whose name holds no catalogue title is never opened" test "$(grep -c unrelated "$T/tasty/scan-cache.tsv")" -eq 0
rm -rf "$G/NES/other"

# --- run tuning: compaction off and writeback on CPU 0 during a play, then put back -----------------------------
# the fake tasty and the good movie again (the update and download tests above replace both)
cp "$T/fake-tasty" "$T/bin/tasty"
rm -f "$T/tasty/movies/1003M-gamma.fm2" "$T/dl/1003M"; printf 'fake movie\n' >"$T/mv/gamma.fm2"
(cd "$T/mv" && zip -q "$T/dl/1003M.zip" gamma.fm2 && mv "$T/dl/1003M.zip" "$T/dl/1003M")
R=$T/tune
mkdir -p "$R/proc/sys/vm" "$R/sys/bus/workqueue/devices/writeback"
echo 1 >"$R/proc/sys/vm/compact_unevictable_allowed"
echo 20 >"$R/proc/sys/vm/compaction_proactiveness"
echo 3 >"$R/sys/bus/workqueue/devices/writeback/cpumask"
: >"$TASTY_TEST_LOG"
TASTY_TUNE_ROOT=$R; export TASTY_TUNE_ROOT
run play gamma >/dev/null 2>"$T/tune.err"
expect "during a play compaction is off and writeback is on CPU 0" has "$TASTY_TEST_LOG" "tune 0 0 1"
expect "after a play the old compaction values are back" test "$(cat "$R/proc/sys/vm/compact_unevictable_allowed") $(cat "$R/proc/sys/vm/compaction_proactiveness")" = "1 20"
expect "after a play the old writeback mask is back" test "$(cat "$R/sys/bus/workqueue/devices/writeback/cpumask")" = 3
echo 0 >"$R/proc/sys/vm/compact_unevictable_allowed"; echo 0 >"$R/proc/sys/vm/compaction_proactiveness"; echo 00000001 >"$R/sys/bus/workqueue/devices/writeback/cpumask"
run play gamma >/dev/null 2>&1
expect "values already as wanted are left exactly as they were" test "$(cat "$R/sys/bus/workqueue/devices/writeback/cpumask")" = 00000001
echo 00000000,00000001 >"$R/sys/bus/workqueue/devices/writeback/cpumask"
run play gamma >/dev/null 2>&1
expect "a comma-grouped mask already as wanted is left alone" test "$(cat "$R/sys/bus/workqueue/devices/writeback/cpumask")" = 00000000,00000001
rm -rf "$R/sys"
run play gamma >/dev/null 2>"$T/tune2.err"; echo $? >"$T/tune2.rc"
expect "a kernel without the writeback mask still plays" test "$(cat "$T/tune2.rc")" -eq 0
expect "no tuning warning when a knob is absent" test ! -s "$T/tune2.err"
unset TASTY_TUNE_ROOT

# --- the menu, driven from stdin --------------------------------------------------------------------------------
: >"$TASTY_TEST_LOG"
printf '1\n1\n\n0\n' | run >"$T/menu.out" 2>&1
expect "the menu lists the ready count" has "$T/menu.out" "Play a TAS ("
expect "the menu offers the not-found list" has "$T/menu.out" "Not found ("
printf '2\n\n6\n' | run >"$T/menu2.out" 2>&1
expect "the not-found screen names the ROM to bring" has "$T/menu2.out" "Nu Nowhere.nes"

echo "tasty menu tests: $((checks - fails))/$checks passed (shell: $SHELL_UNDER_TEST)"
[ "$fails" -eq 0 ]
