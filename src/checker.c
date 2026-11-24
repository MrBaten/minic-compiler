/* checker.c - semantic analysis for Mini-C: scoping, typing, warnings. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "checker.h"

/* Kind of a symbol-table entry. */
typedef enum { SYM_VAR, SYM_FUNC } SymKind;

/* One symbol-table entry; `name` and nothing else borrows from the AST. */
typedef struct {
    const char *name;
    SymKind kind;
    TypeKind type;
    int param_count;
    TypeKind *param_types;
    int initialized;
    int warned;
} Symbol;

/* One lexical scope: a growable array of symbols. */
typedef struct {
    Symbol *syms;
    int count;
    int capacity;
} Scope;

/* Checker state: the scope stack, error count and current function. */
typedef struct {
    Scope *scopes;
    int depth;
    int capacity;
    int errors;
    const Node *func;
} Checker;

/* Names that may not be declared: print, C keywords and library names. */
static const char *const RESERVED[] = {
    "print",
    "auto", "break", "case", "const", "continue", "default", "do", "double",
    "enum", "extern", "float", "goto", "inline", "long", "register",
    "restrict", "short", "signed", "sizeof", "static", "struct", "switch",
    "typedef", "union", "unsigned", "volatile", "_Bool", "_Complex",
    "_Imaginary", "_Alignas", "_Alignof", "_Atomic", "_Generic", "_Noreturn",
    "_Static_assert", "_Thread_local",
    "printf", "puts", "putchar", "getchar", "scanf", "sprintf", "fprintf",
    "fopen", "fclose", "remove", "rename", "perror", "fflush", "fgets",
    "fputs", "getc", "putc", "EOF", "NULL", "FILE", "stdin", "stdout",
    "stderr", "size_t", "abs", "exit", "malloc", "free", "exp", "log", "pow",
    "sqrt", "sin", "cos", "tan", "floor", "ceil", "round", "fabs", "strlen",
    "strcmp", "strcpy", "memcpy", "memset", "atoi", "rand", "srand", "time"
};

static TypeKind check_expr(Checker *ck, Node *node);
static void check_stmt(Checker *ck, Node *node);

/* Abort with the standard out-of-memory message. */
static void out_of_memory(void) {
    fprintf(stderr, "minic: out of memory\n");
    exit(1);
}

/* Print one diagnostic ("Semantic Error" or "Warning") at a position. */
static void emit(const char *label, int line, int column, const char *fmt,
                 va_list args) {
    fprintf(stderr, "%s at line %d, column %d: ", label, line, column);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
}

/* Report a semantic error at a node's position and count it. */
static void error_at(Checker *ck, const Node *node, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    emit("Semantic Error", node->line, node->column, fmt, args);
    va_end(args);
    ck->errors++;
}

/* Report a warning at a node's position (not counted as an error). */
static void warn_at(const Node *node, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    emit("Warning", node->line, node->column, fmt, args);
    va_end(args);
}

/* Open a new innermost scope. */
static void scope_push(Checker *ck) {
    if (ck->depth == ck->capacity) {
        int cap = ck->capacity ? ck->capacity * 2 : 8;
        Scope *grown = realloc(ck->scopes, (size_t)cap * sizeof(Scope));
        if (!grown) {
            out_of_memory();
        }
        ck->scopes = grown;
        ck->capacity = cap;
    }
    memset(&ck->scopes[ck->depth], 0, sizeof(Scope));
    ck->depth++;
}

/* Close the innermost scope and free its symbols. */
static void scope_pop(Checker *ck) {
    Scope *scope = &ck->scopes[ck->depth - 1];
    for (int i = 0; i < scope->count; i++) {
        free(scope->syms[i].param_types);
    }
    free(scope->syms);
    ck->depth--;
}

/* Find a symbol by name in one scope, or NULL. */
static Symbol *scope_find(Scope *scope, const char *name) {
    for (int i = 0; i < scope->count; i++) {
        if (strcmp(scope->syms[i].name, name) == 0) {
            return &scope->syms[i];
        }
    }
    return NULL;
}

