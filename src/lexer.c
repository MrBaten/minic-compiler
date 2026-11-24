/* Hand-written lexer for Mini-C. */
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"

/* Cursor over the source text with 1-based line/column tracking. */
typedef struct {
    const char *src;
    size_t pos;
    int line;
    int col;
} Lexer;

/* Reserved words and the token kinds they map to. */
static const struct {
    const char *text;
    TokenType type;
} KEYWORDS[] = {
    {"int", TOK_INT}, {"char", TOK_CHAR}, {"void", TOK_VOID}, {"if", TOK_IF},
    {"else", TOK_ELSE}, {"while", TOK_WHILE}, {"for", TOK_FOR}, {"return", TOK_RETURN}
};

/* Reports an allocation failure and terminates the program. */
static void out_of_memory(void)
{
    fputs("minic: out of memory\n", stderr);
    exit(1);
}

/* Prints a formatted lexer error at the given position and exits with status 1. */
static void lex_error(int line, int col, const char *fmt, ...)
{
    va_list args;

    fprintf(stderr, "Lexer Error at line %d, column %d: ", line, col);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    exit(1);
}

/* Writes a printable form of character `c` into `buf` ('x' or \xNN if not printable). */
static void describe_char(int c, char *buf, size_t size)
{
    if (isprint((unsigned char)c)) {
        snprintf(buf, size, "%c", c);
    } else {
        snprintf(buf, size, "\\x%02X", (unsigned)(unsigned char)c);
    }
}

/* Returns the character at the cursor plus `offset`, or '\0' at end of input. */
static char peek(const Lexer *lx, size_t offset)
{
    size_t i;

    for (i = 0; i < offset; i++) {
        if (lx->src[lx->pos + i] == '\0') {
            return '\0';
        }
    }
    return lx->src[lx->pos + offset];
}

/* Consumes one character, updating line and column. */
static void advance(Lexer *lx)
{
    char c = lx->src[lx->pos];

    if (c == '\0') {
        return;
    }
    lx->pos++;
    if (c == '\n') {
        lx->line++;
        lx->col = 1;
    } else {
        lx->col++;
    }
}

/* Returns a heap copy of `len` bytes of `text` as a NUL-terminated string. */
static char *copy_text(const char *text, size_t len)
{
    char *s = malloc(len + 1);

    if (s == NULL) {
        out_of_memory();
    }
    memcpy(s, text, len);
    s[len] = '\0';
    return s;
}

/* Appends a token to the list (taking ownership of `lexeme`), growing the array. */
static void list_push(TokenList *list, TokenType type, char *lexeme, int int_value,
                      int line, int col)
{
    Token *tok;

    if (list->count == list->capacity) {
        int new_cap = list->capacity == 0 ? 64 : list->capacity * 2;
        Token *grown = realloc(list->tokens, (size_t)new_cap * sizeof *grown);

        if (grown == NULL) {
            out_of_memory();
        }
        list->tokens = grown;
        list->capacity = new_cap;
    }
    tok = &list->tokens[list->count++];
    tok->type = type;
    tok->lexeme = lexeme;
    tok->int_value = int_value;
    tok->line = line;
    tok->column = col;
}

/* Skips whitespace and comments; reports an unterminated block comment. */
static void skip_trivia(Lexer *lx)
{
    for (;;) {
        char c = peek(lx, 0);

        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f') {
            advance(lx);
        } else if (c == '/' && peek(lx, 1) == '/') {
            while (peek(lx, 0) != '\0' && peek(lx, 0) != '\n') {
                advance(lx);
            }
        } else if (c == '/' && peek(lx, 1) == '*') {
            int line = lx->line;
            int col = lx->col;

            advance(lx);
            advance(lx);
            while (!(peek(lx, 0) == '*' && peek(lx, 1) == '/')) {
                if (peek(lx, 0) == '\0') {
                    lex_error(line, col, "unterminated comment");
                }
                advance(lx);
            }
            advance(lx);
            advance(lx);
        } else {
            return;
        }
    }
}

/* Consumes an escape sequence at the cursor (a backslash) and returns its code.
 * `unterminated` is the message used if the literal ends right after the backslash. */
static int read_escape(Lexer *lx, const char *unterminated, int lit_line, int lit_col)
{
    int line = lx->line;
    int col = lx->col;
    char c = peek(lx, 1);
    char shown[8];
    int value;

    if (c == '\0' || c == '\n') {
        lex_error(lit_line, lit_col, "%s", unterminated);
    }
    switch (c) {
    case 'n': value = '\n'; break;
    case 't': value = '\t'; break;
    case 'r': value = '\r'; break;
    case '0': value = '\0'; break;
    case '\\': value = '\\'; break;
    case '\'': value = '\''; break;
    case '"': value = '"'; break;
    default:
        describe_char(c, shown, sizeof shown);
        lex_error(line, col, "invalid escape sequence '\\%s'", shown);
        value = 0;
    }
    advance(lx);
    advance(lx);
    return value;
}

