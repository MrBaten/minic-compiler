// fizzbuzz.c - the interview classic.
// Demonstrates: a for loop, the % operator, an else-if chain, and mixing
// string output with number output. Multiples of 3 print Fizz, multiples
// of 5 print Buzz, multiples of both print FizzBuzz.

int main() {
    for (int i = 1; i <= 20; i = i + 1) {
        if (i % 15 == 0) {
            print("FizzBuzz");
        } else if (i % 3 == 0) {
            print("Fizz");
        } else if (i % 5 == 0) {
            print("Buzz");
        } else {
            print(i);
        }
    }
    return 0;
}
