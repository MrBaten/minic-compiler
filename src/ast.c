#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"

/* Report an allocation failure and terminate. */
static void out_of_memory(void)
{
    fprintf(stderr, "minic: out of memory\n");
    exit(1);
}

/* Allocate a zero-initialised node of the given kind and position. */
Node *ast_new_node(NodeKind kind, int line, int column)
{
    Node *node = calloc(1, sizeof(Node));
    if (node == NULL) {
        out_of_memory();
    }
    node->kind = kind;
    node->line = line;
    node->column = column;
    node->eval_type = TYPE_UNKNOWN;
    return node;
}

/* Heap copy of a string; NULL in, NULL out. */
char *ast_strdup(const char *s)
{
    size_t len;
    char *copy;

    if (s == NULL) {
        return NULL;
    }
    len = strlen(s);
    copy = malloc(len + 1);
    if (copy == NULL) {
        out_of_memory();
    }
    memcpy(copy, s, len + 1);
    return copy;
}

/* Append a node to a list, growing the array as needed (list takes ownership). */
void node_list_push(NodeList *list, Node *node)
{
    if (list->count >= list->capacity) {
        int new_capacity = list->capacity == 0 ? 4 : list->capacity * 2;
        Node **items = realloc(list->items, (size_t)new_capacity * sizeof(Node *));
        if (items == NULL) {
            out_of_memory();
        }
        list->items = items;
        list->capacity = new_capacity;
    }
    list->items[list->count++] = node;
}

