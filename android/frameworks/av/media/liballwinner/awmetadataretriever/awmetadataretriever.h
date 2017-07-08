
#ifndef AW_METADATA_RETRIEVER
#define AW_METADATA_RETRIEVER

#include <utils/String8.h>
#include <media/MediaMetadataRetrieverInterface.h>
#include <media/stagefright/MetaData.h>
#include <utils/KeyedVector.h>
#include "vdecoder.h"           //* video decode library in "LIBRARY/CODEC/VIDEO/DECODER"
#include "CdxParser.h"          //* parser library in "LIBRARY/DEMUX/PARSER/include/"
#include "CdxStream.h"          //* parser library in "LIBRARY/DEMUX/STREAM/include/"
#include <media/MediaPlayerInterface.h>

#define MediaScanDedug (1)

using namespace android;

struct AwMetadataRetriever : public MediaMetadataRetrieverInterface
{
    AwMetadataRetriever();
    virtual ~AwMetadataRetriever();

#if (CONFIG_OS_VERSION >= OPTION_OS_VERSION_ANDROID_5_0)
    virtual status_t    setDataSource(const sp<IMediaHTTPService> &httpService,const char* pUrl, const KeyedVector<String8, String8>* pHeaders);
#else
    virtual status_t    setDataSource(const char* pUrl, const KeyedVector<String8, String8>* pHeaders);
#endif
    virtual status_t setDataSource(int fd, int64_t offset, int64_t length);
#if (CONFIG_OS_VERSION >= OPTION_OS_VERSION_ANDROID_6_0)
    virtual status_t setDataSource(const sp<DataSource>& source);
#endif

    //* option enumerate is defined in 'android/framework/base/media/java/android/media/MediaMetadataRetriver.java'
    //* option == 0: OPTION_PREVIOUS_SYNC
    //* option == 1: OPTION_NEXT_SYNC
    //* option == 2: OPTION_CLOSEST_SYNC
    //* option == 3: OPTION_CLOSEST
    virtual VideoFrame* getFrameAtTime(int64_t timeUs, int option);
    virtual MediaAlbumArt* extractAlbumArt();
    virtual const char* extractMetadata(int keyCode);
    virtual sp<IMemory> getStreamAtTime(int64_t timeUs);

private:
    CdxDataSourceT              mSource;
    CdxMediaInfoT               mMediaInfo;
    KeyedVector<int, String8>   mMetaData;
    CdxParserT*                 mParser;
    CdxStreamT*                 mStream;
    VideoDecoder*               mVideoDecoder;
    int                         mCancelPrepareFlag;
	MediaAlbumArt* 				mAlbumArtPic;
#if MediaScanDedug	
	int							mFd;//hkw
#endif	

private:
    void clear();
    void storeMetadata();
    AwMetadataRetriever(const AwMetadataRetriever &);
    AwMetadataRetriever &operator=(const AwMetadataRetriever &);
};

#endif
