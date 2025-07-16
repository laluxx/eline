#ifndef KEYMAP_H
#define KEYMAP_H

#include <stddef.h>
#include <stdbool.h>

typedef struct Line Line; // Forward declaration

typedef struct {
    char sequence[8];  // Raw escape sequence (e.g., "\001" for C-a)
    size_t length;     // Length of the sequence
} KeySequence;

typedef void (*KeyAction)(Line *line);

typedef struct {
    KeySequence key;
    KeyAction action;
    char *description;
    char *notation;    // Store the original notation like "C-a"
} KeyBinding;

typedef struct {
    KeyBinding *bindings;
    size_t count;
    size_t capacity;
} KeyMap;


void keymap_init(KeyMap *keymap);
void keymap_free(KeyMap *keymap);

bool parse_key_notation(const char *notation, KeySequence *seq);
bool keymap_bind(KeyMap *keymap, const char *notation, KeyAction action, const char *description);
bool keymap_unbind(KeyMap *keymap, const char *notation);
KeyAction keymap_lookup(KeyMap *keymap, const KeySequence *seq); // Find action for a key sequence
// Find binding by notation (for debugging/introspection)
KeyBinding *keymap_find_binding(KeyMap *keymap, const char *notation);
void keymap_print_bindings(KeyMap *keymap);  // Print all bindings (for debugging)

// Helper function to convert raw input to KeySequence
bool make_key_sequence(const char *raw_input, size_t input_len, KeySequence *seq);
bool key_sequence_equal(const KeySequence *a, const KeySequence *b);

#endif // KEYMAP_H
