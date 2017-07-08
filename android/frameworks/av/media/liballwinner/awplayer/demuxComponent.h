
#ifndef DEMUX_COMPONENT_H
#define DEMUX_COMPONENT_H

#include <sys/types.h>
#include <utils/Errors.h>
#include <utils/KeyedVector.h>
#include <utils/String8.h>
#include <utils/RefBase.h>
#include "player.h"      //* player library in "android/hardware/aw/"
#include "mediaInfo.h"

using namespace android;

typedef void* DemuxComp;

#define SOURCE_TYPE_URL           0x1
#define SOURCE_TYPE_FD            0x2
#define SOURCE_TYPE_ISTREAMSOURCE 0x3

enum EDEMUXNOTIFY  //* player internal notify.
{
    DEMUX_NOTIFY_PREPARED       = 512,
    DEMUX_NOTIFY_EOS,
    DEMUX_NOTIFY_IOERROR,
	DEMUX_NOTIFY_RESET_PLAYER,
	DEMUX_NOTIFY_SEEK_FINISH_ASSIST,
    DEMUX_NOTIFY_SEEK_FINISH,
    DEMUX_NOTIFY_CACHE_STAT,
    DEMUX_NOTIFY_BUFFER_START,
    DEMUX_NOTIFY_BUFFER_END,
    DEMUX_NOTIFY_PAUSE_PLAYER,
    DEMUX_NOTIFY_RESUME_PLAYER,

    DEMUX_IOREQ_ACCESS,
    DEMUX_IOREQ_OPEN,
    DEMUX_IOREQ_OPENDIR,
    DEMUX_IOREQ_READDIR,
    DEMUX_IOREQ_CLOSEDIR,
    DEMUX_VIDEO_STREAM_CHANGE,
    DEMUX_AUDIO_STREAM_CHANGE,
    
};

enum EDEMUXERROR
{
    DEMUX_ERROR_NONE        = 0,
    DEMUX_ERROR_UNKNOWN     = -1,
    DEMUX_ERROR_IO          = -2,
    DEMUX_ERROR_USER_CANCEL = -3,
};

enum ECACHEPOLICY
{
    CACHE_POLICY_ADAPTIVE             = 0,
    CACHE_POLICY_QUICK                = 1,
    CACHE_POLICY_SMOOTH               = 2,
    CACHE_POLICY_USER_SPECIFIC_PARAMS = 3,
};

typedef int (*DemuxCallback)(void* pUserData, int eMessageId, void* param);

DemuxComp* DemuxCompCreate(void);

void DemuxCompDestroy(DemuxComp* d);

void DemuxCompClear(DemuxComp* d);  //* clear the data source, like just created.

#if (CONFIG_OS_VERSION >= OPTION_OS_VERSION_ANDROID_5_0)
int DemuxCompSetUrlSource(DemuxComp* d, void* pHTTPServer, const char* pUrl, const KeyedVector<String8, String8>* pHeaders);
#else
int DemuxCompSetUrlSource(DemuxComp* d, const char* pUrl, const KeyedVector<String8, String8>* pHeaders);

#endif

int DemuxCompSetFdSource(DemuxComp* d, int fd, int64_t nOffset, int64_t nLength);

int DemuxCompSetStreamSource(DemuxComp* d, const char* pUri);

int DemuxCompSetPlayer(DemuxComp* d, Player* player);

int DemuxCompSetCallback(DemuxComp* d, DemuxCallback callback, void* pUserData);

int DemuxCompPrepareAsync(DemuxComp* d);

int DemuxCompCancelPrepare(DemuxComp* d);   //* should call back DEMUX_PREPARE_FINISH message.

int DemuxProbeH265RefPictureNumber(char* pDataBuf, int nDataLen);

MediaInfo* DemuxCompGetMediaInfo(DemuxComp* d);

int DemuxCompStart(DemuxComp* d);

int DemuxCompStop(DemuxComp* d);    //* close the data source, must call prepare again to restart.

int DemuxCompPause(DemuxComp* d);   //* no pause status in demux component, return OK immediately.

int DemuxCompGetStatus(DemuxComp* d);

int DemuxCompSeekTo(DemuxComp* d, int nSeekTimeMs);

int DemuxCompCancelSeek(DemuxComp* d);  //* should not call back DEMUX_SEEK_FINISH message.

int DemuxCompNotifyFirstFrameShowed(DemuxComp* d);   //* notify video first frame showed.


int DemuxCompSetCacheStatReportInterval(DemuxComp* d, int ms);

int DemuxCompSetCachePolicy(DemuxComp*          d, 
                            enum ECACHEPOLICY   eCachePolicy, 
                            int                 nStartPlaySize, 
                            int                 nStartPlayTimeMs, 
                            int                 nMaxBufferSize);

int DemuxCompSetSecureBufferCount(DemuxComp* d, void* param);

int DemuxCompSetSecureBuffers(DemuxComp* d,void* param);

int DemuxCompGetTSInfoYUNOS(DemuxComp* d,int eMessageId,void* info,void* param);

int DemuxCompGetCacheSize(DemuxComp* d);

#endif

