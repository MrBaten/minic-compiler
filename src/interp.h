#ifndef MINIC_INTERP_H
#define MINIC_INTERP_H

#include <stdio.h>
#include "ast.h"

/*
 * Execute a checked Mini-C program directly (tree-walking interpreter).
 *
 * Preconditions: check_program(program) returned 0, so every expression
 * node carries a valid eval_type.
 *
 * Program output from print() goes to `out`; runtime errors are reported
 * to stderr as "Runtime Error at line L, column C: <message>".
 *
 * max_steps limits the number of executed statements, loop iterations and
 * calls (0 = unlimited); exceeding it is a runtime error, which lets an
 * embedding (e.g. the web playground) stop infinite loops.
 *
 * Returns 0 and stores main's return value in *exit_code. A runtime error
 * is fatal (like lexer and parser errors): it is printed and the process
 * exits with status 1.
 */
int interp_run(const Node *program, FILE *out, long max_steps, int *exit_code);

#endif
