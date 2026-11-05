#!/bin/sh
# MiSTer Tasty menu: finds the ROMs you already own (loose or zipped), checks them by hash against the TAS
# catalogue, and plays the movies you can run. It never downloads a ROM; only tasvideos.org movie files.
# Run it from the MiSTer Scripts menu, or from ssh: Tasty.sh [menu|scan|list|play <id>|stop|update|help]
# Concurrent runs share a scan cache and are not supported.
# For testing: TASTY_DIR, TASTY_GAMES (newline-separated roots), TASTY_UI=text, TASTY_PLAY_ARGS,
# TASTY_URL (tasty binary), TASTY_SHA_URL (its published sha256), TASTY_CATALOG_URL, TASTY_MOVIE_URL_BASE override the defaults.

TASTY_DIR=${TASTY_DIR:-/media/fat/tasty}
BIN=${TASTY_BIN:-$TASTY_DIR/tasty}
MOVIES=$TASTY_DIR/movies
CATALOG=$TASTY_DIR/tas-catalog.tsv
LOCALCAT=$TASTY_DIR/catalog.local.tsv
CACHE=$TASTY_DIR/scan-cache.tsv
TASTY_URL=${TASTY_URL:-https://github.com/mcfbytes/Tasty_MiSTer/releases/latest/download/tasty}
TASTY_SHA_URL=${TASTY_SHA_URL:-https://github.com/mcfbytes/Tasty_MiSTer/releases/latest/download/tasty.sha256}
TASTY_CATALOG_URL=${TASTY_CATALOG_URL:-https://raw.githubusercontent.com/mcfbytes/Tasty_MiSTer/main/data/tas-catalog.tsv}
US=$(printf '\037')
SELF=$0

WORK=$(mktemp -d "${TMPDIR:-/tmp}/tasty-menu.XXXXXX") || exit 1
cleanup() { [ -s "$WORK/tune.saved" ] && tune_restore; rm -rf "$WORK"; }
trap 'cleanup' EXIT
trap 'cleanup; exit 130' INT TERM HUP

say() { printf '%s\n' "$*"; }
progress() { [ -t 2 ] && printf '%s\n' "$*" >&2; return 0; }
warn() { printf 'tasty: %s\n' "$*" >&2; }

INFOZIP=0
case $(unzip 2>&1 | head -n 3) in *Info-ZIP*) INFOZIP=1 ;; esac
HAVE_UNZIP=0
command -v unzip >/dev/null 2>&1 && HAVE_UNZIP=1
PLUS=0
find "$WORK" -maxdepth 0 -exec true {} + >/dev/null 2>&1 && PLUS=1

UI=text
if [ "${TASTY_UI:-}" != text ] && [ -t 1 ] && command -v dialog >/dev/null 2>&1; then UI=dialog; fi

# ---------------------------------------------------------------- user interface

shq() { printf '%s' "$1" | sed "s/'/'\\\\''/g"; }

# ui_menu <title> <items-file>: items are "tag<US>text" lines; sets CHOICE (empty on cancel)
ui_menu() {
    CHOICE=
    if [ "$UI" = dialog ]; then
        args=
        while IFS=$US read -r tag text; do
            args="$args '$(shq "$tag")' '$(shq "$text")'"
        done <"$2"
        CHOICE=$(eval "dialog --stdout --no-tags --title 'Tasty' --menu '$(shq "$1")' 22 76 14 $args")
        return 0
    fi
    say ""
    say "== $1"
    n=0
    while IFS=$US read -r tag text; do
        n=$((n + 1))
        printf '%3d) %s\n' "$n" "$text"
    done <"$2"
    printf 'choose (blank to go back): '
    read -r pick || return 0
    case $pick in
        '' | *[!0-9]*) return 0 ;;
    esac
    [ "$pick" -ge 1 ] && [ "$pick" -le "$n" ] || return 0
    CHOICE=$(sed -n "${pick}p" "$2" | cut -d"$US" -f1)
}

# ui_text <title> <file>
ui_text() {
    if [ "$UI" = dialog ]; then
        dialog --title "$1" --exitlabel Back --textbox "$2" 22 76
    else
        say ""
        say "== $1"
        cat "$2"
    fi
}

ui_msg() {
    if [ "$UI" = dialog ]; then dialog --title Tasty --msgbox "$1" 10 64; else say "$1"; fi
}

# ---------------------------------------------------------------- catalogue

# The catalogue is tab-separated; it is held here with a unit separator so empty columns survive `read`.
load_catalog() {
    : >"$WORK/cat.tsv"
    for f in "$CATALOG" "$LOCALCAT"; do
        [ -f "$f" ] || continue
        grep -v '^#' "$f" | grep -v '^id	' | grep -v '^[[:space:]]*$' | tr '\t' '\037' >>"$WORK/cat.tsv"
    done
    [ -s "$WORK/cat.tsv" ]
}

# Columns: id platform title aliases region rom_name hash_kind rom_hash layout movie_page movie_url movie_sha1 core play_args status note
# shellcheck disable=SC2034
cat_row() {
    line=$(awk -F"$US" -v id="$1" '$1 == id { print; exit }' "$WORK/cat.tsv")
    [ -n "$line" ] || return 1
    IFS=$US read -r ROW_ID ROW_PLAT ROW_TITLE ROW_ALIAS ROW_REGION ROW_ROM ROW_KIND ROW_HASH ROW_LAYOUT \
        ROW_PAGE ROW_URL ROW_MSHA ROW_CORE ROW_ARGS ROW_STATUS ROW_NOTE <<EOT
$line
EOT
}

# ---------------------------------------------------------------- scanning

# plat_dirs / plat_exts: where a platform's games live in the MiSTer tree and what its ROMs are called
plat_dirs() {
    case $1 in
        NES) echo "NES Famicom" ;;
        SNES) echo "SNES SuperFamicom" ;;
        MegaDrive) echo "MegaDrive Genesis" ;;
        *) echo "" ;;
    esac
}
plat_exts() {
    case $1 in
        NES) echo "nes" ;;
        SNES) echo "sfc smc swc fig" ;;
        MegaDrive) echo "md gen smd bin" ;;
        *) echo "" ;;
    esac
}