/* Free every node in a list together with the backing array. */
static void node_list_free(NodeList *list)
{
    int i;

    for (i = 0; i < list->count; i++) {
        ast_free(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

/* Recursively free a node and everything it owns; NULL-safe. */
void ast_free(Node *node)
{
    if (node == NULL) {
        return;
    }
    free(node->name);
    free(node->str_value);
    ast_free(node->left);
    ast_free(node->right);
    ast_free(node->expr);
    ast_free(node->init);
    ast_free(node->cond);
    ast_free(node->update);
    ast_free(node->body);
    ast_free(node->else_branch);
    node_list_free(&node->items);
    node_list_free(&node->params);
    free(node);
}

/* Human-readable name of a node kind, e.g. "BinaryExpr". */
const char *ast_node_kind_name(NodeKind kind)
{
    switch (kind) {
    case NODE_PROGRAM:     return "Program";
    case NODE_VAR_DECL:    return "VarDecl";
    case NODE_FUNC_DECL:   return "FuncDecl";
    case NODE_BLOCK:       return "Block";
    case NODE_IF_STMT:     return "IfStmt";
    case NODE_WHILE_STMT:  return "WhileStmt";
    case NODE_FOR_STMT:    return "ForStmt";
    case NODE_RETURN_STMT: return "ReturnStmt";
    case NODE_EXPR_STMT:   return "ExprStmt";
    case NODE_BINARY_EXPR: return "BinaryExpr";
    case NODE_UNARY_EXPR:  return "UnaryExpr";
    case NODE_ASSIGN_EXPR: return "AssignExpr";
    case NODE_CALL_EXPR:   return "CallExpr";
    case NODE_VAR_EXPR:    return "VarExpr";
    case NODE_NUMBER_EXPR: return "NumberExpr";
    case NODE_CHAR_EXPR:   return "CharExpr";
    case NODE_STRING_EXPR: return "StringExpr";
    }
    return "Unknown";
}

/* Source symbol of an operator, e.g. "+" or "&&". */
const char *ast_op_symbol(OpKind op)
{
    switch (op) {
    case OP_ADD: return "+";
    case OP_SUB: return "-";
    case OP_MUL: return "*";
    case OP_DIV: return "/";
    case OP_MOD: return "%";
    case OP_EQ:  return "==";
    case OP_NEQ: return "!=";
    case OP_LT:  return "<";
    case OP_GT:  return ">";
    case OP_LE:  return "<=";
    case OP_GE:  return ">=";
    case OP_AND: return "&&";
    case OP_OR:  return "||";
    case OP_NEG: return "-";
    case OP_NOT: return "!";
    }
    return "?";
}

/* Name of a type, e.g. "int". */
const char *ast_type_name(TypeKind type)
{
    switch (type) {
    case TYPE_UNKNOWN: return "unknown";
    case TYPE_INT:     return "int";
    case TYPE_CHAR:    return "char";
    case TYPE_VOID:    return "void";
    case TYPE_STRING:  return "string";
    }
    return "unknown";
}

/* Print 2*indent spaces. */
static void print_indent(int indent)
{
    int i;

    for (i = 0; i < indent * 2; i++) {
        putchar(' ');
    }
}

/* Print a section label at the given indent, then its child one level deeper. */
static void print_section(const char *label, const Node *child, int indent)
{
    print_indent(indent);
    printf("%s\n", label);
    print_ast(child, indent + 1);
}

/* Print every node of a list at the given indent. */
static void print_list(const NodeList *list, int indent)
{
    int i;

    for (i = 0; i < list->count; i++) {
        print_ast(list->items[i], indent);
    }
}

/* Print the one-line FuncDecl header including the parameter list. */
static void print_func_header(const Node *node)
{
    int i;

    printf("FuncDecl: %s %s(", ast_type_name(node->decl_type),
           node->name != NULL ? node->name : "");
    for (i = 0; i < node->params.count; i++) {
        const Node *param = node->params.items[i];
        if (i > 0) {
            printf(", ");
        }
        printf("%s %s", ast_type_name(param->decl_type),
               param->name != NULL ? param->name : "");
    }
    printf(")\n");
}

/* Print an indented textual tree of the AST to stdout (see PLAN.md §6.4). */
void print_ast(const Node *node, int indent)
{
    const char *name;

    if (node == NULL) {
        return;
    }
    print_indent(indent);
    name = node->name != NULL ? node->name : "";
    switch (node->kind) {
    case NODE_PROGRAM:
        printf("Program\n");
        print_list(&node->items, indent + 1);
        break;
    case NODE_FUNC_DECL:
        print_func_header(node);
        print_ast(node->body, indent + 1);
        break;
    case NODE_VAR_DECL:
        printf("VarDecl: %s %s\n", ast_type_name(node->decl_type), name);
        print_ast(node->init, indent + 1);
        break;
    case NODE_BLOCK:
        printf("Block\n");
        print_list(&node->items, indent + 1);
        break;
    case NODE_IF_STMT:
        printf("IfStmt\n");
        print_section("Condition:", node->cond, indent + 1);
        print_section("Then:", node->body, indent + 1);
        if (node->else_branch != NULL) {
            print_section("Else:", node->else_branch, indent + 1);
        }
        break;
    case NODE_WHILE_STMT:
        printf("WhileStmt\n");
        print_section("Condition:", node->cond, indent + 1);
        print_section("Body:", node->body, indent + 1);
        break;
    case NODE_FOR_STMT:
        printf("ForStmt\n");
        if (node->init != NULL) {
            print_section("Init:", node->init, indent + 1);
        }
        if (node->cond != NULL) {
            print_section("Condition:", node->cond, indent + 1);
        }
        if (node->update != NULL) {
            print_section("Update:", node->update, indent + 1);
        }
        print_section("Body:", node->body, indent + 1);
        break;
    case NODE_RETURN_STMT:
        printf("ReturnStmt\n");
        print_ast(node->expr, indent + 1);
        break;
    case NODE_EXPR_STMT:
        printf(node->expr == NULL ? "ExprStmt (empty)\n" : "ExprStmt\n");
        print_ast(node->expr, indent + 1);
        break;
    case NODE_BINARY_EXPR:
        printf("BinaryExpr: %s\n", ast_op_symbol(node->op));
        print_ast(node->left, indent + 1);
        print_ast(node->right, indent + 1);
        break;
    case NODE_UNARY_EXPR:
        printf("UnaryExpr: %s\n", ast_op_symbol(node->op));
        print_ast(node->expr, indent + 1);
        break;
    case NODE_ASSIGN_EXPR:
        printf("AssignExpr: %s\n",
               node->left != NULL && node->left->name != NULL ? node->left->name : "");
        print_ast(node->right, indent + 1);
        break;
    case NODE_CALL_EXPR:
        printf("CallExpr: %s\n", name);
        print_list(&node->items, indent + 1);
        break;
    case NODE_VAR_EXPR:
        printf("VarExpr: %s\n", name);
        break;
    case NODE_NUMBER_EXPR:
        printf("NumberExpr: %d\n", node->int_value);
        break;
    case NODE_CHAR_EXPR:
        printf("CharExpr: '%s'\n", node->str_value != NULL ? node->str_value : "");
        break;
    case NODE_STRING_EXPR:
        printf("StringExpr: \"%s\"\n", node->str_value != NULL ? node->str_value : "");
        break;
    }
}
