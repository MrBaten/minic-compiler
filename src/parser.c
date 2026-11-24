/* Recursive-descent parser for Mini-C: TokenList -> AST (see PLAN.md section 7). */

#include <stdio.h>
#include <stdlib.h>

#include "parser.h"

/* Parser state: the token array and the index of the current token. */
typedef struct {
    const Token *tokens;
    int pos;
} Parser;

static Node *parse_expression(Parser *p);
static Node *parse_statement(Parser *p);
static Node *parse_body_statement(Parser *p);
static Node *parse_block(Parser *p);

/* Returns the current token (never advances past the final EOF). */
static const Token *peek(const Parser *p)
{
    return &p->tokens[p->pos];
}

/* Returns the token after the current one, or the EOF token if none. */
static const Token *peek_next(const Parser *p)
{
    const Token *cur = peek(p);
    return cur->type == TOK_EOF ? cur : cur + 1;
}

/* Consumes and returns the current token (EOF is never consumed). */
static const Token *advance(Parser *p)
{
    const Token *tok = peek(p);
    if (tok->type != TOK_EOF) {
        p->pos++;
    }
    return tok;
}

/* Returns 1 if the current token has the given type. */
static int check(const Parser *p, TokenType type)
{
    return peek(p)->type == type;
}

/* Consumes the current token if it has the given type; returns 1 if so. */
static int match(Parser *p, TokenType type)
{
    if (check(p, type)) {
        advance(p);
        return 1;
    }
    return 0;
}

/* Returns 1 if the token type is one of the type keywords int/char/void. */
static int is_type_token(TokenType type)
{
    return type == TOK_INT || type == TOK_CHAR || type == TOK_VOID;
}

/* Reports "expected <what> but got <tok>" at the current token and exits. */
static void error_expected(const Parser *p, const char *what)
{
    const Token *tok = peek(p);
    fprintf(stderr, "Parser Error at line %d, column %d: expected %s but got ",
            tok->line, tok->column, what);
    switch (tok->type) {
    case TOK_EOF:
        fprintf(stderr, "end of file\n");
        break;
    case TOK_STRING:
        fprintf(stderr, "string \"%s\"\n", tok->lexeme);
        break;
    case TOK_CHAR_LITERAL:
        fprintf(stderr, "character literal '%s'\n", tok->lexeme);
        break;
    default:
        fprintf(stderr, "'%s'\n", tok->lexeme);
        break;
    }
    exit(1);
}

/* Reports "invalid assignment target" at the given token and exits. */
static void error_assign_target(const Token *tok)
{
    fprintf(stderr, "Parser Error at line %d, column %d: invalid assignment target\n",
            tok->line, tok->column);
    exit(1);
}

/* Consumes a token of the given type or reports "expected <what>". */
static const Token *expect(Parser *p, TokenType type, const char *what)
{
    if (!check(p, type)) {
        error_expected(p, what);
    }
    return advance(p);
}

/* Maps a type keyword token to its TypeKind. */
static TypeKind type_from_token(TokenType type)
{
    switch (type) {
    case TOK_INT:
        return TYPE_INT;
    case TOK_CHAR:
        return TYPE_CHAR;
    default:
        return TYPE_VOID;
    }
}

/* type := 'int' | 'char' | 'void' */
static TypeKind parse_type(Parser *p)
{
    if (!is_type_token(peek(p)->type)) {
        error_expected(p, "type");
    }
    return type_from_token(advance(p)->type);
}

/* Creates a node positioned at the given token. */
static Node *new_node_at(NodeKind kind, const Token *tok)
{
    return ast_new_node(kind, tok->line, tok->column);
}

/* primary := NUMBER | CHAR_LITERAL | STRING | IDENT '(' args? ')' | IDENT
 *          | '(' expression ')' */
