
#include <unistd.h>
#include <stdlib.h>
#include <malloc.h>
#include "IPTVcache.h"
#include "log.h"

static const int MAX_PASSED_NODE_COUNT_KEPT = 200;
static const int MAX_PASSED_DATA_SIZE_KEPT  = (4*1024*1024);

static void StreamCacheFlushPassedList(StreamCache* c);
static int  StreamCacheIsKeyFrame(StreamCache* c, CacheNode* pNode);
static int64_t StreamCacheSeekByPts(StreamCache* c, int64_t nSeekTimeUs);
static int64_t StreamCacheSeekByPcr(StreamCache* c, int64_t nSeekTimeUs);
static int StreamCacheSeekByOffset(StreamCache* c, int nOffset);
static int StreamCachePlayerCacheTime(Player* p);

static int StreamCacheIsMpeg12KeyFrame(CacheNode* pNode);
static int StreamCacheIsWMV3KeyFrame(CacheNode* pNode);
static int StreamCacheIsH264KeyFrame(CacheNode* pNode);
static int StreamCacheIsH265KeyFrame(CacheNode* pNode);

static unsigned int ReadGolomb(unsigned char* buffer, unsigned int* init);


StreamCache* IptvStreamCacheCreate(void)
{
    StreamCache* c;
    c = (StreamCache*)malloc(sizeof(StreamCache));
    if(c == NULL)
        return NULL;
    memset(c, 0, sizeof(StreamCache));
    
    c->nLastValidPts     = -1;
    c->nLastValidPcr     = -1;
    c->nMaxBufferSize    = 16*1024*1024;
    logd("set cache size: [16M]");
    
    pthread_mutex_init(&c->mutex, NULL);
    return c;
}


void IptvStreamCacheDestroy(StreamCache* c)
{
    CacheNode* node;
    
    pthread_mutex_lock(&c->mutex);
    node = c->pHead;
    while(node != NULL)
    {
        c->pHead = node->pNext;
        if(node->pData)
            free(node->pData);
		
        free(node);
        node = c->pHead;
    }
    
    node = c->pPassedHead;
    while(node != NULL)
    {
        c->pPassedHead = node->pNext;
        if(node->pData)
            free(node->pData);
				
        free(node);
        node = c->pPassedHead;
    }
    pthread_mutex_unlock(&c->mutex);
    
    free(c);
    return;
}


void IptvStreamCacheSetSize(StreamCache* c, int nStartPlaySize, int nMaxBufferSize)
{
    pthread_mutex_lock(&c->mutex);
    c->nMaxBufferSize = nMaxBufferSize;
    c->nStartPlaySize = nStartPlaySize;
    pthread_mutex_unlock(&c->mutex);
    return;
}


int IptvStreamCacheGetSize(StreamCache* c)
{
    return c->nTotalDataSize;
}


int IptvStreamCacheUnderflow(StreamCache* c)
{
    int bUnderFlow;
    pthread_mutex_lock(&c->mutex);
    if(c->nFrameNum > 0)
        bUnderFlow = 0;
    else
        bUnderFlow = 1;
    pthread_mutex_unlock(&c->mutex);
    return bUnderFlow;
}


int IptvStreamCacheOverflow(StreamCache* c)
{
    int bOverFlow;
    pthread_mutex_lock(&c->mutex);
    if(c->nTotalDataSize >= c->nMaxBufferSize)
        bOverFlow = 1;
    else
        bOverFlow = 0;
    pthread_mutex_unlock(&c->mutex);
    return bOverFlow;
}


int IptvStreamCacheDataEnough(StreamCache* c)
{
    int bDataEnough;
    pthread_mutex_lock(&c->mutex);
    if(c->nTotalDataSize >= c->nStartPlaySize && c->nFrameNum > 0)
        bDataEnough = 1;
    else
        bDataEnough = 0;
    pthread_mutex_unlock(&c->mutex);
    return bDataEnough;
}


CacheNode* IptvStreamCacheNextFrame(StreamCache* c)
{
    CacheNode* node;
    pthread_mutex_lock(&c->mutex);
    node = c->pHead;
    pthread_mutex_unlock(&c->mutex);
    return node;
}


void IptvStreamCacheFlushOneFrame(StreamCache* c)
{
    CacheNode* node;
    
    pthread_mutex_lock(&c->mutex);
    
    //* flush one frame from list.
    node = c->pHead;
    if(node != NULL)
    {
        c->pHead  = node->pNext;
        node->pNext = NULL;
        c->nFrameNum--;
        c->nTotalDataSize -= node->nLength;
    
        if(c->pHead == NULL)
            c->pTail = NULL;
    }
    
    //* add this frame to the passed list.
    if(c->pPassedTail != NULL)
    {
        c->pPassedTail->pNext = node;
        c->pPassedTail = c->pPassedTail->pNext;
    }
    else
    {
        c->pPassedTail = node;
        c->pPassedHead = node;
    }
    c->nPassedDataSize += node->nLength;
    c->nPassedFrameNum++;
    
    //* delete frames in passed list if too much data in this list.
    StreamCacheFlushPassedList(c);
    
    pthread_mutex_unlock(&c->mutex);
    return;
}


