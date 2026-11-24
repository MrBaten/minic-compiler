/* main.c - command-line driver for the Mini-C to C transpiler. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"
#include "ast.h"
#include "parser.h"
#include "checker.h"
#include "codegen.h"

/* Command-line options collected by parse_args. */
typedef struct {
    const char *input;
    const char *output;
    int show_tokens;
    int show_ast;
    int show_help;
} Options;

/* Print the usage text to the given stream. */
static void print_usage(FILE *out)
{
    fprintf(out, "Usage: minic <input.c> [-o <output.c>] [--tokens] [--ast]\n");
    fprintf(out, "       minic -h | --help\n");
}

/* Parse argv into opts; return 1 on success, 0 on a usage error. */
static int parse_args(int argc, char **argv, Options *opts)
{
    int i;

    memset(opts, 0, sizeof(*opts));
    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            opts->show_help = 1;
        } else if (strcmp(arg, "--tokens") == 0) {
            opts->show_tokens = 1;
        } else if (strcmp(arg, "--ast") == 0) {
            opts->show_ast = 1;
        } else if (strcmp(arg, "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "minic: error: option '-o' requires an argument\n");
                return 0;
            }
            opts->output = argv[++i];
        } else if (arg[0] == '-' && arg[1] != '\0') {
            fprintf(stderr, "minic: error: unknown option '%s'\n", arg);
            return 0;
        } else if (opts->input != NULL) {
            fprintf(stderr, "minic: error: multiple input files given\n");
            return 0;
        } else {
            opts->input = arg;
        }
    }
    if (!opts->show_help && opts->input == NULL) {
        fprintf(stderr, "minic: error: no input file\n");
        return 0;
    }
    return 1;
}

/* Read a whole file into a NUL-terminated heap buffer; NULL if unreadable. */
static char *read_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    char *buf;
    size_t cap = 4096;
    size_t len = 0;
    size_t got;

    if (fp == NULL) {
        return NULL;
    }
    buf = malloc(cap);
    if (buf == NULL) {
        fprintf(stderr, "minic: out of memory\n");
        exit(1);
    }
    while ((got = fread(buf + len, 1, cap - len - 1, fp)) > 0) {
        len += got;
        if (cap - len - 1 == 0) {
            char *grown;
            cap *= 2;
            grown = realloc(buf, cap);
            if (grown == NULL) {
                fprintf(stderr, "minic: out of memory\n");
                exit(1);
            }
            buf = grown;
        }
    }
    if (ferror(fp)) {
        fclose(fp);
        free(buf);
        return NULL;
    }
    fclose(fp);
    buf[len] = '\0';
    return buf;
}

/* Generate C for a checked program into the output file or stdout; return 0 on success. */
static int emit_output(const Node *program, const char *path)
{
    FILE *fp;

    if (path == NULL) {
        codegen_program(program, stdout);
        if (fflush(stdout) != 0 || ferror(stdout)) {
            fprintf(stderr, "minic: error: cannot write file '<stdout>'\n");
            return 1;
        }
        return 0;
    }
    fp = fopen(path, "w");
    if (fp == NULL) {
        fprintf(stderr, "minic: error: cannot write file '%s'\n", path);
        return 1;
    }
    codegen_program(program, fp);
    if (ferror(fp)) {
        fclose(fp);
        fprintf(stderr, "minic: error: cannot write file '%s'\n", path);
        return 1;
    }
    if (fclose(fp) != 0) {
        fprintf(stderr, "minic: error: cannot write file '%s'\n", path);
        return 1;
    }
    return 0;
}

/* Program entry: run the lexer, parser, checker and code generator as requested. */
int main(int argc, char **argv)
{
    Options opts;
    char *source;
    TokenList tokens;
    Node *program;
    int errors;
    int status = 0;

    if (!parse_args(argc, argv, &opts)) {
        print_usage(stderr);
        return 1;
    }
    if (opts.show_help) {
        print_usage(stdout);
        return 0;
    }
    source = read_file(opts.input);
    if (source == NULL) {
        fprintf(stderr, "minic: error: cannot open file '%s'\n", opts.input);
        return 1;
    }

    tokens = lexer_tokenize(source);
    if (opts.show_tokens) {
        token_list_print(&tokens, stdout);
        token_list_free(&tokens);
        free(source);
        return 0;
    }

    program = parse_program(&tokens);
    if (opts.show_ast) {
        print_ast(program, 0);
        ast_free(program);
        token_list_free(&tokens);
        free(source);
        return 0;
    }

    errors = check_program(program);
    if (errors > 0) {
        fprintf(stderr, "minic: compilation failed with %d error(s)\n", errors);
        status = 1;
    } else {
        status = emit_output(program, opts.output);
    }

    ast_free(program);
    token_list_free(&tokens);
    free(source);
    return status;
}
