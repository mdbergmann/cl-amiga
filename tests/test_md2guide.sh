#!/bin/sh
# tools/docs/md2guide.lisp -- the Markdown to AmigaGuide converter
# (specs/amigaguide-docs.md).
#
# Covered:
#   golden fixture: tests/md2guide/fixture.md + other.md convert to files
#     byte-identical to the committed fixture.guide / other.guide (every
#     supported construct, every link form, every mapped character, Latin-1
#     pass-through, a cross-file link in both directions)
#   error fixtures: each file under tests/md2guide/errors/ fails with a
#     `file:line:` diagnostic naming the construct, exit status 1, and no
#     output file is written
#   the real docs: tools/docs/md2guide.sh converts README.md and docs/*.md
#     (plus clamacs/README.md when the submodule is present) with zero
#     diagnostics -- the drift gate: a construct the converter does not
#     handle, or a dangling link, in the shipped documentation fails here
#   structural checks over that output, independent of the converter:
#     @DATABASE first, @NODE/@ENDNODE balance, every LINK target names an
#     @NODE of the right file, no byte above 0x7F other than the Latin-1
#     pass-through set, no unescaped @ outside a command, no unindented
#     body line wider than 76 visible columns, the @$VER: line carries the
#     version from src/core/types.h
#
# Run: sh tests/test_md2guide.sh build/host/clamiga

CLAMIGA="${1:-build/host/clamiga}"
ROOT=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 1
. tests/shpath.sh

passed=0
failed=0
total=0
ok()   { total=$((total + 1)); passed=$((passed + 1)); echo "  ok  $1"; }
fail() { total=$((total + 1)); failed=$((failed + 1)); echo "  FAIL  $1"; shift; for l in "$@"; do echo "    $l"; done; }

WORK=$(mktemp -d "${TMPDIR:-/tmp}/clamiga_md2guide_XXXXXX")
trap 'rm -rf "$WORK"' EXIT INT TERM
WORK_NATIVE=$(native_path "$WORK")

run_convert() {
    # run_convert <inputs-as-lisp-alist> <output-dir>  -> stdout+stderr, rc in $rc
    CLAMIGA_NO_USERINIT=1 "$CLAMIGA" --no-userinit --non-interactive \
        --load tools/docs/md2guide.lisp \
        --eval "(md2guide:cli '($1) \"$2/\" \"0.0.0\" \"01.01.2000\")" </dev/null 2>&1
}

# --- golden fixture -------------------------------------------------------
mkdir -p "$WORK/fx"
out=$(run_convert '("tests/md2guide/fixture.md" . "fixture.guide") ("tests/md2guide/other.md" . "other.guide")' "$WORK_NATIVE/fx")
rc=$?
if [ "$rc" -eq 0 ]; then ok "fixture_converts"; else fail "fixture_converts" "$out"; fi
for g in fixture other; do
    if [ -f "$WORK/fx/$g.guide" ] && cmp -s "$WORK/fx/$g.guide" "tests/md2guide/$g.guide"; then
        ok "golden_$g"
    else
        fail "golden_$g" "output differs from tests/md2guide/$g.guide:" \
             "$(diff "tests/md2guide/$g.guide" "$WORK/fx/$g.guide" 2>&1 | head -20)"
    fi
done

# --- error fixtures -------------------------------------------------------
expect_error() {
    # expect_error <name> <line> <message-fragment>
    name=$1; line=$2; frag=$3
    rm -rf "$WORK/err"; mkdir -p "$WORK/err"
    out=$(run_convert "(\"tests/md2guide/errors/$name.md\" . \"$name.guide\")" "$WORK_NATIVE/err")
    rc=$?
    if [ "$rc" -ne 1 ]; then
        fail "error_${name}_status" "expected exit 1, got $rc" "$out"
    elif ! printf '%s\n' "$out" | grep -q "tests/md2guide/errors/$name.md:$line: .*$frag"; then
        fail "error_${name}_diagnostic" "expected '$name.md:$line: ...$frag', got:" "$out"
    elif [ -n "$(ls "$WORK/err")" ]; then
        fail "error_${name}_no_output" "an output file was written despite the error"
    else
        ok "error_$name"
    fi
}
expect_error setext              4 "setext heading"
expect_error refstyle-link       3 "reference-style link"
expect_error html-block          3 "HTML block"
expect_error dangling-link       5 "dangling link: #nowhere"
expect_error unmapped-char       3 "unmapped character U+2603"
expect_error indented-code       5 "indented code block"
expect_error hard-break          3 "hard line break"
expect_error unmatched-backtick  3 "unmatched backtick"
expect_error lazy-list           4 "lazy continuation"
expect_error no-title            1 "level-1 heading"
expect_error table-cells         5 "table row has 3 cells, the header has 2"
expect_error unterminated-fence  3 "unterminated code fence"