static Node *parse_primary(Parser *p)
{
    const Token *tok = peek(p);
    Node *node;

    switch (tok->type) {
    case TOK_NUMBER:
        advance(p);
        node = new_node_at(NODE_NUMBER_EXPR, tok);
        node->int_value = tok->int_value;
        return node;
    case TOK_CHAR_LITERAL:
        advance(p);
        node = new_node_at(NODE_CHAR_EXPR, tok);
        node->int_value = tok->int_value;
        node->str_value = ast_strdup(tok->lexeme);
        return node;
    case TOK_STRING:
        advance(p);
        node = new_node_at(NODE_STRING_EXPR, tok);
        node->str_value = ast_strdup(tok->lexeme);
        return node;
    case TOK_IDENTIFIER:
        advance(p);
        if (match(p, TOK_LPAREN)) {
            node = new_node_at(NODE_CALL_EXPR, tok);
            node->name = ast_strdup(tok->lexeme);
            if (!check(p, TOK_RPAREN)) {
                node_list_push(&node->items, parse_expression(p));
                while (match(p, TOK_COMMA)) {
                    node_list_push(&node->items, parse_expression(p));
                }
            }
            expect(p, TOK_RPAREN, "')'");
            return node;
        }
        node = new_node_at(NODE_VAR_EXPR, tok);
        node->name = ast_strdup(tok->lexeme);
        return node;
    case TOK_LPAREN:
        advance(p);
        node = parse_expression(p);
        expect(p, TOK_RPAREN, "')'");
        return node;
    default:
        error_expected(p, "expression");
        return NULL;
    }
}

/* unary := ( '-' | '!' ) unary | primary */
static Node *parse_unary(Parser *p)
{
    const Token *tok = peek(p);

    if (tok->type == TOK_MINUS || tok->type == TOK_NOT) {
        Node *node = new_node_at(NODE_UNARY_EXPR, tok);
        advance(p);
        node->op = tok->type == TOK_MINUS ? OP_NEG : OP_NOT;
        node->expr = parse_unary(p);
        return node;
    }
    return parse_primary(p);
}

/* Maps a token type to a binary operator via the given table; returns 1 and
 * sets *op on a match, 0 otherwise. */
static int lookup_op(TokenType type, const TokenType *toks, const OpKind *ops,
                     int n, OpKind *op)
{
    for (int i = 0; i < n; i++) {
        if (toks[i] == type) {
            *op = ops[i];
            return 1;
        }
    }
    return 0;
}

/* Builds a binary node for the operator token with the given operands. */
static Node *make_binary(const Token *tok, OpKind op, Node *left, Node *right)
{
    Node *node = new_node_at(NODE_BINARY_EXPR, tok);
    node->op = op;
    node->left = left;
    node->right = right;
    return node;
}

/* multiplicative := unary ( ( '*' | '/' | '%' ) unary )* */
static Node *parse_multiplicative(Parser *p)
{
    static const TokenType toks[] = { TOK_STAR, TOK_SLASH, TOK_PERCENT };
    static const OpKind ops[] = { OP_MUL, OP_DIV, OP_MOD };
    Node *left = parse_unary(p);
    OpKind op;

    while (lookup_op(peek(p)->type, toks, ops, 3, &op)) {
        const Token *tok = advance(p);
        left = make_binary(tok, op, left, parse_unary(p));
    }
    return left;
}

/* additive := multiplicative ( ( '+' | '-' ) multiplicative )* */
static Node *parse_additive(Parser *p)
{
    static const TokenType toks[] = { TOK_PLUS, TOK_MINUS };
    static const OpKind ops[] = { OP_ADD, OP_SUB };
    Node *left = parse_multiplicative(p);
    OpKind op;

    while (lookup_op(peek(p)->type, toks, ops, 2, &op)) {
        const Token *tok = advance(p);
        left = make_binary(tok, op, left, parse_multiplicative(p));
    }
    return left;
}

/* comparison := additive ( ( '<' | '>' | '<=' | '>=' ) additive )* */
static Node *parse_comparison(Parser *p)
{
    static const TokenType toks[] = { TOK_LT, TOK_GT, TOK_LE, TOK_GE };
    static const OpKind ops[] = { OP_LT, OP_GT, OP_LE, OP_GE };
    Node *left = parse_additive(p);
    OpKind op;

    while (lookup_op(peek(p)->type, toks, ops, 4, &op)) {
        const Token *tok = advance(p);
        left = make_binary(tok, op, left, parse_additive(p));
    }
    return left;
}

/* equality := comparison ( ( '==' | '!=' ) comparison )* */
static Node *parse_equality(Parser *p)
{
    static const TokenType toks[] = { TOK_EQ, TOK_NEQ };
    static const OpKind ops[] = { OP_EQ, OP_NEQ };
    Node *left = parse_comparison(p);
    OpKind op;

    while (lookup_op(peek(p)->type, toks, ops, 2, &op)) {
        const Token *tok = advance(p);
        left = make_binary(tok, op, left, parse_comparison(p));
    }
    return left;
}

