#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "cJSON.h"

#define SERVER_PORT 8080 // Replace with the port number to listen on

// Stores configuration information
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

// Creates a TCP socket, binds to a port, and listens for incoming connections
// Returns server socket file descriptor
int bind_and_listen() {
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
    server_addr.sin_port = htons(SERVER_PORT);

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

    printf("Server listening on port %d...\n", SERVER_PORT);
    return server_sock;
}

// Accepts a client connection and returns client file descriptor
int accept_connection(int server_sock) {
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

// Parses JSON config file and returns cJSON object
cJSON* parse_config(FILE* file) {
    // Get file size
    rewind(file);
    char *json_string = NULL;
    size_t json_len;
    fseek(file, 0, SEEK_END);
    json_len = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Read file contents into string
    json_string = malloc(json_len + 1);
    if (json_string == NULL) {
        perror("Error allocating JSON string");
        fclose(file);
        return NULL;
    }
    fread(json_string, 1, json_len, file);
    json_string[json_len] = '\0';
    
    // Create cJSON object
    cJSON *json_root = cJSON_Parse(json_string);
    if (json_root == NULL) {
        fprintf(stderr, "Error parsing JSON file.\n");
        free(json_string);
        fclose(file);
        return NULL;
    }

    free(json_string);
    return json_root;
}

// Prints ConfigData struct
void print_config(ConfigData* config_data) {
    printf("Server IP Address: %s\n", config_data->server_ip_addr);
    printf("UDP Source Port: %d\n", config_data->udp_source_port);
    printf("UDP Destination Port: %d\n", config_data->udp_destination_port);
    printf("TCP Head SYN Port: %d\n", config_data->tcp_head_syn);
    printf("TCP Tail SYN Port: %d\n", config_data->tcp_tail_syn);
    printf("TCP Pre-Probe Port: %d\n", config_data->tcp_pre_probe);
    printf("TCP Post-Probe Port: %d\n", config_data->tcp_post_probe);
    printf("UDP Payload Size: %d\n", config_data->udp_payload_size);
    printf("Inter-Measurement Time: %d\n", config_data->inter_measurement_time);
    printf("Number of UDP Packets: %d\n", config_data->num_udp_packets);
    printf("TTL: %d\n", config_data->ttl);
}

// Extracts config info from cJSON object and stores it in ConfigData struct
void extract_config(ConfigData* config_data, const cJSON* json_root) {
    strncpy(config_data->server_ip_addr, cJSON_GetObjectItem(json_root, "server_ip_addr")->valuestring, sizeof(config_data->server_ip_addr));
    config_data->udp_source_port = cJSON_GetObjectItem(json_root, "udp_source_port")->valueint;
    config_data->udp_destination_port = cJSON_GetObjectItem(json_root, "udp_destination_port")->valueint;
    config_data->tcp_head_syn = cJSON_GetObjectItem(json_root, "tcp_head_syn")->valueint;
    config_data->tcp_tail_syn = cJSON_GetObjectItem(json_root, "tcp_tail_syn")->valueint;
    config_data->tcp_pre_probe = cJSON_GetObjectItem(json_root, "tcp_pre_probe")->valueint;
    config_data->tcp_post_probe = cJSON_GetObjectItem(json_root, "tcp_post_probe")->valueint;
    config_data->udp_payload_size = cJSON_GetObjectItem(json_root, "udp_payload_size")->valueint;
    config_data->inter_measurement_time = cJSON_GetObjectItem(json_root, "inter_measurement_time")->valueint;
    config_data->num_udp_packets = cJSON_GetObjectItem(json_root, "num_udp_packets")->valueint;
    config_data->ttl = cJSON_GetObjectItem(json_root, "ttl")->valueint;
    printf("JSON file received and parsed successfully.\n");
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
        int client_sock = accept_connection(server_sock);
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

int main(int argc, char *argv[]) {
    // Arg error checking
    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        return -1;
    }

    // Create socket and listen for connections
    int server_sock = bind_and_listen();
    if (server_sock < 0) {
        perror("Unable to create socket");
        return -1;
    }

    // Get config data from client
    ConfigData* config_data = get_config_data(server_sock);
    if (config_data == NULL) {
        perror("Unable to get config data");
        free(config_data);
        close(server_sock);
        return -1;
    }

    print_config(config_data);
    free(config_data);
    close(server_sock);

    return 0;
}