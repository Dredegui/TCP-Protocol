#include "packet-format.h"
#include <arpa/inet.h>
#include <limits.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#define SENDER_TIMEOUT 1

/*
  Tryout Zé configuration and make tests that
  compare it's run times to fseeks one (meaning mine) 
*/ 

int nbit0(uint32_t wd, int n){
  if (n == 0) return 1;
  n--;
  wd = wd >> n;
  return !(wd & 1);
}

int main(int argc, char *argv[]) {
  int port = atoi(argv[1]);
  int wd_size = atoi(argv[2]);
  int snwd = wd_size;
  uint32_t curr_window = 0;
  int timeout_state = 0;
  // Prepare server socket.
  int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd == -1) {
    perror("socket");
    exit(-1);
  }

  // Allow address reuse so we can rebind to the same port,
  // after restarting the server.
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {
    perror("setsockopt");
    exit(-1);
  }

  struct timeval tv;
  tv.tv_sec = SENDER_TIMEOUT;
  tv.tv_usec = 0;
  if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
    perror("timeout setsockopt");
    exit(-1);
  }

  struct sockaddr_in srv_addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = htonl(INADDR_ANY),
      .sin_port = htons(port),
  };
  if (bind(sockfd, (struct sockaddr *)&srv_addr, sizeof(srv_addr))) {
    perror("bind");
    exit(-1);
  }
  fprintf(stderr, "Receiving on port: %d\n", port);

  ssize_t len;
  struct sockaddr_in src_addr;
  struct sockaddr_in unk_addr;
  req_file_pkt_t req_file_pkt;

  len = recvfrom(sockfd, &req_file_pkt, sizeof(req_file_pkt), 0,
                 (struct sockaddr *)&src_addr, &(socklen_t){sizeof(src_addr)});
  if (len < MAX_PATH_SIZE) {
    req_file_pkt.file_path[len] = '\0';
  }
  printf("Received request for file %s, size %ld.\n", req_file_pkt.file_path,
         len);

  FILE *file = fopen(req_file_pkt.file_path, "r");
  if (!file) {
    perror("fopen");
    exit(-1);
  }

  uint32_t seq_num = 0;
  data_pkt_t data_pkt;
  ack_pkt_t ack_pkt;
  ack_pkt_t unk_ack;
  size_t data_len;
  uint32_t ack_num = 0;
  int block_wd = 0;
  do { // Generate segments from file, until the the end of the file.
    // Prepare data segment.
    while (snwd > seq_num) {
      if (nbit0(curr_window, seq_num - ack_num)) { // TO-DO reconfigure how to update window
        data_pkt.seq_num = htonl(seq_num);
        // Load data from file.
        fseek(file, seq_num * MAX_CHUNK_SIZE, SEEK_SET);
        data_len = fread(data_pkt.data, 1, sizeof(data_pkt.data), file);
        printf("read %ld\n", data_len);
        // Send segment.
        printf("Sending segment %d, size %ld.\n", ntohl(data_pkt.seq_num), offsetof(data_pkt_t, data) + data_len);
        ssize_t sent_len = sendto(sockfd, &data_pkt, offsetof(data_pkt_t, data) + data_len, 0,
                    (struct sockaddr *)&src_addr, sizeof(src_addr));
        if (sent_len != offsetof(data_pkt_t, data) + data_len) {
          fprintf(stderr, "Truncated packet.\n");
          exit(-1);
        }
      }
      seq_num++;
      if (data_len == 0) {
        block_wd = 1;
        snwd = seq_num;
      }
    }
    ssize_t ack_len = recvfrom(sockfd, &unk_ack, sizeof(unk_ack), 0, (struct sockaddr *)&unk_addr, &(socklen_t){sizeof(unk_addr)});
    if (src_addr.sin_addr.s_addr != unk_addr.sin_addr.s_addr || src_addr.sin_port != unk_addr.sin_port) continue;
    else memcpy(&ack_pkt, &unk_ack, sizeof(unk_ack));
    if(ack_len < 0 && errno == EWOULDBLOCK) {   //TO-DO decomentar situações
        timeout_state++;
        if (timeout_state == MAX_RETRIES) {
          exit(-1);
        }
        seq_num = ack_num;
        fseek(file, seq_num * MAX_CHUNK_SIZE, SEEK_SET);
    } else {
      timeout_state = 0;
      if (!block_wd) snwd += (ntohl(ack_pkt.seq_num) - ack_num);
      ack_num = ntohl(ack_pkt.seq_num);
      curr_window = ntohl(ack_pkt.selective_acks);
    }    
  } while (ack_num != snwd);
  // Clean up and exit.
  close(sockfd);
  fclose(file);

  return 0;
}


