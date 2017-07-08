
#define LOG_TAG "awmetadataretriever"
#include "log.h"

#include "awmetadataretriever.h"
#include "memoryAdapter.h"
#include <AwPluginManager.h>
#include <stdio.h>
#include <binder/MemoryBase.h>
#include <binder/MemoryHeapBase.h>
#include <private/media/VideoFrame.h>
#include "vencoder.h"  //* video encode library in "LIBRARY/CODEC/VIDEO/ENCODER"

#if( CONFIG_OS_VERSION >= OPTION_OS_VERSION_ANDROID_5_0)   
#include "media/CharacterEncodingDetector.h"
#endif

#include "unicode/ucnv.h"
#include "unicode/ustring.h"
#include <cutils/properties.h>


#define SAVE_BITSTREAM 0

struct Map {
    int from;
    int to;
    const char *name;
};

static const Map kMap[] = {
    { kKeyMIMEType, METADATA_KEY_MIMETYPE, NULL },
    { kKeyCDTrackNumber, METADATA_KEY_CD_TRACK_NUMBER, "tracknumber" },
    { kKeyDiscNumber, METADATA_KEY_DISC_NUMBER, "discnumber" },
    { kKeyAlbum, METADATA_KEY_ALBUM, "album" },
    { kKeyArtist, METADATA_KEY_ARTIST, "artist" },
    { kKeyAlbumArtist, METADATA_KEY_ALBUMARTIST, "albumartist" },
    { kKeyAuthor, METADATA_KEY_AUTHOR, NULL },
    { kKeyComposer, METADATA_KEY_COMPOSER, "composer" },
    { kKeyDate, METADATA_KEY_DATE, NULL },
    { kKeyGenre, METADATA_KEY_GENRE, "genre" },
    { kKeyTitle, METADATA_KEY_TITLE, "title" },
    { kKeyYear, METADATA_KEY_YEAR, "year" },
    { kKeyWriter, METADATA_KEY_WRITER, "writer" },
    { kKeyCompilation, METADATA_KEY_COMPILATION, "compilation" },
    { kKeyLocation, METADATA_KEY_LOCATION, NULL },
};

static cdx_int32 kNumMapEntries = sizeof(kMap) / sizeof(kMap[0]);

#if SAVE_BITSTREAM
const char* bitstreamPath = "/data/camera/out.h264";
static FILE* fph264 = NULL;
#endif

#define MAX_PACKET_COUNT_TO_GET_A_FRAME 4096    /* process 1024 packets to get a frame at maximum. */
#define MAX_TIME_TO_GET_A_FRAME         5000000 /* use 5 seconds to get a frame at maximum. */
#define MAX_TIME_TO_GET_A_STREAM        10000000 /* use 10 seconds to get a stream at maximum. */
#define MAX_OUTPUT_STREAM_SIZE          (1024*1024)


static int setDataSourceFields(CdxDataSourceT* source, char* uri, KeyedVector<String8,String8>* pHeaders);
static void clearDataSourceFields(CdxDataSourceT* source);
static int64_t GetSysTime();
static int transformPicture(VideoPicture* pPicture, VideoFrame* pVideoFrame);
static int transfromId3Info(String8* mStringId3, cdx_uint8* pData, cdx_int32 nSize, cdx_int32 nEncodeTpye, cdx_int32 flag);

AwMetadataRetriever::AwMetadataRetriever()
{
    logi("AwMetadataRetriever Created.");
    AwPluginInit();
    mParser             = NULL;
	mStream		        = NULL;
    mVideoDecoder       = NULL;
	mCancelPrepareFlag  = 0;
	mAlbumArtPic        = NULL;
	mFd					= 0;
    memset(&mSource, 0, sizeof(CdxDataSourceT));
    MemAdapterOpen();
}


AwMetadataRetriever::~AwMetadataRetriever()
{
    mCancelPrepareFlag = 1;
    clear();
    MemAdapterClose();
    logi("AwMetadataRetriever destroyed.");
}


void AwMetadataRetriever::clear()
{
    //* set mCancelPrepareFlag to force the CdxParserPrepare() quit.
    //* this can prevend the setDataSource() operation from blocking at a network io.
    //* but the retriever's setDataSource() method is a synchronize operation, so I think 
    //* this take no effect, because user can not return from setDataSource() to call this 
    //* method if user's thread is blocked at the setDataSource() operation.
	if(mParser)
	{
        CdxParserForceStop(mParser);    //* to prevend parser from blocking at a network io.
        CdxParserClose(mParser);
        mParser = NULL;
		mStream = NULL;
	}
	else if(mStream)
	{
		CdxStreamForceStop(mStream);
        CdxStreamClose(mStream);
		mStream = NULL;
	}
        
    if(mVideoDecoder != NULL)
    {
        DestroyVideoDecoder(mVideoDecoder);
        mVideoDecoder = NULL;
    }
	
	if(mAlbumArtPic != NULL)
	{
		delete mAlbumArtPic;
		mAlbumArtPic = NULL;
	}
    
    clearDataSourceFields(&mSource);
    memset(&mMediaInfo, 0, sizeof(CdxMediaInfoT));
    mMetaData.clear();
#if MediaScanDedug	
	mFd = -1;
#endif	
    return;
}


#if (CONFIG_OS_VERSION >= OPTION_OS_VERSION_ANDROID_5_0)
status_t AwMetadataRetriever::setDataSource(const sp<IMediaHTTPService> &httpService,const char* pUrl, const KeyedVector<String8, String8>* pHeaders)
#else
status_t AwMetadataRetriever::setDataSource(const char* pUrl, const KeyedVector<String8, String8>* pHeaders)
#endif
{

#if (CONFIG_OS_VERSION >= OPTION_OS_VERSION_ANDROID_5_0)
	CEDARX_UNUSE(httpService);
#endif

    logd("set data source, url = %s", pUrl);
    clear();    //* release parser, decoder and other resource for previous file.
    
    //* 1. set the datasource object.
    if( setDataSourceFields(&mSource, (char*)pUrl, (KeyedVector<String8, String8>*)pHeaders) != 0)
    {
        loge("initialize media source for parser fail.");
        return NO_MEMORY;
    }
    //* 2. create a parser.
	mStream = CdxStreamCreate(&mSource);
	if(!mStream)
	{
		loge("stream creat fail.");
        return UNKNOWN_ERROR;
	}
	int ret = CdxStreamConnect(mStream);
	if(ret < 0)
	{
        return UNKNOWN_ERROR;
	}
	mParser = CdxParserCreate(mStream, MUTIL_AUDIO);
	if(!mParser)
	{
		loge("parser creat fail.");
        return UNKNOWN_ERROR;
	}
	ret = CdxParserInit(mParser);
	if(ret < 0)
	{
        return UNKNOWN_ERROR;
	}
    
    //* 3. get media info.
    memset(&mMediaInfo, 0, sizeof(CdxMediaInfoT));
    if(CdxParserGetMediaInfo(mParser, &mMediaInfo) == 0)
	{
		if (mParser->type == CDX_PARSER_TS ||
			mParser->type == CDX_PARSER_BD || 
			mParser->type == CDX_PARSER_HLS)
		{
			mMediaInfo.program[0].video[0].bIsFramePackage = 0; /* stream package */
		}
		else
		{
			mMediaInfo.program[0].video[0].bIsFramePackage = 1; /* frame package */
		}
        storeMetadata();
	}

    return OK;
}


status_t AwMetadataRetriever::setDataSource(int fd, int64_t nOffset, int64_t nLength)
{
    char str[128];
    
    clear();    //* release parser, decoder and other resource for previous file.
    logd("set data source, fd = %d", fd);
#if MediaScanDedug	
	mFd = fd;
#endif	
    
    //* 1. set the datasource object.
    clearDataSourceFields(&mSource);
    sprintf(str, "fd://%d?offset=%ld&length=%ld", fd, (long int)nOffset, (long int)nLength);
    mSource.uri = strdup(str);
    if(mSource.uri == NULL)
    {
        loge("initialize media source for parser fail.");
        return NO_MEMORY;
    }
    
    //* 2. create a parser.
	mStream = CdxStreamCreate(&mSource);
	if(!mStream)
	{
		loge("stream creat fail.");
        return UNKNOWN_ERROR;
	}
	int ret = CdxStreamConnect(mStream);
	if(ret < 0)
	{
        return UNKNOWN_ERROR;
	}
	mParser = CdxParserCreate(mStream, MUTIL_AUDIO);
	if(!mParser)
	{
		loge("parser creat fail.");
        return UNKNOWN_ERROR;
	}
	ret = CdxParserInit(mParser);
	if(ret < 0)
	{
        return UNKNOWN_ERROR;
	}
    
    //* 3. get media info.
    memset(&mMediaInfo, 0, sizeof(CdxMediaInfoT));
    if(CdxParserGetMediaInfo(mParser, &mMediaInfo) == 0)
	{
		if (mParser->type == CDX_PARSER_TS ||
			mParser->type == CDX_PARSER_BD || 
			mParser->type == CDX_PARSER_HLS)
		{
			mMediaInfo.program[0].video[0].bIsFramePackage = 0; /* stream package */
		}
		else
		{
			mMediaInfo.program[0].video[0].bIsFramePackage = 1; /* frame package */
		}
        storeMetadata();
	}
    
    return OK;
}

#if (CONFIG_OS_VERSION == OPTION_OS_VERSION_ANDROID_6_0)
status_t AwMetadataRetriever::setDataSource(const sp<DataSource>& source)
{
	CEDARX_UNUSE(source);
	return UNKNOWN_ERROR;
}
#endif

