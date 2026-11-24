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

/* Allocate a zero-initialised node of the given kind and position. */
Node *ast_new_node(NodeKind kind, int line, int column);
/* Heap copy of a string; NULL in, NULL out. */
char *ast_strdup(const char *s);
/* Append a node to a list, growing the array as needed (list takes ownership). */
void node_list_push(NodeList *list, Node *node);
/* Recursively free a node and everything it owns; NULL-safe. */
void ast_free(Node *node);
/* Print an indented textual tree of the AST to stdout (see PLAN.md §6.4). */
void print_ast(const Node *node, int indent);
/* Human-readable name of a node kind, e.g. "BinaryExpr". */
const char *ast_node_kind_name(NodeKind kind);
/* Source symbol of an operator, e.g. "+" or "&&". */
const char *ast_op_symbol(OpKind op);
/* Name of a type, e.g. "int". */
const char *ast_type_name(TypeKind type);

#endif
