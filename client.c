#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#define SERVER_IP "127.0.0.1" // Replace with the IP address of the server
#define SERVER_PORT 8080 // Replace with the port number the server is listening on

int pre_probe_tcp() {
    // Create a TCP socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    // Set up the server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    server_addr.sin_port = htons(SERVER_PORT);

    // Connect to the server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        return -1;
    }

    return sock;
}

int send_config(char* file_path, int sock) {
    // Open the file
    FILE *file = fopen(file_path, "rb");
    if (file == NULL) {
        perror("Error opening config file");
        close(sock);
        return -1;
    }

    // Send the file to the server
    char buffer[1024];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (send(sock, buffer, bytes_read, 0) < 0) {
            perror("Error sending config file");
            fclose(file);
            close(sock);
            return -1;
        }
    }

    // Close the file
    fclose(file);
    printf("File sent successfully.\n");
    return sock;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <file_path>\n", argv[0]);
        return -1;
    }

    // Create socket and connect to server
    int sock = pre_probe_tcp();
    if (sock < 0) {
        perror("Error connecting to server");
        return -1;
    }

    // Send config file to server
    char *file_path = argv[1];
    sock = send_config(file_path, sock);
    if (sock < 0) {
        perror("Unable to send config file");
        return -1;
    }

    close(sock);
    return 0;
}