/* interp.c - tree-walking interpreter for checked Mini-C programs.
 *
 * The interpreter executes the AST directly, following the same semantics
 * as the C code that codegen.c emits:
 *   - int arithmetic is 32-bit two's complement (wrap-around, no UB here);
 *   - a value stored into a char variable is truncated to char;
 *   - && and || short-circuit and yield 0 or 1;
 *   - a non-void function that falls off its end returns 0 (codegen inserts
 *     the same "return 0;");
 *   - print() writes exactly what the generated printf() call would.
 * Division by zero, INT_MIN / -1, runaway recursion and (optionally) a step
 * limit are reported as runtime errors instead of crashing. Like lexer and
 * parser errors, a runtime error is fatal: it is printed and the process
 * exits with status 1 (no setjmp, so the code also builds for WebAssembly).
 */
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "interp.h"

/* Maximum nesting of Mini-C function calls before reporting a runtime error. */
#define MAX_CALL_DEPTH 5000

/* One live variable: name (borrowed from the AST), declared type and value. */
typedef struct {
    const char *name;
    TypeKind type;
    int value;
} Var;

/* Whole interpreter state. */
typedef struct {
    const Node *program;
    FILE *out;
    long max_steps;
    long steps;
    Var *vars;          /* globals at the bottom, then the active call frames */
    int nvars;
    int var_cap;
    int nglobals;
    int frame_base;     /* first variable index of the current function frame */
    int *args;          /* scratch stack for evaluated call arguments */
    int nargs;
    int arg_cap;
    int depth;
    int return_value;
} Interp;

/* Control-flow outcome of executing a statement. */
typedef enum { FLOW_NORMAL, FLOW_RETURN } Flow;

static int eval(Interp *in, const Node *n);
static Flow exec(Interp *in, const Node *n);

/* Report a runtime error at a node's position and exit with status 1. */
static void runtime_error(Interp *in, const Node *at, const char *fmt, ...)
{
    va_list ap;

    fflush(in->out);
    fprintf(stderr, "Runtime Error at line %d, column %d: ", at->line, at->column);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

/* Abort with the standard out-of-memory message. */
static void out_of_memory(void)
{
    fprintf(stderr, "minic: out of memory\n");
    exit(1);
}

/* Count one unit of work and enforce the optional step limit. */
static void step(Interp *in, const Node *at)
{
    in->steps++;
    if (in->max_steps > 0 && in->steps > in->max_steps) {
        runtime_error(in, at, "step limit of %ld exceeded (infinite loop?)", in->max_steps);
    }
}

/* Convert a value to the representation of a variable of the given type. */
static int store_as(TypeKind type, int value)
{
    return type == TYPE_CHAR ? (int)(char)value : value;
}

/* Push a new variable onto the variable stack. */
static void push_var(Interp *in, const char *name, TypeKind type, int value)
{
    if (in->nvars == in->var_cap) {
        int cap = in->var_cap == 0 ? 64 : in->var_cap * 2;
        Var *grown = realloc(in->vars, (size_t)cap * sizeof(Var));
        if (grown == NULL) {
            out_of_memory();
        }
        in->vars = grown;
        in->var_cap = cap;
    }
    in->vars[in->nvars].name = name;
    in->vars[in->nvars].type = type;
    in->vars[in->nvars].value = store_as(type, value);
    in->nvars++;
}

/* Push an evaluated argument onto the argument scratch stack. */
static void push_arg(Interp *in, int value)
{
    if (in->nargs == in->arg_cap) {
        int cap = in->arg_cap == 0 ? 32 : in->arg_cap * 2;
        int *grown = realloc(in->args, (size_t)cap * sizeof(int));
        if (grown == NULL) {
            out_of_memory();
        }
        in->args = grown;
        in->arg_cap = cap;
    }
    in->args[in->nargs++] = value;
}

/* Find a visible variable: innermost in the current frame first, then globals. */
static Var *lookup(Interp *in, const Node *at, const char *name)
{
    int i;

    for (i = in->nvars - 1; i >= in->frame_base; i--) {
        if (strcmp(in->vars[i].name, name) == 0) {
            return &in->vars[i];
        }
    }
    for (i = in->nglobals - 1; i >= 0; i--) {
        if (strcmp(in->vars[i].name, name) == 0) {
            return &in->vars[i];
        }
    }
    runtime_error(in, at, "unknown variable '%s'", name);
    return NULL;
}

/* Find a function definition by name. */
static const Node *find_function(const Interp *in, const char *name)
{
    int i;

    for (i = 0; i < in->program->items.count; i++) {
        const Node *item = in->program->items.items[i];
        if (item->kind == NODE_FUNC_DECL && strcmp(item->name, name) == 0) {
            return item;
        }
    }
    return NULL;
}

/* Write a raw string literal (escapes still encoded) the way printf would,
 * stopping at an embedded \0 just like a C format string. */
static void write_raw_string(FILE *out, const char *s)
{
    while (*s != '\0') {
        char c = *s++;
        if (c == '\\' && *s != '\0') {
            char e = *s++;
            switch (e) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case '0': return;
            default:  c = e; break;   /* \\  \'  \" */
            }
        }
        fputc(c, out);
    }
}

