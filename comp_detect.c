#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/errno.h>
#include <math.h>
#include <netdb.h>            
#include <sys/types.h>        
#include <sys/socket.h>       
#include <netinet/in.h>       
#include <netinet/ip.h>       
#include <netinet/tcp.h>      
#include <arpa/inet.h>        
#include <sys/ioctl.h>
#include <pthread.h>      
#include <errno.h>    
#include "cJSON.h"
#include "config.h"

unsigned short tcp_checksum(struct iphdr *iph, struct tcphdr *tcph) {
    unsigned long sum = 0;
    unsigned short *ptr;
    int tcplen = ntohs(iph->tot_len) - iph->ihl * 4;
    int i;

    // Pseudo-header checksum
    sum += (iph->saddr >> 16) & 0xFFFF;
    sum += iph->saddr & 0xFFFF;
    sum += (iph->daddr >> 16) & 0xFFFF;
    sum += iph->daddr & 0xFFFF;
    sum += htons(IPPROTO_TCP);
    sum += htons(tcplen);

    // TCP header checksum
    ptr = (unsigned short *)tcph;
    for (i = tcplen; i > 1; i -= 2)
        sum += *ptr++;
    if (i == 1)
        sum += *((unsigned char *)ptr);

    // Fold 32-bit sum to 16 bits
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return (unsigned short)(~sum);
}

unsigned short ip_checksum(struct iphdr *iph) {
    unsigned long sum = 0;
    unsigned short *ptr;

    // IP header checksum
    ptr = (unsigned short *)iph;
    for (int i = iph->ihl * 2; i > 0; i--)
        sum += *ptr++;
    
    // Fold 32-bit sum to 16 bits
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return (unsigned short)(~sum);
}

// Function to create and send a SYN packet
void send_syn_packet(ConfigData *config_data, int destination_port) {
    int sockfd;
    struct sockaddr_in dest;
    char packet[4096]; // Raw packet buffer

    // Create raw socket
    if ((sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW)) < 0) {
        perror("Error creating raw socket");
        exit(EXIT_FAILURE);
    }

    // Zero out the packet buffer
    memset(packet, 0, sizeof(packet));

    // IP header
    struct iphdr *ip = (struct iphdr *)packet;
    ip->ihl = 5;
    ip->version = 4;
    ip->tos = 0;
    ip->tot_len = htons(sizeof(struct iphdr) + sizeof(struct tcphdr));
    ip->id = htons(54321);
    ip->frag_off = 0;
    ip->ttl = 255;
    ip->protocol = IPPROTO_TCP;
    ip->check = 0; // Will be filled later
    ip->saddr = inet_addr("192.168.128.2");
    ip->daddr = inet_addr(config_data->server_ip_addr);

    // TCP header
    struct tcphdr *tcp = (struct tcphdr *)(packet + sizeof(struct iphdr));
    tcp->source = htons(12345);
    tcp->dest = htons(destination_port);
    tcp->seq = 0;
    tcp->ack_seq = 0;
    tcp->doff = 5;
    tcp->fin = 0;
    tcp->syn = 1; // Set SYN flag
    tcp->rst = 0;
    tcp->psh = 0;
    tcp->ack = 0;
    tcp->urg = 0;
    tcp->window = htons(5840);
    tcp->check = 0; // Will be filled later
    tcp->urg_ptr = 0;

    // IP checksum
    ip->check = ip_checksum(ip);

    // TCP checksum
    tcp->check = tcp_checksum(ip, tcp);

    // Send packet
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = ip->daddr;
    ssize_t bytes_sent = sendto(sockfd, packet, ntohs(ip->tot_len), 0, (struct sockaddr *)&dest, sizeof(dest));
    if (bytes_sent < 0) {
        perror("Error sending SYN packet\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Close socket
    close(sockfd);
}

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
void send_packets(ConfigData* config_data) {
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
    
    // Send head SYN packet
    send_syn_packet(config_data, config_data->tcp_head_syn);

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

    // Send tail SYN
    send_syn_packet(config_data, config_data->tcp_tail_syn);

    sleep(config_data->inter_measurement_time);

    // Get random bytes
    memset(payload, 0, sizeof(payload));
    get_random_bytes(payload, config_data);

    // Send head SYN
    send_syn_packet(config_data, config_data->tcp_head_syn);

    // Send high entropy packets
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
    
    // Send tail SYN
    send_syn_packet(config_data, config_data->tcp_tail_syn);

    close(sock);
}

// https://www.gnu.org/software/libc/manual/html_node/Calculating-Elapsed-Time.html
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

void* listen_for_rst(void *arg) {
    ConfigData *config_data = (ConfigData *) arg;

    // Create socket
    int sock_fd = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
    if (sock_fd < 0) {
        perror("Error creating listen socket\n");
        exit(EXIT_FAILURE);
    }

    // Set socket to receive all TCP packets
    int val = 1;
    if (setsockopt(sock_fd, IPPROTO_IP, IP_HDRINCL, &val, sizeof(val)) < 0) {
        perror("Error setting listen socket options\n");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    socklen_t server_size = sizeof(server_addr);
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind the socket
    if (bind(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error binding listen socket\n");
        exit(EXIT_FAILURE);
    }

    // Listen for RST packets
    struct timeval first_rst_time, second_rst_time, elapsed_rst_time;
    int num_rst_received = 0;
    double low_entropy_diff, high_entropy_diff;

    while (1) {
        struct iphdr *ip_header;
        struct tcphdr *tcp_header;
        char buffer[65536];

        ssize_t bytes_received = recvfrom(sock_fd, buffer, sizeof(buffer), 0, (struct sockaddr*) &server_addr, &server_size);
        if (bytes_received < 0) {
            perror("Error receiving RST packets\n");
            exit(EXIT_FAILURE);
        }

        ip_header = (struct iphdr *)buffer;
        tcp_header = (struct tcphdr *)(buffer + (ip_header->ihl * 4));

        if (tcp_header->rst) {
            if (num_rst_received == 0 || num_rst_received == 2) {
                gettimeofday(&first_rst_time, NULL);
            } else if (num_rst_received == 1) {
                gettimeofday(&second_rst_time, NULL);
                timeval_subtract(&elapsed_rst_time, &first_rst_time, &second_rst_time);
                low_entropy_diff = (elapsed_rst_time.tv_sec + 1e-6 * elapsed_rst_time.tv_usec) * 1000;
            } else if (num_rst_received == 3) {
                gettimeofday(&second_rst_time, NULL);
                timeval_subtract(&elapsed_rst_time, &first_rst_time, &second_rst_time);
                high_entropy_diff = (elapsed_rst_time.tv_sec + 1e-6 * elapsed_rst_time.tv_usec) * 1000;
                break;
            }
        }

        num_rst_received++;
    }

    if (fabs(high_entropy_diff - low_entropy_diff) > 100) {
        printf("Compression detected\n");
    } else {
        printf("No compression detected\n");
    }

    close(sock_fd);
    pthread_exit(NULL);
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

    // Create listener thread
    pthread_t listenThread;
    if (pthread_create(&listenThread, NULL, listen_for_rst, (void*)&config_data) != 0) {
        perror("Error creating listen thread");
        exit(EXIT_FAILURE);
    }

    // Send packets
    send_packets(config_data);

    // Join listener thread
    if (pthread_join(listenThread, NULL) != 0) {
        perror("Error joining listen thread");
        exit(EXIT_FAILURE);
    }

    free(config_data);
    return 0;
}
