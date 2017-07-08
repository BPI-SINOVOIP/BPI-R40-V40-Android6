/*
**
** Copyright 2012, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

//#define LOG_NDEBUG 0
#define LOG_TAG "MediaPlayerFactory"
#include <utils/Log.h>

#include <cutils/properties.h>
#include <media/IMediaPlayer.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/FileSource.h>
#include <media/stagefright/foundation/ADebug.h>
#include <utils/Errors.h>
#include <utils/misc.h>
#include <../libstagefright/include/WVMExtractor.h>

#include "MediaPlayerFactory.h"

#include "TestPlayerStub.h"
#include "StagefrightPlayer.h"
#include "nuplayer/NuPlayerDriver.h"


#include "tplayer.h"
#include "awplayer.h"
#include <binder/IPCThreadState.h>
#include <fcntl.h>
#include <libsonivox/eas.h>
#include "media/MidiIoWrapper.h"
#include "SimpleMediaFormatProbe.h"

namespace android {

#define MAKE_AW_PLAYER_VALID (1)

Mutex MediaPlayerFactory::sLock;
MediaPlayerFactory::tFactoryMap MediaPlayerFactory::sFactoryMap;
bool MediaPlayerFactory::sInitComplete = false;

// TODO: Temp hack until we can register players
typedef struct {
    const char *extension;
    const player_type playertype;
} extmap;

extmap FILE_EXTS [] =  {
		{".ogg",  NU_PLAYER},
		{".mp3",  NU_PLAYER},
		{".wav",  NU_PLAYER},
		{".amr",  NU_PLAYER},
		{".flac", NU_PLAYER},
		{".m4a",  NU_PLAYER},
		{".m4r",  NU_PLAYER},
		{".out",  AW_PLAYER},
		//{".3gp",  STAGEFRIGHT_PLAYER},
        //{".aac",  STAGEFRIGHT_PLAYER},
            
        {".mid",  NU_PLAYER},
        {".midi", NU_PLAYER},
        {".smf",  NU_PLAYER},
        {".xmf",  NU_PLAYER},
        {".mxmf", NU_PLAYER},
        {".imy",  NU_PLAYER},
        {".rtttl",NU_PLAYER},
        {".rtx",  NU_PLAYER},
        {".ota",  NU_PLAYER},
            
        {".ape", AW_PLAYER},
        {".ac3", AW_PLAYER},
        {".dts", AW_PLAYER},
        {".wma", AW_PLAYER},
        {".aac", AW_PLAYER},
        {".mp2", AW_PLAYER},
        {".mp1", AW_PLAYER},
        {".athumb", THUMBNAIL_PLAYER},
};

#define GET_CALLING_PID	(IPCThreadState::self()->getCallingPid())
void getCallingProcessName(char *name)
{
	char proc_node[128];

	if (name == 0)
	{
		loge("error in params");
		return;
	}

	memset(proc_node, 0, sizeof(proc_node));
	sprintf(proc_node, "/proc/%d/cmdline", GET_CALLING_PID);
	int fp = ::open(proc_node, O_RDONLY);
	if (fp > 0)
	{
		memset(name, 0, 128);
		::read(fp, name, 128);
		::close(fp);
		fp = 0;
		logd("Calling process is: %s", name);
	}
	else
	{
		loge("Obtain calling process failed");
	}
}

player_type getPlayerType_l(int fd, int64_t offset, int64_t length)
{
	int r_size;
    char buf[4096];
	int file_format;
	char  mCallingProcess[256]={0};
    lseek(fd, offset, SEEK_SET);
    r_size = read(fd, buf, sizeof(buf));
    lseek(fd, offset, SEEK_SET);

    long ident = *((long*)buf);

    // Oggs file return nuplayer
    if (ident == 0x5367674f) {
    	// 'OggS'
    	return NU_PLAYER;
    } else if(ident == 0x6d756874) {
    	return THUMBNAIL_PLAYER;
    }

    // MIDI file use nuplayer
    EAS_DATA_HANDLE easdata;
    if (EAS_Init(&easdata) == EAS_SUCCESS) {
		sp<MidiIoWrapper> mIoWrapper = new MidiIoWrapper(fd,offset,length);
        EAS_HANDLE  eashandle;
        if (EAS_OpenFile(easdata, mIoWrapper->getLocator(), &eashandle) == EAS_SUCCESS) {
            EAS_CloseFile(easdata, eashandle);
            EAS_Shutdown(easdata);
			mIoWrapper.clear();
            return NU_PLAYER;
        }
		mIoWrapper.clear();
        EAS_Shutdown(easdata);
    }

	getCallingProcessName(mCallingProcess);

	if(strcmp(mCallingProcess, "com.android.cts.security") == 0)
		return NU_PLAYER;

	if((strcmp(mCallingProcess, "com.android.cts.media") == 0) || (strcmp(mCallingProcess, "android.process.media") == 0))
	{
			file_format = audio_format_detect((unsigned char*)buf, r_size, fd, offset);

			if (file_format == MEDIA_FORMAT_MP3 || file_format == MEDIA_FORMAT_3GP || file_format == MEDIA_FORMAT_M4A)
			{
				return NU_PLAYER;
			}
	}

#if 1
    file_format = audio_format_detect((unsigned char*)buf, r_size, fd, offset);

    if (file_format == MEDIA_FORMAT_MP3 )
    {
	    return NU_PLAYER;
    }
#endif

    return AW_PLAYER;
}

player_type getPlayerType_l(const char* url)
{
	char *strpos;
	char  mCallingProcess[256]={0};

    if (TestPlayerStub::canBeUsed(url)) {
            return TEST_PLAYER;
        }

    if (!strncasecmp("http://", url, 7) || !strncasecmp("https://", url, 8)) {
		getCallingProcessName(mCallingProcess);
		if((strpos = strrchr(url,'.')) != NULL){
			if ((strcmp(mCallingProcess, "com.android.cts.media") == 0) && !strncasecmp(strpos, ".m3u8", 5)){
				return NU_PLAYER;
			}
		}

		if((strcmp(mCallingProcess, "android.netsecpolicy.usescleartext.false.cts") == 0))
			return NU_PLAYER;

		if((strpos = strrchr(url,'?')) != NULL) {
			for (int i = 0; i < NELEM(FILE_EXTS); ++i) {
					int len = strlen(FILE_EXTS[i].extension);
						if (!strncasecmp(strpos -len, FILE_EXTS[i].extension, len)) {
                            if(i==2)//net wav
							{
								return AW_PLAYER;
							}
							else
							{
								return FILE_EXTS[i].playertype;
							}
						}
				}
		}
	}

	if (!strncmp("data:;base64", url, strlen("data:;base64"))){
		return NU_PLAYER;
	}

    // use MidiFile for MIDI extensions
    int lenURL = strlen(url);
    int len;
    int start;
    for (int i = 0; i < NELEM(FILE_EXTS); ++i) {
        len = strlen(FILE_EXTS[i].extension);
        start = lenURL - len;
        if (start > 0) {
            if (!strncasecmp(url + start, FILE_EXTS[i].extension, len)) {
                return FILE_EXTS[i].playertype;
            }
        }
    }
    
    return AW_PLAYER;
}

status_t MediaPlayerFactory::registerFactory_l(IFactory* factory,
                                               player_type type) {
    if (NULL == factory) {
        ALOGE("Failed to register MediaPlayerFactory of type %d, factory is"
              " NULL.", type);
        return BAD_VALUE;
    }

    if (sFactoryMap.indexOfKey(type) >= 0) {
        ALOGE("Failed to register MediaPlayerFactory of type %d, type is"
              " already registered.", type);
        return ALREADY_EXISTS;
    }

    if (sFactoryMap.add(type, factory) < 0) {
        ALOGE("Failed to register MediaPlayerFactory of type %d, failed to add"
              " to map.", type);
        return UNKNOWN_ERROR;
    }

    return OK;
}

static player_type getDefaultPlayerType() {
    char value[PROPERTY_VALUE_MAX];
    if (property_get("media.stagefright.use-awesome", value, NULL)
            && (!strcmp("1", value) || !strcasecmp("true", value))) {
        return STAGEFRIGHT_PLAYER;
    }

    return NU_PLAYER;
}

status_t MediaPlayerFactory::registerFactory(IFactory* factory,
                                             player_type type) {
    Mutex::Autolock lock_(&sLock);
    return registerFactory_l(factory, type);
}

void MediaPlayerFactory::unregisterFactory(player_type type) {
    Mutex::Autolock lock_(&sLock);
    sFactoryMap.removeItem(type);
}

#define GET_PLAYER_TYPE_IMPL(a...)                      \
    Mutex::Autolock lock_(&sLock);                      \
                                                        \
    player_type ret = STAGEFRIGHT_PLAYER;               \
    float bestScore = 0.0;                              \
                                                        \
    for (size_t i = 0; i < sFactoryMap.size(); ++i) {   \
                                                        \
        IFactory* v = sFactoryMap.valueAt(i);           \
        float thisScore;                                \
        CHECK(v != NULL);                               \
        thisScore = v->scoreFactory(a, bestScore);      \
        if (thisScore > bestScore) {                    \
            ret = sFactoryMap.keyAt(i);                 \
            bestScore = thisScore;                      \
        }                                               \
    }                                                   \
                                                        \
    if (0.0 == bestScore) {                             \
        ret = getDefaultPlayerType();                   \
    }                                                   \
                                                        \
    return ret;

player_type MediaPlayerFactory::getPlayerType(const sp<IMediaPlayer>& client,
                                              const char* url) {
#if(MAKE_AW_PLAYER_VALID)
	return android::getPlayerType_l(url);
#else
    GET_PLAYER_TYPE_IMPL(client, url);
#endif

}

player_type MediaPlayerFactory::getPlayerType(const sp<IMediaPlayer>& client,
                                              int fd,
                                              int64_t offset,
                                              int64_t length) {
#if(MAKE_AW_PLAYER_VALID)
	return android::getPlayerType_l(fd,offset,length);
#else
    GET_PLAYER_TYPE_IMPL(client, fd, offset, length);
#endif
}

player_type MediaPlayerFactory::getPlayerType(const sp<IMediaPlayer>& client,
                                              const sp<IStreamSource> &source) {
    GET_PLAYER_TYPE_IMPL(client, source);
}

player_type MediaPlayerFactory::getPlayerType(const sp<IMediaPlayer>& client,
                                              const sp<DataSource> &source) {
    GET_PLAYER_TYPE_IMPL(client, source);
}

#undef GET_PLAYER_TYPE_IMPL

sp<MediaPlayerBase> MediaPlayerFactory::createPlayer(
        player_type playerType,
        void* cookie,
        notify_callback_f notifyFunc,
        pid_t pid) {
    sp<MediaPlayerBase> p;
    IFactory* factory;
    status_t init_result;
    Mutex::Autolock lock_(&sLock);

    if (sFactoryMap.indexOfKey(playerType) < 0) {
        ALOGE("Failed to create player object of type %d, no registered"
              " factory", playerType);
        return p;
    }

    factory = sFactoryMap.valueFor(playerType);
    CHECK(NULL != factory);
    p = factory->createPlayer(pid);

    if (p == NULL) {
        ALOGE("Failed to create player object of type %d, create failed",
               playerType);
        return p;
    }

    init_result = p->initCheck();
    if (init_result == NO_ERROR) {
        p->setNotifyCallback(cookie, notifyFunc);
    } else {
        ALOGE("Failed to create player object of type %d, initCheck failed"
              " (res = %d)", playerType, init_result);
        p.clear();
    }

    return p;
}

/*****************************************************************************
 *                                                                           *
 *                     Built-In Factory Implementations                      *
 *                                                                           *
 *****************************************************************************/