void AwMetadataRetriever::storeMetadata(void)
{
    int  i;
    char tmp[256];
	int nStrLen = 0;
    String8 mStrData;
	if(mMediaInfo.pAlbumArtBuf != NULL && mMediaInfo.nAlbumArtBufSize > 0 && mAlbumArtPic == NULL)
	{
#if (CONFIG_OS_VERSION >= OPTION_OS_VERSION_ANDROID_5_0)
		mAlbumArtPic = MediaAlbumArt::fromData(mMediaInfo.nAlbumArtBufSize, mMediaInfo.pAlbumArtBuf);
#else
		mAlbumArtPic = new MediaAlbumArt;
	    mAlbumArtPic->mSize = mMediaInfo.nAlbumArtBufSize;
	    mAlbumArtPic->mData = new uint8_t[mMediaInfo.nAlbumArtBufSize];
		memcpy(mAlbumArtPic->mData,mMediaInfo.pAlbumArtBuf,mMediaInfo.nAlbumArtBufSize);
#endif
	}
	
    //* /**
    //*  * The metadata key to retrieve the numeric string describing the
    //*  * order of the audio data source on its original recording.
    //*  */
    //* public static final int METADATA_KEY_CD_TRACK_NUMBER = 0;
#if 0
    mMetaData.add(METADATA_KEY_CD_TRACK_NUMBER, String8("0"));
    //* no information to give the order of the audio 
    //* ogg file may contain this information in its vorbis comment, index by tag "TRACKNUMBER";
    //* mp3 file may contain this information in its metadata, index by tag "TRK" or "TRCK";
    //* mov file may contain this information in its metadata, index by FOURCC 't' 'r' 'k' 'n';
#endif
    
    //* /**
    //*  * The metadata key to retrieve the information about the album title
    //*  * of the data source.
    //*  */
    //* public static final int METADATA_KEY_ALBUM           = 1;
    //nStrLen = strlen((const char*)mMediaInfo.album);
    nStrLen = mMediaInfo.albumsz;
	if(nStrLen > 0)
	{
	    logd("METADATA_KEY_ALBUM:%d",nStrLen);
        transfromId3Info(&mStrData,mMediaInfo.album,mMediaInfo.albumsz,mMediaInfo.albumCharEncode,kKeyAlbum);
        mMetaData.add(METADATA_KEY_ALBUM,mStrData);
    }
    //* no information to give the album title.
    //* ogg file may contain this information in its vorbis comment, index by tag "ALBUM";
    //* mp3 file may contain this information in its metadata, index by tag "TALB" or "TAL";
    //* mov file may contain this information in its metadata, index by FOURCC 0xa9 'a' 'l' 'b';
    
    //* /**
    //*  * The metadata key to retrieve the information about the artist of
    //*  * the data source.
    //*  */
    //* public static final int METADATA_KEY_ARTIST          = 2;
#if 0
    mMetaData.add(METADATA_KEY_ARTIST, String8("unknown artist"));
    //* no information to give the artist.
    //* ogg file may contain this information in its vorbis comment, index by tag "ARTIST";
    //* mp3 file may contain this information in its metadata, index by tag "TPE1" or "TP1";
    //* mov file may contain this information in its metadata, index by FOURCC 0xa9 'a' 'r' 't';
#endif

    //* warning: we put author as artist here
    //nStrLen = strlen((const char*)mMediaInfo.author);
    nStrLen = mMediaInfo.authorsz;
    if(nStrLen != 0)
    {
    	logd("METADATA_KEY_ARTIST:%d",nStrLen);
        transfromId3Info(&mStrData,mMediaInfo.author,mMediaInfo.authorsz,mMediaInfo.authorCharEncode,kKeyArtist);
        mMetaData.add(METADATA_KEY_ARTIST, mStrData);
    }

    //* /**
    //*  * The metadata key to retrieve the information about the author of
    //*  * the data source.
    //*  */
    //* public static final int METADATA_KEY_AUTHOR          = 3;
	//nStrLen = strlen((const char*)mMediaInfo.author);
	nStrLen = mMediaInfo.authorsz;
	if(nStrLen > 0)
	{
		logd("METADATA_KEY_AUTHOR:%d",nStrLen);
        transfromId3Info(&mStrData,mMediaInfo.author,mMediaInfo.authorsz,mMediaInfo.authorCharEncode,kKeyAuthor);
        mMetaData.add(METADATA_KEY_AUTHOR, mStrData);
    }
    //* no information to give the author.
    //* ogg file may contain this information in its vorbis comment, index by tag "AUTHOR";
    //* mp3 file may contain this information in its metadata, index by tag "TXT" or "TEXT";

    //* /**
    //*  * The metadata key to retrieve the information about the composer of
    //*  * the data source.
    //*  */
    //* public static final int METADATA_KEY_COMPOSER        = 4;
#if 0
    mMetaData.add(METADATA_KEY_COMPOSER, String8("unknown composer"));
    //* no information to give the composer.
    //* ogg file may contain this information in its vorbis comment, index by tag "COMPOSER";
    //* mp3 file may contain this information in its metadata, index by tag "TCOM" or "TCM";
#endif

    //* /**
    //*  * The metadata key to retrieve the date when the data source was created
    //*  * or modified.
    //*  */
    //* public static final int METADATA_KEY_DATE            = 5;
#if 0
    mMetaData.add(METADATA_KEY_DATE, String8("unknown date"));
    //* no information to give the date.
    //* ogg file may contain this information in its vorbis comment, index by tag "DATE";
    //* mov file may find this information by getting its mvhd header's creation time.
#endif

    //* /**
    //*  * The metadata key to retrieve the content type or genre of the data
    //*  * source.
    //*  */
    //* public static final int METADATA_KEY_GENRE           = 6;
	nStrLen = strlen((const char*)mMediaInfo.genre);
	if(nStrLen > 0)
	    mMetaData.add(METADATA_KEY_GENRE, String8((const char*)mMediaInfo.genre));
    //* no information to give the genre.
    //* ogg file may contain this information in its vorbis comment, index by tag "GENRE";
    //* mp3 file may contain this information in its metadata, index by tag "TCON" or "TCO";
    //* mov file may contain this information in its metadata, index by FOURCC 'gnre' or "0xa9 'g' 'e' 'n'";
    //* for mov, iTunes genre codes are the standard id3 codes, except they start at 1 instead of 0 
    //* (e.g. Pop is 14, not 13), if you use standard id3 numbering, you should subtract 1.

    //* /**
    //*  * The metadata key to retrieve the data source title.
    //*  */
    //* public static final int METADATA_KEY_TITLE           = 7;

    //* mMetaData.add(METADATA_KEY_TITLE, String8("unknown title"));

    //* no information to give the title.
    //* ogg file may contain this information in its vorbis comment, index by tag "TITLE";
    //* mp3 file may contain this information in its metadata, index by tag "TIT2" or "TT2";
    //* mov file may contain this information in its metadata, index by FOURCC "0xa9 'n' 'a' 'm'";

    //nStrLen = strlen((const char*)mMediaInfo.title);
    nStrLen = mMediaInfo.titlesz;
    if(nStrLen != 0)
    {
        logd("METADATA_KEY_TITLE:%d",nStrLen);
        transfromId3Info(&mStrData,mMediaInfo.title,mMediaInfo.titlesz,mMediaInfo.titleCharEncode,kKeyTitle);
        mMetaData.add(METADATA_KEY_TITLE, mStrData);
    }


    //* /**
    //*  * The metadata key to retrieve the year when the data source was created
    //*  * or modified.
    //*  */
    //* public static final int METADATA_KEY_YEAR            = 8;
	nStrLen = strlen((const char*)mMediaInfo.year);
	if(nStrLen > 0)
	    mMetaData.add(METADATA_KEY_YEAR, String8((const char*)mMediaInfo.year));
    //* no information to give the title.
    //* mp3 file may contain this information in its metadata, index by tag "TYE" or "TYER";
    //* mov file may contain this information in its metadata, index by FOURCC "0xa9 'd' 'a' 'y'";

    //* /**
    //*  * The metadata key to retrieve the playback duration of the data source.
    //*  */
    //* public static final int METADATA_KEY_DURATION        = 9;
    if(mMediaInfo.programIndex >= 0 && mMediaInfo.programNum >= mMediaInfo.programIndex)
    {
        //* The duration value is a string representing the duration in ms.
        sprintf(tmp, "%d", mMediaInfo.program[mMediaInfo.programIndex].duration);
        mMetaData.add(METADATA_KEY_DURATION, String8(tmp));
    }
    
    //* /**
    //*  * The metadata key to retrieve the number of tracks, such as audio, video,
    //*  * text, in the data source, such as a mp4 or 3gpp file.
    //*  */
    //* public static final int METADATA_KEY_NUM_TRACKS      = 10;
    if(mMediaInfo.programIndex >= 0 && mMediaInfo.programNum >= mMediaInfo.programIndex)
    {
        int nTrackCount = mMediaInfo.program[mMediaInfo.programIndex].videoNum + \
                           mMediaInfo.program[mMediaInfo.programIndex].audioNum + \
                           mMediaInfo.program[mMediaInfo.programIndex].subtitleNum;
        sprintf(tmp, "%d", nTrackCount);
        mMetaData.add(METADATA_KEY_NUM_TRACKS, String8(tmp));
    }
    
    //* /**
    //*  * The metadata key to retrieve the information of the writer (such as
    //*  * lyricist) of the data source.
    //*  */
    //* public static final int METADATA_KEY_WRITER          = 11;
#if 0
    mMetaData.add(METADATA_KEY_WRITER, String8("unknown writer"));
    //* no information to give the writer.
    //* ogg file may contain this information in its vorbis comment, index by tag "LYRICIST";
    //* mov file may contain this information in its metadata, index by FOURCC "0xa9 'w' 'r' 't'";
#endif
    
    //* /**
    //*  * The metadata key to retrieve the mime type of the data source. Some
    //*  * example mime types include: "video/mp4", "audio/mp4", "audio/amr-wb",
    //*  * etc.
    //*  */
    //* public static final int METADATA_KEY_MIMETYPE        = 12;
    if(mMediaInfo.programIndex >= 0 && mMediaInfo.programNum >= mMediaInfo.programIndex)
    {
        switch(mParser->type)
        {
            case CDX_PARSER_MOV:
                mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/mp4"));           //* original mimetype is "video/mp4"
                break;
            case CDX_PARSER_MKV:
                mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/x-matroska"));    //* original mimetype is "video/x-matroska"
                break;
            case CDX_PARSER_ASF:
                mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/x-ms-asf")); //* original mimetype is "video/windows-media"
                break;
            case CDX_PARSER_MPG:
                mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/mpeg"));
                break;
            case CDX_PARSER_TS:
                mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/mp2ts"));
                break;
            case CDX_PARSER_AVI:
                mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/avi"));
                break;
            case CDX_PARSER_FLV:
                mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/cedarx"));
                break;
            case CDX_PARSER_PMP:
                mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/cedarx"));
                break;
            case CDX_PARSER_HLS:
                mMetaData.add(METADATA_KEY_MIMETYPE, String8("application/vnd.apple.mpegurl"));
                break;
            case CDX_PARSER_DASH:
                mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/cedarx"));
                break;
            case CDX_PARSER_MMS:
                mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/cedarx")); //* original mimetype is "video/windows-media"
                break;
            case CDX_PARSER_BD:
                mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/cedarx"));
                break;
            case CDX_PARSER_OGG:
                mMetaData.add(METADATA_KEY_MIMETYPE, String8("application/ogg"));
                break;
            case CDX_PARSER_M3U9:
                mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/cedarx"));
                break;
            case CDX_PARSER_RMVB:
                mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/cedarx"));
                break;
            case CDX_PARSER_PLAYLIST:
                mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/cedarx"));
                break;
            case CDX_PARSER_WAV:
                mMetaData.add(METADATA_KEY_MIMETYPE, String8("audio/x-wav"));
                break;
            case CDX_PARSER_APE:
                mMetaData.add(METADATA_KEY_MIMETYPE, String8("audio/cedara"));
                break;
            case CDX_PARSER_MP3:
                mMetaData.add(METADATA_KEY_MIMETYPE, String8("audio/mpeg"));
                break;
            case CDX_PARSER_WVM:
                mMetaData.add(METADATA_KEY_MIMETYPE, String8("application/x-android-drm-fl"));
                break;
            case CDX_PARSER_REMUX:
                mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/cedarx"));
                break;
            default:
                break;
            //* going to be add:
            //* WAV: audio/x-wav
            //* APE:
            //* ...
            //* Widevine: video/wvm
            //* ...
        }

        #if 0
        //* add video mimetype.
        for(i=0; i<mMediaInfo.program[mMediaInfo.programIndex].videoNum; i++)
        {
            switch(mMediaInfo.program[mMediaInfo.programIndex].video[i].eCodecFormat)
            {
                case VIDEO_CODEC_FORMAT_MJPEG:
                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/jpeg"));
                    break;
                case VIDEO_CODEC_FORMAT_MPEG1:
                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/mpeg1"));
                    break;
                case VIDEO_CODEC_FORMAT_MPEG2:
                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/mpeg2"));
                    break;
                case VIDEO_CODEC_FORMAT_MPEG4:
                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/mp4v-es"));
                    break;
                case VIDEO_CODEC_FORMAT_MSMPEG4V1:
                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/x-ms-mpeg4v1"));
                    break;
                case VIDEO_CODEC_FORMAT_MSMPEG4V2:
                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/x-ms-mpeg4v2"));
                    break;
                case VIDEO_CODEC_FORMAT_DIVX3:
                case VIDEO_CODEC_FORMAT_DIVX4:
                case VIDEO_CODEC_FORMAT_DIVX5:
                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/divx"));
                    break;
                case VIDEO_CODEC_FORMAT_XVID:
                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/xvid"));
                    break;
                case VIDEO_CODEC_FORMAT_H263:
                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/3gpp"));
                    break;
                case VIDEO_CODEC_FORMAT_SORENSSON_H263:
                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/flv1"));
                    break;
                case VIDEO_CODEC_FORMAT_RXG2:
                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/rvg2"));
                    break;
                case VIDEO_CODEC_FORMAT_WMV1:
                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/wmv1"));
                    break;
                case VIDEO_CODEC_FORMAT_WMV2:
                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/wmv2"));
                    break;
                case VIDEO_CODEC_FORMAT_WMV3:
                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/x-ms-wmv"));
                    break;
                case VIDEO_CODEC_FORMAT_VP6:
                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/x-vnd.on2.vp6"));
                    break;
                case VIDEO_CODEC_FORMAT_VP8:
                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/x-vnd.on2.vp8"));
                    break;
                case VIDEO_CODEC_FORMAT_VP9:
                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/x-vnd.on2.vp9"));
                    break;
                case VIDEO_CODEC_FORMAT_RX:
                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/realvideo"));
                    break;
                case VIDEO_CODEC_FORMAT_H264:
                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/avc"));
                    break;
                case VIDEO_CODEC_FORMAT_H265:
                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/hevc"));
                    break;
                case VIDEO_CODEC_FORMAT_AVS:
                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("video/avs"));
                    break;
                default:
                    break;
            }
        }
		#endif

		#if 1
		if(mMediaInfo.programIndex >= 0 && mMediaInfo.programNum >= mMediaInfo.programIndex)
	    {
	        if((mMediaInfo.program[mMediaInfo.programIndex].audioNum > 0) &&
			    (mMediaInfo.program[mMediaInfo.programIndex].videoNum <= 0))
	        {
	            //* add audio mimetype.
                for(i=0; i<mMediaInfo.program[mMediaInfo.programIndex].audioNum; i++)
		        {
		            switch(mMediaInfo.program[mMediaInfo.programIndex].audio[i].eCodecFormat)
		            {
		                case AUDIO_CODEC_FORMAT_MP1:
		                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("audio/cedara"));
		                    break;
		                case AUDIO_CODEC_FORMAT_MP2:
		                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("audio/cedara"));
		                    break;
		                case AUDIO_CODEC_FORMAT_MP3:
		                case AUDIO_CODEC_FORMAT_MP3_PRO:
		                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("audio/mpeg"));
		                    break;
		                case AUDIO_CODEC_FORMAT_MPEG_AAC_LC:
		                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("audio/cedara"));
		                    //* mMetaData.add(METADATA_KEY_MIMETYPE, String8("audio/aac-adts"));
		                    break;
		                case AUDIO_CODEC_FORMAT_AC3:
		                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("audio/cedara"));
		                    break;
		                case AUDIO_CODEC_FORMAT_DTS:
		                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("audio/cedara"));
		                    break;
		                case AUDIO_CODEC_FORMAT_LPCM_V:
		                case AUDIO_CODEC_FORMAT_LPCM_A:
		                case AUDIO_CODEC_FORMAT_ADPCM:
		                case AUDIO_CODEC_FORMAT_PPCM:
		                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("audio/cedara"));
		                    break;
		                case AUDIO_CODEC_FORMAT_PCM:
		                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("audio/cedara"));
		                    break;
		                case AUDIO_CODEC_FORMAT_WMA_STANDARD:
		                case AUDIO_CODEC_FORMAT_WMA_LOSS:
		                case AUDIO_CODEC_FORMAT_WMA_PRO:
		                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("audio/x-ms-wma"));
		                    break;
		                case AUDIO_CODEC_FORMAT_FLAC:
		                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("audio/flac"));
		                    break;
		                case AUDIO_CODEC_FORMAT_APE:
		                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("audio/cedara"));
		                    break;
		                case AUDIO_CODEC_FORMAT_OGG:
		                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("audio/ogg"));
		                    break;
		                case AUDIO_CODEC_FORMAT_RAAC:
		                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("audio/aac-adts"));
		                    break;
		                case AUDIO_CODEC_FORMAT_COOK:
		                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("audio/cook"));
		                    break;
		                case AUDIO_CODEC_FORMAT_SIPR:
		                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("audio/sipr"));
		                    break;
		                case AUDIO_CODEC_FORMAT_ATRC:
		                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("audio/atrc"));
		                    break;
		                case AUDIO_CODEC_FORMAT_AMR:
		                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("audio/amr"));      //* for amr-wb.
		                    //* mMetaData.add(METADATA_KEY_MIMETYPE, String8("audio/3gpp"));      //* for amr-nb.
		                    break;
		                case AUDIO_CODEC_FORMAT_RA:
		                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("audio/cedara"));
		                    break;
		                default:
		                    break;
		            }
		        }
	        }
	    }
		#endif

		#if 0
        //* add subtitle mimetype.
        for(i=0; i<mMediaInfo.program[mMediaInfo.programIndex].subtitleNum; i++)
        {
            switch(mMediaInfo.program[mMediaInfo.programIndex].subtitle[i].eCodecFormat)
            {
                case SUBTITLE_CODEC_DVDSUB:
                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("subtitle/x-subrip"));  //* original mimetype is "application/x-subrip"
                    break;
                case SUBTITLE_CODEC_IDXSUB:
                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("subtitle/index-subtitle"));    //* original mimetype is "application/x-subrip"
                    break;
                case SUBTITLE_CODEC_PGS:
                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("subtitle/bd-subtitle"));
                    break;
                case SUBTITLE_CODEC_TXT:
                case SUBTITLE_CODEC_TIMEDTEXT:
                case SUBTITLE_CODEC_SRT:
                case SUBTITLE_CODEC_SMI:
                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("text/3gpp-tt"));
                    break;
                case SUBTITLE_CODEC_SSA:
                    mMetaData.add(METADATA_KEY_MIMETYPE, String8("text/ssa"));
                    break;
                default:
                    break;
            }
        }
        #endif
    }

    
    //* /**
    //*  * The metadata key to retrieve the information about the performers or
    //*  * artist associated with the data source.
    //*  */
    //* public static final int METADATA_KEY_ALBUMARTIST     = 13;
