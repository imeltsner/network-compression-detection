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

cJSON* parse_config(FILE* file);
void extract_config(ConfigData* config_data, const cJSON* json_root);
void print_config(ConfigData* config_data);