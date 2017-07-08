
#include <semaphore.h>
#include <pthread.h>
#include "demuxComponent.h"
#include "awMessageQueue.h"
#include "CdxParser.h"          //* parser library in "LIBRARY/DEMUX/PARSER/include/"
#include "CdxStream.h"          //* parser library in "LIBRARY/DEMUX/STREAM/include/"
#include "player.h"             //* player library in "LIBRARY/PLAYER/"
#include "cache.h"
#include "log.h"
#include "AwHDCPModule.h"
#include "awplayerConfig.h"

//* demux status, same with the awplayer.
static const int DEMUX_STATUS_IDLE        = 0;      //* the very beginning status.
static const int DEMUX_STATUS_INITIALIZED = 1<<0;   //* after data source set.
static const int DEMUX_STATUS_PREPARING   = 1<<1;   //* when preparing.
static const int DEMUX_STATUS_PREPARED    = 1<<2;   //* after parser is opened and media info get.
static const int DEMUX_STATUS_STARTED     = 1<<3;   //* parsing and sending data.
static const int DEMUX_STATUS_PAUSED      = 1<<4;   //* sending job paused.
static const int DEMUX_STATUS_STOPPED     = 1<<5;   //* parser closed.
static const int DEMUX_STATUS_COMPLETE    = 1<<6;   //* all data parsed.

//* command id, same with the awplayer.
static const int DEMUX_COMMAND_SET_SOURCE                = 0x101;
static const int DEMUX_COMMAND_PREPARE                   = 0x104;
static const int DEMUX_COMMAND_START                     = 0x105;
static const int DEMUX_COMMAND_PAUSE                     = 0x106;
static const int DEMUX_COMMAND_STOP                      = 0x107;
static const int DEMUX_COMMAND_QUIT                      = 0x109;
static const int DEMUX_COMMAND_SEEK                      = 0x10a;
static const int DEMUX_COMMAND_CLEAR                     = 0x10b;
static const int DEMUX_COMMAND_CANCEL_PREPARE            = 0x10c;
static const int DEMUX_COMMAND_CANCEL_SEEK               = 0x10d;
static const int DEMUX_COMMAND_READ                      = 0x10e;
static const int DEMUX_COMMAND_NOTIFY_FIRST_FRAME_SHOWED = 0x10f;
static const int DEMUX_COMMAND_VIDEO_STREAM_CHANGE       = 0x110;
static const int DEMUX_COMMAND_AUDIO_STREAM_CHANGE       = 0x111;

//* cache start play size and max buffer size.
static const int DEFAULT_START_PLAY_SIZE 	   				 = 256*1024;
static const int DEFAULT_CACHE_START_PLAY_SIZE 			     = 512*1024;
static const int DEFAULT_CACHE_START_PLAY_SIZE_WITHOUT_VIDEO = 10*1024; // just have audio stream
static const int DEFAULT_CACHE_BUFFER_SIZE     				 = 20*1024*1024;
static const int DEFAULT_CACHE_START_PLAY_TIME 				 = 5000;          //* 5 seconds.
static const int MAX_CACHE_START_PLAY_SIZE     				 = 150*1024*1024;
static const int MAX_CACHE_BUFFER_SIZE        				 = 200*1024*1024;

#define kUseSecureInputBuffers 256 //copy from wvm/ExtractorWrapper.cpp

typedef struct DemuxCompContext_t
{
    int                         eStatus;
    int                         bLiveStream;        //* live streaming from network.
    int                         bFileStream;        //* local media file.
    int                         bVodStream;         //* vod stream from network.
    
    //* data source.
    int                         nSourceType;        //* url or fd or IStreamSource.
    CdxDataSourceT              source;
    IStreamSource*              pStreamSource;
    MediaInfo                   mediaInfo;
    
    pthread_t                   threadId;
    AwMessageQueue*             mq;
    
    CdxParserT*                 pParser;
	CdxStreamT*					pStream;
    Player*                     pPlayer;
    DemuxCallback               callback;
    void*                       pUserData;
    
    pthread_mutex_t             mutex;
    pthread_mutex_t             mutex1;
	sem_t                       semSetDataSource;
	sem_t                       semStart;
	sem_t                       semStop;
	sem_t                       semQuit;
	sem_t                       semClear;
	sem_t                       semCancelPrepare;
	sem_t                       semCancelSeek;
    sem_t                       semStreamChange;
	
	int                         nSetDataSourceReply;
    int                         nStartReply;
    int                         nStopReply;
    
    pthread_t                   cacheThreadId;
    AwMessageQueue*             mqCache;
	sem_t                       semCache;
    int                         nCacheReply;
    StreamCache*                pCache;
    int                         bBufferring;
    int                         bEOS;
    int                         bIOError;
    
    struct ParserCacheStateS    sCacheState;        //* for buffering status update notify.
    int64_t                     nCacheStatReportIntervalUs;
    enum ECACHEPOLICY           eCachePolicy;
    int                         nCacheTimeMs;
    
    int                         bCancelPrepare;
    int                         bCancelSeek;
    int                         bSeeking;
    int                         bStopping;
    
    int                         bBufferStartNotified;
    int                         bNeedPausePlayerNotified;

	//* YUNOS Info
	cdx_int64					tsSendTime[2];
	cdx_int64					tsFirstBtime[2];
	cdx_int64					tsLastBtime;
	int 						tsNum;
	cdx_int64					tsBandwidth;//* m3u8
	cdx_int64					tsLength;
	cdx_int64					tsDuration;
	char						tsHttpTcpIP[128];
	int 						errorCode;
	char						httpRes[4096];
	char						mYunOSUUID[32];
	int							mYunOSInfoEndale;

}DemuxCompContext;

struct BufferInfo {
	uint32_t size;
	void * buffer;
};

static void* DemuxThread(void* arg);
static void* CacheThread(void* arg);

#if (CONFIG_OS_VERSION >= OPTION_OS_VERSION_ANDROID_5_0)
static int setDataSourceFields(DemuxCompContext *demuxHdr, CdxDataSourceT* source, void* pHTTPServer, char* uri, KeyedVector<String8,String8>* pHeader);
#else
static int setDataSourceFields(DemuxCompContext *demuxHdr, CdxDataSourceT* source, char* uri, KeyedVector<String8,String8>* pHeader);
#endif

static void clearDataSourceFields(CdxDataSourceT* source);

static int setMediaInfo(MediaInfo* pMediaInfo, CdxMediaInfoT* pInfoFromParser);
static void clearMediaInfo(MediaInfo* pMediaInfo);

static int PlayerBufferOverflow(Player* p);
static int PlayerBufferUnderflow(Player* p);

static int GetCacheState(DemuxCompContext* demux);
static int DemuxCompAdjustCacheParamsWithBitrate(DemuxCompContext* demux, int nBitrate);
static int DemuxCompSetCacheTime(DemuxCompContext* demux, int nCacheTimeMs);
static int DemuxCompUseDefaultCacheParams(DemuxCompContext* demux);
static void AdjsutCacheParams(DemuxCompContext* demux);

static void NotifyCacheState(DemuxCompContext* demux);
static void PostReadMessage(AwMessageQueue* mq);
static int64_t GetSysTime();

static int CheckParserWhetherBeForbided(CdxParserTypeE eType);
static int base64Encode(const unsigned char *in_ptr,  unsigned long encode_len,  unsigned char *out_ptr);


extern "C"
{
/*
        char *dirPath = (char *)((int *)param)[0];
        int *pDirId = (int *)((int *)param)[1];
*/
static int DemuxCBOpenDir(void *cbhdr, const char *name, void **pDirHdr)
{
    DemuxCompContext *d = (DemuxCompContext *)cbhdr;
    uintptr_t msgParam[4];
    int dirId = -1;
    
    msgParam[0] = (uintptr_t)name;
    msgParam[1] = (uintptr_t)&dirId;
    
    d->callback(d->pUserData, DEMUX_IOREQ_OPENDIR, msgParam);

	uintptr_t nTmpDirId = dirId;
    *pDirHdr = (void *)nTmpDirId;
	
    return (dirId == -1) ? -1 : 0;
}

/*
        int dirId = ((int *)param)[0];
        int *pRet = (int *)((int *)param)[1];
        char *buf = (char *)((int *)param)[2];
        int bufLen = ((int *)param)[3];
*/
static int DemuxCBReadDir(void *cbhdr, void *dirHdr, char *dname, int dnameLen)
{
    DemuxCompContext *d = (DemuxCompContext *)cbhdr;
    uintptr_t msgParam[4];
    int ret = -1;
    //int dirId = (int)dirHdr;
    
    msgParam[0] = (uintptr_t)dirHdr;
    msgParam[1] = (uintptr_t)&ret;
    msgParam[2] = (uintptr_t)dname;
    msgParam[3] = dnameLen;
    
    d->callback(d->pUserData, DEMUX_IOREQ_READDIR, msgParam);
    return ret;
}

/*
        int dirId = ((int *)param)[0];
        int *pRet = (int *)((int *)param)[1];
*/
static int DemuxCBCloseDir(void *cbhdr, void *dirHdr)
{
    DemuxCompContext *d = (DemuxCompContext *)cbhdr;
    uintptr_t msgParam[4];
    int ret;
    //int dirId = (int)dirHdr;
    
    msgParam[0] = (uintptr_t)dirHdr;
    msgParam[1] = (uintptr_t)&ret;
    
    d->callback(d->pUserData, DEMUX_IOREQ_CLOSEDIR, msgParam);
    return ret;
}

/*
        char *filePath = (char *)((int *)param)[0];
        int *pFd = (int *)((int *)param)[1];
*/
static int DemuxCBOpenFile(void *cbhdr, const char *pathname, int flags)
{
    DemuxCompContext *d = (DemuxCompContext *)cbhdr;
    uintptr_t msgParam[4];
    int fd = -1;

	CEDARX_UNUSE(flags);
	
    msgParam[0] = (uintptr_t)pathname;
    msgParam[1] = (uintptr_t)&fd;
    
    d->callback(d->pUserData, DEMUX_IOREQ_OPEN, msgParam);
    return fd;
}

/*
        char *filePath = (char *)((int *)param)[0];
        int mode = ((int *)param)[1];
        int *pRet = (int *)((int *)param)[2];
*/
static int DemuxCBAccessFile(void *cbhdr, const char *pathname, int mode)
{
    DemuxCompContext *d = (DemuxCompContext *)cbhdr;
    uintptr_t msgParam[4];
    int ret = -1;
    
    msgParam[0] = (uintptr_t)pathname;
    msgParam[1] = mode;    
    msgParam[2] = (uintptr_t)&ret;
    
    d->callback(d->pUserData, DEMUX_IOREQ_ACCESS, msgParam);

    return ret;
}

static IoOperationS demuxIoOps =
{
    /*.openDir = */DemuxCBOpenDir,
    /*.readDir = */DemuxCBReadDir,
    /*.closeDir = */DemuxCBCloseDir,
    /*.openFile = */DemuxCBOpenFile,
    /*.accessFile = */DemuxCBAccessFile
};
static int ParserCallbackProcess(void* pUserData, int eMessageId, void* param)
{
    DemuxCompContext *demux = (DemuxCompContext *)pUserData;
    
    switch(eMessageId)
    {
        case PARSER_NOTIFY_VIDEO_STREAM_CHANGE:
		{
			CdxMediaInfoT parserMediaInfo;
            memset(&parserMediaInfo,0,sizeof(CdxMediaInfoT));
			CdxParserGetMediaInfo(demux->pParser, &parserMediaInfo);
			setMediaInfo(&demux->mediaInfo, &parserMediaInfo);
			demux->mediaInfo.eContainerType = (enum ECONTAINER)demux->pParser->type;
             
            //* if have cacheThread, we should make the demuxThread do callback()
            //* or will cause some async bug
            if(demux->cacheThreadId != 0)
            {
                AwMessage msg;
                setMessage(&msg, 
                           DEMUX_COMMAND_VIDEO_STREAM_CHANGE,   //* message id
                          (uintptr_t)&demux->semStreamChange,//* sem
                           0);                                  //* replyVuale
                AwMessageQueuePostMessage(demux->mq, &msg);

                SemTimedWait(&demux->semStreamChange, -1);
            }
            else
            {
                int nMsg = DEMUX_VIDEO_STREAM_CHANGE;
                demux->callback(demux->pUserData, nMsg, param);
            }
            break;
		}
		case PARSER_NOTIFY_AUDIO_STREAM_CHANGE:
		{
			CdxMediaInfoT parserMediaInfo;
            memset(&parserMediaInfo,0,sizeof(CdxMediaInfoT));
			CdxParserGetMediaInfo(demux->pParser, &parserMediaInfo);
			setMediaInfo(&demux->mediaInfo, &parserMediaInfo);
			demux->mediaInfo.eContainerType = (enum ECONTAINER)demux->pParser->type;
            
            //* if have cacheThread, we should make the demuxThread do callback()
            //* or will cause some async bug
            if(demux->cacheThreadId != 0)
            {
                AwMessage msg1;
                setMessage(&msg1, 
                           DEMUX_COMMAND_AUDIO_STREAM_CHANGE,   //* message id
                          (uintptr_t)&demux->semStreamChange,//* sem
                           0);                                  //* replyVuale
                AwMessageQueuePostMessage(demux->mq, &msg1);

                SemTimedWait(&demux->semStreamChange, -1);
            }
            else
            {
                int nMsg = DEMUX_VIDEO_STREAM_CHANGE;
                demux->callback(demux->pUserData, nMsg, param);
            }
			break;
		}
		case STREAM_EVT_DOWNLOAD_START_TIME:
		{
			demux->tsSendTime[0] = *(cdx_int64*)param;
			int nMsg = STREAM_EVT_DOWNLOAD_START;
			demux->callback(demux->pUserData, nMsg, NULL);
			break;
		}

		case STREAM_EVT_DOWNLOAD_FIRST_TIME:
		{
			demux->tsFirstBtime[0] = *(cdx_int64*)param;
			break;
		}
		case STREAM_EVT_DOWNLOAD_END_TIME:
		{
		    demux->tsSendTime[1] = demux->tsSendTime[0];
		    demux->tsFirstBtime[1] = demux->tsFirstBtime[0];
			demux->tsLastBtime = *(cdx_int64*)param;
			int nMsg = STREAM_EVT_DOWNLOAD_END;
			demux->callback(demux->pUserData, nMsg, NULL);
			break;
		}
		case STREAM_EVT_DOWNLOAD_RESPONSE_HEADER:
		{
			int length = strlen((char*)param);
			memset(demux->httpRes, 0x00, length);
			memcpy(demux->httpRes, (char*)param, length);
			break;
		}
		case STREAM_EVT_DOWNLOAD_GET_TCP_IP:
		{
			strcpy(demux->tsHttpTcpIP, (char*)param);
			break;
		}
		case STREAM_EVT_DOWNLOAD_DOWNLOAD_ERROR:
		{
			int nMsg = STREAM_EVT_DOWNLOAD_DOWNLOAD_ERROR;
			demux->callback(demux->pUserData, nMsg, param);
			break;
		}

        default:
            logw("ignore demux callback message, eMessageId = 0x%x.", eMessageId);
            return -1;
    }
    
    return 0;
}

} /*extern "C" */

DemuxComp* DemuxCompCreate(void)
{
    DemuxCompContext* d;
    
    d = (DemuxCompContext*)malloc(sizeof(DemuxCompContext));
    if(d == NULL)
    {
        loge("malloc memory fail.");
        return NULL;
    }
    memset(d, 0, sizeof(DemuxCompContext));
    
    d->nCacheStatReportIntervalUs = 1000000;
    
    pthread_mutex_init(&d->mutex, NULL);
    pthread_mutex_init(&d->mutex1, NULL);
    sem_init(&d->semSetDataSource, 0, 0);
    sem_init(&d->semStart, 0, 0);
    sem_init(&d->semStop, 0, 0);
    sem_init(&d->semQuit, 0, 0);
    sem_init(&d->semClear, 0, 0);
    sem_init(&d->semCancelPrepare, 0, 0);
    sem_init(&d->semCancelSeek, 0, 0);
    sem_init(&d->semCache, 0, 0);
    sem_init(&d->semStreamChange, 0, 0);
    
    d->mq = AwMessageQueueCreate(64);
    if(d->mq == NULL)
    {
        loge("AwMessageQueueCreate() return fail.");
        pthread_mutex_destroy(&d->mutex);
        pthread_mutex_destroy(&d->mutex1);
        sem_destroy(&d->semSetDataSource);
        sem_destroy(&d->semStart);
        sem_destroy(&d->semStop);
        sem_destroy(&d->semQuit);
        sem_destroy(&d->semClear);
        sem_destroy(&d->semCancelPrepare);
        sem_destroy(&d->semCancelSeek);
        sem_destroy(&d->semCache);
        sem_destroy(&d->semStreamChange);
        free(d);
        return NULL;
    }
    
    d->mqCache = AwMessageQueueCreate(64);
    if(d->mqCache == NULL)
    {
        loge("AwMessageQueueCreate() return fail.");
        AwMessageQueueDestroy(d->mq);
        pthread_mutex_destroy(&d->mutex);
		pthread_mutex_destroy(&d->mutex1);
        sem_destroy(&d->semSetDataSource);
        sem_destroy(&d->semStart);
        sem_destroy(&d->semStop);
        sem_destroy(&d->semQuit);
        sem_destroy(&d->semClear);
        sem_destroy(&d->semCancelPrepare);
        sem_destroy(&d->semCancelSeek);
        sem_destroy(&d->semCache);
        sem_destroy(&d->semStreamChange);
        free(d);
        return NULL;
    }
    
    if(pthread_create(&d->threadId, NULL, DemuxThread, (void*)d) != 0)
    {
        loge("can not create thread for demux component.");
        AwMessageQueueDestroy(d->mq);
        AwMessageQueueDestroy(d->mqCache);
        pthread_mutex_destroy(&d->mutex);
		pthread_mutex_destroy(&d->mutex1);
        sem_destroy(&d->semSetDataSource);
        sem_destroy(&d->semStart);
        sem_destroy(&d->semStop);
        sem_destroy(&d->semQuit);
        sem_destroy(&d->semClear);
        sem_destroy(&d->semCancelPrepare);
        sem_destroy(&d->semCancelSeek);
        sem_destroy(&d->semCache);
        sem_destroy(&d->semStreamChange);
        free(d);
        return NULL;
    }
    
    d->pCache = StreamCacheCreate();
    
    return (DemuxComp*)d;
}


