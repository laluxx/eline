#include "eline.h"
#include "keymap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/ioctl.h>

// TODO delete_backward_char doubles the line

#define ELINES_INIT_CAP 128

#define ANSI_CLEAR_LINE        "\033[2K"
#define ANSI_MOVE_CURSOR_START "\033[G"
#define ANSI_CURSOR_FORWARD    "\033[1C"
#define ANSI_CURSOR_BACKWARD   "\033[1D"
#define ANSI_SAVE_CURSOR       "\033[s"
#define ANSI_RESTORE_CURSOR    "\033[u"

#define MAX_ARG_DIGITS 6
#define MAX_ARG_VALUE 999999

bool mark_word_navigation = true;
bool show_digit_argument  = true;
bool mark_yank = true;
bool electric_pair_mode  = true;
bool electric_pair_mode_brackets = true;
bool show_last_key = true; // TODO

static struct termios original_term;

static int get_terminal_width() {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        return w.ws_col;
    }
    return 80; // fallback
}

// Calculate how many lines the current content takes
static int calculate_lines_used(size_t prompt_len, size_t buffer_len) {
    int term_width = get_terminal_width();
    int total_chars = prompt_len + buffer_len;
    return (total_chars + term_width - 1) / term_width; // ceiling division
}

// Calculate cursor position in terminal coordinates
static void calculate_cursor_position(const char *prompt, const char *buffer, size_t point, 
                                    int *row, int *col) {
    int term_width = get_terminal_width();
    size_t prompt_len = strlen(prompt);
    size_t total_pos = prompt_len + point;
    
    *row = total_pos / term_width;
    *col = total_pos % term_width;
}

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
    line->arg = 1;
    memset(&line->last_key, 0, sizeof(KeySequence));

    initKillRing(&line->kr, 5000);
    keymap_init(&line->keymap);
    // Set up default key bindings
    keymap_bind(&line->keymap,	"C-a",	move_beginning_of_line, "Move to beginning of line");
    keymap_bind(&line->keymap,	"C-e",	move_end_of_line,		"Move to end of line");
    keymap_bind(&line->keymap,	"C-b",	backward_char,		    "Move backward one character");
    keymap_bind(&line->keymap,	"C-f",	forward_char,		    "Move forward one character");
    keymap_bind(&line->keymap,	"C-d",	delete_char,		    "Delete character at point");
    keymap_bind(&line->keymap,	"DEL",	delete_backward_char,   "Delete backward character");
    keymap_bind(&line->keymap,	"C-h",	delete_backward_char,   "Delete backward character");
    keymap_bind(&line->keymap,	"C-u",	kill_region,		    "Kill line before point");
    keymap_bind(&line->keymap,	"C-@",	set_mark,			    "Set mark at point"); // C-SPC
    keymap_bind(&line->keymap,	"C-w",	kill_region,			"Kill region between mark and point");
    keymap_bind(&line->keymap,	"M-w",	kill_region,			"Kill region between mark and point");
    keymap_bind(&line->keymap,	"M-f",	forward_word,			"Move point forward ARG words (backward if ARG is negative).");
    keymap_bind(&line->keymap,	"M-b",	backward_word,			"Move backward until encountering the beginning of a word.");
    keymap_bind(&line->keymap,	"C-o",	open_line,			"Insert a newline and leave point before it.");
    keymap_bind(&line->keymap,	"C-g",	keyboard_quit,		"Cancel current operation and reset argument");
    keymap_bind(&line->keymap,	"M-d",	kill_word,		    "Kill characters forward until encountering the end of a word.");
    keymap_bind(&line->keymap,	"C-y",	yank,		        "Reinsert the last stretch of killed text.");
    keymap_bind(&line->keymap,	"C-k",	kill_line,		    "Kill the rest of the current line; if no nonblanks there, kill thru newline.");

    keymap_bind(&line->keymap, "M-0", digit_argument, "Digit argument 0");
    keymap_bind(&line->keymap, "M-1", digit_argument, "Digit argument 1");
    keymap_bind(&line->keymap, "M-2", digit_argument, "Digit argument 2");
    keymap_bind(&line->keymap, "M-3", digit_argument, "Digit argument 3");
    keymap_bind(&line->keymap, "M-4", digit_argument, "Digit argument 4");
    keymap_bind(&line->keymap, "M-5", digit_argument, "Digit argument 5");
    keymap_bind(&line->keymap, "M-6", digit_argument, "Digit argument 6");
    keymap_bind(&line->keymap, "M-7", digit_argument, "Digit argument 7");
    keymap_bind(&line->keymap, "M-8", digit_argument, "Digit argument 8");
    keymap_bind(&line->keymap, "M-9", digit_argument, "Digit argument 9");

    keymap_print_bindings(&line->keymap);
}