/* Execute the print() builtin. */
static void do_print(Interp *in, const Node *call)
{
    const Node *arg = call->items.items[0];

    if (arg->kind == NODE_STRING_EXPR) {
        write_raw_string(in->out, arg->str_value);
        fputc('\n', in->out);
    } else if (arg->eval_type == TYPE_CHAR) {
        int v = eval(in, arg);
        fputc((unsigned char)v, in->out);
        fputc('\n', in->out);
    } else {
        fprintf(in->out, "%d\n", eval(in, arg));
    }
}

/* Run a function body with already-evaluated arguments; returns its value. */
static int call_function(Interp *in, const Node *fn, const Node *at, int arg_base)
{
    int saved_base = in->frame_base;
    int new_base = in->nvars;
    int result = 0;
    int i;

    step(in, at);
    if (++in->depth > MAX_CALL_DEPTH) {
        runtime_error(in, at, "call stack overflow in '%s' (recursion deeper than %d)",
                      fn->name, MAX_CALL_DEPTH);
    }
    for (i = 0; i < fn->params.count; i++) {
        const Node *p = fn->params.items[i];
        push_var(in, p->name, p->decl_type, in->args[arg_base + i]);
    }
    in->nargs = arg_base;
    in->frame_base = new_base;

    /* Params and the body's top-level statements share one scope. */
    for (i = 0; i < fn->body->items.count; i++) {
        if (exec(in, fn->body->items.items[i]) == FLOW_RETURN) {
            result = in->return_value;
            break;
        }
    }

    in->nvars = new_base;
    in->frame_base = saved_base;
    in->depth--;
    return fn->decl_type == TYPE_VOID ? 0 : store_as(fn->decl_type, result);
}

/* Evaluate a call expression (builtin print or a user function). */
static int eval_call(Interp *in, const Node *n)
{
    const Node *fn;
    int base;
    int i;

    if (strcmp(n->name, "print") == 0) {
        do_print(in, n);
        return 0;
    }
    fn = find_function(in, n->name);
    if (fn == NULL) {
        runtime_error(in, n, "unknown function '%s'", n->name);
    }
    base = in->nargs;
    for (i = 0; i < n->items.count; i++) {
        push_arg(in, eval(in, n->items.items[i]));
    }
    return call_function(in, fn, n, base);
}

/* Evaluate a binary expression with C semantics (wrap-around, short-circuit). */
static int eval_binary(Interp *in, const Node *n)
{
    int a;
    int b;

    if (n->op == OP_AND) {
        return eval(in, n->left) ? (eval(in, n->right) != 0) : 0;
    }
    if (n->op == OP_OR) {
        return eval(in, n->left) ? 1 : (eval(in, n->right) != 0);
    }
    a = eval(in, n->left);
    b = eval(in, n->right);
    switch (n->op) {
    case OP_ADD: return (int)((unsigned)a + (unsigned)b);
    case OP_SUB: return (int)((unsigned)a - (unsigned)b);
    case OP_MUL: return (int)((unsigned)a * (unsigned)b);
    case OP_DIV:
    case OP_MOD:
        if (b == 0) {
            runtime_error(in, n, "division by zero");
        }
        if (a == INT_MIN && b == -1) {
            runtime_error(in, n, "integer overflow in '%s'", ast_op_symbol(n->op));
        }
        return n->op == OP_DIV ? a / b : a % b;
    case OP_EQ:  return a == b;
    case OP_NEQ: return a != b;
    case OP_LT:  return a < b;
    case OP_GT:  return a > b;
    case OP_LE:  return a <= b;
    case OP_GE:  return a >= b;
    default:     break;
    }
    runtime_error(in, n, "unsupported operator");
    return 0;
}

