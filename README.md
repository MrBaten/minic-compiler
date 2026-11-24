# MiniC Compiler - I Built a C Compiler in C

[![CI](https://github.com/MrBaten/minic-compiler/actions/workflows/ci.yml/badge.svg)](https://github.com/MrBaten/minic-compiler/actions/workflows/ci.yml)

`minic` is a hand-written compiler for **Mini-C**, a small, well-defined subset of C, written in pure C11 with **zero dependencies**: no Lex, Yacc, Flex or Bison, and no libraries beyond the C standard library. It consists of a hand-written lexer, a recursive-descent parser that builds an abstract syntax tree, a semantic checker with scoped symbol tables and type rules, and a code generator that transpiles the checked program to portable C11, which `gcc` or `clang` then turns into an executable. Every stage can be inspected from the command line (`--tokens`, `--ast`), programs can also be executed directly by a built-in interpreter (`--run`), and the whole project builds with zero warnings under `-Wall -Wextra -Wpedantic`.

### ▶ [Try it in your browser: MiniC Playground](https://mrbaten.github.io/minic/)

The same C sources, compiled to **WebAssembly**, power an in-browser playground: edit Mini-C, watch the tokens, AST and generated C update as you type, and run the program. Nothing is installed and nothing is sent to a server.

## Table of Contents

- [Pipeline](#pipeline)
- [Worked Example](#worked-example)
- [Build and Run](#build-and-run)
- [Web Playground](#web-playground)
- [Language Reference](#language-reference)
- [Error Messages](#error-messages)
- [Project Structure](#project-structure)
- [Architecture Notes](#architecture-notes)
- [Testing](#testing)
- [What I Learned](#what-i-learned)
- [Limitations](#limitations)
- [Roadmap](#roadmap)
- [License](#license)

## Pipeline

```
 Source Code (.c)
       |
       v
 +-----------+   TokenList    +-----------+      AST       +-----------+
 |   Lexer   | -------------> |  Parser   | -------------> |  Checker  |
 | lexer.c   |                | parser.c  |    ast.c       | checker.c |
 +-----------+                +-----------+                +-----------+
                                                                  |
                                                      annotated AST (eval_type)
                                                                  |
                                                                  v
 +------------+    C Code     +-----------+                +-----------+
 | Executable | <------------ |    gcc    | <------------- |  Codegen  |
 +------------+    (out.c)    +-----------+     out.c      | codegen.c |
                                                           +-----------+
           checked AST, alternatively (--run):
                                                           +-----------+
                                       program output <--- |Interpreter|
                                                           | interp.c  |
                                                           +-----------+
```

| Stage       | Input                | Output                              | What it does                                                                                       | On error                                      |
|-------------|----------------------|-------------------------------------|----------------------------------------------------------------------------------------------------|-----------------------------------------------|
| **Lexer**   | source text          | `TokenList`                         | Scans characters into tokens (keywords, identifiers, numbers, strings, operators), skips comments and whitespace, tracks line and column | prints `Lexer Error ...`, exits 1             |
| **Parser**  | `TokenList`          | AST (`NODE_PROGRAM`)                | Recursive descent over the grammar; encodes precedence and associativity in the tree shape         | prints `Parser Error ...` (first error), exits 1 |
| **Checker** | AST                  | error count, `eval_type` on every expression | Builds scoped symbol tables, resolves names, type-checks expressions, calls, returns and conditions, emits warnings | prints **all** errors and warnings, returns N |
| **Codegen** | checked AST          | C source text                       | Emits globals, prototypes and definitions; parenthesizes nested expressions; lowers `print` to `printf` | never fails (input is already checked)        |
| **gcc**     | generated C          | executable                          | Final compilation (external, not part of `minic`)                                                  | -                                             |
| **Interpreter** (`--run`) | checked AST | program output, exit code     | Executes the AST directly with the same semantics as the generated C (32-bit wrap-around, `char` truncation, short-circuit `&&`/`\|\|`) | prints `Runtime Error ...` (division by zero, recursion too deep, step limit), exits 1 |

## Worked Example

The following Mini-C program defines a function, uses a `char`, a `for` loop and the `print` builtin.

**Input: `sample.c`**

```c
int add(int a, int b) {
    return a + b;
}

int main() {
    char c = 'A';
    int total = 0;
    for (int i = 1; i <= 3; i = i + 1) {
        total = add(total, i);
    }
    print(c);
    print(total);
    return 0;
}
```

### 1. Tokens (`./minic sample.c --tokens`, excerpt)

One token per line; the stream always ends with `EOF`.

```
INT
ID(add)
LPAREN
INT
ID(a)
COMMA
INT
ID(b)
RPAREN
LBRACE
RETURN
ID(a)
PLUS
ID(b)
SEMICOLON
RBRACE
INT
ID(main)
LPAREN
RPAREN
LBRACE
CHAR
ID(c)
ASSIGN
CHAR_LITERAL('A')
SEMICOLON
...
RBRACE
EOF
```

### 2. AST (`./minic sample.c --ast`)

Each level is indented by two spaces. Labelled sections (`Init:`, `Condition:`, `Update:`, `Body:`) are printed one level below their statement, and their child one level deeper.

```
Program
  FuncDecl: int add(int a, int b)
    Block
      ReturnStmt
        BinaryExpr: +
          VarExpr: a
          VarExpr: b
  FuncDecl: int main()
    Block
      VarDecl: char c
        CharExpr: 'A'
      VarDecl: int total
        NumberExpr: 0
      ForStmt
        Init:
          VarDecl: int i
            NumberExpr: 1
        Condition:
          BinaryExpr: <=
            VarExpr: i
            NumberExpr: 3
        Update:
          AssignExpr: i
            BinaryExpr: +
              VarExpr: i
              NumberExpr: 1
        Body:
          Block
            ExprStmt
              AssignExpr: total
                CallExpr: add
                  VarExpr: total
                  VarExpr: i
      ExprStmt
        CallExpr: print
          VarExpr: c
      ExprStmt
        CallExpr: print
          VarExpr: total
      ReturnStmt
        NumberExpr: 0
```

### 3. Generated C (`./minic sample.c -o out.c`)

The output has a header comment, a prototypes section (every function except `main`), definitions in source order, and `main(void)`. `print` on a `char` becomes `printf("%c\n", ...)`; on any other value it becomes `printf("%d\n", ...)`.

```c
/* Generated by minic - do not edit */
#include <stdio.h>

int add(int a, int b);

int add(int a, int b) {
    return a + b;
}

int main(void) {
    char c = 'A';
    int total = 0;
    for (int i = 1; i <= 3; i = i + 1) {
        total = add(total, i);
    }
    printf("%c\n", c);
    printf("%d\n", total);
    return 0;
}
```

Running the compiled program prints:

```
A
6
```

### Precedence is made explicit

The code generator parenthesizes every binary expression nested inside another expression, so the generated C reflects the AST exactly (and `gcc -Wparentheses` stays quiet):

| Mini-C                  | Generated C                |
|-------------------------|----------------------------|
| `x = 5 + 3 * 2;`        | `x = 5 + (3 * 2);`         |
| `a && b \|\| c`         | `(a && b) \|\| c`          |
| `-(-x)`                 | `-(-x)`                    |
| `if (x = 5) ...`        | `if ((x = 5)) {` ...       |

Bodies of `if`, `else`, `while` and `for` are always emitted as braced blocks. If a non-void function can fall off its end (the checker warns about this), the generator appends `return 0; /* inserted by minic: missing return */` so the output still compiles cleanly under `-Werror=return-type`.

## Build and Run

Requirements: a C11 compiler (`gcc` or `clang`) and `make`. `valgrind` is optional (used by `make memcheck`).

```sh
make
./minic examples/hello.c -o out.c && gcc out.c -o out && ./out
```

Expected output:

```
Hello, World!
```

### Command-line options

```
Usage: minic <input.c> [-o <output.c>] [--tokens] [--ast] [--run [--max-steps N]]
       minic -h | --help
```

| Option          | Description                                                                                         |
|-----------------|-----------------------------------------------------------------------------------------------------|
| `<input.c>`     | Mini-C source file (required); `-` reads the program from standard input                            |
| `-o <output.c>` | Write the generated C to this file. Without `-o` the C code goes to stdout                          |
| `--tokens`      | Lex only: print the token stream to stdout and exit 0                                               |
| `--ast`         | Lex and parse only: print the AST to stdout and exit 0 (the checker is not run)                     |
| `--run`         | Type-check, then execute the program with the built-in interpreter; exits with `main`'s return value |
| `--max-steps N` | With `--run`: stop with a runtime error after N execution steps (default 0 = no limit)              |
| `-h`, `--help`  | Print usage and exit 0                                                                              |

Options may appear in any order. An unknown option, a missing input file or a missing `-o` argument prints the usage to stderr and exits 1. An unreadable input prints `minic: error: cannot open file '<path>'` and exits 1. If the checker reports errors, `minic` prints `minic: compilation failed with N error(s)`, does **not** create the output file, and exits 1.

### Make targets

| Target          | Description                                                                                              |
|-----------------|----------------------------------------------------------------------------------------------------------|
| `make` / `make all` | Build `./minic` from `src/*.c` (objects in `build/`)                                                 |
| `make test`     | Build, then run `bash tests/test.sh`                                                                     |
| `make examples` | Compile every `examples/*.c` with `minic` into `build/examples/<name>.c`, compile that with `gcc`, and run it |
| `make memcheck` | Run valgrind over the examples (if valgrind is installed)                                                |
| `make wasm`     | Build `build/minic.wasm` with `clang --target=wasm32-wasi` (needs a WASI sysroot, see [Web Playground](#web-playground)) |
| `make playground` | Build the wasm, check it against the native binary, and assemble `build/playground/index.html`         |
| `make clean`    | Remove build artifacts                                                                                   |

Build flags default to `CC ?= gcc` and `CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -g`; both can be overridden, for example `make CC=clang`.

### Running programs without gcc

```sh
./minic examples/factorial.c --run          # interpret directly
echo 'int main() { print(6 * 7); return 0; }' | ./minic - --run
```

The interpreter is a tree walker over the checked AST. It is tested to produce exactly the same output and exit code as the gcc-compiled program for every example, including `int` overflow, `char` truncation, negative division and short-circuit evaluation order.

## Web Playground

**Live: <https://mrbaten.github.io/minic/>**

The playground is a single self-contained HTML page (about 140 KB). It embeds:

- `build/minic.wasm`: the **unmodified compiler sources** built with `clang --target=wasm32-wasi` (about 75 KB);
- `playground/minic-wasi.js`: a tiny WASI host (arguments, stdin/stdout/stderr, exit) so the wasm runs in any browser without Emscripten. The program text is passed on stdin (`minic - ...`);
- the `examples/` programs.

Each keystroke re-runs `minic --tokens`, `--ast` and code generation in a fresh wasm instance (a few milliseconds). **Run** executes `minic --run` inside a Web Worker with a step limit and a 5-second timeout, so an infinite loop never freezes the page. Errors are shown on the pipeline strip and marked in the editor's gutter, and **Share link** encodes the program in the URL.

To build it yourself you need clang with the wasm32 target and a WASI sysroot (Ubuntu: `apt install clang lld wasi-libc libclang-rt-18-dev-wasm32`; elsewhere use [wasi-sdk](https://github.com/WebAssembly/wasi-sdk) and set `WASM_CC`), plus Node.js:

```sh
make playground                     # -> build/playground/index.html
```

`playground/test.mjs` runs every example in every mode through both the wasm build and the native binary and requires byte-identical stdout, stderr and exit codes.

## Language Reference

Mini-C is a strict subset of C. A program is a sequence of global variable declarations and function definitions, in any order, and must define `int main()`.

### Syntax summary

| Feature            | Syntax                                                | Notes                                                                                   |
|--------------------|-------------------------------------------------------|-----------------------------------------------------------------------------------------|
| Types              | `int`, `char`, `void`                                 | `void` is only for function return types and for an explicit empty parameter list. `int` and `char` are mutually assignable. |
| Variables          | `int x;`  `char c = 'a';`  `int y = x + 1;`           | One declarator per declaration. Allowed at global level and inside blocks.             |
| Global variables   | `int counter = 0;`                                    | Initializer must be a constant expression (numbers, chars, unary and binary operators of those). Visible only after their declaration. |
| Literals           | `42`, `'a'`, `'\n'`, `"text\n"`                       | Decimal integers up to 2147483647. String literals appear only as the argument of `print`. |
| Escapes            | `\n \t \r \0 \\ \' \"`                                | Valid in strings and char literals.                                                     |
| Assignment         | `x = expr`                                            | An expression; right-associative; the target must be a plain variable.                  |
| Arithmetic         | `+ - * / %`                                           | Operands must be `int` or `char`; the result is `int`.                                  |
| Comparison         | `< > <= >= == !=`                                     | Result is `int` (0 or 1).                                                               |
| Logical            | `&& \|\| !`                                           | `&&` and `\|\|` short-circuit as in C.                                                  |
| Unary minus        | `-x`                                                  |                                                                                         |
| Grouping           | `( expr )`                                            |                                                                                         |
| `if` / `else`      | `if (c) stmt else stmt`                               | `else` binds to the nearest `if`; `else if` chains work.                                |
| `while`            | `while (c) stmt`                                      |                                                                                         |
| `for`              | `for (init; cond; update) stmt`                       | `init` is a declaration or an expression; each part may be omitted.                     |
| Blocks             | `{ ... }`                                             | Open a new scope.                                                                       |
| Functions          | `int add(int a, int b) { ... }`  `int main() { ... }` | Parameter list may be empty or `void`. Functions may be called before they are defined, and may recurse. |
| `return`           | `return expr;`  `return;`                             | Must match the function's return type.                                                  |
| Empty statement    | `;`                                                   |                                                                                         |
| Comments           | `// to end of line`   `/* block, not nested */`       |                                                                                         |

A declaration is not allowed as the direct body of `if`, `else`, `while` or `for`; it must be inside a block.

### The `print` builtin

`print` is not a keyword; it is a predeclared builtin function that takes exactly one argument and prints it followed by a newline. It behaves in three ways, decided at compile time:

| Argument                          | Generated C                                                          |
|-----------------------------------|----------------------------------------------------------------------|
| String literal without `%`        | `printf("<text>\n");`                                                |
| String literal containing `%`     | `printf("%s\n", "<text>");` (so `%` is printed literally)            |
| Expression of type `char`         | `printf("%c\n", <expr>);`                                             |
| Any other `int` expression        | `printf("%d\n", <expr>);`                                            |

The two string rows are one behaviour (string), the `char` row prints a character, and the last row prints a decimal integer. A raw `??` inside a string literal is emitted as `?\?` so C trigraphs can never change its meaning.

### Operator precedence

From highest to lowest. All binary operators are left-associative except `=`.

| Level | Operators                | Associativity | Description              |
|-------|--------------------------|---------------|--------------------------|
| 1     | `( )` literals, names, calls | -         | primary                  |
| 2     | `-` `!` (prefix)         | right         | unary                    |
| 3     | `*` `/` `%`              | left          | multiplicative           |
| 4     | `+` `-`                  | left          | additive                 |
| 5     | `<` `>` `<=` `>=`        | left          | relational               |
| 6     | `==` `!=`                | left          | equality                 |
| 7     | `&&`                     | left          | logical AND              |
| 8     | `\|\|`                   | left          | logical OR               |
| 9     | `=`                      | right         | assignment               |

### Formal grammar (EBNF)

```
program        := ( func_decl | var_decl )* EOF
type           := 'int' | 'char' | 'void'
func_decl      := type IDENT '(' param_list? ')' block        // lookahead: type IDENT '('
param_list     := 'void' | param ( ',' param )*
param          := type IDENT
var_decl       := type IDENT ( '=' expression )? ';'
block          := '{' statement* '}'
statement      := var_decl | body_statement
body_statement := block | if_stmt | while_stmt | for_stmt | return_stmt | expr_stmt
if_stmt        := 'if' '(' expression ')' body_statement ( 'else' body_statement )?   // else binds to nearest if
while_stmt     := 'while' '(' expression ')' body_statement
for_stmt       := 'for' '(' for_init? ';' expression? ';' expression? ')' body_statement
for_init       := type IDENT ( '=' expression )?  |  expression
return_stmt    := 'return' expression? ';'
expr_stmt      := expression? ';'
expression     := assignment
assignment     := logical_or ( '=' assignment )?              // right-assoc; lhs MUST be a variable
logical_or     := logical_and ( '||' logical_and )*
logical_and    := equality ( '&&' equality )*
equality       := comparison ( ( '==' | '!=' ) comparison )*
comparison     := additive ( ( '<' | '>' | '<=' | '>=' ) additive )*
additive       := multiplicative ( ( '+' | '-' ) multiplicative )*
multiplicative := unary ( ( '*' | '/' | '%' ) unary )*
unary          := ( '-' | '!' ) unary | primary
primary        := NUMBER | CHAR_LITERAL | STRING
                | IDENT '(' ( expression ( ',' expression )* )? ')'
                | IDENT
                | '(' expression ')'
```

### Scoping rules

- Functions and global variables share one global namespace (as in C). Functions are registered before any body is checked, so forward calls and recursion work.
- A function's parameters and the top-level statements of its body share **one** scope, so `int f(int a) { int a; }` is a redeclaration error.
- Every nested block opens a new scope. A `for` statement opens one scope for its init declaration and another for its body block.
- Shadowing a name from an outer scope is allowed.

## Error Messages

All diagnostics go to stderr and carry a line and column (both start at 1).

### Lexer errors (fatal)

```
Lexer Error at line 3, column 13: unexpected char '@'
```

| Situation                                | Message                                       |
|------------------------------------------|-----------------------------------------------|
| unknown character (including lone `&`, `\|`) | `unexpected char '@'`                     |
| unterminated `/*`                        | `unterminated comment`                        |
| unterminated string or newline in string | `unterminated string literal`                 |
| bad escape                               | `invalid escape sequence '\q'`                |
| `''`                                     | `empty character literal`                     |
| unterminated or multi-character literal  | `unterminated or multi-character literal`     |
| number larger than `INT_MAX`             | `integer literal too large`                   |

### Parser errors (fatal, first error only)

```
Parser Error at line 3, column 1: expected ';' but got '}'
Parser Error at line 2, column 13: expected expression but got ';'
Parser Error at line 4, column 7: invalid assignment target
```

The format is `expected <what> but got <token>`, where `<what>` is for example `';'`, `')'`, `identifier`, `expression`, `type`, `statement` or `declaration`, and `<token>` is `'<lexeme>'`, `string "<lexeme>"`, `character literal '<lexeme>'` or `end of file`.

### Semantic errors and warnings

The checker reports **every** problem it finds rather than stopping at the first one. Errors count towards the failure total; warnings do not.

```
Warning at line 7, column 11: Variable 'x' may be used uninitialized
Semantic Error at line 8, column 11: Undefined variable 'y'
Semantic Error at line 9, column 11: Function 'add' expects 2 argument(s) but got 3
minic: compilation failed with 2 error(s)
```

| Check                                    | Message                                                                                  |
|------------------------------------------|------------------------------------------------------------------------------------------|
| undeclared variable                      | `Undefined variable 'y'`                                                                 |
| undeclared function                      | `Undefined function 'foo'`                                                               |
| redeclared variable in the same scope    | `Variable 'x' already declared`                                                          |
| redeclared function                      | `Function 'f' already declared`                                                          |
| calling a variable                       | `'x' is not a function`                                                                  |
| using a function as a value              | `'f' is a function, not a variable`                                                      |
| string assigned to a variable            | `Type mismatch: cannot assign string to 'int' variable 'x'`                              |
| void value assigned to a variable        | `Type mismatch: cannot assign void value to 'int' variable 'x'`                          |
| void variable or parameter               | `Variable 'x' cannot have type void`                                                     |
| wrong argument count (including `print`) | `Function 'add' expects 2 argument(s) but got 3`                                         |
| wrong argument type                      | `Type mismatch in argument 1 of 'add': expected 'int' but got 'string'`                  |
| string literal outside `print`           | `String literals are only allowed as the argument of print()`                            |
| void value inside an expression          | `Void value used in expression`                                                          |
| non-integer condition                    | `Condition must be an integer expression`                                                |
| `return expr;` in a void function        | `Return type mismatch: function 'f' returns void but a value was returned`               |
| `return;` in a non-void function         | `Return type mismatch: function 'f' must return a value of type 'int'`                   |
| returning a string or void value         | `Return type mismatch: function 'f' must return 'int' but got 'string'`                  |
| right operand of `/` or `%` is literal 0 | `Division by zero`                                                                       |
| non-constant global initializer          | `Global variable 'g' must be initialized with a constant expression`                     |
| no `main`                                | `No 'main' function defined`                                                             |
| wrong `main` signature                   | `'main' must return int and take no parameters`                                          |
| reserved identifier used as a name       | `'printf' is a reserved identifier`                                                      |
| **warning:** read before assignment      | `Variable 'x' may be used uninitialized`                                                 |
| **warning:** missing return on some path | `Function 'f' may not return a value on all paths`                                       |

Reserved identifiers cannot be used for any variable, parameter or function. They are `print` (the builtin), C keywords Mini-C does not support (`break`, `long`, `struct`, `unsigned`, ...), and libc names that would collide with the generated code (`printf`, `puts`, `malloc`, `exit`, `abs`, `sqrt`, `time`, ...).

## Project Structure

```
minic-compiler/
|-- README.md             this file
|-- PLAN.md               implementation plan and interface contract
|-- Makefile              build, test, examples, memcheck, clean
|-- .gitignore            ignores objects, binaries and build/
|-- LICENSE               MIT license
|-- .github/workflows/ci.yml  CI: build + tests on Linux (gcc, clang) and macOS (Apple clang)
|-- src/
|   |-- main.c            command-line driver: reads the file, runs the pipeline, handles options and exit codes
|   |-- lexer.h           Token, TokenType, TokenList and lexer API
|   |-- lexer.c           hand-written scanner, comments, escapes, positions, token printing
|   |-- ast.h             node kinds, operators, types and the Node structure
|   |-- ast.c             node construction, recursive free, AST pretty-printer
|   |-- parser.h          parse_program declaration
|   |-- parser.c          recursive-descent parser, one function per grammar rule
|   |-- checker.h         check_program declaration
|   |-- checker.c         scoped symbol table, type rules, errors and warnings
|   |-- codegen.h         codegen_program declaration
|   |-- codegen.c         C emitter: layout, parenthesization, print lowering
|   |-- interp.h          interp_run declaration
|   `-- interp.c          tree-walking interpreter behind --run (and the playground's Run button)
|-- playground/
|   |-- template.html     the web playground UI (editor, tabs, pipeline strip)
|   |-- minic-wasi.js     minimal WASI host that runs minic.wasm in the browser and in Node
|   |-- build.mjs         bundles wasm + shim + examples into one HTML file
|   `-- test.mjs          wasm vs native equivalence tests
|-- examples/
|   |-- *.c               sample Mini-C programs (hello, variables, arithmetic, if_else, while_loop, function, factorial, ...)
|   `-- expected/*.txt    exact expected stdout for each example
`-- tests/
    `-- test.sh           end-to-end test suite
```

## Architecture Notes

Each stage lives in its own module with a deliberately tiny interface. The driver is the only code that knows the order of the stages.

| Module   | Entry point                                       | Contract                                                                                      |
|----------|---------------------------------------------------|-----------------------------------------------------------------------------------------------|
| Lexer    | `TokenList lexer_tokenize(const char *source)`    | Returns a token array terminated by exactly one `TOK_EOF`. Reports a lexical error and exits on failure. |
| Parser   | `Node *parse_program(const TokenList *tokens)`    | Returns a `NODE_PROGRAM` tree. Reports the first syntax error and exits.                      |
| Checker  | `int check_program(Node *program)`                | Annotates `eval_type` on expression nodes, prints all errors and warnings, returns the error count (0 means success). |
| Codegen  | `void codegen_program(const Node *program, FILE *out)` | Writes the C translation. Assumes the checker returned 0.                                |
| Interpreter | `int interp_run(const Node *program, FILE *out, long max_steps, int *exit_code)` | Executes the checked AST; a runtime error is printed and exits 1, like lexer and parser errors. |

Supporting API: `token_list_free`, `token_print`, `token_list_print` (lexer); `ast_new_node`, `node_list_push`, `ast_free`, `print_ast` (ast).

**Memory ownership.**

- The `TokenList` owns the array of tokens and every token's heap-allocated `lexeme`. The caller releases it with `token_list_free`.
- The AST is a strict tree: every child pointer and every entry in `items` or `params` is owned by exactly one parent, and strings (`name`, `str_value`) are owned heap copies. `ast_free` releases the whole tree recursively.
- The parser copies what it needs out of the tokens, so the AST does not point into the token list.
- The checker owns its symbol table and scopes and frees them before returning; it only writes `eval_type` into the AST.
- On the success path the driver frees the source buffer, token list and AST, so a normal run is valgrind-clean. On fatal error paths (`exit(1)`) the process simply exits.
- Out of memory prints `minic: out of memory` and exits 1.

**Coding standards.** C11 only, standard headers only, zero warnings under `-Wall -Wextra -Wpedantic` on both gcc and clang, 4-space indentation, a comment above every function, and no global mutable state beyond file-local `static` data where clearly necessary.

## Testing

```sh
make test       # build, then run tests/test.sh
make memcheck   # valgrind over the examples (if installed)
make examples   # compile, link and run every example
make playground # wasm build + wasm-vs-native equivalence tests (needs WASI clang + Node)
```

`tests/test.sh` is a Bash script that can be run from any directory. It uses the built `./minic` (and fails clearly if it is missing), writes scratch files to a `mktemp -d` directory that is removed on exit, and compiles generated C with `gcc -std=c11 -Wall -Wextra -Werror`. It prints `PASS` or `FAIL` for each test, a summary line `N passed, M failed`, and exits 1 if anything failed. A crash (exit code 128 or higher) always counts as a failure.

The suite currently runs **83 tests**, all passing (plus **54** wasm-vs-native checks in `playground/test.mjs`). Test categories:

- **Lexer**: token stream for `int x = 5;`, comments ignored, lexical errors (unknown character, unterminated comment).
- **Parser and AST**: precedence shape of `5 + 3 * 2` in `--ast` output, syntax errors such as `int x = ;`, invalid assignment target.
- **Execution**: hello world, variables and arithmetic, `if`/`else`, `else if` chains, `while` and `for` loops, function calls, recursion (factorial, fibonacci), global variables, nested scopes and shadowing.
- **Operators**: precedence and associativity (`10 - 3 - 2`, `2 * (3 + 4)`), `%`, unary minus, `!`, comparisons, `&&` and `||` short-circuit.
- **Output forms**: `char` printing, strings with escapes.
- **Semantic errors**: undefined variable, redeclaration, type mismatch, wrong argument count, return type mismatch, missing `main`.
- **Warnings**: uninitialized variable use and missing returns exit 0 with a warning; a function with a missing return still produces `-Werror`-clean C.
- **CLI**: `--help` exits 0, a missing file exits 1.
- **Examples**: every `examples/*.c` goes through `minic`, `gcc` and execution, and its stdout is compared with `examples/expected/<name>.txt`.
- **Interpreter (`--run`)**: every example must print exactly its expected output; exit codes, `int`/`char` wrap-around, C division rounding, short-circuit order, shadowing, 4000-deep recursion, escapes; runtime errors (division by zero, step limit, runaway recursion); reading the program from stdin.
- **Memory**: a valgrind leak check on one example (`--error-exitcode=1 --leak-check=full`), when valgrind is available.

## What I Learned

**Lexer.** A lexer is a small state machine that turns a flat stream of characters into a flat stream of tokens, and almost all of its difficulty lives in the edges: two-character operators like `<=` and `&&`, comments that must vanish without disturbing line numbers, escape sequences, and unterminated strings. I learned to keep one-character lookahead and to record the line and column of a token's *first* character, because every later error message depends on those positions being right. Keeping `print` as an ordinary identifier instead of a keyword also taught me where to draw the line between the language and its library.

**Parser (recursive descent).** Recursive descent is the most direct way to turn a grammar into code: each rule becomes one function, and the call stack mirrors the shape of the derivation. Writing it by hand made the grammar tangible. Decisions like the `type IDENT '('` lookahead that separates function definitions from variable declarations, and restricting `if`/`while`/`for` bodies to non-declarations, fell out of the grammar rather than being bolted on. Reporting only the first syntax error is a deliberate trade-off: error recovery is a big topic of its own, and a clear first error beats a cascade of misleading ones.

**AST.** The AST is the contract between every stage, so designing it first paid off. One uniform `Node` struct with a `kind` tag, a handful of child pointers and two child lists keeps traversal code short, and a strict tree with single ownership makes `ast_free` a trivial recursive walk. Being able to dump the tree with `--ast` turned out to be the best debugging tool in the project: when a program misbehaves, the printed tree immediately shows whether the parser or a later stage is at fault.

**Precedence and associativity.** In recursive descent, precedence is not a table you consult; it is the *nesting of the functions*. `additive` calls `multiplicative`, which calls `unary`, so `5 + 3 * 2` becomes `+` at the top with `*` beneath it with no extra machinery. Left associativity comes from looping (`a - b - c` accumulates left to right), while right-associative assignment comes from recursing on the right-hand side. Seeing `10 - 3 - 2` produce `5` and `a = b = 3` work for the right reason made these ideas concrete.

**Type checking, symbol tables and scopes.** The checker is where a program stops being merely well-formed and starts being meaningful. A stack of scopes gives lexical scoping and shadowing almost for free: push on entering a block, pop on leaving, and look names up from the innermost scope outwards. A two-pass design (register every function first, then walk the bodies) is what makes forward calls and recursion work. Annotating each expression with its `eval_type` means the code generator never has to re-derive types, and reporting every error in one run, rather than stopping at the first, makes the compiler far more pleasant to use.

**Code generation.** Targeting C instead of assembly let me focus on the semantics of the translation rather than on register allocation. Even so, the details matter: parenthesizing nested binary expressions preserves the AST's precedence exactly and silences compiler warnings, always emitting braced bodies avoids the dangling-`else` trap, and choosing `%d` or `%c` for `print` depends on the type the checker computed. Generating `main(void)` and a prototypes section made the output compile cleanly under `-Wall -Wextra -Werror`, which I treat as the real acceptance test.

## Limitations

Mini-C is intentionally small. Be aware of the following:

- **No pointers, arrays, structs, unions or enums.**
- **No floating-point types, and no `long`, `short` or `unsigned`.** Only `int` and `char`.
- **No `++`, `--` or compound assignment** (`+=`, `-=`, ...); write `i = i + 1`.
- **No `break`, `continue`, `do`-`while`, `switch`, `goto` or the ternary operator.**
- **No casts, no `sizeof`, no multiple declarators** (`int a, b;` is not allowed; use one declaration per variable).
- **No preprocessor:** no `#include`, `#define` or conditional compilation.
- **Decimal integer literals only:** no hex, octal or binary literals.
- **No string variables.** Strings exist only as the argument of `print`.
- **No x86-64 assembly backend yet.** `minic` transpiles to C (or interprets with `--run`); native executables come from `gcc` or `clang`.
- **Recursion depth is limited** to 5000 nested calls in the interpreter; in the browser the JavaScript engine's stack usually ends it earlier (around 2000), which is reported as a clean runtime error.
- **Uninitialized-variable analysis is flow-insensitive.** It follows source order and warns once per variable, so it can miss real problems and can occasionally warn when a variable is assigned on every actual path.
- **Single-file programs only.** There is no linking of multiple Mini-C sources.
- **Integer overflow follows C semantics**, because the generated code is compiled as ordinary C.
- **Reserved C and libc identifiers are rejected** as variable, parameter and function names (see [Error Messages](#semantic-errors-and-warnings)).
- Only the first lexical or syntax error is reported; semantic errors are reported in full.
- Input is expected to be a hand-written program, not arbitrary C: valid C that uses unsupported features is rejected by the lexer or parser.

## Roadmap

- An x86-64 assembly backend as an alternative to the C backend
- Arrays and pointers
- `break` / `continue`, `++` / `--` and compound assignment
- Additional types (`long`, `unsigned`, floating point)
- A flow-sensitive uninitialized-variable analysis
- Multiple declarators per declaration and string variables
- Multi-error recovery in the parser

## License

MIT License. See [`LICENSE`](LICENSE).
