#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/statvfs.h>
#include <unistd.h>

static int
sandbox_check (const char *source)
{
    char expected[120], secret[1024], network[120];
    FILE *input = fopen (source, "r");
    if (!input) {
        return 2;
    }
    int read = fscanf (input, "%119s %1023s", expected, secret);
    fclose (input);
    ssize_t length = readlink ("/proc/self/ns/net", network, sizeof network - 1);
    if (length < 1 || read != 2) {
        return 2;
    }
    network[length] = 0;
    struct statvfs usr, source_mount;
    struct rlimit memory, files, output_size;
    if (!strcmp (expected + 1, network) || access (secret, F_OK) == 0 || statvfs ("/usr", &usr)
        || !(usr.f_flag & ST_RDONLY) || statvfs (source, &source_mount)
        || !(source_mount.f_flag & ST_RDONLY) || getrlimit (RLIMIT_AS, &memory)
        || memory.rlim_cur != 2147483648ULL || getrlimit (RLIMIT_NOFILE, &files)
        || files.rlim_cur != 128 || getrlimit (RLIMIT_FSIZE, &output_size)
        || output_size.rlim_cur != 1073741824ULL) {
        return 3;
    }
    void *allocation
        = mmap (NULL, memory.rlim_cur + 4096U, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (allocation != MAP_FAILED || errno != ENOMEM) {
        return 3;
    }
    FILE *temporary = fopen ("/tmp/worker-private", "w");
    if (!temporary) {
        return 3;
    }
    return fclose (temporary);
}

int
main (int argc, char **argv)
{
    if (argc != 6) {
        return 2;
    }
    FILE *source = fopen (argv[2], "rb");
    if (!source) {
        return 2;
    }
    int mode = fgetc (source);
    fclose (source);
    if (mode == 'q' || mode == 'B' || mode == 'D' || mode == 'P' || mode == 0 || mode == 137) {
        fputs ("{\"kind\":\"missing-codec\",\"message\":\"No native test codec\"}\n", stderr);
        return 1;
    }
    if (mode == 'C') {
        raise (SIGSEGV);
    }
    if (mode == 'T') {
        sleep (10);
        return 1;
    }
    if (mode == 'R') {
        if (fopen (argv[2], "wb") != NULL) {
            return 2;
        }
        fputs ("{\"kind\":\"unsupported\",\"message\":\"source is read-only\"}\n", stderr);
        return 1;
    }
    if (mode == '!' && sandbox_check (argv[2]) != 0) {
        fputs ("{\"kind\":\"decoder-failure\",\"message\":\"sandbox isolation or resource limit "
               "check failed\"}\n",
               stderr);
        return 1;
    }
    if (mode == 'U') {
        fputs ("{\"kind\":\"future-error\",\"message\":\"unknown diagnostic\"}\n", stderr);
        return 1;
    }
    if (mode == 'M') {
        return 137;
    }
    if (mode == 'X') {
        FILE *partial = fopen (argv[3], "wb");
        if (partial) {
            fputs ("{", partial);
            fclose (partial);
        }
        raise (SIGSEGV);
    }
    if (mode == 'S' || mode == 'Y' || mode == 'L') {
        FILE *manifest = fopen ("/job/image.json", "wb");
        if (!manifest) {
            return 2;
        }
        fputs ("{\"protocol-version\":1,\"width\":1,\"height\":1,\"loader\":\"fault-test\"}",
               manifest);
        if (fclose (manifest)) {
            return 2;
        }
        FILE *pixels = fopen (argv[3], "wb");
        if (!pixels) {
            return 2;
        }
        unsigned char header[] = { 'M', 'I', 'F', '1', 0, 0, 0, 1, 0, 0, 0, 1 };
        const unsigned char valid[16] = { 0 };
        if (mode == 'L') {
            memset (header + 4, 0xff, 4);
        }
        fwrite (header, sizeof header, 1, pixels);
        fwrite (valid, mode == 'S' ? 15 : mode == 'L' ? 0 : 16, 1, pixels);
        return fclose (pixels);
    }
    FILE *output = fopen (argv[3], "wb");
    if (!output) {
        return 2;
    }
    switch (mode) {
    case '!':
    case 'V':
        fputs ("{\"protocol-version\":1,\"width\":1,\"height\":1,\"loader\":\"fault-test\"}",
               output);
        break;
    case 'J':
        fputs ("{broken", output);
        break;
    case 'N':
        for (int i = 0; i < 2000; i++) {
            fputc ('[', output);
        }
        for (int i = 0; i < 2000; i++) {
            fputc (']', output);
        }
        break;
    case 'Q':
        fputs ("{\"protocol-version\":2}", output);
        break;
    case 'Z':
        fputs ("{\"protocol-version\":1,\"width\":0,\"height\":16,\"loader\":\"fake\"}", output);
        break;
    case 'E':
        fputs ("{\"protocol-version\":1,\"width\":2147483648,\"height\":1,\"loader\":\"fake\"}",
               output);
        break;
    case 'K':
        fputs ("{\"protocol-version\":1,\"height\":1,\"loader\":\"fake\"}", output);
        break;
    case 'W':
        fputs ("{\"protocol-version\":1,\"width\":\"1\",\"height\":1,\"loader\":\"fake\"}", output);
        break;
    default:
        fputs ("{\"protocol-version\":1,\"width\":-1,\"height\":16,\"loader\":\"fake\"}", output);
        break;
    }
    return fclose (output) != 0;
}