/* Evaluate an expression and return its integer value. */
static int eval(Interp *in, const Node *n)
{
    switch (n->kind) {
    case NODE_NUMBER_EXPR:
        return n->int_value;
    case NODE_CHAR_EXPR:
        return (int)(char)n->int_value;
    case NODE_VAR_EXPR:
        return lookup(in, n, n->name)->value;
    case NODE_ASSIGN_EXPR: {
        int value = eval(in, n->right);
        Var *v = lookup(in, n->left, n->left->name);
        v->value = store_as(v->type, value);
        return v->value;
    }
    case NODE_UNARY_EXPR: {
        int v = eval(in, n->expr);
        return n->op == OP_NEG ? (int)(0u - (unsigned)v) : !v;
    }
    case NODE_BINARY_EXPR:
        return eval_binary(in, n);
    case NODE_CALL_EXPR:
        return eval_call(in, n);
    default:
        break;
    }
    runtime_error(in, n, "cannot evaluate this expression");
    return 0;
}

/* Execute a statement; FLOW_RETURN means a return statement was reached. */
static Flow exec(Interp *in, const Node *n)
{
    int mark;
    int i;
    Flow flow = FLOW_NORMAL;

    step(in, n);
    switch (n->kind) {
    case NODE_VAR_DECL:
        push_var(in, n->name, n->decl_type, n->init != NULL ? eval(in, n->init) : 0);
        break;
    case NODE_BLOCK:
        mark = in->nvars;
        for (i = 0; i < n->items.count && flow == FLOW_NORMAL; i++) {
            flow = exec(in, n->items.items[i]);
        }
        in->nvars = mark;
        break;
    case NODE_IF_STMT:
        if (eval(in, n->cond)) {
            flow = exec(in, n->body);
        } else if (n->else_branch != NULL) {
            flow = exec(in, n->else_branch);
        }
        break;
    case NODE_WHILE_STMT:
        while (flow == FLOW_NORMAL && eval(in, n->cond)) {
            step(in, n);
            flow = exec(in, n->body);
        }
        break;
    case NODE_FOR_STMT:
        mark = in->nvars;
        if (n->init != NULL) {
            if (n->init->kind == NODE_VAR_DECL) {
                exec(in, n->init);
            } else {
                eval(in, n->init);
            }
        }
        while (n->cond == NULL || eval(in, n->cond)) {
            step(in, n);
            flow = exec(in, n->body);
            if (flow == FLOW_RETURN) {
                break;
            }
            if (n->update != NULL) {
                eval(in, n->update);
            }
        }
        in->nvars = mark;
        break;
    case NODE_RETURN_STMT:
        in->return_value = n->expr != NULL ? eval(in, n->expr) : 0;
        flow = FLOW_RETURN;
        break;
    case NODE_EXPR_STMT:
        if (n->expr != NULL) {
            eval(in, n->expr);
        }
        break;
    default:
        runtime_error(in, n, "cannot execute this statement");
    }
    return flow;
}

/* Public entry point: initialise globals, run main, report its exit code.
 * Returns 0 on success; a runtime error exits the process with status 1. */
int interp_run(const Node *program, FILE *out, long max_steps, int *exit_code)
{
    Interp *in = calloc(1, sizeof(Interp));
    const Node *main_fn;
    int i;

    if (in == NULL) {
        out_of_memory();
    }
    in->program = program;
    in->out = out;
    in->max_steps = max_steps;

    for (i = 0; i < program->items.count; i++) {
        const Node *item = program->items.items[i];
        if (item->kind == NODE_VAR_DECL) {
            push_var(in, item->name, item->decl_type,
                     item->init != NULL ? eval(in, item->init) : 0);
        }
    }
    in->nglobals = in->nvars;
    in->frame_base = in->nvars;
    main_fn = find_function(in, "main");
    if (main_fn == NULL) {
        runtime_error(in, program, "no 'main' function");
    }
    *exit_code = call_function(in, main_fn, main_fn, in->nargs);

    fflush(out);
    free(in->vars);
    free(in->args);
    free(in);
    return 0;
}
