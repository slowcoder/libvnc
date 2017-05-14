#pragma once

#include <stdint.h>

typedef void VNCServer;

VNCServer *VNCServer_Create(uint16_t port,void *pFB,int w,int h);
void       VNCServer_Release(VNCServer *pSrv);
void       VNCServer_UpdateFB(VNCServer *pSrv);

