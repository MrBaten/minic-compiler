# MiniC Compiler — Implementation Plan & Interface Contract

This document is the **single source of truth** for every module. All subagents
MUST follow it exactly. Headers in §5 are given **verbatim**: the owner of a
header copies the declarations exactly (comments may be expanded, declarations
may not change). Every other module codes against these declarations.

---

## 1. Goal

`minic` is a hand-written compiler (no Lex/Yacc/Flex/Bison, no external libs,
C11 + standard library only) for a small C subset ("Mini-C"). It transpiles
Mini-C to portable C11, which is then compiled with `gcc`/`clang`.

x86-64 assembly output is **out of scope** for this version (documented as a
limitation in the README).

---

## 2. Pipeline

```
 source.c ──► Lexer ──► TokenList ──► Parser ──► AST ──► Checker ──► annotated AST ──► Codegen ──► out.c ──► gcc ──► executable
   (text)   lexer.c                 parser.c   ast.c   checker.c                     codegen.c
```

| Stage   | Input            | Output                         | On error                                 |
|---------|------------------|--------------------------------|------------------------------------------|
| Lexer   | `const char *`   | `TokenList`                    | print to stderr, `exit(1)`               |
| Parser  | `TokenList *`    | `Node *` (NODE_PROGRAM)        | print to stderr, `exit(1)` (first error) |
| Checker | `Node *`         | error count; sets `eval_type`  | prints ALL errors + warnings, returns N  |
| Codegen | `const Node *`   | C text written to `FILE *`     | never fails (input already checked)      |
| Driver  | argv             | output file / stdout           | exit code 1                              |

---

## 3. File structure & ownership (no two agents touch the same file)

```
minic-compiler/
  src/main.c                 Agent-6 (driver)
  src/lexer.h  src/lexer.c   Agent-1 (lexer)
  src/ast.h    src/ast.c     Agent-2 (ast)
  src/parser.h src/parser.c  Agent-3 (parser)
  src/checker.h src/checker.c Agent-4 (checker)
  src/codegen.h src/codegen.c Agent-5 (codegen)
  Makefile  .gitignore       Agent-6 (driver)
  examples/*.c  examples/expected/*.txt   Agent-7 (examples)
  tests/test.sh              Agent-8 (tests)
  README.md                  Agent-9 (docs)
  PLAN.md                    Main agent (read-only for subagents)
```

Scratch work: an agent may create throw-away files ONLY under `/tmp/<agent-name>/`
(e.g. `/tmp/agent-lexer/`) — e.g. copies of headers from §5 to syntax-check or
unit-test its own file. Never create or edit any other file inside the project.

---

## 4. Coding standards (all C files)

* C11, must compile with **zero warnings** under
  `gcc -std=c11 -Wall -Wextra -Wpedantic` and `clang -std=c11 -Wall -Wextra -Wpedantic`.
* Every function (including `static` helpers) has a short comment above it.
* No `TODO`, `FIXME`, `XXX`. No dead code. No global mutable state except
  `static` file-local state where clearly necessary.
* Out-of-memory: print `minic: out of memory` to stderr and `exit(1)`.
* No memory leaks on the success path (valgrind-clean). On fatal error paths
  (`exit(1)`) leaking is acceptable.
* Include only standard headers (`stdio.h stdlib.h string.h ctype.h limits.h stdarg.h`).
* 4-space indentation, no tabs.

---

## 5. Verbatim header contracts

### 5.1 `src/lexer.h` (Agent-1)