game_roots() {
    if [ -n "${TASTY_GAMES:-}" ]; then
        printf '%s\n' "$TASTY_GAMES"
        return
    fi
    for r in /media/fat/games /media/usb*/games /media/fat/cifs/games /media/network/games; do
        [ -d "$r" ] && printf '%s\n' "$r"
    done
}

# list_files <platform>: "path<US>size<US>mtime" for the platform's ROM files and zips
list_files() {
    exts="$(plat_exts "$1") zip"
    game_roots | while IFS= read -r root; do
        for d in $(plat_dirs "$1"); do
            find "$root" -maxdepth 1 -type d -iname "$d" 2>/dev/null
        done
    done | while IFS= read -r dir; do
        for e in $exts; do
            if [ "$PLUS" = 1 ]; then
                find "$dir" -maxdepth 6 -type f -iname "*.$e" -exec stat -c "%n${US}%s${US}%Y" {} + 2>/dev/null
            else
                find "$dir" -maxdepth 6 -type f -iname "*.$e" 2>/dev/null | while IFS= read -r f; do
                    stat -c "%n${US}%s${US}%Y" "$f" 2>/dev/null
                done
            fi
        done
    done
}

# list_zip <zip>: "size<US>member" per file inside, from either Info-ZIP or busybox unzip
list_zip() {
    unzip -l "$1" 2>/dev/null | awk '
        /^ *-+ / { n++; next }
        n == 1 { name = $0; sub(/^ *[0-9]+ +[^ ]+ +[^ ]+ +/, "", name)
                 if (name !~ /\/$/) print $1 "\037" name }'
}