#if 0
    mMetaData.add(METADATA_KEY_ALBUMARTIST, String8("unknown album artist"));
    //* no information to give the artist.
    //* ogg file may contain this information in its vorbis comment, index by tag "ALBUMARTIST" or "ALBUM ARTIST";
    //* mp3 file may contain this information in its metadata, index by tag "TPE2" or "TP2";
    //* mov file may contain this information in its metadata, index by FOURCC "'a' 'A' 'R' 'T'";
#endif

    //* /**
    //*  * The metadata key to retrieve the numberic string that describes which
    //*  * part of a set the audio data source comes from.
    //*  */
    //* public static final int METADATA_KEY_DISC_NUMBER     = 14;
#if 0
    mMetaData.add(METADATA_KEY_DISC_NUMBER, String8("unknown disk number"));
    //* no information to tell which part of a set the audio data source comes from.
    //* ogg file may contain this information in its vorbis comment, index by tag "DISCNUMBER";
    //* mp3 file may contain this information in its metadata, index by tag "TPA" or "TPOS";
    //* mov file may contain this information in its metadata, index by FOURCC "'d' 'i' 's' 'k'";
#endif

    //* /**
    //*  * The metadata key to retrieve the music album compilation status.
    //*  */
    //* public static final int METADATA_KEY_COMPILATION     = 15;
#if 0
    mMetaData.add(METADATA_KEY_COMPILATION, String8("unknown compilation"));
    //* no information to tell which part of a set the audio data source comes from.
    //* ogg file may contain this information in its vorbis comment, index by tag "COMPILATION";
    //* mp3 file may contain this information in its metadata, index by tag "TCP" or "TCMP";
    //* mov file may contain this information in its metadata, index by FOURCC "'c' 'p' 'i' 'l'";
#endif

    //* /**
    //*  * If this key exists the media contains audio content.
    //*  */
    //* public static final int METADATA_KEY_HAS_AUDIO       = 16;
    if(mMediaInfo.programIndex >= 0 && mMediaInfo.programNum >= mMediaInfo.programIndex)
    {
        //* add HAS_AUDIO info when have audio,or not add
        //* (reference to StagefrightMetadataRetriever.cpp)
        if(mMediaInfo.program[mMediaInfo.programIndex].audioNum > 0)
            mMetaData.add(METADATA_KEY_HAS_AUDIO, String8("yes"));
    }
    
    //* /**
    //*  * If this key exists the media contains video content.
    //*  */
    //* public static final int METADATA_KEY_HAS_VIDEO       = 17;
    if(mMediaInfo.programIndex >= 0 && mMediaInfo.programNum >= mMediaInfo.programIndex)
    {
        //* add HAS_VIDEO info when hava video, or not add
        //* (reference to StagefrightMetadataRetriever.cpp)
        if(mMediaInfo.program[mMediaInfo.programIndex].videoNum > 0)
            mMetaData.add(METADATA_KEY_HAS_VIDEO, String8("yes"));
    }
    
    //* /**
    //*  * If the media contains video, this key retrieves its width.
    //*  */
    //* public static final int METADATA_KEY_VIDEO_WIDTH     = 18;
    if(mMediaInfo.programIndex >= 0 && mMediaInfo.programNum >= mMediaInfo.programIndex)
    {
        if(mMediaInfo.program[mMediaInfo.programIndex].videoNum > 0)
        {
            sprintf(tmp, "%d", mMediaInfo.program[mMediaInfo.programIndex].video[0].nWidth);
            mMetaData.add(METADATA_KEY_VIDEO_WIDTH, String8(tmp));
        }
    }
    
    //* /**
    //*  * If the media contains video, this key retrieves its height.
    //*  */
    //* public static final int METADATA_KEY_VIDEO_HEIGHT    = 19;
    if(mMediaInfo.programIndex >= 0 && mMediaInfo.programNum >= mMediaInfo.programIndex)
    {
        if(mMediaInfo.program[mMediaInfo.programIndex].videoNum > 0)
        {
            sprintf(tmp, "%d", mMediaInfo.program[mMediaInfo.programIndex].video[0].nHeight);
            //* add the video height
            mMetaData.add(METADATA_KEY_VIDEO_HEIGHT, String8(tmp));
        }
    }
    
    //* /**
    //*  * This key retrieves the average bitrate (in bits/sec), if available.
    //*  */
    //* public static final int METADATA_KEY_BITRATE         = 20;
#if 0
    mMetaData.add(METADATA_KEY_BITRATE, String8("unknown bitrate"));
    //* no information to give the average bitrate.
#endif

    //* /**
    //*  * This key retrieves the language code of text tracks, if available.
    //*  * If multiple text tracks present, the return value will look like:
    //*  * "eng:chi"
    //*  * @hide
    //*  */
    //* public static final int METADATA_KEY_TIMED_TEXT_LANGUAGES      = 21;
    if(mMediaInfo.programIndex >= 0 && mMediaInfo.programNum >= mMediaInfo.programIndex)
    {
        if(mMediaInfo.program[mMediaInfo.programIndex].subtitleNum > 0)
        {
            int len;
            char buffer[256];
            tmp[0] = '\0';
			buffer[0] = '\0';
            for(i=0; i<mMediaInfo.program[mMediaInfo.programIndex].subtitleNum; i++)
            {
                if(i != 0)
                    sprintf(buffer, "%s:", tmp);   //* add a ":".
                
                len = strlen((const char*)mMediaInfo.program[mMediaInfo.programIndex].subtitle[i].strLang);
                if(len == 0)
                    len = strlen("unknown");
                
                if(len + strlen(tmp) >= (sizeof(tmp)-1))
                {
                    logw("can not set the language of subtitle correctly, cause of string too long");
                    break;  //* for save.
                }
                
                if(strlen((const char*)mMediaInfo.program[mMediaInfo.programIndex].subtitle[i].strLang) > 0)
                    sprintf(buffer, "%s%s", tmp, mMediaInfo.program[mMediaInfo.programIndex].subtitle[i].strLang);
                else
                    sprintf(buffer, "%s%s", tmp, "unknown");
            }
            mMetaData.add(METADATA_KEY_TIMED_TEXT_LANGUAGES, String8(buffer));
        }
    }
        
    //* /**
    //*  * If this key exists the media is drm-protected.
    //*  * @hide
    //*  */
    //* public static final int METADATA_KEY_IS_DRM          = 22;
