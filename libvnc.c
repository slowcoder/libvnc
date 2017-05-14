#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <signal.h>
#include <assert.h>

#include "libvnc/libvnc.h"
#include "tcp.h"

#define cpu_to_net32(x)      \
  ((((x) & 0xff000000) >> 24) | \
   (((x) & 0x00ff0000) >>  8) | \
   (((x) & 0x0000ff00) <<  8) | \
   (((x) & 0x000000ff) << 24))
#define cpu_to_net16(x) \
  ((((x) & 0xff00) >> 8) | \
   (((x) & 0x00ff) << 8))

#define net16_to_cpu(__x) cpu_to_net16(__x)
#define net32_to_cpu(__x) cpu_to_net32(__x)

typedef struct {
  pthread_t thread;

  uint16_t port;
  void *serversocket;

  void *pFB;
} Server_t;

#pragma pack(push,1,bah)
typedef struct {
  uint8_t  bpp;
  uint8_t  depth;
  uint8_t  bigendianflag;
  uint8_t  truecolorflag;
  uint16_t redmax;
  uint16_t greenmax;
  uint16_t bluemax;
  uint8_t  redshift;
  uint8_t  greenshift;
  uint8_t  blueshift;
  uint8_t  unused[3];
} PixelFormat_t;

typedef struct {
  uint16_t fbwidth;
  uint16_t fbheight;

  PixelFormat_t pixelformat;

  uint32_t namelen;
  uint8_t  name[0];
} ServerInit_t;
#pragma pack(pop,bah)

typedef struct Client {
  void *s;

  void *pFB;

  PixelFormat_t pixelformat;

  uint16_t  numencodings;
  int32_t  *encoding;

  struct client *pNext;
} Client_t;

static int vncp_send_framebufferupdate(Client_t *c);

static int vncp_do_serverinit(Client_t *c) {
  ServerInit_t *pSi;

  pSi = (ServerInit_t*)calloc(1,1024);

  pSi->fbwidth                   = cpu_to_net16(640);
  pSi->fbheight                  = cpu_to_net16(480);
  pSi->pixelformat.bpp           = 32;
  pSi->pixelformat.depth         = 32;
  pSi->pixelformat.bigendianflag = 0;
  pSi->pixelformat.truecolorflag = 1; // Not palettized
  pSi->pixelformat.redmax        = cpu_to_net16(255);
  pSi->pixelformat.greenmax      = cpu_to_net16(255);
  pSi->pixelformat.bluemax       = cpu_to_net16(255);
  pSi->pixelformat.redshift      = 16;
  pSi->pixelformat.greenshift    = 8;
  pSi->pixelformat.blueshift     = 0;
  pSi->namelen                   = cpu_to_net32(4);
  strcpy((char*)pSi->name,"ZOMG");

  // Store the pixelformat in the client-struct
  // in case we don't get a SetPixelFormat packet from the client
  memcpy(&c->pixelformat,&pSi->pixelformat,sizeof(PixelFormat_t));

  // Send it to the client
  TCP_Send(c->s,pSi,sizeof(ServerInit_t)+(pSi->namelen>>24),1);

  vncp_send_framebufferupdate(c);
  
  return 0;
}

static int vncp_do_clientinit(Client_t *c) {
  uint8_t desktopshare;

  TCP_Recv(c->s,&desktopshare,1,1);
  if( desktopshare == 0 ) {
    printf("Client does not wish to share desktop\n");
  } else {
    printf("Client wishes to share desktop\n");
  }

  return vncp_do_serverinit(c);
}

static int vncp_do_initial(Client_t *c) {
  uint8_t protocolversion[12];
  uint8_t security[2];
  uint32_t secres;

  memcpy(protocolversion,"RFB 003.008\n",12);
  TCP_Send(c->s,protocolversion,12,1);
  TCP_Recv(c->s,protocolversion,12,1);
  printf("Client protoversion: %12s",protocolversion);

  // TODO: Make sure the client version is something we understand

  // Security (3.8 style)

  // Tell the client what mechs we support
  security[0] = 1; // One security mechanism
  security[1] = 1; // It is "None"
  TCP_Send(c->s,&security,2,1);

  // See which one the client selects to use
  TCP_Recv(c->s,&security,1,1);
  assert(security[0] == 1);

  // Send back the security result
  secres = cpu_to_net32(0);
  TCP_Send(c->s,&secres,4,1);
  printf("Client supported \"None\" as security\n");

  vncp_do_clientinit(c);

  return 0;
}