# NORM: awk functions shared by the title filters; norm() folds a title to lowercase alphanumerics, dropping
# (tags) and [tags], so "Zelda II - The Adventure of Link (USA) (Rev 1)" and "Zelda II: ..." compare equal
NORM='
function norm(s) {
    s = tolower(s)
    gsub(/&/, "and", s)
    gsub(/\([^)]*\)/, "", s)
    gsub(/\[[^]]*\]/, "", s)
    sub(/[ \t]+$/, "", s)
    if (match(s, /, *the$/)) s = "the " substr(s, 1, RSTART - 1)
    gsub(/[^a-z0-9]/, "", s)
    return s
}
function stem(p) { sub(/^.*\//, "", p); sub(/\.[^.]*$/, "", p); return p }
function load_titles(catf, plat,   line, c, al, k, j) {
    while ((getline line < catf) > 0) {
        split(line, c, "\037")
        if (c[2] != plat) continue
        k = split(c[3] ";" c[4], al, ";")
        for (j = 1; j <= k; j++) if (length(norm(al[j])) >= 3) { nn++; id[nn] = c[1]; nm[nn] = norm(al[j]) }
    }
}
function titled(s,   i) { s = norm(s); for (i = 1; i <= nn; i++) if (index(s, nm[i])) return 1; return 0 }
'

# zip_members <files-list> <platform>: "zip<US>size<US>mtime<US>member<US>msize" for every zip whose name holds a
# catalogue title for <platform>; only those zips are opened, and listings are cached by zip path+size+mtime
zip_members() {
    awk -F"$US" -v plat="$2" -v catf="$WORK/cat.tsv" "$NORM"'
        BEGIN { load_titles(catf, plat) }
        tolower($1) ~ /\.zip$/ && titled(stem($1))' "$1" >"$WORK/zips.tsv"
    [ -s "$WORK/zips.tsv" ] || return 0
    [ "$HAVE_UNZIP" = 1 ] || return 0
    [ -f "$CACHE" ] || : >"$CACHE"
    awk -F"$US" -v OFS="$US" 'FILENAME == ARGV[1] { c[$2 FS $3 FS $4] = 1; next } !(($1 FS $2 FS $3) in c)' \
        "$CACHE" "$WORK/zips.tsv" >"$WORK/zips.new"
    nz=0
    while IFS=$US read -r zp zs zm; do
        nz=$((nz + 1))
        [ $((nz % 250)) -eq 0 ] && progress "  listed $nz new zips..."
        got=0
        list_zip "$zp" >"$WORK/zl.tsv"
        while IFS=$US read -r msz mem; do
            got=1
            printf 'L%s%s%s%s%s%s%s%s%s%s\n' "$US" "$zp" "$US" "$zs" "$US" "$zm" "$US" "$mem" "$US" "$msz" >>"$CACHE"
        done <"$WORK/zl.tsv"
        # a zip that lists nothing is remembered so it is not retried every run
        [ "$got" = 1 ] || printf 'L%s%s%s%s%s%s%s%s%s%s\n' "$US" "$zp" "$US" "$zs" "$US" "$zm" "$US" "" "$US" "-1" >>"$CACHE"
    done <"$WORK/zips.new"
    awk -F"$US" -v OFS="$US" 'FILENAME == ARGV[1] { c[$1 FS $2 FS $3] = 1; next }
                              $1 == "L" && (($2 FS $3 FS $4) in c) && $5 != "" { print $2, $3, $4, $5, $6 }' \
        "$WORK/zips.tsv" "$CACHE"
}

# name_match <platform>: reads files.tsv/zipmem.tsv, writes "id<US>path<US>member<US>size<US>mtime" name matches
name_match() {
    exts=$(plat_exts "$1")
    awk -F"$US" -v OFS="$US" -v plat="$1" -v exts="$exts" -v catf="$WORK/cat.tsv" "$NORM"'
        function ext(p) { if (match(p, /\.[^.\/]*$/)) return tolower(substr(p, RSTART + 1)); return "" }
        function isrom(p,   i) { for (i = 1; i <= ne; i++) if (ext(p) == el[i]) return 1; return 0 }
        function try(path, member, size, mtime, n1, n2,   i) {
            n1 = norm(n1); n2 = norm(n2)
            for (i = 1; i <= nn; i++)
                if (index(n1, nm[i]) || index(n2, nm[i])) print id[i], path, member, size, mtime
        }
        BEGIN { ne = split(exts, el, " "); load_titles(catf, plat) }
        FILENAME == ARGV[1] { if (ext($1) != "zip" && isrom($1)) try($1, "", $2, $3, stem($1), stem($1)); next }
        { if (isrom($4)) try($1, $4, $5, $3, stem($4), stem($1)) }
    ' "$WORK/files.tsv" "$WORK/zipmem.tsv"
}

src() {
    if [ -n "$2" ]; then
        # both unzips glob the member name, so it is escaped; busybox takes no "--" (the zip path is absolute)
        m=$(printf '%s' "$2" | sed 's/[][*?\\]/\\&/g; s/^-/\\-/')
        if [ "$INFOZIP" = 1 ]; then unzip -p -- "$1" "$m" 2>/dev/null; else unzip -p "$1" "$m" 2>/dev/null; fi
    else
        cat "$1"
    fi
}

hsum() {
    case $1 in
        md5) md5sum ;;
        sha1) sha1sum ;;
        *) cat >/dev/null ;;
    esac | cut -d' ' -f1
}

