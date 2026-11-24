// function.c - defining and calling functions.
// Demonstrates: parameters and return values, a void function, a function
// with an explicit (void) parameter list, a function that returns char,
// nested calls, and calling a function before its definition appears
// (twice_square uses square, which is defined further down).

void banner(void) {
    print("=== functions ===");
}

int add(int a, int b) {
    return a + b;
}

int max(int a, int b) {
    if (a > b) {
        return a;
    }
    return b;
}

/* A void function: no return value, called as a statement. */
void greet(int times) {
    int i = 0;
    while (i < times) {
        print("Hello from greet");
        i = i + 1;
    }
}

/* Returns a char, so print() shows a letter rather than a number. */
char grade_letter(int score) {
    if (score >= 90) {
        return 'A';
    }
    if (score >= 80) {
        return 'B';
    }
    return 'C';
}

// square is defined after twice_square, yet twice_square may call it.
int twice_square(int n) {
    return square(n) * 2;
}

int square(int n) {
    return n * n;
}

int main() {
    banner();
    print(add(2, 3));                   // 5
    print(add(add(1, 2), add(3, 4)));   // 10
    print(max(4, 9));                   // 9
    print(max(12, -3));                 // 12
    greet(2);                           // two lines
    print(grade_letter(95));            // A
    print(grade_letter(85));            // B
    print(grade_letter(50));            // C
    print(square(7));                   // 49
    print(twice_square(3));             // 18
    return 0;
}
