#!/usr/bin/env bash
# Test suite for the Mini-C compiler (minic).
# Runs from any directory; builds nothing (run `make` first).
# Usage: bash tests/test.sh

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

if [ ! -x ./minic ]; then
    echo "minic binary not found - run make first"
    exit 1
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
CC="${CC:-gcc}"
CFLAGS_TEST="-std=c11 -Wall -Wextra -Werror"

PASSED=0
FAILED=0

# Colors only when stdout is a terminal.
if [ -t 1 ]; then
    GREEN=$'\033[32m'; RED=$'\033[31m'; YELLOW=$'\033[33m'; RESET=$'\033[0m'
else
    GREEN=""; RED=""; YELLOW=""; RESET=""
fi

# Record a passing test.
pass() {
    PASSED=$((PASSED + 1))
    echo "${GREEN}PASS${RESET} $1"
}

# Record a failing test with an optional detail message.
fail() {
    FAILED=$((FAILED + 1))
    echo "${RED}FAIL${RESET} $1"
    if [ -n "${2:-}" ]; then
        echo "$2" | sed 's/^/     /'
    fi
}

# Print a note for a skipped test (not counted).
skip() {
    echo "${YELLOW}SKIP${RESET} $1"
}

# Turn a test name into a safe file stem (non-alphanumerics become '_').
stem() {
    printf '%s' "$1" | tr -c 'A-Za-z0-9\n' '_'
}

# Write SOURCE to $TMP/<stem of NAME>.c and echo the path.
write_src() {
    local path
    path="$TMP/$(stem "$1").c"
    printf '%s\n' "$2" > "$path"
    echo "$path"
}

# Run minic with the given args; sets RC, OUT_FILE (stdout), ERR_FILE (stderr).
OUT_FILE="$TMP/stdout.txt"
ERR_FILE="$TMP/stderr.txt"
RC=0
run_minic() {
    ./minic "$@" > "$OUT_FILE" 2> "$ERR_FILE"
    RC=$?
}

# run_prog NAME SOURCE EXPECTED_STDOUT
# Compile SOURCE with minic, then gcc, run it and compare stdout exactly.
run_prog() {
    local name="$1" source="$2" expected="$3"
    local src gen exe st
    st=$(stem "$name")
    src=$(write_src "$name" "$source")
    gen="$TMP/$st.gen.c"
    exe="$TMP/$st.exe"
    printf '%s\n' "$expected" > "$TMP/$st.expected"

    run_minic "$src" -o "$gen"
    if [ "$RC" -ne 0 ]; then
        fail "$name" "minic exited with $RC: $(cat "$ERR_FILE")"
        return
    fi
    local cc_out
    if ! cc_out=$($CC $CFLAGS_TEST "$gen" -o "$exe" 2>&1); then
        fail "$name" "gcc failed: $cc_out"
        return
    fi
    "$exe" > "$TMP/$st.actual" 2> /dev/null
    local prc=$?
    if [ "$prc" -ge 128 ]; then
        fail "$name" "compiled program crashed (exit $prc)"
        return
    fi
    if diff "$TMP/$st.expected" "$TMP/$st.actual" > "$TMP/$st.diff"; then
        pass "$name"
    else
        fail "$name" "stdout differs (< expected, > actual):
$(cat "$TMP/$st.diff")"
    fi
}

# expect_compile_error NAME SOURCE EXPECTED_STDERR_SUBSTRING
# minic must exit exactly 1 (>=128 is a crash) and stderr must contain the substring.
expect_compile_error() {
    local name="$1" source="$2" needle="$3"
    local src
    src=$(write_src "$name" "$source")
    run_minic "$src" -o "$TMP/$(stem "$name").gen.c"
    if [ "$RC" -ge 128 ]; then
        fail "$name" "minic crashed (exit $RC)"
    elif [ "$RC" -ne 1 ]; then
        fail "$name" "expected exit 1, got $RC"
    elif ! grep -qF -- "$needle" "$ERR_FILE"; then
        fail "$name" "stderr lacks '$needle'; got: $(cat "$ERR_FILE")"
    else
        pass "$name"
    fi
}

