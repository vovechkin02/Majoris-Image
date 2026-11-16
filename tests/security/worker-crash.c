#include <signal.h>
#include <string.h>
#include <unistd.h>
int
main (int argc, char **argv)
{
    if (argc > 1 && !strcmp (argv[1], "hang")) {
        sleep (10);
        return 0;
    }
    raise (SIGSEGV);
    return 1;
}
