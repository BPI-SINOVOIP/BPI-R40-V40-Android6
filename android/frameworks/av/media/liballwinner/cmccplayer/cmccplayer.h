
#ifndef CMCC_PLAYER_H
#define CMCC_PLAYER_H

#include <semaphore.h>
#include <pthread.h>
#include <media/MediaPlayerInterface.h>
#include "config.h"             //* configuration file in "LiBRARY/"
#include "player.h"             //* player library in "LIBRARY/PLAYER/"
#include "mediaInfo.h"
#include "demuxComponent.h"
#include "awMessageQueue.h"
#include "awLogRecorder.h"


#if (CONFIG_OS_VERSION >= OPTION_OS_VERSION_ANDROID_5_0)
#include <media/IMediaHTTPService.h>
#endif

using namespace android;

enum CmccApplicationType
{
    CMCCNORMALPLAY,
    CMCCMIRACAST,
};


class CmccPlayer : public MediaPlayerInterface
{
public:
    CmccPlayer();
    virtual ~CmccPlayer();

    virtual status_t    initCheck();

    virtual status_t    setUID(uid_t nUid);

#if (CONFIG_OS_VERSION >= OPTION_OS_VERSION_ANDROID_5_0)
    virtual status_t    setDataSource(const sp<IMediaHTTPService> &httpService,const char* pUrl, const KeyedVector<String8, String8>* pHeaders);
#else
    virtual status_t    setDataSource(const char* pUrl, const KeyedVector<String8, String8>* pHeaders);
#endif
    virtual status_t    setDataSource(int fd, int64_t nOffset, int64_t nLength);
    virtual status_t    setDataSource(const sp<IStreamSource>& source);

#if CONFIG_OS_VERSION != OPTION_OS_VERSION_ANDROID_4_2
    //* android 4.4 use IGraphicBufferProducer instead of ISurfaceTexture in android 4.2.
    virtual status_t    setVideoSurfaceTexture(const sp<IGraphicBufferProducer>& bufferProducer);
#else
    virtual status_t    setVideoSurfaceTexture(const sp<ISurfaceTexture>& surfaceTexture);
#endif

    virtual status_t    prepare();
    virtual status_t    prepareAsync();
    virtual status_t    start();
    virtual status_t    stop();
    virtual status_t    pause();
    virtual bool        isPlaying();
    virtual status_t    seekTo(int nSeekTimeMs);

    virtual status_t    getCurrentPosition(int* msec);
    virtual status_t    getDuration(int* msec);
    virtual status_t    reset();
    virtual status_t    setLooping(int bLoop);
    
    virtual player_type playerType();   //* return AW_PLAYER
    
    virtual status_t    invoke(const Parcel &request, Parcel *reply);
    virtual void        setAudioSink(const sp<AudioSink>& audioSink);
    virtual status_t    setParameter(int key, const Parcel& request);
    virtual status_t    getParameter(int key, Parcel* reply);

    virtual status_t    getMetadata(const media::Metadata::Filter& ids, Parcel* records);
    
    virtual status_t    callbackProcess(int messageId, void* param);
    virtual status_t    mainThread();
    
    //* this method setSubCharset(const char* charset) is added by allwinner.
    virtual status_t    setSubCharset(const char* charset);
	virtual status_t   	getSubCharset(char *charset);
    virtual status_t    setSubDelay(int nTimeMs);
    virtual int         getSubDelay();
    
private:
    virtual status_t    initializePlayer();
    virtual void        clearMediaInfo();
  
    virtual status_t    updateVideoInfo();

private:
    AwMessageQueue*     mMessageQueue;
    Player*             mPlayer;
    DemuxComp*          mDemux;
    pthread_t           mThreadId;
    int                 mThreadCreated;
    uid_t               mUID;             //* no use.
    
    //* data source.
    char*               mSourceUrl;       //* file path or network stream url.
    CdxStreamT*         mSourceStream;    //* outside streaming source like miracast.
    
    int                 mSourceFd;        //* file descriptor.
    int64_t             mSourceFdOffset;
    int64_t             mSourceFdLength;
    
    //* media information.
    MediaInfo*          mMediaInfo;
    