```c
#ifndef MINIC_LEXER_H
#define MINIC_LEXER_H

#include <stdio.h>

/* All token kinds produced by the lexer. */
typedef enum {
    TOK_INT, TOK_CHAR, TOK_VOID, TOK_IF, TOK_ELSE, TOK_WHILE, TOK_FOR, TOK_RETURN,
    TOK_IDENTIFIER, TOK_NUMBER, TOK_STRING, TOK_CHAR_LITERAL,
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_ASSIGN, TOK_EQ, TOK_NEQ, TOK_LT, TOK_GT, TOK_LE, TOK_GE,
    TOK_AND, TOK_OR, TOK_NOT,
    TOK_LPAREN, TOK_RPAREN, TOK_LBRACE, TOK_RBRACE, TOK_SEMICOLON, TOK_COMMA,
    TOK_EOF
} TokenType;

/* One token. `lexeme` is heap-allocated and owned by the TokenList. */
typedef struct {
    TokenType type;
    char *lexeme;
    int int_value;
    int line;
    int column;
} Token;

/* Growable array of tokens; always terminated by exactly one TOK_EOF token. */
typedef struct {
    Token *tokens;
    int count;
    int capacity;
} TokenList;

TokenList lexer_tokenize(const char *source);
void token_list_free(TokenList *list);
const char *token_type_name(TokenType type);
void token_print(const Token *tok, FILE *out);
void token_list_print(const TokenList *list, FILE *out);

#endif
```

### 5.2 `src/ast.h` (Agent-2)

```c
#ifndef MINIC_AST_H
#define MINIC_AST_H

#include <stdio.h>

/* Mini-C types. TYPE_STRING is only the type of a string literal expression. */
typedef enum { TYPE_UNKNOWN = 0, TYPE_INT, TYPE_CHAR, TYPE_VOID, TYPE_STRING } TypeKind;

/* All AST node kinds. */
typedef enum {
    NODE_PROGRAM, NODE_VAR_DECL, NODE_FUNC_DECL, NODE_BLOCK,
    NODE_IF_STMT, NODE_WHILE_STMT, NODE_FOR_STMT, NODE_RETURN_STMT, NODE_EXPR_STMT,
    NODE_BINARY_EXPR, NODE_UNARY_EXPR, NODE_ASSIGN_EXPR, NODE_CALL_EXPR,
    NODE_VAR_EXPR, NODE_NUMBER_EXPR, NODE_CHAR_EXPR, NODE_STRING_EXPR
} NodeKind;

/* Binary operators (OP_ADD..OP_OR) and unary operators (OP_NEG, OP_NOT). */
typedef enum {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_EQ, OP_NEQ, OP_LT, OP_GT, OP_LE, OP_GE,
    OP_AND, OP_OR,
    OP_NEG, OP_NOT
} OpKind;

typedef struct Node Node;

/* Growable array of owned child nodes. A zeroed NodeList is a valid empty list. */
typedef struct {
    Node **items;
    int count;
    int capacity;
} NodeList;

/* One AST node. Field usage per kind is documented in PLAN.md §6. */
struct Node {
    NodeKind kind;
    int line;
    int column;
    TypeKind eval_type;
    char *name;
    char *str_value;
    int int_value;
    TypeKind decl_type;
    OpKind op;
    int is_global;
    Node *left;
    Node *right;
    Node *expr;
    Node *init;
    Node *cond;
    Node *update;
    Node *body;
    Node *else_branch;
    NodeList items;
    NodeList params;
};

Node *ast_new_node(NodeKind kind, int line, int column);
char *ast_strdup(const char *s);
void node_list_push(NodeList *list, Node *node);
void ast_free(Node *node);
void print_ast(const Node *node, int indent);
const char *ast_node_kind_name(NodeKind kind);
const char *ast_op_symbol(OpKind op);
const char *ast_type_name(TypeKind type);

#endif
```

### 5.3 `src/parser.h` (Agent-3)

```c
#ifndef MINIC_PARSER_H
#define MINIC_PARSER_H

#include "lexer.h"
#include "ast.h"

Node *parse_program(const TokenList *tokens);

#endif
```

### 5.4 `src/checker.h` (Agent-4)

```c
#ifndef MINIC_CHECKER_H
#define MINIC_CHECKER_H

#include "ast.h"

int check_program(Node *program);

#endif
```

### 5.5 `src/codegen.h` (Agent-5)

```c
#ifndef MINIC_CODEGEN_H
#define MINIC_CODEGEN_H

#include <stdio.h>
#include "ast.h"

void codegen_program(const Node *program, FILE *out);

#endif
```

