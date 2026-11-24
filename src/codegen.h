#ifndef MINIC_CODEGEN_H
#define MINIC_CODEGEN_H

#include <stdio.h>
#include "ast.h"

/* Writes portable C11 for a checked NODE_PROGRAM (every expression must have
 * its eval_type set by the checker) to `out`. Never fails. */
void codegen_program(const Node *program, FILE *out);

#endif
