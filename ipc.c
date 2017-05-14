#include <string.h>
#include <pthread.h>

#include "libvnc/ipc.h"

static pthread_mutex_t queuemutex = PTHREAD_MUTEX_INITIALIZER;

#define QUEUE_SIZE 32
static struct {
  IPCEvent_t ev;
  int seq;
} queue[QUEUE_SIZE];

int IPCEvent_Post(IPCEvent_t *pEv) {
  int i;
  int maxseq = -1;

  pthread_mutex_lock( &queuemutex );

  for(i=0;i<QUEUE_SIZE;i++) {
    if( queue[i].ev.type != eEventType_Invalid ) {
      if( queue[i].seq > maxseq ) 
	maxseq = queue[i].seq;
    }
  }

  for(i=0;i<QUEUE_SIZE;i++) {
    if( queue[i].ev.type == eEventType_Invalid ) {
      memcpy(&queue[i].ev,pEv,sizeof(IPCEvent_t));
      queue[i].seq = maxseq+1;
      pthread_mutex_unlock( &queuemutex );
      return 0;
    }
  }

  pthread_mutex_unlock( &queuemutex );

  return -1;
}

int IPCEvent_Poll(IPCEvent_t *pEv) {
  int i;
  int minseqndx = -1;
  int minseq = -1;

  pthread_mutex_lock( &queuemutex );

  for(i=0;i<QUEUE_SIZE;i++) {
    if( queue[i].ev.type != eEventType_Invalid ) {
      if( minseq == -1 || queue[i].seq < minseq ) {
	minseq = queue[i].seq;
	minseqndx = i;
      }
    }
  }

  if( minseqndx >= 0 ) {
    memcpy(pEv,&queue[minseqndx].ev,sizeof(IPCEvent_t));
    queue[minseqndx].ev.type = eEventType_Invalid;
    queue[minseqndx].seq  = -1;
    pthread_mutex_unlock( &queuemutex );
    return 0;
  }

  pthread_mutex_unlock( &queuemutex );

  return -1;
}

static int bWantsToQuit = 0;
void IPCQuitApp(void)
{
	bWantsToQuit = 1;
}

int  IPCWantsToQuit(void)
{
	return bWantsToQuit;
}
