// variables.c - int and char variables, a global variable, reassignment.
// Demonstrates: declarations with and without initializers, global state,
// printing a char (prints the character) versus an int (prints the number),
// and assigning between char and int, which are mutually assignable.

/* A global variable: its initializer must be a constant expression. */
int counter = 10;

int main() {
    int age = 25;          // int with an initializer
    char grade = 'A';      // char with a character literal
    char letter;           // declared first, assigned before it is read
    letter = 'z';

    print(age);            // 25
    print(grade);          // A
    print(letter);         // z
    print(counter);        // 10

    /* Reassignment replaces the old value. */
    age = age + 1;
    print(age);            // 26
    grade = 'B';
    print(grade);          // B
    counter = counter * 2;
    print(counter);        // 20

    /* char and int convert freely: 'B' is character code 66. */
    int code = grade;
    print(code);           // 66
    code = code + 1;
    char next_grade = code;
    print(next_grade);     // C
    return 0;
}