static int vncp_do_setpixelformat(Client_t *c) {
  uint8_t pad[3];

  TCP_Recv(c->s,pad,3,1); // Skip 3 bytes of padding
  TCP_Recv(c->s,&c->pixelformat,sizeof(PixelFormat_t),1);

  c->pixelformat.redmax = net16_to_cpu(c->pixelformat.redmax);
  c->pixelformat.greenmax = net16_to_cpu(c->pixelformat.greenmax);
  c->pixelformat.bluemax = net16_to_cpu(c->pixelformat.bluemax);

  printf(" New pixelformat\n");
  printf("  BPP        = %u\n",c->pixelformat.bpp);
  printf("  Depth      = %u\n",c->pixelformat.depth);
  printf("  TruColor   = %u\n",c->pixelformat.truecolorflag);
  printf("  RedMax     = %u\n",c->pixelformat.redmax);
  printf("  GreenMax   = %u\n",c->pixelformat.greenmax);
  printf("  BlueMax    = %u\n",c->pixelformat.bluemax);
  printf("  RedShift   = %u\n",c->pixelformat.redshift);
  printf("  GreenShift = %u\n",c->pixelformat.greenshift);
  printf("  BlueShift  = %u\n",c->pixelformat.blueshift);

  return 0;
}

static int vncp_do_setencodings(Client_t *c) {
  uint8_t  pad[1];
  uint16_t numencs;
  int i;
  int bHasRaw = 0;

  TCP_Recv(c->s,pad,1,1); // Skip 1 byte of padding

  TCP_Recv(c->s,&numencs,2,1);
  numencs = net16_to_cpu(numencs);

  c->numencodings = numencs;
  c->encoding = (int32_t*)calloc(numencs,sizeof(int32_t));
  
  printf(" Number of encodings supported by client: %i\n",numencs);
  for(i=0;i<numencs;i++) {
    int32_t enc;

    TCP_Recv(c->s,&enc,4,1);
    enc = net32_to_cpu(enc);
    printf("  Encoding %i: 0x%08x\n",i,enc);
    c->encoding[i] = enc;

    if(enc == 0x00) bHasRaw = 1;
  }

  assert(bHasRaw == 1);

  return 0;
}

static int vncp_do_framebufferupdaterequest(Client_t *c) {
#pragma pack(push,1,fbur)
  struct {
    uint8_t  incremental;
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
  } req;
#pragma pack(pop,fbur)

  TCP_Recv(c->s,&req,sizeof(req),1);
  req.x = net16_to_cpu(req.x);
  req.y = net16_to_cpu(req.y);
  req.w = net16_to_cpu(req.w);
  req.h = net16_to_cpu(req.h);
  //printf(" Got FBUR for (%i,%i) (%i,%i)  Incremental=%i\n",req.x,req.y,req.w,req.h,req.incremental);

  vncp_send_framebufferupdate(c);

  return 0;
}

static int vncp_do_pointerevent(Client_t *c) {
#pragma pack(push,1,pe)
  struct {
    uint8_t buttonmask;
    uint16_t x;
    uint16_t y;
  } ev;
#pragma pack(pop,pe)

  TCP_Recv(c->s,&ev,sizeof(ev),1);
  ev.x = net16_to_cpu(ev.x);
  ev.y = net16_to_cpu(ev.y);
  printf("Got pointer-event  (%u,%u) (0x%x)\n",ev.x,ev.y,ev.buttonmask);

  vncp_send_framebufferupdate(c);

  return 0;
}

static int vncp_do_keyevent(Client_t *c) {
#pragma pack(push,1,pe)
  struct {
    uint8_t  downflag;
    uint8_t  padding[2];
    uint32_t key;
  } ev;
#pragma pack(pop,pe)

  TCP_Recv(c->s,&ev,sizeof(ev),1);
  ev.key = net32_to_cpu(ev.key);
  printf("Got key-event  (0x%x is %u)\n",ev.key,ev.downflag);

  return 0;
}