class StagefrightPlayerFactory :
    public MediaPlayerFactory::IFactory {
  public:
    virtual float scoreFactory(const sp<IMediaPlayer>& /*client*/,
                               int fd,
                               int64_t offset,
                               int64_t length,
                               float /*curScore*/) {
        if (legacyDrm()) {
            sp<DataSource> source = new FileSource(dup(fd), offset, length);
            String8 mimeType;
            float confidence;
            if (SniffWVM(source, &mimeType, &confidence, NULL /* format */)) {
                return 1.0;
            }
        }

        if (getDefaultPlayerType() == STAGEFRIGHT_PLAYER) {
            char buf[20];
            lseek(fd, offset, SEEK_SET);
            read(fd, buf, sizeof(buf));
            lseek(fd, offset, SEEK_SET);

            uint32_t ident = *((uint32_t*)buf);

            // Ogg vorbis?
            if (ident == 0x5367674f) // 'OggS'
                return 1.0;
        }

        return 0.0;
    }

    virtual float scoreFactory(const sp<IMediaPlayer>& /*client*/,
                               const char* url,
                               float /*curScore*/) {
        if (legacyDrm() && !strncasecmp("widevine://", url, 11)) {
            return 1.0;
        }
        return 0.0;
    }

    virtual sp<MediaPlayerBase> createPlayer(pid_t /* pid */) {
        ALOGV(" create StagefrightPlayer");
        return new StagefrightPlayer();
    }
  private:
    bool legacyDrm() {
        char value[PROPERTY_VALUE_MAX];
        if (property_get("persist.sys.media.legacy-drm", value, NULL)
                && (!strcmp("1", value) || !strcasecmp("true", value))) {
            return true;
        }
        return false;
    }
};

