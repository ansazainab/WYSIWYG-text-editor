#include "../libs/markdown.h"
#include "../libs/helper_functions.h"
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define SUCCESS 0
#define INVALID_CURSOR_POS -1
#define DELETED_POSITION -2
#define OUTDATED_VERSION -3

// === Init and Free ===
document* markdown_init(void) {
    document* doc = malloc(sizeof(document));
    if (!doc) {
        return NULL;
    }

    doc->length = 0;
    doc->version = 0;
    doc->chunks = NULL;
    return doc;
}

void markdown_free(document* doc) {
    chunk* ch = doc->chunks;
    while (ch) {
        chunk* temp = ch;
        ch = ch->next;
        free(temp->buffer);
        temp->next = NULL;
        free(temp);
    }

    doc->chunks = NULL;
    free(doc);
    doc = NULL;
    return;
}

// === Edit Commands ===
int markdown_insert(document* doc, uint64_t version, size_t pos, const char* content) {
    if (version != doc->version) {
        return OUTDATED_VERSION;
    }
    if (pos > doc->length) {
        return INVALID_CURSOR_POS;
    }

    //initialise a new chunk for content to be inserted
    size_t content_len = strlen(content);
    chunk* new_chunk = malloc(sizeof(chunk));
    new_chunk->buffer = malloc(sizeof(char) * content_len);
    memcpy(new_chunk->buffer, content, content_len);
    
    new_chunk->is_deleted = false;
    new_chunk->is_inserted = true;
    new_chunk->is_newline = false;
    new_chunk->ordered_list = 0;
    new_chunk->length = content_len;
    new_chunk->next = NULL;

    if (new_chunk->length == 1 && new_chunk->buffer[0] == '\n') {
        new_chunk->is_newline = true;
    }
    
    size_t offset = 0;
    chunk* dest_chunk = doc->chunks;
    
    if (!dest_chunk) { //inserting in empty document
        doc->chunks = new_chunk;
        return SUCCESS;
    }
    if (pos == 0) { //inserting at start of document
        doc->chunks = new_chunk;
        new_chunk->next = dest_chunk;
        return SUCCESS;
    }

    //chunk* prev_chunk = NULL;
    
    while (dest_chunk) { //find position to insert
        while (dest_chunk && (offset + dest_chunk->length < pos)) {
            if (!dest_chunk->is_inserted) { //skip over chunks that haven't been committed yet
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

    // if (dest_chunk->is_deleted) { //adjust position if target position was deleted
    //     if (prev_chunk) {
    //         new_chunk->next = prev_chunk->next;
    //         prev_chunk->next = new_chunk;
    //         if (prev_chunk->ordered_list != 0 && !new_chunk->is_newline) { //adjust ordered list properties
    //             new_chunk->ordered_list = 2;
    //         }
    //     }
    //     else {
    //         new_chunk->next = doc->chunks;
    //         doc->chunks = new_chunk;
    //     }
    // }
    if (offset + dest_chunk->length == pos) { //insert at boundary of 2 chunks
        new_chunk->next = dest_chunk->next;
        dest_chunk->next = new_chunk;
        if (dest_chunk->ordered_list != 0 && !new_chunk->is_newline) {
            new_chunk->ordered_list = 2;
        }
    }
    else { //insert in the middle of existing chunk
        size_t local_offset = pos - offset;
        split_chunk_after(dest_chunk, local_offset);
        new_chunk->next = dest_chunk->next;
        dest_chunk->next = new_chunk;
        if (dest_chunk->ordered_list != 0 && !new_chunk->is_newline) {
            new_chunk->ordered_list = 2;
        }
    }

    return SUCCESS;
}

int markdown_delete(document* doc, uint64_t version, size_t pos, size_t len) {
    if (version != doc->version) {
        return OUTDATED_VERSION;
    }
    if (pos > doc->length) {
        return INVALID_CURSOR_POS;
    }

    size_t offset = 0;
    size_t remaining = len;
    chunk* dest_chunk = doc->chunks;
    chunk* prev = NULL;
    
    while (dest_chunk && remaining > 0) {
        //find region to delete
        if (offset + dest_chunk->length <= pos) {
            if (!dest_chunk->is_inserted) {
                offset += dest_chunk->length;
                prev = dest_chunk;
            }

            dest_chunk = dest_chunk->next;
            continue;
        }

        if (dest_chunk && dest_chunk->is_inserted) {
            dest_chunk = dest_chunk->next;
            continue;
        }

        size_t start = 0;
        if (pos > offset) {
            start = pos - offset;
        }
        size_t available = dest_chunk->length - start;
        size_t to_delete = remaining;
        if (available < remaining) {
            to_delete = available;
        }

        if (start == 0 && to_delete == dest_chunk->length) { //entire chunk to be deleted
            dest_chunk->is_deleted = true;
            if (prev && prev->is_newline && dest_chunk->ordered_list == 1) { //adjust ordered list properties
                prev->ordered_list = 0;
            }
        }
        else if (start == 0 && (start + to_delete < dest_chunk->length)) { //start of chunk to be deleted
            split_chunk_after(dest_chunk, to_delete);
            dest_chunk->is_deleted = true;
            if (prev && prev->is_newline && dest_chunk->ordered_list == 1) {
                prev->ordered_list = 0;
            }
        }
        else if (start > 0 && (start + to_delete == dest_chunk->length)) { //end of chunk to be deleted
            split_chunk_after(dest_chunk, start);
            dest_chunk->next->is_deleted = true;
            if (dest_chunk->buffer[dest_chunk->length-1] == '\n' && dest_chunk->next->ordered_list == 1) {
                dest_chunk->ordered_list = 0;
            }
        }
        else if (start > 0 && (start + to_delete < dest_chunk->length)) { //middle of chunk to be deleted
            split_chunk_middle(dest_chunk, start, start + to_delete);
            dest_chunk->next->is_deleted = true;
            if (dest_chunk->buffer[dest_chunk->length-1] == '\n' && dest_chunk->next->ordered_list == 1) {
                dest_chunk->ordered_list = 0;
            }
        }

        remaining -= to_delete;
        pos += to_delete;
        offset += dest_chunk->length;
        if (remaining > 0) {
            dest_chunk = dest_chunk->next;
        }  
    }
    return SUCCESS;
}

// === Formatting Commands ===
int markdown_newline(document* doc, uint64_t version, size_t pos) {
    if (version != doc->version) {
        return OUTDATED_VERSION;
    }
    if (pos > doc->length) {
        return INVALID_CURSOR_POS;
    }

    markdown_insert(doc, version, pos, "\n");
    return SUCCESS;
}

int markdown_heading(document* doc, uint64_t version, size_t level, size_t pos) {
    if (version != doc->version) {
        return OUTDATED_VERSION;
    }
    if (pos > doc->length) {
        return INVALID_CURSOR_POS;
    }

    const char* heading1[] = {"# ", "## ", "### "};
    const char* heading2[] = {"\n# ", "\n## ", "\n### "};

    return insert_prefix(doc, pos, heading1[level-1], heading2[level-1]);
}

int markdown_bold(document* doc, uint64_t version, size_t start, size_t end) {
    if (version != doc->version) {
        return OUTDATED_VERSION;
    }
    if (start > doc->length || end > doc->length || end <= start) {
        return INVALID_CURSOR_POS;
    }
    if (check_deleted_pos(doc, start, end)) {
        return DELETED_POSITION;
    }

    markdown_insert(doc, version, start, "**");
    markdown_insert(doc, version, end, "**");
    return SUCCESS;
}

int markdown_italic(document* doc, uint64_t version, size_t start, size_t end) {
    if (version != doc->version) {
        return OUTDATED_VERSION;
    }
    if (start > doc->length || end > doc->length || end <= start) {
        return INVALID_CURSOR_POS;
    }
    if (check_deleted_pos(doc, start, end)) {
        return DELETED_POSITION;
    }

    markdown_insert(doc, version, start, "*");
    markdown_insert(doc, version, end, "*");
    return SUCCESS;
}

int markdown_blockquote(document* doc, uint64_t version, size_t pos) {
    if (version != doc->version) {
        return OUTDATED_VERSION;
    }
    if (pos > doc->length) {
        return INVALID_CURSOR_POS;
    }

    return insert_prefix(doc, pos, "> ", "\n> ");
}

int markdown_ordered_list(document* doc, uint64_t version, size_t pos) {
    if (version != doc->version) {
        return OUTDATED_VERSION;
    }
    if (pos > doc->length) {
        return INVALID_CURSOR_POS;
    }

    chunk* new_chunk = malloc(sizeof(chunk));
    new_chunk->is_deleted = false;
    new_chunk->is_inserted = true;
    new_chunk->is_newline = false;
    new_chunk->ordered_list = 1;
    new_chunk->next = NULL;

    char* new_list1 = "0. ";
    char* new_list2 = "\n0. ";
    size_t len1 = strlen(new_list1);
    size_t len2 = strlen(new_list2);

    size_t offset = 0;
    chunk* dest_chunk = doc->chunks;
    
    if (!dest_chunk) { //insert in empty document
        doc->chunks = new_chunk;
        new_chunk->buffer = malloc(sizeof(char) * len1);
        memcpy(new_chunk->buffer, new_list1, len1);
        new_chunk->length = len1;
        return SUCCESS;
    }

    if (pos == 0) { //insert at start of document
        doc->chunks = new_chunk;
        new_chunk->next = dest_chunk;
        new_chunk->buffer = malloc(sizeof(char) * len1);
        memcpy(new_chunk->buffer, new_list1, len1);
        new_chunk->length = len1;
        
        while (dest_chunk) { //mark subsequent chunks as ordered list items
            if (dest_chunk->buffer[0] == '\n') {
                break;
            }
            dest_chunk->ordered_list = 2;
            dest_chunk = dest_chunk->next;
        }
        return SUCCESS;
    }

    //chunk* prev_chunk = NULL;
    while (dest_chunk) { //find position to insert ordered list
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
    //         if (prev_chunk->is_newline) {
    //             prev_chunk->ordered_list = 2;
    //         }
    //     }
    //     else {
    //         new_chunk->next = doc->chunks;
    //         doc->chunks = new_chunk;
    //         exclude_newline = true;
    //     }
    // }
    if (offset + dest_chunk->length == pos) { //inserting at boundary of 2 chunks
        new_chunk->next = dest_chunk->next;
        dest_chunk->next = new_chunk;
        exclude_newline = (dest_chunk->is_newline || dest_chunk->buffer[dest_chunk->length-1] == '\n');
        if (dest_chunk->is_newline) {
            dest_chunk->ordered_list = 2;
        }
    }
    else { //insert in middle of existing chunk
        size_t local_offset = pos - offset;
        split_chunk_after(dest_chunk, local_offset);
        new_chunk->next = dest_chunk->next;
        dest_chunk->next = new_chunk;
        exclude_newline = false;
    }

    //set new chunk buffer based on whether newline needed or not
    char* chosen_prefix = exclude_newline ? new_list1 : new_list2;
    size_t chosen_len = exclude_newline ? len1 : len2;

    new_chunk->buffer = malloc(chosen_len);
    memcpy(new_chunk->buffer, chosen_prefix, chosen_len);
    new_chunk->length = chosen_len;

    chunk* tmp = new_chunk->next;
    while (tmp) { //mark subsequent chunks as ordered list items
        if (tmp->buffer[0] == '\n') {
            break;
        }
        tmp->ordered_list = 2;
        tmp = tmp->next;
    }

    return SUCCESS;
}

int markdown_unordered_list(document* doc, uint64_t version, size_t pos) {
    if (version != doc->version) {
        return OUTDATED_VERSION;
    }
    if (pos > doc->length) {
        return INVALID_CURSOR_POS;
    }

    return insert_prefix(doc, pos, "- ", "\n- ");
}

int markdown_code(document* doc, uint64_t version, size_t start, size_t end) {
    if (version != doc->version) {
        return OUTDATED_VERSION;
    }
    if (start > doc->length || end > doc->length || end <= start) {
        return INVALID_CURSOR_POS;
    }
    if (check_deleted_pos(doc, start, end)) {
        return DELETED_POSITION;
    }

    markdown_insert(doc, version, start, "`");
    markdown_insert(doc, version, end, "`");
    return SUCCESS;
}

int markdown_horizontal_rule(document* doc, uint64_t version, size_t pos) {
    if (version != doc->version) {
        return OUTDATED_VERSION;
    }
    if (pos > doc->length) {
        return INVALID_CURSOR_POS;
    }

    chunk* new_chunk = malloc(sizeof(chunk));
    new_chunk->is_deleted = false;
    new_chunk->is_inserted = true;
    new_chunk->is_newline = false;
    new_chunk->ordered_list = 0;
    new_chunk->next = NULL;

    char* new_rule1 = "---";
    char* new_rule2 = "\n---";
    char* new_rule3 = "---\n";
    char* new_rule4 = "\n---\n";
    size_t len1 = strlen(new_rule1);
    size_t len2 = strlen(new_rule2);
    size_t len3 = strlen(new_rule3);
    size_t len4 = strlen(new_rule4);

    size_t offset = 0;
    chunk* dest_chunk = doc->chunks;
    
    if (!dest_chunk) { //insert in empty document
        doc->chunks = new_chunk;
        new_chunk->buffer = malloc(sizeof(char) * len3);
        memcpy(new_chunk->buffer, new_rule3, len3);
        new_chunk->length = len3;
        return SUCCESS;
    }

    if (pos == 0) { //insert at start of document
        doc->chunks = new_chunk;
        new_chunk->next = dest_chunk;
        
        chunk* curr = dest_chunk;
        while (curr->next && curr->is_inserted) { //determine whether post newline needed, exclude uncommitted chunks
            curr = curr->next;
        }

        if (curr->buffer[0] == '\n' && !curr->is_inserted) { //assign buffers accordingly
            new_chunk->buffer = malloc(sizeof(char) * len1);
            memcpy(new_chunk->buffer, new_rule1, len1);
            new_chunk->length = len1;
        }
        else {
            new_chunk->buffer = malloc(sizeof(char) * len3);
            memcpy(new_chunk->buffer, new_rule3, len3);
            new_chunk->length = len3;
        }
        return SUCCESS;
    }

    //chunk* prev_chunk = NULL;
    while (dest_chunk) { //find position to insert
        while (dest_chunk && (offset + dest_chunk->length < pos)) {
            if (!dest_chunk->is_inserted) {
                offset += dest_chunk->length;
                // if (!dest_chunk->is_deleted) {
                //     prev_chunk = dest_chunk;
                // }
            }

           // prev_chunk = dest_chunk;
            dest_chunk = dest_chunk->next;
        }

        if (dest_chunk && dest_chunk->is_inserted) {
            //prev_chunk = dest_chunk;
            dest_chunk = dest_chunk->next;
            continue;
        }

        break;
    }

    bool exclude_newline_before = false;
    bool exclude_newline_after = false;

    // if (dest_chunk->is_deleted) { //adjust position if target position was deleted
    //     if (prev_chunk) {
    //         new_chunk->next = prev_chunk->next;
    //         prev_chunk->next = new_chunk;
            
    //         chunk* curr = new_chunk->next;
    //         while (curr && curr->next && curr->is_inserted) {
    //             curr = curr->next;
    //         }

    //         //determine whether pre or post newlines needed
    //         exclude_newline_before = (prev_chunk->is_newline || prev_chunk->buffer[prev_chunk->length-1] == '\n');
    //         exclude_newline_after = (curr && curr->buffer[0] == '\n' && !curr->is_inserted);
    //     }
    //     else {
    //         new_chunk->next = doc->chunks;
    //         doc->chunks = new_chunk;
            
    //         chunk* curr = new_chunk->next;
    //         while (curr && curr->next && curr->is_inserted) {
    //             curr = curr->next;
    //         }

    //         exclude_newline_before = true;
    //         exclude_newline_after = (curr && curr->buffer[0] == '\n' && !curr->is_inserted);
    //     }
    // }
    if (offset + dest_chunk->length == pos) { //inserting at boundary of 2 chunks
        new_chunk->next = dest_chunk->next;
        dest_chunk->next = new_chunk;
        
        chunk* curr = new_chunk->next;
        while (curr && curr->next && curr->is_inserted) {
            curr = curr->next;
        }
        
        exclude_newline_before = (dest_chunk->is_newline || dest_chunk->buffer[dest_chunk->length-1] == '\n');
        exclude_newline_after = (curr && curr->buffer[0] == '\n' && !curr->is_inserted);
    }
    else { //inserting in middle of existing chunk
        size_t local_offset = pos - offset;
        split_chunk_after(dest_chunk, local_offset);
        new_chunk->next = dest_chunk->next;
        dest_chunk->next = new_chunk;
        exclude_newline_before = false;
        exclude_newline_after = false;
    }

    //set new chunk buffer based on whether pre and post newlines needed or not
    char* chosen_prefix;
    size_t chosen_len;

    if (exclude_newline_before) {
        chosen_prefix = exclude_newline_after ? new_rule1 : new_rule3;
        chosen_len = exclude_newline_after ? len1 : len3;
    }
    else {
        chosen_prefix = exclude_newline_after ? new_rule2 : new_rule4;
        chosen_len = exclude_newline_after ? len2 : len4;
    }

    new_chunk->buffer = malloc(sizeof(char) * chosen_len);
    memcpy(new_chunk->buffer, chosen_prefix, chosen_len);
    new_chunk->length = chosen_len;
    return SUCCESS;
}

int markdown_link(document* doc, uint64_t version, size_t start, size_t end, const char* url) {
    if (version != doc->version) {
        return OUTDATED_VERSION;
    }
    if (start > doc->length || end > doc->length || end <= start) {
        return INVALID_CURSOR_POS;
    }
    if (check_deleted_pos(doc, start, end)) {
        return DELETED_POSITION;
    }

    size_t len = strlen(url);
    char* bracket_url = malloc(sizeof(char) * (len+4));
    snprintf(bracket_url, len+4, "](%s)", url);
    
    markdown_insert(doc, version, start, "[");
    markdown_insert(doc, version, end, bracket_url);
    
    free(bracket_url);
    bracket_url = NULL;
    return SUCCESS;
}

// === Utilities ===
void markdown_print(const document* doc, FILE* stream) {
    char* final_doc = markdown_flatten(doc);
    fprintf(stream, "%s", final_doc);
    fflush(stream);
    free(final_doc);
    return;
}

char* markdown_flatten(const document* doc) {
    char* final_doc = malloc(sizeof(char) * (doc->length+1));
    if (!final_doc) {
        return NULL;
    }

    size_t count = 0;
    chunk* tmp = doc->chunks;
    while (tmp) {
        if (tmp->is_inserted) { //skip over uncommitted chunks
            tmp = tmp->next;
            continue;
        }

        memcpy(final_doc+count, tmp->buffer, tmp->length);
        count += tmp->length;
        tmp = tmp->next;
    }

    final_doc[doc->length] = '\0';
    return final_doc;
}

// === Versioning ===
void markdown_increment_version(document* doc) {
    size_t list_count = 1;
    chunk* prev_chunk = NULL;
    chunk* current = doc->chunks;
    
    while (current) {
        chunk* tmp = current;
        current = current->next;
        if (tmp->is_deleted) { //delete and free chunks to be deleted
            if (prev_chunk) {
                prev_chunk->next = tmp->next;
            }
            else {
                doc->chunks = tmp->next;
            }

            doc->length -= tmp->length;
            free(tmp->buffer);
            tmp->buffer = NULL;
            tmp->next = NULL;
            free(tmp);
            continue;
        }

        if (tmp->is_inserted) { //commit new chunks to be inserted
            tmp->is_inserted = false;
            doc->length += tmp->length;
        }

        if (tmp->ordered_list == 1) { //process ordered list numbering and set number if it is a numbered chunk
            for (size_t i = 0; i < tmp->length; i++) {
                if (isdigit(tmp->buffer[i])) {
                    char c = '0' + list_count;
                    tmp->buffer[i] = c;
                    list_count++;
                    break;
                }
            }
        }

        if (tmp->ordered_list == 0) { //reset ordered list count if non-ordered list chunk encountered
            list_count = 1;
        }

        prev_chunk = tmp;
    }

    doc->version++;
    return;
}