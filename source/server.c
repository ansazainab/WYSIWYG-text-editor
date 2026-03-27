#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>

#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <pthread.h>
#include "../libs/markdown.h"

#define SUCCESS 0
#define INVALID_CURSOR_POS -1
#define DELETED_POSITION -2
#define OUTDATED_VERSION -3
#define MAX_LEN 64
#define MAX_CMD_LEN 256

long int TIME_INTERVAL;
char* all_logs = NULL;
char* version_log = NULL;
volatile bool terminate = false;
document* my_doc;
struct all_clients* my_clients;
struct command_queue* my_queue;

pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t doc_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t client_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t term_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;

//append a command to logs of the respective version
void append_to_log(char* line) {
    size_t old_len = strlen(version_log);
    size_t add_len = strlen(line);
    version_log = realloc(version_log, old_len+add_len+1);
    memcpy(version_log+old_len, line, add_len);
    version_log[old_len+add_len] = '\0';
    return;
}

//create a line with the result of a command and add to log
void create_log_line(int ret_val, char* username, char* command_buf) {
    char command_summary[MAX_CMD_LEN*2];
    char* reason = NULL;
    
    if (ret_val == SUCCESS) {
        snprintf(command_summary, sizeof(command_summary), "EDIT %s %s SUCCESS\n", username, command_buf);
    }
    else if (ret_val == INVALID_CURSOR_POS) {
        reason = "INVALID_CURSOR_POS";
    }
    else if (ret_val == DELETED_POSITION) {
        reason = "DELETED_POSITION";
    }
    else if (ret_val == OUTDATED_VERSION) {
        reason = "OUTDATED_VERSION";
    }

    if (reason) {
        snprintf(command_summary, sizeof(command_summary), "EDIT %s %s Reject %s\n", username, command_buf, reason);
    }
    
    pthread_mutex_lock(&log_lock);
    append_to_log(command_summary);
    pthread_mutex_unlock(&log_lock);
    return;
}

//thread to broadcast to all clients every time_interval milliseconds
void* broadcast(void*) {
    while (true) {
        pthread_mutex_lock(&term_lock);
        bool term = terminate;
        pthread_mutex_unlock(&term_lock);
        if (term) {
            return NULL;
        }

        usleep(TIME_INTERVAL * 1000);

        pthread_mutex_lock(&log_lock);

        bool has_success = false;
        char* save_ptr = NULL;
        char* temp = strdup(version_log);
        char* line = strtok_r(temp, "\n", &save_ptr);
        
        while (line) { //check for successful edits
            if (strstr(line, "SUCCESS")) {
                has_success = true;
                break;
            }
            line = strtok_r(NULL, "\n", &save_ptr);
        }
        free(temp);

        if (has_success) {
            pthread_mutex_lock(&doc_lock);
            markdown_increment_version(my_doc);
            pthread_mutex_unlock(&doc_lock);
        }

        //make payload with version header and end footer
        char version_header[MAX_LEN];
        snprintf(version_header, sizeof(version_header), "VERSION %lu\n", my_doc->version);
        size_t version_header_len = strlen(version_header);
        size_t version_log_len = strlen(version_log);
        char* end_marker = "END\n";
        size_t payload_len = version_header_len + version_log_len + strlen(end_marker) + 1;
        char* payload = malloc(payload_len);
        snprintf(payload, payload_len, "%s%s%s", version_header, version_log, end_marker);

        //add payload to all logs
        size_t all_logs_len = strlen(all_logs);
        all_logs = realloc(all_logs, all_logs_len + payload_len);
        memcpy(all_logs+all_logs_len, payload, payload_len);

        pthread_mutex_lock(&client_lock);
        
        //send payload to all clients
        struct client_pipe* current = my_clients->clients;
        while (current) {
            fprintf(current->s2c_fd, "%s", payload);
            fflush(current->s2c_fd);
            current = current->next;
        }

        pthread_mutex_unlock(&client_lock);
        
        free(payload);
        version_log = realloc(version_log, 1);
        version_log[0] = '\0';

        pthread_mutex_unlock(&log_lock);
    }
    return NULL;
}

