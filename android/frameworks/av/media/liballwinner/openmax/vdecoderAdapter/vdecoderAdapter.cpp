
//#define CONFIG_LOG_LEVEL    OPTION_LOG_LEVEL_DETAIL
#define LOG_TAG "vdecoderAdapter"
#include <pthread.h>
#include <utils/Vector.h>
#include <dlfcn.h>           // dynamic library
#include <semaphore.h>
#include <cutils/properties.h>

#include "HardwareAPI.h"
#include "log.h"
#include "OMX_Core.h"
#include "aw_omx_component.h"
#include "include/vdecoderAdapter.h"
#include "vdecoderAdapterBase.h"

typedef struct VdecoderAdapterContext
{
    
    VdecoderAdapterBaseContext* pAdatpterBaseContext;
    VdecoderAdapterBaseOpsT     mAdapterBaseOps;
    void* OmxSoHandle;
    
}VdecoderAdapterContext;

static const CodecFormatToName kCodecFormatToName[] = 
{
    {VIDEO_CODEC_FORMAT_VP8, OMX_VIDEO_CodingVP8, 
     "OMX.allwinner.video.decoder.vp8", "video_decoder.vp8"},
     
    {VIDEO_CODEC_FORMAT_H264, OMX_VIDEO_CodingAVC, 
     "OMX.allwinner.video.decoder.avc", "video_decoder.avc"},
     
     {VIDEO_CODEC_FORMAT_H265, OMX_VIDEO_CodingHEVC, 
     "OMX.allwinner.video.decoder.hevc", "video_decoder.hevc"},
     
     {VIDEO_CODEC_FORMAT_H263, OMX_VIDEO_CodingH263, 
     "OMX.allwinner.video.decoder.h263", "video_decoder.h263"},
     
     {VIDEO_CODEC_FORMAT_MPEG4, OMX_VIDEO_CodingMPEG4, 
     "OMX.allwinner.video.decoder.mpeg4", "video_decoder.mpeg4"},
     
    {VIDEO_CODEC_FORMAT_XVID, OMX_VIDEO_CodingXVID, 
     "OMX.allwinner.video.decoder.xvid", "video_decoder.xvid"},
     
     {VIDEO_CODEC_FORMAT_DIVX5, OMX_VIDEO_CodingDIVX, 
     "OMX.allwinner.video.decoder.divx", "video_decoder.divx"},


     {VIDEO_CODEC_FORMAT_MSMPEG4V1, OMX_VIDEO_CodingMSMPEG4V1, 
     "OMX.allwinner.video.decoder.msmpeg4v1", "video_decoder.msmpeg4v1"},

    {VIDEO_CODEC_FORMAT_MSMPEG4V2, OMX_VIDEO_CodingMSMPEG4V2, 
     "OMX.allwinner.video.decoder.msmpeg4v2", "video_decoder.msmpeg4v2"},

     {VIDEO_CODEC_FORMAT_MPEG1, OMX_VIDEO_CodingMPEG1, 
     "OMX.allwinner.video.decoder.mpeg1", "video_decoder.mpeg1"},

    {VIDEO_CODEC_FORMAT_MPEG2, OMX_VIDEO_CodingMPEG2, 
     "OMX.allwinner.video.decoder.mpeg2", "video_decoder.mpeg2"},

     {VIDEO_CODEC_FORMAT_MJPEG, OMX_VIDEO_CodingMJPEG, 
     "OMX.allwinner.video.decoder.mjpeg", "video_decoder.mjpeg"},

     {VIDEO_CODEC_FORMAT_SORENSSON_H263, OMX_VIDEO_CodingS263, 
     "OMX.allwinner.video.decoder.s263", "video_decoder.s263"},

     {VIDEO_CODEC_FORMAT_WMV1, OMX_VIDEO_CodingWMV1, 
     "OMX.allwinner.video.decoder.wmv1", "video_decoder.wmv1"},

    {VIDEO_CODEC_FORMAT_WMV2, OMX_VIDEO_CodingWMV2, 
     "OMX.allwinner.video.decoder.wmv2", "video_decoder.wmv2"},

    {VIDEO_CODEC_FORMAT_WMV3, OMX_VIDEO_CodingWMV, 
     "OMX.allwinner.video.decoder.vc1", "video_decoder.vc1"},

    {VIDEO_CODEC_FORMAT_RXG2, OMX_VIDEO_CodingRXG2, 
     "OMX.allwinner.video.decoder.rxg2", "video_decoder.rxg2"},

    {VIDEO_CODEC_FORMAT_VP6, OMX_VIDEO_CodingVP6, 
     "OMX.allwinner.video.decoder.vp6", "video_decoder.vp6"},

    {VIDEO_CODEC_FORMAT_VP9, OMX_VIDEO_CodingVP9, 
     "OMX.allwinner.video.decoder.vp9", "video_decoder.vp9"},
};

