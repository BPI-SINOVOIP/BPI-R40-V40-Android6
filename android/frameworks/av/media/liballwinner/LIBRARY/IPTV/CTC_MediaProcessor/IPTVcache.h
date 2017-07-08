
#ifndef IPTV_CACHE_H
#define IPTV_CACHE_H

#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

#include "vdecoder.h"
#include "player.h"

typedef struct CacheNode_t CacheNode;
struct CacheNode_t
{
    unsigned char* pData;
    int            nLength;  
    CacheNode*     pNext;
};

typedef struct StreamCache_t
{
    pthread_mutex_t         mutex;
    int                     nMaxBufferSize;
    int                     nStartPlaySize;
    
    int                     nTotalDataSize;
    int                     nFrameNum;
    CacheNode*              pHead;
    CacheNode*              pTail;
    
    int                     nPassedDataSize;
    int                     nPassedFrameNum;
    CacheNode*              pPassedHead;
    CacheNode*              pPassedTail;
    
    int64_t                 nLastValidPts;
    CacheNode*              pNodeWithLastValidPts;
    int64_t                 nLastValidPcr;
    CacheNode*              pNodeWithLastValidPcr;
    
    int                     nBitrate;           //* for ts/m2ts stream seek processing.
    Player*                 pPlayer;            //* for ts/m2ts stream seek processing.
    
}StreamCache;

StreamCache* IptvStreamCacheCreate(void);

void IptvStreamCacheDestroy(StreamCache* c);

void IptvStreamCacheSetSize(StreamCache* c, int nStartPlaySize, int nMaxBufferSize);

int IptvStreamCacheGetSize(StreamCache* c);

int IptvStreamCacheUnderflow(StreamCache* c);

int IptvStreamCacheOverflow(StreamCache* c);

int IptvStreamCacheDataEnough(StreamCache* c);

CacheNode* IptvStreamCacheNextFrame(StreamCache* c);

void IptvStreamCacheFlushOneFrame(StreamCache* c);

int IptvStreamCacheAddOneFrame(StreamCache* c, CacheNode* node);

void IptvStreamCacheFlushAll(StreamCache* c);

int IptvStreamCacheGetBufferFullness(StreamCache* c);

int IptvStreamCacheGetLoadingProgress(StreamCache* c);


#endif
