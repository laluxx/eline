#include "keymap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define KEYMAP_INIT_CAP 32

void keymap_init(KeyMap *keymap) {
    keymap->bindings = malloc(KEYMAP_INIT_CAP * sizeof(KeyBinding));
    keymap->count = 0;
    keymap->capacity = KEYMAP_INIT_CAP;
}

void keymap_free(KeyMap *keymap) {
    for (size_t i = 0; i < keymap->count; i++) {
        free(keymap->bindings[i].description);
        free(keymap->bindings[i].notation);
    }
    free(keymap->bindings);
    keymap->bindings = NULL;
    keymap->count = keymap->capacity = 0;
}


// TODO C-M-P doesn't work
// TODO Keychords
bool parse_key_notation(const char *notation, KeySequence *seq) {
    if (!notation || !seq) return false;
    
    memset(seq, 0, sizeof(KeySequence));
    size_t len = strlen(notation);
    
    if (len == 0) return false;
    
    // Handle single character keys
    if (len == 1) {
        seq->sequence[0] = notation[0];
        seq->length = 1;
        return true;
    }
    
    // Handle special key names first
    if (strcmp(notation, "DEL") == 0 || strcmp(notation, "BS") == 0) {
        // Support both traditional DEL (127) and modern terminal sequence
        seq->sequence[0] = 127;
        seq->length = 1;
        return true;
    }
    
    if (strcmp(notation, "TAB") == 0) {
        seq->sequence[0] = '\t';
        seq->length = 1;
        return true;
    }
    
    if (strcmp(notation, "RET") == 0 || strcmp(notation, "ENTER") == 0) {
        seq->sequence[0] = '\r';
        seq->length = 1;
        return true;
    }
    
    if (strcmp(notation, "SPC") == 0) {
        seq->sequence[0] = ' ';
        seq->length = 1;
        return true;
    }
    
    // Handle arrow keys
    if (strcmp(notation, "UP") == 0) {
        const char *seq_str = "\033[A";
        memcpy(seq->sequence, seq_str, 3);
        seq->length = 3;
        return true;
    }
    
    if (strcmp(notation, "DOWN") == 0) {
        const char *seq_str = "\033[B";
        memcpy(seq->sequence, seq_str, 3);
        seq->length = 3;
        return true;
    }
    
    if (strcmp(notation, "RIGHT") == 0) {
        const char *seq_str = "\033[C";
        memcpy(seq->sequence, seq_str, 3);
        seq->length = 3;
        return true;
    }
    
    if (strcmp(notation, "LEFT") == 0) {
        const char *seq_str = "\033[D";
        memcpy(seq->sequence, seq_str, 3);
        seq->length = 3;
        return true;
    }
    
    // Handle modifier combinations (C-, M-, C-M-)
    bool has_ctrl = false;
    bool has_meta = false;
    const char *key_part = notation;
    
    // Check for C- prefix
    if (len >= 3 && notation[0] == 'C' && notation[1] == '-') {
        has_ctrl = true;
        key_part += 2;
        len -= 2;
    }
    
    // Check for M- prefix (might be after C-)
    if (len >= 3 && key_part[0] == 'M' && key_part[1] == '-') {
        has_meta = true;
        key_part += 2;
        len -= 2;
    }
    
    // At this point, key_part should point to the base key
    if (len == 0) return false;
    
    // Handle the base key
    char base_key = key_part[0];
    
    // Build the sequence
    size_t pos = 0;
    
    // Meta keys start with ESC
    if (has_meta) {
        seq->sequence[pos++] = 27; // ESC
    }
    
    // Control keys are modified
    if (has_ctrl) {
        if (isalpha(base_key)) {
            seq->sequence[pos++] = tolower(base_key) - 'a' + 1;
        } else {
            // Handle special control keys
            switch (base_key) {
                case '@': seq->sequence[pos++] = 0; break;    // C-@
                case '?': seq->sequence[pos++] = 127; break; // C-?
                case '-': seq->sequence[pos++] = 31; break;  // C--
                case '[': seq->sequence[pos++] = 27; break;  // C-[
                case '\\': seq->sequence[pos++] = 28; break; // C-\
                case ']': seq->sequence[pos++] = 29; break;  // C-]
                case '^': seq->sequence[pos++] = 30; break;  // C-^
                case '_': seq->sequence[pos++] = 31; break;  // C-_
                default: return false; // Unsupported control combination
            }
        }
    } else {
        // No control, just the key (possibly with meta)
        seq->sequence[pos++] = base_key;
    }
    
    seq->length = pos;
    return true;
}

