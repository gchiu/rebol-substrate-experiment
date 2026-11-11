#include "c1_compiler.h"
#include "b3_machine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
    if (argc < 2) return 2;
    FILE *f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 2; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *s = malloc((size_t)n + 1); if (!s) return 2;
    if (fread(s, 1, (size_t)n, f) != (size_t)n) { fclose(f); return 2; }
    fclose(f); s[n] = 0;
    b3_code code; char err[512];
    int ok = c1_compile(s, NULL, 0, B3_CONV_RETSTACK, &code, err, sizeof err);
    printf("%s: %s%s\n", argv[1], ok ? "COMPILED" : "UNSUPPORTED: ", ok ? "" : err);
    free(s); return ok ? 0 : 1;
}
