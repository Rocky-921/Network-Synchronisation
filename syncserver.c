// Header files
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <fcntl.h>

char *root_pth;

typedef struct {
    int op;     // 0 -> create, 1 -> delete
    int type; // 0 -> folder, 1 -> file
    int len_name, len_data;
    char data[];    // name then content for file creation
} sync_data;

// map wd to path
struct wd_map{
    int wd;
    char *path;
    struct wd_map *next;
} wd_map;

typedef struct{
    int len;
    char list[];
} ignore_list_send;


struct wd_map* watch_all(char *dir_pth, int inotify_fd, struct wd_map *wd_map_head){
    struct wd_map *wd_map_new = malloc(sizeof(struct wd_map));
    wd_map_new->next = wd_map_head;
    wd_map_new->wd = inotify_add_watch(inotify_fd, dir_pth, IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO);
    wd_map_new->path = malloc(strlen(dir_pth) - strlen(root_pth) + 1);
    if(dir_pth == root_pth){
        strcpy(wd_map_new->path, "");
    }else{
        strcpy(wd_map_new->path, dir_pth+strlen(root_pth)+1);
    }
    wd_map_head = wd_map_new;
    struct dirent *entry;
    DIR *dir = opendir(dir_pth);
    struct stat file_stat;

    while((entry = readdir(dir)) != NULL){
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0){
            continue;
        }
        int pth_len = strlen(dir_pth) + strlen(entry->d_name) + 2;
        char curr_pth[pth_len];
        snprintf(curr_pth, pth_len, "%s/%s", dir_pth, entry->d_name);
        
        stat(curr_pth, &file_stat);
        
        if(S_ISDIR(file_stat.st_mode)){
            wd_map_head = watch_all(curr_pth, inotify_fd, wd_map_head);
        }
    }
    closedir(dir);
    return wd_map_head;
}

char *get_path_from_wd(int wd, struct wd_map *wd_map_head){
    while(wd_map_head != NULL){
        if(wd_map_head->wd == wd){
            return wd_map_head->path;
        }
        wd_map_head = wd_map_head->next;
    }
    return NULL;
}

int ext_in_ignore_list(char *name, char **ignore_list, int ignore_size){
    int ignore = 0;
    int b=strlen(name);
    for(int i=0; i<ignore_size; i++){
        int a=strlen(ignore_list[i]);
        if(a > b){
            continue;
        }
        if(name[b-a] != '.'){
            continue;
        }
        for(int j=a-1; j>=0; j--){
            if(ignore_list[i][j] != name[b-a+j]){
                break;
            }
            if(j == 0){
                return ignore=1;
            }
        }
    }
    
    return ignore;
}

void send_create(int client_fd, char *path, char *full_path, char **ignore_list, int ignore_size){
    // check if path is folder or file
    struct stat file_stat;
    stat(full_path, &file_stat);
    if(S_ISDIR(file_stat.st_mode)){
        // send folder creation

        if(strcmp(path, "") != 0){
            printf("Sending folder creation event: %s\n", path);
            int ttl_send = sizeof(sync_data) + strlen(path);
            sync_data *data = malloc(ttl_send+1);
            data->op = 0;
            data->type = 0;
            data->len_name = strlen(path);
            data->len_data = 0;
            strcpy(data->data, path);
            send(client_fd, data, ttl_send, 0);
        }
        struct dirent *entry;
        DIR *dir = opendir(full_path);
        struct stat file_stat;

        while((entry = readdir(dir)) != NULL){
            if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0){
                continue;
            }
            int pth_len = strlen(path) + strlen(entry->d_name) + 2;
            char curr_pth[pth_len];
            snprintf(curr_pth, pth_len, "%s/%s", path, entry->d_name);
            pth_len = strlen(full_path) + strlen(entry->d_name) + 2;
            char curr_full_pth[pth_len];
            snprintf(curr_full_pth, pth_len, "%s/%s", full_path, entry->d_name);
            send_create(client_fd, curr_pth, curr_full_pth, ignore_list, ignore_size);
        }
    }else{
        if(ext_in_ignore_list(path, ignore_list, ignore_size)){
            printf("Ignoring this file\n");
            return;
        }
        // send file creation
        printf("Sending file creation event: %s\n", full_path);
        FILE *file = fopen(full_path, "rb");
        if(file == NULL){
            perror("Error opening file");
            return;
        }
        fseek(file, 0, SEEK_END);
        int file_size = ftell(file);
        int ttl_send = sizeof(sync_data) + strlen(path) + file_size;
        sync_data *data = malloc(ttl_send+1);
        data->op = 0;
        data->type = 1;
        data->len_name = strlen(path);
        data->len_data = file_size;
        strcpy(data->data, path);
        fseek(file, 0, SEEK_SET);
        fread(data->data+data->len_name, 1, file_size, file);
        fclose(file);
        send(client_fd, data, ttl_send, 0);

    }
}

