#include "eline.h"
#include "keymap.h"
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

    keymap_init(&line->keymap);
    // Set up default key bindings
    keymap_bind(&line->keymap, "C-a", move_beginning_of_line, "Move to beginning of line");
    keymap_bind(&line->keymap, "C-e", move_end_of_line,       "Move to end of line");
    keymap_bind(&line->keymap, "C-b", backward_char,          "Move backward one character");
    keymap_bind(&line->keymap, "C-f", forward_char,           "Move forward one character");
    keymap_bind(&line->keymap, "C-d", delete_char,            "Delete character at point");
    keymap_bind(&line->keymap, "DEL", delete_backward_char,   "Delete backward character");
    keymap_bind(&line->keymap, "C-h", delete_backward_char,   "Delete backward character");
    keymap_bind(&line->keymap, "C-u", kill_region,            "Kill line before point");
    keymap_bind(&line->keymap, "C-@", set_mark,               "Set mark at point"); // C-space
    keymap_bind(&line->keymap, "C-w", kill_region,            "Kill region between mark and point");
    keymap_bind(&line->keymap, "M-w", kill_region,            "Kill region between mark and point");
    keymap_print_bindings(&line->keymap);
}

void line_free(Line *line) {
    free(line->buffer);
    line->buffer = NULL;
    line->len = line->point = line->cap = 0;
    keymap_free(&line->keymap);
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

void set_mark(Line *line) {
    line->region.mark = line->point;
    line->region.active = true;
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
void line_refresh(Line *line, const char *prompt) {
    printf(ANSI_CLEAR_LINE ANSI_MOVE_CURSOR_START "%s%s", prompt, line->buffer);
    fflush(stdout);
    // Move cursor back to correct position
    if (line->point < line->len) {
        printf("\033[%zuD", line->len - line->point);
        fflush(stdout);
    }
}

bool line_read(Line *line, const char *prompt) {
    tcgetattr(STDIN_FILENO, &original_term);
    enable_raw_mode();

    clear_line(line);
    printf("%s", prompt);
    fflush(stdout);

    KeySequence seq;
    char c;
    while (read(STDIN_FILENO, &c, 1) == 1) {
        // Handle escape sequences and special keys
        if (c == 27) { // ESC - could be start of escape sequence or Meta key
            char esc_seq[8] = {27};
            size_t esc_len = 1;
            
            // Set a small timeout for reading the rest of the sequence
            struct timeval tv = {0, 10000}; // 10ms timeout
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(STDIN_FILENO, &fds);
            
            while (select(1, &fds, NULL, NULL, &tv) > 0 && esc_len < sizeof(esc_seq)) {
                if (read(STDIN_FILENO, &esc_seq[esc_len], 1) == 1) {
                    esc_len++;
                }
            }
            
            // Special case for DEL key which might be sent as \e[3~
            if (esc_len == 3 && esc_seq[1] == '[' && esc_seq[2] == '3' &&
                read(STDIN_FILENO, &esc_seq[3], 1) == 1 && esc_seq[3] == '~') {
                esc_len = 4;
            }
            
            if (!make_key_sequence(esc_seq, esc_len, &seq)) {
                // If we can't make a sequence, treat it as a Meta key
                seq.sequence[0] = 27;
                seq.sequence[1] = esc_seq[1];
                seq.length = 2;
            }
        }
        else if (c == 127) { // DEL key
            seq.sequence[0] = c;
            seq.length = 1;
        }
        else {
            // Regular key
            if (!make_key_sequence(&c, 1, &seq)) continue;
        }

        // Look up action
        KeyAction action = keymap_lookup(&line->keymap, &seq);

        if (seq.sequence[0] == 4) {  // Ctrl-D (EOF)
            if (line->len == 0) {
                putchar('\n');
                disable_raw_mode();
                return false;
            }
        }
        
        if (action) {
            // Execute the bound action
            action(line);
        } else if (seq.sequence[0] == '\n' || seq.sequence[0] == '\r') {
            // Enter key
            putchar('\n');
            break;
        } else if (isprint(seq.sequence[0])) {  // Printable characters
            insert(line, seq.sequence[0]);
        }
        
        line_refresh(line, prompt);
    }

    disable_raw_mode();
    return true;
}
