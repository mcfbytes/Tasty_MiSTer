#!/bin/sh
# Try MiSTer Tasty: download tasty and a known-good NES movie, find your SMB ROM, check it, and play.
# Usage: curl -fsSL https://raw.githubusercontent.com/mcfbytes/Tasty_MiSTer/main/test-tasty.sh | sh
#    or: sh test-tasty.sh "/path/to/Super Mario Bros. (World).nes"
# It never downloads a ROM: it looks for your own copy and checks it before anything runs.
# For testing: TASTY_URL and MOVIE_URL override the downloads (file:// works), and
# TASTY_PLAY_ARGS adds options to the final `tasty play`.
set -eu

DIR=/media/fat/tasty
BIN="$DIR/tasty"
MOVIE="$DIR/movies/klmz3-smb.fm2"
TASTY_URL=${TASTY_URL:-https://github.com/mcfbytes/Tasty_MiSTer/releases/latest/download/tasty}
MOVIE_URL=${MOVIE_URL:-'https://tasvideos.org/1330M?handler=Download'}

say() { printf '\033[1;35mtasty:\033[0m %s\n' "$*"; }
die() { printf '\033[1;31mtasty:\033[0m %s\n' "$*" >&2; exit 1; }

[ -d /media/fat ] || die "run this on the MiSTer itself (ssh root@<your-mister>)"
command -v curl >/dev/null || die "curl is missing"
command -v unzip >/dev/null || die "unzip is missing"

# Super Mario Bros. (World): the MD5 of the ROM without its 16-byte iNES header. It is the checksum the movie itself records
# (romChecksum), so any correctly dumped copy matches whatever its file name or header flavour.
SMB_MD5=8e3630186e35d477231bf8fd50e54cdd
rom_md5() { tail -c +17 "$1" | md5sum | cut -d' ' -f1; }

# The ROM first: nothing is downloaded or stopped until your copy checks out.
ROM="${1:-}"
if [ -z "$ROM" ]; then
    say "looking for your Super Mario Bros. (World) ROM under /media/fat/games/NES"
    for f in $(find /media/fat/games/NES -iname '*.nes' -size 40976c 2>/dev/null | tr ' ' '\001'); do
        f=$(printf '%s' "$f" | tr '\001' ' ')
        if [ "$(rom_md5 "$f")" = "$SMB_MD5" ]; then ROM=$f; break; fi
    done
    [ -n "$ROM" ] || die "no matching Super Mario Bros. (World) ROM found under /media/fat/games/NES (MD5 without header: $SMB_MD5). Pass its path: sh test-tasty.sh \"/path/to/rom.nes\""
fi
[ -f "$ROM" ] || die "not found: $ROM"
[ "$(rom_md5 "$ROM")" = "$SMB_MD5" ] || die "$ROM is not Super Mario Bros. (World) (its MD5 without header isn't $SMB_MD5)"
say "found $ROM"

mkdir -p "$DIR/movies"

say "downloading tasty"
curl -fL --retry 3 -o "$BIN.new" "$TASTY_URL" || die "could not download tasty"
chmod +x "$BIN.new" && mv "$BIN.new" "$BIN"

if [ ! -f "$MOVIE" ]; then
    say "downloading the Super Mario Bros. movie (TASVideos 1330M, by klmz)"
    curl -fL --retry 3 -o "$DIR/movies/1330M.zip" "$MOVIE_URL" || die "could not download the movie"
    unzip -o -q "$DIR/movies/1330M.zip" -d "$DIR/movies" && rm -f "$DIR/movies/1330M.zip"
fi
[ -f "$MOVIE" ] || die "the movie zip did not contain klmz3-smb.fm2"

say "double-checking the ROM against the movie"
"$BIN" check "$MOVIE" --rom "$ROM" || die "tasty check failed"

say "playing: the MiSTer menu comes back 30 seconds after the run ends (tasty stop ends it early)"
# shellcheck disable=SC2086
exec "$BIN" play "$MOVIE" --rom "$ROM" ${TASTY_PLAY_ARGS:-}
