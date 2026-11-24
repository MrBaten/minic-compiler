// factorial.c - recursion versus iteration.
// Demonstrates: a recursive function, an iterative function using a while
// loop, and comparing both. 12! = 479001600 is the largest factorial that
// fits in a 32-bit int, so we stop there.

/* Recursive: n! = n * (n - 1)!, with 0! = 1! = 1 as the base case. */
int factorial(int n) {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

/* Iterative: multiply a running result by 2, 3, ..., n. */
int factorial_iter(int n) {
    int result = 1;
    int i = 2;
    while (i <= n) {
        result = result * i;
        i = i + 1;
    }
    return result;
}

int main() {
    print(factorial(0));          // 1
    print(factorial(5));          // 120
    print(factorial(7));          // 5040
    print(factorial(10));         // 3628800
    print(factorial(12));         // 479001600

    print(factorial_iter(5));     // 120
    print(factorial_iter(10));    // 3628800
    print(factorial_iter(12));    // 479001600

    // A table of 1! .. 6!
    int n = 1;
    while (n <= 6) {
        print(factorial(n));
        n = n + 1;
    }

    if (factorial(7) == factorial_iter(7)) {
        print("recursive and iterative agree");
    }
    return 0;
}
