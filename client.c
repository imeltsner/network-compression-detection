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
        free(config_data);
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
        free(config_data);
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

ConfigData* tcp_pre_probe(char* file_path) {
    // Open config file
    FILE *file = fopen(file_path, "rb");
    if (file == NULL) {
        perror("Error opening config file");
        return NULL;
    }

    // Parse and extract config data
    ConfigData* config_data = malloc(sizeof(config_data));
    if (config_data == NULL) {
        perror("Error allocating config data");
        return NULL;
    }
    cJSON* json_root = parse_config(file);
    if (json_root == NULL) {
        perror("Unnable to parse config file");
        free(config_data);
        return NULL;
    }
    extract_config(config_data, json_root);

    // Create socket and connect to server
    int sock = connect_to_server(config_data);
    if (sock < 0) {
        free(config_data);
        perror("Error connecting to server");
        return NULL;
    }

    // Send config file to server
    sock = send_config(file, sock);
    if (sock < 0) {
        perror("Unable to send config file");
        return NULL;
    }

    close(sock);
    return config_data;
}

// Create a udp socket
int send_udp_packets(ConfigData* config_data) {
    // Create a UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    // Set up the server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(config_data->server_ip_addr);
    server_addr.sin_port = htons(config_data->udp_source_port);

    // Send low entropy payload
    char payload[config_data->udp_payload_size];
    bzero(payload, sizeof(payload));

    for (int i = 0; i < config_data->num_udp_packets; i++) {
        ssize_t bytes_sent = sendto(sock, payload, strlen(payload), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
        if (bytes_sent < 0) {
            perror("Error sending low entropy packets");
            close(sock);
            return -1;
        }
    }
}

int main(int argc, char *argv[]) {
    // Arg error checking
    if (argc != 2) {
        printf("Usage: %s <file_path>\n", argv[0]);
        return -1;
    }

    // Send config info to server and parse config file info
    char *file_path = argv[1];
    ConfigData* config_data = tcp_pre_probe(file_path);
    if (config_data == NULL) {
        perror("Pre probe failed");
        free(config_data);
        return -1;
    }

    free(config_data);
    return 0;
}