# rom_variants <path> <member> <size> <layout> <kind>: the digests tasty's own check would accept for this file
rom_variants() {
    rp=$1 rmem=$2 rs=$3 lay=$4 kind=$5
    case $lay in
        plain) src "$rp" "$rmem" | hsum "$kind" ;;
        copier)
            rem=$((rs % 1024))
            skip=0
            case $rem in 128 | 512) skip=$rem ;; esac
            src "$rp" "$rmem" | tail -c +$((skip + 1)) | hsum "$kind"
            ;;
        ines_body)
            # shellcheck disable=SC2046
            set -- $(src "$rp" "$rmem" | head -c 16 | od -An -tu1)
            [ $# -ge 16 ] || return 0
            [ "$1 $2 $3 $4" = "78 69 83 26" ] || return 0
            nes2=0
            [ $(($8 & 12)) -eq 8 ] && nes2=1
            phi=0 chi=0
            if [ $nes2 = 1 ]; then
                phi=$((${10} & 15))
                chi=$((${10} >> 4))
            fi
            [ $phi -ne 15 ] && [ $chi -ne 15 ] || return 0
            prg=$((((phi << 8) | $5) * 16384))
            chr=$((((chi << 8) | $6) * 8192))
            off=16
            [ $(($7 & 4)) -ne 0 ] && off=528
            tot=$((prg + chr))
            [ $tot -gt 0 ] && [ $((off + tot)) -le "$rs" ] || return 0
            src "$rp" "$rmem" | tail -c +$((off + 1)) | head -c $tot | hsum "$kind"
            ;;
        ines_file)
            rem=$((rs % 1024))
            skip=0
            case $rem in 128 | 512) skip=$rem ;; esac
            if [ "$skip" -eq 0 ]; then
                # shellcheck disable=SC2046
                set -- $(src "$rp" "$rmem" | head -c 16 | od -An -tu1)
            else
                set --
            fi
            src "$rp" "$rmem" | tail -c +$((skip + 1)) | hsum "$kind"
            if [ $# -ge 16 ] && [ "$1 $2 $3 $4" = "78 69 83 26" ]; then
                id=$(($8 & 12))
                archaic=0
                if [ $id -eq 4 ] || [ $id -eq 12 ]; then archaic=1; fi
                if [ $id -eq 0 ] && [ $((${13} | ${14} | ${15} | ${16})) -ne 0 ]; then archaic=1; fi
                b7=0
                [ $archaic -eq 0 ] && b7=$(($8 & 243))
                if [ $(((b7 ^ $8) | $9 | ${10} | ${11} | ${12} | ${13} | ${14} | ${15} | ${16})) -ne 0 ]; then
                    canon=$(printf '\\0%03o' "$1" "$2" "$3" "$4" "$5" "$6" "$7" "$b7" 0 0 0 0 0 0 0 0)
                    { printf "%b" "$canon"; src "$rp" "$rmem" | tail -c +17; } | hsum "$kind"
                fi
            fi
            ;;
    esac
}

# rom_hashes: cached rom_variants, keyed by path+member+size+mtime+layout+kind
rom_hashes() {
    hp=$1 hm=$2 hs=$3 ht=$4 hl=$5 hk=$6
    [ -f "$CACHE" ] || : >"$CACHE"
    cached=$(HP=$hp HM=$hm awk -F"$US" -v s="$hs" -v t="$ht" -v l="$hl" -v k="$hk" \
        '$1 == "H" && $2 == ENVIRON["HP"] && $3 == ENVIRON["HM"] && $4 == s && $5 == t && $6 == l && $7 == k { print $8; exit }' "$CACHE")
    if [ -n "$cached" ]; then
        printf '%s\n' "$cached"
        return 0
    fi
    # an unreadable member is not hashed or cached, so a later run with a working unzip retries it
    if [ -n "$hm" ]; then
        case $(src "$hp" "$hm" | head -c 1 | wc -c) in *1) ;; *) return 0 ;; esac
    fi
    out=$(rom_variants "$hp" "$hm" "$hs" "$hl" "$hk" | tr '\n' ' ')
    printf 'H%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n' "$US" "$hp" "$US" "$hm" "$US" "$hs" "$US" "$ht" "$US" "$hl" "$US" "$hk" "$US" "$out" >>"$CACHE"
    printf '%s\n' "$out"
}