# expect_warning NAME SOURCE SUBSTRING
# minic must succeed (exit 0) and stderr must contain the substring.
expect_warning() {
    local name="$1" source="$2" needle="$3"
    local src
    src=$(write_src "$name" "$source")
    run_minic "$src" -o "$TMP/$(stem "$name").gen.c"
    if [ "$RC" -ne 0 ]; then
        fail "$name" "expected exit 0, got $RC: $(cat "$ERR_FILE")"
    elif ! grep -qF -- "$needle" "$ERR_FILE"; then
        fail "$name" "stderr lacks '$needle'; got: $(cat "$ERR_FILE")"
    else
        pass "$name"
    fi
}

# expect_tokens NAME SOURCE EXPECTED_SPACE_JOINED_TOKENS
# Compares the --tokens output (lines joined by single spaces).
expect_tokens() {
    local name="$1" source="$2" expected="$3"
    local src joined
    src=$(write_src "$name" "$source")
    run_minic "$src" --tokens
    joined=$(tr '\n' ' ' < "$OUT_FILE" | sed 's/ $//')
    if [ "$RC" -ne 0 ]; then
        fail "$name" "expected exit 0, got $RC"
    elif [ "$joined" = "$expected" ]; then
        pass "$name"
    else
        fail "$name" "expected: $expected
actual:   $joined"
    fi
}

# ---------------------------------------------------------------------------
# Required tests 1-12 (PLAN section 12)
# ---------------------------------------------------------------------------

# 1. Token stream of a simple declaration.
expect_tokens "01 tokens: int x = 5;" 'int x = 5;' \
    "INT ID(x) ASSIGN NUMBER(5) SEMICOLON EOF"

# 2a. AST nesting: '*' must be nested deeper than and after '+'.
src=$(write_src ast_prec 'int x = 5 + 3 * 2;')
run_minic "$src" --ast
if [ "$RC" -ne 0 ]; then
    fail "02a ast: * nested under +" "minic exited with $RC: $(cat "$ERR_FILE")"