typedef void * (*CreateOmxComponent)(void);

VideoDecoder* AdCreateVideoDecoder(void)
{
    logv("CreateVideoDecoder");
    
    VdecoderAdapterContext* p = NULL;
    int err = -1;

    p = (VdecoderAdapterContext*)malloc(sizeof(VdecoderAdapterContext));
    if(p == NULL)
    {
        loge("malloc for OmxAdapterContext failed");
        return NULL;
    }

    memset(p, 0, sizeof(VdecoderAdapterContext));

    //* init the interface      
    p->mAdapterBaseOps.AdbCreateVideoDecoder			    =	AdbCreateVideoDecoder;
    p->mAdapterBaseOps.AdbInitializeVideoDecoder			=	AdbInitializeVideoDecoder;
    p->mAdapterBaseOps.AdbDestroyVideoDecoder			    =	AdbDestroyVideoDecoder;
    p->mAdapterBaseOps.AdbResetVideoDecoder			        =	AdbResetVideoDecoder;
	p->mAdapterBaseOps.AdbDecodeVideoStream					=	AdbDecodeVideoStream;						
    p->mAdapterBaseOps.AdbDecoderSetSpecialData				=	AdbDecoderSetSpecialData;					
    p->mAdapterBaseOps.AdbGetVideoStreamInfo				=	AdbGetVideoStreamInfo;						
    p->mAdapterBaseOps.AdbRequestVideoStreamBuffer			=	AdbRequestVideoStreamBuffer;				
    p->mAdapterBaseOps.AdbSubmitVideoStreamData				=	AdbSubmitVideoStreamData;					
    p->mAdapterBaseOps.AdbVideoStreamBufferSize				=	AdbVideoStreamBufferSize;					
    p->mAdapterBaseOps.AdbVideoStreamDataSize				=	AdbVideoStreamDataSize;						
    p->mAdapterBaseOps.AdbVideoStreamFrameNum				=	AdbVideoStreamFrameNum;						
    p->mAdapterBaseOps.AdbVideoStreamDataInfoPointer		=	AdbVideoStreamDataInfoPointer;				
    p->mAdapterBaseOps.AdbRequestPicture					=	AdbRequestPicture;							
    p->mAdapterBaseOps.AdbReturnPicture						=	AdbReturnPicture;							
    p->mAdapterBaseOps.AdbNextPictureInfo					=	AdbNextPictureInfo;							
    p->mAdapterBaseOps.AdbTotalPictureBufferNum				=	AdbTotalPictureBufferNum;					
    p->mAdapterBaseOps.AdbEmptyPictureBufferNum				=	AdbEmptyPictureBufferNum;                   
    p->mAdapterBaseOps.AdbValidPictureNum					=	AdbValidPictureNum;							
    p->mAdapterBaseOps.AdbConfigHorizonScaleDownRatio		=	AdbConfigHorizonScaleDownRatio;				
    p->mAdapterBaseOps.AdbConfigVerticalScaleDownRatio		=	AdbConfigVerticalScaleDownRatio;			
    p->mAdapterBaseOps.AdbConfigRotation					=	AdbConfigRotation;							
    p->mAdapterBaseOps.AdbConfigDeinterlace					=	AdbConfigDeinterlace;						
    p->mAdapterBaseOps.AdbConfigThumbnailMode			   	=   AdbConfigThumbnailMode;						
    p->mAdapterBaseOps.AdbConfigOutputPicturePixelFormat	=	AdbConfigOutputPicturePixelFormat;			
    p->mAdapterBaseOps.AdbConfigNoBFrames					=	AdbConfigNoBFrames;							
    p->mAdapterBaseOps.AdbConfigDisable3D					=	AdbConfigDisable3D;							
    p->mAdapterBaseOps.AdbConfigVeMemoryThresh				=	AdbConfigVeMemoryThresh;					
    p->mAdapterBaseOps.AdbReopenVideoEngine					=	AdbReopenVideoEngine;						
    p->mAdapterBaseOps.AdbRotatePicture						=	AdbRotatePicture;							
    p->mAdapterBaseOps.AdbRotatePictureHw					=	AdbRotatePictureHw;							
	p->mAdapterBaseOps.AdbAllocatePictureBuffer				=	AdbAllocatePictureBuffer;					
	p->mAdapterBaseOps.AdbFreePictureBuffer					=	AdbFreePictureBuffer;						
	p->mAdapterBaseOps.AdbVideoRequestSecureBuffer			=	AdbVideoRequestSecureBuffer;				
	p->mAdapterBaseOps.AdbVideoReleaseSecureBuffer			=	AdbVideoReleaseSecureBuffer;				
	p->mAdapterBaseOps.AdbReturnRelasePicture				=	AdbReturnRelasePicture;					    
	p->mAdapterBaseOps.AdbRequestReleasePicture				=	AdbRequestReleasePicture;					
	p->mAdapterBaseOps.AdbSetVideoFbmBufRelease				=	AdbSetVideoFbmBufRelease;					
	p->mAdapterBaseOps.AdbSetVideoFbmBufAddress				=	AdbSetVideoFbmBufAddress;					
	p->mAdapterBaseOps.AdbGetVideoFbmBufInfo				=	AdbGetVideoFbmBufInfo;				    
	p->mAdapterBaseOps.AdbSetGpuBufferNum			    	=	AdbSetGpuBufferNum;   
    p->mAdapterBaseOps.AdbSetCodecFormatToName			    =	AdbSetCodecFormatToName;
    p->mAdapterBaseOps.AdbSetInEosFlag                      =   AdbSetInEosFlag;
    p->mAdapterBaseOps.AdbGetOutEosFlag                     =   AdbGetOutEosFlag;

    p->pAdatpterBaseContext = (VdecoderAdapterBaseContext*)p->mAdapterBaseOps.AdbCreateVideoDecoder();
    if(p->pAdatpterBaseContext == NULL)
    {
        loge("***AdbCreateVideoDecoder failed");
        free(p);
        return NULL;
    }

    int nNum = sizeof(kCodecFormatToName)/sizeof(CodecFormatToName);
    p->mAdapterBaseOps.AdbSetCodecFormatToName((VideoDecoder*)p->pAdatpterBaseContext,
                                                kCodecFormatToName,
                                                nNum);
    
    return (VideoDecoder*)p;
}

