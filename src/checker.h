#ifndef MINIC_CHECKER_H
#define MINIC_CHECKER_H

#include "ast.h"

/* Semantic analysis. Prints every error and warning to stderr, sets
 * eval_type on every expression node and returns the number of errors. */
int check_program(Node *program);

#endif