static int vncp_send_framebufferupdate(Client_t *c) {
#pragma pack(push,1,fbu)
  struct {
    uint8_t  messagetype;
    uint8_t  padding;
    uint16_t numrects;
  } cmd;
  struct {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
    int32_t  encoding;
  } rect;
#pragma pack(pop,fbu)

  cmd.messagetype = 0;
  cmd.padding     = 0;
  cmd.numrects    = cpu_to_net16(1);
  rect.x          = cpu_to_net16(0);
  rect.y          = cpu_to_net16(0);
  rect.w          = cpu_to_net16(640);
  rect.h          = cpu_to_net16(480);
  rect.encoding   = 0; // Raw

  TCP_Send(c->s,&cmd,sizeof(cmd),1);
  TCP_Send(c->s,&rect,sizeof(rect),1);

  // Convert the FB into the format requested by the client
  if( c->pixelformat.bpp == 32 ) {
    TCP_Send(c->s,c->pFB,640*480*4,1);
  } else {
    uint8_t  *pConv;
    uint32_t *pIn = (uint32_t*)c->pFB;
    int i;

    pConv = malloc(c->pixelformat.bpp*640*480);
    
    for(i=0;i<(640*480);i++) {
      if( c->pixelformat.bpp == 8 ) {
      	uint8_t r,g,b;

      	r = (pIn[i]>>16) & 0xFF;
      	g = (pIn[i]>> 8) & 0xFF;
      	b = (pIn[i]>> 0) & 0xFF;
      	pConv[i]  = ((r*c->pixelformat.redmax)/255)   << c->pixelformat.redshift;
      	pConv[i] |= ((g*c->pixelformat.greenmax)/255) << c->pixelformat.greenshift;
      	pConv[i] |= ((b*c->pixelformat.bluemax)/255)  << c->pixelformat.blueshift;
      } else {
      	assert(c->pixelformat.bpp == 8);
      }
    }

    TCP_Send(c->s,c->pFB,640*480*(c->pixelformat.bpp/8),1);
    free(pConv);
  }

  return 0;
}

void *server_thread(void *pArg) {
  int done = 0;
  Server_t *pSrv = (Server_t*)pArg;

  pSrv->serversocket = TCP_Create();
  TCP_Listen(pSrv->serversocket,pSrv->port);

  while(1) {
    void *clientsocket;
    int r;
    uint8_t cmd;
    Client_t *pCli = NULL;

    if( TCP_Accept(pSrv->serversocket,&clientsocket,250) == 0 ) { // Got new connection
      printf("Got connection!\n");
      done = 0;

      pCli = (Client_t*)calloc(1,sizeof(Client_t));
      pCli->s = clientsocket;
      pCli->pFB = pSrv->pFB;

      r = vncp_do_initial(pCli);
      printf("Do_Initial r=%i\n",r);

      while(!done) {

      	// Process all pending commands from the client
      	do {
      	  r = TCP_Recv(pCli->s,&cmd,1,0);

      	  if( r == 1 ) {
      	    //printf("Processing cmd 0x%02x\n",cmd);
      	    switch(cmd) {
      	    case 0x00: if(vncp_do_setpixelformat(pCli))           done = 1; break;
      	    case 0x02: if(vncp_do_setencodings(pCli))             done = 1; break;
      	    case 0x03: if(vncp_do_framebufferupdaterequest(pCli)) done = 1; break;
      	    case 0x04: if(vncp_do_keyevent(pCli))                 done = 1; break;
      	    case 0x05: if(vncp_do_pointerevent(pCli))             done = 1; break;
      	    default:
      	      printf("Unsupported cmd 0x%02x\n",cmd);
      	      assert(0);
      	    }
      	  }
      	} while(r == 1);

      	if( r == 0 ) // Client closed the connection
      	  done = 1;

    	// Process all pending SHM commands
    	//printf("done = %i\n",done);
      }
      TCP_Close(pCli->s);
      printf("done with client\n");
    }
  }

  TCP_Close(pSrv->serversocket);

  return NULL;
}

VNCServer *VNCServer_Create(uint16_t port,void *pFB,int w,int h) {
  Server_t *pRet;
  pthread_attr_t attr;

  pRet = (Server_t*)calloc(1,sizeof(Server_t));
  pRet->pFB  = pFB;
  pRet->port = port;

  printf("VNC Listening at %u\n",port);

  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_create(&pRet->thread, &attr, server_thread, pRet);
  pthread_attr_destroy(&attr);

  return pRet;
}
  
void       VNCServer_Release(VNCServer *pVoidSrv) {
  Server_t *pSrv;

  assert(pVoidSrv != NULL);

  pSrv = (Server_t*)pVoidSrv;
  pthread_kill(pSrv->thread,SIGHUP);

  free(pSrv);
}

void VNCServer_UpdateFB(VNCServer *pSrv) {
  assert(pSrv != NULL);
}
