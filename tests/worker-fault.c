#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc != 6) return 2;
    FILE *source = fopen(argv[2], "rb");
    if (!source) return 2;
    int mode = fgetc(source);
    fclose(source);
    if (mode == 'q') {
        fputs("{\"kind\":\"missing-codec\",\"message\":\"No native test codec\"}\n", stderr);
        return 1;
    }
    if (mode == 'C') raise(SIGSEGV);
    if (mode == 'T') { sleep(10); return 1; }
    if (mode == 'R') {
        if (fopen(argv[2], "wb") != NULL) return 2;
        fputs("{\"kind\":\"unsupported\",\"message\":\"source is read-only\"}\n", stderr);
        return 1;
    }
    FILE *output = fopen(argv[3], "wb");
    if (!output) return 2;
    fputs("{\"protocol-version\":1,\"width\":-1,\"height\":16,\"loader\":\"fake\"}", output);
    return fclose(output) != 0;
}
