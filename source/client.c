#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <ctype.h>

#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <pthread.h>
#include "../libs/markdown.h"

#define MAX_LEN 64
#define MAX_CMD_LEN 256

document* my_doc;
char* all_logs = NULL;

pthread_mutex_t doc_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

//append a command to all logs
void append_to_log(char* line) {
    size_t old_len = strlen(all_logs);
    size_t add_len = strlen(line);
    all_logs = realloc(all_logs, old_len+add_len+1);
    memcpy(all_logs+old_len, line, add_len);
    all_logs[old_len+add_len] = '\0';
    return;
}

//thread to handle broadcasts from server
void* server_thread(void* arg) {
    FILE* fd_s2c = (FILE*) arg;
    char line[MAX_CMD_LEN*2];

    bool has_success = false;

    while (fgets(line, sizeof(line), fd_s2c)) {
        pthread_mutex_lock(&log_lock);
        append_to_log(line);
        pthread_mutex_unlock(&log_lock);

        line[strcspn(line, "\n")] = '\0';

        if (strstr(line, "END") && has_success) { //increment version if commands were successful
            pthread_mutex_lock(&doc_lock);
            markdown_increment_version(my_doc);
            pthread_mutex_unlock(&doc_lock);
            has_success = false;
        }
        else if (strstr(line, "SUCCESS")) { //apply successful commands to local document
            has_success = true;

            size_t len = strlen(line);
            line[len - strlen(" SUCCESS")] = '\0';

            char* saveptr;
            char* token = strtok_r(line, " ", &saveptr); //EDIT
            token = strtok_r(NULL, " ", &saveptr); //username
            token = strtok_r(NULL, " ", &saveptr); //command name

            if (strcmp(token, "INSERT") == 0) {
                size_t pos = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
                char* content = strtok_r(NULL, "", &saveptr);
                pthread_mutex_lock(&doc_lock);
                markdown_insert(my_doc, my_doc->version, pos, content);
                pthread_mutex_unlock(&doc_lock);
            }
            else if (strcmp(token, "DEL") == 0) {
                size_t pos = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
                size_t len = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
                pthread_mutex_lock(&doc_lock);
                markdown_delete(my_doc, my_doc->version, pos, len);
                pthread_mutex_unlock(&doc_lock);
            }
            else if (strcmp(token, "NEWLINE") == 0) {
                size_t pos = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
                pthread_mutex_lock(&doc_lock);
                markdown_newline(my_doc, my_doc->version, pos);
                pthread_mutex_unlock(&doc_lock);
            }
            else if (strcmp(token, "HEADING") == 0) {
                size_t level = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
                size_t pos = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
                pthread_mutex_lock(&doc_lock);
                markdown_heading(my_doc, my_doc->version, level, pos);
                pthread_mutex_unlock(&doc_lock);
            }
            else if (strcmp(token, "BOLD") == 0) {
                size_t start = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
                size_t end = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
                pthread_mutex_lock(&doc_lock);
                markdown_bold(my_doc, my_doc->version, start, end);
                pthread_mutex_unlock(&doc_lock);
            }
            else if (strcmp(token, "ITALIC") == 0) {
                size_t start = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
                size_t end = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
                pthread_mutex_lock(&doc_lock);
                markdown_italic(my_doc, my_doc->version, start, end);
                pthread_mutex_unlock(&doc_lock);
            }
            else if (strcmp(token, "BLOCKQUOTE") == 0) {
                size_t pos = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
                pthread_mutex_lock(&doc_lock);
                markdown_blockquote(my_doc, my_doc->version, pos);
                pthread_mutex_unlock(&doc_lock);
            }
            else if (strcmp(token, "ORDERED_LIST") == 0) {
                size_t pos = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
                pthread_mutex_lock(&doc_lock);
                markdown_ordered_list(my_doc, my_doc->version, pos);
                pthread_mutex_unlock(&doc_lock);
            }
            else if (strcmp(token, "UNORDERED_LIST") == 0) {
                size_t pos = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
                pthread_mutex_lock(&doc_lock);
                markdown_unordered_list(my_doc, my_doc->version, pos);
                pthread_mutex_unlock(&doc_lock);
            }
            else if (strcmp(token, "CODE") == 0) {
                size_t start = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
                size_t end = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
                pthread_mutex_lock(&doc_lock);
                markdown_code(my_doc, my_doc->version, start, end);
                pthread_mutex_unlock(&doc_lock);
            }
            else if (strcmp(token, "HORIZONTAL_RULE") == 0) {
                size_t pos = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
                pthread_mutex_lock(&doc_lock);
                markdown_horizontal_rule(my_doc, my_doc->version, pos);
                pthread_mutex_unlock(&doc_lock);
            }
            else if (strcmp(token, "LINK") == 0) {
                size_t start = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
                size_t end = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
                char* url = strtok_r(NULL, "", &saveptr);
                pthread_mutex_lock(&doc_lock);
                markdown_link(my_doc, my_doc->version, start, end, url);
                pthread_mutex_unlock(&doc_lock);
            }
        }
    }
    return NULL;
}