/* Find a symbol by name in the innermost scope that has it, or NULL. */
static Symbol *lookup(Checker *ck, const char *name) {
    for (int d = ck->depth - 1; d >= 0; d--) {
        Symbol *sym = scope_find(&ck->scopes[d], name);
        if (sym) {
            return sym;
        }
    }
    return NULL;
}

/* Append a fresh zeroed symbol to the innermost scope and return it. */
static Symbol *scope_add(Checker *ck, const char *name, SymKind kind,
                         TypeKind type) {
    Scope *scope = &ck->scopes[ck->depth - 1];
    if (scope->count == scope->capacity) {
        int cap = scope->capacity ? scope->capacity * 2 : 8;
        Symbol *grown = realloc(scope->syms, (size_t)cap * sizeof(Symbol));
        if (!grown) {
            out_of_memory();
        }
        scope->syms = grown;
        scope->capacity = cap;
    }
    Symbol *sym = &scope->syms[scope->count++];
    memset(sym, 0, sizeof(Symbol));
    sym->name = name;
    sym->kind = kind;
    sym->type = type;
    return sym;
}

/* True if `name` is a reserved identifier. */
static int is_reserved(const char *name) {
    size_t n = sizeof(RESERVED) / sizeof(RESERVED[0]);
    for (size_t i = 0; i < n; i++) {
        if (strcmp(RESERVED[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Report a reserved identifier used as a declared name. */
static void check_reserved(Checker *ck, const Node *decl) {
    if (is_reserved(decl->name)) {
        error_at(ck, decl, "'%s' is a reserved identifier", decl->name);
    }
}

/* Declare a variable or parameter in the current scope; returns its type
 * (int when the declaration wrongly says void). */
static TypeKind declare_variable(Checker *ck, const Node *decl,
                                 int initialized) {
    TypeKind type = decl->decl_type;
    check_reserved(ck, decl);
    if (type == TYPE_VOID) {
        error_at(ck, decl, "Variable '%s' cannot have type void", decl->name);
        type = TYPE_INT;
    }
    if (scope_find(&ck->scopes[ck->depth - 1], decl->name)) {
        error_at(ck, decl, "Variable '%s' already declared", decl->name);
        return type;
    }
    Symbol *sym = scope_add(ck, decl->name, SYM_VAR, type);
    sym->initialized = initialized;
    return type;
}

/* Report a type mismatch if `type` cannot be stored into a `target` variable. */
static void check_assignable(Checker *ck, const Node *at, TypeKind type,
                             TypeKind target, const char *name) {
    if (type == TYPE_STRING || type == TYPE_VOID) {
        error_at(ck, at, "Type mismatch: cannot assign %s to '%s' variable '%s'",
                 type == TYPE_VOID ? "void value" : "string",
                 ast_type_name(target), name);
    }
}

/* Check an expression used as an operand: no strings, no void values.
 * Returns the operand type, or int after an error. */
static TypeKind check_operand(Checker *ck, Node *node) {
    TypeKind type = check_expr(ck, node);
    if (type == TYPE_STRING) {
        error_at(ck, node,
                 "String literals are only allowed as the argument of print()");
        return TYPE_INT;
    }
    if (type == TYPE_VOID) {
        error_at(ck, node, "Void value used in expression");
        return TYPE_INT;
    }
    return type;
}

/* Check an expression whose value is discarded (void is allowed here). */
static void check_discarded(Checker *ck, Node *node) {
    if (check_expr(ck, node) == TYPE_STRING) {
        error_at(ck, node,
                 "String literals are only allowed as the argument of print()");
    }
}

/* Check an if/while/for condition. */
static void check_condition(Checker *ck, Node *cond) {
    TypeKind type = check_expr(ck, cond);
    if (type == TYPE_STRING || type == TYPE_VOID) {
        error_at(ck, cond, "Condition must be an integer expression");
    }
}

/* True if the expression consists only of numbers, chars, unary and binary
 * operators over those. */
static int is_constant_expr(const Node *node) {
    switch (node->kind) {
    case NODE_NUMBER_EXPR:
    case NODE_CHAR_EXPR:
        return 1;
    case NODE_UNARY_EXPR:
        return is_constant_expr(node->expr);
    case NODE_BINARY_EXPR:
        return is_constant_expr(node->left) && is_constant_expr(node->right);
    default:
        return 0;
    }
}

/* Check a variable read: resolve the name, warn on uninitialized use. */
static TypeKind check_var_expr(Checker *ck, Node *node) {
    Symbol *sym = lookup(ck, node->name);
    if (!sym) {
        error_at(ck, node, "Undefined variable '%s'", node->name);
        return TYPE_INT;
    }
    if (sym->kind == SYM_FUNC) {
        error_at(ck, node, "'%s' is a function, not a variable", node->name);
        return TYPE_INT;
    }
    if (!sym->initialized && !sym->warned) {
        sym->warned = 1;
        warn_at(node, "Variable '%s' may be used uninitialized", node->name);
    }
    return sym->type;
}

/* Check an assignment: value first, then the target, then mark it set. */
static TypeKind check_assign(Checker *ck, Node *node) {
    Node *target = node->left;
    TypeKind value = check_expr(ck, node->right);
    Symbol *sym = lookup(ck, target->name);
    TypeKind type = TYPE_INT;
    if (!sym) {
        error_at(ck, target, "Undefined variable '%s'", target->name);
    } else if (sym->kind == SYM_FUNC) {
        error_at(ck, target, "'%s' is a function, not a variable",
                 target->name);
    } else {
        type = sym->type;
        check_assignable(ck, node, value, type, target->name);
        sym->initialized = 1;
    }
    target->eval_type = type;
    return type;
}

/* Check a call to the builtin print(). */
static TypeKind check_print_call(Checker *ck, Node *node) {
    int count = node->items.count;
    TypeKind type = TYPE_UNKNOWN;
    for (int i = 0; i < count; i++) {
        TypeKind t = check_expr(ck, node->items.items[i]);
        if (i == 0) {
            type = t;
        }
    }
    if (count != 1) {
        error_at(ck, node, "Function 'print' expects 1 argument(s) but got %d",
                 count);
    } else if (type == TYPE_VOID) {
        error_at(ck, node->items.items[0],
                 "Type mismatch in argument 1 of 'print': expected 'int' but "
                 "got 'void'");
    }
    return TYPE_VOID;
}

/* Check a call to a user function (or report why it is not callable). */
static TypeKind check_call(Checker *ck, Node *node) {
    if (strcmp(node->name, "print") == 0) {
        return check_print_call(ck, node);
    }
    int count = node->items.count;
    TypeKind *arg_types = NULL;
    if (count > 0) {
        arg_types = malloc((size_t)count * sizeof(TypeKind));
        if (!arg_types) {
            out_of_memory();
        }
    }
    for (int i = 0; i < count; i++) {
        arg_types[i] = check_expr(ck, node->items.items[i]);
    }
    TypeKind result = TYPE_INT;
    Symbol *sym = lookup(ck, node->name);
    if (!sym) {
        error_at(ck, node, "Undefined function '%s'", node->name);
    } else if (sym->kind != SYM_FUNC) {
        error_at(ck, node, "'%s' is not a function", node->name);
    } else {
        result = sym->type;
        if (count != sym->param_count) {
            error_at(ck, node, "Function '%s' expects %d argument(s) but got %d",
                     node->name, sym->param_count, count);
        } else {
            for (int i = 0; i < count; i++) {
                if (arg_types[i] == TYPE_STRING || arg_types[i] == TYPE_VOID) {
                    error_at(ck, node->items.items[i],
                             "Type mismatch in argument %d of '%s': expected "
                             "'%s' but got '%s'",
                             i + 1, node->name,
                             ast_type_name(sym->param_types[i]),
                             ast_type_name(arg_types[i]));
                }
            }
        }
    }
    free(arg_types);
    return result;
}

/* Check a binary expression (operands, division by literal zero). */
static TypeKind check_binary(Checker *ck, Node *node) {
    check_operand(ck, node->left);
    check_operand(ck, node->right);
    if ((node->op == OP_DIV || node->op == OP_MOD) &&
        node->right->kind == NODE_NUMBER_EXPR && node->right->int_value == 0) {
        error_at(ck, node, "Division by zero");
    }
    return TYPE_INT;
}

/* Check an expression, set its eval_type, and return that type. A string or
 * void result is reported by the caller that cannot accept it. */
static TypeKind check_expr(Checker *ck, Node *node) {
    TypeKind type = TYPE_INT;
    switch (node->kind) {
    case NODE_NUMBER_EXPR:
        type = TYPE_INT;
        break;
    case NODE_CHAR_EXPR:
        type = TYPE_CHAR;
        break;
    case NODE_STRING_EXPR:
        type = TYPE_STRING;
        break;
    case NODE_VAR_EXPR:
        type = check_var_expr(ck, node);
        break;
    case NODE_ASSIGN_EXPR:
        type = check_assign(ck, node);
        break;
    case NODE_CALL_EXPR:
        type = check_call(ck, node);
        break;
    case NODE_BINARY_EXPR:
        type = check_binary(ck, node);
        break;
    case NODE_UNARY_EXPR:
        check_operand(ck, node->expr);
        type = TYPE_INT;
        break;
    default:
        break;
    }
    node->eval_type = type;
    return type;
}

/* Check a variable declaration (local or global) including its initializer. */
static void check_var_decl(Checker *ck, Node *node) {
    TypeKind target = node->decl_type == TYPE_VOID ? TYPE_INT : node->decl_type;
    if (node->init) {
        TypeKind type = check_expr(ck, node->init);
        check_assignable(ck, node->init, type, target, node->name);
        if (node->is_global && !is_constant_expr(node->init)) {
            error_at(ck, node->init,
                     "Global variable '%s' must be initialized with a "
                     "constant expression",
                     node->name);
        }
    }
    declare_variable(ck, node, node->init != NULL || node->is_global);
}

/* Check a return statement against the current function's type. */
static void check_return(Checker *ck, Node *node) {
    const Node *func = ck->func;
    if (!node->expr) {
        if (func->decl_type != TYPE_VOID) {
            error_at(ck, node,
                     "Return type mismatch: function '%s' must return a value "
                     "of type '%s'",
                     func->name, ast_type_name(func->decl_type));
        }
        return;
    }
    TypeKind type = check_expr(ck, node->expr);
    if (func->decl_type == TYPE_VOID) {
        error_at(ck, node,
                 "Return type mismatch: function '%s' returns void but a "
                 "value was returned",
                 func->name);
    } else if (type == TYPE_STRING || type == TYPE_VOID) {
        error_at(ck, node,
                 "Return type mismatch: function '%s' must return '%s' but "
                 "got '%s'",
                 func->name, ast_type_name(func->decl_type),
                 ast_type_name(type));
    }
}

/* Check every statement of a list in the current scope. */
static void check_items(Checker *ck, NodeList *list) {
    for (int i = 0; i < list->count; i++) {
        check_stmt(ck, list->items[i]);
    }
}

/* Check a for statement; its init declaration lives in its own scope. */
static void check_for(Checker *ck, Node *node) {
    scope_push(ck);
    if (node->init) {
        if (node->init->kind == NODE_VAR_DECL) {
            check_var_decl(ck, node->init);
        } else {
            check_operand(ck, node->init);
        }
    }
    if (node->cond) {
        check_condition(ck, node->cond);
    }
    if (node->update) {
        check_discarded(ck, node->update);
    }
    check_stmt(ck, node->body);
    scope_pop(ck);
}

/* Check one statement. */
static void check_stmt(Checker *ck, Node *node) {
    switch (node->kind) {
    case NODE_VAR_DECL:
        check_var_decl(ck, node);
        break;
    case NODE_BLOCK:
        scope_push(ck);
        check_items(ck, &node->items);
        scope_pop(ck);
        break;
    case NODE_IF_STMT:
        check_condition(ck, node->cond);
        check_stmt(ck, node->body);
        if (node->else_branch) {
            check_stmt(ck, node->else_branch);
        }
        break;
    case NODE_WHILE_STMT:
        check_condition(ck, node->cond);
        check_stmt(ck, node->body);
        break;
    case NODE_FOR_STMT:
        check_for(ck, node);
        break;
    case NODE_RETURN_STMT:
        check_return(ck, node);
        break;
    case NODE_EXPR_STMT:
        if (node->expr) {
            check_discarded(ck, node->expr);
        }
        break;
    default:
        break;
    }
}

/* True if a statement definitely returns on every path. */
static int definitely_returns(const Node *node) {
    int i;
    switch (node->kind) {
    case NODE_RETURN_STMT:
        return 1;
    case NODE_BLOCK:
        for (i = 0; i < node->items.count; i++) {
            if (definitely_returns(node->items.items[i])) {
                return 1;
            }
        }
        return 0;
    case NODE_IF_STMT:
        return node->else_branch != NULL && definitely_returns(node->body) &&
               definitely_returns(node->else_branch);
    default:
        return 0;
    }
}

/* Pass 1: register a function in the global scope and check main's shape. */
static void register_function(Checker *ck, Node *func, int *has_main) {
    check_reserved(ck, func);
    if (scope_find(&ck->scopes[0], func->name)) {
        error_at(ck, func, "Function '%s' already declared", func->name);
        return;
    }
    Symbol *sym = scope_add(ck, func->name, SYM_FUNC, func->decl_type);
    sym->param_count = func->params.count;
    if (func->params.count > 0) {
        sym->param_types =
            malloc((size_t)func->params.count * sizeof(TypeKind));
        if (!sym->param_types) {
            out_of_memory();
        }
        for (int i = 0; i < func->params.count; i++) {
            TypeKind t = func->params.items[i]->decl_type;
            sym->param_types[i] = t == TYPE_VOID ? TYPE_INT : t;
        }
    }
    if (strcmp(func->name, "main") == 0) {
        *has_main = 1;
        if (func->decl_type != TYPE_INT || func->params.count != 0) {
            error_at(ck, func, "'main' must return int and take no parameters");
        }
    }
}

/* Pass 2: check one function; params and body statements share one scope. */
static void check_function(Checker *ck, Node *func) {
    ck->func = func;
    scope_push(ck);
    for (int i = 0; i < func->params.count; i++) {
        declare_variable(ck, func->params.items[i], 1);
    }
    check_items(ck, &func->body->items);
    scope_pop(ck);
    if (func->decl_type != TYPE_VOID && strcmp(func->name, "main") != 0 &&
        !definitely_returns(func->body)) {
        warn_at(func, "Function '%s' may not return a value on all paths",
                func->name);
    }
    ck->func = NULL;
}

/* Public entry point: runs both checker passes over the program, prints all
 * errors and warnings to stderr, annotates eval_type on every expression,
 * and returns the number of errors (0 means the program is valid). */
int check_program(Node *program) {
    Checker ck;
    int has_main = 0;
    memset(&ck, 0, sizeof(ck));
    scope_push(&ck);
    for (int i = 0; i < program->items.count; i++) {
        Node *item = program->items.items[i];
        if (item->kind == NODE_FUNC_DECL) {
            register_function(&ck, item, &has_main);
        }
    }
    for (int i = 0; i < program->items.count; i++) {
        Node *item = program->items.items[i];
        if (item->kind == NODE_FUNC_DECL) {
            check_function(&ck, item);
        } else {
            check_var_decl(&ck, item);
        }
    }
    if (!has_main) {
        fprintf(stderr,
                "Semantic Error at line 1, column 1: No 'main' function "
                "defined\n");
        ck.errors++;
    }
    scope_pop(&ck);
    free(ck.scopes);
    return ck.errors;
}