//thread to process commands queue
void* process_queue(void*) {
    while (true) {
        pthread_mutex_lock(&term_lock);
        bool term = terminate;
        pthread_mutex_unlock(&term_lock);

        pthread_mutex_lock(&queue_lock);
        int num = my_queue->num_commands;
        pthread_mutex_unlock(&queue_lock);

        if (term && num == 0) {
            return NULL;
        }
        if (num == 0) {
            continue;
        }

        pthread_mutex_lock(&queue_lock);
        
        //get the earliest command
        struct command_item* curr = my_queue->commands;
        struct command_item* earliest = curr;
        while (curr) {
            if (curr->time < earliest->time) {
                earliest = curr;
            }
            curr = curr->next;
        }

        //store command and associated user for further processing
        bool check_authorised = earliest->authorised;
        char* username = strdup(earliest->username);
        char* command_buf = strdup(earliest->command);

        //remove command from queue
        free(earliest->command);
        earliest->command = NULL;
        free(earliest->username);
        earliest->username = NULL;
        
        if (earliest->prev) {
            earliest->prev->next = earliest->next;
        }
        if (earliest->next) {
            earliest->next->prev = earliest->prev;
        }
        if (my_queue->commands == earliest) {
            my_queue->commands = earliest->next;
        }
        my_queue->num_commands--;
        free(earliest);
        earliest = NULL;
        
        pthread_mutex_unlock(&queue_lock);

        char command_summary[MAX_CMD_LEN*2];
        char command_buf_copy[MAX_CMD_LEN+1];
        snprintf(command_buf_copy, sizeof(command_buf_copy), "%s", command_buf);

        //reject if unauthorised
        if (!check_authorised) {
            snprintf(command_summary, MAX_CMD_LEN*2, "EDIT %s %s Reject UNAUTHORISED\n", username, command_buf);
            pthread_mutex_lock(&log_lock);
            append_to_log(command_summary);
            pthread_mutex_unlock(&log_lock);

            free(username);
            username = NULL;
            free(command_buf);
            command_buf = NULL;
            continue;
        }

        //parse command, process it and add it to log
        char* saveptr;
        char* cmd = strtok_r(command_buf_copy, " ", &saveptr);
        int ret_val = 1;

        if (strcmp(cmd, "INSERT") == 0) {
            size_t pos = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
            char* content = strtok_r(NULL, "", &saveptr);
            pthread_mutex_lock(&doc_lock);
            ret_val = markdown_insert(my_doc, my_doc->version, pos, content);
            pthread_mutex_unlock(&doc_lock);
        }
        else if (strcmp(cmd, "DEL") == 0) {
            size_t pos = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
            size_t len = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
            pthread_mutex_lock(&doc_lock);
            ret_val = markdown_delete(my_doc, my_doc->version, pos, len);
            pthread_mutex_unlock(&doc_lock);
        }
        else if (strcmp(cmd, "NEWLINE") == 0) {
            size_t pos = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
            pthread_mutex_lock(&doc_lock);
            ret_val = markdown_newline(my_doc, my_doc->version, pos);
            pthread_mutex_unlock(&doc_lock);
        }
        else if (strcmp(cmd, "HEADING") == 0) {
            size_t level = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
            size_t pos = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
            pthread_mutex_lock(&doc_lock);
            ret_val = markdown_heading(my_doc, my_doc->version, level, pos);
            pthread_mutex_unlock(&doc_lock);
        }
        else if (strcmp(cmd, "BOLD") == 0) {
            size_t start = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
            size_t end = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
            pthread_mutex_lock(&doc_lock);
            ret_val = markdown_bold(my_doc, my_doc->version, start, end);
            pthread_mutex_unlock(&doc_lock);
        }
        else if (strcmp(cmd, "ITALIC") == 0) {
            size_t start = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
            size_t end = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
            pthread_mutex_lock(&doc_lock);
            ret_val = markdown_italic(my_doc, my_doc->version, start, end);
            pthread_mutex_unlock(&doc_lock);
        }
        else if (strcmp(cmd, "BLOCKQUOTE") == 0) {
            size_t pos = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
            pthread_mutex_lock(&doc_lock);
            ret_val = markdown_blockquote(my_doc, my_doc->version, pos);
            pthread_mutex_unlock(&doc_lock);
        }
        else if (strcmp(cmd, "ORDERED_LIST") == 0) {
            size_t pos = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
            pthread_mutex_lock(&doc_lock);
            ret_val = markdown_ordered_list(my_doc, my_doc->version, pos);
            pthread_mutex_unlock(&doc_lock);
        }
        else if (strcmp(cmd, "UNORDERED_LIST") == 0) {
            size_t pos = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
            pthread_mutex_lock(&doc_lock);
            ret_val = markdown_unordered_list(my_doc, my_doc->version, pos);
            pthread_mutex_unlock(&doc_lock);
        }
        else if (strcmp(cmd, "CODE") == 0) {
            size_t start = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
            size_t end = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
            pthread_mutex_lock(&doc_lock);
            ret_val = markdown_code(my_doc, my_doc->version, start, end);
            pthread_mutex_unlock(&doc_lock);
        }
        else if (strcmp(cmd, "HORIZONTAL_RULE") == 0) {
            size_t pos = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
            pthread_mutex_lock(&doc_lock);
            ret_val = markdown_horizontal_rule(my_doc, my_doc->version, pos);
            pthread_mutex_unlock(&doc_lock);
        }
        else if (strcmp(cmd, "LINK") == 0) {
            size_t start = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
            size_t end = (size_t) strtol(strtok_r(NULL, " ", &saveptr), NULL, 10);
            char* url = strtok_r(NULL, "", &saveptr);
            pthread_mutex_lock(&doc_lock);
            ret_val = markdown_link(my_doc, my_doc->version, start, end, url);
            pthread_mutex_unlock(&doc_lock);
        }

        create_log_line(ret_val, username, command_buf); //add the processed command to log

        free(username);
        username = NULL;
        free(command_buf);
        command_buf = NULL;
    }
}

