// gcd.c - greatest common divisor and least common multiple.
// Demonstrates: Euclid's algorithm written iteratively and recursively,
// assigning to parameters, and building one function on top of another.

/* Iterative Euclid: replace (a, b) with (b, a % b) until b is zero. */
int gcd(int a, int b) {
    while (b != 0) {
        int t = b;
        b = a % b;
        a = t;
    }
    return a;
}

/* Recursive Euclid: the same idea expressed as a recursive call. */
int gcd_rec(int a, int b) {
    if (b == 0) {
        return a;
    }
    return gcd_rec(b, a % b);
}

/* lcm(a, b) = a / gcd(a, b) * b  (dividing first avoids overflow). */
int lcm(int a, int b) {
    return a / gcd(a, b) * b;
}

int main() {
    print(gcd(48, 18));       // 6
    print(gcd(17, 5));        // 1
    print(gcd(100, 75));      // 25
    print(gcd(0, 9));         // 9
    print(gcd_rec(48, 18));   // 6
    print(lcm(4, 6));         // 12
    print(lcm(21, 6));        // 42
    return 0;
}
