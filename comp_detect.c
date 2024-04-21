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

int main(int argc, char *argv[]) {
    // Arg error checking
    if (argc != 2) {
        printf("Usage: %s <file_path>\n", argv[0]);
        return -1;
    }

    // Send config info to server and parse config file info
    char *file_path = argv[1];
    ConfigData* config_data = get_config_data(file_path);

    free(config_data);
    return 0;
}
