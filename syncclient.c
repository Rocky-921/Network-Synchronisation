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
} sync_data;

typedef struct{
    int len;
    char list[];
} ignore_list_send;


void remove_directory(char *path) {
    DIR *dir = opendir(path);
    if (!dir) {
        perror("opendir");
        return;
    }

    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        int pth_len = strlen(path) + strlen(entry->d_name) + 2;
        char full_path[pth_len];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        struct stat statbuf;
        if (stat(full_path, &statbuf) == 0) {
            if (S_ISDIR(statbuf.st_mode)) {
                remove_directory(full_path);  // Recursive call for subdirectory
            } else {
                remove(full_path);  // Remove file
            }
        }
    }

    closedir(dir);
    rmdir(path);  // Remove the empty directory itself
}


// $./syncclient path_to_local_directory path_to_ignore_list_file ip port
int main(int argc, char *argv[]){

    if(argc != 5){
        printf("Usage: %s <path_to_local_directory> <path_to_ignore_list_file> <ip> <port>\n", argv[0]);
        return 1;
    }
    int server_fd, client_fd;
    struct sockaddr_in server_addr;

    unsigned int addrlen = sizeof(server_addr);
    char buffer[100] = {0};
    client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd == -1) {
        perror("Socket creation failed");
        return 1;
    }

    memset(&server_addr, '\0', sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[4]));
    inet_pton(AF_INET,argv[3], &server_addr.sin_addr);
printf("Connecting to server...\n");
    if(connect(client_fd, (struct sockaddr * restrict) &server_addr, (socklen_t)addrlen) < 0){
        perror("Connection failed");
        return 1;
    }
printf("Connected to server\n");


    root_pth = argv[1];
    if(root_pth[strlen(root_pth)-1] == '/'){
        root_pth[strlen(root_pth)-1] = '\0';
    }

printf("Sending ignore list\n");

    FILE *file = fopen(argv[2], "r");
    if(file == NULL){
        perror("Error opening file");
        return 1;
    }
    fseek(file, 0, SEEK_END);
    int file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    ignore_list_send *ignore_list = malloc(sizeof(ignore_list_send) + file_size);
    ignore_list->len = file_size;
    fread(ignore_list->list, 1, file_size, file);
    fclose(file);
    send(client_fd, ignore_list, sizeof(ignore_list_send) + file_size, 0);

printf("Ignore list sent\n");

    while(1){
        // wait for data from server
        printf("Waiting for server...\n");
        int bytes_received = 0;
        char *header_char = malloc(sizeof(sync_data));
        while(bytes_received < sizeof(sync_data)){
            bytes_received += recv(client_fd, header_char+bytes_received, sizeof(sync_data)-bytes_received, 0);
        }
        sync_data *header = (sync_data *)header_char;
        char name[header->len_name+1];
        char data[header->len_data+1];
        bytes_received = 0;
        while(bytes_received < header->len_name){
            bytes_received += recv(client_fd, name+bytes_received, header->len_name-bytes_received, 0);
        }

        bytes_received = 0;
        
        while(bytes_received < header->len_data){
            bytes_received += recv(client_fd, data+bytes_received, header->len_data-bytes_received, 0);
        }

        
        name[header->len_name] = '\0';
        data[header->len_data] = '\0';
    printf("Received event: %s\n", name);
        int full_path_len = strlen(root_pth) + strlen(name) + 2;
        char *full_path = malloc(full_path_len);
        snprintf(full_path, full_path_len, "%s/%s", root_pth, name);
        if(header->type==0){ // folder
            if(header->op == 0){
                // create folder
                printf("Creating folder: %s\n", full_path);
                mkdir(full_path, 0777);
            }else{
                // delete folder
                printf("Deleting folder: %s\n", full_path);
                remove_directory(full_path);
            }

        }else{  // file
            if(header->op == 0){
                // create file
                printf("Creating file: %s\n", name);
                FILE *file = fopen(full_path, "wb");
                if(file == NULL){
                    perror("Error opening file");
                    return 1;
                }
                fwrite(data, 1, header->len_data, file);
                fclose(file);
            }else{
                // delete file
                printf("Deleting file: %s\n", full_path);
                remove(full_path);
            }
        }
    }
}