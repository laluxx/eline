#include "killring.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

void initKillRing(KillRing* kr, int capacity) {
    kr->entries = malloc(sizeof(char*) * capacity);
    kr->size = 0;
    kr->capacity = capacity;
    kr->index = 0;
    for (int i = 0; i < capacity; i++) {
        kr->entries[i] = NULL;
    }
}

void freeKillRing(KillRing* kr) {
    for (int i = 0; i < kr->capacity; i++) {
        free(kr->entries[i]);
    }
    free(kr->entries);
}

void copy_to_clipboard(const char* text) {
    if (!text) return;
    
    pid_t pid = fork();
    if (pid == -1) {
        // Fork failed
        return;
    }
    
    if (pid == 0) {
        // Child process
        int pipefd[2];
        if (pipe(pipefd) == -1) {
            exit(1);
        }
        
        pid_t xclip_pid = fork();
        if (xclip_pid == -1) {
            exit(1);
        }
        
        if (xclip_pid == 0) {
            // xclip process
            close(pipefd[1]); // Close write end
            dup2(pipefd[0], STDIN_FILENO); // Redirect stdin to pipe
            close(pipefd[0]);
            
            execlp("xclip", "xclip", "-selection", "clipboard", NULL);
            exit(1); // If execlp fails
        } else {
            // Writer process
            close(pipefd[0]); // Close read end
            
            size_t len = strlen(text);
            size_t written = 0;
            
            while (written < len) {
                ssize_t result = write(pipefd[1], text + written, len - written);
                if (result == -1) {
                    break;
                }
                written += result;
            }
            
            close(pipefd[1]);
            
            // Wait for xclip to finish
            int status;
            waitpid(xclip_pid, &status, 0);
            exit(0);
        }
    } else {
        // Parent process - wait for child to complete
        int status;
        waitpid(pid, &status, 0);
    }
}

char* paste_from_clipboard() {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        return NULL;
    }
    
    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return NULL;
    }
    
    if (pid == 0) {
        // Child process - run xclip
        close(pipefd[0]); // Close read end
        dup2(pipefd[1], STDOUT_FILENO); // Redirect stdout to pipe
        close(pipefd[1]);
        
        execlp("xclip", "xclip", "-o", "-selection", "clipboard", NULL);
        exit(1); // If execlp fails
    } else {
        // Parent process - read from pipe
        close(pipefd[1]); // Close write end
        
        char* result = NULL;
        size_t total_size = 0;
        size_t buffer_size = 4096;
        char* buffer = malloc(buffer_size);
        
        if (!buffer) {
            close(pipefd[0]);
            int status;
            waitpid(pid, &status, 0);
            return NULL;
        }
        
        ssize_t bytes_read;
        while ((bytes_read = read(pipefd[0], buffer, buffer_size)) > 0) {
            char* new_result = realloc(result, total_size + bytes_read + 1);
            if (!new_result) {
                free(result);
                free(buffer);
                close(pipefd[0]);
                int status;
                waitpid(pid, &status, 0);
                return NULL;
            }
            result = new_result;
            memcpy(result + total_size, buffer, bytes_read);
            total_size += bytes_read;
        }
        
        close(pipefd[0]);
        free(buffer);
        
        // Wait for child process to complete
        int status;
        waitpid(pid, &status, 0);
        
        if (result) {
            result[total_size] = '\0';  // Null terminate the string
        }
        
        return result;
    }
}

void kr_kill(KillRing* kr, const char* text) {
    if (kr->size >= kr->capacity) {
        // Free the oldest entry if the ring is full
        free(kr->entries[kr->index]);
    } else {
        kr->size++;
    }

    kr->entries[kr->index] = strdup(text);
    kr->index = (kr->index + 1) % kr->capacity;

    // Also copy the text to the system clipboard
    copy_to_clipboard(text);
}
