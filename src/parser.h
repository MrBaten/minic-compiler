#ifndef MINIC_PARSER_H
#define MINIC_PARSER_H

#include "lexer.h"
#include "ast.h"

/* Parses a token list (terminated by TOK_EOF) into a NODE_PROGRAM tree.
 * On the first syntax error prints a "Parser Error" message to stderr and
 * exits with status 1. The returned tree owns all its strings; the token
 * list is not referenced after the call. */
Node *parse_program(const TokenList *tokens);

#endif