void DemuxCompDestroy(DemuxComp* d)
{
    void* status;
    
    AwMessage msg;
    DemuxCompContext* demux;
    
    demux = (DemuxCompContext*)d;
    
    //* lock mutex to sync with the message process thread when processing SEEK message.
    //* when processing the SEEK message, we clear the Parser's force stop flag by calling CdxParserClrForceStop().
    //* if we're playing a network stream and processing seek message, when user wants to stop,
    //* we should set the parser's force stop flag by calling CdxParserForceStop() to 
    //* prevend from blocking at the network io.
    pthread_mutex_lock(&demux->mutex);
    demux->bStopping = 1;
    pthread_mutex_unlock(&demux->mutex);
    //* send a quit message.
    setMessage(&msg, DEMUX_COMMAND_QUIT, (uintptr_t)&demux->semQuit);
    AwMessageQueuePostMessage(demux->mq, &msg);
    SemTimedWait(&demux->semQuit, -1);
    
    pthread_join(demux->threadId, &status);
    
    StreamCacheDestroy(demux->pCache);
    
    if(demux->mq != NULL)
        AwMessageQueueDestroy(demux->mq);
    
    if(demux->mqCache != NULL)
        AwMessageQueueDestroy(demux->mqCache);
    
    pthread_mutex_destroy(&demux->mutex);
	pthread_mutex_destroy(&demux->mutex1);
    sem_destroy(&demux->semSetDataSource);
    sem_destroy(&demux->semStart);
    sem_destroy(&demux->semStop);
    sem_destroy(&demux->semQuit);
    sem_destroy(&demux->semClear);
    sem_destroy(&demux->semCancelPrepare);
    sem_destroy(&demux->semCancelSeek);
    sem_destroy(&demux->semCache);
    sem_destroy(&demux->semStreamChange);
    free(demux);
    
    return;
}


void DemuxCompClear(DemuxComp* d)  //* clear the data source, like just created.
{
    AwMessage msg;
    DemuxCompContext* demux;
    
    demux = (DemuxCompContext*)d;
    
    //* lock mutex to sync with the message process thread when processing SEEK message.
    //* when processing the SEEK message, we clear the Parser's force stop flag by calling CdxParserClrForceStop().
    //* if we're playing a network stream and processing seek message, when user wants to stop,
    //* we should set the parser's force stop flag by calling CdxParserForceStop() to 
    //* prevend from blocking at the network io.
    pthread_mutex_lock(&demux->mutex);
    demux->bStopping = 1;
    pthread_mutex_unlock(&demux->mutex);
    //* send clear message.
    setMessage(&msg, DEMUX_COMMAND_CLEAR, (uintptr_t)&demux->semClear);
    AwMessageQueuePostMessage(demux->mq, &msg);
    SemTimedWait(&demux->semClear, -1);
    
    return;
}


#if (CONFIG_OS_VERSION >= OPTION_OS_VERSION_ANDROID_5_0)
int DemuxCompSetUrlSource(DemuxComp* d, void* pHTTPServer, const char* pUrl, const KeyedVector<String8, String8>* pHeaders)
#else
int DemuxCompSetUrlSource(DemuxComp* d, const char* pUrl, const KeyedVector<String8, String8>* pHeaders)
#endif

{
    AwMessage msg;
    DemuxCompContext* demux;
    
    demux = (DemuxCompContext*)d;

#if (CONFIG_OS_VERSION >= OPTION_OS_VERSION_ANDROID_5_0)
	//* send a set data source message.
	setMessage(&msg, 
			   DEMUX_COMMAND_SET_SOURCE,					//* message id.
			   (uintptr_t)&demux->semSetDataSource,		//* params[0] = &semSetDataSource.
			   (uintptr_t)&demux->nSetDataSourceReply,	//* params[1] = &nSetDataSourceReply.
			   SOURCE_TYPE_URL, 							//* params[2] = SOURCE_TYPE_URL.
			   (uintptr_t)pUrl,							//* params[3] = pUrl.
			   (uintptr_t)pHeaders,   					//* params[4] = KeyedVector<String8, String8>* pHeaders;
               (uintptr_t)pHTTPServer);                  //* params[5] = pHTTPServer;

#else
    //* send a set data source message.
    setMessage(&msg, 
               DEMUX_COMMAND_SET_SOURCE,                    //* message id.
               (uintptr_t)&demux->semSetDataSource,      //* params[0] = &semSetDataSource.
               (uintptr_t)&demux->nSetDataSourceReply,   //* params[1] = &nSetDataSourceReply.
               SOURCE_TYPE_URL,                             //* params[2] = SOURCE_TYPE_URL.
               (uintptr_t)pUrl,                          //* params[3] = pUrl.
               (uintptr_t)pHeaders);                     //* params[4] = KeyedVector<String8, String8>* pHeaders;
#endif
   
    AwMessageQueuePostMessage(demux->mq, &msg);
    SemTimedWait(&demux->semSetDataSource, -1);
    
    return demux->nSetDataSourceReply;
}


int DemuxCompSetFdSource(DemuxComp* d, int fd, int64_t nOffset, int64_t nLength)
{
    AwMessage msg;
    DemuxCompContext* demux;
    
    demux = (DemuxCompContext*)d;
    
    //* send a set data source message.
    setMessage(&msg, 
               DEMUX_COMMAND_SET_SOURCE,                    //* message id.
               (uintptr_t)&demux->semSetDataSource,      //* params[0] = &semSetDataSource.
               (uintptr_t)&demux->nSetDataSourceReply,   //* params[1] = &nSetDataSourceReply.
               SOURCE_TYPE_FD,                              //* params[2] = SOURCE_TYPE_FD.
               fd,                                          //* params[3] = fd.
               (uintptr_t)(nOffset>>32),                 //* params[4] = high 32 bits of offset.
               (uintptr_t)(nOffset & 0xffffffff),        //* params[5] = low 32 bits of offset.
               (uintptr_t)(nLength>>32),                 //* params[6] = high 32 bits of length.
               (uintptr_t)(nLength & 0xffffffff));       //* params[7] = low 32 bits of length.
    AwMessageQueuePostMessage(demux->mq, &msg);
    SemTimedWait(&demux->semSetDataSource, -1);
    
    return demux->nSetDataSourceReply;
}


int DemuxCompSetStreamSource(DemuxComp* d, const char* pUri)
{
    AwMessage msg;
    DemuxCompContext* demux;
    
    demux = (DemuxCompContext*)d;
    
    //* send a set data source message.
    setMessage(&msg, 
               DEMUX_COMMAND_SET_SOURCE,                    //* message id.
               (uintptr_t)&demux->semSetDataSource,      //* params[0] = &semSetDataSource.
               (uintptr_t)&demux->nSetDataSourceReply,   //* params[1] = &nSetDataSourceReply.
               SOURCE_TYPE_ISTREAMSOURCE,                   //* params[2] = SOURCE_TYPE_ISTREAMSOURCE.
               (uintptr_t)pUri);                 		//* params[3] = uri of IStreamSource.
    AwMessageQueuePostMessage(demux->mq, &msg);
    SemTimedWait(&demux->semSetDataSource, -1);
    
    return demux->nSetDataSourceReply;
}


int DemuxCompSetPlayer(DemuxComp* d, Player* player)
{
    DemuxCompContext* demux;
    demux = (DemuxCompContext*)d;
    demux->pPlayer = player;
    return 0;
}


int DemuxCompSetCallback(DemuxComp* d, DemuxCallback callback, void* pUserData)
{
    DemuxCompContext* demux;
    demux = (DemuxCompContext*)d;
    demux->callback  = callback;
    demux->pUserData = pUserData;
    return 0;
}


int DemuxCompPrepareAsync(DemuxComp* d)
{
    AwMessage msg;
    DemuxCompContext* demux;
    
    demux = (DemuxCompContext*)d;
    
    demux->bCancelPrepare = 0;      //* clear this flag, you can set this flag to make the parser quit preparing.
    demux->eStatus = DEMUX_STATUS_PREPARING;
    
    //* send a prepare message.
    setMessage(&msg, DEMUX_COMMAND_PREPARE);
    AwMessageQueuePostMessage(demux->mq, &msg);
    return 0;
}


int DemuxCompCancelPrepare(DemuxComp* d)   //* should call back DEMUX_PREPARE_FINISH message.
{
    AwMessage msg;
    DemuxCompContext* demux;
    
    demux = (DemuxCompContext*)d;
    
    demux->bCancelPrepare = 1;      //* set this flag to make the parser quit preparing.
	pthread_mutex_lock(&demux->mutex);
	if(demux->pParser)
	{
		CdxParserForceStop(demux->pParser);
	}
	else if(demux->pStream)
	{
		CdxStreamForceStop(demux->pStream);
	}
	pthread_mutex_unlock(&demux->mutex);
    //* send a prepare.
    setMessage(&msg, 
               DEMUX_COMMAND_CANCEL_PREPARE,            //* message id.
               (uintptr_t)&demux->semCancelPrepare,  //* params[0] = &semCancelPrepare.
               0);                                      //* no reply.
    AwMessageQueuePostMessage(demux->mq, &msg);
    SemTimedWait(&demux->semCancelPrepare, -1);
    return 0;
}

int DemuxProbeH265RefPictureNumber(char* pDataBuf, int nDataLen)
{
	return probeH265RefPictureNumber((cdx_uint8 *)pDataBuf, (cdx_uint32)nDataLen);
}

MediaInfo* DemuxCompGetMediaInfo(DemuxComp* d)
{
    DemuxCompContext*   demux;
    MediaInfo*          mi;
    MediaInfo*          myMediaInfo;
    int                 i;
    VideoStreamInfo*    pVideoStreamInfo;
    AudioStreamInfo*    pAudioStreamInfo;
    SubtitleStreamInfo* pSubtitleStreamInfo;
    int                 nCodecSpecificDataLen;
    char*               pCodecSpecificData;
    
    demux = (DemuxCompContext*)d;
    
    myMediaInfo = &demux->mediaInfo;
    mi = (MediaInfo*)malloc(sizeof(MediaInfo));
    if(mi == NULL)
    {
        loge("can not alloc memory for media info.");
        return NULL;
    }
    memset(mi, 0, sizeof(MediaInfo));
    mi->nFileSize      = myMediaInfo->nFileSize;
    mi->nDurationMs    = myMediaInfo->nDurationMs;
    mi->nBitrate       = myMediaInfo->nBitrate;
    mi->eContainerType = myMediaInfo->eContainerType;
    mi->bSeekable      = myMediaInfo->bSeekable;
	mi->nFirstPts      = myMediaInfo->nFirstPts;
    memcpy(mi->cRotation,myMediaInfo->cRotation,4*sizeof(unsigned char));
    
    logv("video stream num = %d, video stream info = %p", myMediaInfo->nVideoStreamNum, myMediaInfo->pVideoStreamInfo);
    
    if(myMediaInfo->nVideoStreamNum > 0)
    {
        pVideoStreamInfo = (VideoStreamInfo*)malloc(sizeof(VideoStreamInfo)*myMediaInfo->nVideoStreamNum);
        if(pVideoStreamInfo == NULL)
        {
            loge("can not alloc memory for media info.");
            clearMediaInfo(mi);
            return NULL;
        }
        memset(pVideoStreamInfo, 0, sizeof(VideoStreamInfo)*myMediaInfo->nVideoStreamNum);
        mi->pVideoStreamInfo = pVideoStreamInfo;
        
        for(i=0; i<myMediaInfo->nVideoStreamNum; i++)
        {
            pVideoStreamInfo = &mi->pVideoStreamInfo[i];
            memcpy(pVideoStreamInfo, &myMediaInfo->pVideoStreamInfo[i], sizeof(VideoStreamInfo));
            
            pCodecSpecificData    = pVideoStreamInfo->pCodecSpecificData;
            nCodecSpecificDataLen = pVideoStreamInfo->nCodecSpecificDataLen;
            pVideoStreamInfo->pCodecSpecificData    = NULL;
            pVideoStreamInfo->nCodecSpecificDataLen = 0;
            pVideoStreamInfo->bSecureStreamFlag     = myMediaInfo->pVideoStreamInfo[i].bSecureStreamFlag;
            
            if(pCodecSpecificData != NULL && nCodecSpecificDataLen > 0)
            {
                pVideoStreamInfo->pCodecSpecificData = (char*)malloc(nCodecSpecificDataLen);
                if(pVideoStreamInfo->pCodecSpecificData == NULL)
                {
                    loge("can not alloc memory for media info.");
                    clearMediaInfo(mi);
                    return NULL;
                }
                
                memcpy(pVideoStreamInfo->pCodecSpecificData, pCodecSpecificData, nCodecSpecificDataLen);
                pVideoStreamInfo->nCodecSpecificDataLen = nCodecSpecificDataLen;
            }
            if (demux->pParser->type == CDX_PARSER_TS ||
                demux->pParser->type == CDX_PARSER_BD || 
                demux->pParser->type == CDX_PARSER_HLS)
            {
                pVideoStreamInfo->bIsFramePackage = 0; /* stream package */
            }
            else
            {
                pVideoStreamInfo->bIsFramePackage = 1; /* frame package */
            }
        }
        
        mi->nVideoStreamNum = myMediaInfo->nVideoStreamNum;
    }
    
    logv("video stream num = %d, video stream info = %p", mi->nVideoStreamNum, mi->pVideoStreamInfo);
    
    if(myMediaInfo->nAudioStreamNum > 0)
    {
        pAudioStreamInfo = (AudioStreamInfo*)malloc(sizeof(AudioStreamInfo)*myMediaInfo->nAudioStreamNum);
        if(pAudioStreamInfo == NULL)
        {
            loge("can not alloc memory for media info.");
            clearMediaInfo(mi);
            return NULL;
        }
        memset(pAudioStreamInfo, 0, sizeof(AudioStreamInfo)*myMediaInfo->nAudioStreamNum);
        mi->pAudioStreamInfo = pAudioStreamInfo;
        
        for(i=0; i<myMediaInfo->nAudioStreamNum; i++)
        {
            pAudioStreamInfo = &mi->pAudioStreamInfo[i];
            memcpy(pAudioStreamInfo, &myMediaInfo->pAudioStreamInfo[i], sizeof(AudioStreamInfo));
            
            pCodecSpecificData    = pAudioStreamInfo->pCodecSpecificData;
            nCodecSpecificDataLen = pAudioStreamInfo->nCodecSpecificDataLen;
            pAudioStreamInfo->pCodecSpecificData    = NULL;
            pAudioStreamInfo->nCodecSpecificDataLen = 0;
            
            if(pCodecSpecificData != NULL && nCodecSpecificDataLen > 0)
            {
                pAudioStreamInfo->pCodecSpecificData = (char*)malloc(nCodecSpecificDataLen);
                if(pAudioStreamInfo->pCodecSpecificData == NULL)
                {
                    loge("can not alloc memory for media info.");
                    clearMediaInfo(mi);
                    return NULL;
                }
                
                memcpy(pAudioStreamInfo->pCodecSpecificData, pCodecSpecificData, nCodecSpecificDataLen);
                pAudioStreamInfo->nCodecSpecificDataLen = nCodecSpecificDataLen;
            }
        }
        
        mi->nAudioStreamNum = myMediaInfo->nAudioStreamNum;
    }
    
    if(myMediaInfo->nSubtitleStreamNum > 0)
    {
        pSubtitleStreamInfo = (SubtitleStreamInfo*)malloc(sizeof(SubtitleStreamInfo)*myMediaInfo->nSubtitleStreamNum);
        if(pSubtitleStreamInfo == NULL)
        {
            loge("can not alloc memory for media info.");
            clearMediaInfo(mi);
            return NULL;
        }
        memset(pSubtitleStreamInfo, 0, sizeof(SubtitleStreamInfo)*myMediaInfo->nSubtitleStreamNum);
        mi->pSubtitleStreamInfo = pSubtitleStreamInfo;
        
        for(i=0; i<myMediaInfo->nSubtitleStreamNum; i++)
        {
            pSubtitleStreamInfo = &mi->pSubtitleStreamInfo[i];
            memcpy(pSubtitleStreamInfo, &myMediaInfo->pSubtitleStreamInfo[i], sizeof(SubtitleStreamInfo));
            
            //* parser only process imbedded subtitle stream in media file.
            pSubtitleStreamInfo->pUrl  = NULL;
            pSubtitleStreamInfo->fd    = -1;
            pSubtitleStreamInfo->fdSub = -1;
        }
        
        mi->nSubtitleStreamNum = myMediaInfo->nSubtitleStreamNum;
    }
    
    return mi;
}


int DemuxCompStart(DemuxComp* d)
{
    AwMessage msg;
    DemuxCompContext* demux;
    
    demux = (DemuxCompContext*)d;
    
    if(demux->eStatus == DEMUX_STATUS_STARTED && demux->bBufferring != 1)
    {
        logi("demux component already in started status.");
        return 0;
    }
    
    if(demux->eStatus == DEMUX_STATUS_COMPLETE)
    {
        logv("demux component is in complete status.");
        return 0;
    }
    
    if(pthread_equal(pthread_self(), demux->threadId))
    {
        //* called from demux callback to awplayer.
        if(demux->bSeeking)
        {
            demux->eStatus = DEMUX_STATUS_STARTED;
        }
        return 0;
    }
    
    //* send a start message.
    setMessage(&msg, 
               DEMUX_COMMAND_START,                 //* message id.
               (uintptr_t)&demux->semStart,      //* params[0] = &SemStart.
               (uintptr_t)&demux->nStartReply);  //* params[1] = &nStartReply.
    AwMessageQueuePostMessage(demux->mq, &msg);
    SemTimedWait(&demux->semStart, -1);
    return demux->nStartReply;
}


