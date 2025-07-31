#ifndef KILLRING_H
#define KILLRING_H

typedef struct {
    char **entries; // Array of strings
    int size;       // Number of entries currently in the kill ring
    int capacity;   // Maximum number of entries
    int index;      // Current index for yanking
} KillRing;

void initKillRing(KillRing* kr, int capacity);
void freeKillRing(KillRing* kr);
void copy_to_clipboard(const char* text);
char* paste_from_clipboard();
void kr_kill(KillRing* kr, const char* text);

#endif // KILLRING_H

/* #ifndef KILLRING_H */
/* #define KILLRING_H */

/* typedef struct { */
/*     char **entries; // Array of strings */
/*     int size;       // Number of entries currently in the kill ring */
/*     int capacity;   // Maximum number of entries */
/*     int index;      // Current index for yanking */
/* } KillRing; */

/* void initKillRing(KillRing* kr, int capacity); */
/* void freeKillRing(KillRing* kr); */
/* void copy_to_clipboard(const char* text); */
/* char* paste_from_clipboard(); */
/* void kr_kill(KillRing* kr, const char* text); */


/* #endif // KILLRING_H */