# cache_version: a cache written by an older Tasty.sh is dropped and rebuilt
CACHE_VERSION='#tasty-scan-cache 2'
cache_version() {
    [ -f "$CACHE" ] && [ "$(head -n 1 "$CACHE")" = "$CACHE_VERSION" ] && return 0
    printf '%s\n' "$CACHE_VERSION" >"$CACHE"
}

# prune_cache: drop cache rows whose file is gone
prune_cache() {
    [ -s "$CACHE" ] || return 0
    cut -s -d"$US" -f2 "$CACHE" | sort -u | while IFS= read -r p; do
        [ -e "$p" ] || printf '%s\n' "$p"
    done >"$WORK/dead.txt"
    [ -s "$WORK/dead.txt" ] || return 0
    awk -F"$US" 'FILENAME == ARGV[1] { dead[$0] = 1; next } !($2 in dead)' "$WORK/dead.txt" "$CACHE" >"$WORK/cache.new" &&
        cat "$WORK/cache.new" >"$CACHE"
}

# scan: writes ready.tsv ("id<US>path<US>member") and missing.tsv (ids)
scan() {
    cache_version
    load_catalog || { warn "no catalogue at $CATALOG (run: $SELF update)"; return 1; }
    : >"$WORK/ready.tsv"
    : >"$WORK/missing.tsv"
    # shellcheck disable=SC2013
    for plat in $(cut -d"$US" -f2 "$WORK/cat.tsv" | sort -u); do
        if [ -z "$(plat_exts "$plat")" ]; then continue; fi
        progress "scanning $plat games..."
        list_files "$plat" >"$WORK/files.tsv"
        zip_members "$WORK/files.tsv" "$plat" >"$WORK/zipmem.tsv"
        name_match "$plat" >"$WORK/match.tsv"
        while IFS=$US read -r mid mp mm ms mt; do
            grep -q "^$mid$US" "$WORK/ready.tsv" && continue
            cat_row "$mid" || continue
            hashes=$(rom_hashes "$mp" "$mm" "$ms" "$mt" "$ROW_LAYOUT" "$ROW_KIND")
            for h in $hashes; do
                if [ "$h" = "$ROW_HASH" ]; then
                    printf '%s%s%s%s%s\n' "$mid" "$US" "$mp" "$US" "$mm" >>"$WORK/ready.tsv"
                    break
                fi
            done
        done <"$WORK/match.tsv"
    done
    prune_cache
    cut -d"$US" -f1 "$WORK/cat.tsv" | while IFS= read -r i; do
        grep -q "^$i$US" "$WORK/ready.tsv" || printf '%s\n' "$i" >>"$WORK/missing.tsv"
    done
    return 0
}

describe() {
    cat_row "$1" || return 1
    r=
    [ -n "$ROW_REGION" ] && r=" ($ROW_REGION)"
    printf '%s: %s%s' "$ROW_PLAT" "$ROW_TITLE" "$r"
    [ "$ROW_STATUS" = verified ] || printf ' [%s]' "$ROW_STATUS"
}

# ---------------------------------------------------------------- movies

FETCH_PROTO=https
[ "${TASTY_ALLOW_FILE:-}" = 1 ] && FETCH_PROTO=https,file

fetch() {
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL --proto "=$FETCH_PROTO" --proto-redir "=$FETCH_PROTO" --connect-timeout 15 --max-time 120 \
            --retry 3 -o "$2" "$1"
    elif command -v wget >/dev/null 2>&1; then
        case $1 in
            https://*) ;;
            file://*) [ "${TASTY_ALLOW_FILE:-}" = 1 ] || return 1 ;;
            *) return 1 ;;
        esac
        wget -q -T 20 -O "$2" "$1"
    else
        warn "neither curl nor wget is available"
        return 1
    fi
}

movie_ext() {
    case $(printf '%s' "$1" | tr '[:upper:]' '[:lower:]') in
        *.fm2 | *.bk2 | *.lsmv | *.gmv) return 0 ;;
    esac
    return 1
}

