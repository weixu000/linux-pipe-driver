#include <stdio.h>

int main(void) {
    FILE *fp = fopen("/dev/mypipe/mypipe_in", "w");

    for (size_t i = 0; fprintf(fp, "%zu\n", i) >= 0; ++i) {
        printf("%zu\n", i);
    }
}