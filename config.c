#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "config.h"

// Parses JSON config file and returns cJSON object
cJSON* parse_config(FILE* file) {
    // Get file size
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
        perror("Error parsing JSON file.\n");
        free(json_string);
        fclose(file);
        return NULL;
    }

    free(json_string);
    return json_root;
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
