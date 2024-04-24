#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/errno.h>
#include <math.h>
#include "cJSON.h"
#include "config.h"
#include <netdb.h>            // struct addrinfo
#include <sys/types.h>        // needed for socket(), uint8_t, uint16_t, uint32_t
#include <sys/socket.h>       // needed for socket()
#include <netinet/in.h>       // IPPROTO_RAW, IPPROTO_IP, IPPROTO_TCP, INET_ADDRSTRLEN
#include <netinet/ip.h>       // struct ip and IP_MAXPACKET (which is 65535)
#define __FAVOR_BSD           // Use BSD format of tcp header
#include <netinet/tcp.h>      // struct tcphdr
#include <arpa/inet.h>        // inet_pton() and inet_ntop()
#include <sys/ioctl.h>        // macro ioctl is defined
#include <bits/ioctls.h>      // defines values for argument "request" of ioctl.
#include <net/if.h>           // struct ifreq
#include <errno.h>            // errno, perror()

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
    ip->saddr = inet_addr("0.0.0.0");
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
    int bytes_sent = sendto(sockfd, packet, ntohs(ip->tot_len), 0, (struct sockaddr *)&dest, sizeof(dest));
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

    // Send packets
    send_syn_packet(config_data, config_data->tcp_head_syn);
    send_udp_packets(config_data);
    send_syn_packet(config_data, config_data->tcp_tail_syn);

    free(config_data);
    return 0;
}
