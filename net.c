#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "net.h"
#include "jbod.h"

int PACKET_SIZE = 261;

/* the client socket descriptor for the connection to the server */
int cli_sd = -1;

/* attempts to read n bytes from fd; returns true on success and false on failure */
bool nread(int fd, int len, uint8_t *buf) {
  int total_read = 0;
  if (fd < 0) {
    printf("nread: file descriptor invalid\n");
    return false;
  }
  while (total_read < len) {
    int bytes_read = read(fd, buf + total_read, len - total_read);
    if (bytes_read <= 0) {
      return false;
    }
    total_read += bytes_read;
  }
  return true;
}

/* attempts to write n bytes to fd; returns true on success and false on failure */
bool nwrite(int fd, int len, uint8_t *buf) {
  int total_written = 0;
  if (fd < 0) {
    printf("nwrite: file descriptor invalid\n");
    return false;
  }
  while (total_written < len) {
    int bytes_written = write(fd, buf + total_written, len - total_written);
    if (bytes_written <= 0) {
      return false;
    }
    total_written += bytes_written;
  }
  return true;
}

/* attempts to receive a packet from fd; returns true on success and false on failure */
bool recv_packet(int fd, uint32_t *op, uint8_t *ret, uint8_t *block) {
  uint8_t buf[PACKET_SIZE];

  if (!nread(fd, 5, buf)) {
    return false;
  }

  memcpy(op, buf, sizeof(uint32_t));
  *ret = buf[4];

  if ((*ret & 0x02) == 0x02) {
    if (!nread(fd, 256, buf + 5)) {
      return false;
    }

    memcpy(block, buf + 5, 256);
  }
  return true;
}

/* attempts to send a packet to fd; returns true on success and false on failure */
bool send_packet(int fd, uint32_t op, uint8_t *block) {
  uint8_t buf[PACKET_SIZE];

  int write_len = 5;
  uint32_t nop = htonl(op);

  memcpy(buf, &nop, sizeof(uint32_t));

  if (((op >> 12) & 0x3f) == JBOD_WRITE_BLOCK) {
    buf[4] = 0x03;
    write_len += 256;
    memcpy(buf + 5, block, 256);
  } else {
    buf[4] = 0x01;
  }

  nwrite(fd, write_len, buf);
  return true;
}

/* connect to server and set the global client variable to the socket */
bool jbod_connect(const char *ip, uint16_t port) {
  struct sockaddr_in server_addr;

  cli_sd = socket(AF_INET, SOCK_STREAM, 0);
  if (cli_sd < 0) {
    perror("socket");
    return false;
  }

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  server_addr.sin_addr.s_addr = inet_addr(ip);

  if (connect(cli_sd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    perror("connect");
    close(cli_sd);
    return false;
  }

  return true;
}

/* disconnect from the server */
void jbod_disconnect(void) {
  if (cli_sd != -1) {
    close(cli_sd);
    cli_sd = -1;
  }
}

/* performs the JBOD operation over the network */

int jbod_client_operation(uint32_t op, uint8_t *block) {
  uint8_t *ret = calloc(1, sizeof(uint8_t));
  if(!send_packet(cli_sd, op, block)){
    return -1;
  }
  if(!recv_packet(cli_sd, &op, ret, block)){
    return -1;
  }
  return 0;
}
