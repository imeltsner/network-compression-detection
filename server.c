#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <sys/errno.h>
#include "cJSON.h"
#include "config.h"

// Creates a TCP socket, binds to a port, and listens for incoming connections
// Returns server socket file descriptor
int create_tcp_socket(int port) {
    // Create a TCP socket
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
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
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    // Bind the socket to the server address
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error binding socket");
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_sock, 5) < 0) {
        perror("Error listening for connections");
        close(server_sock);
        exit(EXIT_FAILURE);
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
        close(server_sock);
        exit(EXIT_FAILURE);
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
        exit(EXIT_FAILURE);
    }

    char buffer[1024];
    ssize_t bytes_received;
    while ((bytes_received = recv(client_sock, buffer, sizeof(buffer), 0)) > 0) {
        if (fwrite(buffer, 1, bytes_received, file) != bytes_received) {
            perror("Error receiving file bytes");
            fclose(file);
            exit(EXIT_FAILURE);
        }
    }

    if (bytes_received < 0) {
        perror("Error: bytes received is negative");
        exit(EXIT_FAILURE);
    }

    return file;
}

// Accepts tcp connections until config file data is successfully extracted
ConfigData* get_config_data(int server_sock) {
    // Allocate ConfigData struct
    ConfigData* config_data = malloc(sizeof(ConfigData));
    if (config_data == NULL) {
        perror("Error allocating ConfigData");
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    while (1) {
        // Accept connection
        int client_sock = accept_tcp_connection(server_sock);
        if (client_sock < 0) {
            perror("Unable to accept connection");
            close(server_sock);
            free(config_data);
            exit(EXIT_FAILURE);
        }

        // Read config file from client
        FILE* file = read_config(client_sock);
        if (file == NULL) {
            perror("Unable to read config file");
            close(server_sock);
            close(client_sock);
            free(config_data);
            exit(EXIT_FAILURE);
        }

        // Parse the config file and extract data
        cJSON *json_root = parse_config(file);
        if (json_root == NULL) {
            perror("Unable to parse config file");
            close(server_sock);
            close(client_sock);
            free(config_data);
            exit(EXIT_FAILURE);
        }

        extract_config(config_data, json_root);
    
        // Clean up
        cJSON_Delete(json_root);
        fclose(file);
        close(client_sock);
        close(server_sock);
        return config_data;
    }
}

// Gets difference between two timevals
int timeval_subtract (struct timeval *result, struct timeval *x, struct timeval *y) {
    /* Perform the carry for the later subtraction by updating y. */
    if (x->tv_usec < y->tv_usec) {
        int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
        y->tv_usec -= 1000000 * nsec;
        y->tv_sec += nsec;
    }
    if (x->tv_usec - y->tv_usec > 1000000) {
        int nsec = (x->tv_usec - y->tv_usec) / 1000000;
        y->tv_usec += 1000000 * nsec;
        y->tv_sec -= nsec;
    }

    // Compute the time remaining to wait. tv_usec is certainly positive.
    result->tv_sec = x->tv_sec - y->tv_sec;
    result->tv_usec = x->tv_usec - y->tv_usec;

    /* Return 1 if result is negative. */
    return x->tv_sec < y->tv_sec;
}

double receive_packet_train(ConfigData* config_data) {
    // Create a UDP socket
    int server_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_sock < 0) {
        perror("Error creating udp socket");
        free(config_data);
        exit(EXIT_FAILURE);
    }

    // Set up the server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(config_data->server_ip_addr);
    server_addr.sin_port = htons(config_data->udp_destination_port);
    socklen_t server_addr_len = sizeof(server_addr);

    // Bind the socket to the server address
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error binding udp socket");
        close(server_sock);
        free(config_data);
        exit(EXIT_FAILURE);
    }

    // Set timeout for socket
    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    if (setsockopt(server_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("Unable to set timeout");
        close(server_sock);
        free(config_data);
        exit(EXIT_FAILURE);
    }

    // Initialize payload buffer
    char buffer[config_data->udp_payload_size];
    bzero(buffer, sizeof(buffer));

    // Receive packets
    struct timeval low_entropy_start, low_entropy_end, low_entropy_elapsed;
    gettimeofday(&low_entropy_start, NULL);
    while (1) {
        ssize_t bytes_received = recvfrom(server_sock, buffer, config_data->udp_payload_size, 0, (struct sockaddr *)&server_addr, &server_addr_len);
        if (bytes_received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            else {
                perror("Error receiving bytes");
                close(server_sock);
                free(config_data);
                exit(EXIT_FAILURE);
            }
        }
        gettimeofday(&low_entropy_end, NULL);
    }
    timeval_subtract(&low_entropy_elapsed, &low_entropy_end, &low_entropy_start);
    printf("First packet train received in %ld.%06d seconds\n", low_entropy_elapsed.tv_sec, low_entropy_elapsed.tv_usec);
    double low_entropy_time = low_entropy_elapsed.tv_sec + 1e-6 * low_entropy_elapsed.tv_usec;

    sleep(config_data->inter_measurement_time);

    struct timeval high_entropy_start, high_entropy_end, high_entropy_elapsed;
    gettimeofday(&high_entropy_start, NULL);
    while (1) {
        ssize_t bytes_received = recvfrom(server_sock, buffer, config_data->udp_payload_size, 0, (struct sockaddr *)&server_addr, &server_addr_len);
        if (bytes_received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            else {
                perror("Error receiving bytes");
                close(server_sock);
                free(config_data);
                exit(EXIT_FAILURE);
            }
        }
        gettimeofday(&high_entropy_end, NULL);
    }
    timeval_subtract(&high_entropy_elapsed, &high_entropy_end, &high_entropy_start);
    printf("Second packet train received in %ld.%06d seconds\n", high_entropy_elapsed.tv_sec, high_entropy_elapsed.tv_usec);
    double high_entropy_time = high_entropy_elapsed.tv_sec + 1e-6 * high_entropy_elapsed.tv_usec;

    close(server_sock);
    return (high_entropy_time - low_entropy_time) * 1000;
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

    // Get config data from client
    ConfigData* config_data = get_config_data(tcp_server_sock);

    // Get packet trains
    double time_difference = receive_packet_train(config_data);
    printf("Time difference between packet trains: %f milliseconds\n", time_difference);

    free(config_data);

    return 0;
}