---

## 6. Data contracts

### 6.1 Tokens (lexer)

| Token            | `lexeme`                                                        | `int_value`        |
|------------------|-----------------------------------------------------------------|--------------------|
| keywords         | exact text: `int char void if else while for return`            | 0                  |
| `TOK_IDENTIFIER` | the name (`[A-Za-z_][A-Za-z0-9_]*`, not a keyword)              | 0                  |
| `TOK_NUMBER`     | decimal digits text, e.g. `"42"`                                | the value          |
| `TOK_STRING`     | text **between** the quotes, escapes kept raw (`Hi\n` = 4 chars) | 0                  |
| `TOK_CHAR_LITERAL` | text **between** the quotes, raw (`a`, `\n`, `\'`)            | decoded char code  |
| operators/punct  | exact text: `+ - * / % = == != < > <= >= && \|\| ! ( ) { } ; ,` | 0                  |
| `TOK_EOF`        | `""` (heap-allocated empty string)                              | 0                  |

* `print` is NOT a keyword — it is a `TOK_IDENTIFIER`.
* Lines and columns start at 1. A tab counts as one column. A token's position
  is the position of its first character. EOF gets the position just past the last char.
* Whitespace: space, `\t`, `\r`, `\n`, `\v`, `\f`.
* Comments: `// ...` to end of line; `/* ... */` (not nested).
* Valid escapes in strings and char literals: `\n \t \r \0 \\ \' \"`.
* Strings may not contain a raw newline. A char literal holds exactly one
  (possibly escaped) character.
* Numbers: decimal only, value must fit in `int` (≤ 2147483647).
* Keywords recognized: `int char void if else while for return`.

`token_type_name(t)` returns the enum name without the `TOK_` prefix:
`"INT"`, `"IDENTIFIER"`, `"NUMBER"`, `"CHAR_LITERAL"`, `"EOF"`, ...

`token_print(tok, out)` prints ONE line (with trailing `\n`):

| Token kind         | Printed as              |
|--------------------|-------------------------|
| `TOK_IDENTIFIER`   | `ID(x)`                 |
| `TOK_NUMBER`       | `NUMBER(5)`             |
| `TOK_STRING`       | `STRING("Hello\n")` (raw lexeme) |
| `TOK_CHAR_LITERAL` | `CHAR_LITERAL('a')` (raw lexeme) |
| everything else    | `token_type_name()` e.g. `INT`, `ASSIGN`, `SEMICOLON`, `EOF` |

`token_list_print` prints every token (including EOF) with `token_print`.
So `int x = 5;` prints exactly:
```
INT
ID(x)
ASSIGN
NUMBER(5)
SEMICOLON
EOF
```

**Lexer error messages** (stderr, then `exit(1)`), exact format
`Lexer Error at line L, column C: <message>`:

| Situation                        | `<message>`                                   |
|----------------------------------|-----------------------------------------------|
| unknown char (incl. lone `&`/`\|`) | `unexpected char '@'` (non-printable: `unexpected char '\xNN'`) |
| unterminated `/*`                | `unterminated comment`                        |
| unterminated string / newline in string | `unterminated string literal`          |
| bad escape                       | `invalid escape sequence '\q'`                |
| `''`                             | `empty character literal`                     |
| unterminated / multi-char literal| `unterminated or multi-character literal`     |
| number > INT_MAX                 | `integer literal too large`                   |

### 6.2 AST node field usage

All nodes come from `ast_new_node` (zero-initialised via `calloc`; `eval_type`
starts as `TYPE_UNKNOWN`). Strings (`name`, `str_value`) are heap copies made
with `ast_strdup` and are owned by the node. Every child pointer / list entry
is owned by exactly one parent (a tree, never a DAG). Unused fields stay 0/NULL.

