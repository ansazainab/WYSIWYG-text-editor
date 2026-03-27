#ifndef HELPER_H
#define HELPER_H

#include <stdio.h>
#include <stdint.h>
#include "document.h" 

chunk* split_chunk_after(chunk* ch, size_t offset);
chunk* split_chunk_middle(chunk* ch, size_t start, size_t end);
bool check_deleted_pos(document* doc, size_t start, size_t end);
int insert_prefix(document* doc, size_t pos, const char* prefix1, const char* prefix2);

#endif