//thread to initialise client connections + handle commands from them
void* client_thread(void* arg) {
    pid_t client_pid = *((pid_t*) arg);
    free(arg);
    
    char c2s_fifo[MAX_LEN];
    char s2c_fifo[MAX_LEN];
    snprintf(c2s_fifo, MAX_LEN, "FIFO_C2S_%d", client_pid);
    snprintf(s2c_fifo, MAX_LEN, "FIFO_S2C_%d", client_pid);
    
    unlink(c2s_fifo);
    unlink(s2c_fifo);
    
    mode_t perm = 0600;
    mkfifo(c2s_fifo, perm);
    mkfifo(s2c_fifo, perm);
    
    kill(client_pid, SIGRTMIN+1);
    
    FILE* fd_c2s = fopen(c2s_fifo, "r");
    FILE* fd_s2c = fopen(s2c_fifo, "w");

    char username[MAX_LEN];
    char* role = NULL;
    bool authorised = false;
    
    fgets(username, sizeof(username) - 1, fd_c2s);
    username[strcspn(username, "\n")] = '\0';
    
    FILE* roles = fopen("roles.txt", "r");
    char roles_line[MAX_CMD_LEN];
    bool found = false;
    
    //check if username is present in roles.txt
    while (fgets(roles_line, sizeof(roles_line), roles)) {
        roles_line[strcspn(roles_line, "\n")] = '\0';
        char* save_ptr = NULL;
        char* usr_token = strtok_r(roles_line, " \t", &save_ptr);
        
        if (usr_token && strcmp(usr_token, username) == 0) {
            found = true;
            role = strtok_r(NULL, " \t", &save_ptr);
            break;
        }
    }

    fclose(roles);
    
    if (!found) {
        fprintf(fd_s2c, "Reject UNAUTHORISED\n");
        fflush(fd_s2c);
        sleep(1);
        fclose(fd_c2s);
        fclose(fd_s2c);
        unlink(c2s_fifo);
        unlink(s2c_fifo);
        return NULL;
    }

    if (strcmp(role, "write") == 0) {
        authorised = true;
    }

    pthread_mutex_lock(&doc_lock);
    char* doc_content = markdown_flatten(my_doc);
    pthread_mutex_unlock(&doc_lock);
    
    //send initial payload with document information
    size_t doc_len = strlen(doc_content);
    fprintf(fd_s2c, "%s\n%lu\n%lu\n%s", role, my_doc->version, doc_len, doc_content);
    fflush(fd_s2c);
    free(doc_content);

    struct client_pipe* client = malloc(sizeof(struct client_pipe));
    client->s2c_fd = fd_s2c;
    client->next = NULL;
    client->prev = NULL;
    
    //add client to client list
    pthread_mutex_lock(&client_lock);
    if (!my_clients->clients) {
        my_clients->clients = client;
        my_clients->num_clients++;
    }
    else {
        client->next = my_clients->clients;
        my_clients->clients->prev = client;
        my_clients->clients = client;
        my_clients->num_clients++;
    }
    pthread_mutex_unlock(&client_lock);

    char command_buf[MAX_CMD_LEN+1];
    
    //intercept commands from client, add timestamp and add to queue
    while (fgets(command_buf, MAX_CMD_LEN, fd_c2s)) {
        time_t current_time = time(NULL);
        
        command_buf[strcspn(command_buf, "\n")] = '\0';

        if (strstr(command_buf, "DISCONNECT")) {
            break;
        }
        
        struct command_item* new_cmd = malloc(sizeof(struct command_item));
        new_cmd->command = malloc(sizeof(char) * (strlen(command_buf)+1));
        memcpy(new_cmd->command, command_buf, strlen(command_buf)+1);
        new_cmd->username = malloc(sizeof(char) * (strlen(username)+1));
        memcpy(new_cmd->username, username, strlen(username)+1);
        
        new_cmd->time = current_time;
        new_cmd->authorised = authorised;
        new_cmd->next = NULL;
        new_cmd->prev = NULL;
        
        pthread_mutex_lock(&queue_lock);
        if (!my_queue->commands) {
            my_queue->commands = new_cmd;
            my_queue->num_commands++;
        }
        else {
            new_cmd->next = my_queue->commands;
            my_queue->commands->prev = new_cmd;
            my_queue->commands = new_cmd;
            my_queue->num_commands++;
        }
        pthread_mutex_unlock(&queue_lock);
    }

    //remove client from client list when disconnected
    pthread_mutex_lock(&client_lock);
    if (client->prev) {
        client->prev->next = client->next;
    }
    if (client->next) {
        client->next->prev = client->prev;
    }
    if (my_clients->clients == client) {
        my_clients->clients = client->next;
    }
    my_clients->num_clients--;
    free(client);
    client = NULL;
    pthread_mutex_unlock(&client_lock);

    fclose(fd_c2s);
    fclose(fd_s2c);
    unlink(c2s_fifo);
    unlink(s2c_fifo);
    return NULL;
}