# ensure_movie: sets MOVIE_FILE to the verified movie, downloading it from tasvideos.org when needed
ensure_movie() {
    MOVIE_FILE=
    tv=${ROW_PAGE##*/}
    case $ROW_MSHA in
        *[!0-9a-f]* | '') warn "$ROW_ID has no movie SHA-1 in the catalogue; not playing it"; return 1 ;;
    esac
    [ "${#ROW_MSHA}" -eq 40 ] || { warn "$ROW_ID has a malformed movie SHA-1; not playing it"; return 1; }
    mkdir -p "$MOVIES" || return 1
    for f in "$MOVIES/$tv"-*; do
        [ -f "$f" ] || continue
        if [ "$(sha1sum <"$f" | cut -d' ' -f1)" = "$ROW_MSHA" ]; then
            MOVIE_FILE=$f
            return 0
        fi
    done
    say "downloading the movie ($tv) from tasvideos.org"
    dl=$WORK/dl
    rm -rf "$dl"
    mkdir -p "$dl"
    url=$ROW_URL
    case $url in
        https://tasvideos.org/*) ;;
        *) warn "$ROW_ID: the movie URL is not on tasvideos.org; not downloading it"; return 1 ;;
    esac
    [ -n "${TASTY_MOVIE_URL_BASE:-}" ] && url=$TASTY_MOVIE_URL_BASE/$tv
    fetch "$url" "$dl/movie.bin" || { warn "could not download $url"; return 1; }
    [ "$(head -c 2 "$dl/movie.bin")" = PK ] || { warn "the movie download is not a zip file"; return 1; }
    [ "$HAVE_UNZIP" = 1 ] || { warn "unzip is missing"; return 1; }
    list_zip "$dl/movie.bin" >"$dl/members.tsv"
    mem=
    mext=
    while IFS=$US read -r msz name; do
        movie_ext "$name" || continue
        [ "$msz" -le 67108864 ] 2>/dev/null || continue
        mem=$name
        mext=$(printf '%s' "$name" | sed 's/^.*\.//' | tr '[:upper:]' '[:lower:]')
        break
    done <"$dl/members.tsv"
    [ -n "$mem" ] || { warn "the download holds no movie file"; return 1; }
    found=$dl/movie.$mext
    src "$dl/movie.bin" "$mem" >"$found" || { warn "could not unpack the movie"; return 1; }
    [ -s "$found" ] || { warn "the unpacked movie is empty"; return 1; }
    if [ "$(sha1sum <"$found" | cut -d' ' -f1)" != "$ROW_MSHA" ]; then
        warn "the downloaded movie does not match the catalogue (TASVideos may have replaced it); not playing it"
        return 1
    fi
    MOVIE_FILE=$MOVIES/$tv-$(basename "$mem")
    cp "$found" "$MOVIE_FILE" || return 1
    return 0
}

# ---------------------------------------------------------------- play

# check_args <options...>: only these tasty play options may come from a catalogue row
check_args() {
    while [ $# -gt 0 ]; do
        case $1 in
            --stay | --no-splash | --strict | --loop) shift ;;
            --lead | --phase | --stop-at | --linger | --vsync-adjust)
                case ${2:-x} in
                    -[0-9]* | [0-9]*) ;;
                    *) return 1 ;;
                esac
                case ${2#-} in *[!0-9]*) return 1 ;; esac
                shift 2 ;;
            --ram-init)
                case ${2:-} in zero | ff | random) ;; *) return 1 ;; esac
                shift 2 ;;
            *) return 1 ;;
        esac
    done
}

play_row() {
    pid=$1
    load_catalog || { warn "no catalogue"; return 1; }
    cat_row "$pid" || { warn "no such entry: $pid"; return 1; }
    [ -x "$BIN" ] || { warn "tasty is not installed at $BIN (run: $SELF update)"; return 1; }
    if [ ! -s "$WORK/ready.tsv" ]; then scan || return 1; fi
    hit=$(awk -F"$US" -v id="$pid" '$1 == id { print; exit }' "$WORK/ready.tsv")
    [ -n "$hit" ] || { warn "no matching ROM was found for $pid"; return 1; }
    rp=$(printf '%s' "$hit" | cut -d"$US" -f2)
    rm_=$(printf '%s' "$hit" | cut -d"$US" -f3)
    ensure_movie || return 1
    rom=$rp
    if [ -n "$rm_" ]; then
        mkdir -p "$WORK/rom" || return 1
        rom=$WORK/rom/$(basename "$rm_")
        src "$rp" "$rm_" >"$rom" || { warn "could not unpack the ROM from $rp"; return 1; }
    fi
    set -f
    # shellcheck disable=SC2086
    set -- $ROW_ARGS
    set +f
    check_args "$@" || { warn "$pid: the catalogue row has play options this script does not allow"; return 1; }
    "$BIN" check "$MOVIE_FILE" --rom "$rom" || { warn "tasty check refused this ROM for the movie"; return 1; }
    say "playing $(describe "$pid")"
    tune_for_play
    # shellcheck disable=SC2086
    "$BIN" play "$MOVIE_FILE" --rom "$rom" "$@" ${TASTY_PLAY_ARGS:-}
    rc=$?
    tune_restore
    rm -rf "$WORK/rom"
    return $rc
}

