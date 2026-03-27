#ifndef DOCUMENT_H
#define DOCUMENT_H

#include <stdbool.h>
#include <stdio.h>
#include <time.h>

typedef struct chunk chunk;

struct chunk {
    char* buffer; //actual content of chunk
    size_t length;
    bool is_deleted; //to mark as deleted before committing
    bool is_inserted; //to mark as new chunk inserted before committing
    int8_t ordered_list; //ordered list markers: 0 - not list; 1 - ordered number; 2 - ordered item
    bool is_newline; //to mark as exclusively newline chunk
    chunk* next;
};

typedef struct {
    size_t length;
    uint64_t version;
    chunk* chunks;
} document;

//doubly linked list to store all clients with their file descriptors
struct client_pipe {
    FILE* s2c_fd;
    struct client_pipe* next;
    struct client_pipe* prev;
};

struct all_clients {
    int num_clients;
    struct client_pipe* clients;
};

//doubly linked list to store queued commands
struct command_item {
    char* command; //actual command
    char* username; //user who sent the command
    bool authorised; //whether they have write permissions or not
    time_t time; //timestamp when command was sent
    struct command_item* next;
    struct command_item* prev;
};

struct command_queue {
    int num_commands;
    struct command_item* commands;
};

#endif