int DemuxCompStop(DemuxComp* d)    //* close the data source, must call prepare again to restart.
{
    AwMessage msg;
    DemuxCompContext* demux;
    
    demux = (DemuxCompContext*)d;
    
    //* lock mutex to sync with the message process thread when processing SEEK message.
    //* when processing the SEEK message, we clear the Parser's force stop flag by calling CdxParserClrForceStop().
    //* if we're playing a network stream and processing seek message, when user wants to stop,
    //* we should set the parser's force stop flag by calling CdxParserForceStop() to 
    //* prevend from blocking at the network io.
    pthread_mutex_lock(&demux->mutex);
    demux->bStopping = 1;
    pthread_mutex_unlock(&demux->mutex);
    if(demux->pParser != NULL)
        CdxParserForceStop(demux->pParser); //* quit from reading or seeking.
    
    //* send a start message.
    setMessage(&msg, 
               DEMUX_COMMAND_STOP,                  //* message id.
               (uintptr_t)&demux->semStop,       //* params[0] = &mSemStart.
               (uintptr_t)&demux->nStopReply);   //* params[1] = &mStartReply.
    AwMessageQueuePostMessage(demux->mq, &msg);
    SemTimedWait(&demux->semStop, -1);
    return demux->nStopReply;
}


int DemuxCompPause(DemuxComp* d)   //* no pause status in demux component, return OK immediately.
{
    DemuxCompContext* demux;
    
    demux = (DemuxCompContext*)d;
    if(demux->eStatus != DEMUX_STATUS_STARTED)
    {
        logw("invalid pause operation, demux component not in started status.");
        return -1;
    }
    
    //* currently the demux component has no pause status, it will keep sending data until stopped.
    return 0;
}


int DemuxCompGetStatus(DemuxComp* d)
{
    DemuxCompContext* demux;
    demux = (DemuxCompContext*)d;
    return demux->eStatus;
}


int DemuxCompSeekTo(DemuxComp* d, int nSeekTimeMs)
{
    AwMessage msg;
    DemuxCompContext* demux;
    
    demux = (DemuxCompContext*)d;
    
    demux->bCancelSeek = 0;
    demux->bSeeking = 1;
    if(demux->pParser != NULL && demux->eStatus == DEMUX_STATUS_STARTED)
        CdxParserForceStop(demux->pParser); //* quit from reading.
    
    //* send a start message.
    setMessage(&msg, DEMUX_COMMAND_SEEK, 0, 0, nSeekTimeMs);
    AwMessageQueuePostMessage(demux->mq, &msg);
    return 0;
}


int DemuxCompCancelSeek(DemuxComp* d)  //* should not call back DEMUX_SEEK_FINISH message.
{
    AwMessage msg;
    DemuxCompContext* demux;
    
    demux = (DemuxCompContext*)d;
    
    //* lock mutex to sync with the message process thread when processing SEEK message.
    //* when processing the SEEK message, we clear the Parser's force stop flag by calling CdxParserClrForceStop().
    //* if we're playing a network stream and processing seek message, when user cancel the 
    //* seek message, we should set the parser's force stop flag by calling CdxParserForceStop() to 
    //* prevend from blocking at the network io.
    pthread_mutex_lock(&demux->mutex);
    demux->bCancelSeek = 1;
    pthread_mutex_unlock(&demux->mutex);
    
    if(demux->pParser != NULL)
        CdxParserForceStop(demux->pParser);
    
    //* send a prepare.
    setMessage(&msg, DEMUX_COMMAND_CANCEL_SEEK, (uintptr_t)&demux->semCancelSeek);                                       //* no reply.
    AwMessageQueuePostMessage(demux->mq, &msg);
    SemTimedWait(&demux->semCancelSeek, -1);
    return 0;
}


int DemuxCompNotifyFirstFrameShowed(DemuxComp* d)   //* notify video first frame showed.
{
    AwMessage msg;
    DemuxCompContext* demux;
    
    demux = (DemuxCompContext*)d;
    
    //* send a start message.
    setMessage(&msg, DEMUX_COMMAND_NOTIFY_FIRST_FRAME_SHOWED, 0, 0);
    AwMessageQueuePostMessage(demux->mq, &msg);
    return 0;
}


int DemuxCompSetCacheStatReportInterval(DemuxComp* d, int ms)
{
    DemuxCompContext* demux;
    demux = (DemuxCompContext*)d;
    demux->nCacheStatReportIntervalUs = ms*1000;
    return 0;
}


int DemuxCompSetCachePolicy(DemuxComp*          d, 
                            enum ECACHEPOLICY   eCachePolicy, 
                            int                 nStartPlaySize, 
                            int                 nStartPlayTimeMs, 
                            int                 nMaxBufferSize)
{
    DemuxCompContext* demux;
    demux = (DemuxCompContext*)d;
    
    if(demux->eStatus == DEMUX_STATUS_IDLE ||
       demux->eStatus == DEMUX_STATUS_INITIALIZED ||
       demux->eStatus == DEMUX_STATUS_PREPARING)
    {
        loge("not prepared yet, can not set cache parameter.");
        return -1;
    }
    
    if(demux->bFileStream)
    {
        //* no need to set cache parameter for file stream.
        return -1;
    }
    
    if(demux->bLiveStream && eCachePolicy == CACHE_POLICY_ADAPTIVE)
        eCachePolicy = CACHE_POLICY_QUICK;
    
    switch(eCachePolicy)
    {
        case CACHE_POLICY_QUICK:
        {
            demux->nCacheTimeMs = 0;
            demux->eCachePolicy = CACHE_POLICY_QUICK;
            if(nStartPlaySize <= 0)
                nStartPlaySize = DEFAULT_CACHE_START_PLAY_SIZE;
            //StreamCacheSetSize(demux->pCache, DEFAULT_CACHE_START_PLAY_SIZE, DEFAULT_CACHE_BUFFER_SIZE);
            StreamCacheSetSize(demux->pCache, nStartPlaySize, DEFAULT_CACHE_BUFFER_SIZE);
            break;
        }
        
        case CACHE_POLICY_SMOOTH:
        {
            if(nMaxBufferSize <= 0)
                nMaxBufferSize = DEFAULT_CACHE_BUFFER_SIZE;
            demux->nCacheTimeMs = 0;
            demux->eCachePolicy = CACHE_POLICY_SMOOTH;
            StreamCacheSetSize(demux->pCache, nMaxBufferSize, nMaxBufferSize);
            break;
        }
        
        case CACHE_POLICY_USER_SPECIFIC_PARAMS:
        {
            if(nStartPlayTimeMs > 0)
                DemuxCompSetCacheTime(demux, nStartPlayTimeMs);
            else
            {
                demux->nCacheTimeMs = 0;
                StreamCacheSetSize(demux->pCache, nStartPlaySize, nMaxBufferSize);
            }
            demux->eCachePolicy = CACHE_POLICY_USER_SPECIFIC_PARAMS;
            break;
        }
        
        case CACHE_POLICY_ADAPTIVE:
        default:
        {
            DemuxCompUseDefaultCacheParams(demux);
            demux->eCachePolicy = CACHE_POLICY_ADAPTIVE;
            break;
        }
    }
    
    return 0;
}

int DemuxCompSetSecureBufferCount(DemuxComp* d, void* param)
{
    DemuxCompContext* demux;
    int nMessageId = CDX_PSR_CMD_SET_SECURE_BUFFER_COUNT;
    
    demux = (DemuxCompContext*)d;
    if(demux->pParser != NULL)
        CdxParserControl(demux->pParser,nMessageId,param);
    
    return 0;
}

int DemuxCompSetSecureBuffers(DemuxComp* d,void* param)
{
    DemuxCompContext* demux;
    int nMessageId = CDX_PSR_CMD_SET_SECURE_BUFFERS;
    
    demux = (DemuxCompContext*)d;
    BufferInfo mBufferInfo;
    uintptr_t* tmpParam      = (uintptr_t*)param; 
    mBufferInfo.size   = tmpParam[0];
    mBufferInfo.buffer = (void*)tmpParam[1];
    
    if(demux->pParser != NULL)
        CdxParserControl(demux->pParser,nMessageId,(void*)&mBufferInfo);
    
    return 0;
}

int DemuxCompGetCacheSize(DemuxComp* d)
{
	DemuxCompContext* demux;
	demux = (DemuxCompContext*)d;
	return StreamCacheGetSize(demux->pCache);
}

static int DemuxCompSetCacheTime(DemuxCompContext* demux, int nCacheTimeMs)
{
    int               nStartPlaySize;
    int               nMaxBufferSize;
    int               nBitrate;
    
    if(demux->mediaInfo.nDurationMs <= 0)
    {
        nBitrate = 0;
        if(demux->mediaInfo.nBitrate > 0)
            nBitrate = demux->mediaInfo.nBitrate;
        if(demux->pPlayer != NULL)
            nBitrate = PlayerGetVideoBitrate(demux->pPlayer) + PlayerGetAudioBitrate(demux->pPlayer);
    
        if(nBitrate <= 0)
            nBitrate = 200*1024;    //* default live stream bitrate is 200 kbits.
    }
    else
    {
        if(nCacheTimeMs > demux->mediaInfo.nDurationMs)
            nCacheTimeMs = demux->mediaInfo.nDurationMs;
            
        if(demux->mediaInfo.nFileSize <= 0)
        {
            if(demux->mediaInfo.nBitrate > 0)
                nBitrate = demux->mediaInfo.nBitrate;
            else
                nBitrate = 200*1024;    //* default vod stream bitrate is 200 kbits.
        }
        else
            nBitrate = (int)(demux->mediaInfo.nFileSize*8*1000/demux->mediaInfo.nDurationMs);
    }
    
    if(nBitrate <= 64*1024)
        nBitrate = 64*1024;
    if(nBitrate > 20*1024*1024)
        nBitrate = 20*1024*1024;
        
    nStartPlaySize = (int)(nBitrate * (int64_t)nCacheTimeMs / (8*1000));
    
    if(nStartPlaySize < DEFAULT_CACHE_START_PLAY_SIZE)  //* 128KB.
        nStartPlaySize = DEFAULT_CACHE_START_PLAY_SIZE;
    if(nStartPlaySize > MAX_CACHE_START_PLAY_SIZE)      //* 150MB.
        nStartPlaySize = MAX_CACHE_START_PLAY_SIZE;
    nMaxBufferSize = nStartPlaySize*4/3;
    if(nMaxBufferSize < DEFAULT_CACHE_BUFFER_SIZE)      //* 20MB.
        nMaxBufferSize = DEFAULT_CACHE_BUFFER_SIZE;
    if(nMaxBufferSize > MAX_CACHE_BUFFER_SIZE)          //* 200MB.
        nMaxBufferSize = MAX_CACHE_BUFFER_SIZE;
    
    demux->nCacheTimeMs = nCacheTimeMs;

    if(demux->mediaInfo.nVideoStreamNum == 0)
    {
        nStartPlaySize = DEFAULT_CACHE_START_PLAY_SIZE_WITHOUT_VIDEO;
    }

    StreamCacheSetSize(demux->pCache, nStartPlaySize, nMaxBufferSize);
    return 0;
}


static int DemuxCompUseDefaultCacheParams(DemuxCompContext* demux)
{
    int               nStartPlaySize;
    int               nMaxBufferSize;
    int               nCacheTimeMs;
    int               nBitrate;
    
    nCacheTimeMs = DEFAULT_CACHE_START_PLAY_TIME;
    if(nCacheTimeMs > demux->mediaInfo.nDurationMs)
        nCacheTimeMs = demux->mediaInfo.nDurationMs;
    
    if(demux->mediaInfo.nFileSize <= 0)
    {
        if(demux->mediaInfo.nBitrate > 0)
            nBitrate = demux->mediaInfo.nBitrate;
        else
            nBitrate = 200*1024;    //* default vod stream bitrate is 200 kbits.
    }
    else
    {
        if(demux->mediaInfo.nDurationMs != 0)
            nBitrate = (int)(demux->mediaInfo.nFileSize*8*1000/demux->mediaInfo.nDurationMs);
        else
            nBitrate = 200*1024;    //* default vod stream bitrate is 200 kbits.
    }
    
    if(nBitrate <= 64*1024)
        nBitrate = 64*1024;
    if(nBitrate > 20*1024*1024)
        nBitrate = 20*1024*1024;
    
    nStartPlaySize = (int)(nBitrate * (int64_t)nCacheTimeMs / (8*1000));
    
    if(nStartPlaySize < DEFAULT_CACHE_START_PLAY_SIZE)
        nStartPlaySize = DEFAULT_CACHE_START_PLAY_SIZE;
    if(nStartPlaySize > MAX_CACHE_START_PLAY_SIZE)
        nStartPlaySize = MAX_CACHE_START_PLAY_SIZE;
    
    nMaxBufferSize = nStartPlaySize*4/3;
    if(nMaxBufferSize < DEFAULT_CACHE_BUFFER_SIZE)
        nMaxBufferSize = DEFAULT_CACHE_BUFFER_SIZE;
    if(nMaxBufferSize > MAX_CACHE_BUFFER_SIZE)
        nMaxBufferSize = MAX_CACHE_BUFFER_SIZE;
    
    demux->nCacheTimeMs = 0;

    if(demux->mediaInfo.nVideoStreamNum == 0)
    {
        nStartPlaySize = DEFAULT_CACHE_START_PLAY_SIZE_WITHOUT_VIDEO;
    }
    
    StreamCacheSetSize(demux->pCache, nStartPlaySize, nMaxBufferSize);
    return 0;
}


static void AdjsutCacheParams(DemuxCompContext* demux)
{
    int nBitrate;
    if(demux->eCachePolicy == CACHE_POLICY_ADAPTIVE || 
       (demux->eCachePolicy == CACHE_POLICY_USER_SPECIFIC_PARAMS && demux->nCacheTimeMs != 0))
    {
        if(demux->mediaInfo.nFileSize <= 0)
        {
            if(demux->mediaInfo.nBitrate > 0)
                nBitrate = demux->mediaInfo.nBitrate;
            else
                nBitrate = PlayerGetVideoBitrate(demux->pPlayer) + PlayerGetAudioBitrate(demux->pPlayer);    //* default vod stream bitrate is 200 kbits.
        }
        else
            nBitrate = (int)(demux->mediaInfo.nFileSize*8*1000/demux->mediaInfo.nDurationMs);
        
        logd("bitrate = %d", nBitrate);
        if(nBitrate > 0)
            DemuxCompAdjustCacheParamsWithBitrate(demux, nBitrate);
    }
    return;
}


static int DemuxCompAdjustCacheParamsWithBitrate(DemuxCompContext* demux, int nBitrate)
{
    int nStartPlaySize;
    int nMaxBufferSize;
    int nCacheTimeMs;
    
    if(demux->eCachePolicy == CACHE_POLICY_ADAPTIVE)
        nCacheTimeMs = DEFAULT_CACHE_START_PLAY_TIME;
    else
        nCacheTimeMs = demux->nCacheTimeMs;
        
    if(demux->mediaInfo.nDurationMs > 0 && nCacheTimeMs > demux->mediaInfo.nDurationMs)
        nCacheTimeMs = demux->mediaInfo.nDurationMs;
        
    if(nBitrate <= 64*1024)
        nBitrate = 64*1024;
    if(nBitrate > 20*1024*1024)
        nBitrate = 20*1024*1024;
    
    nStartPlaySize = (int)(nBitrate * (int64_t)nCacheTimeMs / (8*1000));
    
    if(nStartPlaySize < DEFAULT_CACHE_START_PLAY_SIZE)
        nStartPlaySize = DEFAULT_CACHE_START_PLAY_SIZE;
    if(nStartPlaySize > MAX_CACHE_START_PLAY_SIZE)
        nStartPlaySize = MAX_CACHE_START_PLAY_SIZE;
    
    nMaxBufferSize = nStartPlaySize*4/3;
    if(nMaxBufferSize < DEFAULT_CACHE_BUFFER_SIZE)
        nMaxBufferSize = DEFAULT_CACHE_BUFFER_SIZE;
    if(nMaxBufferSize > MAX_CACHE_BUFFER_SIZE)
        nMaxBufferSize = MAX_CACHE_BUFFER_SIZE;

    if(demux->mediaInfo.nVideoStreamNum == 0)
    {
        nStartPlaySize = DEFAULT_CACHE_START_PLAY_SIZE_WITHOUT_VIDEO;
    }
    
    StreamCacheSetSize(demux->pCache, nStartPlaySize, nMaxBufferSize);
    return 0;
}


