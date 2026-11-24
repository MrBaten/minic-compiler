/* Code generator: turns a checked Mini-C AST into portable C11 text. */

#include <stdio.h>
#include <string.h>
#include "codegen.h"

static void emit_expr(const Node *n, FILE *out, int nested);
static void emit_stmt(const Node *n, FILE *out, int level);

/* Writes four spaces per indentation level. */
static void emit_indent(FILE *out, int level) {
    for (int i = 0; i < level; i++) {
        fputs("    ", out);
    }
}

/* Returns the C spelling of a Mini-C type. */
static const char *type_text(TypeKind type) {
    switch (type) {
    case TYPE_CHAR: return "char";
    case TYPE_VOID: return "void";
    default:        return "int";
    }
}

/* Returns the C spelling of a binary or unary operator. */
static const char *op_text(OpKind op) {
    switch (op) {
    case OP_ADD: return "+";
    case OP_SUB: case OP_NEG: return "-";
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
    case OP_NOT: return "!";
    }
    return "";
}

/* Writes a raw string body, escaping any '?' that follows another '?' so no
 * trigraph (such as "??=") can appear in the generated C. */
static void emit_raw_string(const char *raw, FILE *out) {
    char prev = '\0';
    for (const char *p = raw; *p != '\0'; p++) {
        if (*p == '?' && prev == '?') {
            fputs("\\?", out);
        } else {
            fputc(*p, out);
        }
        prev = *p;
    }
}

/* Writes a variable declaration without a trailing ';' (also used for
 * parameters and for-loop initialisers). */
static void emit_decl_core(const Node *n, FILE *out) {
    fprintf(out, "%s %s", type_text(n->decl_type), n->name);
    if (n->init != NULL) {
        fputs(" = ", out);
        emit_expr(n->init, out, 0);
    }
}

/* Writes the builtin print() call as the matching printf() call. */
static void emit_print(const Node *n, FILE *out) {
    const Node *arg = n->items.items[0];
    if (arg->kind == NODE_STRING_EXPR) {
        if (strchr(arg->str_value, '%') == NULL) {
            fputs("printf(\"", out);
            emit_raw_string(arg->str_value, out);
            fputs("\\n\")", out);
        } else {
            fputs("printf(\"%s\\n\", \"", out);
            emit_raw_string(arg->str_value, out);
            fputs("\")", out);
        }
        return;
    }
    fputs(arg->eval_type == TYPE_CHAR ? "printf(\"%c\\n\", " : "printf(\"%d\\n\", ", out);
    emit_expr(arg, out, 0);
    fputc(')', out);
}

/* Writes a call expression (print is lowered to printf). */
static void emit_call(const Node *n, FILE *out) {
    if (strcmp(n->name, "print") == 0 && n->items.count == 1) {
        emit_print(n, out);
        return;
    }
    fprintf(out, "%s(", n->name);
    for (int i = 0; i < n->items.count; i++) {
        if (i > 0) {
            fputs(", ", out);
        }
        emit_expr(n->items.items[i], out, 0);
    }
    fputc(')', out);
}

/* Writes a unary expression; a unary, binary or assign operand is wrapped in
 * parentheses so "--" and precedence surprises never occur. */
static void emit_unary(const Node *n, FILE *out) {
    fputs(op_text(n->op), out);
    if (n->expr->kind == NODE_UNARY_EXPR) {
        fputc('(', out);
        emit_unary(n->expr, out);
        fputc(')', out);
    } else {
        emit_expr(n->expr, out, 1);
    }
}

/* Writes a binary expression, parenthesised when nested inside another one. */
static void emit_binary(const Node *n, FILE *out, int nested) {
    if (nested) {
        fputc('(', out);
    }
    emit_expr(n->left, out, 1);
    fprintf(out, " %s ", op_text(n->op));
    emit_expr(n->right, out, 1);
    if (nested) {
        fputc(')', out);
    }
}

/* Writes an assignment, parenthesised when nested inside another expression. */
static void emit_assign(const Node *n, FILE *out, int nested) {
    if (nested) {
        fputc('(', out);
    }
    fprintf(out, "%s = ", n->left->name);
    emit_expr(n->right, out, 0);
    if (nested) {
        fputc(')', out);
    }
}

/* Writes any expression; `nested` is non-zero when it sits inside another
 * expression and so needs parentheses if it is binary or assign. */
