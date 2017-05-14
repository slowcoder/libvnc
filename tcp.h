#pragma once

void *TCP_Create(void);
int   TCP_Listen(void *s,int port);
int   TCP_Accept(void *s,void **ns,int tout_ms);
void  TCP_Close(void *s);

int   TCP_Send(void *s,void *buf,int len,int bBlocking);
int   TCP_Recv(void *s,void *buf,int len,int bBlocking);