static void* DemuxThread(void* arg)
{
    AwMessage         msg;
    AwMessage         newMsg;
    int               ret;
    sem_t*            pReplySem;
    int*              pReplyValue;
    DemuxCompContext* demux;
    int64_t           nLastCacheStateNotifyTimeUs;
    int64_t           nLastBufferingStartTimeUs;
    int64_t           nCacheStartPlaySizeIntervalUs;
    int64_t           nCurTimeUs;
    int               bVideoFirstFrameShowed;
	int	nVideoInfoVersion = -1;
	int nAudioInfoVersion = -1;
    
    demux = (DemuxCompContext*)arg;
    nLastCacheStateNotifyTimeUs         = 0;
    nLastBufferingStartTimeUs           = 0;
    nCacheStartPlaySizeIntervalUs       = 2000000;   //* adjust start play size every 2 seconds.
    bVideoFirstFrameShowed              = 0;
    
    while(1)
    {
        if(AwMessageQueueGetMessage(demux->mq, &msg) < 0)
        {
            loge("get message fail.");
            continue;
        }
        
process_message:
        pReplySem   = (sem_t*)msg.params[0];
        pReplyValue = (int*)msg.params[1];
        
        if(msg.messageId == DEMUX_COMMAND_SET_SOURCE)
        {
            logv("process message DEMUX_COMMAND_SET_SOURCE.");
            
            demux->nSourceType = (int)msg.params[2];
            
            if(demux->nSourceType == SOURCE_TYPE_URL)
            {
                //* data source of url path.
                char*                          uri;
                KeyedVector<String8, String8>* pHeaders;
                
                uri = (char*)msg.params[3];
                pHeaders = (KeyedVector<String8, String8>*)msg.params[4];
                
#if (CONFIG_OS_VERSION >= OPTION_OS_VERSION_ANDROID_5_0)
                void* pHTTPServer;
				pHTTPServer = (void*)msg.params[5];

				if(setDataSourceFields(demux, &demux->source, pHTTPServer, uri, pHeaders) == 0)
#else
                if(setDataSourceFields(demux, &demux->source, uri, pHeaders) == 0)
#endif
                {
                    demux->eStatus = DEMUX_STATUS_INITIALIZED;
                    if(pReplyValue != NULL)
                        *pReplyValue = 0;
                }
                else
                {
                    demux->eStatus = DEMUX_STATUS_IDLE;
                    if(pReplyValue != NULL)
                        *pReplyValue = -1;
                }
                
                if(pReplySem != NULL)
		            sem_post(pReplySem);
		        continue;
            }
            else if(demux->nSourceType == SOURCE_TYPE_FD)
            {
                //* data source is a file descriptor.
                int     fd;
                long long nOffset;
                long long nLength;
                char    str[128];
                
                clearDataSourceFields(&demux->source);
                
                fd = msg.params[3];
                nOffset = msg.params[4];
                nOffset<<=32;
                nOffset |= msg.params[5];
                nLength = msg.params[6];
                nLength<<=32;
                nLength |= msg.params[7];
                
                memset(&demux->source, 0x00, sizeof(demux->source));
                sprintf(str, "fd://%d?offset=%lld&length=%lld", fd, nOffset, nLength);
                demux->source.uri = strdup(str);
                if(demux->source.uri != NULL)
                {
                    demux->eStatus = DEMUX_STATUS_INITIALIZED;
                    if(pReplyValue != NULL)
                        *pReplyValue = 0;
                }
                else
                {
                    loge("can not dump string to represent fd source.");
                    demux->eStatus = DEMUX_STATUS_IDLE;
                    if(pReplyValue != NULL)
                        *pReplyValue = -1;
                }
                
                if(pReplySem != NULL)
		            sem_post(pReplySem);
		        continue;
            }
            else
            {
                //* data source of IStringSource interface.
                clearDataSourceFields(&demux->source);
                memset(&demux->source, 0x00, sizeof(demux->source));
				
                char *uri = (char*)msg.params[3];
                demux->source.uri = strdup(uri);
                if(demux->source.uri != NULL)
                {
                    demux->eStatus = DEMUX_STATUS_INITIALIZED;
                    if(pReplyValue != NULL)
                        *pReplyValue = 0;
                }
                else
                {
                    loge("can not dump string to represent interface.");
                    demux->eStatus = DEMUX_STATUS_IDLE;
                    if(pReplyValue != NULL)
                        *pReplyValue = -1;
                }
                if(pReplySem != NULL)
		            sem_post(pReplySem);
		        continue;
            }
        } //* end DEMUX_COMMAND_SET_SOURCE.
        else if(msg.messageId == DEMUX_COMMAND_PREPARE)
        {
            int flags = 0, ret = 0;
            
            logv("process message DEMUX_COMMAND_PREPARE.");
            
            if(demux->pParser)
            {
                //* should not run here, pParser should be NULL under INITIALIZED or STOPPED status.
                logw("demux->pParser != NULL when DEMUX_COMMAND_PREPARE message received.");
                CdxParserClose(demux->pParser);
                demux->pParser = NULL;
                demux->pStream = NULL;
            }
			else if(demux->pStream)
			{	
				CdxStreamClose(demux->pStream);
                demux->pStream = NULL;
			}
            
			if(demux->nSourceType == SOURCE_TYPE_ISTREAMSOURCE)
			{
				flags |= MIRACST;
			}
#if AWPLAYER_CONFIG_DISABLE_SUBTITLE
            flags |= DISABLE_SUBTITLE;
#endif
#if AWPLAYER_CONFIG_DISABLE_AUDIO
            flags |= DISABLE_AUDIO;
#endif
#if AWPLAYER_CONFIG_DISABLE_VIDEO
            flags |= DISABLE_VIDEO;
#endif
#if !AWPLAYER_CONFIG_DISALBE_MULTI_AUDIO
            flags |= MUTIL_AUDIO;
#endif

			
			struct CallBack cb;
			cb.callback = ParserCallbackProcess;
			cb.pUserData = (void *)demux;

			static struct HdcpOpsS hdcpOps;
			hdcpOps.init = HDCP_Init;
			hdcpOps.deinit = HDCP_Deinit;
			hdcpOps.decrypt = HDCP_Decrypt;
			
			ContorlTask parserContorlTask, *contorlTask;
			ContorlTask contorlTask1;
			ContorlTask contorlTask2;
			parserContorlTask.cmd = CDX_PSR_CMD_SET_CALLBACK;
			parserContorlTask.param = (void *)&cb;
			parserContorlTask.next = NULL;
			contorlTask = &parserContorlTask;

			contorlTask1.cmd = CDX_PSR_CMD_SET_HDCP;
			contorlTask1.param = (void *)&hdcpOps;
			contorlTask1.next = NULL;

			contorlTask2.cmd = CDX_PSR_CMD_SET_YUNOS_UUID;
			contorlTask2.param = (void*)demux->mYunOSUUID;
			contorlTask2.next = NULL;

			if(flags & MIRACST)
			{
				contorlTask->next = &contorlTask1;
				contorlTask = contorlTask->next;
			}

			pthread_mutex_lock(&demux->mutex1);
			if(demux->mYunOSInfoEndale == 1)
			{
				contorlTask->next = &contorlTask2;
				contorlTask = contorlTask->next;
			}
//
			ret = CdxParserPrepare(&demux->source, flags, &demux->mutex, &demux->bCancelPrepare,
				&demux->pParser, &demux->pStream, &parserContorlTask, NULL);
			pthread_mutex_unlock(&demux->mutex1);

			if(ret < 0)
			{
				goto _endTask;
			}
			if(CheckParserWhetherBeForbided(demux->pParser->type) == -1)
			{
				loge("*** the parser type is forbided, tpye = %d",demux->pParser->type);
				demux->eStatus = DEMUX_STATUS_INITIALIZED;
				demux->callback(demux->pUserData, DEMUX_NOTIFY_PREPARED, (void*)DEMUX_ERROR_IO);
				continue;
			}



/*
			pthread_mutex_lock(&demux->mutex);
			if(demux->bCancelPrepare)
			{
				pthread_mutex_unlock(&demux->mutex);
				goto _endTask;
			}

			// lock
			demux->pStream = CdxStreamCreate(&demux->source);
			// unlock
			pthread_mutex_unlock(&demux->mutex);
			if(!demux->pStream)
			{
				loge("stream creat fail.");
				goto _endTask;
			}
			ret = CdxStreamConnect(demux->pStream);
			if(ret < 0)
			{
				goto _endTask;
			}
			pthread_mutex_lock(&demux->mutex);
			if(demux->bCancelPrepare)
			{
				pthread_mutex_unlock(&demux->mutex);
				goto _endTask;
			}
			demux->pParser = CdxParserCreate(demux->pStream);
			pthread_mutex_unlock(&demux->mutex);
			if(!demux->pParser)
			{
				loge("parser creat fail.");
				goto _endTask;
			}
			struct CallBack cb;
			cb.callback = ParserCallbackProcess;
			cb.pUserData = (void *)demux;
			ret = CdxParserControl(demux->pParser, CDX_PSR_CMD_SET_CALLBACK, &cb);
			if(ret < 0)
			{
				logw("CDX_PSR_CMD_SET_CB fail");
			}
			if(flags & MIRACST)
			{
				static struct HdcpOpsS hdcpOps;
				hdcpOps.init = HDCP_Init;
				hdcpOps.deinit = HDCP_Deinit;
				hdcpOps.decrypt = HDCP_Decrypt;
				CdxParserControl(demux->pParser, CDX_PSR_CMD_SET_HDCP, &hdcpOps);
			}
			ret = CdxParserInitialize(demux->pParser);
			if(ret < 0)
			{
				goto _endTask;
			}
			*/
            CdxMediaInfoT parserMediaInfo;
            memset(&parserMediaInfo,0,sizeof(CdxMediaInfoT));
            CdxParserGetMediaInfo(demux->pParser, &parserMediaInfo);
            setMediaInfo(&demux->mediaInfo, &parserMediaInfo);
            demux->mediaInfo.eContainerType = (enum ECONTAINER)demux->pParser->type;

            if(demux->pParser->type == CDX_PARSER_WVM) 
            {
                 demux->mediaInfo.pVideoStreamInfo->bSecureStreamFlag = 1; 
            }
                
            demux->bEOS     = 0;
            demux->bIOError = 0;
                
            if(demux->nSourceType == SOURCE_TYPE_URL)
            {
				if (!strncasecmp(demux->source.uri, "file://", 7) || !strncasecmp(demux->source.uri, "bdmv://", 7) || demux->source.uri[0] == '/')
                    demux->bFileStream = 1;
                else if(demux->mediaInfo.nDurationMs <= 0)
                    demux->bLiveStream = 1;
                else
                    demux->bVodStream = 1;
            }
            else if(demux->nSourceType == SOURCE_TYPE_FD)
                demux->bFileStream = 1;
            else
                demux->bFileStream = 1;//demux->bLiveStream = 1;     //* treat IStreamSource(miracast) as a live stream.//demux->bFileStream = 1;

            //* if the parser is wvm , we should not start cache thread, because wvm need secure buffer.
            //* if the parser is playready sstr, we need secure buffer.
            logd("demux->pParser->type=%d", demux->pParser->type);
            if(demux->bFileStream == 0 && //* start the cache trhead for netstream.
                demux->pParser->type != CDX_PARSER_WVM && 
                demux->pParser->type != CDX_PARSER_SSTR_PLAYREADY) 
            {
                if(pthread_create(&demux->cacheThreadId, NULL, CacheThread, (void*)demux) == 0)
                {
                    //* send a fetch message to start the cache loop.
                    setMessage(&newMsg, DEMUX_COMMAND_START);
                    AwMessageQueuePostMessage(demux->mqCache, &newMsg);
                }
                else
                    demux->cacheThreadId = 0;
            }
                
            //* set player and media format info to cache for seek processing.
            StreamCacheSetPlayer(demux->pCache, demux->pPlayer);
            if(demux->mediaInfo.nVideoStreamNum > 0)
                StreamCacheSetMediaFormat(demux->pCache, 
                                          demux->pParser->type, 
                                          (enum EVIDEOCODECFORMAT)demux->mediaInfo.pVideoStreamInfo->eCodecFormat,
                                          demux->mediaInfo.nBitrate);
            else
                StreamCacheSetMediaFormat(demux->pCache, 
                                          demux->pParser->type, 
                                          VIDEO_CODEC_FORMAT_UNKNOWN,
                                          demux->mediaInfo.nBitrate);
			demux->eStatus = DEMUX_STATUS_PREPARED;
			if(demux->bFileStream == 0)
			{
				//DemuxCompSetCachePolicy((DemuxComp*)demux, CACHE_POLICY_ADAPTIVE, 0, 0, 0);
                //for start playing 
                //DEFAULT_START_PLAY_SIZE 表示最开始播放要缓冲的数据，
                //而DEFAULT_CACHE_START_PLAY_SIZE表示播放过程缓冲时开始播放的数据大小
                if(demux->mediaInfo.nVideoStreamNum == 0)
                {
                    DemuxCompSetCachePolicy((DemuxComp*)demux, CACHE_POLICY_QUICK, DEFAULT_CACHE_START_PLAY_SIZE_WITHOUT_VIDEO, 0, DEFAULT_CACHE_BUFFER_SIZE);
                }
                else
                {
                    DemuxCompSetCachePolicy((DemuxComp*)demux, CACHE_POLICY_QUICK, DEFAULT_START_PLAY_SIZE, 0, DEFAULT_CACHE_BUFFER_SIZE);
                }
            }

			if(!strncasecmp(demux->source.uri, "dtmb://", 7))
			{
				//CDX_LOGD("enter dtmb cache policy:%d\n",demux->cacheThreadId);
                DemuxCompSetCachePolicy((DemuxComp*)demux, CACHE_POLICY_QUICK, 16*1024, 0, 0);
			}
			
			if(demux->cacheThreadId != 0) //* cache some data before start player.
			{
			    while(1)
			    {
                    if(StreamCacheDataEnough(demux->pCache) || demux->bEOS || demux->bIOError)
                    {
                        logd("totSize(%d), startSize(%d)", demux->pCache->nTotalDataSize, demux->pCache->nStartPlaySize);
                        if(demux->bEOS)
                        {
                            //* android.media.cts wait for Buffering Update Notify, before MediaPlayer.start
                            NotifyCacheState(demux);
                        }
                        break;
                    }
                    else
                    {
                        ret = AwMessageQueueTryGetMessage(demux->mq, &msg, 30); //* wait for 30ms if no message come.
                        if(ret == 0)    //* new message come, quit loop to process.
                        {
                            goto process_message;
                        }
                    }
                }
			}
			
			if(demux->bFileStream == 0)
			{
				DemuxCompSetCachePolicy((DemuxComp*)demux, CACHE_POLICY_ADAPTIVE, 0, 0, 0);
			}

			if(!strncasecmp(demux->source.uri, "dtmb://", 7))
			{
				DemuxCompSetCachePolicy((DemuxComp*)demux, CACHE_POLICY_QUICK, 16*1024, 0, 0);
			}

			demux->callback(demux->pUserData, DEMUX_NOTIFY_PREPARED, 0);
            continue;

_endTask:			
			demux->eStatus = DEMUX_STATUS_INITIALIZED;
			if(demux->bCancelPrepare)
			{
				demux->callback(demux->pUserData, DEMUX_NOTIFY_PREPARED, (void*)DEMUX_ERROR_USER_CANCEL);
			}
			else
			{
				loge("DEMUX_ERROR_IO");
				demux->callback(demux->pUserData, DEMUX_NOTIFY_PREPARED, (void*)DEMUX_ERROR_IO);
			}
			continue;
        } //* end DEMUX_COMMAND_PREPARE.
        else if(msg.messageId == DEMUX_COMMAND_START)
        {
            logv("process message DEMUX_COMMAND_START.");
            
            if(demux->bBufferring)
            {
                //* user press the start button when buffering.
                //* start to play even the data is not enough.
                if(demux->bBufferStartNotified == 1)
                {
                    demux->callback(demux->pUserData, DEMUX_NOTIFY_BUFFER_END, NULL);
                    demux->bBufferStartNotified = 0;
                }
                demux->bNeedPausePlayerNotified = 0;
                demux->bBufferring = 0;
            }
            
            if(demux->eStatus == DEMUX_STATUS_STARTED)
            {
                logi("demux already in started status.");
                //* send a read message to start the read loop.
                PostReadMessage(demux->mq);
                if(pReplyValue != NULL)
                    *pReplyValue = 0;
                if(pReplySem != NULL)
		            sem_post(pReplySem);
		        continue;
            }
            
            if(demux->eStatus != DEMUX_STATUS_PREPARED)
            {
                loge("demux not in prepared status when DEMUX_COMMAND_START message received.");
                if(pReplyValue != NULL)
                    *pReplyValue = -1;
                if(pReplySem != NULL)
		            sem_post(pReplySem);
		        continue;
            }
            
            //* in case of bVideoFirstFrameShowed == 0, demux will continue to send data to player
            //* until video first frame showed.
            //* here we set this flag to let the demux don't send data at buffering status.
            if(demux->pPlayer == NULL || PlayerHasVideo(demux->pPlayer) == 0)
                bVideoFirstFrameShowed = 1;
            
            demux->eStatus = DEMUX_STATUS_STARTED;
            //* send a read message to start the read loop.
            PostReadMessage(demux->mq);
            if(pReplyValue != NULL)
                *pReplyValue = 0;
            if(pReplySem != NULL)
		        sem_post(pReplySem);
            continue;
        } //* end DEMUX_COMMAND_START
        else if(msg.messageId == DEMUX_COMMAND_STOP)
        {
            logv("process message DEMUX_COMMAND_STOP.");
            
            //* stop the cache thread.
            if(demux->cacheThreadId != 0)
            {
                void* status;
                setMessage(&newMsg, DEMUX_COMMAND_QUIT, (uintptr_t)&demux->semCache);
                AwMessageQueuePostMessage(demux->mqCache, &newMsg);
                SemTimedWait(&demux->semCache, -1);
                pthread_join(demux->cacheThreadId, &status);
                demux->cacheThreadId = 0;
                
                if(demux->bBufferStartNotified == 1)
                {
                    demux->callback(demux->pUserData, DEMUX_NOTIFY_BUFFER_END, NULL);
                    demux->bBufferStartNotified = 0;
                    demux->bNeedPausePlayerNotified = 0;
                }
            }
            
            if(demux->pParser != NULL)
            {
                CdxParserClose(demux->pParser);
                demux->pParser = NULL;
				demux->pStream = NULL;
            }
			else if(demux->pStream)
			{
				CdxStreamClose(demux->pStream);
				demux->pStream = NULL;
			}
            
            bVideoFirstFrameShowed = 0;
            demux->eStatus = DEMUX_STATUS_STOPPED;
            demux->bStopping = 0;
            if(pReplyValue != NULL)
                *pReplyValue = 0;
            if(pReplySem != NULL)
		        sem_post(pReplySem);
            continue;
        } //* end DEMUX_COMMAND_STOP.
        else if(msg.messageId == DEMUX_COMMAND_QUIT || msg.messageId == DEMUX_COMMAND_CLEAR)
        {
            logv("process message DEMUX_COMMAND_QUIT or DEMUX_COMMAND_CLEAR.");
            
            //* stop the cache thread if it is not stopped yet.
            if(demux->cacheThreadId != 0)
            {
                void* status;
                setMessage(&newMsg, DEMUX_COMMAND_QUIT, (uintptr_t)&demux->semCache);
                AwMessageQueuePostMessage(demux->mqCache, &newMsg);
                SemTimedWait(&demux->semCache, -1);
                pthread_join(demux->cacheThreadId, &status);
                demux->cacheThreadId = 0;
                
                if(demux->bBufferStartNotified == 1)
                {
                    demux->callback(demux->pUserData, DEMUX_NOTIFY_BUFFER_END, NULL);
                    demux->bBufferStartNotified = 0;
                    demux->bNeedPausePlayerNotified = 0;
                }
            }

			//mediaInfo need to free here
			clearMediaInfo(&demux->mediaInfo);

            if(demux->pParser != NULL)
            {
                CdxParserClose(demux->pParser);
                demux->pParser = NULL;
				demux->pStream = NULL;
            }
			else if(demux->pStream)
			{
				CdxStreamClose(demux->pStream);
				demux->pStream = NULL;
			}
            
            clearDataSourceFields(&demux->source);
            demux->eStatus = DEMUX_STATUS_IDLE;
            demux->bStopping = 0;
            bVideoFirstFrameShowed = 0;
            
            if(pReplyValue != NULL)
                *pReplyValue = 0;
            if(pReplySem != NULL)
		        sem_post(pReplySem);
            if(msg.messageId == DEMUX_COMMAND_QUIT)
                break;  //* quit the thread.
            
            continue;
        } //* end DEMUX_COMMAND_QUIT or DEMUX_COMMAND_CLEAR.
        else if(msg.messageId == DEMUX_COMMAND_SEEK)
        {
            int nSeekTimeMs;
            int nFinalSeekTimeMs;
            int params[3];
            
            logv("process message DEMUX_COMMAND_SEEK.");
            bVideoFirstFrameShowed = 0;
            
            nSeekTimeMs = msg.params[2];
            if(demux->pParser != NULL)
            {
                //* flush the cache.
                if(demux->cacheThreadId != 0)
                {
                    setMessage(&newMsg, DEMUX_COMMAND_PAUSE, (uintptr_t)&demux->semCache);
                    AwMessageQueuePostMessage(demux->mqCache, &newMsg);
                    SemTimedWait(&demux->semCache, -1);
                    setMessage(&newMsg, DEMUX_COMMAND_SEEK, 
                               (uintptr_t)&demux->semCache, 
                               (uintptr_t)&demux->nCacheReply,
                               (uintptr_t)nSeekTimeMs);
                    AwMessageQueuePostMessage(demux->mqCache, &newMsg);
                    SemTimedWait(&demux->semCache, -1);
                    
                    if(demux->bBufferStartNotified == 1)
                    {
                        demux->callback(demux->pUserData, DEMUX_NOTIFY_BUFFER_END, NULL);
                        demux->bBufferStartNotified = 0;
                        demux->bNeedPausePlayerNotified = 0;
                    }
                }
				
				//* lock the mutex to sync with the DemuxCompCancelSeek() operation or DEmuxCompStop() operation.
				//* when user are requesting to quit the seek operation, we should not clear the parser's force 
				//* stop flag, other wise the parser may blocked at a network io operation.
				pthread_mutex_lock(&demux->mutex);
				if(demux->bCancelSeek == 0 && demux->bStopping == 0)
				{
					ret = CdxParserClrForceStop(demux->pParser);
					if(ret < 0)
					{
						logw("CdxParserClrForceStop fail, ret(%d)", ret);
					}
				}
				pthread_mutex_unlock(&demux->mutex);
				if(demux->cacheThreadId != 0 && demux->nCacheReply >= 0)
				{
				    ret = 0;
				    nFinalSeekTimeMs = demux->nCacheReply;
				}
				else
				{
                    if(CdxParserSeekTo(demux->pParser, ((int64_t)nSeekTimeMs)*1000) >= 0)
                    {
                        nFinalSeekTimeMs = nSeekTimeMs;
                        ret = 0;
                    }
                    else
                        ret = -1;
				}
				
                if(ret == 0)
                {
                    params[0] = 0;
                    params[1] = nSeekTimeMs;
                    params[2] = nFinalSeekTimeMs;
                    demux->callback(demux->pUserData, DEMUX_NOTIFY_SEEK_FINISH, (void*)params);
                    
                    demux->bSeeking = 0;
                    demux->bEOS     = 0;
                    demux->bIOError = 0;
                    
                    //* send a flush message to the cache thread.
                    if(demux->cacheThreadId != 0)
                    {
                        setMessage(&newMsg, DEMUX_COMMAND_START, (uintptr_t)&demux->semCache);
                        AwMessageQueuePostMessage(demux->mqCache, &newMsg);
                        SemTimedWait(&demux->semCache, -1);
                    }
                    
					if(demux->eStatus == DEMUX_STATUS_COMPLETE)
						demux->eStatus = DEMUX_STATUS_STARTED;
                    
                    if(demux->eStatus == DEMUX_STATUS_STARTED)
                        PostReadMessage(demux->mq); //* send read message to start reading loop.
                    
                    if(pReplyValue != NULL)
                        *pReplyValue = 0;
                    if(pReplySem != NULL)
		                sem_post(pReplySem);
		            continue;
                }
                else
                {
                    loge("CdxParserSeekTo() return fail");
                    demux->eStatus = DEMUX_STATUS_COMPLETE; //* set to complete status to stop reading.
                    demux->bSeeking = 0;
                    if(demux->bCancelSeek == 1 || demux->bStopping == 1)
                        params[0] = DEMUX_ERROR_USER_CANCEL;
                    else
                        params[0] = DEMUX_ERROR_IO;
                    params[1] = nSeekTimeMs;
                    demux->callback(demux->pUserData, DEMUX_NOTIFY_SEEK_FINISH, (void*)params);
                    
                    if(pReplyValue != NULL)
                        *pReplyValue = -1;
                    if(pReplySem != NULL)
		                sem_post(pReplySem);
		            continue;
                }
            }
            else
            {
                params[0] = DEMUX_ERROR_UNKNOWN;
                params[1] = nSeekTimeMs;
                demux->bSeeking = 0;
                demux->callback(demux->pUserData, DEMUX_NOTIFY_SEEK_FINISH, (void*)params);
                
                if(pReplyValue != NULL)
                    *pReplyValue = -1;
                if(pReplySem != NULL)
		            sem_post(pReplySem);
		        continue;
            }
        }
        else if(msg.messageId == DEMUX_COMMAND_CANCEL_PREPARE)
        {
            logv("process message DEMUX_COMMAND_CANCEL_PREPARE.");
            
            demux->bCancelPrepare = 0;
            if(pReplyValue != NULL)
                *pReplyValue = 0;
            if(pReplySem != NULL)
		        sem_post(pReplySem);
		    continue;
        }
        else if(msg.messageId == DEMUX_COMMAND_CANCEL_SEEK)
        {
            logv("process message DEMUX_COMMAND_CANCEL_SEEK.");
            demux->bCancelSeek = 0;
            if(pReplyValue != NULL)
                *pReplyValue = 0;
            if(pReplySem != NULL)
		        sem_post(pReplySem);
		    continue;
        }
        else if(msg.messageId == DEMUX_COMMAND_NOTIFY_FIRST_FRAME_SHOWED)
        {
            bVideoFirstFrameShowed = 1;
            if(demux->eStatus == DEMUX_STATUS_STARTED)
                PostReadMessage(demux->mq);
            
            if(pReplyValue != NULL)
                *pReplyValue = 0;
            if(pReplySem != NULL)
		        sem_post(pReplySem);
		    continue;
        }
        else if(msg.messageId == DEMUX_COMMAND_VIDEO_STREAM_CHANGE)
        {
            logv("process message DEMUX_COMMAND_VIDEO_STREAM_CHANGE!");

            int nMsg =  DEMUX_VIDEO_STREAM_CHANGE;
            demux->callback(demux->pUserData, nMsg, NULL);

            PostReadMessage(demux->mq);
            if(pReplyValue != NULL)
                *pReplyValue = 0;
            if(pReplySem != NULL)
	            sem_post(pReplySem);
            
            continue;
        } //* end DEMUX_COMMAND_VIDEO_STREAM_CHANGE.
        else if(msg.messageId == DEMUX_COMMAND_AUDIO_STREAM_CHANGE)
        {
            logv("process message DEMUX_COMMAND_AUDIO_STREAM_CHANGE!");

            int nMsg =  DEMUX_AUDIO_STREAM_CHANGE;
            demux->callback(demux->pUserData, nMsg, NULL);

            PostReadMessage(demux->mq);
            if(pReplyValue != NULL)
                *pReplyValue = 0;
            if(pReplySem != NULL)
	            sem_post(pReplySem);
            
            continue;
        } //* end DEMUX_COMMAND_AUDIO_STREAM_CHANGE.
        else if(msg.messageId == DEMUX_COMMAND_READ)
        {
            logv("process message DEMUX_COMMAND_READ.");
            
            if(demux->eStatus != DEMUX_STATUS_STARTED)
            {
                logw("demux component not in started status, ignore read message.");
                continue;
            }
            
            if(demux->cacheThreadId != 0)
            {
                if(demux->bFileStream == 0)
                {
                    nCurTimeUs = GetSysTime();
                    
                    //**************************************************************
                    //* notify the cache status.
                    //**************************************************************
                    if(nCurTimeUs >= (nLastCacheStateNotifyTimeUs + demux->nCacheStatReportIntervalUs) ||
                        nCurTimeUs < nLastCacheStateNotifyTimeUs)
                    {
                        NotifyCacheState(demux);
                        nLastCacheStateNotifyTimeUs = nCurTimeUs;
                    }
                }
                
                //**************************************************************
                //* read data from cache.
                //**************************************************************
                if(demux->bBufferring)
                {
                    //* the player is paused and caching stream data.
                    //* check whether data in cache is enough for play.
                    if(StreamCacheDataEnough(demux->pCache) || demux->bEOS || demux->bIOError)
                    {
                        logv("detect data enough, notify BUFFER_END.");
                        demux->bBufferring = 0;
                        if(demux->bBufferStartNotified == 1)
                        {
                            demux->callback(demux->pUserData, DEMUX_NOTIFY_BUFFER_END, NULL);
                            demux->bBufferStartNotified = 0;
                        }
                        demux->callback(demux->pUserData, DEMUX_NOTIFY_RESUME_PLAYER, NULL);
                        demux->bNeedPausePlayerNotified = 0;
                    }
                    else
                    {
                        if(demux->bBufferStartNotified == 0)
                        {
                            nCurTimeUs = GetSysTime();
                            if(nCurTimeUs > (nLastBufferingStartTimeUs + 2000000) ||
                               nCurTimeUs < nLastBufferingStartTimeUs)
                            {
                                //* had been paused for buffering for more than 2 seconds, notify buffer start message
                                //* to let the application know.
                                demux->callback(demux->pUserData, DEMUX_NOTIFY_BUFFER_START, NULL);
                                demux->bBufferStartNotified = 1;
                            }
                        }
                        
                        if(bVideoFirstFrameShowed == 1 ||
                           PlayerHasVideo(demux->pPlayer) == 0 )//* also not send data to Decoder
                                                                //* if just have audio stream
                        {
                            //* wait some time for caching.
                            ret = AwMessageQueueTryGetMessage(demux->mq, &msg, 100); //* wait for 100ms if no message come.
                            if(ret == 0)    //* new message come, quit loop to process.
                                goto process_message;
                            
                            //* post a read message to continue the reading job.
                            PostReadMessage(demux->mq);
                            continue;
                        }
                    }
                }
                
                //* check whether cache underflow.
                if(StreamCacheUnderflow(demux->pCache))
                {
                    logv("detect cache data underflow.");
                    //* cache underflow, if not eos, we need to notify pausing, 
                    //* otherwise we need to notify complete.
                    if(demux->bEOS)
                    {
                        //* end of stream, notify complete.
                        logi("detect eos, notify EOS.");
                        demux->callback(demux->pUserData, DEMUX_NOTIFY_EOS, 0);
                        demux->eStatus = DEMUX_STATUS_COMPLETE;
                        continue;
                    }
                    else if(demux->bIOError)
                    {
                        logi("detect io error, notify IOERROR.");
                        //* end of stream, notify complete.
                        demux->callback(demux->pUserData, DEMUX_NOTIFY_IOERROR, 0);
                        continue;
                    }
                    else
                    {
                        //* no data in cache, check whether player hold enough data, 
                        //* if not, we need to notify pausing to wait for caching 
                        //* more data for player.
                        if(PlayerBufferUnderflow(demux->pPlayer) && demux->bNeedPausePlayerNotified == 0)
                        {
                            logv("detect player data underflow, notify BUFFER_START.");
                            demux->bBufferring = 1;
                            nLastBufferingStartTimeUs = GetSysTime();
                            demux->callback(demux->pUserData, DEMUX_NOTIFY_PAUSE_PLAYER, NULL);
                            demux->bNeedPausePlayerNotified = 1;
                            
                            //**************************************************************
                            //* adjust cache params.
                            //**************************************************************
                            AdjsutCacheParams(demux);
                        }
                        
                        //* wait some time for caching.
                        ret = AwMessageQueueTryGetMessage(demux->mq, &msg, 100); //* wait for 100ms if no message come.
                        if(ret == 0)    //* new message come, quit loop to process.
                            goto process_message;
                        
                        //* post a read message to continue the reading job.
                        PostReadMessage(demux->mq);
                        continue;
                    }
                }
                else
                {
                    //* there is some data in cache for player.
                    //* if data in player is not too much, send it to player, 
                    //* otherwise, just keep it in the cache.
                    if(PlayerBufferOverflow(demux->pPlayer))
                    {
                        logv("detect player data overflow.");
                        //* too much data in player, wait some time.
                        ret = AwMessageQueueTryGetMessage(demux->mq, &msg, 200); //* wait for 200ms if no message come.
                        if(ret == 0)    //* new message come, quit loop to process.
                            goto process_message;
                            
                        //* post a read message to continue the reading job.
                        PostReadMessage(demux->mq);
                        continue;
                    }
                    else
                    {
                        //*************************************
                        //* send data from cache to player.
                        //*************************************
                        CacheNode*          node;
                        enum EMEDIATYPE     ePlayerMediaType;
                        MediaStreamDataInfo streamDataInfo;
                        int                 nStreamIndex;
                        void*               pBuf0;
                        void*               pBuf1;
                        int                 nBufSize0;
                        int                 nBufSize1;
                        
                        //********************************
                        //* 1. get one frame from cache.
                        //********************************
                        node = StreamCacheNextFrame(demux->pCache);
                        if(node == NULL)
                        {
                            loge("Cache not underflow but cannot get stream frame, shouldn't run here.");
                            abort();
                        }
                        
                        //********************************
                        //* 2. request buffer from player.
                        //********************************
                        if(node->eMediaType == CDX_MEDIA_VIDEO)
                        {
                            ePlayerMediaType = MEDIA_TYPE_VIDEO;
                            nStreamIndex     = (node->nFlags&MINOR_STREAM)==0 ? 0 : 1;
                        }
                        else if(node->eMediaType == CDX_MEDIA_AUDIO)
                        {
                            ePlayerMediaType = MEDIA_TYPE_AUDIO;
                            nStreamIndex     = node->nStreamIndex;
                        }
                        else if(node->eMediaType == CDX_MEDIA_SUBTITLE)
                        {
                            ePlayerMediaType = MEDIA_TYPE_SUBTITLE;
                            nStreamIndex     = node->nStreamIndex;
                        }
                        else
                        {
                            loge("media type from parser not valid, should not run here, abort().");
                            abort();
                        }
                
                        while(1)
                        {
                             ret = PlayerRequestStreamBuffer(demux->pPlayer,
                                                             node->nLength, 
                                                             &pBuf0,
                                                             &nBufSize0,
                                                             &pBuf1,
                                                             &nBufSize1,
                                                             ePlayerMediaType,
                                                             nStreamIndex);
                            if(ret<0 || (nBufSize0+nBufSize1)<node->nLength)
                            {
                                logi("waiting for stream buffer.");
                                //* no buffer, try to wait sometime.
                                ret = AwMessageQueueTryGetMessage(demux->mq, &msg, 200); //* wait for 200ms if no message come.
                                if(ret == 0)    //* new message come, quit loop to process.
                                    goto process_message;
                            }
                            else
                                break;  //* get buffer ok.
                        }
                
                        //**********************************************
                        //* 3. copy data to player's buffer and submit.
                        //**********************************************
                        if(node->nLength > nBufSize0)
                        {
                            memcpy(pBuf0, node->pData, nBufSize0);
                            memcpy(pBuf1, node->pData + nBufSize0, node->nLength-nBufSize0);
                        }
                        else
                            memcpy(pBuf0, node->pData, node->nLength);
                            
                        streamDataInfo.pData        = (char*)pBuf0;
                        streamDataInfo.nLength      = node->nLength;
                        streamDataInfo.nPts         = node->nPts;
                        streamDataInfo.nPcr         = node->nPcr;
                        //streamDataInfo.bIsFirstPart = 1;
                        //streamDataInfo.bIsLastPart  = 1;
                        streamDataInfo.bIsFirstPart = (!!(node->nFlags& FIRST_PART));
                        streamDataInfo.bIsLastPart  = (!!(node->nFlags & LAST_PART));
                        streamDataInfo.nDuration    = node->nDuration;
						streamDataInfo.pStreamInfo  = NULL;
						streamDataInfo.nStreamChangeFlag = 0;
                        streamDataInfo.nStreamChangeNum  = 0;
                        
						if(ePlayerMediaType == MEDIA_TYPE_VIDEO && node->infoVersion == 1/*!= nVideoInfoVersion*/)
						{
                            logd(" demux -- video stream info change: node->info = %p",node->info);
							if(node->info)
							{
                                struct VideoInfo* pVideoinfo = (struct VideoInfo *)node->info;
                                logd("****pVideoinfo = %p",pVideoinfo);
                                logd("****pVideoinfo->videoNum = %d",pVideoinfo->videoNum);
                                VideoStreamInfo*  pVideoStreamInfo = (VideoStreamInfo*)malloc(sizeof(VideoStreamInfo)*pVideoinfo->videoNum);
                                if(pVideoStreamInfo == NULL)
                                {
                                    loge("malloc video stream info fail!");
                                    return NULL;
                                }
                                memset(pVideoStreamInfo, 0, sizeof(VideoStreamInfo)*pVideoinfo->videoNum);
                                for(int i = 0; i < pVideoinfo->videoNum; i++)
                                {
                                    memcpy(&pVideoStreamInfo[i],&pVideoinfo->video[i],sizeof(VideoStreamInfo));
                                    if(pVideoinfo->video[i].pCodecSpecificData)
                                    {
                                        pVideoStreamInfo[i].pCodecSpecificData = (char*)malloc(pVideoinfo->video[i].nCodecSpecificDataLen);
                                        if(pVideoStreamInfo[i].pCodecSpecificData == NULL)
                                        {
                                            loge("malloc video specific data fail!");
                                            return NULL;
                                        }
                                        memcpy(pVideoStreamInfo[i].pCodecSpecificData,
                                               pVideoinfo->video[i].pCodecSpecificData,
                                               pVideoinfo->video[i].nCodecSpecificDataLen);
                                        pVideoStreamInfo[i].nCodecSpecificDataLen = pVideoinfo->video[i].nCodecSpecificDataLen;
                                    }
                                }

                                streamDataInfo.pStreamInfo = pVideoStreamInfo;
                                streamDataInfo.nStreamChangeFlag = 1;
                                streamDataInfo.nStreamChangeNum  = pVideoinfo->videoNum;
							}
							nVideoInfoVersion = node->infoVersion;
							
						}
						else if(ePlayerMediaType == MEDIA_TYPE_AUDIO && node->infoVersion == 1/*!= nAudioInfoVersion*/)
						{
                            logd(" demux -- audio stream info change: node->info = %p",node->info);
							if(node->info)
							{
                                struct AudioInfo* pAudioinfo = (struct AudioInfo *)node->info;
                                AudioStreamInfo*  pAudioStreamInfo = (AudioStreamInfo*)malloc(sizeof(AudioStreamInfo)*pAudioinfo->audioNum);
                                if(pAudioStreamInfo == NULL)
                                {
                                    loge("malloc video stream info fail!");
                                    return NULL;
                                }
                                memset(pAudioStreamInfo, 0, sizeof(AudioStreamInfo)*pAudioinfo->audioNum);
                                for(int i = 0; i < pAudioinfo->audioNum; i++)
                                {
                                    memcpy(&pAudioStreamInfo[i],&pAudioinfo->audio[i],sizeof(AudioStreamInfo));
                                    if(pAudioinfo->audio[i].pCodecSpecificData)
                                    {
                                        pAudioStreamInfo[i].pCodecSpecificData = (char*)malloc(pAudioinfo->audio[i].nCodecSpecificDataLen);
                                        if(pAudioStreamInfo[i].pCodecSpecificData == NULL)
                                        {
                                            loge("malloc video specific data fail!");
                                            return NULL;
                                        }
                                        memcpy(pAudioStreamInfo[i].pCodecSpecificData,
                                               pAudioinfo->audio[i].pCodecSpecificData,
                                               pAudioinfo->audio[i].nCodecSpecificDataLen);
                                        pAudioStreamInfo[i].nCodecSpecificDataLen = pAudioinfo->audio[i].nCodecSpecificDataLen;
                                    }
                                }

                                streamDataInfo.pStreamInfo = pAudioStreamInfo;
                                streamDataInfo.nStreamChangeFlag = 1;
                                streamDataInfo.nStreamChangeNum  = pAudioinfo->audioNum;
							}
							nAudioInfoVersion = node->infoVersion;
							
						}
                        PlayerSubmitStreamData(demux->pPlayer, &streamDataInfo, ePlayerMediaType, nStreamIndex);
                        
                        StreamCacheFlushOneFrame(demux->pCache);
                        
                        //* post a read message to continue the reading job.
                        PostReadMessage(demux->mq);
                        continue;
                    }   //* end if(PlayerBufferOverflow(...)){}else {}
                }   //* end if(StreamCacheUnderflow(...)){}else {}
            }
            else
            {
                //**************************************************************
                //* read data directly from parser.
                //**************************************************************
                
                CdxPacketT          packet;
				memset(&packet, 0x00, sizeof(CdxPacketT));
                enum EMEDIATYPE     ePlayerMediaType;
                MediaStreamDataInfo streamDataInfo;
                int                 nStreamIndex;
                
                //* if data in player is not too much, send it to player, 
                //* otherwise don't read.
                if(PlayerBufferOverflow(demux->pPlayer))
                {
                    //* too much data in player, wait some time.
                    ret = AwMessageQueueTryGetMessage(demux->mq, &msg, 200); //* wait for 200ms if no message come.
                    if(ret == 0)    //* new message come, quit loop to process.
                        goto process_message;
                        
                    //* post a read message to continue the reading job.
                    PostReadMessage(demux->mq);
                    continue;
                }
                        
                //* 1. get data type.
                if(CdxParserPrefetch(demux->pParser, &packet) != 0)
                {
                    if(demux->bStopping == 0 && demux->bSeeking == 0)
                    {
                        int err = CdxParserGetStatus(demux->pParser);
                        
                        if(err == PSR_IO_ERR)
                        {
                            demux->bIOError = 1;
                            demux->callback(demux->pUserData, DEMUX_NOTIFY_IOERROR, 0);
                        }
                        else if (err == PSR_USER_CANCEL) // TODO: 
                        {
                            /* do noting */
                        }
                        else
                        {
                            demux->bEOS = 1;
                            demux->callback(demux->pUserData, DEMUX_NOTIFY_EOS, 0);
                            demux->eStatus = DEMUX_STATUS_COMPLETE;
                        }
                    }
                    
                    continue;
                }
                
                //* 2. request buffer from player.
                if(packet.type == CDX_MEDIA_VIDEO)
                {
                    ePlayerMediaType = MEDIA_TYPE_VIDEO;
                    nStreamIndex     = (packet.flags&MINOR_STREAM)==0 ? 0 : 1;
                }
                else if(packet.type == CDX_MEDIA_AUDIO)
                {
                    ePlayerMediaType = MEDIA_TYPE_AUDIO;
                    nStreamIndex     = packet.streamIndex;
                }
                else if(packet.type == CDX_MEDIA_SUBTITLE)
                {
                    ePlayerMediaType = MEDIA_TYPE_SUBTITLE;
                    nStreamIndex     = packet.streamIndex;
                }
                else
                {
                    loge("media type from parser not valid, should not run here, abort().");
                    abort();
                }
                
                while(1)
                {
                    if((!AWPLAYER_CONFIG_DISABLE_VIDEO && ePlayerMediaType == MEDIA_TYPE_VIDEO) ||
                       (!AWPLAYER_CONFIG_DISABLE_AUDIO && ePlayerMediaType == MEDIA_TYPE_AUDIO) ||
                       (!AWPLAYER_CONFIG_DISABLE_SUBTITLE && ePlayerMediaType == MEDIA_TYPE_SUBTITLE))
                    {
                        ret = PlayerRequestStreamBuffer(demux->pPlayer,
                                                        packet.length, 
                                                        &packet.buf,
                                                        &packet.buflen,
                                                        &packet.ringBuf,
                                                        &packet.ringBufLen,
                                                        ePlayerMediaType,
                                                        nStreamIndex);
                    }
                    else
                    {
                        //* allocate a buffer to read uncare media data and skip it.
                        packet.buf = malloc(packet.length);
                        if(packet.buf != NULL)
                        {
                            packet.buflen     = packet.length;
                            packet.ringBuf    = NULL;
                            packet.ringBufLen = 0;
                            ret = 0;
                        }
                        else
                        {
                            packet.buflen     = 0;
                            packet.ringBuf    = NULL;
                            packet.ringBufLen = 0;
                            ret = -1;
                        }
                    }
                    if(ret<0 || (packet.buflen+packet.ringBufLen)<packet.length)
                    {
                        logi("waiting for stream buffer.");
                        //* no buffer, try to wait sometime.
                        ret = AwMessageQueueTryGetMessage(demux->mq, &msg, 200); //* wait for 200ms if no message come.
                        if(ret == 0)    //* new message come, quit loop to process.
                            goto process_message;
                    }
                    else
                        break;  //* get buffer ok.
                }
                
                //* 3. read data to buffer and submit.
                ret = CdxParserRead(demux->pParser, &packet);
                if(ret == 0)
                {
                    if((!AWPLAYER_CONFIG_DISABLE_VIDEO && ePlayerMediaType == MEDIA_TYPE_VIDEO) ||
                       (!AWPLAYER_CONFIG_DISABLE_AUDIO && ePlayerMediaType == MEDIA_TYPE_AUDIO) ||
                       (!AWPLAYER_CONFIG_DISABLE_SUBTITLE && ePlayerMediaType == MEDIA_TYPE_SUBTITLE))
                    {
                        streamDataInfo.pData        = (char*)packet.buf;
                        streamDataInfo.nLength      = packet.length;
                        streamDataInfo.nPts         = packet.pts;
                        streamDataInfo.nPcr         = packet.pcr;
                        streamDataInfo.bIsFirstPart = (!!(packet.flags & FIRST_PART));
                        streamDataInfo.bIsLastPart  = (!!(packet.flags & LAST_PART));
                        streamDataInfo.nDuration    = packet.duration;
						streamDataInfo.pStreamInfo  = NULL;
						streamDataInfo.nStreamChangeFlag = 0;
                        streamDataInfo.nStreamChangeNum  = 0;
						if(ePlayerMediaType == MEDIA_TYPE_VIDEO && packet.infoVersion != nVideoInfoVersion)
						{
							if(packet.info)
							{
                                struct VideoInfo* pVideoinfo = (struct VideoInfo *)packet.info;
                                VideoStreamInfo*  pVideoStreamInfo = (VideoStreamInfo*)malloc(sizeof(VideoStreamInfo)*pVideoinfo->videoNum);
                                if(pVideoStreamInfo == NULL)
                                {
                                    loge("malloc video stream info fail!");
                                    return NULL;
                                }
                                memset(pVideoStreamInfo, 0, sizeof(VideoStreamInfo)*pVideoinfo->videoNum);
                                for(int i = 0; i < pVideoinfo->videoNum; i++)
                                {
                                    memcpy(&pVideoStreamInfo[i],&pVideoinfo->video[i],sizeof(VideoStreamInfo));
                                    if(pVideoinfo->video[i].pCodecSpecificData)
                                    {
                                        pVideoStreamInfo[i].pCodecSpecificData = (char*)malloc(pVideoinfo->video[i].nCodecSpecificDataLen);
                                        if(pVideoStreamInfo[i].pCodecSpecificData == NULL)
                                        {
                                            loge("malloc video specific data fail!");
                                            return NULL;
                                        }
                                        memcpy(pVideoStreamInfo[i].pCodecSpecificData,
                                               pVideoinfo->video[i].pCodecSpecificData,
                                               pVideoinfo->video[i].nCodecSpecificDataLen);
                                        pVideoStreamInfo[i].nCodecSpecificDataLen = pVideoinfo->video[i].nCodecSpecificDataLen;
                                    }
                                }

                                streamDataInfo.pStreamInfo = pVideoStreamInfo;
                                streamDataInfo.nStreamChangeFlag = 1;
                                streamDataInfo.nStreamChangeNum  = pVideoinfo->videoNum;
							}
							nVideoInfoVersion = packet.infoVersion;
							
						}
						else if(ePlayerMediaType == MEDIA_TYPE_AUDIO && packet.infoVersion != nAudioInfoVersion)
						{
							if(packet.info)
							{
                                struct AudioInfo* pAudioinfo = (struct AudioInfo *)packet.info;
                                AudioStreamInfo*  pAudioStreamInfo = (AudioStreamInfo*)malloc(sizeof(AudioStreamInfo)*pAudioinfo->audioNum);
                                if(pAudioStreamInfo == NULL)
                                {
                                    loge("malloc video stream info fail!");
                                    return NULL;
                                }
                                memset(pAudioStreamInfo, 0, sizeof(AudioStreamInfo)*pAudioinfo->audioNum);
                                for(int i = 0; i < pAudioinfo->audioNum; i++)
                                {
                                    memcpy(&pAudioStreamInfo[i],&pAudioinfo->audio[i],sizeof(AudioStreamInfo));
                                    if(pAudioinfo->audio[i].pCodecSpecificData)
                                    {
                                        pAudioStreamInfo[i].pCodecSpecificData = (char*)malloc(pAudioinfo->audio[i].nCodecSpecificDataLen);
                                        if(pAudioStreamInfo[i].pCodecSpecificData == NULL)
                                        {
                                            loge("malloc video specific data fail!");
                                            return NULL;
                                        }
                                        memcpy(pAudioStreamInfo[i].pCodecSpecificData,
                                               pAudioinfo->audio[i].pCodecSpecificData,
                                               pAudioinfo->audio[i].nCodecSpecificDataLen);
                                        pAudioStreamInfo[i].nCodecSpecificDataLen = pAudioinfo->audio[i].nCodecSpecificDataLen;
                                    }
                                }

                                streamDataInfo.pStreamInfo = pAudioStreamInfo;
                                streamDataInfo.nStreamChangeFlag = 1;
                                streamDataInfo.nStreamChangeNum  = pAudioinfo->audioNum;
							}
							nAudioInfoVersion = packet.infoVersion;
							
						}
                        PlayerSubmitStreamData(demux->pPlayer, &streamDataInfo, ePlayerMediaType, nStreamIndex);
                    }
                    else
                    {
                        //* skip the media data.
                        free(packet.buf);
                    }
                
                    //* post a read message to continue the reading job after message processed.
                    PostReadMessage(demux->mq);
                }
                else
                {
                    logw("read data from parser return fail.");
                    if(demux->bStopping == 0 && demux->bSeeking == 0)
                    {
                        int err = CdxParserGetStatus(demux->pParser);
                        
                        if(err == PSR_IO_ERR)
                        {
                            demux->bIOError = 1;
                            demux->callback(demux->pUserData, DEMUX_NOTIFY_IOERROR, 0);
                        }
                        else if (err == PSR_USER_CANCEL) // TODO: 
                        {
                            /* do noting */
                        }
                        else
                        {
                            demux->bEOS = 1;
                            demux->callback(demux->pUserData, DEMUX_NOTIFY_EOS, 0);
                            demux->eStatus = DEMUX_STATUS_COMPLETE;
                        }
                    }
                }
                
                continue;
            }
        }
        else
        {
            logw("unknow message with id %d, ignore.", msg.messageId);
        }
    }
    
    return NULL;
}