void AdDestroyVideoDecoder(VideoDecoder* pDecoder)
{
    logv("AdDestroyVideoDecoder, pDecoder = %p",pDecoder);

    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdDestroyVideoDecoder");
        return;
    }
    
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    
    p->mAdapterBaseOps.AdbDestroyVideoDecoder((VideoDecoder*)p->pAdatpterBaseContext);

    if(p->OmxSoHandle)
    {
    	dlclose(p->OmxSoHandle);
    }
    
    free(p);
    return ;
}

int AdInitializeVideoDecoder(VideoDecoder* pDecoder, VideoStreamInfo* pVideoInfo, VConfig* pVconfig)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdInitializeVideoDecoder");
        return -1;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;

    //*check whether support the size
    if(pVideoInfo->nWidth > MAX_SURPPORT_RESOLUTION_WIDTH
       || pVideoInfo->nHeight > MAX_SURPPORT_RESOLUTION_HEIGHT)
    {
    	logw("the resolution is not support, w&h: %d, %d, MAX: %d, %d",
    			pVideoInfo->nWidth, pVideoInfo->nHeight,
    			MAX_SURPPORT_RESOLUTION_WIDTH, MAX_SURPPORT_RESOLUTION_HEIGHT);
    	return -1;
    }

    memcpy(&p->pAdatpterBaseContext->mConfig, pVconfig, sizeof(VConfig));

    p->pAdatpterBaseContext->mConfig.eOutputPixelFormat = OMX_COLOR_FormatYUV420Planar;

    p->pAdatpterBaseContext->eCodecFormat = pVideoInfo->eCodecFormat;
    
    p->OmxSoHandle = dlopen("libOmxVdec.so", RTLD_NOW);

    if(p->OmxSoHandle == NULL)
    {
        loge("open libOmxVdec.so fail");
        p->mAdapterBaseOps.AdbDestroyVideoDecoder((VideoDecoder*)p->pAdatpterBaseContext);
        free(p);
        return -1;
    }

    CreateOmxComponent FCreateOmxComponent = (CreateOmxComponent)dlsym(p->OmxSoHandle, 
                                             "get_omx_component_factory_fn");
    
    p->pAdatpterBaseContext->pOmx = (aw_omx_component*)(*FCreateOmxComponent)();
    
    return p->mAdapterBaseOps.AdbInitializeVideoDecoder((VideoDecoder*)p->pAdatpterBaseContext, 
                                                         pVideoInfo, pVconfig);
}