    //* note whether the sutitle is in text or in bitmap format.
    int                 mIsSubtitleInTextFormat;
    
    //* text codec format of the subtitle, used to transform subtitle text to
    //* utf8 when the subtitle text codec format is unknown.
    char                mDefaultTextFormat[32];
    
    //* whether enable subtitle show.
    int                 mIsSubtitleDisable;
    
    //* file descriptor of .idx file of index+sub subtitle.
    //* we save the .idx file's fd here because application set .idx file and .sub file
    //* seperately, we need to wait for the .sub file's fd, see 
    //* INVOKE_ID_ADD_EXTERNAL_SOURCE_FD command in invoke() method.
    int                 mIndexFileHasBeenSet;
    int                 mIndexFileFdOfIndexSubtitle;

    //* surface.
#if CONFIG_OS_VERSION != OPTION_OS_VERSION_ANDROID_4_2
    //* android 4.4 use IGraphicBufferProducer instead of ISurfaceTexture in android 4.2.
    sp<IGraphicBufferProducer> mGraphicBufferProducer;
#else
    sp<ISurfaceTexture> mSurfaceTexture;
#endif
	sp<ANativeWindow>   mNativeWindow;
	
	//* for status and synchronize control.
	int                 mStatus;
	pthread_mutex_t     mMutexMediaInfo;    //* for media info protection.
	pthread_mutex_t     mMutexStatus;       //* for mStatus protection in start/stop/pause operation and complete/seek finish callback.
	sem_t               mSemSetDataSource;
	sem_t               mSemPrepare;
	sem_t               mSemStart;
	sem_t               mSemStop;
	sem_t               mSemPause;
	sem_t               mSemQuit;
	sem_t               mSemReset;
	sem_t               mSemSeek;
	sem_t               mSemSetSurface;
	sem_t               mSemSetAudioSink;
	sem_t               mSemPrepareFinish;      //* for signal prepare finish, used in prepare().
	
	//* status control.
	int                 mSetDataSourceReply;
	int                 mPrepareReply;
    int                 mStartReply;
    int                 mStopReply;
    int                 mPauseReply;
    int                 mResetReply;
    int                 mSeekReply;
    int                 mSetSurfaceReply;
    int                 mSetAudioSinkReply;
	int                 mPrepareFinishResult;   //* save the prepare result for prepare().
    
	int                 mPrepareSync;   //* synchroized prarare() call, don't call back to user.
	int                 mSeeking;
	int                 mSeekTime;  //* use to check whether seek callback is for current seek operation or previous.
	int                 mSeekSync;  //* internal seek, don't call back to user.
	int                 mLoop;
	int                 mKeepLastFrame;
    int                 mVideoSizeWidth;  //* use to record videoSize which had send to app
    int                 mVideoSizeHeight;

    enum CmccApplicationType mApplicationType;
	char                strApkName[128];

#if (CONFIG_OS_VERSION >= OPTION_OS_VERSION_ANDROID_5_0)
	sp<IMediaHTTPService> mHTTPService;
#endif

    //* record the id of subtitle which is displaying
    //* we set the Nums to 64 .(32 may be not enough)
    unsigned int        mSubtitleDisplayIds[64];
    int                 mSubtitleDisplayIdsUpdateIndex;

    //* save the currentSelectTrackIndex;
    int                 mCurrentSelectTrackIndex;
	int                 mRawOccupyFlag;


	int                 mLivemode;
	int                 mPauseLivemode;
	bool                mbIsDiagnose;
	int64_t             mPauseTimeStamp;    //us
	int64_t             mShiftTimeStamp;    //us
	int                 mDisplayRatio;
 
	AwLogRecorder*      mLogRecorder;
	char                mUri[1024];
	int                 mFirstStart;

	int                 mSeekTobug;

	// the cmcc player should change pause state when buffer start, to fix getposition bug
	int                 mDemuxNotifyPause;  
	int64_t             mDemuxPauseTimeStamp;

    //*
    int64_t             mPlayTimeMs;
    int64_t             mBufferTimeMs;
    CmccPlayer(const CmccPlayer&);
    CmccPlayer &operator=(const CmccPlayer&);
};


#endif  // AWPLAYER
