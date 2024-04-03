#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "cJSON.h"

#define SERVER_PORT 8080 // Replace with the port number to listen on

typedef struct {
    char server_ip_addr[16];
    int udp_source_port;
    int udp_destination_port;
    int tcp_head_syn;
    int tcp_tail_syn;
    int tcp_pre_probe;
    int tcp_post_probe;
    int udp_payload_size;
    int inter_measurement_time;
    int num_udp_packets;
    int ttl;
} ConfigData;

int main(int argc, char *argv[]) {
    // Create a TCP socket
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("socket");
        return 1;
    }

    // Set up the server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    // Bind the socket to the server address
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_sock);
        return 1;
    }

    // Listen for incoming connections
    if (listen(server_sock, 5) < 0) {
        perror("listen");
        close(server_sock);
        return 1;
    }

    printf("Server listening on port %d...\n", SERVER_PORT);

    while (1) {
        // Accept a client connection
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_sock < 0) {
            perror("accept");
            close(server_sock);
            return 1;
        }

        printf("Client connected from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        // Receive the JSON file from the client
        FILE *file = tmpfile();
        if (file == NULL) {
            perror("tmpfile");
            close(client_sock);
            continue;
        }

        char buffer[1024];
        ssize_t bytes_received;
        while ((bytes_received = recv(client_sock, buffer, sizeof(buffer), 0)) > 0) {
            if (fwrite(buffer, 1, bytes_received, file) != bytes_received) {
                perror("fwrite");
                fclose(file);
                close(client_sock);
                break;
            }
        }

        if (bytes_received < 0) {
            perror("recv");
        }

        // Parse the JSON file
        rewind(file);
        char *json_string = NULL;
        size_t json_len;
        fseek(file, 0, SEEK_END);
        json_len = ftell(file);
        fseek(file, 0, SEEK_SET);
        json_string = malloc(json_len + 1);
        if (json_string == NULL) {
            perror("malloc");
            fclose(file);
            close(client_sock);
            continue;
        }
        fread(json_string, 1, json_len, file);
        json_string[json_len] = '\0';

        cJSON *json_root = cJSON_Parse(json_string);
        if (json_root == NULL) {
            fprintf(stderr, "Error parsing JSON file.\n");
            free(json_string);
            fclose(file);
            close(client_sock);
            continue;
        }

        // Extract data from the JSON object
        ConfigData config_data;
        strncpy(config_data.server_ip_addr, cJSON_GetObjectItem(json_root, "server_ip_addr")->valuestring, sizeof(config_data.server_ip_addr));
        config_data.udp_source_port = cJSON_GetObjectItem(json_root, "udp_source_port")->valueint;
        config_data.udp_destination_port = cJSON_GetObjectItem(json_root, "udp_destination_port")->valueint;
        config_data.tcp_head_syn = cJSON_GetObjectItem(json_root, "tcp_head_syn")->valueint;
        config_data.tcp_tail_syn = cJSON_GetObjectItem(json_root, "tcp_tail_syn")->valueint;
        config_data.tcp_pre_probe = cJSON_GetObjectItem(json_root, "tcp_pre_probe")->valueint;
        config_data.tcp_post_probe = cJSON_GetObjectItem(json_root, "tcp_post_probe")->valueint;
        config_data.udp_payload_size = cJSON_GetObjectItem(json_root, "udp_payload_size")->valueint;
        config_data.inter_measurement_time = cJSON_GetObjectItem(json_root, "inter_measurement_time")->valueint;
        config_data.num_udp_packets = cJSON_GetObjectItem(json_root, "num_udp_packets")->valueint;
        config_data.ttl = cJSON_GetObjectItem(json_root, "ttl")->valueint;

         // Print the extracted data
        printf("Server IP Address: %s\n", config_data.server_ip_addr);
        printf("UDP Source Port: %d\n", config_data.udp_source_port);
        printf("UDP Destination Port: %d\n", config_data.udp_destination_port);
        printf("TCP Head SYN Port: %d\n", config_data.tcp_head_syn);
        printf("TCP Tail SYN Port: %d\n", config_data.tcp_tail_syn);
        printf("TCP Pre-Probe Port: %d\n", config_data.tcp_pre_probe);
        printf("TCP Post-Probe Port: %d\n", config_data.tcp_post_probe);
        printf("UDP Payload Size: %d\n", config_data.udp_payload_size);
        printf("Inter-Measurement Time: %d\n", config_data.inter_measurement_time);
        printf("Number of UDP Packets: %d\n", config_data.num_udp_packets);
        printf("TTL: %d\n", config_data.ttl);

        // Clean up
        cJSON_Delete(json_root);
        free(json_string);
        fclose(file);
        close(client_sock);

        printf("JSON file received and parsed successfully.\n");
    }

    // Close the server socket
    close(server_sock);

    return 0;
}