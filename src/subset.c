#include "syscalls.h"
#include "kerneltypes.h"
#include "string.h"

int main( const char *argv[], int argc )
{
    int i = 1;
    while (i < argc && argv[i][0] == '-') {
        switch (argv[i][1]) {
            case 'w':
                // Window
                // Usage: -w <x> <y> <offset_x> <offset_y>
                break;
            case 'n':
                // Namespace
                // Usage: -n <ns_name> <path/to/lower/root>
                break;
        }
    }
    const char *path = argv[i];
    const char **new_argv = argv + i;
    int new_argc = argc - i;
    exec(path, new_argv, new_argc);
    return 0;
}
