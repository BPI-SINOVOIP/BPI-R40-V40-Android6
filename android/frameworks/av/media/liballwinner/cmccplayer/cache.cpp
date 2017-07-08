
#include <unistd.h>
#include <stdlib.h>
#include <malloc.h>
#include "cache.h"
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

StreamCache* StreamCacheCreate(void)
{
    StreamCache* c;
    c = (StreamCache*)malloc(sizeof(StreamCache));
    if(c == NULL)
        return NULL;
    memset(c, 0, sizeof(StreamCache));
    
    c->eContainerFormat  = CDX_PARSER_UNKNOW;
    c->eVideoCodecFormat = VIDEO_CODEC_FORMAT_UNKNOWN;
    c->nLastValidPts     = -1;
    c->nLastValidPcr     = -1;
    
    pthread_mutex_init(&c->mutex, NULL);
    return c;
}


void StreamCacheDestroy(StreamCache* c)
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


void StreamCacheSetSize(StreamCache* c, int nStartPlaySize, int nMaxBufferSize)
{
    pthread_mutex_lock(&c->mutex);
    c->nMaxBufferSize = nMaxBufferSize;
    c->nStartPlaySize = nStartPlaySize;
    pthread_mutex_unlock(&c->mutex);
    return;
}


int StreamCacheGetSize(StreamCache* c)
{
    return c->nTotalDataSize;
}


int StreamCacheUnderflow(StreamCache* c)
{
    int bUnderFlow;
    pthread_mutex_lock(&c->mutex);
    if(c->nFrameNum > 0)
        bUnderFlow = 0;
    else
        bUnderFlow = 1;

//        logd("StreamCacheUnderflow nTotalDataSize = %d, nStartPlaySize = %d, nFrameNum = %d", c->nTotalDataSize, c->nStartPlaySize, c->nFrameNum);
    pthread_mutex_unlock(&c->mutex);
    return bUnderFlow;
}


int StreamCacheOverflow(StreamCache* c)
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


int StreamCacheDataEnough(StreamCache* c)
{
    int bDataEnough;
    pthread_mutex_lock(&c->mutex);
    if(c->nTotalDataSize >= c->nStartPlaySize && c->nFrameNum > 0)
        bDataEnough = 1;
    else
        bDataEnough = 0;
    
//	logd("StreamCacheDataEnough nTotalDataSize = %d, nStartPlaySize = %d, nFrameNum = %d", c->nTotalDataSize, c->nStartPlaySize, c->nFrameNum);
    pthread_mutex_unlock(&c->mutex);
    return bDataEnough;
}


CacheNode* StreamCacheNextFrame(StreamCache* c)
{
    CacheNode* node;
    pthread_mutex_lock(&c->mutex);
    node = c->pHead;
    pthread_mutex_unlock(&c->mutex);
    return node;
}


void StreamCacheFlushOneFrame(StreamCache* c)
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


