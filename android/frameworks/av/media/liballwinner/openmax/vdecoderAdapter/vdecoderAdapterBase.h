
#ifndef VDECODER_ADAPTER_BASE_H
#define VDECODER_ADAPTER_BASE_H

#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include "typedef.h"
#include "vdecoder.h"
#include "sbm.h"

#define MAX_BUFFER_NUM (32)

#define DEBUG_QCOM_SAVE_BITSTREAM (0)
#define DEBUG_QCOM_SAVE_PICTURE (0)

#define OMX_StateReopen_Add 0x100


#define WIDTH_1080P (1920+100)
#define HEIGHT_1080P (1080+100)
#define WIDTH_MAX  (1080*10)
#define HEIGHT_MAX (1080*10)
#define MAX_SURPPORT_RESOLUTION_WIDTH   WIDTH_MAX
#define MAX_SURPPORT_RESOLUTION_HEIGHT  HEIGHT_MAX

const int QCOM_MESSAGE_ID_FREE_OUT_BUFFER   = 0x01;
const int QCOM_MESSAGE_ID_QUIT              = 0x02;


//* for message
typedef void* MessageQueue;

typedef struct Message
{
    int          messageId;
    uintptr_t    params[4];
}Message;

typedef struct MessageNode
{
    Message      msg;
    MessageNode* next;
    int          valid;
}MessageNode;

typedef struct MessageQueueContext
{
    char*           pName;
    MessageNode*    pHead;
    int             nCount;
    MessageNode*    Nodes;
    int             nMaxMessageNum;
    pthread_mutex_t mutex;
    sem_t           sem;
}MessageQueueContext;
//* end(for message)

enum 
{
    kPortIndexInput  = 0,
    kPortIndexOutput = 1
};

enum BufferStatus
{
    OWN_BY_RENDER     = 1,
    OWN_BY_US         = 2,
    OWN_BY_COMPONENT  = 4,
};

typedef struct OmxInBufferNode
{
    OMX_BUFFERHEADERTYPE* pBufferInfo;
    OmxInBufferNode* pNext;
    enum BufferStatus eStatus;
}OmxInBufferNode;

typedef struct OmxOutBufferNode
{
    OMX_BUFFERHEADERTYPE* pBufferInfo;
    VideoPicture mVideoPicture; //* to transformat 
                                //* OMX_BUFFERHEADERTYPE to VideoPicture
    OmxOutBufferNode* pNext;
    enum BufferStatus eStatus; 
}OmxOutBufferNode;

typedef struct CodecFormatToName
{
    enum EVIDEOCODECFORMAT eCodecFormat;
    int mVideoCodingType;
    const char* pComponentName;
    const char* pComponentRole;
}CodecFormatToName;

typedef struct VdecoderAdapterBaseContext
{
    
    aw_omx_component* pOmx;
    
    int eOmxVideoCodingType;

    int nVideoWidth;
    int nVideoHeight;
    int eColorFormat;
    int eCodecFormat;
    int nAllocateOutBufferNum;
    int mFlushComplete[2];
    int nOutBufferSize;

    FbmBufInfo mFbmBufInfo;

    OmxInBufferNode   mInBufferNode[MAX_BUFFER_NUM];
    OmxOutBufferNode  mOutBufferNode[MAX_BUFFER_NUM];
    OmxInBufferNode*  pInBufferNodeHeader;
    OmxOutBufferNode* pOutBufferNodeHeader;

    pthread_mutex_t mInBufferMutex;
    pthread_mutex_t mOutBufferMutex;

    sem_t           mFlushSem;
    sem_t           mLoadToIdleSem;
    sem_t           mIdleToExecuteSem;
    sem_t           mExecuteToIdleSem;
    sem_t           mIdleToLoadSem;
    sem_t           mDisablePortSem;

    int     bSendSpecificDataFlag;
    int     nSpecificDataLen;
    char*   pSpecificData;
    int     bNeedUpdataDisplayCropFlag;
    int     bFlushPending;
    int     bOutPortSettingChangeFlag;

    pthread_t sNativeThread;
    MessageQueue* mq;
    

    OMX_CONFIG_RECTTYPE mDisplayCrop;

    int             mState;
    Sbm*            pSbm[2];
    VideoStreamDataInfo partialStreamDataInfo[2];

    VConfig          mConfig;
    void*            pHComponent;

	CodecFormatToName* pKCodecFormatToName;
	int	 			   CodecFormatToNameNum;

	int				   nInEosFlag;
	int 			   nOutEosFlag;

	#if DEBUG_QCOM_SAVE_BITSTREAM
	FILE* fpStream ;
	#endif

	#if DEBUG_QCOM_SAVE_PICTURE
	int nSavePicNum;
	#endif
    
}VdecoderAdapterBaseContext;

