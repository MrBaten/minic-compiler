// primes.c - prime numbers by trial division.
// Demonstrates: early returns inside a loop, a function used as an if
// condition, a for loop, and a counter updated inside nested blocks.

/* Returns 1 if n is prime, otherwise 0. Tries divisors d while d*d <= n. */
int is_prime(int n) {
    if (n < 2) {
        return 0;
    }
    int d = 2;
    while (d * d <= n) {
        if (n % d == 0) {
            return 0;
        }
        d = d + 1;
    }
    return 1;
}

int main() {
    int count = 0;
    for (int n = 2; n < 50; n = n + 1) {
        if (is_prime(n)) {
            print(n);
            count = count + 1;
        }
    }
    print("Number of primes below 50:");
    print(count);
    return 0;
}
