#include <stdio.h>

int main(void) {
    FILE *fp = fopen("/dev/mypipe/mypipe_out", "r");

    size_t i;
    while (fscanf(fp, "%zu", &i) >= 0) {
        printf("%zu\n", i);
    }
}