void line_free(Line *line) {
    free(line->buffer);
    line->buffer = NULL;
    line->len = line->point = line->cap = 0;
    keymap_free(&line->keymap);
}

// Helper function to get the closing pair for a character
char get_closing_pair(char c) {
    switch (c) {
        case '(': return ')';
        case '[': return ']';
        case '{': return '}';
        case '<': return electric_pair_mode_brackets ? '>' : '\0';
        default: return '\0';
    }
}


bool should_insert_pair() {
    if (!electric_pair_mode) return false;
    // TODO Check for unbalanced pairs and return false
    return true;
}

void insert(Line *line, char c) {
    if (line->len + 1 >= line->cap) {
        line->cap *= 2;
        line->buffer = realloc(line->buffer, line->cap);
    }
    
    // Check if we should insert a pair
    char closing_char = '\0';
    if (should_insert_pair()) {
        closing_char = get_closing_pair(c);
        
        // Make sure we have enough space for both characters
        if (line->len + 2 >= line->cap) {
            line->cap *= 2;
            line->buffer = realloc(line->buffer, line->cap);
        }
    }
    
    // Insert the opening character
    memmove(line->buffer + line->point + 1, line->buffer + line->point, line->len - line->point);
    line->buffer[line->point] = c;
    line->len++;
    line->point++;
    
    // Insert the closing character if needed
    if (closing_char != '\0') {
        memmove(line->buffer + line->point + 1, line->buffer + line->point, line->len - line->point);
        line->buffer[line->point] = closing_char;
        line->len++;
        // Don't advance point - leave cursor between the pair
    }
    
    line->buffer[line->len] = '\0';
}

bool should_delete_pair(Line *line) {
    if (!electric_pair_mode || line->point == 0 || line->point >= line->len) {
        return false;
    }
    
    char prev_char = line->buffer[line->point - 1];
    char next_char = line->buffer[line->point];
    
    // Check if we have a matching pair
    switch (prev_char) {
        case '(':
            return next_char == ')';
        case '[':
            return next_char == ']';
        case '{':
            return next_char == '}';
        case '<':
            return electric_pair_mode_brackets && next_char == '>';
        default:
            return false;
    }
}

void delete_backward_char(Line *line) {
    if (line->point == 0) return;
    
    bool delete_pair = should_delete_pair(line);
    
    if (delete_pair) {
        // Delete both characters (opening and closing)
        memmove(line->buffer + line->point - 1, line->buffer + line->point + 1, line->len - line->point - 1);
        line->len -= 2;
        line->point--;
    } else {
        // Delete just the previous character
        memmove(line->buffer + line->point - 1, line->buffer + line->point, line->len - line->point);
        line->len--;
        line->point--;
    }
    
    line->buffer[line->len] = '\0';
}

void delete_char(Line *line) {
    if (line->point == line->len) return;
    memmove(line->buffer + line->point, line->buffer + line->point + 1, line->len - line->point - 1);
    line->len--;
    line->buffer[line->len] = '\0';
}

void backward_char(Line *line) {
    for (int i = 0; i < line->arg; i++) {
        if (line->point > 0) line->point--;
    }
}

void forward_char(Line *line) {
    for (int i = 0; i < line->arg; i++) {
        if (line->point < line->len) line->point++;
    }
}

