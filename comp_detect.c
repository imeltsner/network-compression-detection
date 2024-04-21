#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <sys/errno.h>
#include <math.h>
#include "cJSON.h"
#include "config.h"

// Reads a parses a config file
ConfigData* get_config_data(char *file_path) {
    // Open config file
    FILE *file = fopen(file_path, "rb");
    if (file == NULL) {
        perror("Error opening config file");
        exit(EXIT_FAILURE);
    }

    // Parse and extract config data
    ConfigData *config_data = malloc(sizeof(*config_data));
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

    return config_data;
}

// Reads random bytes from a file
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

// Create a udp socket and sends two packet trains
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
    source_addr.sin_addr.s_addr = INADDR_ANY;
    source_addr.sin_port = htons(config_data->udp_source_port);

    // Set DF flag
    int val = 1;
    if (setsockopt(sock, IPPROTO_IP, IP_MTU_DISCOVER, &val, sizeof(val)) < 0) {
        perror("Error setting udp socket options");
        free(config_data);
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Set TTL value
    int ttl = config_data->ttl; // Example TTL value, you can set it according to your requirements
    if (setsockopt(sock, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) < 0) {
        perror("Error setting TTL value");
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
        ssize_t bytes_sent = sendto(sock, payload, sizeof(payload), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
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

    // Parse config file info
    char *file_path = argv[1];
    ConfigData* config_data = get_config_data(file_path);

    // Send udp packet trains
    send_udp_packets(config_data);

    free(config_data);
    return 0;
}