/* Lexes a string literal at the cursor (an opening quote) and appends it. */
static void lex_string(Lexer *lx, TokenList *list)
{
    int line = lx->line;
    int col = lx->col;
    size_t start;

    advance(lx);
    start = lx->pos;
    for (;;) {
        char c = peek(lx, 0);

        if (c == '"') {
            break;
        }
        if (c == '\0' || c == '\n') {
            lex_error(line, col, "unterminated string literal");
        }
        if (c == '\\') {
            read_escape(lx, "unterminated string literal", line, col);
        } else {
            advance(lx);
        }
    }
    list_push(list, TOK_STRING, copy_text(lx->src + start, lx->pos - start), 0, line, col);
    advance(lx);
}

/* Lexes a character literal at the cursor (an opening quote) and appends it. */
static void lex_char_literal(Lexer *lx, TokenList *list)
{
    static const char *const unterminated = "unterminated or multi-character literal";
    int line = lx->line;
    int col = lx->col;
    size_t start;
    int value;
    char c;

    advance(lx);
    start = lx->pos;
    c = peek(lx, 0);
    if (c == '\'') {
        lex_error(line, col, "empty character literal");
    }
    if (c == '\0' || c == '\n') {
        lex_error(line, col, "%s", unterminated);
    }
    if (c == '\\') {
        value = read_escape(lx, unterminated, line, col);
    } else {
        value = (unsigned char)c;
        advance(lx);
    }
    if (peek(lx, 0) != '\'') {
        lex_error(line, col, "%s", unterminated);
    }
    list_push(list, TOK_CHAR_LITERAL, copy_text(lx->src + start, lx->pos - start),
              value, line, col);
    advance(lx);
}

/* Lexes a decimal number at the cursor, rejecting values above INT_MAX. */
static void lex_number(Lexer *lx, TokenList *list)
{
    int line = lx->line;
    int col = lx->col;
    size_t start = lx->pos;
    int value = 0;

    while (isdigit((unsigned char)peek(lx, 0))) {
        int digit = peek(lx, 0) - '0';

        if (value > (INT_MAX - digit) / 10) {
            lex_error(line, col, "integer literal too large");
        }
        value = value * 10 + digit;
        advance(lx);
    }
    list_push(list, TOK_NUMBER, copy_text(lx->src + start, lx->pos - start), value, line, col);
}

/* Lexes an identifier or keyword at the cursor and appends it. */
static void lex_word(Lexer *lx, TokenList *list)
{
    int line = lx->line;
    int col = lx->col;
    size_t start = lx->pos;
    size_t len;
    size_t i;
    TokenType type = TOK_IDENTIFIER;

    while (isalnum((unsigned char)peek(lx, 0)) || peek(lx, 0) == '_') {
        advance(lx);
    }
    len = lx->pos - start;
    for (i = 0; i < sizeof KEYWORDS / sizeof KEYWORDS[0]; i++) {
        if (strlen(KEYWORDS[i].text) == len && strncmp(KEYWORDS[i].text, lx->src + start, len) == 0) {
            type = KEYWORDS[i].type;
            break;
        }
    }
    list_push(list, type, copy_text(lx->src + start, len), 0, line, col);
}

/* Lexes an operator or punctuation token at the cursor; errors on unknown characters. */
static void lex_symbol(Lexer *lx, TokenList *list)
{
    int line = lx->line;
    int col = lx->col;
    char c = peek(lx, 0);
    char next = peek(lx, 1);
    TokenType type;
    size_t len = 1;
    char shown[8];

    switch (c) {
    case '+': type = TOK_PLUS; break;
    case '-': type = TOK_MINUS; break;
    case '*': type = TOK_STAR; break;
    case '/': type = TOK_SLASH; break;
    case '%': type = TOK_PERCENT; break;
    case '(': type = TOK_LPAREN; break;
    case ')': type = TOK_RPAREN; break;
    case '{': type = TOK_LBRACE; break;
    case '}': type = TOK_RBRACE; break;
    case ';': type = TOK_SEMICOLON; break;
    case ',': type = TOK_COMMA; break;
    case '!':
        type = next == '=' ? TOK_NEQ : TOK_NOT;
        break;
    case '=':
        type = next == '=' ? TOK_EQ : TOK_ASSIGN;
        break;
    case '<':
        type = next == '=' ? TOK_LE : TOK_LT;
        break;
    case '>':
        type = next == '=' ? TOK_GE : TOK_GT;
        break;
    case '&':
        type = TOK_AND;
        if (next != '&') {
            describe_char(c, shown, sizeof shown);
            lex_error(line, col, "unexpected char '%s'", shown);
        }
        break;
    case '|':
        type = TOK_OR;
        if (next != '|') {
            describe_char(c, shown, sizeof shown);
            lex_error(line, col, "unexpected char '%s'", shown);
        }
        break;
    default:
        describe_char(c, shown, sizeof shown);
        lex_error(line, col, "unexpected char '%s'", shown);
        type = TOK_EOF;
    }
    if ((c == '!' || c == '=' || c == '<' || c == '>') && next == '=') {
        len = 2;
    } else if (c == '&' || c == '|') {
        len = 2;
    }
    list_push(list, type, copy_text(lx->src + lx->pos, len), 0, line, col);
    while (len-- > 0) {
        advance(lx);
    }
}