struct wd_map *handle_event(struct inotify_event *event, int client_fd, struct wd_map *wd_map_head, char **ignore_list, int ignore_size, int inotify_fd){
    if(!(event->mask & IN_ISDIR) && ext_in_ignore_list(event->name, ignore_list, ignore_size)){
        printf("Ignoring this event\n");
        return wd_map_head;
    }
    printf("event: %s\n", event->name);
    char *path = get_path_from_wd(event->wd, wd_map_head);
    int event_path_len;
    if(strcmp(path, "") == 0){
        event_path_len = event->len + 1;
    }else{
        event_path_len = strlen(path) + event->len + 2;
    }
    char event_path[event_path_len];
    if(strcmp(path, "") == 0){
        snprintf(event_path, event_path_len, "%s", event->name);
    }else{
        snprintf(event_path, event_path_len, "%s/%s", path, event->name);
    }
    printf("event_path: %s\n", event_path);
    event_path_len = strlen(root_pth) + strlen(event_path) + 2;
    char full_path[event_path_len];
    snprintf(full_path, event_path_len, "%s/%s", root_pth, event_path);
    printf("full_path: %s\n", full_path);
    if(event->mask & IN_CREATE || event->mask & IN_MOVED_TO){
        printf("creating watch\n");
        if(event->mask & IN_ISDIR){
            wd_map_head = watch_all(full_path, inotify_fd, wd_map_head);
        }
        send_create(client_fd,event_path,full_path, ignore_list, ignore_size);
    }else if(event->mask & IN_DELETE || event->mask & IN_MOVED_FROM){
        printf("Sending delete event\n");
        int ttl_send = sizeof(sync_data) + strlen(event_path);
        sync_data *data = malloc(ttl_send+1);
        data->op = 1;
        
        if(event->mask & IN_ISDIR){
            data->type = 0;
        }
        else{
            data->type = 1;
        }
        data->len_name = strlen(event_path);
        data->len_data = 0;
        strcpy(data->data, event_path);
        send(client_fd, data, ttl_send, 0);
    }
    return wd_map_head;
}

