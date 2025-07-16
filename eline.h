#ifndef ELINE_H
#define ELINE_H

#include <stddef.h>
#include <stdbool.h>

typedef struct {
    size_t mark;
    size_t end;
    bool active;
} Region;

typedef struct {
    char *buffer;
    size_t len;
    size_t point;
    size_t cap;
    Region region;
} Line;

void line_init(Line *line);
void line_free(Line *line);
void insert(Line *line, char c);
void delete_backward_char(Line *line);
void delete_char(Line *line);
void backward_char(Line *line);
void forward_char(Line *line);
void move_beginning_of_line(Line *line);
void move_end_of_line(Line *line);
void kill_region(Line *line);
void clear_line(Line *line);
bool line_read(Line *line, const char *prompt);

#endif // ELINE_H
