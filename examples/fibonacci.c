// fibonacci.c - the Fibonacci sequence, two ways.
// Demonstrates: double recursion, an iterative version with a block-local
// temporary, a for loop, and comparing the two implementations.

/* Recursive: fib(n) = fib(n - 1) + fib(n - 2). Slow, but very direct. */
int fib(int n) {
    if (n < 2) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

/* Iterative: slide a window of two values along the sequence. */
int fib_iter(int n) {
    int prev = 0;
    int cur = 1;
    int i = 0;
    while (i < n) {
        int next = prev + cur;
        prev = cur;
        cur = next;
        i = i + 1;
    }
    return prev;
}

int main() {
    // First eleven numbers: 0 1 1 2 3 5 8 13 21 34 55
    for (int i = 0; i <= 10; i = i + 1) {
        print(fib(i));
    }

    print(fib(15));        // 610
    print(fib_iter(20));   // 6765
    print(fib_iter(30));   // 832040

    if (fib(12) == fib_iter(12)) {
        print("both versions match");
    }
    return 0;
}
