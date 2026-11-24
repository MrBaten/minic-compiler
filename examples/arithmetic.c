// arithmetic.c - the five arithmetic operators and how they combine.
// Demonstrates: + - * / %, precedence (* / % bind tighter than + -),
// parentheses, unary minus, and left associativity.

int main() {
    int a = 10;
    int b = 3;

    /* The basic operators. Division on ints truncates toward zero. */
    print(a + b);            // 13
    print(a - b);            // 7
    print(a * b);            // 30
    print(a / b);            // 3
    print(a % b);            // 1

    /* Precedence: multiplication happens before addition. */
    print(2 + 3 * 4);        // 14
    print((2 + 3) * 4);      // 20
    print(20 - 4 * 2 + 1);   // 13
    print(2 * 3 + 4 * 5);    // 26
    print(17 % 5 * 2);       // 4

    /* Left associativity: (10 - 3) - 2, not 10 - (3 - 2). */
    print(10 - 3 - 2);       // 5
    print(100 / 10 / 5);     // 2

    /* Unary minus. */
    print(-a);               // -10
    print(-(a + b));         // -13
    print(7 - -2);           // 9
    print(-7 / 2);           // -3  (truncates toward zero)
    print(-7 % 3);           // -1  (sign follows the left operand)

    /* Variables inside a larger expression. */
    print(a * (b + 2) - a / b);  // 47
    return 0;
}