typedef struct VdecoderAdapterBaseOpsS
{
    VideoDecoder* (*AdbCreateVideoDecoder)(void);

    int (*AdbInitializeVideoDecoder)(VideoDecoder*, VideoStreamInfo*, VConfig*);

    void (*AdbDestroyVideoDecoder)(VideoDecoder*);

    void (*AdbResetVideoDecoder)(VideoDecoder*);

    int (*AdbDecodeVideoStream)(VideoDecoder*,int,int,int,int64_t);

    int (*AdbDecoderSetSpecialData)(VideoDecoder*, void *);

    int (*AdbGetVideoStreamInfo)(VideoDecoder*, VideoStreamInfo*);

    int (*AdbRequestVideoStreamBuffer)(VideoDecoder*,int,char**,int*,char**,int*,int);

    int (*AdbSubmitVideoStreamData)(VideoDecoder*,VideoStreamDataInfo*,int);

    int (*AdbVideoStreamBufferSize)(VideoDecoder*, int);

    int (*AdbVideoStreamDataSize)(VideoDecoder*, int);

    int (*AdbVideoStreamFrameNum)(VideoDecoder*, int);
    void* (*AdbVideoStreamDataInfoPointer)(VideoDecoder*, int);


    VideoPicture* (*AdbRequestPicture)(VideoDecoder*, int);

    int (*AdbReturnPicture)(VideoDecoder*, VideoPicture*);

    VideoPicture* (*AdbNextPictureInfo)(VideoDecoder*, int);

    int (*AdbTotalPictureBufferNum)(VideoDecoder*, int);

    int (*AdbEmptyPictureBufferNum)(VideoDecoder*, int);

    int (*AdbValidPictureNum)(VideoDecoder*, int);

    int (*AdbConfigHorizonScaleDownRatio)(VideoDecoder*, int);

    int (*AdbConfigVerticalScaleDownRatio)(VideoDecoder*, int);

    int (*AdbConfigRotation)(VideoDecoder*, int);

    int (*AdbConfigDeinterlace)(VideoDecoder*, int);

    int (*AdbConfigThumbnailMode)(VideoDecoder*, int);

    int (*AdbConfigOutputPicturePixelFormat)(VideoDecoder*, int);

    int (*AdbConfigNoBFrames)(VideoDecoder*, int);

    int (*AdbConfigDisable3D)(VideoDecoder*, int);

    int (*AdbConfigVeMemoryThresh)(VideoDecoder*, int);

    int (*AdbReopenVideoEngine)(VideoDecoder*, VConfig*, VideoStreamInfo*);

    int (*AdbRotatePicture)(VideoPicture*,VideoPicture*,int,int,int);

    int (*AdbRotatePictureHw)(VideoDecoder*,VideoPicture*,VideoPicture*,int);

    VideoPicture* (*AdbAllocatePictureBuffer)(int, int, int, int);

    int (*AdbFreePictureBuffer)(VideoPicture*);

    char* (*AdbVideoRequestSecureBuffer)(VideoDecoder*,int);

    void (*AdbVideoReleaseSecureBuffer)(VideoDecoder*,char*);

    VideoPicture*  (*AdbReturnRelasePicture)(VideoDecoder*, VideoPicture*, int);

    VideoPicture* (*AdbRequestReleasePicture)(VideoDecoder*);

    int (*AdbSetVideoFbmBufRelease)(VideoDecoder*);

    VideoPicture* (*AdbSetVideoFbmBufAddress)(VideoDecoder*, VideoPicture*,int);

    FbmBufInfo* (*AdbGetVideoFbmBufInfo)(VideoDecoder*);

    int (*AdbSetGpuBufferNum)(VideoDecoder*, int);

	int (*AdbSetInEosFlag)(VideoDecoder*, int);

	int (*AdbGetOutEosFlag)(VideoDecoder*);

	//*private

	int (*AdbSetCodecFormatToName)(VideoDecoder*, const CodecFormatToName* ,int);

}VdecoderAdapterBaseOpsT;

VideoDecoder* AdbCreateVideoDecoder(void);

void AdbDestroyVideoDecoder(VideoDecoder* pDecoder);

int AdbInitializeVideoDecoder(VideoDecoder* pDecoder, VideoStreamInfo* pVideoInfo, VConfig* pVconfig);


void AdbResetVideoDecoder(VideoDecoder* pDecoder);

int AdbDecodeVideoStream(VideoDecoder* pDecoder, 
                      int           bEndOfStream,
                      int           bDecodeKeyFrameOnly,
                      int           bDropBFrameIfDelay,
                      int64_t       nCurrentTimeUs);