| Kind                | Fields used                                                                       |
|---------------------|-----------------------------------------------------------------------------------|
| `NODE_PROGRAM`      | `items` = top-level VAR_DECL / FUNC_DECL in source order                          |
| `NODE_VAR_DECL`     | `name`, `decl_type`, `init` (initializer expr or NULL), `is_global` (1 at top level) |
| `NODE_FUNC_DECL`    | `name`, `decl_type` (return type), `params` (VAR_DECL nodes, `init` NULL), `body` (BLOCK) |
| `NODE_BLOCK`        | `items` = statements                                                              |
| `NODE_IF_STMT`      | `cond`, `body` (then statement), `else_branch` (statement or NULL)                |
| `NODE_WHILE_STMT`   | `cond`, `body`                                                                    |
| `NODE_FOR_STMT`     | `init` (VAR_DECL node, OR a bare expression node, OR NULL), `cond` (expr or NULL), `update` (expr or NULL), `body` |
| `NODE_RETURN_STMT`  | `expr` (value or NULL)                                                            |
| `NODE_EXPR_STMT`    | `expr` (NULL means the empty statement `;`)                                       |
| `NODE_BINARY_EXPR`  | `op` (OP_ADD..OP_OR), `left`, `right`                                             |
| `NODE_UNARY_EXPR`   | `op` (OP_NEG or OP_NOT), `expr`                                                   |
| `NODE_ASSIGN_EXPR`  | `left` (always a NODE_VAR_EXPR), `right` (value)                                  |
| `NODE_CALL_EXPR`    | `name` (callee, may be `print`), `items` = argument expressions                   |
| `NODE_VAR_EXPR`     | `name`                                                                            |
| `NODE_NUMBER_EXPR`  | `int_value`                                                                       |
| `NODE_CHAR_EXPR`    | `int_value` (char code), `str_value` (raw text between quotes, e.g. `a` or `\n`)  |
| `NODE_STRING_EXPR`  | `str_value` (raw text between quotes, escapes kept raw)                           |

`line`/`column` of a node = position of its first token (for binary/assign
expressions: position of the operator token is also acceptable; use the operator).

### 6.3 AST helper semantics (`ast.c`)

* `ast_new_node(kind, line, column)` — `calloc` a node, set kind/line/column.
* `ast_strdup(s)` — heap copy (`s` NULL → returns NULL).
* `node_list_push(list, node)` — append, growing capacity (start 4, double).
* `ast_free(node)` — NULL-safe, recursive: frees `name`, `str_value`, all child
  pointers (`left right expr init cond update body else_branch`), every node in
  `items` and `params` plus the arrays themselves, then the node.
* `ast_node_kind_name`: `"Program" "VarDecl" "FuncDecl" "Block" "IfStmt" "WhileStmt" "ForStmt" "ReturnStmt" "ExprStmt" "BinaryExpr" "UnaryExpr" "AssignExpr" "CallExpr" "VarExpr" "NumberExpr" "CharExpr" "StringExpr"`.
* `ast_op_symbol`: `"+" "-" "*" "/" "%" "==" "!=" "<" ">" "<=" ">=" "&&" "||"`, OP_NEG→`"-"`, OP_NOT→`"!"`.
* `ast_type_name`: `"unknown" "int" "char" "void" "string"`.

### 6.4 `print_ast(node, indent)` output format (stdout)

Each line is prefixed by `2*indent` spaces. Children are printed at `indent+1`.
Labelled sections print the label at `indent+1` and the child at `indent+2`.

