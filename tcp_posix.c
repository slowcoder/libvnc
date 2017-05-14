#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>

#include "tcp.h"

typedef struct {
  struct sockaddr_in clientaddr;
  int fd;
} sock_t;

void *TCP_Create(void) {
  sock_t *s;

  s = calloc(1,sizeof(sock_t));
  return s;
}

int   TCP_Listen(void *ins,int port) {
  struct sockaddr_in ma;
  int yes = 1;
  int rc;
  sock_t *s;

  s = (sock_t *)ins;

  s->fd = socket(AF_INET, SOCK_STREAM, 0);
  if (s->fd == -1)
    return -errno;

  rc = setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
  if (rc == -1)
    return -errno;

  ma.sin_family = AF_INET;
  ma.sin_port = htons(port);
  ma.sin_addr.s_addr = INADDR_ANY;
  memset(&ma.sin_zero, 0, 8);

  rc = bind(s->fd, (struct sockaddr *)&ma, sizeof(struct sockaddr));
  if (rc == -1)
    return -errno;

  rc = listen(s->fd, 10);
  if (rc == -1)
    return -errno;

  return 0;
}

int   TCP_Accept(void *ins,void **inns,int tout_ms) {
  struct sockaddr_in ta;
  struct timeval *tvp = NULL;
  struct timeval tv;
  unsigned int insz = sizeof(ta);
  int yes = 1;
  int rc;
  fd_set fds;
  sock_t *s,*ns;

  s = (sock_t*)ins;

  if (tout_ms != -1) {
    tv.tv_sec = tout_ms / 1000;
    tv.tv_usec = (tout_ms % 1000) * 1000;
    tvp = &tv;
  }

  FD_ZERO(&fds);
  FD_SET(s->fd, &fds);

  rc = select(s->fd + 1, &fds, NULL, NULL, tvp);
  if (rc == 0)
    return -EAGAIN;
  else if (rc < 0)
    return -errno;

  ns = (sock_t*)calloc(1,sizeof(sock_t));
  
  ns->fd = accept(s->fd, (struct sockaddr *)&ns->clientaddr, &insz);
  if (ns->fd < 0)
    return -errno;

  rc = setsockopt(ns->fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(int));
  if (rc == -1)
    return -errno;

  *inns = ns;

  return 0;
}

void  TCP_Close(void *ins) {
  sock_t *s;

  s = (sock_t*)ins;
  close(s->fd);
  free(s);
}

/* TODO: Implement nonblocking operation */
int   TCP_Send(void *ins,void *buf,int len,int bBlocking) {
  int r;
  sock_t *s;

  s = (sock_t*)ins;

  r = send(s->fd,buf,len,0);

  return r;
}

/* TODO: Implement nonblocking operation */
int   TCP_Recv(void *ins,void *buf,int len,int bBlocking) {
  int r;
  sock_t *s;

  s = (sock_t*)ins;

  r = recv(s->fd,buf,len,MSG_WAITALL);

  return r;
}
