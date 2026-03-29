#!/bin/sh
# smoke_notty.sh — POSIX smoke test for arksh in non-interactive, no-TTY environment.
# Runs as a plain sh script: passes ARKSH binary as $1.
# Exit 0 = pass, non-zero = fail.
#
# Designed to be CI-safe: does not require a terminal, readline, or an interactive session.
# Used by the CTest test: arksh_smoke_notty

set -eu

ARKSH="${1:?Usage: $0 <path-to-arksh>}"

fail() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

pass() {
    printf 'PASS: %s\n' "$*"
}

# ── 1. Basic echo ─────────────────────────────────────────────────────────────
out=$("$ARKSH" -c 'echo hello')
[ "$out" = "hello" ] || fail "basic echo: expected 'hello', got '$out'"
pass "basic echo"

# ── 2. Arithmetic ─────────────────────────────────────────────────────────────
out=$("$ARKSH" -c 'echo $((2 + 3))')
[ "$out" = "5" ] || fail "arithmetic: expected '5', got '$out'"
pass "arithmetic"

# ── 3. Variable assignment and expansion ─────────────────────────────────────
out=$("$ARKSH" -c 'x=world; echo "hello $x"')
[ "$out" = "hello world" ] || fail "variable: expected 'hello world', got '$out'"
pass "variable assignment"

# ── 4. Pipeline ──────────────────────────────────────────────────────────────
out=$("$ARKSH" -c 'printf "%s\n" "a b c" | tr " " "\n" | wc -l | tr -d " "')
[ "$out" = "3" ] || fail "pipeline: expected '3', got '$out'"
pass "POSIX pipeline"

# ── 5. if / else ─────────────────────────────────────────────────────────────
out=$("$ARKSH" -c 'if [ 1 -eq 1 ]; then echo yes; else echo no; fi')
[ "$out" = "yes" ] || fail "if/else: expected 'yes', got '$out'"
pass "if/else"

# ── 6. for loop ──────────────────────────────────────────────────────────────
out=$("$ARKSH" -c 'sum=0; for i in 1 2 3 4 5; do sum=$((sum + i)); done; echo $sum')
[ "$out" = "15" ] || fail "for loop: expected '15', got '$out'"
pass "for loop"

# ── 7. String length via parameter expansion ─────────────────────────────────
out=$("$ARKSH" -c 'word=hello; echo ${#word}')
[ "$out" = "5" ] || fail "string length: expected '5', got '$out'"
pass "string length"

# ── 8. Parameter expansion default ───────────────────────────────────────────
out=$("$ARKSH" -c 'unset UNDEF; echo "${UNDEF:-default}"')
[ "$out" = "default" ] || fail "param expansion default: expected 'default', got '$out'"
pass "param expansion default"

# ── 9. sh mode ───────────────────────────────────────────────────────────────
out=$("$ARKSH" --sh -c 'echo posix')
[ "$out" = "posix" ] || fail "sh mode: expected 'posix', got '$out'"
pass "sh mode"

# ── 10. \$PPID is numeric ─────────────────────────────────────────────────────
out=$("$ARKSH" -c 'echo $PPID')
case "$out" in
    ''|*[!0-9]*) fail "PPID not numeric: '$out'" ;;
esac
pass "PPID numeric"

# ── 11. Nested arithmetic ────────────────────────────────────────────────────
out=$("$ARKSH" -c 'a=3; b=4; echo $((a * b))')
[ "$out" = "12" ] || fail "nested arithmetic: expected '12', got '$out'"
pass "nested arithmetic"

# ── 12. Command substitution ─────────────────────────────────────────────────
out=$("$ARKSH" -c 'x=$(echo inner); echo "got $x"')
[ "$out" = "got inner" ] || fail "command substitution: expected 'got inner', got '$out'"
pass "command substitution"

# ── 13. while loop ───────────────────────────────────────────────────────────
out=$("$ARKSH" -c 'n=0; while [ $n -lt 3 ]; do n=$((n+1)); done; echo $n')
[ "$out" = "3" ] || fail "while loop: expected '3', got '$out'"
pass "while loop"

printf '\nAll smoke tests passed.\n'
exit 0