void AdResetVideoDecoder(VideoDecoder* pDecoder)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdResetVideoDecoder");
        return;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbResetVideoDecoder((VideoDecoder*)p->pAdatpterBaseContext);
}

int AdDecodeVideoStream(VideoDecoder* pDecoder, 
                      int           bEndOfStream,
                      int           bDecodeKeyFrameOnly,
                      int           bDropBFrameIfDelay,
                      int64_t       nCurrentTimeUs)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdDecodeVideoStream");
        return -1;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbDecodeVideoStream((VideoDecoder*)p->pAdatpterBaseContext,
                                            bEndOfStream,
                                            bDecodeKeyFrameOnly,
                                            bDropBFrameIfDelay,
                                            nCurrentTimeUs);
}

int AdDecoderSetSpecialData(VideoDecoder* pDecoder, void *pArg)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdDecoderSetSpecialData");
        return -1;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbDecoderSetSpecialData((VideoDecoder*)p->pAdatpterBaseContext, pArg);
}

int AdGetVideoStreamInfo(VideoDecoder* pDecoder, VideoStreamInfo* pVideoInfo)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdGetVideoStreamInfo");
        return -1;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbGetVideoStreamInfo((VideoDecoder*)p->pAdatpterBaseContext, 
                                                     pVideoInfo);
}

int AdRequestVideoStreamBuffer(VideoDecoder* pDecoder,
                             int           nRequireSize,
                             char**        ppBuf,
                             int*          pBufSize,
                             char**        ppRingBuf,
                             int*          pRingBufSize,
                             int           nStreamBufIndex)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdRequestVideoStreamBuffer");
        return -1;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbRequestVideoStreamBuffer((VideoDecoder*)p->pAdatpterBaseContext,
                                                          nRequireSize,
                                                          ppBuf,
                                                          pBufSize,
                                                          ppRingBuf,
                                                          pRingBufSize,
                                                          nStreamBufIndex);
}

int AdSubmitVideoStreamData(VideoDecoder*        pDecoder,
                          VideoStreamDataInfo* pDataInfo,
                          int                  nStreamBufIndex)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdSubmitVideoStreamData");
        return -1;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbSubmitVideoStreamData((VideoDecoder*)p->pAdatpterBaseContext, 
                                                       pDataInfo,
                                                       nStreamBufIndex);
}

int AdVideoStreamBufferSize(VideoDecoder* pDecoder, int nStreamBufIndex)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdVideoStreamBufferSize");
        return -1;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbVideoStreamBufferSize((VideoDecoder*)p->pAdatpterBaseContext,
                                                        nStreamBufIndex);
}

int AdVideoStreamDataSize(VideoDecoder* pDecoder, int nStreamBufIndex)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdVideoStreamDataSize");
        return -1;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbVideoStreamDataSize((VideoDecoder*)p->pAdatpterBaseContext,
                                                      nStreamBufIndex);
}

int AdVideoStreamFrameNum(VideoDecoder* pDecoder, int nStreamBufIndex)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdVideoStreamFrameNum");
        return -1;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbVideoStreamFrameNum((VideoDecoder*)p->pAdatpterBaseContext,
                                                      nStreamBufIndex);
}

void* AdVideoStreamDataInfoPointer(VideoDecoder* pDecoder, int nStreamBufIndex)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdVideoStreamDataInfoPointer");
        return NULL;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbVideoStreamDataInfoPointer((VideoDecoder*)p->pAdatpterBaseContext,
                                                             nStreamBufIndex);
}


