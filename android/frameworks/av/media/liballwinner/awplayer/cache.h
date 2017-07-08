
#ifndef CACHE_H
#define CACHE_H

#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

#include "CdxParser.h"
#include "vdecoder.h"
#include "player.h"

typedef struct CacheNode_t CacheNode;
struct CacheNode_t
{
    unsigned char* pData;
    int            nLength;
    int64_t        nPts;
    int64_t        nPcr;
    int            bIsFirstPart;
    int            bIsLastPart;
    int            eMediaType;
    int            nStreamIndex;
    int            nFlags;
    int64_t        nDuration;
    int            infoVersion;
    void*          info;
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
    CdxParserTypeT          eContainerFormat;   //* for seek processing.
    enum EVIDEOCODECFORMAT  eVideoCodecFormat;  //* for finding key frame in cache when seek.
    
}StreamCache;

StreamCache* StreamCacheCreate(void);

void StreamCacheDestroy(StreamCache* c);

void StreamCacheSetSize(StreamCache* c, int nStartPlaySize, int nMaxBufferSize);

int StreamCacheGetSize(StreamCache* c);

int StreamCacheUnderflow(StreamCache* c);

int StreamCacheOverflow(StreamCache* c);

int StreamCacheDataEnough(StreamCache* c);

CacheNode* StreamCacheNextFrame(StreamCache* c);

void StreamCacheFlushOneFrame(StreamCache* c);

int StreamCacheAddOneFrame(StreamCache* c, CacheNode* node);

void StreamCacheFlushAll(StreamCache* c);

int StreamCacheGetBufferFullness(StreamCache* c);

int StreamCacheGetLoadingProgress(StreamCache* c);

int StreamCacheSetMediaFormat(StreamCache*           c, 
                              CdxParserTypeT         eContainerFormat, 
                              enum EVIDEOCODECFORMAT eVideoCodecFormat,
                              int                    nBitrate);

int StreamCacheSetPlayer(StreamCache* c, Player* pPlayer);

int64_t StreamCacheSeekTo(StreamCache* c, int64_t nSeekTimeUs);

#endif
