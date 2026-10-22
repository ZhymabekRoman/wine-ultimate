#!/usr/bin/env bash
# export-series.sh — regenerate the patches/ artifact from the canonical branch.
#
# Canonical source of truth: branch `ultimate` of the wine fork. The patch
# directories in this repository are a generated artifact; this script rebuilds
# them with plain git plumbing (format-patch), writes a wine-staging style
# `definition` per series, and emits README bullets for the series index.
#
# Usage:
#   tools/export-series.sh <wine-repo> <ref> <out-dir> [series ...]
#
#   <wine-repo>  path to a checkout of the wine fork (contains branch ultimate)
#   <ref>        branch/tag to export from, e.g. ultimate or series/v0052-dcomp0077
#   <out-dir>    destination directory for the series directories
#   series ...   restrict to these series (default: every entry of series.map)
#
# Config: tools/series.map (see that file for the field reference).
set -euo pipefail

if [ $# -lt 3 ]; then
    sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
fi
REPO=$1; REF=$2; OUT=$3; shift 3
MAP=$(cd "$(dirname "$0")" && pwd)/series.map
[ -f "$MAP" ] || { echo "missing config: $MAP" >&2; exit 1; }
git -C "$REPO" rev-parse --verify --quiet "$REF^{commit}" >/dev/null || {
    echo "ref not found in $REPO: $REF" >&2; exit 1; }

# field <record> <key> — value of key=…, quoted ("…") or bare.
field() {
    case $1 in *"$2="*) ;; *) return 0 ;; esac
    local rest=${1#*"$2="}
    case $rest in
        '"'*) rest=${rest#'"'}; printf '%s' "${rest%%'"'*}" ;;
        *)    printf '%s' "${rest%% *}" ;;
    esac
}

subject_slug() {
    git -C "$REPO" log -1 --format=%s "$1" \
        | tr '[:upper:]' '[:lower:]' | sed -e 's/[^a-z0-9.]/-/g' -e 's/-\{2,\}/-/g' -e 's/^-//' \
        | cut -c1-70 | sed -e 's/-*$//'
}

total=0; index=""
while IFS= read -r rec; do
    case "$rec" in ''|\#*) continue ;; esac
    name=$(field "$rec" name)
    if [ $# -gt 0 ] && ! printf '%s\n' "$@" | grep -qx -- "$name"; then continue; fi
    sel=$(field "$rec" selection)
    fixes=$(field "$rec" fixes)
    depends=$(field "$rec" depends)
    [ "$sel" = trailer ] || { echo "unknown selection for $name: $sel" >&2; exit 1; }

    mkdir -p "$OUT/$name"
    rm -f "$OUT/$name"/*.patch
    i=0; files=""
    for c in $(git -C "$REPO" rev-list --reverse "$REF"); do
        git -C "$REPO" log -1 --format=%B "$c" | grep -qx "Series: $name" || continue
        i=$((i + 1))
        slug=$(subject_slug "$c")
        f=$(printf '%04d' "$i")-$slug.patch
        git -C "$REPO" format-patch -1 --stdout "$c" > "$OUT/$name/$f"
        files="$files$f
"
    done
    [ $i -gt 0 ] || { echo "no commits for series $name on $REF" >&2; exit 1; }

    {
        printf 'Fixes: %s\n' "$fixes"
        [ -n "$depends" ] && printf '#Depends: %s\n' "$depends"
        printf '%s' "$files"
    } > "$OUT/$name/definition"
    total=$((total + i))
    index="$index- \`$name\` - $i patch(es), generated from \`$REF\`
"
    printf '%s: %s patches -> %s/%s\n' "$name" "$i" "$OUT" "$name"
done < "$MAP"

{
    printf '\nGenerated from `%s` (wine fork), %s patches total:\n' "$REF" "$total"
    printf '%s' "$index"
} | tee "$OUT/README.series.md"
echo "total: $total patches across the exported series"