static void* CacheThread(void* arg)
{
    AwMessage         msg;
    int               ret;
    sem_t*            pReplySem;
    int*              pReplyValue;
    DemuxCompContext* demux;
    int               eCacheStatus;
	int	nVideoInfoVersion = 0;
	int nAudioInfoVersion = 0;
	//struct VideoInfo *videoInfo = NULL;
	//struct AudioInfo *audioInfo = NULL;
    
    demux = (DemuxCompContext*)arg;
    eCacheStatus = DEMUX_STATUS_STOPPED;
    
    while(1)
    {
        if(AwMessageQueueGetMessage(demux->mqCache, &msg) < 0)
        {
            loge("get message fail.");
            continue;
        }
        
cache_process_message:
        pReplySem   = (sem_t*)msg.params[0];
        pReplyValue = (int*)msg.params[1];
        
        if(msg.messageId == DEMUX_COMMAND_START)
        {
            logv("cache thread process message DEMUX_COMMAND_START.");
            
            eCacheStatus = DEMUX_STATUS_STARTED;
            PostReadMessage(demux->mqCache);
            if(pReplyValue != NULL)
                *pReplyValue = 0;
            if(pReplySem != NULL)
		        sem_post(pReplySem);
            continue;
        } //* end DEMUX_COMMAND_START
        else if(msg.messageId == DEMUX_COMMAND_PAUSE || msg.messageId == DEMUX_COMMAND_STOP)
        {
            logv("cache thread process message DEMUX_COMMAND_PAUSE or DEMUX_COMMAND_STOP.");
            
            eCacheStatus = DEMUX_STATUS_STOPPED;
            if(pReplyValue != NULL)
                *pReplyValue = 0;
            if(pReplySem != NULL)
		        sem_post(pReplySem);
            continue;
        } //* end DEMUX_COMMAND_STOP.
        else if(msg.messageId == DEMUX_COMMAND_QUIT)
        {
            logv("cache thread process message DEMUX_COMMAND_QUIT.");
            
            if(pReplyValue != NULL)
                *pReplyValue = 0;
            if(pReplySem != NULL)
		        sem_post(pReplySem);
            
            break;  //* quit the thread.
            
        } //* end DEMUX_COMMAND_QUIT.
        else if(msg.messageId == DEMUX_COMMAND_SEEK)
        {
            int64_t nSeekTimeUs;
            int64_t nSeekTimeFinal;
            
            logv("cache thread process message DEMUX_COMMAND_SEEK.");
            nSeekTimeUs = ((int64_t)msg.params[2])*1000;
            //* TODO
            //* for ts/bd/hls parser, we should map the nSeekTimeUs to pts.
            //* because pts in these parsers may be loopback or may not started with zero value.
            nSeekTimeFinal = StreamCacheSeekTo(demux->pCache, nSeekTimeUs);
            if(nSeekTimeFinal >= 0)
            {
                if(pReplyValue != NULL)
                    *pReplyValue = (int)(nSeekTimeFinal/1000);
            }
            else
            {
                if(pReplyValue != NULL)
                    *pReplyValue = -1;
                StreamCacheFlushAll(demux->pCache);
            }
            
            if(pReplySem != NULL)
		        sem_post(pReplySem);
            
		    continue;
        }
        else if(msg.messageId == DEMUX_COMMAND_READ)
        {
            if(eCacheStatus != DEMUX_STATUS_STARTED)
                continue;
            
            logi("cache thread process message DEMUX_COMMAND_READ.");
            
            if(StreamCacheOverflow(demux->pCache))
            {
                //* wait some time for cache buffer.
                ret = AwMessageQueueTryGetMessage(demux->mqCache, &msg, 200); //* wait for 200ms if no message come.
                if(ret == 0)    //* new message come, quit loop to process.
                    goto cache_process_message;
                
                //* post a read message to continue the reading job after message processed.
                PostReadMessage(demux->mqCache);
            }
            else
            {
                //**************************************************************
                //* read data directly from parser.
                //**************************************************************
                
                CdxPacketT packet;
                CacheNode  node;
				memset(&packet, 0x00, sizeof(CdxPacketT));
                
                //* 1. get data type.
                if(CdxParserPrefetch(demux->pParser, &packet) != 0)
                {
                    logw("prefetch fail.");
                    if(demux->bStopping == 0 && demux->bSeeking == 0)
                    {
                        int err = CdxParserGetStatus(demux->pParser);
                        
                        if(err == PSR_IO_ERR)
                        {
                            demux->bIOError = 1;
                        }
                        else if (err == PSR_USER_CANCEL) // TODO: 
                        {
                            /* do noting */
                        }
                        else
                        {
                            demux->bEOS = 1;
                        }
                    }
                    
                    continue;
                }
                
                //* 2. request cache buffer.
                while(1)
                {
                    node.pData = (unsigned char*)malloc(packet.length);
                    if(node.pData == NULL)
                    {
                        logw("allocate memory for cache node fail, waiting for memory.");
                        //* no free memory, try to wait sometime.
                        ret = AwMessageQueueTryGetMessage(demux->mqCache, &msg, 200); //* wait for 200ms if no message come.
                        if(ret == 0)    //* new message come, quit loop to process.
                            goto cache_process_message;
                    }
                    else
                    {
                        packet.buf        = node.pData;
                        packet.buflen     = packet.length;
                        packet.ringBuf    = NULL;
                        packet.ringBufLen = 0;
                        break;
                    }
                }
                
                //* 3. read data to buffer and submit.
                ret = CdxParserRead(demux->pParser, &packet);
                if(ret == 0)
                {
                    node.pNext        = NULL;
                    node.nLength      = packet.length;
                    node.eMediaType   = packet.type;
                    node.nStreamIndex = packet.streamIndex;
                    node.nFlags       = packet.flags;
                    node.nPts         = packet.pts;
                    node.nPcr         = packet.pcr;
                    node.bIsFirstPart = 1;
                    node.bIsLastPart  = 1;
                    node.nDuration    = packet.duration;
					node.info		  = NULL;
                    node.infoVersion  = -1;

					if(packet.type == CDX_MEDIA_VIDEO)
					{
						if(packet.infoVersion != nVideoInfoVersion && packet.info)
						{
                            logd("cache -- video stream info change: preInfoVersion = %d, curInfoVersion = %d",
                                  nVideoInfoVersion,
                                  packet.infoVersion);
							struct VideoInfo *info = (struct VideoInfo *)malloc(sizeof(struct VideoInfo));
                            
                            if(!info)
							{
								logd("malloc fail.");
								return NULL;
							}
							memcpy(info, packet.info, sizeof(struct VideoInfo));
                            logd("***cache : video info = %p, videoNum = %d",info,info->videoNum);
							int i;
							for(i = 0; i < info->videoNum; i++)
							{
								if(info->video[i].pCodecSpecificData && info->video[i].nCodecSpecificDataLen)
								{
									info->video[i].pCodecSpecificData = (char *)malloc(info->video[i].nCodecSpecificDataLen);
									if(!info->video[i].pCodecSpecificData)
									{
										logd("malloc fail.");
										return NULL;
									}
									memcpy(info->video[i].pCodecSpecificData, ((struct VideoInfo *)packet.info)->video[i].pCodecSpecificData, info->video[i].nCodecSpecificDataLen);
								}
							}
							
							CdxAtomicSet(&info->ref, 0);
                            
                            CdxAtomicInc(&info->ref);
							node.info        = info;
                            node.infoVersion = 1;
						}
						nVideoInfoVersion = packet.infoVersion;
					}
					else if(packet.type == CDX_MEDIA_AUDIO)
					{
						if(packet.infoVersion != nAudioInfoVersion && packet.info)
						{
                            logd("cache -- audio stream info change: preInfoVersion = %d, curInfoVersion = %d",
                                  nAudioInfoVersion,
                                  packet.infoVersion);
                            
							struct AudioInfo *info = (struct AudioInfo *)malloc(sizeof(struct AudioInfo));
							if(!info)
							{
								logd("malloc fail.");
								return NULL;
							}
							memcpy(info, packet.info, sizeof(struct AudioInfo));
							int i;
							for(i = 0; i < info->audioNum; i++)
							{
								if(info->audio[i].pCodecSpecificData && info->audio[i].nCodecSpecificDataLen)
								{
									info->audio[i].pCodecSpecificData = (char *)malloc(info->audio[i].nCodecSpecificDataLen);
									if(!info->audio[i].pCodecSpecificData)
									{
										logd("malloc fail.");
										return NULL;
									}
									memcpy(info->audio[i].pCodecSpecificData, ((struct AudioInfo *)packet.info)->audio[i].pCodecSpecificData, info->audio[i].nCodecSpecificDataLen);
								}
							}
							
							CdxAtomicSet(&info->ref, 0);
                            CdxAtomicInc(&info->ref);
                            node.info        = info;
                            node.infoVersion = 1;
						}
						nAudioInfoVersion = packet.infoVersion;
					}
					
                    StreamCacheAddOneFrame(demux->pCache, &node);
                    
                    //* post a read message to continue the reading job after message processed.
                    PostReadMessage(demux->mqCache);
                }
                else
                {
                    logw("read data from parser return fail.");
                    if(node.pData != NULL)
                        free(node.pData);
                    
                    if(demux->bStopping == 0 && demux->bSeeking == 0)
                    {
                        int err = CdxParserGetStatus(demux->pParser);
                        
                        if(err == PSR_IO_ERR)
                        {
                            demux->bIOError = 1;
                        }
                        else if (err == PSR_USER_CANCEL) // TODO: 
                        {
                            /* do noting */
                        }
                        else
                        {
                            demux->bEOS = 1;
                        }
                    }
                }
                continue;
            }   //* end if(StreamCacheOverflow(demux->pCache)) {} else {}
        }   //* end DEMUX_COMMAND_READ.
    }   //* end while(1).
    
    return NULL;
}