VideoPicture* AdRequestPicture(VideoDecoder* pDecoder, int nStreamIndex)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdRequestPicture");
        return NULL;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbRequestPicture((VideoDecoder*)p->pAdatpterBaseContext,
                                                nStreamIndex);
}

int AdReturnPicture(VideoDecoder* pDecoder, VideoPicture* pPicture)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdReturnPicture");
        return -1;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbReturnPicture((VideoDecoder*)p->pAdatpterBaseContext, pPicture);
}

VideoPicture* AdNextPictureInfo(VideoDecoder* pDecoder, int nStreamIndex)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdNextPictureInfo");
        return NULL;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbNextPictureInfo((VideoDecoder*)p->pAdatpterBaseContext,
                                                  nStreamIndex);
}

int AdTotalPictureBufferNum(VideoDecoder* pDecoder, int nStreamIndex)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdTotalPictureBufferNum");
        return -1;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbTotalPictureBufferNum((VideoDecoder*)p->pAdatpterBaseContext,
                                                       nStreamIndex);
}

int AdEmptyPictureBufferNum(VideoDecoder* pDecoder, int nStreamIndex)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdEmptyPictureBufferNum");
        return -1;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbEmptyPictureBufferNum((VideoDecoder*)p->pAdatpterBaseContext, 
                                                        nStreamIndex);
}

int AdValidPictureNum(VideoDecoder* pDecoder, int nStreamIndex)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdValidPictureNum");
        return -1;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbValidPictureNum((VideoDecoder*)p->pAdatpterBaseContext, 
                                                  nStreamIndex);
}

int AdConfigHorizonScaleDownRatio(VideoDecoder* pDecoder, int AdConfigHorizonScaleDownRatio)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdConfigHorizonScaleDownRatio");
        return -1;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbConfigHorizonScaleDownRatio((VideoDecoder*)p->pAdatpterBaseContext, 
                                                             AdConfigHorizonScaleDownRatio);
}

int AdConfigVerticalScaleDownRatio(VideoDecoder* pDecoder, int nScaleDownRatio)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdConfigVerticalScaleDownRatio");
        return -1;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbConfigVerticalScaleDownRatio((VideoDecoder*)p->pAdatpterBaseContext, 
                                                              nScaleDownRatio);
}

int AdConfigRotation(VideoDecoder* pDecoder, int nRotateDegree)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdConfigRotation");
        return -1;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbConfigRotation((VideoDecoder*)p->pAdatpterBaseContext,
                                                nRotateDegree);
}

int AdConfigDeinterlace(VideoDecoder* pDecoder, int bDeinterlace)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdConfigDeinterlace");
        return -1;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbConfigDeinterlace((VideoDecoder*)p->pAdatpterBaseContext,
                                                    bDeinterlace);
}

int AdConfigThumbnailMode(VideoDecoder* pDecoder, int bOpenThumbnailMode)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdConfigThumbnailMode");
        return -1;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbConfigThumbnailMode((VideoDecoder*)p->pAdatpterBaseContext,
                                                     bOpenThumbnailMode);
}

int AdConfigOutputPicturePixelFormat(VideoDecoder* pDecoder, int ePixelFormat)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdConfigOutputPicturePixelFormat");
        return -1;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbConfigOutputPicturePixelFormat((VideoDecoder*)p->pAdatpterBaseContext, 
                                                                ePixelFormat);
}

int AdConfigNoBFrames(VideoDecoder* pDecoder, int bNoBFrames)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdConfigNoBFrames");
        return -1;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbConfigNoBFrames((VideoDecoder*)p->pAdatpterBaseContext,
                                                 bNoBFrames);
}

int AdConfigDisable3D(VideoDecoder* pDecoder, int bDisable3D)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdConfigDisable3D");
        return -1;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbConfigDisable3D((VideoDecoder*)p->pAdatpterBaseContext, bDisable3D);
}

int AdConfigVeMemoryThresh(VideoDecoder* pDecoder, int nMemoryThresh)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdConfigVeMemoryThresh");
        return -1;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbConfigVeMemoryThresh((VideoDecoder*)p->pAdatpterBaseContext,
                                                       nMemoryThresh);
}