else
    nested=$(awk '
        /BinaryExpr: \+/ && !p { match($0, /^ */); pi = RLENGTH; p = NR }
        /BinaryExpr: \*/ && !m { match($0, /^ */); mi = RLENGTH; m = NR }
        END { if (p && m && m > p && mi > pi) print "yes"; else print "no" }
    ' "$OUT_FILE")
    if [ "$nested" = "yes" ]; then
        pass "02a ast: * nested under +"
    else
        fail "02a ast: * nested under +" "$(cat "$OUT_FILE")"
    fi
fi

# 2b. The same expression evaluates to 11.
run_prog "02b run 5 + 3 * 2" 'int main() { int x = 5 + 3 * 2; print(x); return 0; }' "11"

# 3. examples/hello.c prints Hello, World!
if [ -f examples/hello.c ]; then
    if ./minic examples/hello.c -o "$TMP/hello.gen.c" 2> "$TMP/hello.err" \
        && $CC $CFLAGS_TEST "$TMP/hello.gen.c" -o "$TMP/hello.exe" 2> "$TMP/hello.cc" \
        && "$TMP/hello.exe" > "$TMP/hello.out"; then
        if [ "$(cat "$TMP/hello.out")" = "Hello, World!" ]; then
            pass "03 examples/hello.c"
        else
            fail "03 examples/hello.c" "got: $(cat "$TMP/hello.out")"
        fi
    else
        fail "03 examples/hello.c" "$(cat "$TMP/hello.err" "$TMP/hello.cc" 2>/dev/null)"
    fi
else
    fail "03 examples/hello.c" "examples/hello.c missing"
fi

# 4. Variables and addition.
run_prog "04 a + b" 'int main() { int a = 10; int b = 20; print(a + b); return 0; }' "30"

# 5. if / else.
run_prog "05 if/else" 'int main() { if (5 > 3) print(1); else print(0); return 0; }' "1"

# 6. while loop.
run_prog "06 while loop" 'int main() { int i = 0; while (i < 5) { print(i); i = i + 1; } return 0; }' \
    $'0\n1\n2\n3\n4'

# 7. for loop.
run_prog "07 for loop" 'int main() { for (int i = 0; i < 3; i = i + 1) { print(i); } return 0; }' \
    $'0\n1\n2'

# 8. Function call.
run_prog "08 function add" 'int add(int a, int b) { return a + b; } int main() { print(add(5, 7)); return 0; }' "12"

# 9. Recursive factorial.
run_prog "09 factorial" 'int factorial(int n) { if (n <= 1) { return 1; } return n * factorial(n - 1); } int main() { print(factorial(5)); return 0; }' "120"

# 10. Parser error for a missing initializer.
expect_compile_error "10 parser error" 'int main() { int x = ; return 0; }' "Parser Error"

# 11. Undefined variable.
expect_compile_error "11 undefined variable" 'int main() { print(y); return 0; }' "Undefined variable 'y'"

# 12. Every example compiles, runs, and matches its expected output.
for ex in examples/*.c; do
    [ -f "$ex" ] || continue
    base=$(basename "$ex" .c)
    tname="12 example $base"
    gen="$TMP/ex_$base.c"
    exe="$TMP/ex_$base.exe"
    if ! ./minic "$ex" -o "$gen" 2> "$TMP/ex.err"; then
        fail "$tname" "minic failed: $(cat "$TMP/ex.err")"
        continue
    fi
    if ! cc_out=$($CC $CFLAGS_TEST "$gen" -o "$exe" 2>&1); then
        fail "$tname" "gcc failed: $cc_out"
        continue
    fi
    "$exe" > "$TMP/ex.out" 2> /dev/null
    prc=$?
    if [ "$prc" -ge 128 ]; then
        fail "$tname" "program crashed (exit $prc)"
        continue
    fi
    if [ -f "examples/expected/$base.txt" ]; then
        if diff "examples/expected/$base.txt" "$TMP/ex.out" > "$TMP/ex.diff"; then
            pass "$tname"
        else
            fail "$tname" "stdout differs (< expected, > actual):
$(cat "$TMP/ex.diff")"
        fi
    else
        pass "$tname (no expected file, ran ok)"
    fi
done

# ---------------------------------------------------------------------------
# Extra tests
# ---------------------------------------------------------------------------

# Lexer errors.
expect_compile_error "lexer: unexpected '@'" 'int main() { int x = 1 @ 2; return 0; }' "Lexer Error at line"
expect_compile_error "lexer: unterminated comment" 'int main() { return 0; } /* never closed' "unterminated comment"
expect_compile_error "lexer: unterminated string" 'int main() { print("abc); return 0; }' "unterminated string literal"

# Comments are ignored.
run_prog "comments ignored" '// line comment
int main() { /* block
comment */ print(7); // trailing
return 0; }' "7"

# Char and string printing.
run_prog "print char" "int main() { print('a'); return 0; }" "a"
run_prog "string tab escape" 'int main() { print("Tab\tEnd"); return 0; }' $'Tab\tEnd'
run_prog "string newline escape" 'int main() { print("Line1\nLine2"); return 0; }' $'Line1\nLine2'
run_prog "string with percent" 'int main() { print("100%"); return 0; }' "100%"

# Operators.
run_prog "left assoc subtraction" 'int main() { print(10 - 3 - 2); return 0; }' "5"
run_prog "parenthesized precedence" 'int main() { print(2 * (3 + 4)); return 0; }' "14"
run_prog "modulo" 'int main() { print(7 % 3); return 0; }' "1"
run_prog "left assoc division" 'int main() { print(100 / 10 / 2); return 0; }' "5"
run_prog "unary minus" 'int main() { int x = 5; print(-x + 8); print(- -x); return 0; }' $'3\n5'
run_prog "logical not" 'int main() { print(!0); print(!5); print(!!3); return 0; }' $'1\n0\n1'
run_prog "assignment right assoc" 'int main() { int a = 0; int b = 0; a = b = 4; print(a); print(b); return 0; }' $'4\n4'
run_prog "comparisons" 'int main() { print(3 < 5); print(5 < 3); print(3 <= 3); print(4 >= 5); print(2 == 2); print(2 != 2); return 0; }' \
    $'1\n0\n1\n0\n1\n0'