bool keymap_bind(KeyMap *keymap, const char *notation, KeyAction action, const char *description) {
    if (!keymap || !notation || !action) return false;
    
    KeySequence seq;
    if (!parse_key_notation(notation, &seq)) return false;
    
    // Check if binding already exists and update it
    for (size_t i = 0; i < keymap->count; i++) {
        if (key_sequence_equal(&keymap->bindings[i].key, &seq)) {
            keymap->bindings[i].action = action;
            free(keymap->bindings[i].description);
            keymap->bindings[i].description = description ? strdup(description) : NULL;
            return true;
        }
    }
    
    // Add new binding
    if (keymap->count >= keymap->capacity) {
        keymap->capacity *= 2;
        keymap->bindings = realloc(keymap->bindings, keymap->capacity * sizeof(KeyBinding));
    }
    
    KeyBinding *binding = &keymap->bindings[keymap->count];
    binding->key = seq;
    binding->action = action;
    binding->description = description ? strdup(description) : NULL;
    binding->notation = strdup(notation);
    
    keymap->count++;
    return true;
}

bool keymap_unbind(KeyMap *keymap, const char *notation) {
    if (!keymap || !notation) return false;
    
    KeySequence seq;
    if (!parse_key_notation(notation, &seq)) return false;
    
    for (size_t i = 0; i < keymap->count; i++) {
        if (key_sequence_equal(&keymap->bindings[i].key, &seq)) {
            free(keymap->bindings[i].description);
            free(keymap->bindings[i].notation);
            
            // Move last binding to this position
            if (i < keymap->count - 1) {
                keymap->bindings[i] = keymap->bindings[keymap->count - 1];
            }
            keymap->count--;
            return true;
        }
    }
    
    return false;
}

KeyAction keymap_lookup(KeyMap *keymap, const KeySequence *seq) {
    if (!keymap || !seq) return NULL;
    
    for (size_t i = 0; i < keymap->count; i++) {
        if (key_sequence_equal(&keymap->bindings[i].key, seq)) {
            return keymap->bindings[i].action;
        }
    }
    
    return NULL;
}

KeyBinding *keymap_find_binding(KeyMap *keymap, const char *notation) {
    if (!keymap || !notation) return NULL;
    
    for (size_t i = 0; i < keymap->count; i++) {
        if (keymap->bindings[i].notation && 
            strcmp(keymap->bindings[i].notation, notation) == 0) {
            return &keymap->bindings[i];
        }
    }
    
    return NULL;
}

void keymap_print_bindings(KeyMap *keymap) {
    if (!keymap) return;
    
    printf("Key Bindings:\n");
    printf("=============\n");
    
    for (size_t i = 0; i < keymap->count; i++) {
        KeyBinding *binding = &keymap->bindings[i];
        printf("%-10s", binding->notation ? binding->notation : "Unknown");
        
        // Print raw sequence for debugging
        printf(" [");
        for (size_t j = 0; j < binding->key.length; j++) {
            if (j > 0) printf(" ");
            printf("%3d", (unsigned char)binding->key.sequence[j]);
        }
        printf("]");
        
        if (binding->description) {
            printf(" - %s", binding->description);
        }
        putchar('\n');
    }
}

bool make_key_sequence(const char *raw_input, size_t input_len, KeySequence *seq) {
    if (!raw_input || !seq || input_len == 0 || input_len > 7) return false;
    
    memset(seq, 0, sizeof(KeySequence));
    memcpy(seq->sequence, raw_input, input_len);
    seq->length = input_len;
    
    return true;
}

bool key_sequence_equal(const KeySequence *a, const KeySequence *b) {
    if (!a || !b) return false;
    
    if (a->length != b->length) return false;
    
    return memcmp(a->sequence, b->sequence, a->length) == 0;
}
