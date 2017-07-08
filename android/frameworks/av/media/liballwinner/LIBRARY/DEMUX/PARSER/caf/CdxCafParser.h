#ifndef CDX_ALAC_PARSER_H
#define CDX_ALAC_PARSER_H

#include <CdxTypes.h>

#define CAF_TARGET_RT_LITTLE_ENDIAN 1
#define kMinCAFFPacketTableHeaderSize 24

#define kMaxBERSize 5
#define kCAFFdataChunkEditsSize  4

#define kWAVERIFFChunkSize 12
#define kWAVEfmtChunkSize 24
#define kWAVEdataChunkHeaderSize 8

#define VERBOSE 0


enum
{
    kALACFormatAppleLossless = 'alac',
    kALACFormatLinearPCM = 'lpcm'
};


typedef struct _CafDescription
{
    double   	mSampleRate;
    cdx_uint32  mFormatID;
    cdx_uint32  mFormatFlags;
    cdx_uint32  mBytesPerPacket;
    cdx_uint32  mFramesPerPacket;
    cdx_uint32  mBytesPerFrame;
    cdx_uint32  mChannelsPerFrame;
    cdx_uint32  mBitsPerChannel;
    cdx_uint32  mReserved;
}CafDescription;

typedef struct _port_CAFAudioDescription
{
    double	  mSampleRate;
    uint32_t  mFormatID;
    uint32_t  mFormatFlags;
    uint32_t  mBytesPerPacket;
    uint32_t  mFramesPerPacket;
    uint32_t  mChannelsPerFrame;
    uint32_t  mBitsPerChannel;
}port_CAFAudioDescription;


typedef struct CafParserImplS
{
  //audio common
  CdxParserT base;
  CdxStreamT *stream;   
  cdx_int64  ulDuration;//ms    
  pthread_cond_t  cond;
  cdx_int64  fileSize;//total file length
  cdx_int64  file_offset; //now read location 
  cdx_int32  mErrno; //Parser Status
  cdx_uint32 flags; //cmd
  //caf base
  cdx_uint32 *theIndexBuffer;
  cdx_int32 framecount;
  cdx_int32 ulSampleRate ;
  cdx_int32 ulChannels ;
  cdx_int32 ulBitsSample ;
  cdx_int32 ulBitRate;
  cdx_int32 totalsamples;
  cdx_int32 nowsamples;
  cdx_int32 packetTablePos;
  cdx_int32 thePacketTableSize;
  cdx_int32 firstpktpos;
  cdx_uint32 sampleperpkt;
	cdx_int8 *extradata;
	cdx_int32 extradata_size;
  //meta data
  //not imply yet
}CafParserImplS;


#endif