# Short-circuit evaluation (side effects reveal evaluation order).
run_prog "short-circuit && / ||" 'int side(int v) { print(v); return v; }
int main() {
    if (side(0) && side(1)) { print(9); }
    if (side(1) || side(2)) { print(8); }
    if (side(1) && side(3)) { print(7); }
    if (side(0) || side(4)) { print(6); }
    return 0;
}' $'0\n1\n8\n1\n3\n7\n0\n4\n6'

# Control flow and scoping.
run_prog "else-if chain" 'int classify(int n) {
    if (n < 0) { return 0 - 1; } else if (n == 0) { return 0; } else if (n < 10) { return 1; } else { return 2; }
}
int main() { print(classify(-5)); print(classify(0)); print(classify(5)); print(classify(50)); return 0; }' \
    $'-1\n0\n1\n2'
run_prog "shadowing" 'int main() { int x = 1; { int x = 2; print(x); } print(x); return 0; }' $'2\n1'

# Functions and globals.
run_prog "fibonacci recursion" 'int fib(int n) { if (n < 2) { return n; } return fib(n - 1) + fib(n - 2); }
int main() { print(fib(10)); return 0; }' "55"
run_prog "global variable" 'int counter = 5;
void bump() { counter = counter + 1; }
int main() { bump(); bump(); print(counter); return 0; }' "7"
run_prog "call before definition" 'int main() { print(twice(21)); return 0; }
int twice(int n) { return n * 2; }' "42"

# Semantic errors.
expect_compile_error "redeclaration" 'int main() { int x = 1; int x = 2; print(x); return 0; }' "Variable 'x' already declared"
expect_compile_error "type mismatch" 'int main() { int x = "hello"; print(x); return 0; }' "Type mismatch"
expect_compile_error "wrong arg count" 'int add(int a, int b) { return a + b; }
int main() { print(add(1, 2, 3)); return 0; }' "expects 2 argument(s) but got 3"
expect_compile_error "return type mismatch" 'void f() { return 1; } int main() { f(); return 0; }' "Return type mismatch"
expect_compile_error "missing main" 'int f() { return 1; }' "No 'main' function defined"
expect_compile_error "undefined function" 'int main() { foo(1); return 0; }' "Undefined function 'foo'"
expect_compile_error "invalid assignment target" 'int main() { int x = 1; 5 = x; print(x); return 0; }' "invalid assignment target"
expect_compile_error "division by zero" 'int main() { print(4 / 0); return 0; }' "Division by zero"

# Warnings do not fail compilation.
expect_warning "uninitialized warning" 'int main() { int x; print(x); return 0; }' "may be used uninitialized"
expect_warning "missing return warning" 'int f(int x) { if (x) return 1; } int main() { print(f(1)); return 0; }' "may not return a value on all paths"
# A function that may fall off the end still produces -Werror-clean C.
run_prog "missing return still compiles" 'int f(int x) { if (x) return 7; } int main() { print(f(1)); return 0; }' "7"

# Output file is not created when compilation fails.
src=$(write_src nofile 'int main() { print(y); return 0; }')
rm -f "$TMP/nofile.gen.c"
run_minic "$src" -o "$TMP/nofile.gen.c"
if [ "$RC" -eq 1 ] && [ ! -e "$TMP/nofile.gen.c" ]; then
    pass "no output file on error"
else
    fail "no output file on error" "exit $RC, file exists: $([ -e "$TMP/nofile.gen.c" ] && echo yes || echo no)"
fi

# CLI behaviour.
run_minic --help
if [ "$RC" -eq 0 ]; then
    pass "--help exits 0"
else
    fail "--help exits 0" "exit $RC"
fi

run_minic "$TMP/does_not_exist.c"
if [ "$RC" -eq 1 ]; then
    pass "missing input file exits 1"
else
    fail "missing input file exits 1" "exit $RC"
fi

# ---------------------------------------------------------------------------
# Interpreter (--run): must behave exactly like the gcc-compiled program.
# ---------------------------------------------------------------------------

