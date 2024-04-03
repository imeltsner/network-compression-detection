#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "cJSON.h"
#include "config.h"

// Creates a TCP socket and intializes connection
// Returns socket file descriptor
int connect_to_server(ConfigData* config_data) {
    // Create a TCP socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Error creating socket");
        return -1;
    }

    // Set up the server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(config_data->server_ip_addr);
    server_addr.sin_port = htons(config_data->tcp_pre_probe);

    // Connect to the server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        return -1;
    }

    return sock;
}

// Sends a file via TCP to the server
// Returns socket file descriptor
int send_config(FILE* file, int sock) {
    rewind(file);

    // Send the file to the server
    char buffer[1024];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (send(sock, buffer, bytes_read, 0) < 0) {
            perror("Error sending config file");
            fclose(file);
            return -1;
        }
    }

    // Close the file
    fclose(file);
    printf("File sent successfully.\n");
    return sock;
}

int main(int argc, char *argv[]) {
    // Arg error checking
    if (argc != 2) {
        printf("Usage: %s <file_path>\n", argv[0]);
        return -1;
    }

    // Open the file
    char *file_path = argv[1];
    FILE *file = fopen(file_path, "rb");
    if (file == NULL) {
        perror("Error opening config file");
        return -1;
    }

    // Parse and extract config data
    ConfigData* config_data = malloc(sizeof(config_data));
    if (config_data == NULL) {
        perror("Error allocating config data");
        return -1;
    }
    cJSON* json_root = parse_config(file);
    extract_config(config_data, json_root);


    // Create socket and connect to server
    int sock = connect_to_server(config_data);
    if (sock < 0) {
        perror("Error connecting to server");
        close(sock);
        return -1;
    }

    // Send config file to server
    sock = send_config(file, sock);
    if (sock < 0) {
        perror("Unable to send config file");
        close(sock);
        return -1;
    }

    // Close socket and exit
    close(sock);
    return 0;
}