| Node            | Line printed                                        | Children                                     |
|-----------------|-----------------------------------------------------|----------------------------------------------|
| Program         | `Program`                                           | each item                                    |
| FuncDecl        | `FuncDecl: int add(int a, int b)` / `FuncDecl: int main()` | body Block                            |
| VarDecl         | `VarDecl: int x`                                    | init expr (if any)                           |
| Block           | `Block`                                             | each statement                               |
| IfStmt          | `IfStmt`                                            | `Condition:` cond, `Then:` body, `Else:` else_branch (only if present) |
| WhileStmt       | `WhileStmt`                                         | `Condition:` cond, `Body:` body              |
| ForStmt         | `ForStmt`                                           | `Init:` / `Condition:` / `Update:` (each only if present), `Body:` body |
| ReturnStmt      | `ReturnStmt`                                        | expr (if any)                                |
| ExprStmt        | `ExprStmt` (or `ExprStmt (empty)` if expr NULL)     | expr                                         |
| BinaryExpr      | `BinaryExpr: +`                                     | left, right                                  |
| UnaryExpr       | `UnaryExpr: -`                                      | expr                                         |
| AssignExpr      | `AssignExpr: x`                                     | right (value)                                |
| CallExpr        | `CallExpr: add`                                     | each argument                                |
| VarExpr         | `VarExpr: x`                                        | –                                            |
| NumberExpr      | `NumberExpr: 5`                                     | –                                            |
| CharExpr        | `CharExpr: 'a'` (raw str_value)                     | –                                            |
| StringExpr      | `StringExpr: "Hello\n"` (raw str_value)             | –                                            |

Example — `int x = 5 + 3 * 2;` at top level, `print_ast(prog, 0)`:
```
Program
  VarDecl: int x
    BinaryExpr: +
      NumberExpr: 5
      BinaryExpr: *
        NumberExpr: 3
        NumberExpr: 2
```

---

## 7. Grammar (recursive descent, Agent-3)

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
assignment     := logical_or ( '=' assignment )?              // right-assoc; lhs MUST be VarExpr
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

Precedence, highest → lowest: primary, unary (`- !`), `* / %`, `+ -`,
`< > <= >=`, `== !=`, `&&`, `||`, `=`. All binary operators are left-associative;
`=` is right-associative.

A declaration is not allowed as the direct body of `if/else/while/for`
(only inside a block) — that is why bodies use `body_statement`.

**Parser error messages** (stderr, `exit(1)` on the first error), format:
`Parser Error at line L, column C: expected <what> but got <tok>`
where `<tok>` is: `'<lexeme>'` for keywords, identifiers, numbers, operators and
punctuation (e.g. `'}'`, `'x'`, `'42'`); `string "<lexeme>"` for strings;
`character literal '<lexeme>'` for char literals; `end of file` for EOF.
Typical `<what>` values: `';'`, `')'`, `'('`, `'{'`, `'}'`, `identifier`, `expression`,
`type`, `statement`, `declaration` (top level).
Examples:
```
Parser Error at line 3, column 1: expected ';' but got '}'
Parser Error at line 1, column 20: expected expression but got ';'
```
Invalid assignment target (e.g. `5 = x;`) →
`Parser Error at line L, column C: invalid assignment target`.

---

## 8. Semantic checker (Agent-4)

`int check_program(Node *program)` walks the AST, prints every error and
warning to **stderr**, annotates `eval_type` on every expression node, and
returns the number of errors (0 = success). Warnings do not count.

Formats:
```
Semantic Error at line L, column C: <message>
Warning at line L, column C: <message>
```

### 8.1 Symbol table
* Scope chain (stack of scopes). Symbol = { name, kind (variable | function),
  type, param count + param types (functions), initialized flag, warned flag }.
* Global scope holds all functions **and** global variables (they share a
  namespace, like C).
* **Pass 1**: insert every FuncDecl into the global scope (so functions may be
  called before their definition and recursion works).
* **Pass 2**: walk items in source order. Global variables become visible only
  after their declaration.
* Function scope: params and the function body's top-level statements share
  ONE scope (so `int f(int a) { int a; }` is a redeclaration error, as in C).
  Every nested block, if/while/for body block, gets a new scope. A `for`
  statement opens a scope for its init declaration; its body block opens another.
  Shadowing an outer-scope name is allowed.

### 8.2 Type rules
* `int` and `char` are both integer types and are mutually assignable.
* Variables and params must not be `void`.
* Binary/unary operators require int/char operands; result type is `int`.
* `NODE_NUMBER_EXPR` → int, `NODE_CHAR_EXPR` → **char**, `NODE_STRING_EXPR` → string,
  `NODE_VAR_EXPR` → declared type, `NODE_CALL_EXPR` → return type (`print` → void),
  `NODE_ASSIGN_EXPR` → type of the target variable.