int IptvStreamCacheAddOneFrame(StreamCache* c, CacheNode* node)
{
    CacheNode* newNode;
    
    newNode = (CacheNode*)malloc(sizeof(CacheNode));
    if(newNode == NULL)
        return -1;
    
    newNode->pData          = node->pData;
    newNode->nLength        = node->nLength;
    newNode->pNext          = NULL;
    
    pthread_mutex_lock(&c->mutex);
    if(c->pTail != NULL)
    {
        c->pTail->pNext = newNode;
        c->pTail = c->pTail->pNext;
    }
    else
    {
        c->pTail = newNode;
        c->pHead = newNode;
    }
    c->nTotalDataSize += newNode->nLength;
    c->nFrameNum++;
    pthread_mutex_unlock(&c->mutex);
    
    return 0;
}


void IptvStreamCacheFlushAll(StreamCache* c)
{
    CacheNode* node;
    
    pthread_mutex_lock(&c->mutex);
    //* flush frames in list.
    node = c->pHead;
    while(node != NULL)
    {
        c->pHead = node->pNext;
        if(node->pData)
            free(node->pData);

        free(node);
        node = c->pHead;
    }
    
    c->pTail          = NULL;
    c->nTotalDataSize = 0;
    c->nFrameNum      = 0;
    
    //* flush frames in passed list.
    node = c->pPassedHead;
    while(node != NULL)
    {
        c->pPassedHead = node->pNext;
        if(node->pData)
            free(node->pData);
		
        free(node);
        node = c->pPassedHead;
    }
    
    c->pPassedTail           = NULL;
    c->nPassedDataSize       = 0;
    c->nPassedFrameNum       = 0;
    
    c->nLastValidPts         = -1;
    c->pNodeWithLastValidPts = NULL;
    c->nLastValidPcr         = -1;
    c->pNodeWithLastValidPcr = NULL;
    
    pthread_mutex_unlock(&c->mutex);
    return;
}


int IptvStreamCacheGetBufferFullness(StreamCache* c)
{
    if(c->nMaxBufferSize > 0)
        return (c->nTotalDataSize*100)/c->nMaxBufferSize;
    else
        return 0;
}


int IptvStreamCacheGetLoadingProgress(StreamCache* c)
{
    if(c->nTotalDataSize > c->nStartPlaySize)
        return 100;
    else if(c->nStartPlaySize > 0)
        return (c->nTotalDataSize*100)/c->nStartPlaySize;
    else
        return 0;
}


int IptvStreamCacheSetPlayer(StreamCache* c, Player* pPlayer)
{
    c->pPlayer = pPlayer;
    return 0;
}


static void StreamCacheFlushPassedList(StreamCache* c)
{
    CacheNode* node;
    
    while(c->nPassedFrameNum > MAX_PASSED_NODE_COUNT_KEPT || 
          c->nPassedDataSize > MAX_PASSED_DATA_SIZE_KEPT)
    {
        node = c->pPassedHead;
        if(node != NULL)
        {
            c->pPassedHead  = node->pNext;
            c->nPassedFrameNum--;
            c->nPassedDataSize -= node->nLength;
            
            if(node == c->pNodeWithLastValidPts)
            {
                c->nLastValidPts = -1;
                c->pNodeWithLastValidPts = NULL;
            }
            if(node == c->pNodeWithLastValidPcr)
            {
                c->nLastValidPcr = -1;
                c->pNodeWithLastValidPcr = NULL;
            }
            
            if(node->pData)
                free(node->pData);

            free(node);
        
            if(c->pPassedHead == NULL)
            {
                c->pPassedTail = NULL;
                break;
            }
        }
    }
    
    return;
}



static int StreamCachePlayerCacheTime(Player* p)
{
    int     bHasVideo;
    int     bHasAudio;
    int     nPictureNum;
    int     nFrameDuration;
    int     nPcmDataSize;
    int     nSampleRate;
    int     nChannelCount;
    int     nBitsPerSample;
    int     nStreamDataSize;
    int     nBitrate;
    int64_t nVideoCacheTime;
    int64_t nAudioCacheTime;
    
    nVideoCacheTime = 0;
    nAudioCacheTime = 0;
    bHasVideo = PlayerHasVideo(p);
    bHasAudio = PlayerHasAudio(p);
    
    if(bHasVideo == 0 && bHasAudio == 0)
        return 0;
    
    if(bHasVideo)
    {
        nPictureNum     = PlayerGetValidPictureNum(p);
        nFrameDuration  = PlayerGetVideoFrameDuration(p);
        nStreamDataSize = PlayerGetVideoStreamDataSize(p);
        nBitrate        = PlayerGetVideoBitrate(p);
        
        nVideoCacheTime = nPictureNum*nFrameDuration;
        
        if(nBitrate > 0)
            nVideoCacheTime += ((int64_t)nStreamDataSize)*8*1000*1000/nBitrate;
    }
    
    if(bHasAudio)
    {
        nPcmDataSize    = PlayerGetAudioPcmDataSize(p);
        nStreamDataSize = PlayerGetAudioStreamDataSize(p);
        nBitrate        = PlayerGetAudioBitrate(p);
        PlayerGetAudioParam(p, &nSampleRate, &nChannelCount, &nBitsPerSample);
        
        nAudioCacheTime = 0;
        
        if(nSampleRate != 0 && nChannelCount != 0 && nBitsPerSample != 0)
        {
            nAudioCacheTime += ((int64_t)nPcmDataSize)*8*1000*1000/(nSampleRate*nChannelCount*nBitsPerSample);
        }
        
        if(nBitrate > 0)
            nAudioCacheTime += ((int64_t)nStreamDataSize)*8*1000*1000/nBitrate;
    }

    return (int)(nVideoCacheTime + nAudioCacheTime)/(bHasVideo + bHasAudio);
}