#if 0
    mMetaData.add(METADATA_KEY_IS_DRM, String8("0"));
    //* no information to give the drm info.
#endif

    //* /**
    //*  * This key retrieves the location information, if available.
    //*  * The location should be specified according to ISO-6709 standard, under
    //*  * a mp4/3gp box "@xyz". Location with longitude of -90 degrees and latitude
    //*  * of 180 degrees will be retrieved as "-90.0000+180.0000", for instance.
    //*  */
    //* public static final int METADATA_KEY_LOCATION        = 23;
    
    //* set the location info
    //* we should not add it when the nStrlen is 0
    nStrLen = strlen((const char*)mMediaInfo.location);
    if(nStrLen != 0)
        mMetaData.add(METADATA_KEY_LOCATION, String8((const char*)mMediaInfo.location));

    //* /**
    //*  * This key retrieves the video rotation angle in degrees, if available.
    //*  * The video rotation angle may be 0, 90, 180, or 270 degrees.
    //*  */
    //* public static final int METADATA_KEY_VIDEO_ROTATION = 24;
    
    //* set rotation info
    //* mov parser can get the rotate angle.
    //* we should not add it when the nStrlen is 0
    nStrLen = strlen((const char*)mMediaInfo.rotate);
    if(nStrLen != 0)
        mMetaData.add(METADATA_KEY_VIDEO_ROTATION, String8((const char*)mMediaInfo.rotate));

    return;
}


const char* AwMetadataRetriever::extractMetadata(int keyCode)
{
    //* keyCode is defined in "android/framworks/av/include/media/mediametadataretriever.h" and 
    //* "android/frameworks/base/media/java/android/media/mediametadataretriever.java".
    //* keyCodes list here.
    //* /**
    //*  * The metadata key to retrieve the numeric string describing the
    //*  * order of the audio data source on its original recording.
    //*  */
    //* public static final int METADATA_KEY_CD_TRACK_NUMBER = 0;
    //* /**
    //*  * The metadata key to retrieve the information about the album title
    //*  * of the data source.
    //*  */
    //* public static final int METADATA_KEY_ALBUM           = 1;
    //* /**
    //*  * The metadata key to retrieve the information about the artist of
    //*  * the data source.
    //*  */
    //* public static final int METADATA_KEY_ARTIST          = 2;
    //* /**
    //*  * The metadata key to retrieve the information about the author of
    //*  * the data source.
    //*  */
    //* public static final int METADATA_KEY_AUTHOR          = 3;
    //* /**
    //*  * The metadata key to retrieve the information about the composer of
    //*  * the data source.
    //*  */
    //* public static final int METADATA_KEY_COMPOSER        = 4;
    //* /**
    //*  * The metadata key to retrieve the date when the data source was created
    //*  * or modified.
    //*  */
    //* public static final int METADATA_KEY_DATE            = 5;
    //* /**
    //*  * The metadata key to retrieve the content type or genre of the data
    //*  * source.
    //*  */
    //* public static final int METADATA_KEY_GENRE           = 6;
    //* /**
    //*  * The metadata key to retrieve the data source title.
    //*  */
    //* public static final int METADATA_KEY_TITLE           = 7;
    //* /**
    //*  * The metadata key to retrieve the year when the data source was created
    //*  * or modified.
    //*  */
    //* public static final int METADATA_KEY_YEAR            = 8;
    //* /**
    //*  * The metadata key to retrieve the playback duration of the data source.
    //*  */
    //* public static final int METADATA_KEY_DURATION        = 9;
    //* /**
    //*  * The metadata key to retrieve the number of tracks, such as audio, video,
    //*  * text, in the data source, such as a mp4 or 3gpp file.
    //*  */
    //* public static final int METADATA_KEY_NUM_TRACKS      = 10;
    //* /**
    //*  * The metadata key to retrieve the information of the writer (such as
    //*  * lyricist) of the data source.
    //*  */
    //* public static final int METADATA_KEY_WRITER          = 11;
    //* /**
    //*  * The metadata key to retrieve the mime type of the data source. Some
    //*  * example mime types include: "video/mp4", "audio/mp4", "audio/amr-wb",
    //*  * etc.
    //*  */
    //* public static final int METADATA_KEY_MIMETYPE        = 12;
    //* /**
    //*  * The metadata key to retrieve the information about the performers or
    //*  * artist associated with the data source.
    //*  */
    //* public static final int METADATA_KEY_ALBUMARTIST     = 13;
    //* /**
    //*  * The metadata key to retrieve the numberic string that describes which
    //*  * part of a set the audio data source comes from.
    //*  */
    //* public static final int METADATA_KEY_DISC_NUMBER     = 14;
    //* /**
    //*  * The metadata key to retrieve the music album compilation status.
    //*  */
    //* public static final int METADATA_KEY_COMPILATION     = 15;
    //* /**
    //*  * If this key exists the media contains audio content.
    //*  */
    //* public static final int METADATA_KEY_HAS_AUDIO       = 16;
    //* /**
    //*  * If this key exists the media contains video content.
    //*  */
    //* public static final int METADATA_KEY_HAS_VIDEO       = 17;
    //* /**
    //*  * If the media contains video, this key retrieves its width.
    //*  */
    //* public static final int METADATA_KEY_VIDEO_WIDTH     = 18;
    //* /**
    //*  * If the media contains video, this key retrieves its height.
    //*  */
    //* public static final int METADATA_KEY_VIDEO_HEIGHT    = 19;
    //* /**
    //*  * This key retrieves the average bitrate (in bits/sec), if available.
    //*  */
    //* public static final int METADATA_KEY_BITRATE         = 20;
    //* /**
    //*  * This key retrieves the language code of text tracks, if available.
    //*  * If multiple text tracks present, the return value will look like:
    //*  * "eng:chi"
    //*  * @hide
    //*  */
    //* public static final int METADATA_KEY_TIMED_TEXT_LANGUAGES      = 21;
    //* /**
    //*  * If this key exists the media is drm-protected.
    //*  * @hide
    //*  */
    //* public static final int METADATA_KEY_IS_DRM          = 22;
    //* /**
    //*  * This key retrieves the location information, if available.
    //*  * The location should be specified according to ISO-6709 standard, under
    //*  * a mp4/3gp box "@xyz". Location with longitude of -90 degrees and latitude
    //*  * of 180 degrees will be retrieved as "-90.0000+180.0000", for instance.
    //*  */
    //* public static final int METADATA_KEY_LOCATION        = 23;
    //* /**
    //*  * This key retrieves the video rotation angle in degrees, if available.
    //*  * The video rotation angle may be 0, 90, 180, or 270 degrees.
    //*  */
    //* public static final int METADATA_KEY_VIDEO_ROTATION = 24;
    
    int index;
    const char* strMetadataName[] = 
    {
        "CD_TRACK_NUMBER",      "ALBUM",                "ARTIST",           "AUTHOR",
        "CD_TRACK_COMPOSER",    "DATE",                 "GENRE",            "TITLE",
        "YEAR",                 "DURATION",             "NUM_TRACKS",       "WRITER",
        "MIMETYPE",             "ALBUMARTIST",          "DISC_NUMBER",      "COMPILATION",
        "HAS_AUDIO",            "HAS_VIDEO",            "VIDEO_WIDTH",      "VIDEO_HEIGHT",
        "BITRATE",              "TIMED_TEXT_LANGUAGES", "IS_DRM",           "LOCATION",
        "VIDEO_ROTATION"
    };
    
    index = mMetaData.indexOfKey(keyCode);
    if (index < 0)
    {
        if(keyCode >=0 && keyCode <= 24)
		{
            logw("no metadata for %s.", strMetadataName[keyCode]);
		}
        return NULL;
    }

    return mMetaData.valueAt(index).string();
}


MediaAlbumArt* AwMetadataRetriever::extractAlbumArt()
{
    //* no album art picture extracted.
    //* ogg parser can find the album art picture in its vorbis comment, index by tag "METADATA_BLOCK_PICTURE";
    //* flac parser can find the album art picture in its meta data;
    //* mp3 parser can find the album art picture in its id3 data;
    //* mov parser can find the album art picture in its metadata, index by FOURCC "'c' 'o' 'v' 'r'";

	if(mAlbumArtPic != NULL)
	{
#if (CONFIG_OS_VERSION >= OPTION_OS_VERSION_ANDROID_5_0)
		return mAlbumArtPic->clone();
#else
		return new MediaAlbumArt(*mAlbumArtPic);
#endif
	}
	else
	{
		return NULL;
	}
    //* note:
    //* if it is a media file with drm protection, we should not return an album art picture.
}