class NuPlayerFactory : public MediaPlayerFactory::IFactory {
  public:
    virtual float scoreFactory(const sp<IMediaPlayer>& /*client*/,
                               const char* url,
                               float curScore) {
        static const float kOurScore = 0.8;

        if (kOurScore <= curScore)
            return 0.0;

        if (!strncasecmp("http://", url, 7)
                || !strncasecmp("https://", url, 8)
                || !strncasecmp("file://", url, 7)) {
            size_t len = strlen(url);
            if (len >= 5 && !strcasecmp(".m3u8", &url[len - 5])) {
                return kOurScore;
            }

            if (strstr(url,"m3u8")) {
                return kOurScore;
            }

            if ((len >= 4 && !strcasecmp(".sdp", &url[len - 4])) || strstr(url, ".sdp?")) {
                return kOurScore;
            }
        }

        if (!strncasecmp("rtsp://", url, 7)) {
            return kOurScore;
        }

        return 0.0;
    }

    virtual float scoreFactory(const sp<IMediaPlayer>& /*client*/,
                               const sp<IStreamSource>& /*source*/,
                               float /*curScore*/) {
        return 1.0;
    }

    virtual float scoreFactory(const sp<IMediaPlayer>& /*client*/,
                               const sp<DataSource>& /*source*/,
                               float /*curScore*/) {
        // Only NuPlayer supports setting a DataSource source directly.
        return 1.0;
    }