int main(int argc, char** argv) {
    (void) argc;
    pid_t server_pid = (pid_t) strtol(argv[1], NULL, 10);
    char* client_username = argv[2];
    pid_t client_pid = getpid();
    
    //signal setup for SIGRTMIN+1 from server
    sigset_t blocked_signals;
    sigemptyset(&blocked_signals);
    sigaddset(&blocked_signals, SIGRTMIN+1);
    sigprocmask(SIG_BLOCK, &blocked_signals, NULL);

    kill(server_pid, SIGRTMIN);
    
    int server_sig;
    sigwait(&blocked_signals, &server_sig);

    char c2s_fifo[MAX_LEN];
    char s2c_fifo[MAX_LEN];
    snprintf(c2s_fifo, MAX_LEN, "FIFO_C2S_%d", client_pid);
    snprintf(s2c_fifo, MAX_LEN, "FIFO_S2C_%d", client_pid);
    
    FILE* fd_c2s = fopen(c2s_fifo, "w");
    FILE* fd_s2c = fopen(s2c_fifo, "r");

    fprintf(fd_c2s, "%s\n", client_username);
    fflush(fd_c2s);

    char buf[MAX_LEN];
    fgets(buf, MAX_LEN, fd_s2c);
    
    if (strncmp(buf, "Reject", 6) == 0) { //close connection if username not found in roles.txt
        fclose(fd_c2s);
        fclose(fd_s2c);
        return 0;
    }

    //parse initial payload and initialise local document
    buf[strcspn(buf, "\n")] = '\0';
    char* permission = strdup(buf);
    
    fgets(buf, MAX_LEN, fd_s2c);
    buf[strcspn(buf, "\n")] = '\0';
    uint64_t version = strtol(buf, NULL, 10);

    fgets(buf, MAX_LEN, fd_s2c);
    buf[strcspn(buf, "\n")] = '\0';
    size_t doc_len = strtol(buf, NULL, 10);

    char* doc_buf = malloc(doc_len+1);
    fread(doc_buf, sizeof(char), doc_len, fd_s2c);
    doc_buf[doc_len] = '\0';
    
    my_doc = markdown_init();
    markdown_insert(my_doc, my_doc->version, 0, doc_buf);
    markdown_increment_version(my_doc);

    //construct initial document for correct handling of newlines and ordered lists
    for (size_t i = 0; i < doc_len; i++) {
        if (doc_buf[i] == '\n') {
            markdown_delete(my_doc, my_doc->version, i, 1);
            markdown_newline(my_doc, my_doc->version, i);
        }
    }
    markdown_increment_version(my_doc);
    
    for (size_t i = 0; i + 2 < doc_len; i++) {
        if (isdigit(doc_buf[i]) && doc_buf[i + 1] == '.' && doc_buf[i + 2] == ' ') {
            markdown_delete(my_doc, my_doc->version, i, 3);
            markdown_ordered_list(my_doc, my_doc->version, i);
        }
    }
    markdown_increment_version(my_doc);

    free(doc_buf);

    my_doc->version = version;
    all_logs = calloc(1, sizeof(char));

    pthread_t server_pipe;
    pthread_create(&server_pipe, NULL, server_thread, (void*) fd_s2c);

    char input_buf[MAX_CMD_LEN];
    
    //process commands from client stdin
    while (fgets(input_buf, sizeof(input_buf), stdin)) {
        input_buf[strcspn(input_buf, "\n")] = '\0';

        if (strcmp(input_buf, "DOC?") == 0) {
            pthread_mutex_lock(&doc_lock);
            char* doc_content = markdown_flatten(my_doc);
            pthread_mutex_unlock(&doc_lock);
            
            printf("%s\n", doc_content);
            free(doc_content);
        }
        else if (strcmp(input_buf, "PERM?") == 0) {
            printf("%s\n", permission);
        }
        else if (strcmp(input_buf, "LOG?") == 0) {
            pthread_mutex_lock(&log_lock);
            printf("%s", all_logs);
            pthread_mutex_unlock(&log_lock);
        }
        else {
            fprintf(fd_c2s, "%s\n", input_buf);
            fflush(fd_c2s);

            if (strcmp(input_buf, "DISCONNECT") == 0) {
                break;
            }
        }
    }

    pthread_join(server_pipe, NULL);

    //destroy locks, close pipes and free resources
    pthread_mutex_destroy(&doc_lock);
    pthread_mutex_destroy(&log_lock);

    fclose(fd_c2s);
    fclose(fd_s2c);
    
    free(permission);
    permission = NULL;
    markdown_free(my_doc);
    my_doc = NULL;
    free(all_logs);
    all_logs = NULL;
    return 0;
}