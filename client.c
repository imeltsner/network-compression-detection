#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/ip.h>                
#include <netinet/in.h>
#include "cJSON.h"
#include "config.h"

// Creates a TCP socket and intializes connection
// Returns socket file descriptor
int connect_to_server(ConfigData* config_data, int pre_probe) {
    // Create a TCP socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Error creating socket");
        free(config_data);
        exit(EXIT_FAILURE);
    }

    // Set up the server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(config_data->server_ip_addr);
    if (pre_probe) {
        server_addr.sin_port = htons(config_data->tcp_pre_probe);
    } else {
        server_addr.sin_port = htons(config_data->tcp_post_probe);
    }

    // Connect to the server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        free(config_data);
        exit(EXIT_FAILURE);
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
            exit(EXIT_FAILURE);
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
        exit(EXIT_FAILURE);
    }

    // Parse and extract config data
    ConfigData* config_data = malloc(sizeof(config_data));
    if (config_data == NULL) {
        perror("Error allocating config data");
        exit(EXIT_FAILURE);
    }
    cJSON* json_root = parse_config(file);
    if (json_root == NULL) {
        perror("Unnable to parse config file");
        free(config_data);
        exit(EXIT_FAILURE);
    }
    extract_config(config_data, json_root);

    // Create socket and connect to server
    int sock = connect_to_server(config_data, 1);
    if (sock < 0) {
        free(config_data);
        perror("Error connecting to server");
        exit(EXIT_FAILURE);
    }

    // Send config file to server
    sock = send_config(file, sock);
    if (sock < 0) {
        perror("Unable to send config file");
        exit(EXIT_FAILURE);
    }

    close(sock);
    return config_data;
}

void get_random_bytes(char buffer[], ConfigData* config_data) {
    FILE* fp = fopen("random_file", "rb");
    if (fp == NULL) {
        perror("Error opening random_file");
        free(config_data);
        exit(EXIT_FAILURE);
    }

    ssize_t result = fread(buffer, 1, config_data->udp_payload_size, fp);
    if (result < 0) {
        perror("Error reading random bytes");
        fclose(fp);
        free(config_data);
        exit(EXIT_FAILURE);
    }

    fclose(fp);
}

// Create a udp socket
void send_udp_packets(ConfigData* config_data) {
    // Create a UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Error creating udp socket");
        free(config_data);
        exit(EXIT_FAILURE);
    }

    // Set the source port
    struct sockaddr_in source_addr;
    memset(&source_addr, 0, sizeof(source_addr));
    source_addr.sin_family = AF_INET;
    source_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Change this to client IP
    source_addr.sin_port = htons(config_data->udp_source_port);

    // Set DF flag
    int val = 1;
    if (setsockopt(sock, IPPROTO_IP, IP_DONTFRAG, &val, sizeof(val)) < 0) {
        perror("Error setting udp socket options");
        free(config_data);
        close(sock);
        exit(EXIT_FAILURE);
    }

    if (bind(sock, (struct sockaddr *)&source_addr, sizeof(source_addr)) < 0) {
        perror("Error binding udp socket");
        free(config_data);
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Set up the server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(config_data->server_ip_addr);
    server_addr.sin_port = htons(config_data->udp_destination_port);

    // Configure payload
    char payload[config_data->udp_payload_size];
    memset(payload, 0, sizeof(payload));

    // Send low entropy packets
    int packet_id;
    for (int i = 0; i < config_data->num_udp_packets; i++) {
        packet_id = i;
        payload[0] = (packet_id >> 8) & 0xFF; // Most significant
        payload[1] = packet_id & 0xFF;        // Least significant
        ssize_t bytes_sent = sendto(sock, payload, sizeof(payload), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
        if (bytes_sent < 0) {
            perror("Error sending packets");
            free(config_data);
            close(sock);
            exit(EXIT_FAILURE);
        }
    }
    printf("Low entropy packets sent\n");

    sleep(config_data->inter_measurement_time);

    // Send high entropy packets
    memset(payload, 0, sizeof(payload));
    get_random_bytes(payload, config_data);

    for (int i = 0; i < config_data->num_udp_packets; i++) {
        packet_id = i;
        payload[0] = (packet_id >> 8) & 0xFF;
        payload[1] = packet_id & 0xFF;
        ssize_t bytes_sent = sendto(sock, payload, strlen(payload), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
        if (bytes_sent < 0) {
            perror("Error sending packets");
            free(config_data);
            close(sock);
            exit(EXIT_FAILURE);
        }
    }
    printf("High entropy packets sent\n");

    close(sock);
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

    // Send first packet train
    send_udp_packets(config_data);
    printf("Packets sent successfully");

    // int sock = connect_to_server(config_data, 0);
    // if (sock < 0) {
    //     perror("Post probe failed");
    //     close(sock);
    //     return -1;
    // }

    free(config_data);
    return 0;
}