bool isWordChar(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

bool isPunctuationChar(char c) {
    return strchr(",.;:!?'\"(){}[]<>-+*/&|^%$#@~", c) != NULL;
}

/* Move to the beginning of the current word */
size_t beginning_of_word(Line *line, size_t pos) {
    if (line == NULL || line->buffer == NULL || pos == 0)
        return pos;

    // If already at start of word, move to previous word
    if (pos > 0 && isWordChar(line->buffer[pos - 1])) {
        // Move backward over word chars
        while (pos > 0 && isWordChar(line->buffer[pos - 1]))
            pos--;
    } else {
        // Move backward over non-word chars
        while (pos > 0 && !isWordChar(line->buffer[pos - 1]))
            pos--;
        // Then move to word start
        while (pos > 0 && isWordChar(line->buffer[pos - 1]))
            pos--;
    }

    return pos;
}

/* Move to the end of the current word */
size_t end_of_word(Line *line, size_t pos) {
    if (line == NULL || line->buffer == NULL)
        return pos;

    size_t end = line->len; // Fixed: was using line->cap

    // If already in word, move to end of current word
    if (pos < end && isWordChar(line->buffer[pos])) {
        // Move forward over word chars
        while (pos < end && isWordChar(line->buffer[pos]))
            pos++;
    } else {
        // Move forward over non-word chars
        while (pos < end && !isWordChar(line->buffer[pos]))
            pos++;
        // Then move to word end
        while (pos < end && isWordChar(line->buffer[pos]))
            pos++;
    }

    return pos;
}

void forward_word(Line *line) {
    int arg = line->arg;
    /* size_t original_pos = line->point; */
    int direction = arg > 0 ? 1 : -1;
    arg = abs(arg);

    // Precise word marking behavior
    if (mark_word_navigation) {
        if (direction > 0) {
            // Forward word marking:
            // 1. Find where we'll end up
            size_t new_pos = line->point;
            for (int i = 0; i < arg; i++) {
                new_pos = end_of_word(line, new_pos);
            }
            
            // 2. Mark from original position to start of destination word
            line->region.mark = beginning_of_word(line, new_pos);
            line->point = new_pos;
        } else {
            // Backward word marking:
            // 1. Find where we'll end up
            size_t new_pos = line->point;
            for (int i = 0; i < arg; i++) {
                new_pos = beginning_of_word(line, new_pos);
            }
            
            // 2. Mark from original position to end of destination word
            line->region.mark = end_of_word(line, new_pos);
            line->point = new_pos;
        }
    } else {
        // Simple movement without marking
        while (arg-- > 0) {
            if (direction > 0) {
                line->point = end_of_word(line, line->point);
            } else {
                line->point = beginning_of_word(line, line->point);
            }
        }
    }
}

void backward_word(Line *line) {
    line->arg = -line->arg;
    forward_word(line);
    line->arg = abs(line->arg);
}

void kill_line(Line *line) {
    if (line->point >= line->len) {
        // If at end of line, do nothing (or could kill newline in multi-line context)
        return;
    }
    
    // Calculate how much text to kill (from point to end of line)
    size_t kill_length = line->len - line->point;
    
    if (kill_length > 0) {
        // Copy the text that will be killed to kill ring
        char* killed_text = malloc(kill_length + 1);
        if (killed_text) {
            memcpy(killed_text, line->buffer + line->point, kill_length);
            killed_text[kill_length] = '\0';
            kr_kill(&line->kr, killed_text);
            free(killed_text);
        }
        
        // Kill the line by truncating at point
        line->buffer[line->point] = '\0';
        line->len = line->point;
    }
}




void kill_word(Line *line) {
    size_t start = line->point;
    size_t end = start;
    
    // Skip non-word characters at the current position
    while (end < line->len && !isWordChar(line->buffer[end])) {
        end++;
    }
    
    // Move forward until a non-word character is encountered, marking the end of the word
    while (end < line->len && isWordChar(line->buffer[end])) {
        end++;
    }
    
    size_t lengthToDelete = end - start;
    if (lengthToDelete == 0) return; // No word to delete if length is 0
    
    // Copy the word that will be killed
    char* killed_text = malloc(lengthToDelete + 1);
    if (killed_text) {
        memcpy(killed_text, line->buffer + start, lengthToDelete);
        killed_text[lengthToDelete] = '\0';
        kr_kill(&line->kr, killed_text);
        free(killed_text);
    }
    
    // Remove the word from the buffer
    memmove(line->buffer + start, line->buffer + end, line->len - end);
    line->len -= lengthToDelete;
    line->buffer[line->len] = '\0';
}

// TODO Option to use ARG to yank N lines before or after point
// Useful for relative lines users
void yank(Line *line) {
    char *clipboard_text = paste_from_clipboard();
    if (!clipboard_text) return;

    // Determine the actual text length, ignoring a trailing newline if present
    size_t len = strlen(clipboard_text);
    if (len > 0 && clipboard_text[len - 1] == '\n') {
        len--;  
    }

    size_t original_point = line->point;

    // Insert the text character by character, repeating if arg > 1
    for (int count = 0; count < line->arg; count++) {
        for (size_t i = 0; i < len; i++) {
            insert(line, clipboard_text[i]);
        }
    }

    if (mark_yank) line->region.mark = original_point;

    free(clipboard_text);
}


void open_line(Line *line) {
    insert(line, '\n');
    backward_char(line);
}

void keyboard_quit(Line *line) {
    line->arg = 1;
    // Force a full refresh to remove any argument display
    line_refresh(line, line->prompt);
}

void digit_argument(Line *line) {
    char c;
    
    // Extract the digit from the key sequence
    if (line->last_key.length == 2 && line->last_key.sequence[0] == 27) {
        // Meta+digit
        c = line->last_key.sequence[1];
    } else {
        c = line->last_key.sequence[0];
    }
    
    if (isdigit(c)) {
        int digit = c - '0';
        
        // Check if adding this digit would exceed MAX_ARG_DIGITS
        int temp_arg = line->arg;
        int digit_count = 0;
        
        // Count current digits
        if (temp_arg == 1) {
            digit_count = 0; // Special case: starting fresh
        } else {
            while (temp_arg > 0) {
                temp_arg /= 10;
                digit_count++;
            }
        }
        
        // If we would exceed MAX_ARG_DIGITS, reset to 1
        if (digit_count >= MAX_ARG_DIGITS) {
            line->arg = 1;
            return; // Don't show digit argument anymore
        }
        
        if (line->arg == 1) {
            line->arg = digit;
        } else {
            int new_arg = line->arg * 10 + digit;
            if (new_arg > MAX_ARG_VALUE) {
                line->arg = 1; // Reset on overflow
                return;
            }
            line->arg = new_arg;
        }
    }
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
    if (line->point == line->region.mark) return;
    
    size_t start = line->region.mark < line->point ? line->region.mark : line->point;
    size_t end   = line->region.mark > line->point ? line->region.mark : line->point;
    
    // Validate bounds before proceeding
    if (start > line->len || end > line->len) {
        // Invalid region, reset and return
        line->region.active = false;
        line->region.mark = line->point;
        return;
    }
    
    // Copy the killed text to kill ring
    size_t kill_length = end - start;
    if (kill_length > 0) {
        char* killed_text = malloc(kill_length + 1);
        if (killed_text) {
            memcpy(killed_text, line->buffer + start, kill_length);
            killed_text[kill_length] = '\0';
            kr_kill(&line->kr, killed_text);
            free(killed_text);
        }
    }
    
    // Remove the region from buffer
    memmove(line->buffer + start, line->buffer + end, line->len - end);
    line->len -= (end - start);
    line->point = start;
    line->buffer[line->len] = '\0';
    
    // Update mark to a valid position and deactivate region
    line->region.mark = line->point;
    line->region.active = false;
}


void clear_line(Line *line) {
    line->buffer[0] = '\0';
    line->len = 0;
    line->point = 0;
    line->region.active = false;
}



// Handles multi-line display and wrapping
void line_refresh(Line *line, const char *prompt) {
    int term_width = get_terminal_width();
    size_t prompt_len = strlen(prompt);
    
    // Calculate how many lines we need to clear
    static int last_lines_used = 1;
    
    // Move cursor to beginning of first line and clear all lines we used before
    printf("\r");
    for (int i = 0; i < last_lines_used; i++) {
        printf(ANSI_CLEAR_LINE);
        if (i < last_lines_used - 1) {
            printf("\033[1B"); // move down one line
        }
    }
    
    // Move back to first line
    if (last_lines_used > 1) {
        printf("\033[%dA", last_lines_used - 1);
    }
    printf("\r");
    
    // Print prompt and buffer
    printf("%s%s", prompt, line->buffer);
    
    // Calculate new lines used
    int current_lines_used = calculate_lines_used(prompt_len, line->len);
    last_lines_used = current_lines_used;
    
    // After printing the content, we're at the end of all content
    // Calculate where we actually are now
    size_t end_pos = prompt_len + line->len;
    int actual_row = (end_pos > 0) ? (end_pos - 1) / term_width : 0;
    int actual_col = (end_pos > 0) ? (end_pos - 1) % term_width + 1 : 0;
    
    // Handle the case where we're exactly at the end of a line
    if (end_pos > 0 && end_pos % term_width == 0) {
        actual_row--;
        actual_col = term_width;
    }
    
    // Calculate where cursor should be
    int cursor_row, cursor_col;
    calculate_cursor_position(prompt, line->buffer, line->point, &cursor_row, &cursor_col);
    
    // Move from where we are to where we need to be
    int row_diff = actual_row - cursor_row;
    int col_diff = actual_col - cursor_col;
    
    // Move vertically first
    if (row_diff > 0) {
        printf("\033[%dA", row_diff); // Move up
    } else if (row_diff < 0) {
        printf("\033[%dB", -row_diff); // Move down
    }
    
    // Move horizontally
    if (col_diff > 0) {
        printf("\033[%dD", col_diff); // Move left
    } else if (col_diff < 0) {
        printf("\033[%dC", -col_diff); // Move right
    }
    
    fflush(stdout);
}

// Special refresh for showing digit arguments that preserves cursor position
void line_refresh_with_arg(Line *line, const char *prompt, int arg, bool negative) {
    // We need to do a full refresh since the argument display changes the layout
    int term_width = get_terminal_width();
    size_t prompt_len = strlen(prompt);
    
    // Calculate how many lines we need to clear
    static int last_lines_used_arg = 1;
    
    // Move cursor to beginning of first line and clear all lines we used before
    printf("\r");
    for (int i = 0; i < last_lines_used_arg; i++) {
        printf(ANSI_CLEAR_LINE);
        if (i < last_lines_used_arg - 1) {
            printf("\033[1B"); // move down one line
        }
    }
    
    // Move back to first line
    if (last_lines_used_arg > 1) {
        printf("\033[%dA", last_lines_used_arg - 1);
    }
    printf("\r");
    
    // Create the argument display string
    char arg_display[32];
    if (negative && arg == 0) {
        snprintf(arg_display, sizeof(arg_display), "(arg: -) ");
    } else {
        snprintf(arg_display, sizeof(arg_display), "(arg: %s%d) ", negative ? "-" : "", arg);
    }
    
    // Print prompt with argument display and buffer
    printf("%s%s%s", prompt, arg_display, line->buffer);
    
    // Calculate new lines used with argument display
    size_t total_len = prompt_len + strlen(arg_display) + line->len;
    int current_lines_used = (total_len + term_width - 1) / term_width;
    if (current_lines_used == 0) current_lines_used = 1;
    last_lines_used_arg = current_lines_used;
    
    // Calculate where cursor should be (accounting for argument display)
    size_t cursor_total_pos = prompt_len + strlen(arg_display) + line->point;
    int cursor_row = cursor_total_pos / term_width;
    int cursor_col = cursor_total_pos % term_width;
    
    // After printing the content, we're at the end of all content
    size_t end_pos = total_len;
    int actual_row = (end_pos > 0) ? (end_pos - 1) / term_width : 0;
    int actual_col = (end_pos > 0) ? (end_pos - 1) % term_width + 1 : 0;
    
    // Handle the case where we're exactly at the end of a line
    if (end_pos > 0 && end_pos % term_width == 0) {
        actual_row--;
        actual_col = term_width;
    }
    
    // Move from where we are to where we need to be
    int row_diff = actual_row - cursor_row;
    int col_diff = actual_col - cursor_col;
    
    // Move vertically first
    if (row_diff > 0) {
        printf("\033[%dA", row_diff); // Move up
    } else if (row_diff < 0) {
        printf("\033[%dB", -row_diff); // Move down
    }
    
    // Move horizontally
    if (col_diff > 0) {
        printf("\033[%dD", col_diff); // Move left
    } else if (col_diff < 0) {
        printf("\033[%dC", -col_diff); // Move right
    }
    
    fflush(stdout);
}

bool line_read(Line *line, const char *prompt) {
    line->prompt = prompt;
    tcgetattr(STDIN_FILENO, &original_term);
    enable_raw_mode();

    clear_line(line);
    line->arg = 1; // Reset argument for each new line
    memset(&line->last_key, 0, sizeof(KeySequence));

    printf("%s", prompt);
    fflush(stdout);

    KeySequence seq;
    char c;
    bool building_arg = false;
    bool negative_arg = false;

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

            // Check for Meta+digit (numeric argument)
            if (esc_len == 2 && isdigit(esc_seq[1])) {
                if (!building_arg) {
                    building_arg = true;
                    line->arg = 0;
                }
                
                // Count current digits to check for overflow
                int temp_arg = line->arg;
                int digit_count = 0;
                if (temp_arg > 0) {
                    while (temp_arg > 0) {
                        temp_arg /= 10;
                        digit_count++;
                    }
                }
                
                if (digit_count >= MAX_ARG_DIGITS) {
                    // Reset and stop showing digit argument
                    line->arg = 1;
                    building_arg = false;
                    negative_arg = false;
                    // Force full refresh to clear any wrapped argument display
                    line_refresh(line, prompt);
                    continue;
                }
                
                int digit = esc_seq[1] - '0';
                line->arg = line->arg * 10 + digit;
                if (negative_arg) {
                    line->arg = -line->arg;
                    negative_arg = false; // Apply negative only once
                }
                
                make_key_sequence(esc_seq, esc_len, &seq);
                line->last_key = seq;
                
                if (show_digit_argument) {
                    line_refresh_with_arg(line, prompt, abs(line->arg), line->arg < 0);
                }
                continue;
            }
            
            // Check for Meta+- (negative argument)
            if (esc_len == 2 && esc_seq[1] == '-') {
                negative_arg = true;
                building_arg = true;
                line->arg = 0;
                
                make_key_sequence(esc_seq, esc_len, &seq);
                line->last_key = seq;
                
                if (show_digit_argument) {
                    line_refresh_with_arg(line, prompt, 0, true);
                }
                continue;
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

        // Store the last key
        line->last_key = seq;

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
            
            // Reset argument after command execution unless it's a digit argument
            if (action != digit_argument) {
                building_arg = false;
                negative_arg = false;
                line->arg = 1;
            }
            
            // Special handling for keyboard_quit to reset building_arg state
            if (action == keyboard_quit) {
                building_arg = false;
                negative_arg = false;
            }
        } else if (seq.sequence[0] == '\n' || seq.sequence[0] == '\r') {
            // Enter key
            putchar('\n');
            break;
        } else if (isprint(seq.sequence[0])) {  // Printable characters
            insert(line, seq.sequence[0]);
            building_arg = false;
            negative_arg = false;
            line->arg = 1;
        }
        
        // Refresh the line
        if (building_arg && show_digit_argument) {
            line_refresh_with_arg(line, prompt, abs(line->arg), line->arg < 0);
        } else {
            line_refresh(line, prompt);
        }
    }

    disable_raw_mode();
    return true;
}

