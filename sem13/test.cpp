#include <cstring>
#include <stdio.h>

int main(int argc, const char *argv[]) {
    const int words = 1000000;
    const int symbol_per_word = 15;

    char word[symbol_per_word+1] = "";
    memset(word, 'a', symbol_per_word);

    FILE *first = stdout, *second = stderr;
    if (argc > 1) {
        first = stderr; second = stdout;
    }

    for (int i = 0; i < words; i++) {
        fprintf(first, "%s ", word);
    }

    for (int i = 0; i < words/100; i++) {
        fprintf(second, "%s ", word);
    }
}