static void clearDataSourceFields(CdxDataSourceT* source)
{
    CdxHttpHeaderFieldsT* pHttpHeaders;
    int                   i;
    int                   nHeaderSize;
    
    if(source->uri != NULL)
    {
        free(source->uri);
        source->uri = NULL;
    }
    
    if(source->extraDataType == EXTRA_DATA_HTTP_HEADER &&
       source->extraData != NULL)
    {
        pHttpHeaders = (CdxHttpHeaderFieldsT*)source->extraData;
        nHeaderSize  = pHttpHeaders->num;
        
        for(i=0; i<nHeaderSize; i++)
        {
            if(pHttpHeaders->pHttpHeader[i].key != NULL)
                free((void*)pHttpHeaders->pHttpHeader[i].key);
            if(pHttpHeaders->pHttpHeader[i].val != NULL)
                free((void*)pHttpHeaders->pHttpHeader[i].val);
        }
        
        free(pHttpHeaders->pHttpHeader);
        free(pHttpHeaders);
        source->extraData = NULL;
        source->extraDataType = EXTRA_DATA_UNKNOWN;
    }
    
    return;
}

#if (CONFIG_OS_VERSION >= OPTION_OS_VERSION_ANDROID_5_0)
static int setDataSourceFields(DemuxCompContext *demuxDhr, CdxDataSourceT* source, void* pHTTPServer, char* uri, KeyedVector<String8,String8>* pHeaders)
#else
static int setDataSourceFields(DemuxCompContext *demuxDhr, CdxDataSourceT* source, char* uri, KeyedVector<String8,String8>* pHeaders)
#endif
{
    CdxHttpHeaderFieldsT* pHttpHeaders;
    int                   i;
    int                   nHeaderSize;
    
    clearDataSourceFields(source);
    
    if(uri != NULL)
    {
        //* check whether ths uri has a scheme.
        if(strstr(uri, "://") != NULL)
        {
            source->uri = strdup(uri);
            if(source->uri == NULL)
            {
                loge("can not dump string of uri.");
                return -1;
            }
        }
        else
        {
            source->uri  = (char*)malloc(strlen(uri)+8);
            if(source->uri == NULL)
            {
                loge("can not dump string of uri.");
                return -1;
            }
            sprintf(source->uri, "file://%s", uri);
        }
        
        if (strncasecmp("bdmv://", uri, 7) == 0)
        {
            logw("trace...");
            static struct BdmvExtraDataS bdmvED;
            bdmvED.ioCb = &demuxIoOps;
            bdmvED.cbHdr = (void *)demuxDhr;
            source->extraData = &bdmvED;
            source->extraDataType = EXTRA_DATA_BDMV;
        }


#if (CONFIG_OS_VERSION >= OPTION_OS_VERSION_ANDROID_5_0)
		source->pHTTPServer = pHTTPServer;
#endif

        if(pHeaders != NULL && (!strncasecmp("http://", uri, 7) || !strncasecmp("https://", uri, 8)))
        {
            String8 key;
            String8 value;
            char*   str;
            
            i = pHeaders->indexOfKey(String8("x-hide-urls-from-log"));
            if(i >= 0)
                pHeaders->removeItemsAt(i);
                
            nHeaderSize = pHeaders->size();
            if(nHeaderSize > 0)
            {
                pHttpHeaders = (CdxHttpHeaderFieldsT*)malloc(sizeof(CdxHttpHeaderFieldsT));
                if(pHttpHeaders == NULL)
                {
                    loge("can not malloc memory for http header.");
                    clearDataSourceFields(source);
                    return -1;
                }
                memset(pHttpHeaders, 0, sizeof(CdxHttpHeaderFieldsT));
                pHttpHeaders->num = nHeaderSize;
                
                pHttpHeaders->pHttpHeader = (CdxHttpHeaderFieldT*)malloc(sizeof(CdxHttpHeaderFieldT)*nHeaderSize);
                if(pHttpHeaders->pHttpHeader == NULL)
                {
                    loge("can not malloc memory for http header.");
                    free(pHttpHeaders);
                    clearDataSourceFields(source);
                    return -1;
                }
                
                source->extraData = (void*)pHttpHeaders;
                source->extraDataType = EXTRA_DATA_HTTP_HEADER;
                
                for(i=0; i<nHeaderSize; i++)
                {
                    key   = pHeaders->keyAt(i);
                    value = pHeaders->valueAt(i);
                    str = (char*)key.string();
                    if(str != NULL)
                    {
                        pHttpHeaders->pHttpHeader[i].key = (const char*)strdup(str);
                        if(pHttpHeaders->pHttpHeader[i].key == NULL)
                        {
                            loge("can not dump string of http header.");
                            clearDataSourceFields(source);
                            return -1;
                        }
                    }
                    else
                        pHttpHeaders->pHttpHeader[i].key = NULL;
                    
                    str = (char*)value.string();
                    if(str != NULL)
                    {
                        pHttpHeaders->pHttpHeader[i].val = (const char*)strdup(str);
                        if(pHttpHeaders->pHttpHeader[i].val == NULL)
                        {
                            loge("can not dump string of http header.");
                            clearDataSourceFields(source);
                            return -1;
                        }
                    }
                    else
                        pHttpHeaders->pHttpHeader[i].val = NULL;
                }
            }
        }
    }
    
    return 0;
}