# --- the real docs, through the wrapper ------------------------------------
# Not under GC stress: converting the 2000-line README with a compaction
# before every allocation takes a quarter of an hour, and the fixture set
# above already drives every allocating path of the converter.
if [ "${CLAMIGA_GC_STRESS:-0}" = 1 ]; then
    echo "  skip  real_docs (CLAMIGA_GC_STRESS=1: fixture set only)"
    echo ""
    echo "$passed passed, $failed failed, $total total"
    if [ "$failed" -gt 0 ]; then echo "FAIL"; exit 1; else echo "PASS"; exit 0; fi
fi
out=$(sh tools/docs/md2guide.sh "$CLAMIGA" "$WORK/docs" 2>&1)
rc=$?
if [ "$rc" -eq 0 ]; then ok "real_docs_convert"; else fail "real_docs_convert" "$out"; fi
for g in cl-amiga README ext mp ffi gray mop clamiga amiga; do
    [ -s "$WORK/docs/$g.guide" ] || fail "real_docs_present_$g" "missing $WORK/docs/$g.guide"
done
if [ -f clamacs/README.md ]; then
    [ -s "$WORK/docs/clamacs.guide" ] && ok "real_docs_clamacs" || fail "real_docs_clamacs" "no clamacs.guide although clamacs/README.md exists"
fi
total=$((total + 1)); passed=$((passed + 1)); echo "  ok  real_docs_present"

# --- structural checks (LC_ALL=C: the files carry Latin-1 bytes) -----------
ver_field() { sed -n "s/^#define CL_VERSION_$1 \([0-9][0-9]*\)$/\1/p" src/core/types.h; }
VERSION="$(ver_field MAJOR).$(ver_field MINOR).$(ver_field PATCH)"
VDATE=$(sed -n 's/^#define CL_VERSION_DATE "\([^"]*\)"$/\1/p' src/core/types.h)
export LC_ALL=C
cd "$WORK/docs" || exit 1
bad=""
for f in *.guide; do
    [ "$(head -c 10 "$f")" = "@DATABASE " ] || bad="$bad $f:no-@DATABASE"
    [ "$(sed -n 2p "$f")" = "@\$VER: $f $VERSION ($VDATE)" ] || bad="$bad $f:bad-\$VER"
    [ "$(grep -c '^@NODE ' "$f")" -eq "$(grep -c '^@ENDNODE$' "$f")" ] || bad="$bad $f:node-balance"
    # every LINK "node" / LINK "file.guide/node" names an @NODE of that file
    grep -o 'LINK "[^"]*"' "$f" | sed 's/LINK "//; s/"$//' | sort -u | while read -r t; do
        case "$t" in
            */*) g=${t%%/*}; n=${t#*/} ;;
            *)   g=$f; n=$t ;;
        esac
        grep -q "^@NODE $n " "$g" 2>/dev/null || echo "$f: dangling LINK $t"
    done > "$WORK/links.$f"
    [ -s "$WORK/links.$f" ] && bad="$bad $f:$(head -1 "$WORK/links.$f")"
    # an @ that is neither escaped nor a command
    if grep -v '^@\(DATABASE\|\$VER\|AUTHOR\|(C)\|WIDTH\|REM\|NODE\|ENDNODE\)' "$f" \
        | sed 's/\\\\//g; s/\\@//g; s/@{[^}]*}//g' | grep -q '@'; then
        bad="$bad $f:unescaped-@"
    fi
    # bytes above 0x7F: only the Latin-1 characters the docs use (§ ×, umlauts)
    if tr -d '\000-\177\247\327\300-\377' < "$f" | grep -q .; then
        bad="$bad $f:non-latin1-byte"
    fi
    # unindented body lines (paragraphs, list items, table entries, titles)
    # fit 76 visible columns; commands and code lines are exempt
    wide=$(grep -v '^@' "$f" | grep -v '^ ' | sed 's/@{[^}]*}//g; s/\\@/@/g; s/\\\\/\\/g' \
           | awk 'length > 76 { n++ } END { print n + 0 }')
    [ "$wide" -eq 0 ] || bad="$bad $f:$wide-lines-wider-than-76"
done
cd "$ROOT" || exit 1
if [ -z "$bad" ]; then ok "structure"; else fail "structure" "$bad"; fi
# the index page links to every reference page
bad=""
for g in ext mp ffi gray mop clamiga amiga; do
    grep -q "LINK \"$g.guide/main\"" "$WORK/docs/README.guide" || bad="$bad README.guide:no-link-to-$g"
done
[ -z "$bad" ] && ok "index_links_every_page" || fail "index_links_every_page" "$bad"
# the manual links into the reference
grep -q 'LINK "ext.guide/' "$WORK/docs/cl-amiga.guide" && ok "manual_links_reference" || fail "manual_links_reference"

echo ""
echo "$passed passed, $failed failed, $total total"
if [ "$failed" -gt 0 ]; then echo "FAIL"; exit 1; else echo "PASS"; exit 0; fi