/* logical_and := equality ( '&&' equality )* */
static Node *parse_logical_and(Parser *p)
{
    Node *left = parse_equality(p);

    while (check(p, TOK_AND)) {
        const Token *tok = advance(p);
        left = make_binary(tok, OP_AND, left, parse_equality(p));
    }
    return left;
}

/* logical_or := logical_and ( '||' logical_and )* */
static Node *parse_logical_or(Parser *p)
{
    Node *left = parse_logical_and(p);

    while (check(p, TOK_OR)) {
        const Token *tok = advance(p);
        left = make_binary(tok, OP_OR, left, parse_logical_and(p));
    }
    return left;
}

/* assignment := logical_or ( '=' assignment )?   (right-assoc, lhs a VarExpr) */
static Node *parse_assignment(Parser *p)
{
    Node *left = parse_logical_or(p);

    if (check(p, TOK_ASSIGN)) {
        const Token *tok = advance(p);
        Node *node;

        if (left->kind != NODE_VAR_EXPR) {
            error_assign_target(tok);
        }
        node = new_node_at(NODE_ASSIGN_EXPR, tok);
        node->left = left;
        node->right = parse_assignment(p);
        return node;
    }
    return left;
}

/* expression := assignment */
static Node *parse_expression(Parser *p)
{
    return parse_assignment(p);
}

/* Parses "type IDENT ( '=' expression )?" without the trailing ';'.
 * Shared by var_decl, for_init and param handling. */
static Node *parse_decl_core(Parser *p, int is_global)
{
    const Token *start = peek(p);
    Node *node = new_node_at(NODE_VAR_DECL, start);
    const Token *name;

    node->decl_type = parse_type(p);
    name = expect(p, TOK_IDENTIFIER, "identifier");
    node->name = ast_strdup(name->lexeme);
    node->is_global = is_global;
    if (match(p, TOK_ASSIGN)) {
        node->init = parse_expression(p);
    }
    return node;
}

/* var_decl := type IDENT ( '=' expression )? ';' */
static Node *parse_var_decl(Parser *p, int is_global)
{
    Node *node = parse_decl_core(p, is_global);
    expect(p, TOK_SEMICOLON, "';'");
    return node;
}

/* param := type IDENT */
static Node *parse_param(Parser *p)
{
    const Token *start = peek(p);
    Node *node = new_node_at(NODE_VAR_DECL, start);
    const Token *name;

    node->decl_type = parse_type(p);
    name = expect(p, TOK_IDENTIFIER, "identifier");
    node->name = ast_strdup(name->lexeme);
    return node;
}

/* param_list := 'void' | param ( ',' param )*   (also empty); stops before ')' */
static void parse_param_list(Parser *p, Node *func)
{
    if (check(p, TOK_RPAREN)) {
        return;
    }
    if (check(p, TOK_VOID) && peek_next(p)->type == TOK_RPAREN) {
        advance(p);
        return;
    }
    node_list_push(&func->params, parse_param(p));
    while (match(p, TOK_COMMA)) {
        node_list_push(&func->params, parse_param(p));
    }
}

/* func_decl := type IDENT '(' param_list? ')' block */
static Node *parse_func_decl(Parser *p)
{
    const Token *start = peek(p);
    Node *node = new_node_at(NODE_FUNC_DECL, start);
    const Token *name;

    node->decl_type = parse_type(p);
    name = expect(p, TOK_IDENTIFIER, "identifier");
    node->name = ast_strdup(name->lexeme);
    expect(p, TOK_LPAREN, "'('");
    parse_param_list(p, node);
    expect(p, TOK_RPAREN, "')'");
    node->body = parse_block(p);
    return node;
}

/* block := '{' statement* '}' */
static Node *parse_block(Parser *p)
{
    const Token *open = expect(p, TOK_LBRACE, "'{'");
    Node *node = new_node_at(NODE_BLOCK, open);

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        node_list_push(&node->items, parse_statement(p));
    }
    expect(p, TOK_RBRACE, "'}'");
    return node;
}

