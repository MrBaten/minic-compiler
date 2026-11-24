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

/* Splits `source` into tokens. On a lexical error prints a message to stderr
 * and exits with status 1. The caller releases the result with token_list_free. */
TokenList lexer_tokenize(const char *source);

/* Frees every lexeme and the token array, then resets the list to empty. */
void token_list_free(TokenList *list);

/* Returns the enum name of `type` without the TOK_ prefix, e.g. "IDENTIFIER". */
const char *token_type_name(TokenType type);

/* Prints one token as a single line (see PLAN.md section 6.1). */
void token_print(const Token *tok, FILE *out);

/* Prints every token of `list`, including EOF, one per line. */
void token_list_print(const TokenList *list, FILE *out);

#endif