* String literals are allowed ONLY as the single argument of `print()`.
* A void value may only appear as the whole expression of an ExprStmt or as
  the whole `for` update expression.
* Conditions (if/while/for) must be int/char.

### 8.3 Errors (exact message text — tests grep these substrings)

| Situation | Message |
|---|---|
| use of undeclared variable | `Undefined variable 'y'` |
| call of undeclared function | `Undefined function 'foo'` |
| redeclared variable in same scope | `Variable 'x' already declared` |
| redeclared function / name clash in global scope | `Function 'f' already declared` (function) / `Variable 'x' already declared` (variable) |
| calling a variable | `'x' is not a function` |
| using a function as a value / assigning to it | `'f' is a function, not a variable` |
| init/assign string to variable | `Type mismatch: cannot assign string to 'int' variable 'x'` |
| init/assign void value | `Type mismatch: cannot assign void value to 'int' variable 'x'` |
| void variable or param | `Variable 'x' cannot have type void` |
| wrong arg count (incl. print) | `Function 'add' expects 2 argument(s) but got 3` |
| bad arg type | `Type mismatch in argument 1 of 'add': expected 'int' but got 'string'` (or `'void'`) |
| string outside print | `String literals are only allowed as the argument of print()` |
| void value inside an expression | `Void value used in expression` |
| non-integer condition | `Condition must be an integer expression` |
| `return expr;` in void function | `Return type mismatch: function 'f' returns void but a value was returned` |
| `return;` in non-void function | `Return type mismatch: function 'f' must return a value of type 'int'` |
| returning string / void value | `Return type mismatch: function 'f' must return 'int' but got 'string'` |
| right operand of `/` or `%` is literal 0 | `Division by zero` |
| global initializer not constant (only NUMBER/CHAR/UNARY/BINARY of those allowed) | `Global variable 'g' must be initialized with a constant expression` |
| no `main` | `No 'main' function defined` (line/column 1, 1) |
| main has wrong signature | `'main' must return int and take no parameters` |
| reserved identifier as any declared name | `'printf' is a reserved identifier` |

Reserved identifiers (any variable/param/function name): `print` (builtin) plus
C names that would break the generated C: `auto break case const continue default do double enum extern float goto inline long register restrict short signed sizeof static struct switch typedef union unsigned volatile _Bool _Complex _Imaginary _Alignas _Alignof _Atomic _Generic _Noreturn _Static_assert _Thread_local`
and library names `printf puts putchar getchar scanf sprintf fprintf fopen fclose remove rename perror fflush fgets fputs getc putc EOF NULL FILE stdin stdout stderr size_t abs exit malloc free exp log pow sqrt sin cos tan floor ceil round fabs strlen strcmp strcpy memcpy memset atoi rand srand time`.

### 8.4 Warnings
| Situation | Message |
|---|---|
| reading a local variable that was declared without initializer and not yet assigned (flow-insensitive, source order; warn once per variable) | `Variable 'x' may be used uninitialized` |
| non-void function other than `main` whose body does not definitely return | `Function 'f' may not return a value on all paths` |

Params and globals count as initialized. "Definitely returns": a statement list
returns if any statement in it returns; `if` returns only if it has an else and
both branches return; a return statement returns; loops do not.

---

## 9. Code generation (Agent-5)

`codegen_program(program, out)` assumes `check_program` returned 0 (so every
expression has a valid `eval_type`). Output layout:

```c
/* Generated by minic - do not edit */
#include <stdio.h>

int counter = 0;            <- global variables in source order (section omitted if none)

int add(int a, int b);      <- prototypes of every function except main, source order (omitted if none)

int add(int a, int b) {     <- definitions in source order, one blank line between them
    return a + b;
}

int main(void) {
    int x = 5 + 3;
    printf("%d\n", x);
    return 0;
}
```
* Exactly one blank line between sections and between functions; file ends with `\n`.
* Functions with no params are emitted as `name(void)`.
* (Added during integration) A non-void function whose body does not definitely
  return (§8.4 rules) gets a final `return 0; /* inserted by minic: missing return */`
  so the output stays `-Wreturn-type` clean. The checker still emits its warning.