static void emit_expr(const Node *n, FILE *out, int nested) {
    switch (n->kind) {
    case NODE_BINARY_EXPR:
        emit_binary(n, out, nested);
        break;
    case NODE_UNARY_EXPR:
        emit_unary(n, out);
        break;
    case NODE_ASSIGN_EXPR:
        emit_assign(n, out, nested);
        break;
    case NODE_CALL_EXPR:
        emit_call(n, out);
        break;
    case NODE_VAR_EXPR:
        fputs(n->name, out);
        break;
    case NODE_NUMBER_EXPR:
        fprintf(out, "%d", n->int_value);
        break;
    case NODE_CHAR_EXPR:
        fprintf(out, "'%s'", n->str_value);
        break;
    case NODE_STRING_EXPR:
        fputc('"', out);
        emit_raw_string(n->str_value, out);
        fputc('"', out);
        break;
    default:
        break;
    }
}

/* Writes a condition expression: an assignment used directly as a condition
 * is parenthesised, everything else is written at top level. */
static void emit_cond(const Node *n, FILE *out) {
    emit_expr(n, out, n->kind == NODE_ASSIGN_EXPR);
}

/* Writes the statements of a body as an indented list: the items of a block,
 * or a single non-block statement. The braces are written by the caller. */
static void emit_body_items(const Node *body, FILE *out, int level) {
    if (body->kind == NODE_BLOCK) {
        for (int i = 0; i < body->items.count; i++) {
            emit_stmt(body->items.items[i], out, level);
        }
    } else {
        emit_stmt(body, out, level);
    }
}

/* Writes an if / else-if / else chain with every body braced. */
static void emit_if(const Node *n, FILE *out, int level) {
    emit_indent(out, level);
    fputs("if (", out);
    emit_cond(n->cond, out);
    fputs(") {\n", out);
    emit_body_items(n->body, out, level + 1);
    while (n->else_branch != NULL) {
        const Node *e = n->else_branch;
        emit_indent(out, level);
        if (e->kind == NODE_IF_STMT) {
            fputs("} else if (", out);
            emit_cond(e->cond, out);
            fputs(") {\n", out);
            emit_body_items(e->body, out, level + 1);
            n = e;
        } else {
            fputs("} else {\n", out);
            emit_body_items(e, out, level + 1);
            break;
        }
    }
    emit_indent(out, level);
    fputs("}\n", out);
}

/* Writes a while loop with a braced body. */
static void emit_while(const Node *n, FILE *out, int level) {
    emit_indent(out, level);
    fputs("while (", out);
    emit_cond(n->cond, out);
    fputs(") {\n", out);
    emit_body_items(n->body, out, level + 1);
    emit_indent(out, level);
    fputs("}\n", out);
}

/* Writes a for loop; the initialiser may be an inline declaration, an
 * expression or absent, and condition/update may be absent. */
static void emit_for(const Node *n, FILE *out, int level) {
    emit_indent(out, level);
    fputs("for (", out);
    if (n->init != NULL) {
        if (n->init->kind == NODE_VAR_DECL) {
            emit_decl_core(n->init, out);
        } else {
            emit_expr(n->init, out, 0);
        }
    }
    fputs(";", out);
    if (n->cond != NULL) {
        fputc(' ', out);
        emit_cond(n->cond, out);
    }
    fputs(";", out);
    if (n->update != NULL) {
        fputc(' ', out);
        emit_expr(n->update, out, 0);
    }
    fputs(") {\n", out);
    emit_body_items(n->body, out, level + 1);
    emit_indent(out, level);
    fputs("}\n", out);
}

/* Writes a return statement with an optional value. */
static void emit_return(const Node *n, FILE *out, int level) {
    emit_indent(out, level);
    if (n->expr != NULL) {
        fputs("return ", out);
        emit_expr(n->expr, out, 0);
        fputs(";\n", out);
    } else {
        fputs("return;\n", out);
    }
}

/* Writes a bare nested block statement. */
static void emit_block(const Node *n, FILE *out, int level) {
    emit_indent(out, level);
    fputs("{\n", out);
    emit_body_items(n, out, level + 1);
    emit_indent(out, level);
    fputs("}\n", out);
}

