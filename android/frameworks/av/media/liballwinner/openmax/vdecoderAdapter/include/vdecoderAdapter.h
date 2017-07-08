
#ifndef VDECODER_ADAPTER_H
#define VDECODER_ADAPTER_H

#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include "typedef.h"
#include "vdecoder.h"

VideoDecoder* AdCreateVideoDecoder(void);

void AdDestroyVideoDecoder(VideoDecoder* pDecoder);

int AdInitializeVideoDecoder(VideoDecoder* pDecoder, VideoStreamInfo* pVideoInfo, VConfig* pVconfig);


void AdResetVideoDecoder(VideoDecoder* pDecoder);

int AdDecodeVideoStream(VideoDecoder* pDecoder, 
                      int           bEndOfStream,
                      int           bDecodeKeyFrameOnly,
                      int           bDropBFrameIfDelay,
                      int64_t       nCurrentTimeUs);

int AdDecoderSetSpecialData(VideoDecoder* pDecoder, void *pArg);

int AdGetVideoStreamInfo(VideoDecoder* pDecoder, VideoStreamInfo* pVideoInfo);

int AdRequestVideoStreamBuffer(VideoDecoder* pDecoder,
                             int           nRequireSize,
                             char**        ppBuf,
                             int*          pBufSize,
                             char**        ppRingBuf,
                             int*          pRingBufSize,
                             int           nStreamBufIndex);

int AdSubmitVideoStreamData(VideoDecoder*        pDecoder,
                          VideoStreamDataInfo* pDataInfo,
                          int                  nStreamBufIndex);

int AdVideoStreamBufferSize(VideoDecoder* pDecoder, int nStreamBufIndex);

int AdVideoStreamDataSize(VideoDecoder* pDecoder, int nStreamBufIndex);

int AdVideoStreamFrameNum(VideoDecoder* pDecoder, int nStreamBufIndex);
void* AdVideoStreamDataInfoPointer(VideoDecoder* pDecoder, int nStreamBufIndex);


VideoPicture* AdRequestPicture(VideoDecoder* pDecoder, int nStreamIndex);

int AdReturnPicture(VideoDecoder* pDecoder, VideoPicture* pPicture);

VideoPicture* AdNextPictureInfo(VideoDecoder* pDecoder, int nStreamIndex);

int AdTotalPictureBufferNum(VideoDecoder* pDecoder, int nStreamIndex);

int AdEmptyPictureBufferNum(VideoDecoder* pDecoder, int nStreamIndex);

int AdValidPictureNum(VideoDecoder* pDecoder, int nStreamIndex);

int AdConfigHorizonScaleDownRatio(VideoDecoder* pDecoder, int nScaleDownRatio);

int AdConfigVerticalScaleDownRatio(VideoDecoder* pDecoder, int nScaleDownRatio);

int AdConfigRotation(VideoDecoder* pDecoder, int nRotateDegree);

int AdConfigDeinterlace(VideoDecoder* pDecoder, int bDeinterlace);

int AdConfigThumbnailMode(VideoDecoder* pDecoder, int bOpenThumbnailMode);

int AdConfigOutputPicturePixelFormat(VideoDecoder* pDecoder, int ePixelFormat);

int AdConfigNoBFrames(VideoDecoder* pDecoder, int bNoBFrames);

int AdConfigDisable3D(VideoDecoder* pDecoder, int bDisable3D);

int AdConfigVeMemoryThresh(VideoDecoder* pDecoder, int nMemoryThresh);

int AdReopenVideoEngine(VideoDecoder* pDecoder, VConfig* pVConfig, VideoStreamInfo* pStreamInfo);

int AdRotatePicture(VideoPicture* pPictureIn, 
                  VideoPicture* pPictureOut, 
                  int           nRotateDegree,
                  int           nGpuYAlign,
                  int           nGpuCAlign);

int AdRotatePictureHw(VideoDecoder* pDecoder,
                    VideoPicture* pPictureIn, 
                    VideoPicture* pPictureOut, 
                    int           nRotateDegree);

VideoPicture* AdAllocatePictureBuffer(int nWidth, int nHeight, int nLineStride, int ePixelFormat);

int AdFreePictureBuffer(VideoPicture* pPicture);
char* AdVideoRequestSecureBuffer(VideoDecoder* pDecoder,int nBufferSize);

void AdVideoReleaseSecureBuffer(VideoDecoder* pDecoder,char* pBuf);

VideoPicture*  AdReturnRelasePicture(VideoDecoder* pDecoder, VideoPicture* pVpicture, int bForbidUseFlag);
VideoPicture* AdRequestReleasePicture(VideoDecoder* pDecoder);
int AdSetVideoFbmBufRelease(VideoDecoder* pDecoder);
VideoPicture* AdSetVideoFbmBufAddress(VideoDecoder* pDecoder, VideoPicture* pVideoPicture,int bForbidUseFlag);
FbmBufInfo* AdGetVideoFbmBufInfo(VideoDecoder* pDecoder);

int	AdSetInEosFlag(VideoDecoder* pDecoder, int nFlag);

int	AdGetOutEosFlag(VideoDecoder* pDecoder);

#endif