static int setMediaInfo(MediaInfo* pMediaInfo, CdxMediaInfoT* pInfoFromParser)
{
    int                 i;
    int                 nStreamCount;
    VideoStreamInfo*    pVideoStreamInfo = NULL;
	AudioStreamInfo*    pAudioStreamInfo = NULL;
    SubtitleStreamInfo* pSubtitleStreamInfo = NULL;
    int                 nCodecSpecificDataLen;
    char*               pCodecSpecificData = NULL;
    
    clearMediaInfo(pMediaInfo);
    
    pMediaInfo->nDurationMs = pInfoFromParser->program[0].duration;
    pMediaInfo->nFileSize   = pInfoFromParser->fileSize;
	pMediaInfo->nFirstPts   = pInfoFromParser->program[0].firstPts;
    if(pInfoFromParser->bitrate > 0)
        pMediaInfo->nBitrate = pInfoFromParser->bitrate;
    else if(pInfoFromParser->fileSize > 0 && pInfoFromParser->program[0].duration > 0)
        pMediaInfo->nBitrate = (int)((pInfoFromParser->fileSize*8*1000)/pInfoFromParser->program[0].duration);
    else
        pMediaInfo->nBitrate = 0;
    pMediaInfo->bSeekable   = pInfoFromParser->bSeekable;
    
    memcpy(pMediaInfo->cRotation,pInfoFromParser->rotate,4*sizeof(unsigned char));
    
    nStreamCount = pInfoFromParser->program[0].videoNum;
    logv("video stream count = %d", nStreamCount);
    if(nStreamCount > 0)
    {
        pVideoStreamInfo = (VideoStreamInfo*)malloc(sizeof(VideoStreamInfo)*nStreamCount);
        if(pVideoStreamInfo == NULL)
        {
            loge("can not alloc memory for media info.");
            return -1;
        }
        memset(pVideoStreamInfo, 0, sizeof(VideoStreamInfo)*nStreamCount);
        pMediaInfo->pVideoStreamInfo = pVideoStreamInfo;
        
        for(i=0; i<nStreamCount; i++)
        {
            pVideoStreamInfo = &pMediaInfo->pVideoStreamInfo[i];
            memcpy(pVideoStreamInfo, &pInfoFromParser->program[0].video[i], sizeof(VideoStreamInfo));
            
            pCodecSpecificData    = pVideoStreamInfo->pCodecSpecificData;
            nCodecSpecificDataLen = pVideoStreamInfo->nCodecSpecificDataLen;
            pVideoStreamInfo->pCodecSpecificData = NULL;
            pVideoStreamInfo->nCodecSpecificDataLen = 0;
            
            if(pCodecSpecificData != NULL && nCodecSpecificDataLen > 0)
            {
                pVideoStreamInfo->pCodecSpecificData = (char*)malloc(nCodecSpecificDataLen);
                if(pVideoStreamInfo->pCodecSpecificData == NULL)
                {
                    loge("can not alloc memory for media info.");
                    clearMediaInfo(pMediaInfo);
                    return -1;
                }
                
                memcpy(pVideoStreamInfo->pCodecSpecificData, pCodecSpecificData, nCodecSpecificDataLen);
                pVideoStreamInfo->nCodecSpecificDataLen = nCodecSpecificDataLen;
            }

            if(pInfoFromParser->program[0].flags & kUseSecureInputBuffers)
                pVideoStreamInfo->bSecureStreamFlagLevel1 = 1;
            
            logv("the %dth video stream info.", i);
            logv("    codec: %d.", pVideoStreamInfo->eCodecFormat);
            logv("    width: %d.", pVideoStreamInfo->nWidth);
            logv("    height: %d.", pVideoStreamInfo->nHeight);
            logv("    frame rate: %d.", pVideoStreamInfo->nFrameRate);
            logv("    aspect ratio: %d.", pVideoStreamInfo->nAspectRatio);
            logv("    is 3D: %s.", pVideoStreamInfo->bIs3DStream ? "true" : "false");
            logv("    codec specific data size: %d.", pVideoStreamInfo->nCodecSpecificDataLen);
            logv("    bSecureStreamFlag : %d",pVideoStreamInfo->bSecureStreamFlag);
        }
        
        pMediaInfo->nVideoStreamNum = nStreamCount;
    }
    
    //* copy audio stream info.
    nStreamCount = pInfoFromParser->program[0].audioNum;
    if(nStreamCount > 0)
    {
        pAudioStreamInfo = (AudioStreamInfo*)malloc(sizeof(AudioStreamInfo)*nStreamCount);
        if(pAudioStreamInfo == NULL)
        {
            clearMediaInfo(pMediaInfo);
            loge("can not alloc memory for media info.");
            return -1;
        }
        memset(pAudioStreamInfo, 0, sizeof(AudioStreamInfo)*nStreamCount);
        pMediaInfo->pAudioStreamInfo = pAudioStreamInfo;
        
        for(i=0; i<nStreamCount; i++)
        {
            pAudioStreamInfo = &pMediaInfo->pAudioStreamInfo[i];
            memcpy(pAudioStreamInfo, &pInfoFromParser->program[0].audio[i], sizeof(AudioStreamInfo));
            
            pCodecSpecificData    = pAudioStreamInfo->pCodecSpecificData;
            nCodecSpecificDataLen = pAudioStreamInfo->nCodecSpecificDataLen;
            pAudioStreamInfo->pCodecSpecificData = NULL;
            pAudioStreamInfo->nCodecSpecificDataLen = 0;
            
            if(pCodecSpecificData != NULL && nCodecSpecificDataLen > 0)
            {
                pAudioStreamInfo->pCodecSpecificData = (char*)malloc(nCodecSpecificDataLen);
                if(pAudioStreamInfo->pCodecSpecificData == NULL)
                {
                    loge("can not alloc memory for media info.");
                    clearMediaInfo(pMediaInfo);
                    return -1;
                }
                
                memcpy(pAudioStreamInfo->pCodecSpecificData, pCodecSpecificData, nCodecSpecificDataLen);
                pAudioStreamInfo->nCodecSpecificDataLen = nCodecSpecificDataLen;
            }
        }
        
        pMediaInfo->nAudioStreamNum = nStreamCount;
    }
	
    //* copy subtitle stream info.
    nStreamCount = pInfoFromParser->program[0].subtitleNum;
    if(nStreamCount > 0)
    {
        pSubtitleStreamInfo = (SubtitleStreamInfo*)malloc(sizeof(SubtitleStreamInfo)*nStreamCount);
        if(pSubtitleStreamInfo == NULL)
        {
            clearMediaInfo(pMediaInfo);
            loge("can not alloc memory for media info.");
            return -1;
        }
        memset(pSubtitleStreamInfo, 0, sizeof(SubtitleStreamInfo)*nStreamCount);
        pMediaInfo->pSubtitleStreamInfo = pSubtitleStreamInfo;

        for(i=0; i<nStreamCount; i++)
        {
            pSubtitleStreamInfo = &pMediaInfo->pSubtitleStreamInfo[i];
            memcpy(pSubtitleStreamInfo, &pInfoFromParser->program[0].subtitle[i], sizeof(SubtitleStreamInfo));
            pSubtitleStreamInfo->bExternal = 0;
            pSubtitleStreamInfo->pUrl      = NULL;
            pSubtitleStreamInfo->fd        = -1;
            pSubtitleStreamInfo->fdSub     = -1;
        }

        pMediaInfo->nSubtitleStreamNum = nStreamCount;
    }
    
    return 0;
}


