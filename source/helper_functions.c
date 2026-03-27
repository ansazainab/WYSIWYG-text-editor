#include "../libs/helper_functions.h"
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define SUCCESS 0

//splits a chunk into 2 chunks at offset with the right half as a new chunk and left half's fields updated
chunk* split_chunk_after(chunk* ch, size_t offset) {
    chunk* right = malloc(sizeof(chunk));
    if (!right) {
        return NULL;
    }

    size_t len = ch->length - offset;
    right->buffer = malloc(sizeof(char) * len);
    if (!right->buffer) {
        free(right);
        return NULL;
    }

    memcpy(right->buffer, ch->buffer+offset, len);
    right->length = len;
    right->is_deleted = ch->is_deleted;
    right->is_inserted = ch->is_inserted;
    right->is_newline = ch->is_newline;
    right->ordered_list = ch->ordered_list;
    right->next = ch->next;
    
    char* new_buf = realloc(ch->buffer, offset);
    if (!new_buf) {
        free(right->buffer);
        free(right);
        return NULL;
    }

    ch->buffer = new_buf;
    ch->length = offset;
    ch->next = right;
    return right;
}

//splits chunk into 3 chunks at start and end with middle & right parts as new chunks, left part's fields updated
chunk* split_chunk_middle(chunk* ch, size_t start, size_t end) {
    chunk* middle = malloc(sizeof(chunk));
    if (!middle) {
        return NULL;
    }

    size_t len = end - start;
    middle->buffer = malloc(sizeof(char) * len);
    if (!middle->buffer) {
        free(middle);
        return NULL;
    }

    memcpy(middle->buffer, ch->buffer+start, len);
    middle->length = len;
    middle->is_deleted = ch->is_deleted;
    middle->is_inserted = ch->is_inserted;
    middle->is_newline = ch->is_newline;
    middle->ordered_list = ch->ordered_list;
    middle->next = NULL;
    
    chunk* right = NULL;
    right = split_chunk_after(ch, end);
    if (!right) {
        free(middle);
        return NULL;
    }
    middle->next = right;
    
    char* new_buf = realloc(ch->buffer, start);
    if (!new_buf) {
        free(right->buffer);
        free(right);
        free(middle->buffer);
        free(middle);
        return NULL;
    }

    ch->buffer = new_buf;
    ch->length = start;
    ch->next = middle;
    return middle;
}

//checks if target positions fall within a deleted region
bool check_deleted_pos(document* doc, size_t start, size_t end) {
    bool del_start = false;
    bool del_end = false;
    size_t offset = 0;
    
    chunk* dest_chunk = doc->chunks;
    while (dest_chunk) {
        if (dest_chunk->is_inserted) {
            dest_chunk = dest_chunk->next;
            continue;
        }

        if (dest_chunk->is_deleted) {
            size_t chunk_start = offset;
            size_t chunk_end = offset + dest_chunk->length;
            
            if (start >= chunk_start && end <= chunk_end) {
                return true;
            }
            if (start >= chunk_start && start < chunk_end) {
                del_start = true;
            }
            if (end > chunk_start && end < chunk_end) {
                del_end = true;
            }
            if (end == chunk_end && del_start) {
                del_end = true;
            }
        }

        offset += dest_chunk->length;
        dest_chunk = dest_chunk->next;
    }

    if (del_start && del_end) {
        return true;
    }
    return false;
}

//generalised function for inserting block-level elements
int insert_prefix(document* doc, size_t pos, const char* prefix1, const char* prefix2) {
    chunk* new_chunk = malloc(sizeof(chunk));
    new_chunk->is_deleted = false;
    new_chunk->is_inserted = true;
    new_chunk->is_newline = false;
    new_chunk->ordered_list = 0;
    new_chunk->next = NULL;

    size_t len1 = strlen(prefix1); //without pre newline
    size_t len2 = strlen(prefix2); //with pre newline

    size_t offset = 0;
    chunk* dest_chunk = doc->chunks;

    if (!dest_chunk) { //inserting in empty document
        doc->chunks = new_chunk;
        new_chunk->buffer = malloc(len1);
        memcpy(new_chunk->buffer, prefix1, len1);
        new_chunk->length = len1;
        return SUCCESS;
    }

    if (pos == 0) { //inserting at start of document
        new_chunk->next = dest_chunk;
        doc->chunks = new_chunk;
        new_chunk->buffer = malloc(len1);
        memcpy(new_chunk->buffer, prefix1, len1);
        new_chunk->length = len1;
        return SUCCESS;
    }

    //chunk* prev_chunk = NULL;

    while (dest_chunk) { //find position to insert formatting
        while (dest_chunk && (offset + dest_chunk->length < pos)) {
            if (!dest_chunk->is_inserted) {
                offset += dest_chunk->length;
                // if (!dest_chunk->is_deleted) {
                //     prev_chunk = dest_chunk;
                // }
            }

            //prev_chunk = dest_chunk;
            dest_chunk = dest_chunk->next;
        }

        if (dest_chunk && dest_chunk->is_inserted) {
            //prev_chunk = dest_chunk;
            dest_chunk = dest_chunk->next;
            continue;
        }

        break;
    }

    bool exclude_newline = false;
    
    // if (dest_chunk->is_deleted) { //adjust position if target position was deleted
    //     if (prev_chunk) {
    //         new_chunk->next = prev_chunk->next;
    //         prev_chunk->next = new_chunk;
    //         //determine whether to include newline or not
    //         exclude_newline = (prev_chunk->is_newline || prev_chunk->buffer[prev_chunk->length-1] == '\n');
    //     }
    //     else {
    //         new_chunk->next = doc->chunks;
    //         doc->chunks = new_chunk;
    //         exclude_newline = true;
    //     }
    // }
    if (offset + dest_chunk->length == pos) { //insert at boundary between 2 chunks
        new_chunk->next = dest_chunk->next;
        dest_chunk->next = new_chunk;
        exclude_newline = (dest_chunk->is_newline || dest_chunk->buffer[dest_chunk->length-1] == '\n');
    }
    else { //insert in middle of existing chunk
        size_t local_offset = pos - offset;
        split_chunk_after(dest_chunk, local_offset);
        new_chunk->next = dest_chunk->next;
        dest_chunk->next = new_chunk;
        exclude_newline = false;
    }

    //set new chunk buffers respectively
    const char* chosen_prefix = exclude_newline ? prefix1 : prefix2;
    size_t chosen_len = exclude_newline ? len1 : len2;

    new_chunk->buffer = malloc(chosen_len);
    memcpy(new_chunk->buffer, chosen_prefix, chosen_len);
    new_chunk->length = chosen_len;

    return SUCCESS;
}