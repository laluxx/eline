#include "eline.h"
#include <stdio.h>

int main() {
    Line line;
    line_init(&line);

    printf("ELines REPL (Press Ctrl-D to exit)\n");
    while (1) {
        bool success = line_read(&line, ">> ");
        if (!success) break;  // Ctrl-D on empty line exits

        printf("You entered: '%s'\n", line.buffer);
    }

    line_free(&line);
    return 0;
}