/* if_stmt := 'if' '(' expression ')' body_statement ( 'else' body_statement )? */
static Node *parse_if_stmt(Parser *p)
{
    const Token *start = advance(p);
    Node *node = new_node_at(NODE_IF_STMT, start);

    expect(p, TOK_LPAREN, "'('");
    node->cond = parse_expression(p);
    expect(p, TOK_RPAREN, "')'");
    node->body = parse_body_statement(p);
    if (match(p, TOK_ELSE)) {
        node->else_branch = parse_body_statement(p);
    }
    return node;
}

/* while_stmt := 'while' '(' expression ')' body_statement */
static Node *parse_while_stmt(Parser *p)
{
    const Token *start = advance(p);
    Node *node = new_node_at(NODE_WHILE_STMT, start);

    expect(p, TOK_LPAREN, "'('");
    node->cond = parse_expression(p);
    expect(p, TOK_RPAREN, "')'");
    node->body = parse_body_statement(p);
    return node;
}

/* for_init := type IDENT ( '=' expression )? | expression */
static Node *parse_for_init(Parser *p)
{
    if (is_type_token(peek(p)->type)) {
        return parse_decl_core(p, 0);
    }
    return parse_expression(p);
}

/* for_stmt := 'for' '(' for_init? ';' expression? ';' expression? ')' body_statement */
static Node *parse_for_stmt(Parser *p)
{
    const Token *start = advance(p);
    Node *node = new_node_at(NODE_FOR_STMT, start);

    expect(p, TOK_LPAREN, "'('");
    if (!check(p, TOK_SEMICOLON)) {
        node->init = parse_for_init(p);
    }
    expect(p, TOK_SEMICOLON, "';'");
    if (!check(p, TOK_SEMICOLON)) {
        node->cond = parse_expression(p);
    }
    expect(p, TOK_SEMICOLON, "';'");
    if (!check(p, TOK_RPAREN)) {
        node->update = parse_expression(p);
    }
    expect(p, TOK_RPAREN, "')'");
    node->body = parse_body_statement(p);
    return node;
}

/* return_stmt := 'return' expression? ';' */
static Node *parse_return_stmt(Parser *p)
{
    const Token *start = advance(p);
    Node *node = new_node_at(NODE_RETURN_STMT, start);

    if (!check(p, TOK_SEMICOLON)) {
        node->expr = parse_expression(p);
    }
    expect(p, TOK_SEMICOLON, "';'");
    return node;
}

/* expr_stmt := expression? ';'   (empty statement has expr == NULL) */
static Node *parse_expr_stmt(Parser *p)
{
    Node *node = new_node_at(NODE_EXPR_STMT, peek(p));

    if (!check(p, TOK_SEMICOLON)) {
        node->expr = parse_expression(p);
    }
    expect(p, TOK_SEMICOLON, "';'");
    return node;
}

/* body_statement := block | if_stmt | while_stmt | for_stmt | return_stmt | expr_stmt */
static Node *parse_body_statement(Parser *p)
{
    switch (peek(p)->type) {
    case TOK_LBRACE:
        return parse_block(p);
    case TOK_IF:
        return parse_if_stmt(p);
    case TOK_WHILE:
        return parse_while_stmt(p);
    case TOK_FOR:
        return parse_for_stmt(p);
    case TOK_RETURN:
        return parse_return_stmt(p);
    case TOK_INT:
    case TOK_CHAR:
    case TOK_VOID:
    case TOK_ELSE:
    case TOK_RBRACE:
    case TOK_EOF:
        error_expected(p, "statement");
        return NULL;
    default:
        return parse_expr_stmt(p);
    }
}

/* statement := var_decl | body_statement */
static Node *parse_statement(Parser *p)
{
    if (is_type_token(peek(p)->type)) {
        return parse_var_decl(p, 0);
    }
    return parse_body_statement(p);
}

/* program := ( func_decl | var_decl )* EOF */
Node *parse_program(const TokenList *tokens)
{
    Parser parser;
    Node *program;

    parser.tokens = tokens->tokens;
    parser.pos = 0;
    program = new_node_at(NODE_PROGRAM, peek(&parser));
    while (!check(&parser, TOK_EOF)) {
        if (!is_type_token(peek(&parser)->type)) {
            error_expected(&parser, "declaration");
        }
        if (parser.tokens[parser.pos + 1].type == TOK_IDENTIFIER
            && parser.tokens[parser.pos + 2].type == TOK_LPAREN) {
            node_list_push(&program->items, parse_func_decl(&parser));
        } else {
            node_list_push(&program->items, parse_var_decl(&parser, 1));
        }
    }
    return program;
}
