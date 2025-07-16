#include "eline.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <ctype.h>

#define ELINES_INIT_CAP 128

#define ANSI_CLEAR_LINE "\033[2K"
#define ANSI_MOVE_CURSOR_START "\033[G"
#define ANSI_CURSOR_FORWARD "\033[1C"
#define ANSI_CURSOR_BACKWARD "\033[1D"

static struct termios original_term;

static void enable_raw_mode() {
    struct termios raw = original_term;
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_term);
}

void line_init(Line *line) {
    line->buffer = malloc(ELINES_INIT_CAP);
    line->buffer[0] = '\0';
    line->len = 0;
    line->point = 0;
    line->cap = ELINES_INIT_CAP;
}

void line_free(Line *line) {
    free(line->buffer);
    line->buffer = NULL;
    line->len = line->point = line->cap = 0;
}

void insert(Line *line, char c) {
    if (line->len + 1 >= line->cap) {
        line->cap *= 2;
        line->buffer = realloc(line->buffer, line->cap);
    }
    memmove(line->buffer + line->point + 1, line->buffer + line->point, line->len - line->point);
    line->buffer[line->point] = c;
    line->len++;
    line->point++;
    line->buffer[line->len] = '\0';
}


void delete_backward_char(Line *line) {
    if (line->point == 0) return;
    memmove(line->buffer + line->point - 1, line->buffer + line->point, line->len - line->point);
    line->len--;
    line->point--;
    line->buffer[line->len] = '\0';
}


void delete_char(Line *line) {
    if (line->point == line->len) return;
    memmove(line->buffer + line->point, line->buffer + line->point + 1, line->len - line->point - 1);
    line->len--;
    line->buffer[line->len] = '\0';
}

void backward_char(Line *line) {
    if (line->point > 0) line->point--;
}

void forward_char(Line *line) {
    if (line->point < line->len) line->point++;
}

void move_beginning_of_line(Line *line) {
    line->point = 0;
}

void move_end_of_line(Line *line) {
    line->point = line->len;
}

void kill_region(Line *line) {
    if (!line->region.active || line->region.mark == line->point) return;

    size_t start = line->region.mark < line->point ? line->region.mark : line->point;
    size_t end   = line->region.mark > line->point ? line->region.mark : line->point;
    memmove(line->buffer + start, line->buffer + end, line->len - end);
    line->len -= (end - start);
    line->point = start;
    line->buffer[line->len] = '\0';
    line->region.active = false; // deactivate region after killing
}

void clear_line(Line *line) {
    line->buffer[0] = '\0';
    line->len = 0;
    line->point = 0;
    line->region.active = false;
}

// Redraw the current line (after cursor movement)
static void line_refresh(Line *line, const char *prompt) {
    printf(ANSI_CLEAR_LINE ANSI_MOVE_CURSOR_START "%s%s", prompt, line->buffer);
    fflush(stdout);
    // Move cursor back to correct position
    if (line->point < line->len) {
        printf("\033[%zuD", line->len - line->point);
        fflush(stdout);
    }
}

// Read a line with Emacs keybindings
bool line_read(Line *line, const char *prompt) {
    tcgetattr(STDIN_FILENO, &original_term);
    enable_raw_mode();

    clear_line(line);
    printf("%s", prompt);
    fflush(stdout);

    char c;
    while (read(STDIN_FILENO, &c, 1) == 1) {
        if (c == '\n') {
            printf("\n");
            break;
        }else if (c == 4) {  // Ctrl-D (EOF)
            if (line->len == 0) {
                printf("\n");
                disable_raw_mode();
                return false;
            } else {
                delete_char(line);
            }
        } else if (c == 1) {  // C-a
            move_beginning_of_line(line);
        } else if (c == 5) {  // C-e
            move_end_of_line(line);
        } else if (c == 2) {  // C-b
            backward_char(line);
        } else if (c == 6) {  // C-f
            forward_char(line);
        } else if (c == 21) {  // C-u 
            kill_region(line);
        } else if (c == 127 || c == 8) {  // Backspace / Ctrl-H
            delete_backward_char(line);
        } else if (c == 0) { // Ctrl-Space, ASCII 0
                line->region.mark = line->point;
                line->region.active = true;
        } else if (isprint(c)) {  // Printable characters
            insert(line, c);
        }
        line_refresh(line, prompt);
    }

    disable_raw_mode();
    return true;
}