    virtual sp<MediaPlayerBase> createPlayer(pid_t pid) {
        ALOGV(" create NuPlayer");
        return new NuPlayerDriver(pid);
    }
};

class TestPlayerFactory : public MediaPlayerFactory::IFactory {
  public:
    virtual float scoreFactory(const sp<IMediaPlayer>& /*client*/,
                               const char* url,
                               float /*curScore*/) {
        if (TestPlayerStub::canBeUsed(url)) {
            return 1.0;
        }

        return 0.0;
    }

    virtual sp<MediaPlayerBase> createPlayer(pid_t /* pid */) {
        ALOGV("Create Test Player stub");
        return new TestPlayerStub();
    }
};

class TPlayerFactory : public MediaPlayerFactory::IFactory {
  public:
    virtual float scoreFactory(const sp<IMediaPlayer>& /*client*/,
                               int /*fd*/,
                               int64_t /*offset*/,
                               int64_t /*length*/,
                               float /*curScore*/) {

        return 0.0;
    }

    virtual sp<MediaPlayerBase> createPlayer(pid_t /* pid */) {
        ALOGV(" create TPlayer");
        return new TPlayer();
    }
};

class AwPlayerFactory : public MediaPlayerFactory::IFactory {
  public:
    virtual float scoreFactory(const sp<IMediaPlayer>& /*client*/,
                               int /*fd*/,
                               int64_t /*offset*/,
                               int64_t /*length*/,
                               float /*curScore*/) {

        return 0.0;
    }

    virtual sp<MediaPlayerBase> createPlayer(pid_t /* pid */) {
        ALOGV(" create AwPlayer");
        return new AwPlayer();
    }
};

void MediaPlayerFactory::registerBuiltinFactories() {
    Mutex::Autolock lock_(&sLock);

    if (sInitComplete)
        return;


    registerFactory_l(new StagefrightPlayerFactory(), STAGEFRIGHT_PLAYER);
    registerFactory_l(new NuPlayerFactory(), NU_PLAYER);
    registerFactory_l(new TestPlayerFactory(), TEST_PLAYER);
	registerFactory_l(new AwPlayerFactory(), AW_PLAYER);
    registerFactory_l(new TPlayerFactory(), THUMBNAIL_PLAYER);

    sInitComplete = true;
}

}  // namespace android