int StreamCacheAddOneFrame(StreamCache* c, CacheNode* node)
{
    CacheNode* newNode;
    
    newNode = (CacheNode*)malloc(sizeof(CacheNode));
    if(newNode == NULL)
        return -1;
    
    newNode->pData          = node->pData;
    newNode->nLength        = node->nLength;
    newNode->nPts           = node->nPts;
    newNode->nPcr           = node->nPcr;
    newNode->bIsFirstPart   = node->bIsFirstPart;
    newNode->bIsLastPart    = node->bIsLastPart;
    newNode->eMediaType     = node->eMediaType;
    newNode->nStreamIndex   = node->nStreamIndex;
    newNode->nFlags         = node->nFlags;
    newNode->pNext          = NULL;
    
    if(newNode->nPts != -1)
    {
        c->nLastValidPts = node->nPts;
        c->pNodeWithLastValidPts = newNode;
    }
    if(newNode->nPcr != -1)
    {
        c->nLastValidPcr = node->nPcr;
        c->pNodeWithLastValidPcr = newNode;
    }
    
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


void StreamCacheFlushAll(StreamCache* c)
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


int StreamCacheGetBufferFullness(StreamCache* c)
{
    if(c->nMaxBufferSize > 0)
        return (c->nTotalDataSize*100)/c->nMaxBufferSize;
    else
        return 0;
}


int StreamCacheGetLoadingProgress(StreamCache* c)
{
    if(c->nTotalDataSize > c->nStartPlaySize)
        return 100;
    else if(c->nStartPlaySize > 0)
        return (c->nTotalDataSize*100)/c->nStartPlaySize;
    else
        return 0;
}


int StreamCacheSetMediaFormat(StreamCache*           c, 
                              CdxParserTypeT         eContainerFormat, 
                              enum EVIDEOCODECFORMAT eVideoCodecFormat,
                              int                    nBitrate)
{
    c->eContainerFormat  = eContainerFormat;
    c->eVideoCodecFormat = eVideoCodecFormat;
    c->nBitrate          = nBitrate;
    return 0;
}


int StreamCacheSetPlayer(StreamCache* c, Player* pPlayer)
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


int64_t StreamCacheSeekTo(StreamCache* c, int64_t nSeekTimeUs)
{
    int64_t ret;
    switch(c->eContainerFormat)
    {        
        case CDX_PARSER_TS:
        case CDX_PARSER_BD:
        {
            int64_t nCurTimePosUs;
            int64_t nTimeDiffUs;
            int     nByteOffset;
            int     nBitrate;
            
            nBitrate = c->nBitrate;
            if(nBitrate <= 0)
            {
                int i;
                int nStreamNum;
                
                nBitrate = 0;
                if(PlayerHasVideo(c->pPlayer))
                    nBitrate += PlayerGetVideoBitrate(c->pPlayer);
                if(PlayerHasAudio(c->pPlayer))
                    nBitrate += PlayerGetAudioBitrate(c->pPlayer);
                
                if(nBitrate == 0)
                    return -1;
            }
            
            //* 1. get current position.
            nCurTimePosUs = PlayerGetPosition(c->pPlayer);
            if(nCurTimePosUs < 0)
                return -1;
            
            //* 2. calculate time diff.
            nTimeDiffUs = nSeekTimeUs - nCurTimePosUs;
            
            //* 3. if seek forward, substract time diff with player cache time.
            if(nTimeDiffUs > 0)
                nTimeDiffUs -= StreamCachePlayerCacheTime(c->pPlayer);
            
            //* 4. calculate offset with time diff and bitrate.
            nByteOffset = (int)(nBitrate*nTimeDiffUs/(8*1000*1000));
            
            //* 5. seek by offset in cache.
            ret = StreamCacheSeekByOffset(c, nByteOffset);
            if(ret == 0)
                ret = nSeekTimeUs;
            break;
        }
        
        case CDX_PARSER_HLS:
        {
            ret = StreamCacheSeekByPcr(c, nSeekTimeUs);
            break;
        }
        
        default:
        {
            ret = StreamCacheSeekByPts(c, nSeekTimeUs);
            break;
        }
    }
    
    return ret;
}


static int64_t StreamCacheSeekByPts(StreamCache* c, int64_t nSeekTimeUs)
{
    CacheNode* pFirst;
    CacheNode* pNodeFound;
    CacheNode* pNode;
    CacheNode* pLastNodeBefore;
    CacheNode* pFirstNodeAfter;
    int64_t    nLastPtsBefore;
    int64_t    nFirstPtsAfter;
    int64_t    nCurPts;
    int        bIsKeyFrame;
    int        nPassedDataSize;
    int        nPassedFrameNum;
    
    //* make a CacheNode list by attaching the node list to the passed node list.
    //* set the pFirst as the list head, 
    //* and then search a node with nPts nearest to nSeekTimeUs in this list.
    if(c->pPassedTail != NULL)
        c->pPassedTail->pNext = c->pHead;
    pFirst = (c->pPassedHead == NULL) ? c->pHead : c->pPassedHead;
    if(pFirst == NULL)
        return -1;
    
    //* check whether the nSeekTimeUs is in cache.
    if(c->nLastValidPts < nSeekTimeUs)
    {
        //* nSeekTimeUs is not in cache, it is bigger than the last valid pts in cache, return fail.
        if(c->pPassedTail != NULL)
            c->pPassedTail->pNext = NULL;
        return -1;
    }
    
    pNode = pFirst;
    while(pNode != NULL)
    {
        if(pNode->nPts != -1)
        {
            if(pNode->nPts > nSeekTimeUs)
            {
                //* nSeekTimeUs is not in cache, it is smaller than the first valid 
                //* key frame pts in cache, return fail.
                if(c->pPassedTail != NULL)
                    c->pPassedTail->pNext = NULL;
                return -1;
            }
            
            if(StreamCacheIsKeyFrame(c, pNode))
                break;
        }
        
        pNode = pNode->pNext;
    }
    
    if(pNode == NULL)
    {
        //* can not find a key frame with pts smaller than nSeekTimeUs, quit.
        if(c->pPassedTail != NULL)
            c->pPassedTail->pNext = NULL;
        return -1;
    }
    
    //*  find the last node with pts small than nSeekTimeUs,
    //*  find the first node with pts bigger than nSeekTimeUs,
    //*  choose the one with pts more near to nSeekTimeUs as new list head.
    pNode           = pFirst;
    nLastPtsBefore  = -1;
    nFirstPtsAfter  = 0x7fffffffffffffffLL;
    pLastNodeBefore = NULL;
    pFirstNodeAfter = NULL;
    while(pNode != NULL)
    {
        nCurPts = pNode->nPts;
        if(nCurPts != -1)
        {
            bIsKeyFrame = StreamCacheIsKeyFrame(c, pNode);
            if(bIsKeyFrame)
            {
                if(nCurPts <= nSeekTimeUs && nCurPts > nLastPtsBefore)
                {
                    //* update the first node before.
                    nLastPtsBefore  = nCurPts;
                    pLastNodeBefore = pNode;
                }
                else if(nCurPts > nSeekTimeUs)
                {
                    //* set the last node after.
                    nFirstPtsAfter  = nCurPts;
                    pFirstNodeAfter = pNode;
                    break;
                }
            }
        }
        
        pNode = pNode->pNext;
    }
    
    if(pFirstNodeAfter == NULL)
    {
        if(nSeekTimeUs - nLastPtsBefore <= 1000000) //* near enough.
        {
            //* search to the end of the cache and can not find a key frame with pts bigger than 
            //* nSeekTimeUs. But nLastPtsBefore is near enough to the nSeekTimeUs, so set the 
            //* pLastNodeBefore as the node we want. 
            pNodeFound = pLastNodeBefore;
        }
        else
        {
            //* search to the end of the cache and can not find a key frame with pts bigger than 
            //* nSeekTimeUs. As there may be a key frame in new coming data with pts more near 
            //* to nSeekTimeUs, so just return fail to let the parser seek.
            if(c->pPassedTail != NULL)
                c->pPassedTail->pNext = NULL;
            return -1;
        }
    }
    else
    {
        //* choose a key frame node with pts more near to nSeekTimeUs.
        if((nSeekTimeUs - nLastPtsBefore) <= (nFirstPtsAfter - nSeekTimeUs))
            pNodeFound = pLastNodeBefore;
        else
            pNodeFound = pFirstNodeAfter;
    }
    
    //* adjust the list and the passed list according to pNodeFound.
    if(pNodeFound == pFirst)
    {
        c->pPassedHead     = NULL;
        c->pPassedTail     = NULL;
        c->pHead           = pFirst;
        c->nTotalDataSize  += c->nPassedDataSize;
        c->nFrameNum       += c->nPassedFrameNum;
        c->nPassedDataSize = 0;
        c->nPassedFrameNum = 0;
    }
    else
    {
        pNode           = pFirst;
        nPassedDataSize = 0;
        nPassedFrameNum = 0;
        
        while(pNode != NULL)
        {
            nPassedDataSize += pNode->nLength;
            nPassedFrameNum++;
            if(pNode->pNext == pNodeFound)
            {
                pNode->pNext = NULL;
                break;
            }
            pNode = pNode->pNext;
        }
        
        if(c->pPassedHead == NULL)
        {
            c->pPassedHead = pNode;
        }

        c->pPassedTail     = pNode;
        c->pHead           = pNodeFound;
        c->nTotalDataSize  = c->nPassedDataSize + c->nTotalDataSize - nPassedDataSize;
        c->nFrameNum       = c->nPassedFrameNum + c->nFrameNum - nPassedFrameNum;
        c->nPassedFrameNum = nPassedFrameNum;
        c->nPassedDataSize = nPassedDataSize;
        StreamCacheFlushPassedList(c);
    }
    
    return c->pHead->nPts;
}


static int64_t StreamCacheSeekByPcr(StreamCache* c, int64_t nSeekTimeUs)
{
    CacheNode* pFirst;
    CacheNode* pNodeFound;
    CacheNode* pNode;
    CacheNode* pLastNodeBefore;
    CacheNode* pFirstNodeAfter;
    int64_t    nLastPcrBefore;
    int64_t    nFirstPcrAfter;
    int64_t    nCurPcr;
    int64_t    nFoundMappedPts;
    int64_t    nNextMappedPts;
    int64_t    nPtsBase;
    int64_t    nPtsOffset;
    int        bIsKeyFrame;
    int        nPassedDataSize;
    int        nPassedFrameNum;
    
    //* make a CacheNode list by attaching the node list to the passed node list.
    //* set the pFirst as the list head, 
    //* and then search a node with nPcr nearest to nSeekTimeUs in this list.
    if(c->pPassedTail != NULL)
        c->pPassedTail->pNext = c->pHead;
    pFirst = (c->pPassedHead == NULL) ? c->pHead : c->pPassedHead;
    if(pFirst == NULL)
        return -1;
    
    //* check whether the nSeekTimeUs is in cache.
    if(c->pNodeWithLastValidPcr == NULL)
    {
        if(c->pPassedTail != NULL)
            c->pPassedTail->pNext = NULL;
		
        return -1;//hkw
    }
    if(c->nLastValidPcr + (c->nLastValidPts - c->pNodeWithLastValidPcr->nPts) < nSeekTimeUs)
    {
        //* nSeekTimeUs is not in cache, it is bigger than the last valid pts in cache, return fail.
        if(c->pPassedTail != NULL)
            c->pPassedTail->pNext = NULL;
        return -1;
    }
    
    pNode = pFirst;
    while(pNode != NULL)
    {
        if(pNode->nPcr != -1)
        {
            if(pNode->nPcr > nSeekTimeUs)
            {
                //* nSeekTimeUs is not in cache, it is smaller than the first valid 
                //* pcr in cache, return fail.
                if(c->pPassedTail != NULL)
                    c->pPassedTail->pNext = NULL;
                return -1;
            }
            
            break;
        }
        
        pNode = pNode->pNext;
    }
    
    //*  find the last node with pcr small than nSeekTimeUs,
    //*  find the first node with pcr bigger than nSeekTimeUs,
    //*  choose the one with pcr more near to nSeekTimeUs as new list head.
    pNode           = pFirst;
    nLastPcrBefore  = -1;
    nFirstPcrAfter  = 0x7fffffffffffffffLL;
    pLastNodeBefore = NULL;
    pFirstNodeAfter = NULL;
    while(pNode != NULL)
    {
        nCurPcr = pNode->nPcr;
        if(nCurPcr != -1)
        {
            if(nCurPcr <= nSeekTimeUs && nCurPcr > nLastPcrBefore)
            {
                //* update the first node before.
                nLastPcrBefore  = nCurPcr;
                pLastNodeBefore = pNode;
            }
            else if(nCurPcr > nSeekTimeUs)
            {
                //* set the last node after.
                nFirstPcrAfter  = nCurPcr;
                pFirstNodeAfter = pNode;
                break;
            }
        }
        
        pNode = pNode->pNext;
    }
    
    nPtsBase      = pLastNodeBefore->nPcr;
    nPtsOffset    = pLastNodeBefore->nPts;
    
    nFoundMappedPts = nPtsBase;
    pNode           = pLastNodeBefore;
    pNodeFound      = pLastNodeBefore;
    while(pNode->pNext != pFirstNodeAfter)  //* pFirstNodeAfter can be NULL.
    {
        if(pNode->pNext->nPts != -1)
        {
            if(StreamCacheIsKeyFrame(c, pNode->pNext))
            {
                nNextMappedPts = pNode->pNext->nPts - nPtsOffset + nPtsBase;
                if(nNextMappedPts <= nSeekTimeUs && nNextMappedPts > nFoundMappedPts)
                {
                    nFoundMappedPts = nNextMappedPts;
                    pNodeFound = pNode;
                }
            }
        }
        
        pNode = pNode->pNext;
    }
    
    if(nSeekTimeUs - nFoundMappedPts > 1000000)
    {
        //* search to the end of the cache and can not find a frame with mapped pts bigger than 
        //* nSeekTimeUs. As there may be a key frame in new coming data with mapped pts more near 
        //* to nSeekTimeUs, so just return fail to let the parser seek.
        if(c->pPassedTail != NULL)
            c->pPassedTail->pNext = NULL;
        return -1;
    }
    
    //* adjust the list and the passed list according to pNodeFound.
    if(pNodeFound == pFirst)
    {
        c->pPassedHead     = NULL;
        c->pPassedTail     = NULL;
        c->pHead           = pFirst;
        c->nTotalDataSize  += c->nPassedDataSize;
        c->nFrameNum       += c->nPassedFrameNum;
        c->nPassedDataSize = 0;
        c->nPassedFrameNum = 0;
    }
    else
    {
        pNode           = pFirst;
        nPassedDataSize = 0;
        nPassedFrameNum = 0;
        
        while(pNode != NULL)
        {
            nPassedDataSize += pNode->nLength;
            nPassedFrameNum++;
            if(pNode->pNext == pNodeFound)
            {
                pNode->pNext = NULL;
                break;
            }
            pNode = pNode->pNext;
        }
        
        c->pPassedTail     = pNode;
        c->pHead           = pNodeFound;
        c->nTotalDataSize  = c->nPassedDataSize + c->nTotalDataSize - nPassedDataSize;
        c->nFrameNum       = c->nPassedFrameNum + c->nFrameNum - nPassedFrameNum;
        c->nPassedFrameNum = nPassedFrameNum;
        c->nPassedDataSize = nPassedDataSize;
        StreamCacheFlushPassedList(c);
    }
    
    return nFoundMappedPts;
}


static int StreamCacheSeekByOffset(StreamCache* c, int nSeekOffset)
{
    CacheNode* pNodeFound;
    CacheNode* pNode;
    int        nCurOffset;
    int        nPassedDataSize;
    int        nPassedFrameNum;
    
    if((nSeekOffset < -c->nPassedDataSize) || (nSeekOffset >= c->nTotalDataSize))
        return -1;
    
    if(nSeekOffset < 0)
    {
        //* find the last node with offset before nSeekOffset.
        pNode = c->pPassedHead;
        nCurOffset = -c->nPassedDataSize;
        while(pNode != NULL)
        {
            if(pNode->pNext == NULL)
                break;      //* search to the pPassedTail node, the last one.
            
            if(nCurOffset + pNode->nLength > nSeekOffset)
                break;      //* next node offset is after nSeekOffset.
            
            nCurOffset += pNode->nLength;
            pNode = pNode->pNext;
        }
        pNodeFound = pNode;
        
        //* reset the pHead to pNodeFound and adjust the node list and the passed node list.
        c->pPassedTail->pNext = c->pHead;
        if(pNodeFound == c->pPassedHead)
        {
            c->pPassedHead = NULL;
            c->pPassedTail = NULL;
            c->pHead = pNodeFound;
            c->nTotalDataSize += c->nPassedDataSize;
            c->nFrameNum += c->nPassedFrameNum;
            c->nPassedFrameNum = 0;
            c->nPassedDataSize = 0;
        }
        else
        {
            nPassedDataSize = 0;
            nPassedFrameNum = 0;
            pNode = c->pPassedHead;
            while(pNode != NULL)
            {
                nPassedDataSize += pNode->nLength;
                nPassedFrameNum++;
                if(pNode->pNext == pNodeFound)
                {
                    pNode->pNext = NULL;
                    break;
                }
				pNode = pNode->pNext;
            }
            
            c->pPassedTail = pNode;
            c->pHead = pNodeFound;
            c->nTotalDataSize += (c->nPassedDataSize - nPassedDataSize);
            c->nFrameNum += (c->nPassedFrameNum - nPassedFrameNum);
            c->nPassedDataSize = nPassedDataSize;
            c->nPassedFrameNum = nPassedFrameNum;
        }
    }
    else
    {
        //* find the last node with offset before nSeekOffset.
        pNode = c->pHead;
        nCurOffset = 0;
        while(pNode != NULL)
        {
            if(pNode->pNext == NULL)
                break;      //* search to the pTail node, the last one.
            
            if(nCurOffset + pNode->nLength > nSeekOffset)
                break;      //* next node offset is after nSeekOffset.
            
            nCurOffset += pNode->nLength;
            pNode = pNode->pNext;
        }
        pNodeFound = pNode;
        
        //* reset the pHead to pNodeFound and adjust the node list and the passed node list.
        if(pNodeFound != c->pHead)
        {
            nPassedDataSize = 0;
            nPassedFrameNum = 0;
            pNode = c->pHead;
            while(pNode != NULL)
            {
                nPassedDataSize += pNode->nLength;
                nPassedFrameNum++;
                if(pNode->pNext == pNodeFound)
                {
                    pNode->pNext = NULL;
                    break;
                }
				pNode = pNode->pNext;
            }
            
            c->pPassedTail->pNext = c->pHead;
            c->pPassedTail = pNode;
            c->pHead = pNodeFound;
            c->nTotalDataSize  -= nPassedDataSize;
            c->nFrameNum       -= nPassedFrameNum;
            c->nPassedDataSize += nPassedDataSize;
            c->nPassedFrameNum += nPassedFrameNum;
        }
    }
    
    return 0;
}


static int StreamCacheIsKeyFrame(StreamCache* c, CacheNode* pNode)
{
    if(c->eVideoCodecFormat == VIDEO_CODEC_FORMAT_UNKNOWN)
    {
        if(pNode->eMediaType == CDX_MEDIA_AUDIO)
            return 1;
    }
    else
    {
        if(c->eContainerFormat == CDX_PARSER_TS ||
           c->eContainerFormat == CDX_PARSER_BD ||
           c->eContainerFormat == CDX_PARSER_HLS)
        {
            if(c->eVideoCodecFormat == VIDEO_CODEC_FORMAT_H264)
            {
                return StreamCacheIsH264KeyFrame(pNode);
            }
            else if(c->eVideoCodecFormat == VIDEO_CODEC_FORMAT_MPEG2 || c->eVideoCodecFormat == VIDEO_CODEC_FORMAT_MPEG1)
            {
                return StreamCacheIsMpeg12KeyFrame(pNode);
            }
            else if(c->eVideoCodecFormat == VIDEO_CODEC_FORMAT_WMV3)
            {
                return StreamCacheIsWMV3KeyFrame(pNode);
            }
            else if(c->eVideoCodecFormat == VIDEO_CODEC_FORMAT_H265)
            {
                return StreamCacheIsH265KeyFrame(pNode);
            }
            else
                return 0;
        }
        else
        {
            if(pNode->eMediaType == CDX_MEDIA_VIDEO && (pNode->nFlags & KEY_FRAME)!= 0 )
                return 1;
        }
    }
    
    return 0;
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


static int StreamCacheIsMpeg12KeyFrame(CacheNode* pNode)
{
    unsigned int   code;
    unsigned int   pictureType;
    unsigned char* ptr;
    
    if(pNode->nLength < 6)
        return 0;
    
    code = 0xffffffff;
    for(ptr = pNode->pData; ptr <= pNode->pData + pNode->nLength - 6;)
    {
        code = code<<8 | *ptr++;
        if (code == 0x01b3 ||   //* sequence header.
            code == 0x01b8)     //* gop header
            return 1;
        
        if(code == 0x0100)      //* picture header, check picture type.
        {
            pictureType = (ptr[1]>>3) & 0x7;
            if(pictureType == 1)
                return 1;
            else
                return 0;
        }
    }
    
    return 0;
}


static int StreamCacheIsWMV3KeyFrame(CacheNode* pNode)
{
    unsigned int    code;
    unsigned char*  ptr;

    if(pNode->nLength < 16)
        return 0;
    
    code = 0xffffffff;

    for (ptr = pNode->pData; ptr <= pNode->pData + pNode->nLength - 16;)
    {
        code = code<<8 | *ptr++;
        if (code == 0x010f) //* sequence header
            return 1;
    }
    
    return 0;
}


static int StreamCacheIsH264KeyFrame(CacheNode* pNode)
{
    unsigned int   code;
    unsigned int   tmp;
    unsigned char* ptr;

    if(pNode->nLength < 16)
        return 0;

    code = 0xffffffff;

    for (ptr = pNode->pData; ptr <= pNode->pData + pNode->nLength - 16;)
    {
        code = code<<8 | *ptr++;
        tmp = code & 0xffffff1f;
        if (tmp == 0x0107 ||    //* sps
            tmp == 0x0108 ||    //* pps
            tmp == 0x0105)      //* idr
            return 1;
#if 0
        if(tmp == 0x0101)   //* slice NULU, check mbNum==0 and pictureType;
        {
            mbNum = ReadGolomb(...);   //* Ue() not implement here.
            type  = ReadGolomb(...);
            if(mbNum == 0 && (type == 2 || type == 7))
                return 1;
        }
#endif
    }
    
    return 0;
}


static int StreamCacheIsH265KeyFrame(CacheNode* pNode)
{
    unsigned int   code;
    unsigned char* ptr;
    unsigned int   tmp;

    if(pNode->nLength < 16)
        return 0;

    code = 0xffffffff;

    for (ptr = pNode->pData; ptr <= pNode->pData + pNode->nLength - 16;)
    {
        code = code<<8 | *ptr++;
        if (code == 0x0140 ||   //* vps
            code == 0x0142 ||   //* sps
            code == 0x0144 ||   //* pps
            code == 0x0126 ||   //* key frame
            code == 0x0128 ||   //* key frame
            code == 0x012a)     //* key frame
        {
            if(*ptr == 0x01)
                return 1;
        }
    }
    
    return 0;
}