static void clearMediaInfo(MediaInfo* pMediaInfo)
{
    int                 i;
    VideoStreamInfo*    pVideoStreamInfo;
    AudioStreamInfo*    pAudioStreamInfo;
    
    if(pMediaInfo->nVideoStreamNum > 0)
    {
        for(i=0; i<pMediaInfo->nVideoStreamNum; i++)
        {
            pVideoStreamInfo = &pMediaInfo->pVideoStreamInfo[i];
            if(pVideoStreamInfo->pCodecSpecificData != NULL && 
               pVideoStreamInfo->nCodecSpecificDataLen > 0)
            {
                free(pVideoStreamInfo->pCodecSpecificData);
                pVideoStreamInfo->pCodecSpecificData = NULL;
                pVideoStreamInfo->nCodecSpecificDataLen = 0;
            }
        }
        
        free(pMediaInfo->pVideoStreamInfo);
        pMediaInfo->pVideoStreamInfo = NULL;
        pMediaInfo->nVideoStreamNum = 0;
    }
    
    
    if(pMediaInfo->nAudioStreamNum > 0)
    {
        for(i=0; i<pMediaInfo->nAudioStreamNum; i++)
        {
            pAudioStreamInfo = &pMediaInfo->pAudioStreamInfo[i];
            if(pAudioStreamInfo->pCodecSpecificData != NULL &&
               pAudioStreamInfo->nCodecSpecificDataLen > 0)
            {
                free(pAudioStreamInfo->pCodecSpecificData);
                pAudioStreamInfo->pCodecSpecificData = NULL;
                pAudioStreamInfo->nCodecSpecificDataLen = 0;
            }
        }
        
        free(pMediaInfo->pAudioStreamInfo);
        pMediaInfo->pAudioStreamInfo = NULL;
        pMediaInfo->nAudioStreamNum = 0;
    }
    
    
    if(pMediaInfo->nSubtitleStreamNum > 0)
    {
        free(pMediaInfo->pSubtitleStreamInfo);
        pMediaInfo->pSubtitleStreamInfo = NULL;
        pMediaInfo->nSubtitleStreamNum = 0;
    }
    
    pMediaInfo->nFileSize      = 0;
    pMediaInfo->nDurationMs    = 0;
    pMediaInfo->eContainerType = CONTAINER_TYPE_UNKNOWN;
    pMediaInfo->bSeekable      = 0;
    
    return;
}


static int PlayerBufferOverflow(Player* p)
{
    int bVideoOverflow;
    int bAudioOverflow;
    
    int     nPictureNum;
    int     nFrameDuration;
    int     nPcmDataSize;
    int     nSampleRate;
    int     nChannelCount;
    int     nBitsPerSample;
    int     nStreamDataSize;
    int     nStreamBufferSize;
    int     nBitrate;
    int64_t nVideoCacheTime;
    int64_t nAudioCacheTime;
    
    bVideoOverflow = 1;
    bAudioOverflow = 1;
    
    if(PlayerHasVideo(p))
    {
        nPictureNum     = PlayerGetValidPictureNum(p);
        nFrameDuration  = PlayerGetVideoFrameDuration(p);
        nStreamDataSize = PlayerGetVideoStreamDataSize(p);
        nStreamBufferSize = PlayerGetVideoStreamBufferSize(p);
        nBitrate        = PlayerGetVideoBitrate(p);
        
        nVideoCacheTime = nPictureNum*nFrameDuration;
        
        if(nBitrate > 0)
            nVideoCacheTime += ((int64_t)nStreamDataSize)*8*1000*1000/nBitrate;
        
        if( nVideoCacheTime <= 2000000  || nStreamDataSize<nStreamBufferSize/2 )   //* cache more than 2 seconds of data.
            bVideoOverflow = 0;
        
        logv("picNum = %d, frameDuration = %d, dataSize = %d, bitrate = %d, bVideoOverflow = %d ,nStreamDataSize = %d ,nStreamBufferSize = %d",
            nPictureNum, nFrameDuration, nStreamDataSize, nBitrate, bVideoOverflow,nStreamDataSize,nStreamBufferSize);
    }
    
    if(PlayerHasAudio(p))
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
        
        if(nAudioCacheTime <= 2000000)   //* cache more than 2 seconds of data.
            bAudioOverflow = 0;
        
        logv("nPcmDataSize = %d, nStreamDataSize = %d, nBitrate = %d, nAudioCacheTime = %lld, bAudioOverflow = %d",
            nPcmDataSize, nStreamDataSize, nBitrate, nAudioCacheTime, bAudioOverflow);
    }
    
    return bVideoOverflow && bAudioOverflow;
}


static int PlayerBufferUnderflow(Player* p)
{
    int bVideoUnderflow;
    int bAudioUnderFlow;
    
    bVideoUnderflow = 0;
    bAudioUnderFlow = 0;
    
    if(PlayerHasVideo(p))
    {
        int nPictureNum;
        int nStreamFrameNum;
        
        nPictureNum = PlayerGetValidPictureNum(p);
        nStreamFrameNum = PlayerGetVideoStreamFrameNum(p);
        if(nPictureNum + nStreamFrameNum < 10)
            bVideoUnderflow = 1;
        
        logi("nPictureNum = %d, nStreamFrameNum = %d, bVideoUnderflow = %d",
            nPictureNum, nStreamFrameNum, bVideoUnderflow);
    }
    
    if(PlayerHasAudio(p))
    {
        int nStreamDataSize;
        int nPcmDataSize;
        int nCacheTime;
        
        nStreamDataSize = PlayerGetAudioStreamDataSize(p);
        nPcmDataSize    = PlayerGetAudioPcmDataSize(p);
        nCacheTime      = 0;
        if(nCacheTime == 0 && nPcmDataSize == 0 && nStreamDataSize == 0)
            bAudioUnderFlow = 1;
        
        logi("nStreamDataSize = %d, nPcmDataSize = %d, nCacheTime = %d, bAudioUnderFlow = %d",
            nStreamDataSize, nPcmDataSize, nCacheTime, bAudioUnderFlow);
    }
    
    return bVideoUnderflow | bAudioUnderFlow;
}


static int GetCacheState(DemuxCompContext* demux)
{    
    if(CdxParserControl(demux->pParser, CDX_PSR_CMD_GET_CACHESTATE, &demux->sCacheState) == 0)
        return 0;
    else
        return -1;
}


static int64_t GetSysTime()
{
    int64_t time;
    struct timeval t;
    gettimeofday(&t, NULL);
    time = (int64_t)t.tv_sec * 1000000LL;
    time += t.tv_usec;
    return time;
}


static void NotifyCacheState(DemuxCompContext* demux)
{
    int nTotalPercentage;
    int nBufferPercentage;
    int nLoadingPercentage;
    int param[3];
        
    GetCacheState(demux);
        
    param[0] = nTotalPercentage   = demux->sCacheState.nPercentage;
    param[1] = nBufferPercentage  = StreamCacheGetBufferFullness(demux->pCache);
    param[2] = nLoadingPercentage = StreamCacheGetLoadingProgress(demux->pCache);
        
    logv("notify cache state, total percent = %d%%, buffer fullness = %d%%, loading progress = %d%%", 
        nTotalPercentage, nBufferPercentage, nLoadingPercentage);
    demux->callback(demux->pUserData, DEMUX_NOTIFY_CACHE_STAT, (void*)param);
    return;
}


static void PostReadMessage(AwMessageQueue* mq)
{
    if(AwMessageQueueGetCount(mq)<=0)
    {
        AwMessage msg;
        setMessage(&msg, DEMUX_COMMAND_READ);
        AwMessageQueuePostMessage(mq, &msg);
    }
    return;
}

static int CheckParserWhetherBeForbided(CdxParserTypeE eType)
{
	if(CONFIG_ENABLE_DEMUX_ASF == 0 && eType == CDX_PARSER_ASF)
		return -1;
	else if(CONFIG_ENABLE_DEMUX_AVI == 0 && eType == CDX_PARSER_AVI)
		return -1;
	else if(CONFIG_ENABLE_DEMUX_BLUERAYDISK == 0 && eType == CDX_PARSER_BD)
		return -1;
	else if(CONFIG_ENABLE_DEMUX_MPEGDASH == 0 && eType == CDX_PARSER_DASH)
		return -1;
	else if(CONFIG_ENABLE_DEMUX_FLV == 0 && eType == CDX_PARSER_FLV)
		return -1;
	else if(CONFIG_ENABLE_DEMUX_HLS == 0 && eType == CDX_PARSER_HLS)
		return -1;
	else if(CONFIG_ENABLE_DEMUX_MKV == 0 && eType == CDX_PARSER_MKV)
		return -1;
	else if(CONFIG_ENABLE_DEMUX_MMS == 0 && eType == CDX_PARSER_MMS)
		return -1;
	else if(CONFIG_ENABLE_DEMUX_MOV == 0 && eType == CDX_PARSER_MOV)
		return -1;
	else if(CONFIG_ENABLE_DEMUX_MPG == 0 && eType == CDX_PARSER_MPG)
		return -1;
	else if(CONFIG_ENABLE_DEMUX_PMP == 0 && eType == CDX_PARSER_PMP)
		return -1;
	else if(CONFIG_ENABLE_DEMUX_OGG == 0 && eType == CDX_PARSER_OGG)
		return -1;
	else if(CONFIG_ENABLE_DEMUX_RX == 0 && eType == CDX_PARSER_RMVB)
		return -1;	
	else if(CONFIG_ENABLE_DEMUX_TS == 0 && eType == CDX_PARSER_TS)
		return -1;
	else if(CONFIG_ENABLE_DEMUX_M3U9 == 0 && eType == CDX_PARSER_M3U9)
		return -1;
	else if(CONFIG_ENABLE_DEMUX_PLAYLIST == 0 && eType == CDX_PARSER_PLAYLIST)
		return -1;
	else if(CONFIG_ENABLE_DEMUX_APE == 0 && eType == CDX_PARSER_APE)
		return -1;
	else if(CONFIG_ENABLE_DEMUX_FLAC == 0 && eType == CDX_PARSER_FLAC)
		return -1;
	else if(CONFIG_ENABLE_DEMUX_AMR == 0 && eType == CDX_PARSER_AMR)
		return -1;
	else if(CONFIG_ENABLE_DEMUX_ATRAC == 0 && eType == CDX_PARSER_ATRAC)
		return -1;
	else if(CONFIG_ENABLE_DEMUX_MP3 == 0 && eType == CDX_PARSER_MP3)
		return -1;
	else if(CONFIG_ENABLE_DEMUX_DTS == 0 && eType == CDX_PARSER_DTS)
		return -1;
	else if(CONFIG_ENABLE_DEMUX_AC3 == 0 && eType == CDX_PARSER_AC3)
		return -1;
	else if(CONFIG_ENABLE_DEMUX_AAC == 0 && eType == CDX_PARSER_AAC)
		return -1;
	else if(CONFIG_ENABLE_DEMUX_WAV == 0 && eType == CDX_PARSER_WAV)
		return -1;
	else if(CONFIG_ENABLE_DEMUX_REMUX == 0 && eType == CDX_PARSER_REMUX)
		return -1;
	else if(CONFIG_ENABLE_DEMUX_WVM == 0 && eType == CDX_PARSER_WVM)
		return -1;
	else if(CONFIG_ENABLE_DEMUX_MMSHTTP == 0 && eType == CDX_PARSER_MMSHTTP)
		return -1;
	else if(CONFIG_ENABLE_DEMUX_AWTS == 0 && eType == CDX_PARSER_AWTS)
		return -1;
	else if(CONFIG_ENABLE_DEMUX_SSTR == 0 && eType == CDX_PARSER_SSTR)
		return -1;
	else if(CONFIG_ENABLE_DEMUX_CAF == 0 && eType == CDX_PARSER_CAF)
		return -1;
	else
		return 0;
}


//*****************************************************************
//* DemuxCompGetTSInfoYUNOS
//* eMessageId:
//* 	YUNOS_TS_INFO 					= 1
//* 	YUNOS_MEDIA_INFO				= 2
//* 	YUNOS_HTTP_DOWNLOAD_ERROR_INFO	= 3
//*		YUNOS_SOURCE					= 4
//*****************************************************************
int DemuxCompGetTSInfoYUNOS(DemuxComp* d,int eMessageId,void* info,void* param)
{
	DemuxCompContext* demux;
	demux = (DemuxCompContext*)d;

    switch(eMessageId)
    {
		case 1: //YUNOS_TS_INFO
		{
			pthread_mutex_lock(&demux->mutex1);
			if(demux->pParser && param != NULL)
			{
				cdx_int64 tsBandwidth =0;
				CdxParserControl(demux->pParser,CDX_PSR_CMD_GET_TS_M3U_BANDWIDTH,(void*)&tsBandwidth);
				demux->tsBandwidth = tsBandwidth;

				int tsNum = 0;
				CdxParserControl(demux->pParser,CDX_PSR_CMD_GET_TS_SEQ_NUM,(void*)&tsNum);
				demux->tsNum = tsNum;

				cdx_int64 tsLength =0;
				CdxParserControl(demux->pParser,CDX_PSR_CMD_GET_TS_LENGTH,(void*)&tsLength);
				demux->tsLength = tsLength;

				cdx_int64 tsDuration =0;
				CdxParserControl(demux->pParser,CDX_PSR_CMD_GET_TS_DURATION,(void*)&tsDuration);
				demux->tsDuration = tsDuration;

				((cdx_int64*)info)[0] = demux->tsBandwidth;
				((cdx_int64*)info)[1] = (cdx_int64)demux->tsNum;
				((cdx_int64*)info)[2] = demux->tsLength;
				((cdx_int64*)info)[3] = demux->tsDuration;
				((cdx_int64*)info)[4] = demux->tsSendTime[1];
				((cdx_int64*)info)[5] = demux->tsFirstBtime[1];
				((cdx_int64*)info)[6] = demux->tsLastBtime;
				((cdx_int64*)info)[7] = GetSysTime();

				CDX_LOGV(" DemuxCompGetTSInfoYUNOS tsHttpTcpIP[%s] tsBandwidth[%lld] tsNum[%d] tsDuration[%lld] tsLength[%lld]",
					demux->tsHttpTcpIP,demux->tsBandwidth,demux->tsNum,demux->tsDuration,demux->tsLength);
				if( param != NULL && demux->tsHttpTcpIP[0])
				{
					strcpy((char *)param, demux->tsHttpTcpIP);
				}
			}
			pthread_mutex_unlock(&demux->mutex1);
			break;
		}
		case 2: //YUNOS_MEDIA_INFO
		{
			pthread_mutex_lock(&demux->mutex1);
			if( param != NULL && demux->pParser )
			{
				cdx_int64 tsBandwidth =0;
				CdxParserControl(demux->pParser,CDX_PSR_CMD_GET_TS_M3U_BANDWIDTH,(void*)&tsBandwidth);
				*(cdx_int64*)info = tsBandwidth;
			}
			if( param != NULL && demux->tsHttpTcpIP[0])
			{
				strcpy((char *)param, demux->tsHttpTcpIP);
			}
			pthread_mutex_unlock(&demux->mutex1);
			break;
		}
		case 3: //YUNOS_HTTP_DOWNLOAD_ERROR_INFO
		{
			pthread_mutex_lock(&demux->mutex1);
			if( param != NULL && demux->pParser )
			{
				int tsNum = 0;
				CdxParserControl(demux->pParser,CDX_PSR_CMD_GET_TS_SEQ_NUM,(void*)&tsNum);
				cdx_int64 tsBandwidth =0;
				CdxParserControl(demux->pParser,CDX_PSR_CMD_GET_TS_M3U_BANDWIDTH,(void*)&tsBandwidth);
				((cdx_int64*)info)[0] = (cdx_int64)tsNum;
				((cdx_int64*)info)[1] = tsBandwidth;
				if( demux->httpRes[0])
				{
					char base64EncodeOut[4096];
					unsigned long httpResLen = strlen(demux->httpRes);
					if(httpResLen > 4096)
						return -1;
					base64Encode((unsigned char*)demux->httpRes, httpResLen, (unsigned char*)base64EncodeOut);
					strcpy((char *)param, base64EncodeOut);
				}
			}
			pthread_mutex_unlock(&demux->mutex1);
			break;
		}
		case 4:  //YUNOS_SOURCE set UUID
		{
			if( param != NULL )
			{
				strncpy(demux->mYunOSUUID,(char*)param,32);
			}
			else
			{
				return -1;
			}
			pthread_mutex_lock(&demux->mutex1);
			demux->mYunOSInfoEndale = 1;
			if(demux->pParser)
			{
				CdxParserControl(demux->pParser,CDX_PSR_CMD_SET_YUNOS_UUID,(void*)demux->mYunOSUUID);
			}
			pthread_mutex_unlock(&demux->mutex1);
			break;
		}
        default:
	    logw("ignore eMessageId = 0x%x.", eMessageId);
	    return -1;
    }

	return 0;
}

static int base64Encode(const unsigned char *in_ptr,  unsigned long encode_len,  unsigned char *out_ptr)
{
	static const char *codes_src = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	unsigned long i,loop_len;
	unsigned char *dst;
	dst   = out_ptr;
	loop_len = 3*(encode_len / 3);
	for (i = 0; i < loop_len; i += 3)
	{
		*dst++ = codes_src[in_ptr[0] >> 2];
		*dst++ = codes_src[((in_ptr[0] & 3) << 4) + (in_ptr[1] >> 4)];
		*dst++ = codes_src[((in_ptr[1] & 0xf) << 2) + (in_ptr[2] >> 6)];
		*dst++ = codes_src[in_ptr[2] & 0x3f];
		in_ptr += 3;
	}
	if (i < encode_len)
	{
		unsigned a_tmp = in_ptr[0];
		unsigned b_tmp = (i+1 < encode_len) ? in_ptr[1] : 0;
		unsigned c_tmp = 0;
		*dst++ = codes_src[a_tmp >> 2];
		*dst++ = codes_src[((a_tmp & 3) << 4) + (b_tmp >> 4)];
		*dst++ = (i+1 < encode_len) ? codes_src[((b_tmp & 0xf) << 2) + (c_tmp >> 6)] : '=';
		*dst++ = '=';
	}

	*dst = '\0';

	return dst - out_ptr;
}
