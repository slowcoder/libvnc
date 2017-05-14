#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>

#include "libvnc/libvnc.h"

VNCServer    *pVNC  = NULL;

static void VNC_Start(void) {
  uint32_t *pFB = NULL;
  int x,y;

  // Create dummy FB
  pFB = (uint32_t*)calloc(1,640*480*4);

  // Start a server, and update the FB
  pVNC = VNCServer_Create(5900,pFB,640,480);

  // Paint a pretty picture
  for(y=0;y<480;y++) {
    for(x=0;x<640;x++) {
      pFB[y*640+x] = (((y*255)/480) << 16) |
	(((x*255)/640)<<8);
    }
  }
  VNCServer_UpdateFB(pVNC);
}

int main(void) {

  // Start VNC server
  VNC_Start();

  while(1) {
  	sleep(1);
  }

  VNCServer_Release(pVNC);

  return 0;
}
