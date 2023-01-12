#include "packet-format.h"
#include <limits.h>
#include <netdb.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#define RECEIVER_TIMEOUT 4
#define ALL_CHUNK_TIMEOUT 2

uint32_t curr_window;
int wdsize;

void set_timeout(int sockfd, struct timeval *tv, int sec) {
  tv->tv_sec = sec;
  tv->tv_usec = 0;
  if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, tv, sizeof(struct timeval)) < 0) {
    perror("timeout setsockopt");
    exit(-1);
  }
}

int find_last_path_separator(char* path) {
  int last_found_pos = -1;
  int curr_pos = 0;

  while (*path != '\0') {
    if (*path == '/') {
      last_found_pos = curr_pos;
    }

    path += 1;
    curr_pos += 1;
  }

  return last_found_pos;
}

void uptacksel(int n) {
  n--;
  curr_window = curr_window | (1 << n); 
}

int uptwd() {
  int mov = 1;
  while((curr_window & 1)) {
    curr_window = curr_window >> 1;
    mov++;
  }
  curr_window = curr_window >> 1;
  return mov;
}
ssize_t receivepack(int sockfd, data_pkt_t *data_pkt, struct sockaddr_in *src_addr, struct timeval *tv) {
  ssize_t len = recvfrom(sockfd, data_pkt, sizeof(data_pkt_t), 0, (struct sockaddr *)src_addr, &(socklen_t){sizeof(struct sockaddr_in)});
  if (len < 0 && errno == EWOULDBLOCK) {
    exit(EXIT_FAILURE);
  }
  return len;
}

int ackpack(int sockfd, data_pkt_t *data_pkt, ack_pkt_t *ack_pkt, struct sockaddr_in *srv_addr, FILE *file, ssize_t *len, uint32_t *ack_num) {
  uint32_t seq_num = ntohl(data_pkt->seq_num);
  if (seq_num >= *ack_num && seq_num < *ack_num + wdsize) { // Received Valid data packet    
    // Alter Window
    int mov = 0;
    int curr = seq_num - *ack_num;
    if (curr == 0) mov = uptwd(); // If it's a base sequence number moves window  
    else uptacksel(curr);         // If it's out of order packets updates bit (doesn't move window)
    // Update ACK package
    *ack_num = *ack_num + mov;
    ack_pkt->seq_num = htonl(*ack_num); 
    ack_pkt->selective_acks = htonl(curr_window);
    // Send ACK
    sendto(sockfd, ack_pkt, sizeof(ack_pkt_t), 0, (struct sockaddr *)srv_addr, sizeof(struct sockaddr_in));
    // Write data to file.
    fseek(file, seq_num * MAX_CHUNK_SIZE, SEEK_SET);
    fwrite(data_pkt->data, 1, *len - offsetof(data_pkt_t, data), file);
  } else if (seq_num < *ack_num || seq_num >= *ack_num + wdsize) {  // Received out of bounds data packet    
    // Resend Latest ACK
    sendto(sockfd, ack_pkt, sizeof(ack_pkt_t), 0, (struct sockaddr *)srv_addr, sizeof(struct sockaddr_in));
    *len = sizeof(data_pkt_t); // Don't want out of bounds data to affect closing conditions
  }
  return 0;
}

int main(int argc, char *argv[]) {
  char *file_path = argv[1];
  char *host = argv[2];
  int port = atoi(argv[3]);
  wdsize = atoi(argv[4]);
  curr_window = 0;

  int last_path_sep_index = find_last_path_separator(file_path);
  char *file_name = file_path;

  if (last_path_sep_index != -1 && last_path_sep_index < MAX_PATH_SIZE - 1) {
    file_name = file_path + last_path_sep_index + 1;
  }

  FILE *file = fopen(file_name, "w");
  if (!file) {
    perror("fopen");
    exit(-1);
  }

  // Prepare server host address.
  struct hostent *he;
  if (!(he = gethostbyname(host))) {
    perror("gethostbyname");
    exit(-1);
  }

  struct sockaddr_in srv_addr = {
      .sin_family = AF_INET,
      .sin_port = htons(port),
      .sin_addr = *((struct in_addr *)he->h_addr),
  };

  int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd == -1) {
    perror("socket");
    exit(-1);
  }

  struct timeval tv;
  set_timeout(sockfd, &tv, RECEIVER_TIMEOUT);

  req_file_pkt_t req_file_pkt;
  size_t file_path_len = strlen(file_path);
  strncpy(req_file_pkt.file_path, file_path, file_path_len);

  ssize_t sent_len = sendto(sockfd, &req_file_pkt, file_path_len, 0,
                            (struct sockaddr *)&srv_addr, sizeof(srv_addr));
  if (sent_len != file_path_len) {
    fprintf(stderr, "Truncated packet.\n");
    exit(-1);
  }
  printf("Sending request for file %s, size %ld.\n", file_path, file_path_len);
  uint32_t ack_num = 0;
  ssize_t len;
  ack_pkt_t ack_pkt;
  ack_pkt.seq_num = 0;
  ack_pkt.selective_acks = 0;
  data_pkt_t data_pkt;
  struct sockaddr_in src_addr;
  int end = -1;
  do { // Iterate over segments, until last the segment is detected.
    // Receive segment.
    len = receivepack(sockfd, &data_pkt, &src_addr, &tv);
    // Write segment and Send ACK
    if (len != 0) {
      ackpack(sockfd, &data_pkt, &ack_pkt, &src_addr, file, &len, &ack_num);
      if (len != sizeof(data_pkt)) end = ntohl(data_pkt.seq_num) + 1;
    }
  } while (end != ack_num);

  // Wait 2 Seconds for Server
  set_timeout(sockfd, &tv, ALL_CHUNK_TIMEOUT);
  do {
    len = recvfrom(sockfd, &data_pkt, sizeof(data_pkt_t), 0, (struct sockaddr *)&src_addr, &(socklen_t){sizeof(struct sockaddr_in)});
    if (len == sizeof(data_pkt_t)) sendto(sockfd, &ack_pkt, sizeof(ack_pkt_t), 0, (struct sockaddr *)&src_addr, sizeof(struct sockaddr_in));

  } while (len != -1);
    
  // Clean up and exit.
  close(sockfd);
  fclose(file);

  return 0;
}