* 4 spaces per indent level.
* Statement bodies of if/else/while/for are ALWAYS emitted as braced blocks
  (a non-block body is wrapped in `{ }`). `else if` chains: when `else_branch`
  is an IF_STMT emit `} else if (...) {`.
* `for` init declaration is emitted inline: `for (int i = 0; i < 3; i = i + 1) {`;
  absent parts are empty: `for (; ; ) {` is fine (`for (;;)` also fine).
* Empty statement → `;`.
* **Expressions**: a binary expression that is nested inside another expression
  is wrapped in parentheses; the top-level expression of a statement/initializer
  is not. E.g. `x = 5 + 3 * 2` → `x = 5 + (3 * 2)`, `a && b || c` → `(a && b) || c`.
  This preserves the AST's precedence exactly and avoids `-Wparentheses`.
  An AssignExpr nested inside another expression, or used as a condition, is
  wrapped in parentheses (`if ((x = 5))`). Unary: `-x`, `!x`; if the operand is
  itself a unary expression, parenthesize it (`-(-x)`, `!(!x)`) so `--` is never emitted.
* Char literal → `'<raw>'`; number → decimal; variables/calls by name.
* **print** (builtin, exactly one argument):
  * string argument: if raw text contains no `%` → `printf("<raw>\n")`,
    otherwise `printf("%s\n", "<raw>")`.
  * argument with `eval_type == TYPE_CHAR` → `printf("%c\n", <expr>)`
  * otherwise → `printf("%d\n", <expr>)`
* When emitting a raw string literal, replace every `??` with `?\?` (avoids C trigraphs).
* Generated code must compile with `gcc -std=c11 -Wall -Wextra -Werror` for
  any program that has no checker warnings and no unused variables/params.

---

## 10. Driver & build (Agent-6)

```
Usage: minic <input.c> [-o <output.c>] [--tokens] [--ast]
       minic -h | --help
```
* `--tokens`: lex, print tokens (`token_list_print` to stdout), exit 0.
* `--ast`: lex + parse, `print_ast(program, 0)`, exit 0 (checker not run).
* default: lex → parse → check → codegen. If `-o` is given write there, else to stdout.
  If the checker reports errors print `minic: compilation failed with N error(s)` to stderr,
  do NOT create the output file, exit 1.
* Options may appear in any order. Unknown option / missing input / missing `-o` arg →
  usage to stderr, exit 1. Unreadable input → `minic: error: cannot open file '<path>'`, exit 1.
* Read the whole file into a heap buffer; free buffer, tokens and AST before a successful exit.

**Makefile** targets: `all` (default; builds `./minic` from `src/*.c`, objects in `build/`),
`test` (builds then runs `bash tests/test.sh`), `examples` (compile every `examples/*.c`
with minic into `build/examples/<name>.c`, then gcc it to `build/examples/<name>`, run it),
`memcheck` (run valgrind over the examples if available), `clean`.
Flags: `CC ?= gcc`, `CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -g`.

`.gitignore`: `*.o`, `minic`, `out.c`, `out`, `build/`.

---

## 11. Examples (Agent-7)

`examples/` must contain at least: `hello.c variables.c arithmetic.c if_else.c while_loop.c function.c factorial.c`
(plus optional extras: `fibonacci.c fizzbuzz.c primes.c`). For each `examples/<name>.c` there is an
`examples/expected/<name>.txt` with the exact stdout of the compiled program.
Each example: header comment, uses only Mini-C features, has a `int main()` returning 0,
uses every declared variable and parameter (generated C is compiled with `-Werror`),
and triggers no checker warning.
`hello.c` prints exactly `Hello, World!`. `factorial.c` prints `120` for `factorial(5)` (among others).

