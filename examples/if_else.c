// if_else.c - conditional statements.
// Demonstrates: if, else, else-if chains, the comparison operators
// (== != < > <= >=), and the logical operators && || !.

/* Returns -1 for negative numbers, 0 for zero and 1 for positive numbers. */
int classify(int n) {
    if (n < 0) {
        return -1;
    } else if (n == 0) {
        return 0;
    } else {
        return 1;
    }
}

int main() {
    int x = 7;

    // Simple if / else with single-statement bodies.
    if (x > 5)
        print("x is greater than 5");
    else
        print("x is not greater than 5");

    // An else-if chain: the first true condition wins.
    int score = 85;
    if (score >= 90) {
        print("Grade A");
    } else if (score >= 80) {
        print("Grade B");
    } else if (score >= 70) {
        print("Grade C");
    } else {
        print("Grade F");
    }

    // Equality and relational operators.
    if (x == 7) print("x == 7");
    if (x != 8) print("x != 8");
    if (x <= 7 && x >= 7) print("x is exactly 7");

    // Logical operators on int values (0 is false, anything else is true).
    int yes = 1;
    int no = 0;
    if (yes && no) print("and: true"); else print("and: false");
    if (yes || no) print("or: true"); else print("or: false");
    if (!no) print("not no: true");
    if (!(x == 7)) print("never printed"); else print("negated test is false");
    if (x < 0 || x > 100) {
        print("out of range");
    } else {
        print("in range");
    }

    // Using a function that contains an if / else-if / else chain.
    print(classify(-5));   // -1
    print(classify(0));    // 0
    print(classify(42));   // 1
    return 0;
}