//signal handler to intercept SIGRTMIN from client
void sig_handler(int sig, siginfo_t* info, void*) {
    if (sig == SIGRTMIN) {
        pid_t client_pid = info->si_pid;
        pid_t* arg = malloc(sizeof(pid_t));
        *arg = client_pid;

        pthread_t thread1;
        pthread_create(&thread1, NULL, client_thread, arg);
    }

    return;
}

int main(int argc, char** argv) {
    (void) argc;
    TIME_INTERVAL = strtol(argv[1], NULL, 10);
    
    pid_t server_pid = getpid();
    printf("Server PID: %d\n", server_pid);
    
    //initialise global variables
    my_doc = markdown_init();
    
    my_clients = malloc(sizeof(struct all_clients));
    my_clients->num_clients = 0;
    my_clients->clients = NULL;

    my_queue = malloc(sizeof(struct command_queue));
    my_queue->num_commands = 0;
    my_queue->commands = NULL;

    version_log = calloc(1, sizeof(char));
    all_logs = calloc(1, sizeof(char));

    //start threads
    pthread_t broadcast_thread;
    pthread_create(&broadcast_thread, NULL, broadcast, NULL);

    pthread_t queue_thread;
    pthread_create(&queue_thread, NULL, process_queue, NULL);

    //signal setup to intercept signals from client
    sigset_t blocked_signals;
    sigemptyset(&blocked_signals);
    sigaddset(&blocked_signals, SIGRTMIN);
    sigaddset(&blocked_signals, SIGRTMIN+1);
    sigprocmask(SIG_BLOCK, &blocked_signals, NULL);

    struct sigaction sig1 = {0};
    sig1.sa_flags = SA_SIGINFO;
    sig1.sa_sigaction = sig_handler;
    sigaction(SIGRTMIN, &sig1, NULL);

    sigprocmask(SIG_UNBLOCK, &blocked_signals, NULL);

    char stdin_buffer[MAX_LEN];
    
    //process commands from server stdin
    while (true) {
        if (fgets(stdin_buffer, sizeof(stdin_buffer), stdin)) {
            stdin_buffer[strcspn(stdin_buffer, "\n")] = '\0';
            
            if (strcmp(stdin_buffer, "DOC?") == 0) {
                pthread_mutex_lock(&doc_lock);
                char* doc_content = markdown_flatten(my_doc);
                pthread_mutex_unlock(&doc_lock);
                
                printf("%s\n", doc_content);
                free(doc_content);
            }
            else if (strcmp(stdin_buffer, "LOG?") == 0) {
                pthread_mutex_lock(&log_lock);
                printf("%s", all_logs);
                pthread_mutex_unlock(&log_lock);
            }
            else if (strcmp(stdin_buffer, "QUIT") == 0) {
                pthread_mutex_lock(&client_lock);
                int num = my_clients->num_clients;
                pthread_mutex_unlock(&client_lock);
                
                if (num > 0) {
                    printf("QUIT rejected, %d clients still connected.\n", num);
                    continue;
                }
                
                //save document before quitting
                FILE* doc = fopen("doc.md", "w");
                
                pthread_mutex_lock(&doc_lock);
                markdown_print(my_doc, doc);
                pthread_mutex_unlock(&doc_lock);
                
                fclose(doc);
                break;
            }
        }
    }

    //terminate threads, destroy locks and free all resources
    pthread_mutex_lock(&term_lock);
    terminate = true;
    pthread_mutex_unlock(&term_lock);

    pthread_cancel(broadcast_thread);
    pthread_join(broadcast_thread, NULL);
    pthread_join(queue_thread, NULL);

    pthread_mutex_destroy(&doc_lock);
    pthread_mutex_destroy(&client_lock);
    pthread_mutex_destroy(&log_lock);
    pthread_mutex_destroy(&term_lock);
    pthread_mutex_destroy(&queue_lock);

    free(version_log);
    version_log = NULL;
    free(all_logs);
    all_logs = NULL;
    free(my_clients);
    my_clients = NULL;
    free(my_queue);
    my_queue = NULL;
    markdown_free(my_doc);
    my_doc = NULL;
    return 0;
}