sp<IMemory> AwMetadataRetriever::getStreamAtTime(int64_t timeUs)
{

	VideoFrame* 	  pVideoFrame;
	VideoPicture*	  pPicture;
	int 			  bDone;
	int 			  bHasVideo;
	int 			  nHorizonScaleRatio;
	int 			  nVerticalScaleRatio;
	enum EPIXELFORMAT ePixelFormat;
	VConfig 		  vconfig;
	CdxPacketT		  packet;
	int 			  ret;
	int64_t 		  nStartTime;
	int64_t 		  nTimePassed;
    sp<IMemory> 	  mem = NULL;

	//* for encoder
	VencBaseConfig    sBaseEncConfig;
	VencInputBuffer   sInputBuffer;
	VencOutputBuffer  sOutputBuffer;
	VencHeaderData    spsppsInfo;
	VencH264Param     h264Param;
	VideoEncoder*     pVideoEncoder    = NULL;
	int               bEncoderInited   = 0;
	unsigned char*    pOutBuf          = NULL;
	int               bBitStreamLength = 0;
	int               bEncodeAllFrames = 0;
	int               bFrameCounter    = 0;

#if SAVE_BITSTREAM
	fph264 = fopen(bitstreamPath, "wb");
#endif

	//* FIXME:
	//* if it is a media file with drm protection, we should not return an album art picture.
	
	bDone = 0;
	bHasVideo = 0;
	memset(&vconfig, 0, sizeof(VConfig));
	
	//* 1. check whether there is a video stream.
	if(mMediaInfo.programIndex >= 0 && mMediaInfo.programNum >= mMediaInfo.programIndex)
	{
		if(mMediaInfo.program[mMediaInfo.programIndex].videoNum > 0)
			bHasVideo = 1;
	}
	if(!bHasVideo)
	{
		logw("media file do not contain a video stream, getFrameAtTime() return fail.");
		goto NEED_EXIT;
	}

	if(mMediaInfo.program[mMediaInfo.programIndex].video[0].eCodecFormat == VIDEO_CODEC_FORMAT_WMV1 ||
		mMediaInfo.program[mMediaInfo.programIndex].video[0].eCodecFormat == VIDEO_CODEC_FORMAT_WMV2 ||
		mMediaInfo.program[mMediaInfo.programIndex].video[0].eCodecFormat == VIDEO_CODEC_FORMAT_VP6 ||
		mMediaInfo.program[mMediaInfo.programIndex].video[0].eCodecFormat == VIDEO_CODEC_FORMAT_H265 ||
		mMediaInfo.program[mMediaInfo.programIndex].video[0].eCodecFormat == VIDEO_CODEC_FORMAT_VP9)
	{
		logw("Soft decoder thumb disable!");
		goto NEED_EXIT;
	}

	//* 2. create a video encoder
	pVideoEncoder = VideoEncCreate(VENC_CODEC_H264);
	if(pVideoEncoder == NULL)
	{
		goto NEED_EXIT;
	}

	pOutBuf = (unsigned char*)malloc(MAX_OUTPUT_STREAM_SIZE);
	if(pOutBuf == NULL)
	{
		goto NEED_EXIT;
	}
	
	//* 3. create a video decoder.
	if(mVideoDecoder == NULL)
	{
		mVideoDecoder = CreateVideoDecoder();
		//* use decoder to capture thumbnail, decoder use less memory in this mode.
		vconfig.bThumbnailMode		= 0;
		
		//* all decoder support YUV_MB32_420 format.
		vconfig.eOutputPixelFormat	= PIXEL_FORMAT_NV21;
		//* no need to decode two picture when decoding a thumbnail picture.
		vconfig.bDisable3D			= 1;
		
		vconfig.nAlignStride        = 16;//* set align stride to 16 as defualt
#if 1
        //* set this flag when the parser can give this info, mov files recorded by iphone or android phone
        //* conains this info.

        //* set the rotation
		int nRotateDegree;
		int nRotation = atoi((const char*)mMediaInfo.rotate);
		
		if(nRotation == 0)
		   nRotateDegree = 0;
		else if(nRotation == 90)
		   nRotateDegree = 1;
		else if(nRotation == 180)
		   nRotateDegree = 2;
		else if(nRotation == 270)
		   nRotateDegree = 3;
		else
		   nRotateDegree = 0;

		if(nRotateDegree != 0)
		{
			vconfig.bRotationEn         = 1;
			vconfig.nRotateDegree       = nRotateDegree;
		}else
		{
	        vconfig.bRotationEn         = 0;
	        vconfig.nRotateDegree       = 0;
		}

#endif
		//* set the picture scale down ratio, we generate a picture with pixel size less than 640x480.
		nHorizonScaleRatio	= 0;
		nVerticalScaleRatio = 0;

		if(vconfig.nRotateDegree == 1 || vconfig.nRotateDegree == 3)
		{
			if(mMediaInfo.program[mMediaInfo.programIndex].video[0].nWidth > 960 ||
				mMediaInfo.program[mMediaInfo.programIndex].video[0].nWidth  == 0) /* nWidth=0 treated as nWidth=1920 */
				nHorizonScaleRatio = 2; //* scale down to 1/4 the original width;
			else if(mMediaInfo.program[mMediaInfo.programIndex].video[0].nWidth > 480)
				nHorizonScaleRatio = 1; //* scale down to 1/2 the original width;
			if(mMediaInfo.program[mMediaInfo.programIndex].video[0].nHeight > 1280 ||
				mMediaInfo.program[mMediaInfo.programIndex].video[0].nHeight == 0) /* nHeight=0 treated as nHeight=1080 */
				nVerticalScaleRatio = 2; //* scale down to 1/4 the original height;
			else if(mMediaInfo.program[mMediaInfo.programIndex].video[0].nHeight > 640)
				nVerticalScaleRatio = 1; //* scale down to 1/2 the original height;
		}else
		{
			if(mMediaInfo.program[mMediaInfo.programIndex].video[0].nWidth > 1280 ||
				mMediaInfo.program[mMediaInfo.programIndex].video[0].nWidth  == 0) /* nWidth=0 treated as nWidth=1920 */
				nHorizonScaleRatio = 2; //* scale down to 1/4 the original width;
			else if(mMediaInfo.program[mMediaInfo.programIndex].video[0].nWidth > 640)
				nHorizonScaleRatio = 1; //* scale down to 1/2 the original width;
			if(mMediaInfo.program[mMediaInfo.programIndex].video[0].nHeight > 960 ||
				mMediaInfo.program[mMediaInfo.programIndex].video[0].nHeight == 0) /* nHeight=0 treated as nHeight=1080 */
				nVerticalScaleRatio = 2; //* scale down to 1/4 the original height;
			else if(mMediaInfo.program[mMediaInfo.programIndex].video[0].nHeight > 480)
				nVerticalScaleRatio = 1; //* scale down to 1/2 the original height;
		}
		
		//* set to the same scale ratio.
		if(nVerticalScaleRatio > nHorizonScaleRatio)
			nHorizonScaleRatio = nVerticalScaleRatio;
		else
			nVerticalScaleRatio = nHorizonScaleRatio;
		
		//* set scale ratio to vconfig.
		if(nHorizonScaleRatio || nVerticalScaleRatio)
		{
			vconfig.bScaleDownEn			= 1;
			vconfig.nHorizonScaleDownRatio	= nHorizonScaleRatio;
			vconfig.nVerticalScaleDownRatio = nVerticalScaleRatio;
		}
		
		//* initialize the decoder.
        vconfig.nDeInterlaceHoldingFrameBufferNum = 0;//*not use deinterlace
        vconfig.nDisplayHoldingFrameBufferNum     = 0;//*gpu and decoder not share buffer
        vconfig.nRotateHoldingFrameBufferNum      = NUM_OF_PICTURES_KEEPPED_BY_ROTATE;
        vconfig.nDecodeSmoothFrameBufferNum       = NUM_OF_PICTURES_FOR_EXTRA_SMOOTH;
		if(InitializeVideoDecoder(mVideoDecoder, &mMediaInfo.program[mMediaInfo.programIndex].video[0], &vconfig) != 0)
		{
			loge("initialize video decoder fail.");
			goto NEED_EXIT;
		}
	}
 
	//* 4. seek parser to the specific position.
	if(mMediaInfo.bSeekable && timeUs != 0 && timeUs < ((int64_t)mMediaInfo.program[mMediaInfo.programIndex].duration*1000))
	{
		//* FIXME.
		//* we should seek to a position according to the 'option' param.
		//* option = 0 means seek to the sync frame privious to the timeUs.
		//* option = 1 means seek to the sync frame next to the timeUs.
		//* option = 2 means seek to the sync frame closest to the timeUs.
		//* option = 3 means seek to the closest frame to the timeUs.
		//* here we process all case as option = 0.
		if(CdxParserSeekTo(mParser, timeUs) != 0)
		{
			loge("can not seek media file to the specific time %lld us.", timeUs);
			goto NEED_EXIT;
		}
	}
	else
	{
		logw("media file do not support seek operation, get frame from the begin.");
	}
	
	//* 5. loop to get bitstream.
	nStartTime	 = GetSysTime();
	do
	{	
		while(VideoStreamFrameNum(mVideoDecoder, 0) < 4)
		{
			//* 5.1 prefetch packet type and packet data size.
			if(CdxParserPrefetch(mParser, &packet) != 0)
			{
				//* prefetch fail, may be file end reached.
				bDone = 1;
				goto EOS_EXIT;
			}
			
			//* 5.2 feed data to the video decoder.
			if(packet.type == CDX_MEDIA_VIDEO && (packet.flags&MINOR_STREAM)==0)
			{
				ret = RequestVideoStreamBuffer(mVideoDecoder, 
											   packet.length, 
											   (char**)&packet.buf, 
											   &packet.buflen,
											   (char**)&packet.ringBuf,
											   &packet.ringBufLen,
											   0);
				
				if(ret==0 && (packet.buflen+packet.ringBufLen)>=packet.length)
				{
					if(CdxParserRead(mParser, &packet) == 0)
					{
						VideoStreamDataInfo dataInfo;
						dataInfo.pData		  = (char*)packet.buf;
						dataInfo.nLength	  = packet.length;
						dataInfo.nPts		  = packet.pts;
						dataInfo.nPcr		  = -1;
						dataInfo.bIsFirstPart = 1;
						dataInfo.bIsLastPart  = 1;
						dataInfo.nStreamIndex = 0;
						SubmitVideoStreamData(mVideoDecoder, &dataInfo, 0);
					}
					else
					{
						//* read data fail, may be data error.
						loge("read packet from parser fail.");
						bDone = 1;
						goto EOS_EXIT;
					}
				}
				else
				{
					//* no buffer, may be the decoder is full of stream.
					logw("waiting for stream buffer.");
					
					bDone = 1;
					goto EOS_EXIT;
				}
			}
			else
			{
				//* only process the major video stream.
				//* allocate a buffer to read uncare media data and skip it.
				packet.buf = malloc(packet.length);
				if(packet.buf != NULL)
				{
					packet.buflen	  = packet.length;
					packet.ringBuf	  = NULL;
					packet.ringBufLen = 0;
					if(CdxParserRead(mParser, &packet) == 0)
					{
						free(packet.buf);
						continue;
					}
					else
					{
					    free(packet.buf);
						//* read data fail, may be data error.
						loge("read packet from parser fail.");
						bDone = 1;
						goto EOS_EXIT;
					}
				}
				else
				{
					loge("can not allocate buffer for none video packet.");
					bDone = 1;
					goto EOS_EXIT;
				}
			}			
		}

		logv("stream number: %d", VideoStreamFrameNum(mVideoDecoder, 0));
		
		//* 5.3 decode stream.
		ret = DecodeVideoStream(mVideoDecoder, 0 /*eos*/, 0/*key frame only*/, 0/*drop b frame*/, 0/*current time*/);

		if(ret < 0)
		{
			loge("decode stream return fail.");
			bDone = 1;
			break;
		}
		else if(ret == VDECODE_RESULT_RESOLUTION_CHANGE)
		{
			logi("video resolution changed.");
			bDone = 1;
			break;
		}
		
		//* 5.4 try to get a picture from the decoder and encode it.
		if(ret == VDECODE_RESULT_FRAME_DECODED || ret == VDECODE_RESULT_KEYFRAME_DECODED)
		{
			pPicture = RequestPicture(mVideoDecoder, 0/*the major stream*/);
			if(pPicture != NULL)
			{
				bFrameCounter++;
				int enc_width  = 0;
				int enc_height = 0;
				if(bEncoderInited == 0)
				{	
					
					//* h264 param
					memset(&h264Param, 0, sizeof(VencH264Param));
					h264Param.bEntropyCodingCABAC = 1;
					h264Param.nBitrate = 150000; /* bps */
					h264Param.nCodingMode = VENC_FRAME_CODING;
					h264Param.nMaxKeyInterval = 60;
					h264Param.sProfileLevel.nProfile = VENC_H264ProfileMain;
					h264Param.sProfileLevel.nLevel = VENC_H264Level31;
					h264Param.sQPRange.nMinqp = 20;
					h264Param.sQPRange.nMaxqp = 40;
					
					VideoEncSetParameter(pVideoEncoder, VENC_IndexParamH264Param, &h264Param);

					if(pPicture->nFrameRate > 20000)
					{
						h264Param.nFramerate = (pPicture->nFrameRate/1000)>>1;
						bEncodeAllFrames = 0;
					}
					else if(pPicture->nFrameRate > 0)
					{
						h264Param.nFramerate = pPicture->nFrameRate/1000;
						bEncodeAllFrames = 1;
					}
					else
					{
						h264Param.nFramerate = 15;  /* fps */
						bEncodeAllFrames = 0;
					}


					//* init encoder config info
					memset(&sBaseEncConfig, 0 ,sizeof(VencBaseConfig));

			        if((pPicture->nBottomOffset != 0 || pPicture->nRightOffset != 0) &&
			            pPicture->nRightOffset <= pPicture->nLineStride)
			        {
			        	enc_width  = pPicture->nRightOffset - pPicture->nLeftOffset;
			        	enc_height = pPicture->nBottomOffset - pPicture->nTopOffset;
			        }
			        else
			        {
			        	enc_width  = pPicture->nWidth;
			            enc_height = pPicture->nHeight;
			        }


					sBaseEncConfig.nInputWidth= (pPicture->nWidth);
					sBaseEncConfig.nInputHeight = (pPicture->nHeight);
					sBaseEncConfig.nStride = pPicture->nLineStride;


					sBaseEncConfig.nDstWidth = 256;
					sBaseEncConfig.nDstHeight = 144;
					sBaseEncConfig.eInputFormat = VENC_PIXEL_YVU420SP;

					logd("w:%d, h:%d, stride:%d", sBaseEncConfig.nInputWidth, sBaseEncConfig.nInputHeight, pPicture->nLineStride);
					logd("ycy nRightOffset:%d, nLeftOffset:%d, nBottomOffset:%d, nTopOffset:%d", pPicture->nRightOffset, pPicture->nLeftOffset, pPicture->nBottomOffset,pPicture->nTopOffset);

					if(VideoEncInit(pVideoEncoder, &sBaseEncConfig)!=0)
					{
						loge("VideoEncInit failed");
						goto NEED_EXIT;
					}

					VideoEncGetParameter(pVideoEncoder, VENC_IndexParamH264SPSPPS, &spsppsInfo);

					
					if(spsppsInfo.pBuffer != 0 && (spsppsInfo.nLength + 8 <= MAX_OUTPUT_STREAM_SIZE))
					{
						int frameRate =  h264Param.nFramerate*1000;
						pOutBuf[bBitStreamLength++] = 't';
						pOutBuf[bBitStreamLength++] = 'h';
						pOutBuf[bBitStreamLength++] = 'u';
						pOutBuf[bBitStreamLength++] = 'm';
						
						pOutBuf[bBitStreamLength++]	= frameRate		& 0xff;
						pOutBuf[bBitStreamLength++]	= (frameRate>>8) & 0xff;
						pOutBuf[bBitStreamLength++]	= (frameRate>>16) & 0xff;
						pOutBuf[bBitStreamLength++]	= (frameRate>>24) & 0xff;
						pOutBuf[bBitStreamLength++]	= spsppsInfo.nLength	 & 0xff;
						pOutBuf[bBitStreamLength++]	= (spsppsInfo.nLength>>8) & 0xff;
						pOutBuf[bBitStreamLength++]	= (spsppsInfo.nLength>>16) & 0xff;
						pOutBuf[bBitStreamLength++]	= (spsppsInfo.nLength>>24) & 0xff;
						memcpy(pOutBuf + bBitStreamLength, (unsigned char*)spsppsInfo.pBuffer, (int)spsppsInfo.nLength);
						bBitStreamLength += spsppsInfo.nLength;

#if SAVE_BITSTREAM
						fwrite(spsppsInfo.pBuffer, 1, spsppsInfo.nLength, fph264);
#endif

					}
					else
					{
						loge("can not get sps and pps from encoder.");
						goto NEED_EXIT;
					}

					bEncoderInited = 1;
				}

				if(bEncodeAllFrames || (bFrameCounter & 1))
				{
					if((pPicture->nBottomOffset != 0 || pPicture->nRightOffset != 0) &&
			            pPicture->nRightOffset <= pPicture->nLineStride)
			        {
			        	enc_width  = pPicture->nRightOffset - pPicture->nLeftOffset;
			        	enc_height = pPicture->nBottomOffset - pPicture->nTopOffset;
			        }
			        else
			        {
			        	enc_width  = pPicture->nWidth;
			            enc_height = pPicture->nHeight;
			        }

					
					memset(&sInputBuffer, 0, sizeof(VencInputBuffer));
					sInputBuffer.pAddrPhyY = (unsigned char*)MemAdapterGetPhysicAddressCpu(pPicture->pData0);
					sInputBuffer.pAddrPhyC = (unsigned char*)MemAdapterGetPhysicAddressCpu(pPicture->pData1);
					if((enc_width <= pPicture->nWidth) || (enc_height <= pPicture->nHeight))
					{
						sInputBuffer.bEnableCorp = 1;
						sInputBuffer.sCropInfo.nWidth = enc_width;
						sInputBuffer.sCropInfo.nHeight= enc_height;
						sInputBuffer.sCropInfo.nLeft  = pPicture->nLeftOffset;
						sInputBuffer.sCropInfo.nTop   = pPicture->nTopOffset ;
					}
					

					AddOneInputBuffer(pVideoEncoder, &sInputBuffer);
					ret = VideoEncodeOneFrame(pVideoEncoder);
					if(ret != VENC_RESULT_OK)
					{	
						loge("encoder error");
						goto NEED_EXIT;
					}
					AlreadyUsedInputBuffer(pVideoEncoder,&sInputBuffer);

					memset(&sOutputBuffer, 0, sizeof(VencOutputBuffer));
					GetOneBitstreamFrame(pVideoEncoder, &sOutputBuffer);
					if((sOutputBuffer.nSize0 + sOutputBuffer.nSize1 + 4) <= MAX_OUTPUT_STREAM_SIZE)
					{
						int frameSize = sOutputBuffer.nSize0 + sOutputBuffer.nSize1;
						
						pOutBuf[bBitStreamLength++]	= frameSize & 0xff;
						pOutBuf[bBitStreamLength++]	= (frameSize>>8) & 0xff;
						pOutBuf[bBitStreamLength++]	= (frameSize>>16) & 0xff;
						pOutBuf[bBitStreamLength++]	= (frameSize>>24) & 0xff;
						memcpy(pOutBuf + bBitStreamLength, sOutputBuffer.pData0, sOutputBuffer.nSize0);

						bBitStreamLength += sOutputBuffer.nSize0;
						if(sOutputBuffer.pData1 != NULL && sOutputBuffer.nSize1 > 0)
						{
							memcpy(pOutBuf + bBitStreamLength, sOutputBuffer.pData1, sOutputBuffer.nSize1);
							bBitStreamLength += sOutputBuffer.nSize1;
						}
					}

#if SAVE_BITSTREAM
					fwrite(sOutputBuffer.pData0, 1, sOutputBuffer.nSize0, fph264);
					if(sOutputBuffer.pData1 != NULL && sOutputBuffer.nSize1 > 0)
					{
						fwrite(sOutputBuffer.pData1, 1, sOutputBuffer.nSize1, fph264);
					}
#endif
					FreeOneBitStreamFrame(pVideoEncoder, &sOutputBuffer);
				}
				
				ReturnPicture(mVideoDecoder, pPicture);
			}
		}
		
		//* check whether cost too long time or process too much packets.
		nTimePassed = GetSysTime() - nStartTime;
		if(nTimePassed > MAX_TIME_TO_GET_A_STREAM)
		{
			logw("cost more than %d us to get a steam, quit.", MAX_TIME_TO_GET_A_FRAME);
			bDone = 1;
			break;
		}

		if(bFrameCounter >= 150)
		{
			bDone = 1;
			break;
		}
		
	}while(!bDone);


EOS_EXIT:
	//* 6. copy bitstream to ashmem.
	if(pOutBuf && bBitStreamLength)
	{
	    sp<MemoryHeapBase> heap = new MemoryHeapBase(bBitStreamLength + 4, 0, "awmetadataretriever");
		unsigned char* pData = NULL;
	    if (heap == NULL)
	        loge("failed to create MemoryDealer");

	    mem = new MemoryBase(heap, 0, bBitStreamLength + 4);
	    if (mem == NULL)
	    {
	        loge("not enough memory for stream size = %d", bBitStreamLength);
	    }
	    else
	    {
		    pData = static_cast<unsigned char*>(mem->pointer());
		    pData[0] = (bBitStreamLength) & 0xff;
		    pData[1] = (bBitStreamLength>>8) & 0xff;
		    pData[2] = (bBitStreamLength>>16) & 0xff;
		    pData[3] = (bBitStreamLength>>24) & 0xff;
		    memcpy(pData+4, pOutBuf, bBitStreamLength);
	    }
	}

	logv("bBitStreamLength: %p,%d", pOutBuf, bBitStreamLength);

NEED_EXIT:
	if(pVideoEncoder)
	{
		VideoEncUnInit(pVideoEncoder);
		VideoEncDestroy(pVideoEncoder);
		pVideoEncoder = NULL;
	}

	if(pOutBuf)
	{
		free(pOutBuf);
		pOutBuf = NULL;
	}

	if(mVideoDecoder)
    {
        DestroyVideoDecoder(mVideoDecoder);
        mVideoDecoder = NULL;
    }

#if SAVE_BITSTREAM
	fclose(fph264);
	fph264 = NULL;
#endif

	return mem;
}