/* Tokenizes the whole source text; always ends the list with one TOK_EOF token. */
TokenList lexer_tokenize(const char *source)
{
    TokenList list = {NULL, 0, 0};
    Lexer lx;

    lx.src = source;
    lx.pos = 0;
    lx.line = 1;
    lx.col = 1;
    for (;;) {
        char c;

        skip_trivia(&lx);
        c = peek(&lx, 0);
        if (c == '\0') {
            break;
        }
        if (isdigit((unsigned char)c)) {
            lex_number(&lx, &list);
        } else if (isalpha((unsigned char)c) || c == '_') {
            lex_word(&lx, &list);
        } else if (c == '"') {
            lex_string(&lx, &list);
        } else if (c == '\'') {
            lex_char_literal(&lx, &list);
        } else {
            lex_symbol(&lx, &list);
        }
    }
    list_push(&list, TOK_EOF, copy_text("", 0), 0, lx.line, lx.col);
    return list;
}

/* Releases all lexemes and the token array and resets the list. */
void token_list_free(TokenList *list)
{
    int i;

    for (i = 0; i < list->count; i++) {
        free(list->tokens[i].lexeme);
    }
    free(list->tokens);
    list->tokens = NULL;
    list->count = 0;
    list->capacity = 0;
}

/* Maps a token type to its enum name without the TOK_ prefix. */
const char *token_type_name(TokenType type)
{
    switch (type) {
    case TOK_INT: return "INT";
    case TOK_CHAR: return "CHAR";
    case TOK_VOID: return "VOID";
    case TOK_IF: return "IF";
    case TOK_ELSE: return "ELSE";
    case TOK_WHILE: return "WHILE";
    case TOK_FOR: return "FOR";
    case TOK_RETURN: return "RETURN";
    case TOK_IDENTIFIER: return "IDENTIFIER";
    case TOK_NUMBER: return "NUMBER";
    case TOK_STRING: return "STRING";
    case TOK_CHAR_LITERAL: return "CHAR_LITERAL";
    case TOK_PLUS: return "PLUS";
    case TOK_MINUS: return "MINUS";
    case TOK_STAR: return "STAR";
    case TOK_SLASH: return "SLASH";
    case TOK_PERCENT: return "PERCENT";
    case TOK_ASSIGN: return "ASSIGN";
    case TOK_EQ: return "EQ";
    case TOK_NEQ: return "NEQ";
    case TOK_LT: return "LT";
    case TOK_GT: return "GT";
    case TOK_LE: return "LE";
    case TOK_GE: return "GE";
    case TOK_AND: return "AND";
    case TOK_OR: return "OR";
    case TOK_NOT: return "NOT";
    case TOK_LPAREN: return "LPAREN";
    case TOK_RPAREN: return "RPAREN";
    case TOK_LBRACE: return "LBRACE";
    case TOK_RBRACE: return "RBRACE";
    case TOK_SEMICOLON: return "SEMICOLON";
    case TOK_COMMA: return "COMMA";
    case TOK_EOF: return "EOF";
    }
    return "UNKNOWN";
}

/* Prints one token on its own line. */
void token_print(const Token *tok, FILE *out)
{
    switch (tok->type) {
    case TOK_IDENTIFIER:
        fprintf(out, "ID(%s)\n", tok->lexeme);
        break;
    case TOK_NUMBER:
        fprintf(out, "NUMBER(%s)\n", tok->lexeme);
        break;
    case TOK_STRING:
        fprintf(out, "STRING(\"%s\")\n", tok->lexeme);
        break;
    case TOK_CHAR_LITERAL:
        fprintf(out, "CHAR_LITERAL('%s')\n", tok->lexeme);
        break;
    default:
        fprintf(out, "%s\n", token_type_name(tok->type));
        break;
    }
}

/* Prints all tokens of the list, one per line. */
void token_list_print(const TokenList *list, FILE *out)
{
    int i;

    for (i = 0; i < list->count; i++) {
        token_print(&list->tokens[i], out);
    }
}
