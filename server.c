#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include "cJSON.h"
#include "config.h"

// Creates a TCP socket, binds to a port, and listens for incoming connections
// Returns server socket file descriptor
int create_tcp_socket(int port) {
    // Create a TCP socket
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Error creating socket");
        return -1;
    }

    // Set up the server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // Set socket options
    int optval = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        perror("Errror reusing address");
        return -1;
    }

    // Bind the socket to the server address
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error binding socket");
        close(server_sock);
        return -1;
    }

    // Listen for incoming connections
    if (listen(server_sock, 5) < 0) {
        perror("Error listening for connections");
        close(server_sock);
        return -1;
    }

    printf("Server listening for TCP connections on port %d...\n", port);
    return server_sock;
}

// Accepts a client connection and returns client file descriptor
int accept_tcp_connection(int server_sock) {
    // Accept a client connection
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_len);
    if (client_sock < 0) {
        return -1;
    }

    printf("Client connected from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
    return client_sock;
}

// Reads bytes of config file from client and returns file contents
FILE* read_config(int client_sock) {
    // Receive the JSON file from the client
    FILE *file = tmpfile();
    if (file == NULL) {
        perror("Error creating temp file");
        return NULL;
    }

    char buffer[1024];
    ssize_t bytes_received;
    while ((bytes_received = recv(client_sock, buffer, sizeof(buffer), 0)) > 0) {
        if (fwrite(buffer, 1, bytes_received, file) != bytes_received) {
            perror("Error receiving file bytes");
            fclose(file);
            return NULL;
        }
    }

    if (bytes_received < 0) {
        perror("Error: bytes received is negative");
        return NULL;
    }

    return file;
}

// Accepts tcp connections until config file data is successfully extracted
ConfigData* get_config_data(int server_sock) {
    // Allocate ConfigData struct
    ConfigData* config_data = malloc(sizeof(ConfigData));
    if (config_data == NULL) {
        perror("Error allocating ConfigData");
        return NULL;
    }

    while (1) {
        // Accept connection
        int client_sock = accept_tcp_connection(server_sock);
        if (client_sock < 0) {
            perror("Unable to accept connection");
            return NULL;
        }

        // Read config file from client
        FILE* file = read_config(client_sock);
        if (file == NULL) {
            perror("Unable to read config file");
            close(client_sock);
            continue;
        }

        // Parse the config file and extract data
        cJSON *json_root = parse_config(file);
        if (json_root == NULL) {
            perror("Unable to parse config file");
            close(client_sock);
            continue;
        }

        extract_config(config_data, json_root);
    
        // Clean up
        cJSON_Delete(json_root);
        fclose(file);
        close(client_sock);

        return config_data;
    }
}

// Creates a udp socket and binds to a port
// Return created udp socket
int create_udp_socket(ConfigData* config_data) {
    // Create a UDP socket
    int server_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_sock < 0) {
        perror("Error creating udp socket");
        return -1;
    }

    // Set up the server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(config_data->udp_destination_port);

    // Bind the socket to the server address
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error binding udp socket");
        close(server_sock);
        return -1;
    }

    printf("Server listening on port %d...\n", config_data->udp_destination_port);
    return server_sock;
}

double receive_packet_train(int server_sock, ConfigData* config_data) {
    char buffer[config_data->udp_payload_size];
    bzero(buffer, sizeof(buffer));

    clock_t start_low_entropy = clock();
    for (int i = 0; i < config_data->num_udp_packets; i++) {
        // Receive the UDP packet
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        ssize_t bytes_received = recvfrom(server_sock, buffer, config_data->udp_payload_size, 0, (struct sockaddr *)&client_addr, &client_addr_len);

        if (bytes_received < 0) {
            perror("Error receiving udp bytes");
            return -1;
        }
    }
    clock_t end_low_entropy = clock();
    double total_low_entropy = (double)(end_low_entropy - start_low_entropy) / CLOCKS_PER_SEC;
    printf("First packet train received in %f seconds\n", total_low_entropy);

    // sleep(config_data->inter_measurement_time);

    // clock_t start_high_entropy = clock();
    // for (int i = 0; i < config_data->num_udp_packets; i++) {
    //     // Receive the UDP packet
    //     struct sockaddr_in client_addr;
    //     socklen_t client_addr_len = sizeof(client_addr);
    //     ssize_t bytes_received = recvfrom(server_sock, buffer, config_data->udp_payload_size, 0, (struct sockaddr *)&client_addr, &client_addr_len);

    //     if (bytes_received < 0) {
    //         perror("Error receiving udp bytes");
    //         return -1;
    //     }
    // }
    // clock_t end_high_entropy = clock();
    // double total_high_entropy = (double)(end_high_entropy - start_high_entropy) / CLOCKS_PER_SEC;
    // printf("Second packet train received in %f seconds\n", total_high_entropy);

    return total_low_entropy;
}

int main(int argc, char *argv[]) {
    // Arg error checking
    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        return -1;
    }

    int port = atoi(argv[1]);

    // Create socket and listen for connections
    int tcp_server_sock = create_tcp_socket(port);
    if (tcp_server_sock < 0) {
        perror("Unable to create socket");
        return -1;
    }

    // Get config data from client
    ConfigData* config_data = get_config_data(tcp_server_sock);
    if (config_data == NULL) {
        perror("Unable to get config data");
        free(config_data);
        close(tcp_server_sock);
        return -1;
    }
    close(tcp_server_sock);

    // Get first packet train
    int udp_server_sock = create_udp_socket(config_data);
    double time_difference = receive_packet_train(udp_server_sock, config_data);
    printf("Time difference between packet trains: %f\n", time_difference);

    free(config_data);

    return 0;
}