# expect_run NAME SOURCE EXPECTED_STDOUT [EXPECTED_EXIT]
# Runs SOURCE with `minic --run` and compares stdout and the exit code.
expect_run() {
    local name="$1" source="$2" expected="$3" want_rc="${4:-0}"
    local src
    src=$(write_src "$name" "$source")
    run_minic "$src" --run
    if [ "$RC" -ge 128 ]; then
        fail "$name" "minic crashed (exit $RC)"
    elif [ "$RC" -ne "$want_rc" ]; then
        fail "$name" "expected exit $want_rc, got $RC: $(cat "$ERR_FILE")"
    elif [ "$(cat "$OUT_FILE")" != "$expected" ]; then
        fail "$name" "stdout differs: got '$(cat "$OUT_FILE")'"
    else
        pass "$name"
    fi
}

# expect_runtime_error NAME SOURCE STDERR_SUBSTRING
expect_runtime_error() {
    local name="$1" source="$2" needle="$3"
    local src
    src=$(write_src "$name" "$source")
    run_minic "$src" --run --max-steps 2000000
    if [ "$RC" -ge 128 ]; then
        fail "$name" "minic crashed (exit $RC)"
    elif [ "$RC" -ne 1 ]; then
        fail "$name" "expected exit 1, got $RC"
    elif ! grep -qF -- "$needle" "$ERR_FILE"; then
        fail "$name" "stderr lacks '$needle': $(cat "$ERR_FILE")"
    else
        pass "$name"
    fi
}

# Every example: interpreter output must equal the expected (gcc) output.
for ex in examples/*.c; do
    [ -f "$ex" ] || continue
    base=$(basename "$ex" .c)
    [ -f "examples/expected/$base.txt" ] || continue
    if ./minic "$ex" --run > "$TMP/run.out" 2> "$TMP/run.err" \
        && diff "examples/expected/$base.txt" "$TMP/run.out" > /dev/null; then
        pass "run example $base"
    else
        fail "run example $base" "$(cat "$TMP/run.err")"
    fi
done

expect_run "run: exit code from main" 'int main() { print(1); return 42; }' "1" 42
expect_run "run: char wraps like C" 'int main() { char c = 300; print(c + 0); return 0; }' "44"
expect_run "run: int wraps like C" 'int main() { int m = 2147483647; print(m + 1); return 0; }' "-2147483648"
expect_run "run: C division rounding" 'int main() { print(-7 / 2); print(-7 % 2); return 0; }' $'-3\n-1'
expect_run "run: short-circuit order" 'int t(int x) { print(x); return x; } int main() { print(t(0) && t(1)); print(t(1) || t(2)); return 0; }' $'0\n0\n1\n1'
expect_run "run: shadowing scopes" 'int g = 1; int main() { print(g); { int g = 2; print(g); } print(g); return 0; }' $'1\n2\n1'
expect_run "run: deep recursion" 'int d(int n) { if (n == 0) return 0; return 1 + d(n - 1); } int main() { print(d(4000)); return 0; }' "4000"
expect_run "run: string escapes" 'int main() { print("a\tb \"q\" 100%"); return 0; }' $'a\tb "q" 100%'
expect_runtime_error "run: division by zero" 'int main() { int z = 0; print(1 / z); return 0; }' "division by zero"
expect_runtime_error "run: step limit stops infinite loop" 'int main() { while (1) { } return 0; }' "step limit"
expect_runtime_error "run: runaway recursion" 'int f(int n) { return f(n + 1); } int main() { print(f(0)); return 0; }' "call stack overflow"

# Reading the program from standard input with "-".
if [ "$(printf 'int main() { print(7); return 0; }' | ./minic - --run 2>&1)" = "7" ]; then
    pass "stdin input with -"
else
    fail "stdin input with -" "unexpected output"
fi

# Valgrind leak check on one example (if available).
if command -v valgrind > /dev/null 2>&1; then
    if [ -f examples/factorial.c ]; then
        if valgrind -q --leak-check=full --error-exitcode=1 ./minic examples/factorial.c -o "$TMP/v.c" > /dev/null 2> "$TMP/valgrind.err"; then
            pass "valgrind leak check (factorial.c)"
        else
            fail "valgrind leak check (factorial.c)" "$(head -20 "$TMP/valgrind.err")"
        fi
    else
        skip "valgrind leak check (examples/factorial.c missing)"
    fi
else
    skip "valgrind leak check (valgrind not installed)"
fi

# ---------------------------------------------------------------------------
echo
echo "$PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
