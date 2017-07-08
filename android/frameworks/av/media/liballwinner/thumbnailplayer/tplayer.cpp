#include "tplayer.h"
#include <media/Metadata.h>
#include <media/stagefright/MediaExtractor.h>
#include <media/stagefright/MediaErrors.h>

#if (CONFIG_OS_VERSION == OPTION_OS_VERSION_ANDROID_4_2)
#include <gui/ISurfaceTexture.h>
#include <gui/SurfaceTextureClient.h>
#endif

#include <ui/GraphicBufferMapper.h>

#include "log.h"
#include "avtimer.h"


namespace android {


static void* MainThread(void* arg);


TPlayer::TPlayer()
{
	mDecoder      = NULL;
    mStatus       = PLAYER_STATE_UNKOWN;
    mNativeWindow = NULL;
    mFpStream     = NULL;
    mFd           = -1;
    mThreadId     = 0;
    mStopFlag     = 0;
    mURL          = NULL;
    mFrameRate    = 30000;
    mFrameCount   = 0;

	pthread_mutex_init(&mBufferMutex, NULL);
}


TPlayer::~TPlayer()
{
	void* err;

	logv("~~~~~~~~~~~~~~~~~~~TPlayer");
	if(mThreadId != 0)
	{
		pthread_mutex_lock(&mBufferMutex);
		mStopFlag = 1;
		pthread_mutex_unlock(&mBufferMutex);
		pthread_join(mThreadId, (void**)&err);
		mThreadId = 0;
	}

	if(mDecoder != NULL)
		DestroyVideoDecoder(mDecoder);

	if(mFpStream != NULL)
	{
		fclose(mFpStream);
		mFpStream = NULL;
	}

	if(mFd != -1)
	{
	    close(mFd);
	    mFd = -1;
	}

	if(mURL)
		free(mURL);

	pthread_mutex_destroy(&mBufferMutex);
}


status_t TPlayer::initCheck()
{
    return OK;
}


status_t TPlayer::setUID(uid_t uid)
{
	CEDARX_UNUSE(uid);
    return OK;
}


#if (CONFIG_OS_VERSION >= OPTION_OS_VERSION_ANDROID_5_0)
status_t TPlayer::setDataSource(const sp<IMediaHTTPService> &httpService,const char* url, const KeyedVector<String8, String8>* headers)
{
	CEDARX_UNUSE(httpService);
	CEDARX_UNUSE(headers);

	if(mURL)
		free(mURL);

	mURL = (char*)malloc(strlen(url) + 1);
	if(mURL == NULL)
		return NO_MEMORY;

	strcpy(mURL, url);

	return OK;
}

#else

status_t TPlayer::setDataSource(const char* url, const KeyedVector<String8, String8>* headers)
{
	CEDARX_UNUSE(headers);
	if(mURL)
		free(mURL);

	mURL = (char*)malloc(strlen(url) + 1);
	if(mURL == NULL)
		return NO_MEMORY;

	strcpy(mURL, url);

	return OK;
}

#endif

// Warning: The filedescriptor passed into this method will only be valid until
// the method returns, if you want to keep it, dup it!
status_t TPlayer::setDataSource(int fd, int64_t offset, int64_t length)
{
	CEDARX_UNUSE(offset);
	CEDARX_UNUSE(length);

    mFd = dup(fd);
    if(mFd >= 0)
        return OK;
    else
        return ERROR_UNSUPPORTED;
}

status_t TPlayer::setDataSource(const sp<IStreamSource> &source)
{
	CEDARX_UNUSE(source);
	return ERROR_UNSUPPORTED;
}

status_t TPlayer::setParameter(int key, const Parcel &request)
{
	CEDARX_UNUSE(key);
	CEDARX_UNUSE(request);
	return OK;
}

status_t TPlayer::getParameter(int key, Parcel *reply)
{
	CEDARX_UNUSE(key);
	CEDARX_UNUSE(reply);
	return ERROR_UNSUPPORTED;
}


status_t TPlayer::setVideoSurface(const sp<Surface> &surface)
{
	CEDARX_UNUSE(surface);
//	mNativeWindow = surface.get();
	return OK;
}


#if (CONFIG_OS_VERSION == OPTION_OS_VERSION_ANDROID_4_2)
status_t TPlayer::setVideoSurfaceTexture(const sp<ISurfaceTexture> &surfaceTexture)
{
	mNativeWindow = new SurfaceTextureClient(surfaceTexture);	return OK;
}

#else

status_t TPlayer::setVideoSurfaceTexture(const sp<IGraphicBufferProducer>& bufferProducer)
{
	mNativeWindow = new Surface(bufferProducer, true);
	return OK;
}
#endif

status_t TPlayer::prepare()
{
	mStatus = PLAYER_STATE_PREPARED;
	return OK;
}

status_t TPlayer::prepareAsync()
{
	if(prepare() == OK)
		sendEvent(MEDIA_PREPARED, 0, 0);
	else
		sendEvent(MEDIA_ERROR, MEDIA_ERROR_UNKNOWN, -1);

	return OK;
}

status_t TPlayer::start()
{
	int err;

	if(mThreadId != 0)
	{
		mStatus = PLAYER_STATE_PLAYING;
		return OK;
	}

	mStopFlag = 0;

	mStatus = PLAYER_STATE_PLAYING;
	err = pthread_create(&mThreadId, NULL, MainThread, this);
	if(err != 0 || mThreadId == 0)
	{
		mStatus = PLAYER_STATE_UNKOWN;
		return UNKNOWN_ERROR;
	}

	return OK;
}

status_t TPlayer::stop()
{
	int err;


	if(mThreadId != 0)
	{
		pthread_mutex_lock(&mBufferMutex);
		mStopFlag = 1;
		pthread_mutex_unlock(&mBufferMutex);
	}

	mStatus = PLAYER_STATE_UNKOWN;
	mFrameCount = 0;

	sendEvent(MEDIA_PLAYBACK_COMPLETE, 0, 0);

	return OK;
}

status_t TPlayer::pause()
{

	mStatus = PLAYER_STATE_SUSPEND;
	return OK;
}


bool TPlayer::isPlaying()
{
	return (mStatus == PLAYER_STATE_PLAYING);
}


status_t TPlayer::seekTo(int msec)
{
	CEDARX_UNUSE(msec);

	sendEvent(MEDIA_SEEK_COMPLETE, 0, 0);
	return ERROR_UNSUPPORTED;
}


status_t TPlayer::getCurrentPosition(int *msec)
{
	int frameDuration;
	if(mFrameRate == 0)
		frameDuration = 1000*1000/3000;
	else
		frameDuration = 1000*1000/mFrameRate;

	*msec = mFrameCount * frameDuration / 1000;
	return OK;
}


status_t TPlayer::getDuration(int *msec)
{
	*msec = 5000;
	return OK;
}


status_t TPlayer::reset()
{
	return stop();
}


status_t TPlayer::setLooping(int loop)
{
	CEDARX_UNUSE(loop);
	return OK;
}


player_type TPlayer::playerType()
{
    return THUMBNAIL_PLAYER;
}

status_t TPlayer::setScreen(int screen)
{
	CEDARX_UNUSE(screen);
	return OK;
}

int TPlayer::getMeidaPlayerState()
{
	return mStatus;
}

status_t TPlayer::invoke(const Parcel &request, Parcel *reply)
{
	CEDARX_UNUSE(request);
	CEDARX_UNUSE(reply);

   // return INVALID_OPERATION;
        return OK;
}

void TPlayer::setAudioSink(const sp<AudioSink> &audioSink)
{
	CEDARX_UNUSE(audioSink);
	return;
}

status_t TPlayer::getMetadata(const media::Metadata::Filter& ids, Parcel *records)
{
    using media::Metadata;
    Metadata metadata(records);
    metadata.appendBool(Metadata::kPauseAvailable, MediaExtractor::CAN_PAUSE);

	CEDARX_UNUSE(ids);
    return OK;
}

int TPlayer::initDecoder()
{
	int err;
	int readBytes;
	char RIFF[4];
	VConfig             VideoConf;
	VideoStreamInfo     VideoInfo;

    if(mFd < 0)
    {
        mFpStream = fopen(mURL, "rb");
        if(mFpStream == NULL)
            return UNKNOWN_ERROR;
    }

    if(mFd < 0)
        readBytes = fread(RIFF, 1, 4, mFpStream);    //* read the 'thum' RIFF
    else
        readBytes = read(mFd, RIFF, 4);    //* read the 'thum' RIFF

    if(mFd < 0)
        readBytes = fread(&mFrameRate, 1, sizeof(int), mFpStream);
    else
        readBytes = read(mFd, &mFrameRate, sizeof(int));

	if(readBytes != sizeof(int))
	{
	    if(mFd < 0)
	    {
	        fclose(mFpStream);
	        mFpStream = NULL;
	    }
	    else
	    {
	        close(mFd);
	        mFd = -1;
	    }
		return UNKNOWN_ERROR;
	}

	if(mFrameRate < 1000)
		mFrameRate *= 1000;

	if(mFrameRate > 30000 || mFrameRate == 0)
		mFrameRate = 30000;


	if(mDecoder == NULL)
	{
		mDecoder = CreateVideoDecoder();

		memset(&VideoConf, 0, sizeof(VConfig));
		memset(&VideoInfo, 0, sizeof(VideoStreamInfo));
		
        //* all decoder support YV12 format.
        VideoConf.eOutputPixelFormat  = PIXEL_FORMAT_YV12;
        //* never decode two picture when decoding a thumbnail picture.
        VideoConf.bDisable3D          = 1;

		VideoConf.nVbvBufferSize 	  = 128*1024;
        VideoConf.nAlignStride        = 16;//* set align stride to 16 as defualt

        VideoConf.nDeInterlaceHoldingFrameBufferNum = 0; //we not use deinterlace so set to 0
        VideoConf.nDisplayHoldingFrameBufferNum     = 0; //gpu and decoder not share buffer
        VideoConf.nRotateHoldingFrameBufferNum      = NUM_OF_PICTURES_KEEPPED_BY_ROTATE;
        VideoConf.nDecodeSmoothFrameBufferNum       = NUM_OF_PICTURES_FOR_EXTRA_SMOOTH;

		VideoInfo.eCodecFormat = VIDEO_CODEC_FORMAT_H264;
		VideoInfo.nWidth = 0;
		VideoInfo.nHeight = 0;
		VideoInfo.nFrameRate = mFrameRate;
		VideoInfo.nFrameDuration = 1000*1000/mFrameRate;
		VideoInfo.nAspectRatio = 1000;
		VideoInfo.bIs3DStream = 0;
		VideoInfo.nCodecSpecificDataLen = 0;
		VideoInfo.pCodecSpecificData = NULL;

		//* initialize the decoder.
		if(InitializeVideoDecoder(mDecoder, &VideoInfo, &VideoConf) != 0)
		{
			loge("initialize video decoder fail.");
			DestroyVideoDecoder(mDecoder);
			mDecoder = NULL;
		}
	}

	if(mDecoder == NULL)
	{
        if(mFd < 0)
        {
            fclose(mFpStream);
            mFpStream = NULL;
        }
        else
        {
            close(mFd);
            mFd = -1;
        }
		return UNKNOWN_ERROR;
	}

    mFrameCount   = 0;

    return 0;
}

status_t TPlayer::generalInterface(int cmd, int int1, int int2, int int3, void *p)
{
	CEDARX_UNUSE(cmd);
	CEDARX_UNUSE(int1);
	CEDARX_UNUSE(int2);
	CEDARX_UNUSE(int3);
	CEDARX_UNUSE(p);

    if(cmd == MEDIAPLAYER_CMD_QUERY_HWLAYER_RENDER)
    {
            *((int*)p) = 0;
    }

    return OK;
}

static void* MainThread(void* arg)
{
	int                  frameLen;
	int                  readBytes;
	int                  ret;
	int                  widthAlign;
	int                  heightAlign;
	void*                pGraphicBuf;
	void*                dst;
	int                  firstFrame;
	int                  decodeResult;
	TPlayer*             t;
    ANativeWindowBuffer* winBuf;
	VideoPicture*        picture;
	int64_t              frameDuration;
	int64_t              pts;
	AvTimer              timer;

	firstFrame = 1;

	t = (TPlayer*)arg;

	if(t->initDecoder() != 0)
		return (void*)0;

	if(t->mFrameRate == 0)
		frameDuration = 40000;
	else
		frameDuration = 1000*1000*1000/t->mFrameRate;

	//logd("mFrameRate = %d", t->mFrameRate);

	while(1)
	{
		if(t->mStopFlag)
			break;

		if(t->mStatus == PLAYER_STATE_PLAYING)
		{
        	//* 1. check if there is a picture to output.
        	if(ValidPictureNum(t->mDecoder,0))
        	{
        		logv("picture ready");

                //* check if the picture size changed.
                picture = NextPictureInfo(t->mDecoder,0);

            	heightAlign = (picture->nHeight + 7) & (~7);
				widthAlign  = (picture->nWidth + 15) & (~15);

				if(firstFrame == 1)
				{
					pthread_mutex_lock(&t->mBufferMutex);
					if(t->mStopFlag)
					{
						pthread_mutex_unlock(&t->mBufferMutex);
						break;
					}
                    
                    unsigned int nNativeWindowUsage = 0;
                    nNativeWindowUsage  = GRALLOC_USAGE_SW_READ_NEVER;
                    nNativeWindowUsage |= GRALLOC_USAGE_SW_WRITE_OFTEN;
                    nNativeWindowUsage |= GRALLOC_USAGE_HW_TEXTURE;
                    nNativeWindowUsage |= GRALLOC_USAGE_EXTERNAL_DISP;
                    unsigned int nScaleMode = NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW;
                    
				    native_window_set_usage(t->mNativeWindow.get(),nNativeWindowUsage);
				    native_window_set_scaling_mode(t->mNativeWindow.get(), nScaleMode);
				    native_window_set_buffers_geometry(t->mNativeWindow.get(), widthAlign, heightAlign, HAL_PIXEL_FORMAT_YV12);
				    native_window_set_buffers_transform(t->mNativeWindow.get(), 0);
					pthread_mutex_unlock(&t->mBufferMutex);

					firstFrame = 0;

					timer.SetTime(0);
					timer.SetSpeed(1000);
					timer.Start();
				}

				pthread_mutex_lock(&t->mBufferMutex);
				if(t->mStopFlag)
				{
					pthread_mutex_unlock(&t->mBufferMutex);
					break;
				}

				//* output a buffer.
				//ret = t->mNativeWindow->dequeueBuffer(t->mNativeWindow.get(), &winBuf);

#if (CONFIG_OS_VERSION == OPTION_OS_VERSION_ANDROID_4_2)

                ret = t->mNativeWindow->dequeueBuffer_DEPRECATED(t->mNativeWindow.get(), &winBuf);
#else
                // dequeue a buffer
                int fenceFd = -1;
				if ((ret = t->mNativeWindow->dequeueBuffer(t->mNativeWindow.get(), &winBuf, &fenceFd)) != 0)
				{
				    logd("Surface::dequeueBuffer returned error %d", ret);
				}

				if(ret != 0)
            	{
					pthread_mutex_unlock(&t->mBufferMutex);
            		usleep(5*1000);
					continue;
            	}
#endif


#if (CONFIG_OS_VERSION == OPTION_OS_VERSION_ANDROID_4_2)
                t->mNativeWindow->lockBuffer_DEPRECATED(t->mNativeWindow.get(), winBuf);
#else
				// wait for the buffer
                sp<Fence> fence(new Fence(fenceFd));
		        if ((ret = fence->wait(Fence::TIMEOUT_NEVER)) != 0) {
			        t->mNativeWindow->cancelBuffer(t->mNativeWindow.get(), winBuf, fenceFd);
			        logd("fence->wait returned error %d", ret);
		        }
				if(ret != 0)
            	{
					pthread_mutex_unlock(&t->mBufferMutex);
            		usleep(5*1000);
					continue;
            	}
#endif

			    //t->mNativeWindow->lockBuffer(t->mNativeWindow.get(), winBuf);

			    {
                	GraphicBufferMapper &GraphicMapper = GraphicBufferMapper::get();
                	Rect bounds(picture->nWidth, picture->nHeight);
				    GraphicMapper.lock(winBuf->handle, GRALLOC_USAGE_SW_WRITE_OFTEN, bounds, &dst);
					picture = RequestPicture(t->mDecoder, 0);
	            	memcpy(dst, (void*)picture->pData0, picture->nWidth*picture->nHeight);
					memcpy((char*)dst + picture->nWidth*picture->nHeight, (void*)picture->pData1, picture->nWidth*picture->nHeight/2);
					ReturnPicture(t->mDecoder, picture);
    			    GraphicMapper.unlock(winBuf->handle);
			    }

			    pthread_mutex_unlock(&t->mBufferMutex);

			    {
			    	pts = t->mFrameCount * frameDuration;
			    	if(pts > (timer.GetTime() + 1000))
			    		usleep(pts - timer.GetTime());
			    	else if(pts + 100000 < timer.GetTime())
			    		timer.SetTime(pts);
			    }

				pthread_mutex_lock(&t->mBufferMutex);
				if(t->mStopFlag)
				{
					pthread_mutex_unlock(&t->mBufferMutex);
					break;
				}

#if (CONFIG_OS_VERSION == OPTION_OS_VERSION_ANDROID_4_2)
			    t->mNativeWindow->queueBuffer_DEPRECATED(t->mNativeWindow.get(), winBuf);
#else
			    t->mNativeWindow->queueBuffer(t->mNativeWindow.get(), winBuf, -1);
#endif
			    pthread_mutex_unlock(&t->mBufferMutex);

			    t->mFrameCount++;
                continue;
        	}
        	else
        	{
        		while(1)
        		{
					decodeResult = DecodeVideoStream(t->mDecoder,0,0,0,0);
					
                    logv("(f:%s l:%d) TPlayer", __FUNCTION__, __LINE__); //when hdmi plugin and plugout, thumbplayer will ...

        			if(decodeResult == VDECODE_RESULT_KEYFRAME_DECODED || decodeResult == VDECODE_RESULT_FRAME_DECODED)
        			{
        				break;
        			}
        			else if(decodeResult == VDECODE_RESULT_NO_FRAME_BUFFER)
        			{
        				break;
        			}
        			else if(decodeResult == VDECODE_RESULT_NO_BITSTREAM)
        			{
                    	char* pBuf0;
                    	char* pBuf1;
                    	int size0;
                    	int size1;
                    	int require_size;
                    	char* pData;
                    	VideoStreamDataInfo dataInfo;

                    	if(t->mFd < 0)
                    	{
                            if(fread(&require_size, 1, sizeof(int), t->mFpStream) != sizeof(int))
                            {
                                fseek(t->mFpStream, 4, SEEK_SET);
                                fread(&t->mFrameRate, 1, sizeof(int), t->mFpStream);
                                t->mFrameCount = 0;
                                continue;
                            }
                    	}
                    	else
                    	{
                            if(read(t->mFd, &require_size, sizeof(int)) != sizeof(int))
                            {
                                lseek(t->mFd, 4, SEEK_SET);
                                read(t->mFd, &t->mFrameRate, sizeof(int));
                                t->mFrameCount = 0;
                                continue;
                            }
                    	}

						ret = RequestVideoStreamBuffer(t->mDecoder, require_size, &pBuf0, &size0, &pBuf1, &size1,0);
                        if(ret != 0 || (size0 + size1 < require_size))
                        {
							ResetVideoDecoder(t->mDecoder);

            				if(t->mFd < 0)
            				{
            				    fseek(t->mFpStream, 4, SEEK_SET);
            				    fread(&t->mFrameRate, 1, sizeof(int), t->mFpStream);
            				}
            				else
            				{
                                lseek(t->mFd, 4, SEEK_SET);
                                read(t->mFd, &t->mFrameRate, sizeof(int));
            				}
            				t->mFrameCount = 0;
            				continue;
                        }

						memset(&dataInfo, 0, sizeof(VideoStreamDataInfo));
						dataInfo.nLength	  = require_size;
						dataInfo.bIsFirstPart = 1;
						dataInfo.bIsLastPart  = 1;
						dataInfo.pData		  = pBuf0;
						dataInfo.nPts         = -1;
						dataInfo.nPcr		  = -1;

                        if(require_size <= size0)
                        {
                            if(t->mFd < 0)
                                fread(pBuf0, 1, require_size, t->mFpStream);
                            else
                                read(t->mFd, pBuf0, require_size);
                        }
                        else
                        {
                            if(t->mFd < 0)
                            {
                                fread(pBuf0, 1, size0, t->mFpStream);
                                fread(pBuf1, 1, require_size - size0, t->mFpStream);
                            }
                            else
                            {
                                read(t->mFd, pBuf0, size0);
                                read(t->mFd, pBuf1, require_size - size0);
                            }
                        }

						SubmitVideoStreamData(t->mDecoder, &dataInfo, 0);
                        break;
        			}
        			else if(decodeResult < 0)
        			{
                        break;
        			}
        		}
        	}

		}
		else if(t->mStatus == PLAYER_STATE_PAUSE)
		{
			usleep(10*1000);
		}
	}

	if(t->mDecoder != NULL)
	{
		DestroyVideoDecoder(t->mDecoder);
		t->mDecoder = NULL;
	}

	return (void*)0;
}


}  // namespace android