# Run tuning: during a play, kernel memory compaction stays off and writeback stays on CPU 0, away from the
# replay's real-time CPU. Only a value that differs is changed, and every changed value is put back afterwards.
TUNE_ROOT=${TASTY_TUNE_ROOT:-}
tune_one() { # tune_one <path> <wanted>
    tp=$TUNE_ROOT$1
    [ -r "$tp" ] && [ -w "$tp" ] || return 0
    old=$(cat "$tp" 2>/dev/null) || return 0
    [ "$(printf '%s' "$old" | sed 's/,//g; s/^0*//')" = "$(printf '%s' "$2" | sed 's/,//g; s/^0*//')" ] && return 0
    if printf '%s\n' "$2" >"$tp" 2>/dev/null; then
        printf '%s%s%s\n' "$tp" "$US" "$old" >>"$WORK/tune.saved"
    else
        warn "could not set $1 to $2 (left at $old)"
    fi
}
tune_for_play() {
    tune_one /proc/sys/vm/compact_unevictable_allowed 0
    tune_one /proc/sys/vm/compaction_proactiveness 0
    tune_one /sys/bus/workqueue/devices/writeback/cpumask 1
}
tune_restore() {
    [ -s "$WORK/tune.saved" ] || return 0
    while IFS=$US read -r tp old; do
        printf '%s\n' "$old" >"$tp" 2>/dev/null || warn "could not restore $tp to $old"
    done <"$WORK/tune.saved"
    rm -f "$WORK/tune.saved"
}

stop_play() {
    [ -x "$BIN" ] || { warn "tasty is not installed at $BIN"; return 1; }
    "$BIN" stop
}

# file_sha <file>: the sha256 of a file (BusyBox sha256sum is on the card)
file_sha() { sha256sum <"$1" | cut -d' ' -f1; }

# published_sha <tmp>: print the published sha256 of the release binary; fail when it can't be fetched or isn't 64 hex
published_sha() {
    rm -f "$1"
    fetch "$TASTY_SHA_URL" "$1" 2>/dev/null || { rm -f "$1"; return 1; }
    h=
    read -r h _ <"$1" || :
    rm -f "$1"
    case $h in *[!0-9a-f]*) return 1 ;; esac
    [ "${#h}" -eq 64 ] || return 1
    printf '%s\n' "$h"
}

# install_download <url> <dest> <kind> [sha256]: fetch next to <dest> (same filesystem), check it, then rename over <dest>
install_download() {
    new=$2.new
    rm -f "$new"
    if ! fetch "$1" "$new"; then rm -f "$new"; return 1; fi
    [ -s "$new" ] || { rm -f "$new"; return 1; }
    if [ -n "${4:-}" ] && [ "$(file_sha "$new")" != "$4" ]; then
        warn "the download does not match its published sha256"
        rm -f "$new"; return 1
    fi
    case $3 in
        elf) [ "$(head -c 4 "$new" | od -An -tx1 | tr -d ' \n')" = 7f454c46 ] || { rm -f "$new"; return 1; }
             chmod 755 "$new" || { rm -f "$new"; return 1; } ;;
        catalog)
            head -n 40 "$new" | grep -q '^id	platform	' || { rm -f "$new"; return 1; }
            [ "$(grep -v '^#' "$new" | grep -v '^id	' | grep -c '[^[:space:]]')" -ge 1 ] || { rm -f "$new"; return 1; }
            chmod 644 "$new" || { rm -f "$new"; return 1; } ;;
    esac
    mv "$new" "$2" || { rm -f "$new"; return 1; }
}

