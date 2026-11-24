// while_loop.c - repetition with while.
// Demonstrates: counting up, counting down, accumulating a sum, and
// nested loops. Mini-C has no ++ or +=, so we write i = i + 1.

int main() {
    // Count from 1 to 5.
    int i = 1;
    while (i <= 5) {
        print(i);
        i = i + 1;
    }

    // Sum of 1..10, kept in an accumulator.
    int total = 0;
    i = 1;
    while (i <= 10) {
        total = total + i;
        i = i + 1;
    }
    print(total);            // 55

    // Countdown.
    int n = 3;
    while (n > 0) {
        print(n);
        n = n - 1;
    }
    print("Liftoff!");

    /* Nested loops: a 3 x 3 multiplication table, one product per line.
       The inner counter j is declared inside the outer loop body, so it
       starts fresh on every pass. */
    int row = 1;
    while (row <= 3) {
        int j = 1;
        while (j <= 3) {
            print(row * j);
            j = j + 1;
        }
        row = row + 1;
    }
    return 0;
}