VideoFrame *AwMetadataRetriever::getFrameAtTime(int64_t timeUs, int option)
{
    VideoFrame*       pVideoFrame;
    VideoPicture*     pPicture;
    int               bDone;
    int               bSuccess;
    int               bHasVideo;
    int               nHorizonScaleRatio;
    int               nVerticalScaleRatio;
    enum EPIXELFORMAT ePixelFormat;
    VConfig           vconfig;
    CdxPacketT        packet;
    int               ret;
    int               nPacketCount;
    int64_t           nStartTime;
    int64_t           nTimePassed;
    
    //* FIXME:
    //* if it is a media file with drm protection, we should not return an album art picture.
#if MediaScanDedug	
	logd("getFrameAtTime, mFd=%d", mFd);
#endif	
	CEDARX_UNUSE(option);

    bDone = 0;
    bSuccess = 0;
    bHasVideo = 0;
    memset(&vconfig, 0, sizeof(VConfig));
    
    //* 1. check whether there is a video stream.
    if(mMediaInfo.programIndex >= 0 && mMediaInfo.programNum >= mMediaInfo.programIndex)
    {
        if(mMediaInfo.program[mMediaInfo.programIndex].videoNum > 0)
            bHasVideo = 1;
    }
    if(!bHasVideo)
    {
        logw("media file do not contain a video stream, getFrameAtTime() return fail.");
        return NULL;
    }
    
    //* 2. create a video decoder.
    if(mVideoDecoder == NULL)
    {
        mVideoDecoder = CreateVideoDecoder();
        //* use decoder to capture thumbnail, decoder use less memory in this mode.
        vconfig.bThumbnailMode      = 1;
        //* all decoder support YV12 format.
        vconfig.eOutputPixelFormat  = PIXEL_FORMAT_YV12;
        //* no need to decode two picture when decoding a thumbnail picture.
        vconfig.bDisable3D          = 1;

        vconfig.nAlignStride        = 16;//* set align stride to 16 as defualt
#if 1
        //* set this flag when the parser can give this info, mov files recorded by iphone or android phone
        //* conains this info.

        //* set the rotation
		int nRotateDegree;
		int nRotation = atoi((const char*)mMediaInfo.rotate);

		if(nRotation == 0)
		   nRotateDegree = 0;
		else if(nRotation == 90)
		   nRotateDegree = 1;
		else if(nRotation == 180)
		   nRotateDegree = 2;
		else if(nRotation == 270)
		   nRotateDegree = 3;
		else
		   nRotateDegree = 0;

		if(nRotateDegree != 0)
		{
			vconfig.bRotationEn         = 1;
			vconfig.nRotateDegree       = nRotateDegree;
		}else
		{
	        vconfig.bRotationEn         = 0;
	        vconfig.nRotateDegree       = 0;
		}

#endif
        //* set the picture scale down ratio, we generate a picture with pixel size less than 640x480.
        nHorizonScaleRatio  = 0;
        nVerticalScaleRatio = 0;

		if(vconfig.nRotateDegree == 1 || vconfig.nRotateDegree == 3)
		{
			if(mMediaInfo.program[mMediaInfo.programIndex].video[0].nWidth > 960 ||
				mMediaInfo.program[mMediaInfo.programIndex].video[0].nWidth  == 0) /* nWidth=0 treated as nWidth=1920 */
				nHorizonScaleRatio = 2; //* scale down to 1/4 the original width;
			else if(mMediaInfo.program[mMediaInfo.programIndex].video[0].nWidth > 480)
				nHorizonScaleRatio = 1; //* scale down to 1/2 the original width;
			if(mMediaInfo.program[mMediaInfo.programIndex].video[0].nHeight > 1280 ||
				mMediaInfo.program[mMediaInfo.programIndex].video[0].nHeight == 0) /* nHeight=0 treated as nHeight=1080 */
				nVerticalScaleRatio = 2; //* scale down to 1/4 the original height;
			else if(mMediaInfo.program[mMediaInfo.programIndex].video[0].nHeight > 640)
				nVerticalScaleRatio = 1; //* scale down to 1/2 the original height;
		}else
		{
			if(mMediaInfo.program[mMediaInfo.programIndex].video[0].nWidth > 1280 ||
				mMediaInfo.program[mMediaInfo.programIndex].video[0].nWidth  == 0) /* nWidth=0 treated as nWidth=1920 */
				nHorizonScaleRatio = 2; //* scale down to 1/4 the original width;
			else if(mMediaInfo.program[mMediaInfo.programIndex].video[0].nWidth > 640)
				nHorizonScaleRatio = 1; //* scale down to 1/2 the original width;
			if(mMediaInfo.program[mMediaInfo.programIndex].video[0].nHeight > 960 ||
				mMediaInfo.program[mMediaInfo.programIndex].video[0].nHeight == 0) /* nHeight=0 treated as nHeight=1080 */
				nVerticalScaleRatio = 2; //* scale down to 1/4 the original height;
			else if(mMediaInfo.program[mMediaInfo.programIndex].video[0].nHeight > 480)
				nVerticalScaleRatio = 1; //* scale down to 1/2 the original height;
		}

        //* set to the same scale ratio.
        if(nVerticalScaleRatio > nHorizonScaleRatio)
            nHorizonScaleRatio = nVerticalScaleRatio;
        else
            nVerticalScaleRatio = nHorizonScaleRatio;
        
        //* set scale ratio to vconfig.
        if(nHorizonScaleRatio || nVerticalScaleRatio)
        {
            vconfig.bScaleDownEn            = 1;
            vconfig.nHorizonScaleDownRatio  = nHorizonScaleRatio;
            vconfig.nVerticalScaleDownRatio = nVerticalScaleRatio;
        }
        
        //* initialize the decoder.
        vconfig.nDeInterlaceHoldingFrameBufferNum = 0;//*not use deinterlace
        vconfig.nDisplayHoldingFrameBufferNum     = 0;//*gpu and decoder not share buffer
        vconfig.nRotateHoldingFrameBufferNum      = NUM_OF_PICTURES_KEEPPED_BY_ROTATE;
        vconfig.nDecodeSmoothFrameBufferNum       = NUM_OF_PICTURES_FOR_EXTRA_SMOOTH;
        if(InitializeVideoDecoder(mVideoDecoder, &mMediaInfo.program[mMediaInfo.programIndex].video[0], &vconfig) != 0)
        {
            loge("initialize video decoder fail.");
            return NULL;
        }
    }
    
    if(timeUs < 0)
    {
		//The key frame of MKV always at the end of file, need reset to 0, 
		//otherwise will return mix thumbnail
		if(mParser->type == CDX_PARSER_MKV)
		{
			timeUs = 0;
		}
		else
		{
			timeUs = ((int64_t)mMediaInfo.program[mMediaInfo.programIndex].duration*1000/2);
		}
    }

 	if(mMediaInfo.program[mMediaInfo.programIndex].duration < 30000)
 	{
 		timeUs = 0;
 	}

    //* 3. seek parser to the specific position.
    if(mMediaInfo.bSeekable && timeUs > 0 && timeUs < ((int64_t)mMediaInfo.program[mMediaInfo.programIndex].duration*1000))
    {
        //* FIXME.
        //* we should seek to a position according to the 'option' param.
        //* option = 0 means seek to the sync frame privious to the timeUs.
        //* option = 1 means seek to the sync frame next to the timeUs.
        //* option = 2 means seek to the sync frame closest to the timeUs.
        //* option = 3 means seek to the closest frame to the timeUs.
        //* here we process all case as option = 0.
        if(CdxParserSeekTo(mParser, timeUs) != 0)
        {
            loge("can not seek media file to the specific time %lld us.", timeUs);
            return NULL;
        }
    }
    else
    {
        logw("media file do not support seek operation, get frame from the begin.");
    }
    
    //* 4. loop to decode a picture.
    nPacketCount = 0;
    nStartTime   = GetSysTime();
    do
    {
        //* 4.1 prefetch packet type and packet data size.
    	packet.flags = 0;
        if(CdxParserPrefetch(mParser, &packet) != 0)
        {
            //* prefetch fail, may be file end reached.
            bDone = 1;
            bSuccess = 0;
            break;
        }
        
        //* 4.2 feed data to the video decoder.
        if(packet.type == CDX_MEDIA_VIDEO && (packet.flags&MINOR_STREAM)==0)
        {
            ret = RequestVideoStreamBuffer(mVideoDecoder, 
                                           packet.length, 
                                           (char**)&packet.buf, 
                                           &packet.buflen,
                                           (char**)&packet.ringBuf,
                                           &packet.ringBufLen,
                                           0);
            
            if(ret==0 && (packet.buflen+packet.ringBufLen)>=packet.length)
            {
                nPacketCount++;
                if(CdxParserRead(mParser, &packet) == 0)
                {
                    VideoStreamDataInfo dataInfo;
                    dataInfo.pData        = (char*)packet.buf;
                    dataInfo.nLength      = packet.length;
                    dataInfo.nPts         = packet.pts;
                    dataInfo.nPcr         = -1;
                    dataInfo.bIsFirstPart = 1;
                    dataInfo.bIsLastPart  = 1;
                    dataInfo.nStreamIndex = 0;
                    SubmitVideoStreamData(mVideoDecoder, &dataInfo, 0);
                }
                else
                {
                    //* read data fail, may be data error.
                    loge("read packet from parser fail.");
                    bDone = 1;
                    bSuccess = 0;
                    break;
                }
            }
            else
            {
                //* no buffer, may be the decoder is full of stream.
                logw("waiting for stream buffer.");
            }
        }
        else
        {
            //* only process the major video stream.
            //* allocate a buffer to read uncare media data and skip it.
            packet.buf = malloc(packet.length);
            if(packet.buf != NULL)
            {
                nPacketCount++;
                packet.buflen     = packet.length;
                packet.ringBuf    = NULL;
                packet.ringBufLen = 0;
                if(CdxParserRead(mParser, &packet) == 0)
                {
                    free(packet.buf);
                    continue;
                }
                else
                {
                	free(packet.buf);
					
                    //* read data fail, may be data error.
                    loge("read packet from parser fail.");
                    bDone = 1;
                    bSuccess = 0;
                    break;
                }
            }
            else
            {
                loge("can not allocate buffer for none video packet.");
                bDone = 1;
                bSuccess = 0;
                break;
            }
        }
        
        //* 4.3 decode stream.
        ret = DecodeVideoStream(mVideoDecoder, 0 /*eos*/, 0/*key frame only*/, 0/*drop b frame*/, 0/*current time*/);

		if(ret < 0)
        {
            loge("decode stream return fail.");
            bDone = 1;
            bSuccess = 0;
            break;
        }
        else if(ret == VDECODE_RESULT_RESOLUTION_CHANGE)
        {
            logi("video resolution changed.");
            ReopenVideoEngine(mVideoDecoder, &vconfig, &mMediaInfo.program[mMediaInfo.programIndex].video[0]);
            continue;
        }
        
        //* 4.4 try to get a picture from the decoder.
        if(ret == VDECODE_RESULT_FRAME_DECODED || ret == VDECODE_RESULT_KEYFRAME_DECODED)
        {
            pPicture = RequestPicture(mVideoDecoder, 0/*the major stream*/);
            if(pPicture != NULL)
            {
                bDone = 1;
                bSuccess = 1;
                break;
            }
        }
        
        //* check whether cost too long time or process too much packets.
        nTimePassed = GetSysTime() - nStartTime;
        if(nTimePassed > MAX_TIME_TO_GET_A_FRAME || nTimePassed < 0)
        {
            logw("cost more than %d us but can not get a picture, quit.", MAX_TIME_TO_GET_A_FRAME);
            bDone = 1;
            bSuccess = 0;
            break;
        }
        if(nPacketCount > MAX_PACKET_COUNT_TO_GET_A_FRAME)
        {
            logw("process more than %d packets but can not get a picture, quit.", MAX_PACKET_COUNT_TO_GET_A_FRAME);
            bDone = 1;
            bSuccess = 0;
            break;
        }
    }while(!bDone);
    
    //* 5. transform the picture if suceess to get a picture.
    if(bSuccess)
    {
        pVideoFrame = new VideoFrame;
        if(pVideoFrame == NULL)
        {
            loge("can not allocate memory for video frame.");
            return NULL;
        }
        
        //* let the width and height is multiple of 2, for convinient of yuv to rgb565 converting.
        if(pPicture->nLeftOffset & 1)
            pPicture->nLeftOffset += 1;
        if(pPicture->nRightOffset & 1)
            pPicture->nRightOffset -= 1;
        if(pPicture->nTopOffset & 1)
            pPicture->nTopOffset += 1;
        if(pPicture->nBottomOffset & 1)
            pPicture->nBottomOffset -= 1;
        
        //* I find that the mpeg2 decoder output the original picture's crop params, 
        //* it is bigger than the scaledown picture's size.
        if((pPicture->nBottomOffset != 0 || pPicture->nRightOffset != 0) &&
            pPicture->nRightOffset <= pPicture->nLineStride)
        {
            pVideoFrame->mDisplayWidth  = pPicture->nRightOffset - pPicture->nLeftOffset;
            pVideoFrame->mDisplayHeight = pPicture->nBottomOffset - pPicture->nTopOffset;
            pVideoFrame->mWidth         = pVideoFrame->mDisplayWidth;
            pVideoFrame->mHeight        = pVideoFrame->mDisplayHeight;
        }
        else
        {
            pVideoFrame->mDisplayWidth  = pPicture->nWidth;
            pVideoFrame->mDisplayHeight = pPicture->nHeight;
            pVideoFrame->mWidth         = pPicture->nWidth;
            pVideoFrame->mHeight        = pPicture->nHeight;
        }
        
        pVideoFrame->mSize = pVideoFrame->mWidth * pVideoFrame->mHeight * 2;    //* for RGB565 pixel format.
        pVideoFrame->mData = new unsigned char[pVideoFrame->mSize];
        if(pVideoFrame->mData == NULL)
        {
            loge("can not allocate memory for video frame.");
            delete pVideoFrame;
            return NULL;
        }
        pVideoFrame->mRotationAngle = 0;
        
        //* convert pixel format.
        
        if(transformPicture(pPicture, pVideoFrame) < 0)
        {
            delete pVideoFrame;
            return NULL;
        }

        return pVideoFrame;
    }
    else
	{
		loge("cannot decode a picture.");
        return NULL;
	}
}