void *handle_client(void* fd){
    int client_fd = *((int *)fd);

printf("Client thread started for fd: %d\n", client_fd);
printf("Receiving ignore list...\n");
    int ignore_list_data_size;
    recv(client_fd, &ignore_list_data_size, sizeof(int), 0);
    char ignore_list_data[ignore_list_data_size];
    int bytes_received = 0;
    while(bytes_received < ignore_list_data_size){
        bytes_received += recv(client_fd, ignore_list_data+bytes_received, ignore_list_data_size-bytes_received, 0);
    }
    
    int ignore_size = 0;
    for(int i=0; i<ignore_list_data_size; i++){
        if(ignore_list_data[i] == ','){
            ignore_size++;
        }
    }
    if(ignore_size>0){
        ignore_size++;
    }else if(ignore_list_data_size > 0){
        ignore_size = 1;
    }

    char *ignore_list[ignore_size];
    ignore_size = 0;
    char buffer[256];      // max 255 size of .extension_name
    int j = 0;
    for(int i=0; i<ignore_list_data_size; i++){
        if(ignore_list_data[i] == ','){
            buffer[j] = '\0';
            ignore_list[ignore_size] = malloc((j+1) * sizeof(char));
            strcpy(ignore_list[ignore_size], buffer);
            ignore_size++;
            j = 0;
        }else{
            buffer[j] = ignore_list_data[i];
            j++;
        }
    }

    if(j > 0){
        buffer[j] = '\0';
        ignore_list[ignore_size] = malloc((j+1) * sizeof(char));
        strcpy(ignore_list[ignore_size], buffer);
        ignore_size++;
    }

printf("Received ignore list of size %d\n", ignore_size);
    for(int i=0; i<ignore_size; i++){
        printf("%s\n", ignore_list[i]);
    }

    int inotify_fd = inotify_init();
printf("watch starting...\n");
    struct wd_map *wd_map_head = NULL;
    wd_map_head = watch_all(root_pth, inotify_fd, wd_map_head);

    printf("sending current files...\n");
    struct dirent *entry;
    DIR *dir = opendir(root_pth);
    struct stat file_stat;

    while((entry = readdir(dir)) != NULL){
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0){
            continue;
        }
        
        int pth_len = strlen(root_pth) + strlen(entry->d_name) + 2;
        char curr_full_pth[pth_len];
        snprintf(curr_full_pth, pth_len, "%s/%s", root_pth, entry->d_name);
        
        struct stat file_stat;
        stat(curr_full_pth, &file_stat);
        if(!S_ISDIR(file_stat.st_mode) && ext_in_ignore_list(entry->d_name, ignore_list, ignore_size)){
            continue;
        }
        
        pth_len = strlen(entry->d_name) + 1;
        char curr_pth[pth_len];
        snprintf(curr_pth, pth_len, "%s", entry->d_name);
        send_create(client_fd, curr_pth, curr_full_pth, ignore_list, ignore_size);
    }
    printf("all files sent\n");
    

    while(1){
        printf("Waiting for event...\n");
        char buffer[4096]; // Large buffer to hold multiple events
        int length = read(inotify_fd, buffer, 4096); // Read all available events
                
        struct inotify_event *event;
        for (char *ptr = buffer; ptr < buffer + length; ptr += sizeof(struct inotify_event) + event->len) {
            event = (struct inotify_event *)ptr;
            wd_map_head = handle_event(event, client_fd, wd_map_head, ignore_list, ignore_size, inotify_fd);
        }
        

    }



    close(inotify_fd);
}

// $./syncserver path_to_local_directory port max_clients

int main(int argc, char *argv[]){

    if(argc != 4){
        printf("Usage: %s <path_to_local_directory> <port> <max_clients>\n", argv[0]);
        return 1;
    }

printf("Starting Server...\n");

    int server_fd, client_fd;
    struct sockaddr_in server_addr;

    int addrlen = sizeof(server_addr);
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, '\0', sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(atoi(argv[2]));

printf("Starting bind\n");

    if(bind(server_fd,(const struct sockaddr *) &server_addr, sizeof(server_addr)) < 0){
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    int mx_clients = atoi(argv[3]);
printf("listening on port %s\n", argv[2]);
    
    if(listen(server_fd, mx_clients) < 0){
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    
    root_pth = argv[1];
    
    if(root_pth[strlen(root_pth)-1] == '/'){
        root_pth[strlen(root_pth)-1] = '\0';
    }
        
    pthread_t thread_id[mx_clients];
    int id = 0;
printf("Waiting for client...\n");
    while(1){
        struct sockaddr_in client_addr;
        unsigned int addrlen = sizeof(client_addr);
        client_fd = accept(server_fd, (struct sockaddr * restrict) &client_addr, (socklen_t *)&addrlen);
        if(client_fd < 0){
            perror("Accept failed");
            exit(EXIT_FAILURE);
        }
        if(id == mx_clients){
            exit(1);    // Error: too many clients
        }
        printf("Connected to client with ip: %s, fd: %d\n", inet_ntoa(client_addr.sin_addr), client_fd);

        int *tmp = malloc(sizeof(int));
        *tmp = client_fd;
        pthread_create(&thread_id[id], NULL, handle_client, (void *)tmp);
        pthread_detach(thread_id[id]);
        id++;
    }
}