---

## 12. Tests (Agent-8) — `tests/test.sh`

Bash script, runnable from any directory (it `cd`s to the project root), uses
`./minic`, builds nothing itself (Makefile `test` builds first; script fails clearly if
`./minic` is missing). Writes temp files to a `mktemp -d` dir removed via `trap`.
Generated C compiled with `${CC:-gcc} -std=c11 -Wall -Wextra -Werror`.
Prints `PASS`/`FAIL` per test with a name, a summary `N passed, M failed`, exits 1 if any fail.
A crash (exit code ≥ 128) is always a failure.

Required tests:
1. `--tokens` on `int x = 5;` → exactly `INT ID(x) ASSIGN NUMBER(5) SEMICOLON EOF`.
2. `--ast` on `int x = 5 + 3 * 2;` → `BinaryExpr: *` is nested under `BinaryExpr: +`;
   and running `int main() { int x = 5 + 3 * 2; print(x); return 0; }` prints `11`.
3. `examples/hello.c` → prints `Hello, World!`.
4. `int a=10; int b=20; print(a+b);` (inside main) → `30`.
5. `if (5 > 3) print(1); else print(0);` → `1`.
6. while loop 0..4 → `0 1 2 3 4` (one per line).
7. for loop → `0 1 2`.
8. `add(5,7)` → `12`.
9. `factorial(5)` → `120`.
10. `int x = ;` → stderr contains `Parser Error`, exit 1, no crash.
11. `print(y);` undeclared → stderr contains `Undefined variable 'y'`, exit 1.
12. every `examples/*.c` → minic → gcc → run succeeds; stdout matches `examples/expected/<name>.txt` if present.

Extra tests: lexer error (`@`), unterminated comment, comments ignored, char print (`a`),
string with escapes, operator precedence/associativity (`10 - 3 - 2` = 5, `2 * (3 + 4)` = 14,
`7 % 3`, unary minus, `!`), comparisons, `&&`/`||` short-circuit, nested if/else-if,
shadowing in nested scopes, recursion (fibonacci), global variable, redeclaration error,
type mismatch error, wrong arg count error, return type mismatch error,
uninitialized warning (exit 0 + `may be used uninitialized`), missing main,
invalid assignment target, `--help` exit 0, missing file exit 1,
and (if `valgrind` exists) a leak check on one example (`--error-exitcode=1 --leak-check=full`).

---

## 13. Integration (Main agent)
1. `make` with zero warnings (gcc and clang).
2. `make test` all green; `make examples`; valgrind clean.
3. Replace README sample outputs with real compiler output.

---

## 14. Extension: interpreter and web playground (added after v1)

* **`src/interp.h` / `src/interp.c`** — `int interp_run(const Node *program, FILE *out, long max_steps, int *exit_code)`.
  Tree-walking interpreter over the checked AST with the same semantics as the generated C
  (32-bit wrap-around arithmetic, `char` truncation on store, short-circuit `&&`/`||`,
  missing return in a non-void function yields 0). Runtime errors
  (`Runtime Error at line L, column C: <message>`: division by zero, `INT_MIN / -1`,
  recursion deeper than 5000 calls, step limit exceeded) are fatal: printed to stderr, exit 1.
  No `setjmp`, so the module also builds for wasm32-wasi.
* **Driver**: `--run` (execute instead of generating C; exit status = `main`'s return value & 0xff),
  `--max-steps N` (0 = unlimited), and input path `-` = read the program from stdin.
* **`playground/`**: `minic-wasi.js` (minimal WASI host: args, stdin/stdout/stderr, proc_exit;
  no filesystem), `template.html` (UI), `build.mjs` (bundles `build/minic.wasm`, the shim and
  `examples/*.c` into one self-contained HTML file), `test.mjs` (wasm output must be
  byte-identical to the native binary for every example in every mode).
* **Make**: `make wasm`, `make playground`. Deployed copy: <https://mrbaten.github.io/minic/>.