int AdbDecoderSetSpecialData(VideoDecoder* pDecoder, void *pArg);

int AdbGetVideoStreamInfo(VideoDecoder* pDecoder, VideoStreamInfo* pVideoInfo);

int AdbRequestVideoStreamBuffer(VideoDecoder* pDecoder,
                             int           nRequireSize,
                             char**        ppBuf,
                             int*          pBufSize,
                             char**        ppRingBuf,
                             int*          pRingBufSize,
                             int           nStreamBufIndex);

int AdbSubmitVideoStreamData(VideoDecoder*        pDecoder,
                          VideoStreamDataInfo* pDataInfo,
                          int                  nStreamBufIndex);

int AdbVideoStreamBufferSize(VideoDecoder* pDecoder, int nStreamBufIndex);

int AdbVideoStreamDataSize(VideoDecoder* pDecoder, int nStreamBufIndex);

int AdbVideoStreamFrameNum(VideoDecoder* pDecoder, int nStreamBufIndex);
void* AdbVideoStreamDataInfoPointer(VideoDecoder* pDecoder, int nStreamBufIndex);


VideoPicture* AdbRequestPicture(VideoDecoder* pDecoder, int nStreamIndex);

int AdbReturnPicture(VideoDecoder* pDecoder, VideoPicture* pPicture);

VideoPicture* AdbNextPictureInfo(VideoDecoder* pDecoder, int nStreamIndex);

int AdbTotalPictureBufferNum(VideoDecoder* pDecoder, int nStreamIndex);

int AdbEmptyPictureBufferNum(VideoDecoder* pDecoder, int nStreamIndex);

int AdbValidPictureNum(VideoDecoder* pDecoder, int nStreamIndex);

int AdbConfigHorizonScaleDownRatio(VideoDecoder* pDecoder, int nScaleDownRatio);

int AdbConfigVerticalScaleDownRatio(VideoDecoder* pDecoder, int nScaleDownRatio);

int AdbConfigRotation(VideoDecoder* pDecoder, int nRotateDegree);

int AdbConfigDeinterlace(VideoDecoder* pDecoder, int bDeinterlace);

int AdbConfigThumbnailMode(VideoDecoder* pDecoder, int bOpenThumbnailMode);

int AdbConfigOutputPicturePixelFormat(VideoDecoder* pDecoder, int ePixelFormat);

int AdbConfigNoBFrames(VideoDecoder* pDecoder, int bNoBFrames);

int AdbConfigDisable3D(VideoDecoder* pDecoder, int bDisable3D);

int AdbConfigVeMemoryThresh(VideoDecoder* pDecoder, int nMemoryThresh);

int AdbReopenVideoEngine(VideoDecoder* pDecoder, VConfig* pVConfig, VideoStreamInfo* pStreamInfo);

int AdbRotatePicture(VideoPicture* pPictureIn, 
                  VideoPicture* pPictureOut, 
                  int           nRotateDegree,
                  int           nGpuYAlign,
                  int           nGpuCAlign);

int AdbRotatePictureHw(VideoDecoder* pDecoder,
                    VideoPicture* pPictureIn, 
                    VideoPicture* pPictureOut, 
                    int           nRotateDegree);

VideoPicture* AdbAllocatePictureBuffer(int nWidth, int nHeight, int nLineStride, int ePixelFormat);

int AdbFreePictureBuffer(VideoPicture* pPicture);

char* AdbVideoRequestSecureBuffer(VideoDecoder* pDecoder,int nBufferSize);

void AdbVideoReleaseSecureBuffer(VideoDecoder* pDecoder,char* pBuf);

VideoPicture*  AdbReturnRelasePicture(VideoDecoder* pDecoder, VideoPicture* pVpicture, int bForbidUseFlag);

VideoPicture* AdbRequestReleasePicture(VideoDecoder* pDecoder);

int AdbSetVideoFbmBufRelease(VideoDecoder* pDecoder);

VideoPicture* AdbSetVideoFbmBufAddress(VideoDecoder* pDecoder, VideoPicture* pVideoPicture,int bForbidUseFlag);

FbmBufInfo* AdbGetVideoFbmBufInfo(VideoDecoder* pDecoder);

int AdbSetGpuBufferNum(VideoDecoder* pDecoder, int nNum);

int AdbSetInEosFlag(VideoDecoder* pDecoder, int nEosFlag);

int AdbGetOutEosFlag(VideoDecoder* pDecoder);

int AdbSetCodecFormatToName(VideoDecoder*, const CodecFormatToName* pCodecFormatToName, int nNum);

#endif