# update_binary: download tasty only when the published sha256 differs from the installed one
update_binary() {
    want=$(published_sha "$BIN.sha") || want=
    if [ -n "$want" ] && [ -f "$BIN" ] && [ "$(file_sha "$BIN")" = "$want" ]; then
        say "tasty is up to date"
        return 0
    fi
    install_download "$TASTY_URL" "$BIN" elf "$want"
}

update() {
    mkdir -p "$TASTY_DIR" "$MOVIES" || return 1
    upd_rc=0
    say "updating the catalogue"
    install_download "$TASTY_CATALOG_URL" "$CATALOG" catalog || { warn "could not update the catalogue (the old one is kept)"; upd_rc=1; }
    say "updating tasty"
    mkdir -p "$(dirname "$BIN")" || return 1
    update_binary || { warn "could not update tasty (the old one is kept)"; upd_rc=1; }
    return $upd_rc
}

# ---------------------------------------------------------------- commands

cmd_scan() {
    scan || return 1
    while IFS=$US read -r i p m; do
        t=$p
        [ -n "$m" ] && t="$p::$m"
        printf 'ready\t%s\t%s\t%s\n' "$i" "$(describe "$i")" "$t"
    done <"$WORK/ready.tsv"
    while IFS= read -r i; do
        cat_row "$i" || continue
        printf 'missing\t%s\t%s\t%s\n' "$i" "$(describe "$i")" "$ROW_ROM"
    done <"$WORK/missing.tsv"
}

menu_play() {
    : >"$WORK/items"
    while IFS=$US read -r i p m; do
        printf '%s%s%s\n' "$i" "$US" "$(describe "$i")" >>"$WORK/items"
    done <"$WORK/ready.tsv"
    if [ ! -s "$WORK/items" ]; then
        ui_msg "None of the catalogue's movies can be played yet: no matching ROM was found. See 'Not found'."
        return
    fi
    ui_menu "Pick a movie to play" "$WORK/items"
    [ -n "$CHOICE" ] || return
    play_row "$CHOICE"
    printf 'press enter to continue '
    read -r _ || true
}

menu_missing() {
    : >"$WORK/missing.txt"
    say "Your own ROM is needed for these; put it under games/<system> (a zip is fine)." >"$WORK/missing.txt"
    say "" >>"$WORK/missing.txt"
    while IFS= read -r i; do
        cat_row "$i" || continue
        printf '%s\n    ROM: %s\n' "$(describe "$i")" "$ROW_ROM" >>"$WORK/missing.txt"
    done <"$WORK/missing.tsv"
    ui_text "Not found" "$WORK/missing.txt"
}

cmd_menu() {
    if ! load_catalog; then
        warn "no catalogue yet; fetching it"
        update
        load_catalog || { warn "no catalogue available"; return 1; }
    fi
    say "scanning your ROMs..."
    scan || return 1
    while :; do
        np=$(wc -l <"$WORK/ready.tsv" | tr -d ' ')
        nm=$(wc -l <"$WORK/missing.tsv" | tr -d ' ')
        {
            printf 'play%sPlay a TAS (%s ready)\n' "$US" "$np"
            printf 'missing%sNot found (%s)\n' "$US" "$nm"
            printf 'rescan%sRescan my ROMs\n' "$US"
            printf 'stop%sStop playback\n' "$US"
            printf 'update%sUpdate tasty and the catalogue\n' "$US"
            printf 'quit%sQuit\n' "$US"
        } >"$WORK/top"
        ui_menu "MiSTer Tasty" "$WORK/top"
        case $CHOICE in
            play) menu_play ;;
            missing) menu_missing ;;
            rescan) say "scanning your ROMs..."; scan ;;
            stop) stop_play ;;
            update) update; load_catalog; scan ;;
            quit | '') return 0 ;;
        esac
    done
}

usage() {
    say "usage: $SELF [menu|scan|list|play <id>|stop|update|help]"
}

case ${1:-menu} in
    menu) cmd_menu ;;
    scan | list) cmd_scan ;;
    play)
        [ -n "${2:-}" ] || { usage; exit 2; }
        load_catalog && scan && play_row "$2"
        ;;
    stop) stop_play ;;
    update) update ;;
    help | -h | --help) usage ;;
    *) usage; exit 2 ;;
esac