static int setDataSourceFields(CdxDataSourceT* source, char* uri, KeyedVector<String8,String8>* pHeaders)
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


static int64_t GetSysTime()
{
    int64_t time;
    struct timeval t;
    gettimeofday(&t, NULL);
    time = (int64_t)t.tv_sec * 1000000;
    time += t.tv_usec;
    return time;
}


static int transformPicture(VideoPicture* pPicture, VideoFrame* pVideoFrame)
{
    unsigned short*  pDst;
    unsigned char*   pSrcY;
    unsigned char*   pSrcU;
    unsigned char*   pSrcV;
    int              y;
    int              x;
    unsigned char*   pClipTable;
    unsigned char*   pClip;
    
    static const int nClipMin = -278;
    static const int nClipMax = 535;
    
    if((pPicture->ePixelFormat!= PIXEL_FORMAT_YV12) &&
    		(pPicture->ePixelFormat!= PIXEL_FORMAT_YUV_PLANER_420))
    {
        loge("source pixel format is not YV12, quit.");
        return -1;
    }
    
    //* initialize the clip table.
    pClipTable = (unsigned char*)malloc(nClipMax - nClipMin + 1);
    if(pClipTable == NULL)
    {
        loge("can not allocate memory for the clip table, quit.");
        return -1;
    }
    for(x=nClipMin; x<=nClipMax; x++)
    {
        pClipTable[x-nClipMin] = (x<0) ? 0 : (x>255) ? 255 : x;
    }
    pClip = &pClipTable[-nClipMin];

    //* flush cache.
    MemAdapterFlushCache(pPicture->pData0, pPicture->nLineStride*pPicture->nHeight);
    MemAdapterFlushCache(pPicture->pData1, pPicture->nLineStride*pPicture->nHeight/4);
    MemAdapterFlushCache(pPicture->pData2, pPicture->nLineStride*pPicture->nHeight/4);
    
    //* set pointers.
    pDst  = (unsigned short*)pVideoFrame->mData;
    pSrcY = (unsigned char*)pPicture->pData0 + pPicture->nTopOffset * pPicture->nLineStride + pPicture->nLeftOffset;


    if(pPicture->ePixelFormat== PIXEL_FORMAT_YV12)
    {
    	pSrcV = (unsigned char*)pPicture->pData1 + (pPicture->nTopOffset/2) * (pPicture->nLineStride/2) + pPicture->nLeftOffset/2;
    	pSrcU = (unsigned char*)pPicture->pData2 + (pPicture->nTopOffset/2) * (pPicture->nLineStride/2) + pPicture->nLeftOffset/2;
    }
    else
    {
    	pSrcU = (unsigned char*)pPicture->pData1 + (pPicture->nTopOffset/2) * (pPicture->nLineStride/2) + pPicture->nLeftOffset/2;
        pSrcV = (unsigned char*)pPicture->pData2 + (pPicture->nTopOffset/2) * (pPicture->nLineStride/2) + pPicture->nLeftOffset/2;
    }

    for(y = 0; y < (int)pVideoFrame->mHeight; ++y)
    {
        for(x = 0; x < (int)pVideoFrame->mWidth; x += 2)
        {
            // B = 1.164 * (Y - 16) + 2.018 * (U - 128)
            // G = 1.164 * (Y - 16) - 0.813 * (V - 128) - 0.391 * (U - 128)
            // R = 1.164 * (Y - 16) + 1.596 * (V - 128)

            // B = 298/256 * (Y - 16) + 517/256 * (U - 128)
            // G = .................. - 208/256 * (V - 128) - 100/256 * (U - 128)
            // R = .................. + 409/256 * (V - 128)

            // min_B = (298 * (- 16) + 517 * (- 128)) / 256 = -277
            // min_G = (298 * (- 16) - 208 * (255 - 128) - 100 * (255 - 128)) / 256 = -172
            // min_R = (298 * (- 16) + 409 * (- 128)) / 256 = -223

            // max_B = (298 * (255 - 16) + 517 * (255 - 128)) / 256 = 534
            // max_G = (298 * (255 - 16) - 208 * (- 128) - 100 * (- 128)) / 256 = 432
            // max_R = (298 * (255 - 16) + 409 * (255 - 128)) / 256 = 481

            // clip range -278 .. 535

            signed y1 = (signed)pSrcY[x] - 16;
            signed y2 = (signed)pSrcY[x + 1] - 16;

            signed u = (signed)pSrcU[x / 2] - 128;
            signed v = (signed)pSrcV[x / 2] - 128;

            signed u_b_val = u * 517;
            signed u_g_val = -u * 100;
            signed v_g_val = -v * 208;
            signed v_r_val = v * 409;

            signed tmp_y1 = y1 * 298;
            signed b1_index = (tmp_y1 + u_b_val) / 256;
            signed g1_index = (tmp_y1 + v_g_val + u_g_val) / 256;
            signed r1_index = (tmp_y1 + v_r_val) / 256;

            signed tmp_y2 = y2 * 298;
            signed b2_index = (tmp_y2 + u_b_val) / 256;
            signed g2_index = (tmp_y2 + v_g_val + u_g_val) / 256;
            signed r2_index = (tmp_y2 + v_r_val) / 256;

            unsigned int rgb1 = ((pClip[r1_index] >> 3) << 11) | 
                                ((pClip[g1_index] >> 2) << 5)  |
                                ( pClip[b1_index] >> 3);

            unsigned int rgb2 = ((pClip[r2_index] >> 3) << 11) | 
                                ((pClip[g2_index] >> 2) << 5)  |
                                ( pClip[b2_index] >> 3);

            *(unsigned int *)(&pDst[x]) = (rgb2 << 16) | rgb1;
        }

        pSrcY += pPicture->nLineStride;

        if(y & 1)
        {
            pSrcU += pPicture->nLineStride / 2;
            pSrcV += pPicture->nLineStride / 2;
        }

        pDst += pVideoFrame->mWidth;
    }
    
    free(pClipTable);

    return 0;
}