int AdReopenVideoEngine(VideoDecoder* pDecoder, VConfig* pVConfig, VideoStreamInfo* pStreamInfo)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdReopenVideoEngine");
        return -1;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbReopenVideoEngine((VideoDecoder*)p->pAdatpterBaseContext,
                                                    pVConfig,
                                                    pStreamInfo);
}

int AdRotatePicture(VideoPicture* pPictureIn, 
                  VideoPicture* pPictureOut, 
                  int           nRotateDegree,
                  int           nGpuYAlign,
                  int           nGpuCAlign)
{
    CEDARX_UNUSE(pPictureIn);
    CEDARX_UNUSE(pPictureOut);
    CEDARX_UNUSE(nRotateDegree);
    CEDARX_UNUSE(nGpuYAlign);
    CEDARX_UNUSE(nGpuCAlign);
    return -1;
}

int AdRotatePictureHw(VideoDecoder* pDecoder,
                    VideoPicture* pPictureIn, 
                    VideoPicture* pPictureOut, 
                    int           nRotateDegree)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdRotatePictureHw");
        return -1;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbRotatePictureHw((VideoDecoder*)p->pAdatpterBaseContext,
                                                pPictureIn,
                                                pPictureOut,
                                                nRotateDegree);
}

VideoPicture* AdAllocatePictureBuffer(int nWidth, int nHeight, int nLineStride, int ePixelFormat)
{
    CEDARX_UNUSE(nWidth);
    CEDARX_UNUSE(nHeight);
    CEDARX_UNUSE(nLineStride);
    CEDARX_UNUSE(ePixelFormat);
    return NULL;
}

int AdFreePictureBuffer(VideoPicture* pPicture)
{
    CEDARX_UNUSE(pPicture);
    return -1;
}
char* AdVideoRequestSecureBuffer(VideoDecoder* pDecoder,int nBufferSize)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdVideoRequestSecureBuffer");
        return NULL;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbVideoRequestSecureBuffer((VideoDecoder*)p->pAdatpterBaseContext,
                                                          nBufferSize);
}

void AdVideoReleaseSecureBuffer(VideoDecoder* pDecoder,char* pBuf)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdVideoReleaseSecureBuffer");
        return ;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbVideoReleaseSecureBuffer((VideoDecoder*)p->pAdatpterBaseContext, pBuf);
}

VideoPicture*  AdReturnRelasePicture(VideoDecoder* pDecoder, VideoPicture* pVpicture, int bForbidUseFlag)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdReturnRelasePicture");
        return NULL;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbReturnRelasePicture((VideoDecoder*)p->pAdatpterBaseContext, 
                                                     pVpicture,
                                                     bForbidUseFlag);
}

VideoPicture* AdRequestReleasePicture(VideoDecoder* pDecoder)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdRequestReleasePicture");
        return NULL;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbRequestReleasePicture((VideoDecoder*)p->pAdatpterBaseContext);
}

int AdSetVideoFbmBufRelease(VideoDecoder* pDecoder)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdSetVideoFbmBufRelease");
        return -1;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbSetVideoFbmBufRelease((VideoDecoder*)p->pAdatpterBaseContext); 
}

VideoPicture* AdSetVideoFbmBufAddress(VideoDecoder* pDecoder, VideoPicture* pVideoPicture,int bForbidUseFlag)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdSetVideoFbmBufAddress");
        return NULL;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbSetVideoFbmBufAddress((VideoDecoder*)p->pAdatpterBaseContext,
                                                      pVideoPicture,
                                                      bForbidUseFlag); 
}

FbmBufInfo* AdGetVideoFbmBufInfo(VideoDecoder* pDecoder)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdGetVideoFbmBufInfo");
        return NULL;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbGetVideoFbmBufInfo((VideoDecoder*)p->pAdatpterBaseContext); 
}

int	AdSetInEosFlag(VideoDecoder* pDecoder, int nFlag)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdGetVideoFbmBufInfo");
        return -1;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbSetInEosFlag((VideoDecoder*)p->pAdatpterBaseContext, nFlag); 
}

int	AdGetOutEosFlag(VideoDecoder* pDecoder)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when call AdGetVideoFbmBufInfo");
        return 0;
    }
    VdecoderAdapterContext* p = (VdecoderAdapterContext*)pDecoder;
    return p->mAdapterBaseOps.AdbGetOutEosFlag((VideoDecoder*)p->pAdatpterBaseContext); 
}