/* Writes one statement (declaration, control flow, block or expression). */
static void emit_stmt(const Node *n, FILE *out, int level) {
    switch (n->kind) {
    case NODE_VAR_DECL:
        emit_indent(out, level);
        emit_decl_core(n, out);
        fputs(";\n", out);
        break;
    case NODE_BLOCK:
        emit_block(n, out, level);
        break;
    case NODE_IF_STMT:
        emit_if(n, out, level);
        break;
    case NODE_WHILE_STMT:
        emit_while(n, out, level);
        break;
    case NODE_FOR_STMT:
        emit_for(n, out, level);
        break;
    case NODE_RETURN_STMT:
        emit_return(n, out, level);
        break;
    case NODE_EXPR_STMT:
        emit_indent(out, level);
        if (n->expr != NULL) {
            emit_expr(n->expr, out, 0);
        }
        fputs(";\n", out);
        break;
    default:
        break;
    }
}

/* Writes a function signature (return type, name, parameter list) without
 * any terminator; no parameters become "(void)". */
static void emit_signature(const Node *fn, FILE *out) {
    fprintf(out, "%s %s(", type_text(fn->decl_type), fn->name);
    if (fn->params.count == 0) {
        fputs("void", out);
    }
    for (int i = 0; i < fn->params.count; i++) {
        if (i > 0) {
            fputs(", ", out);
        }
        fprintf(out, "%s %s", type_text(fn->params.items[i]->decl_type),
                fn->params.items[i]->name);
    }
    fputc(')', out);
}

/* Returns non-zero when the statement definitely returns on every path,
 * using the same rules as the checker (PLAN.md §8.4): a return returns, a
 * block returns if any statement in it returns, an if returns only when it
 * has an else and both branches return, loops never count. */
static int always_returns(const Node *n) {
    if (n == NULL) {
        return 0;
    }
    switch (n->kind) {
    case NODE_RETURN_STMT:
        return 1;
    case NODE_BLOCK:
        for (int i = 0; i < n->items.count; i++) {
            if (always_returns(n->items.items[i])) {
                return 1;
            }
        }
        return 0;
    case NODE_IF_STMT:
        return n->else_branch != NULL && always_returns(n->body) &&
               always_returns(n->else_branch);
    default:
        return 0;
    }
}

/* Writes a full function definition. A non-void function whose body may
 * fall off the end (the checker already warned about it) gets a defensive
 * "return 0;" so the generated C stays free of -Wreturn-type warnings;
 * in C, using the value of such a call would be undefined behaviour. */
static void emit_function(const Node *fn, FILE *out) {
    emit_signature(fn, out);
    fputs(" {\n", out);
    emit_body_items(fn->body, out, 1);
    if (fn->decl_type != TYPE_VOID && !always_returns(fn->body)) {
        emit_indent(out, 1);
        fputs("return 0; /* inserted by minic: missing return */\n", out);
    }
    fputs("}\n", out);
}

/* Returns non-zero when the function is the program entry point. */
static int is_main(const Node *fn) {
    return strcmp(fn->name, "main") == 0;
}

/* Writes all global variables; returns how many were written. */
static int emit_globals(const Node *program, FILE *out) {
    int count = 0;
    for (int i = 0; i < program->items.count; i++) {
        const Node *n = program->items.items[i];
        if (n->kind == NODE_VAR_DECL) {
            emit_decl_core(n, out);
            fputs(";\n", out);
            count++;
        }
    }
    return count;
}

/* Writes prototypes for every function except main; returns how many. */
static int emit_prototypes(const Node *program, FILE *out) {
    int count = 0;
    for (int i = 0; i < program->items.count; i++) {
        const Node *n = program->items.items[i];
        if (n->kind == NODE_FUNC_DECL && !is_main(n)) {
            emit_signature(n, out);
            fputs(";\n", out);
            count++;
        }
    }
    return count;
}

/* Writes every function definition, separated by single blank lines. */
static void emit_definitions(const Node *program, FILE *out) {
    int first = 1;
    for (int i = 0; i < program->items.count; i++) {
        const Node *n = program->items.items[i];
        if (n->kind == NODE_FUNC_DECL) {
            if (!first) {
                fputc('\n', out);
            }
            emit_function(n, out);
            first = 0;
        }
    }
}

/* Writes the whole translation unit: header, globals, prototypes, functions. */
void codegen_program(const Node *program, FILE *out) {
    fputs("/* Generated by minic - do not edit */\n#include <stdio.h>\n", out);
    fputc('\n', out);
    if (emit_globals(program, out) > 0) {
        fputc('\n', out);
    }
    if (emit_prototypes(program, out) > 0) {
        fputc('\n', out);
    }
    emit_definitions(program, out);
}