static int transfromId3Info(String8* mStringId3, cdx_uint8* pData, cdx_int32 nSize, cdx_int32 nEncodeTpye,cdx_int32 flag)
{
    if(pData == NULL || nSize == 0)
    {
        logd("*** transfromId3Info failed, pData = %p, size = %d",pData,nSize);
        return -1;
    }
    
#if( CONFIG_OS_VERSION >= OPTION_OS_VERSION_ANDROID_5_0)    
    cdx_int32   encoding     = nEncodeTpye;
    size_t      nDataLen     = nSize;
    const char* name         = NULL;
    const char* pDataConvert = NULL;

    mStringId3->setTo("");

    if (encoding == 0x00) 
    {
        // supposedly ISO 8859-1
        mStringId3->setTo((const char*)(pData), nDataLen);
    } 
    else if (encoding == 0x03) 
    {
        // supposedly UTF-8
        mStringId3->setTo((const char *)(pData), nDataLen);
    } 
    else if (encoding == 0x02) 
    {
        // supposedly UTF-16 BE, no byte order mark.
        // API wants number of characters, not number of bytes...
        int len = nDataLen / 2;
        const char16_t *framedata = (const char16_t *) (pData);
        mStringId3->setTo(framedata, len);
    } 
    else if (encoding == 0x01) 
    {
        // UCS-2
        // API wants number of characters, not number of bytes...
        int len = nDataLen / 2;
        const char16_t *framedata = (const char16_t *) (pData);

        // check if the resulting data consists entirely of 8-bit values
        bool eightBit = true;
        for (int i = 0; i < len; i++) 
        {
            if (framedata[i] > 0xff) 
            {
                eightBit = false;
                break;
            }
        }
        
        if (eightBit) 
        {
            // collapse to 8 bit, then let the media scanner client figure out the real encoding
            char *frame8 = new char[len];
            for (int i = 0; i < len; i++) 
            {
                frame8[i] = framedata[i];
            }
            mStringId3->setTo(frame8, len);
            delete [] frame8;
        } 
        else 
        {
            mStringId3->setTo(framedata, len);
        }
    }
	cdx_int32 idx = 0;
    logd("** pDataConvert before = %s",mStringId3->string());
#if 1
    CharacterEncodingDetector *pEcodingdetector = new CharacterEncodingDetector();
	for(idx = 0; idx < kNumMapEntries; idx++)
	{
		if(flag == kMap[idx].from)
		{
			if(kMap[idx].name != NULL)
			{
				pEcodingdetector->addTag(kMap[idx].name, (const char*)mStringId3->string());
			}
		}
	}
	pEcodingdetector->detectAndConvert();
	int nDetectorSize = pEcodingdetector->size();
    if (nDetectorSize) {
        for (int i = 0; i < nDetectorSize; i++) {
            const char *pName;
            const char *pValue;
            pEcodingdetector->getTag(i, &pName, &pValue);
            for (cdx_int32 j = 0; j < kNumMapEntries; ++j) {
                if (kMap[j].name && !strcmp(kMap[j].name, pName)) {
                    //mMetaData.add(kMap[j].to, String8(value));
					mStringId3->setTo("");
					mStringId3->setTo(pValue,strlen(pValue));
                }
            }
        }
    }   
	delete pEcodingdetector;
	
#endif	
	logd("** pDataConvert finish = %s",mStringId3->string());
#else
    cdx_int32 encoding = nEncodeTpye;
    size_t nDataLen = nSize;
    UErrorCode status = U_ZERO_ERROR;
    String8 srcString;
    
    srcString.setTo("");
    
    if(encoding == 0x00) //why gbk come here ?
    {
    	// supposedly ISO 8859-1
    	srcString.setTo((const char*)(pData), nDataLen);
    }
    else if(encoding == 0x03)
    {
   	 	// supposedly UTF-8
    	srcString.setTo((const char *)(pData), nDataLen);
    }
    else if(encoding == 0x02)
    {
	    // supposedly UTF-16 BE, no byte order mark.
	    // API wants number of characters, not number of bytes...
	    int len = nDataLen / 2;
	    const uint16_t *framedata = (const uint16_t *) (pData);
	
	    srcString.setTo(framedata, len);
    }
    else if(encoding == 0x01)
	{
	    // UCS-2
	    // API wants number of characters, not number of bytes...
	    int len = nDataLen / 2;
	    const uint16_t *framedata = (const uint16_t *) (pData);
	    
	    // check if the resulting data consists entirely of 8-bit values
	    bool eightBit = true;
	    for(int i = 0; i < len; i++)
	    {
	    	if(framedata[i] > 0xff)
		    {
			    eightBit = false;
			    break;
		    }
	    }
	    
	    if(eightBit)
	    {
	    	// collapse to 8 bit, then let the media scanner client figure out the real encoding
	    	char *frame8 = new char[len];
	    	for(int i = 0; i < len; i++)
		    {
		    	frame8[i] = framedata[i];
		    }
	    	srcString.setTo(frame8, len);
	    	delete [] frame8;
	    }
	    else
	    {
	    	srcString.setTo(framedata, len);
	    }
	}
	
    logd("convert before = %s, nDataLen %d",srcString.string(), nDataLen);
    
    //*first we need to untangle the utf8 and convert it back to the original bytes
    // since we are reducing the length of the string, we can do this in place
    
    //*now convert from native encoding to UTF-8
    if(0x00 == nEncodeTpye)
    {
    	const char* gbkSrc = (char*)srcString.string();
    	int gbkLen = nDataLen;
    	int utf8Len = gbkLen * 3 + 1; //TODO
    	char* utf8Dst = (char*)malloc(utf8Len);
    	memset(utf8Dst, 0, utf8Len);

        char value[256];
        const char *srcEncoding;
        property_get("persist.sys.language", value, "");
        if (strncmp(value, "ko", 256) == 0)
        {
            srcEncoding = "EUC-KR";
        }
        else /* default set to GBK */
        {
            srcEncoding = "GBK";
        }
    	
        logd("ucnv_convert, nEncodeTpye 0x%02x, using %s", nEncodeTpye, srcEncoding);
    	
    	ucnv_convert("UTF-8",
		    srcEncoding,
		    utf8Dst,
		    utf8Len,
		    gbkSrc,
		    gbkLen,
		    &status);
		    
	    if(U_FAILURE(status))
	    {
		    logw("ucnv_convertEx failed: %d\n", status);
	    }
	    
	    logd("convert %s to utf-8 src = %s, dst = %s\n", srcEncoding, gbkSrc, utf8Dst);
	    
	    mStringId3->setTo("");
	    mStringId3->setTo((const char*)utf8Dst);
    }
    else if(0x01 == nEncodeTpye)
    {
	    logd("no conversion, nEncodeTpye 0x%02x", nEncodeTpye);
	    mStringId3->setTo("");
	    mStringId3->setTo((const char*)srcString.string());
    }
    else
    {
	    logd("no implement, nEncodeTpye 0x%02x", nEncodeTpye);
	    mStringId3->setTo("");
	    mStringId3->setTo((const char*)srcString.string());
    }
#endif
    
    return 0;
}
