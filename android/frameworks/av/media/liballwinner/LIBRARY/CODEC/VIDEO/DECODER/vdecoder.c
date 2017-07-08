
#include <pthread.h>
#include <memory.h>
#include "vdecoder.h"
#include "sbm.h"
#include "fbm.h"
#include "videoengine.h"
#include "adapter.h"
#include "secureMemoryAdapter.h"
#include "log.h"
#include <stdio.h>

#include "sunxi_tr.h"
#include<sys/types.h>  
#include<sys/stat.h>
#include<fcntl.h>

#define DEBUG_SAVE_BITSTREAM	(0)
#define DEBUG_SAVE_PICTURE      (0)

/* show decoder speed  */
#define AW_VDECODER_SPEED_INFO  (0)
#define DEBUG_SAVE_FRAME_TIME   (0)
#define DEBUG_MAX_FRAME_IN_LIST 16

#define TRANSFORM_DEV_TIMEOUT  50  //* ms , tr timeout value

#define DEBUG_MAKE_SPECIAL_STREAM (0)
#define SPECIAL_STREAM_FILE "/data/camera/special.awsp"
#if DEBUG_SAVE_BITSTREAM
const char* fpStreamPath = "/data/camera/1011.dat";
static FILE* fpStream = NULL;
#endif
#if DEBUG_SAVE_PICTURE
static int count;
#endif

const char* strCodecFormat[] = 
{
    "MJPEG",        "MPEG1",        "MPEG2",    "MPEG4",    
    "MSMPEG4V1",    "MSMPEG4V2",    "DIVX3",    "DIVX4",
    "DIVX5",        "XVID",         "H263",     "H263SORRENSSON", 
    "RXG2",         "WMV1",         "WMV2",     "WMV3",
    "VP6",          "VP8",          "VP9",      "RX",      
    "H264",         "H265",         "AVS"
};

const char* strDecodeResult[] = 
{
    "UNSUPPORTED",
    "OK",
    "FRAME_DECODED",
    "CONTINU",
    "KEYFRAME_DECODED",
    "NO_FRAME_BUFFER",
    "NO_BITSTREAM",
    "RESOLUTION_CHANGE"
};

const char* strPixelFormat[] = 
{
    "DEFAULT",          "YUV_PLANER_420",           "YUV_PLANER_420",
    "YUV_PLANER_444",   "YV12",                     "NV1",
    "YUV_MB32_420",     "MB32_422",                 "MB32_444",
};

typedef struct VideoDecoderDebugContext
{
	int      nFrameNum;
	int 	 nFrameNumDuration;
    int      nThreshHoldNum;
    float    nMaxCostTime;
    float    nCostTimeList[DEBUG_MAX_FRAME_IN_LIST];
	int64_t  nStartTime;
	int64_t  nDuration;
	int64_t  nCurrentTime;
	int64_t  nHardwareCurrentTime;
	int64_t  nHardwareDuration;
	int64_t  nHardwareTotalTime;
    int64_t  nGetOneFrameFinishTime;
    float    nExtraDecodeTimeList[DEBUG_MAX_FRAME_IN_LIST];
    float    nFrameTimeList[DEBUG_MAX_FRAME_IN_LIST];
    float    nFrameSmoothTime;
    int      nWaitingForDispPicNum[DEBUG_MAX_FRAME_IN_LIST];
    int      nValidDisplayPic;
    int      nStartIndexExtra;
    unsigned int *pFrameTime;
    int      nFrameTimeNum;
    unsigned int *pDispFrameNum;
}VideoDecoderDebugContext;

typedef struct VideoDecoderContext
{
    VConfig             vconfig;
    VideoStreamInfo     videoStreamInfo;
    VideoEngine*        pVideoEngine;
    int                 nFbmNum;
    Fbm*                pFbm[2];
    int                 nSbmNum;
    Sbm*                pSbm[2];
    VideoStreamDataInfo partialStreamDataInfo[2];
    int nTrhandle;
    int nTrChannel;
    int bInitTrFlag;
    
    VideoDecoderDebugContext debug;
}VideoDecoderContext;


static int  DecideStreamBufferSize(VideoDecoderContext* p);
static void UpdateVideoStreamInfo(VideoDecoderContext* p, VideoPicture* pPicture);
static void CheckConfiguration(VideoDecoderContext* p);
extern void ConvertPixelFormat(VideoPicture* pPictureIn, VideoPicture* pPictureOut);
extern int  RotatePicture0Degree(VideoPicture* pPictureIn, VideoPicture* pPictureOut, int nGpuYAlign, int nGpuCAlign);
extern int  RotatePicture90Degree(VideoPicture* pPictureIn, VideoPicture* pPictureOut);
extern int  RotatePicture180Degree(VideoPicture* pPictureIn, VideoPicture* pPictureOut);
extern int  RotatePicture270Degree(VideoPicture* pPictureIn, VideoPicture* pPictureOut);

static inline int64_t VdecoderGetCurrentTime(void)
{
	struct timeval tv;
	int64_t time;
	gettimeofday(&tv,NULL);
	time = tv.tv_sec*1000000 + tv.tv_usec;
    return time;
}

VideoDecoder* CreateVideoDecoder(void)
{
    VideoDecoderContext* p;
    
    logv("CreateVideoDecoder");
    
    p = (VideoDecoderContext*)malloc(sizeof(VideoDecoderContext));
    if(p == NULL)
    {
        loge("memory alloc fail.");
        AdpaterRelease(0);
        return NULL;
    }
    
    memset(p, 0, sizeof(VideoDecoderContext));
    
    //* the partialStreamDataInfo is for data submit status recording.
    p->partialStreamDataInfo[0].nStreamIndex = 0;
    p->partialStreamDataInfo[0].nPts         = -1;
    p->partialStreamDataInfo[1].nStreamIndex = 1;
    p->partialStreamDataInfo[1].nPts         = -1;

    p->nTrhandle   = -1;
    p->nTrChannel  = 0;
    p->bInitTrFlag = 0;
    logv("create a video decoder with handle=%p", p);
    
#if DEBUG_SAVE_BITSTREAM
	fpStream = fopen(fpStreamPath, "wb");
#endif
    return (VideoDecoder*)p;
}


void DestroyVideoDecoder(VideoDecoder* pDecoder)
{
    VideoDecoderContext* p;
    int bSecureosEn = 0;
    int bVirMallocSbm = 0;
    int veVersion = VeGetIcVersion();
    
    logv("DestroyVideoDecoder, pDecoder=%p", pDecoder);
    
    p = (VideoDecoderContext*)pDecoder;
    
    bSecureosEn = p->vconfig.bSecureosEn;
    bVirMallocSbm = p->vconfig.bVirMallocSbm;
#if DEBUG_SAVE_FRAME_TIME
    if(p->debug.pFrameTime != NULL)
    {
         FILE *fp = fopen("/data/camera/vdecoderFrameTime.txt", "wt");
         if(fp != NULL)
         {
             int i;
             loge(" vdecoder saving frame time. number: %d ", p->debug.nFrameTimeNum);
             for(i = 0; i < p->debug.nFrameTimeNum; i++)
             {
                 fprintf(fp, "%u\n", p->debug.pFrameTime[i]);
             }
             fclose(fp);
         }
         else
         {
             loge("vdecoder.c save frame open file error ");
         }
         free(p->debug.pFrameTime);
         p->debug.pFrameTime = NULL;
    }
    if(p->debug.pDispFrameNum != NULL)
    {
        FILE *fp = fopen("/data/camera/vdecoderHadSendDispNum.txt", "wt");
         if(fp != NULL)
         {
             int i;
             loge(" vdecoder saving frame time. number: %d ", p->debug.nFrameTimeNum);
             for(i = 0; i < p->debug.nFrameTimeNum; i++)
             {
                 fprintf(fp, "%u\n", p->debug.pDispFrameNum[i]);
             }
             fclose(fp);
         }
         else
         {
             loge("vdecoder.c save frame open file error ");
         }
         free(p->debug.pDispFrameNum);
         p->debug.pDispFrameNum = NULL;
    }

#endif //DEBUG_SAVE_FRAME_TIME
    //* Destroy the video engine first.
    if(p->pVideoEngine != NULL)
    {
    	if((veVersion==0x1681 || veVersion==0x1689) && (p->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
        	AdapterLockVideoEngine(1);
        else
        	AdapterLockVideoEngine(0);
        	
        VideoEngineDestroy(p->pVideoEngine);
        
        if((veVersion==0x1681 || veVersion==0x1689) && (p->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
        	AdapterUnLockVideoEngine(1);
        else
        	AdapterUnLockVideoEngine(0);
    }
        
    //* the memory space for codec specific data was allocated in InitializeVideoDecoder.
    if(p->videoStreamInfo.pCodecSpecificData != NULL)
        free(p->videoStreamInfo.pCodecSpecificData);
	
	 //* close tr driver
    if(p->bInitTrFlag == 1)
    {
        if(p->nTrhandle > 0)
        {
            if(p->nTrChannel != 0)
            {
                size_addr arg[6] = {0};
                arg[0] = p->nTrChannel;
                logd("release channel start : 0x%x , %d",p->nTrChannel,p->nTrhandle);
                if(ioctl(p->nTrhandle,TR_RELEASE,(void*)arg) != 0)
                {
                    loge("#### release channel failed!");
                    //return;
                }
                logd("release channel finish");
                p->nTrChannel = 0;
            }

            close(p->nTrhandle);
            p->nTrhandle = -1;
             
        }
    }
    
    //* Destroy the stream buffer manager.
    if(bVirMallocSbm == 0)
    {
	    if(p->pSbm[0] != NULL)
	        SbmDestroy(p->pSbm[0]);

	    if(p->pSbm[1] != NULL)
	        SbmDestroy(p->pSbm[1]);
    }
    else
    {
    	if(p->pSbm[0] != NULL)
	        SbmVirDestroy(p->pSbm[0]);

	    if(p->pSbm[1] != NULL)
	        SbmVirDestroy(p->pSbm[1]);
    }
    
    AdpaterRelease(bSecureosEn);
    
    free(p);

#if DEBUG_SAVE_BITSTREAM
	if(fpStream)
	{
		fclose(fpStream);
		fpStream = NULL;
	}
#endif
    return;
}


int InitializeVideoDecoder(VideoDecoder* pDecoder, VideoStreamInfo* pVideoInfo,  VConfig* pVconfig)
{
    VideoDecoderContext* p;
    int                  nStreamBufferSize;
    int                  i;
    int                  chipId;
    unsigned int         veVersion;
    
    logv("InitializeVideoDecoder, pDecoder=%p, pVideoInfo=%p", pDecoder, pVideoInfo);
    
    p = (VideoDecoderContext*)pDecoder;
    
    //* check codec format.
    if(pVideoInfo->eCodecFormat > VIDEO_CODEC_FORMAT_MAX ||
        pVideoInfo->eCodecFormat <  VIDEO_CODEC_FORMAT_MIN)
    {
        loge("codec format(0x%x) invalid.", pVideoInfo->eCodecFormat);
        return -1;
    }

	if(AdapterInitialize(pVconfig->bSecureosEn) != 0)
	{
		loge("can not set up video engine runtime environment.");
		return -1;
	}
	chipId = VeChipId();
	if(chipId == CHIP_H3s)
	{
        logd("chipId=%d\n", chipId);
		if(pVideoInfo->nWidth > 2048)
		{
				//DestroyVideoDecoder(pDecoder);
				return -1;
		}
	}

	veVersion = VeGetIcVersion();

    //* print video stream information.
    {
        logv("Video Stream Information:");
        logv("     codec          = %s",        strCodecFormat[pVideoInfo->eCodecFormat - VIDEO_CODEC_FORMAT_MIN]);
        logv("     width          = %d pixels", pVideoInfo->nWidth);
        logv("     height         = %d pixels", pVideoInfo->nHeight);
        logv("     frame rate     = %d",        pVideoInfo->nFrameRate);
        logv("     frame duration = %d us",     pVideoInfo->nFrameDuration);
        logv("     aspect ratio   = %d",        pVideoInfo->nAspectRatio);
        logv("     is 3D stream   = %s",        pVideoInfo->bIs3DStream ? "yes" : "no");
        logv("     csd data len   = %d",        pVideoInfo->nCodecSpecificDataLen);
		logv("     bIsFrameCtsTestFlag  = %d",  pVideoInfo->bIsFrameCtsTestFlag);
    }
    
    //* save the video stream information.
    p->videoStreamInfo.eCodecFormat          = pVideoInfo->eCodecFormat;
    p->videoStreamInfo.nWidth                = pVideoInfo->nWidth;
    p->videoStreamInfo.nHeight               = pVideoInfo->nHeight;
    p->videoStreamInfo.nFrameRate            = pVideoInfo->nFrameRate;
    p->videoStreamInfo.nFrameDuration        = pVideoInfo->nFrameDuration;
    p->videoStreamInfo.nAspectRatio          = pVideoInfo->nAspectRatio;
    p->videoStreamInfo.bIs3DStream           = pVideoInfo->bIs3DStream;
    p->videoStreamInfo.bIsFramePackage       = pVideoInfo->bIsFramePackage;
    p->videoStreamInfo.nCodecSpecificDataLen = pVideoInfo->nCodecSpecificDataLen;
    p->videoStreamInfo.bSecureStreamFlag     = pVideoInfo->bSecureStreamFlag;
    p->videoStreamInfo.bSecureStreamFlagLevel1 = pVideoInfo->bSecureStreamFlagLevel1;	
    p->videoStreamInfo.bIsFrameCtsTestFlag = pVideoInfo->bIsFrameCtsTestFlag;
	
    if(p->videoStreamInfo.nCodecSpecificDataLen > 0)
    {
        int   nSize = p->videoStreamInfo.nCodecSpecificDataLen;
        char* pMem  = (char*)malloc(nSize);
        
        if(pMem == NULL)
        {
            p->videoStreamInfo.nCodecSpecificDataLen = 0;
            loge("memory alloc fail.");
            return -1;
        }
        
        memcpy(pMem, pVideoInfo->pCodecSpecificData, nSize);
        p->videoStreamInfo.pCodecSpecificData = pMem;
    }

	if(pVconfig->nDecodeSmoothFrameBufferNum <= 0 
	   || pVconfig->nDecodeSmoothFrameBufferNum > 16)
	{
		logw("warning: the nDecodeSmoothFrameBufferNum is %d",
			pVconfig->nDecodeSmoothFrameBufferNum);
	}
	if(pVconfig->nDeInterlaceHoldingFrameBufferNum <= 0 
	   || pVconfig->nDeInterlaceHoldingFrameBufferNum > 16)
	{
		logw("warning: the nDeInterlaceHoldingFrameBufferNum is %d",
			pVconfig->nDeInterlaceHoldingFrameBufferNum);
	}
	if(pVconfig->nDisplayHoldingFrameBufferNum <= 0 
	   || pVconfig->nDisplayHoldingFrameBufferNum > 16)
	{
		logw("warning: the nDisplayHoldingFrameBufferNum is %d",
			pVconfig->nDisplayHoldingFrameBufferNum);
	}
    
    memcpy(&p->vconfig, pVconfig, sizeof(VConfig));
	
    //* create stream buffer.
    nStreamBufferSize = DecideStreamBufferSize(p);
    if(p->vconfig.bSecureosEn == 0)
    {
    	if(p->vconfig.bVirMallocSbm == 0)
    	{
    		p->pSbm[0] = SbmCreate(nStreamBufferSize);
    	}
    	else
    	{ 
    		p->pSbm[0] = SbmVirCreate(nStreamBufferSize);
    	}
    }
    else
    {
    	p->pSbm[0] = SbmSecureosCreate(nStreamBufferSize);
    }
    if(p->pSbm[0] == NULL)
    {
        loge("create stream buffer fail.");
        return -1;
    }
    p->nSbmNum++;
    
    if(p->videoStreamInfo.bIs3DStream)
    {
    	if(p->vconfig.bSecureosEn == 0)
        {
    		p->pSbm[1] = SbmCreate(nStreamBufferSize);
        }
    	else
    	{
    		p->pSbm[1] = SbmSecureosCreate(nStreamBufferSize);
    	}
        if(p->pSbm[1] == NULL)
        {
            loge("create stream buffer fail.");
            return -1;
        }
        p->nSbmNum++;
    }
    
    //* check and fix the configuration for decoder.
    CheckConfiguration(p);
    

    //* create video engine.
    if((veVersion==0x1681 || veVersion==0x1689) && (p->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
        AdapterLockVideoEngine(1);
    else
    	AdapterLockVideoEngine(0);

    p->pVideoEngine = VideoEngineCreate(&p->vconfig, &p->videoStreamInfo);

    if((veVersion==0x1681 || veVersion==0x1689) && (p->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
    	AdapterUnLockVideoEngine(1);
    else
        AdapterUnLockVideoEngine(0);
        
    if(p->pVideoEngine == NULL)
    {
        loge("create video engine fail.");
        return -1;
    }
    
    //* set stream buffer to video engine.
    if((veVersion==0x1681 || veVersion==0x1689) && (p->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
        AdapterLockVideoEngine(1);
    else
    	AdapterLockVideoEngine(0);
    	
    for(i=0; i<p->nSbmNum; i++)
        VideoEngineSetSbm(p->pVideoEngine, p->pSbm[i], i);
        
    if((veVersion==0x1681 || veVersion==0x1689) && (p->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
    	AdapterUnLockVideoEngine(1);
    else
        AdapterUnLockVideoEngine(0);
#if DEBUG_SAVE_FRAME_TIME
    if(p->debug.pFrameTime == NULL)
    {
        p->debug.pFrameTime  = calloc(1, 4*1024*1024);
        if(p->debug.pFrameTime == NULL)
        {
             loge(" save frame time calloc error  ");
        }
    }
    if(p->debug.pDispFrameNum == NULL)
    {
        p->debug.pDispFrameNum = calloc(1, 4*1024*1024);
        if(p->debug.pDispFrameNum == NULL)
        {
             loge(" save frame time calloc error  ");
        }
    }
#endif //DEBUG_SAVE_FRAME_TIM
#if DEBUG_SAVE_BITSTREAM
   logd("%d\n", p->videoStreamInfo.nCodecSpecificDataLen);
   logd("pts=0\n");
   if(p->videoStreamInfo.nCodecSpecificDataLen > 0)
   {
	   fwrite(p->videoStreamInfo.pCodecSpecificData, 1,p->videoStreamInfo.nCodecSpecificDataLen, fpStream);
   }
#endif //DEBUG_SAVE_BITSTREAM
#if DEBUG_MAKE_SPECIAL_STREAM
   if(1)
   {
        FILE *fp = fopen(SPECIAL_STREAM_FILE, "ab");
        if(fp == NULL)
        {
            loge(" make special stream open file error, initial  ");
        }
        else
        {
            char mask[16] = "awspcialstream";
            logd(" make special stream save special data size: %d ", pVideoInfo->nCodecSpecificDataLen);
            fwrite(mask, 14, sizeof(char), fp);
            fwrite(&pVideoInfo->eCodecFormat, 1, sizeof(int), fp);
            fwrite(&pVideoInfo->nWidth, 1, sizeof(int), fp);
            fwrite(&pVideoInfo->nHeight, 1, sizeof(int), fp);
            fwrite(&pVideoInfo->bIs3DStream, 1, sizeof(int), fp);
            fwrite(&pVideoInfo->nCodecSpecificDataLen, 1, sizeof(int), fp);
            if(pVideoInfo->nCodecSpecificDataLen != 0)
            {
                fwrite(pVideoInfo->pCodecSpecificData, 1, pVideoInfo->nCodecSpecificDataLen, fp);
            }
            fclose(fp);
        }
   }
#endif//DEBUG_MAKE_SPECIAL_STREAM
    return 0;
}



void ResetVideoDecoder(VideoDecoder* pDecoder)
{
    int                  i;
    VideoDecoderContext* p;
    unsigned int         veVersion;
    
    logv("ResetVideoDecoder, pDecoder=%p", pDecoder);
    
    p = (VideoDecoderContext*)pDecoder;
    veVersion = VeGetIcVersion();
    
    //* reset the video engine.
    if(p->pVideoEngine)
    {
        if((veVersion==0x1681 || veVersion==0x1689) && (p->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
    		AdapterLockVideoEngine(1);
    	else
        	AdapterLockVideoEngine(0);
        	
        VideoEngineReset(p->pVideoEngine);
        
        if((veVersion==0x1681 || veVersion==0x1689) && (p->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
    		AdapterUnLockVideoEngine(1);
    	else
        	AdapterUnLockVideoEngine(0);
    }
    
    //* flush pictures.
    if(p->nFbmNum == 0 && p->pVideoEngine != NULL)
    {
        if((veVersion==0x1681 || veVersion==0x1689) && (p->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
    		AdapterLockVideoEngine(1);
    	else
        	AdapterLockVideoEngine(0);
        	
        p->nFbmNum = VideoEngineGetFbmNum(p->pVideoEngine);
        
        if((veVersion==0x1681 || veVersion==0x1689) && (p->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
    		AdapterUnLockVideoEngine(1);
    	else
        	AdapterUnLockVideoEngine(0);
    }
        
    for(i=0; i<p->nFbmNum; i++)
    {
        if(p->pFbm[i] == NULL && p->pVideoEngine != NULL)
        {
            if((veVersion==0x1681 || veVersion==0x1689) && (p->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
    			AdapterLockVideoEngine(1);
    		else
        		AdapterLockVideoEngine(0);
            p->pFbm[i] = VideoEngineGetFbm(p->pVideoEngine, i);
            if((veVersion==0x1681 || veVersion==0x1689) && (p->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
    			AdapterUnLockVideoEngine(1);
    		else
        		AdapterUnLockVideoEngine(0);
        }
        
        if(p->pFbm[i] != NULL)
            FbmFlush(p->pFbm[i]);
    }
    
    //* flush stream data.
    for(i=0; i<p->nSbmNum; i++)
    {
        if(p->pSbm[i] != NULL)
            SbmReset(p->pSbm[i]);
    }
    
    //* clear the partialStreamDataInfo.
    memset(p->partialStreamDataInfo, 0, sizeof(VideoStreamDataInfo)*2);
    
    return;
}

int DecoderSetSpecialData(VideoDecoder* pDecoder, void *pArg)
{
	int ret;
	VideoDecoderContext* p;
	DecoderInterface *pDecoderInterface;
    p = (VideoDecoderContext*)pDecoder;

    if(p == NULL ||
    	p->pVideoEngine == NULL ||
    	p->pVideoEngine->pDecoderInterface == NULL)
    {
    	loge(" set decoder special data fail ");
    	return VDECODE_RESULT_UNSUPPORTED;
    }

    pDecoderInterface = p->pVideoEngine->pDecoderInterface;
    ret = 0;
    if(pDecoderInterface->SetSpecialData != NULL)
    {
    	ret = pDecoderInterface->SetSpecialData(pDecoderInterface, pArg);
    }
	return ret;
}

#define DEBUG_VDECODER_SWAP(a,b,tmp) {(tmp)=(a);(a)=(b);(b)=(tmp);}
static void DebugUpDateCostTimeList(float *list, float time)
{
    int i;
    float tmp;
    if(list == NULL)
        return;
    if(time <= list[0])
        return;
    list[0] = time;
    for(i = 0; i < DEBUG_MAX_FRAME_IN_LIST - 1; i++)
    {
        if(list[i+1] < time)
            DEBUG_VDECODER_SWAP(list[i+1], list[i], tmp)
        else
            break;
    }
}
static void DebugShowExtraTime(float *list, int index)
{
    int i, j;
    float f[DEBUG_MAX_FRAME_IN_LIST];
    j = index;
    for(i = 0; i <= DEBUG_MAX_FRAME_IN_LIST - 1; i++)
    {
        f[i] = list[j];
        j++;
        if(j >= DEBUG_MAX_FRAME_IN_LIST - 1)
            j = 0;
    }
    if(1)
    {
        float *l = f;
        logd("time [1-8]: %.3f, %.3f, %.3f, %.3f,  %.3f, %.3f, %.3f, %.3f",
                l[0],l[1],l[2],l[3], l[4],l[5],l[6],l[7]);
        logd("           [9-16]:  %.3f, %.3f, %.3f, %.3f,  %.3f, %.3f, %.3f, %.3f",
                l[8],l[9],l[10],l[11],  l[12],l[13],l[14],l[15]);
    }
}
static void DebugShowValidPicList(int *list, int index)
{
    int i, j;
    int f[DEBUG_MAX_FRAME_IN_LIST];
    j = index;
    for(i = 0; i <= DEBUG_MAX_FRAME_IN_LIST - 1; i++)
    {
        f[i] = list[j];
        j++;
        if(j >= DEBUG_MAX_FRAME_IN_LIST - 1)
            j = 0;
    }
    if(1)
    {
        int *l = f;
        logd(" when decoding the last 16 frame, waiting for display number   ");
        logd("valid frame [1-8]: %d, %d, %d, %d,  %d, %d, %d, %d",
                l[0],l[1],l[2],l[3], l[4],l[5],l[6],l[7]);
        logd("           [9-16]:  %d, %d, %d, %d,  %d, %d, %d, %d",
                l[8],l[9],l[10],l[11],  l[12],l[13],l[14],l[15]);
    }
}
static void DebugDecoderSpeedInfo(VideoDecoder* pDecoder)
{
    VideoDecoderContext* p;
    int nDeltaFrame = 64;
    int64_t hwtime;
    int64_t nCurrentFrameCostTime;
    int     nCostTime;
    float   fCostTime;
    p = (VideoDecoderContext*)pDecoder;
    hwtime = VdecoderGetCurrentTime();
    nCurrentFrameCostTime = (hwtime - p->debug.nHardwareCurrentTime);
    nCostTime = nCurrentFrameCostTime;
    fCostTime = nCostTime/1000.0;
#if DEBUG_SAVE_FRAME_TIME
    if(p->debug.pFrameTime != NULL)
    {
        p->debug.pFrameTime[p->debug.nFrameTimeNum] = nCurrentFrameCostTime;
    }
    if(p->debug.pDispFrameNum != NULL)
    {
        int num = DecoderSendToDisplayPictureNum(pDecoder, 0);
        p->debug.pDispFrameNum[p->debug.nFrameTimeNum] = num;
    }
    p->debug.nFrameTimeNum++;
#endif //DEBUG_SAVE_FRAME_TIME
    if(p->debug.nFrameNum > 8 && fCostTime > p->debug.nCostTimeList[0])
    {
        if(fCostTime > p->debug.nMaxCostTime)
            p->debug.nMaxCostTime = fCostTime;
        if(fCostTime >= 34.0)
            p->debug.nThreshHoldNum++;
        DebugUpDateCostTimeList(p->debug.nCostTimeList, fCostTime);
    }
    if(p->debug.nFrameNum > 8)
    {
        int64_t last = p->debug.nGetOneFrameFinishTime;
         p->debug.nGetOneFrameFinishTime = VdecoderGetCurrentTime();
         if(1)
         {
             int64_t nExtraTime = 0;
             int nExtTimeInt;
             float fExtTime;
            nExtraTime = p->debug.nGetOneFrameFinishTime - last;
            nExtTimeInt = nExtraTime;
            fExtTime = nExtTimeInt/1000.0;
            p->debug.nExtraDecodeTimeList[p->debug.nStartIndexExtra] = fExtTime - fCostTime;
            p->debug.nFrameTimeList[p->debug.nStartIndexExtra] = fCostTime;
             p->debug.nWaitingForDispPicNum[p->debug.nStartIndexExtra] = p->debug.nValidDisplayPic;
            p->debug.nStartIndexExtra++;
            if(p->debug.nStartIndexExtra >= DEBUG_MAX_FRAME_IN_LIST - 1)
                p->debug.nStartIndexExtra = 0;
         }
    }
    if(fCostTime < 1.0)
    {
        logd("warning, maybe drop frame enable, cost time: %.2f ms", fCostTime);
        return;
    }
    p->debug.nHardwareDuration += nCurrentFrameCostTime;
    p->debug.nFrameNumDuration += 1;
    p->debug.nFrameNum += 1;
    if(p->debug.nFrameNumDuration >= nDeltaFrame)
    {
        float PlayerCurrFps, PlayerAverageFps, HardwareCurrFps, HardwareAvgFps;
        int64_t nTime, nTotalDiff, hwDiff, hwAvgTime;
        nTime = VdecoderGetCurrentTime();
        p->debug.nHardwareTotalTime += p->debug.nHardwareDuration;
        p->debug.nDuration = nTime - p->debug.nCurrentTime;
        if(1)
        {
            nTotalDiff = (nTime - p->debug.nStartTime) / 1000; /* ms */
            nTime = p->debug.nDuration / 1000;    /* ms */
            hwDiff = p->debug.nHardwareDuration / 1000; /* ms */
            hwAvgTime = p->debug.nHardwareTotalTime / 1000;
            PlayerCurrFps = (float)(p->debug.nFrameNumDuration * 1000.0) / nTime;
            PlayerAverageFps = (float)(p->debug.nFrameNum * 1000.0) / nTotalDiff;
            HardwareCurrFps = (float)(p->debug.nFrameNumDuration * 1000.0) / hwDiff;
            HardwareAvgFps = (float)(p->debug.nFrameNum * 1000.0) / hwAvgTime;
            loge("player speed: %.2ffps, average: %.2ffps;slowest frame cost time: %.2f ms; hardware speed: %.2ffps, average: %.2ffps",
                    PlayerCurrFps, PlayerAverageFps, p->debug.nMaxCostTime, HardwareCurrFps, HardwareAvgFps);
            logv(" 0527 vdecoder hardware speed: %.2f fps, slowest frame cost time: %.2f ms,  average speed: %.2f fps",
                    HardwareCurrFps, p->debug.nMaxCostTime, HardwareAvgFps);
            logv(" 0527  vdecoder   player current speed: %.2f fps; player average speed: %.2f fps",
                    PlayerCurrFps, PlayerAverageFps);
        }
        if(0)
        {
            float *l = p->debug.nCostTimeList;
            logd("cost most time frame list[1-8]: %.2f, %.2f, %.2f, %.2f,  %.2f, %.2f, %.2f, %.2f",
                    l[0],l[1],l[2],l[3], l[4],l[5],l[6],l[7]);
            logd("cost most time frame list[9-16]:  %.2f, %.2f, %.2f, %.2f,  %.2f, %.2f, %.2f, %.2f",
                    l[8],l[9],l[10],l[11],  l[12],l[13],l[14],l[15]);
            logd("decoded frame number: %d,  cost time >= 34ms frame number: %d", p->debug.nFrameNum, p->debug.nThreshHoldNum);
        }
         if(0)
         {
             logd("current frame cost time: %.3fms last 16 frame extra time:  ", fCostTime);
             DebugShowExtraTime(p->debug.nExtraDecodeTimeList, p->debug.nStartIndexExtra);
             logd("    last 16 frame decode time: , smooth time: %.3f ", p->debug.nFrameSmoothTime);
             p->debug.nFrameSmoothTime = 0;
             DebugShowExtraTime(p->debug.nFrameTimeList, p->debug.nStartIndexExtra);
             DebugShowValidPicList(p->debug.nWaitingForDispPicNum, p->debug.nStartIndexExtra);
             logd(" ");
         }
        p->debug.nDuration = 0;
        p->debug.nHardwareDuration = 0;
        p->debug.nFrameNumDuration = 0;
        p->debug.nCurrentTime = VdecoderGetCurrentTime();
    }
}

int DecodeVideoStream(VideoDecoder* pDecoder, 
                      int           bEndOfStream,
                      int           bDecodeKeyFrameOnly,
                      int           bDropBFrameIfDelay,
                      int64_t       nCurrentTimeUs)
{
    int                  ret;
    VideoDecoderContext* p;
    unsigned int         veVersion;
    
    logi("DecodeVideoStream, pDecoder=%p, bEndOfStream=%d, bDropBFrameIfDelay=%d, nCurrentTimeUs=%lld",
            pDecoder, bEndOfStream, bDropBFrameIfDelay, nCurrentTimeUs);
    
    p = (VideoDecoderContext*)pDecoder;
    veVersion = VeGetIcVersion();
    
    if(p->pVideoEngine == NULL)
        return VDECODE_RESULT_UNSUPPORTED;
    
#if AW_VDECODER_SPEED_INFO
    p->debug.nHardwareCurrentTime = VdecoderGetCurrentTime();
    if(p->debug.nCurrentTime == 0)
    	p->debug.nCurrentTime = p->debug.nHardwareCurrentTime;
    if(p->debug.nStartTime == 0)
    	p->debug.nStartTime = p->debug.nHardwareCurrentTime;
    p->debug.nValidDisplayPic = ValidPictureNum(pDecoder, 0);
#endif //AW_VDECODER_SPEED_INFO

    if((veVersion==0x1681 || veVersion==0x1689) && (p->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
		AdapterLockVideoEngine(1);
	else
    	AdapterLockVideoEngine(0);
    if(p->videoStreamInfo.bSecureStreamFlagLevel1)
    	AdapterSetupSecureHardware();

    ret = VideoEngineDecode(p->pVideoEngine, 
                            bEndOfStream, 
                            bDecodeKeyFrameOnly, 
                            bDropBFrameIfDelay, 
                            nCurrentTimeUs);

    if(p->videoStreamInfo.bSecureStreamFlagLevel1)
    	AdapterShutdownSecureHardware();

    if((veVersion==0x1681 || veVersion==0x1689) && (p->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
    	AdapterUnLockVideoEngine(1);
    else
        AdapterUnLockVideoEngine(0);
    logi("decode method return %s", strDecodeResult[ret-VDECODE_RESULT_MIN]);
    
#if AW_VDECODER_SPEED_INFO
    if(ret == VDECODE_RESULT_FRAME_DECODED ||
       ret == VDECODE_RESULT_KEYFRAME_DECODED)
    {
        DebugDecoderSpeedInfo(pDecoder);
    }
#endif //AW_VDECODER_SPEED_INFO

    //* if in thumbnail mode and picture decoded, 
    //* reset the video engine to flush the picture out to FBM.
    if(p->vconfig.bThumbnailMode)
    {
        if(ret == VDECODE_RESULT_FRAME_DECODED || ret == VDECODE_RESULT_KEYFRAME_DECODED)
        {
            //AdapterLockVideoEngine();
            //VideoEngineReset(p->pVideoEngine);
            //AdapterUnLockVideoEngine();
        }
    }
    else if(bEndOfStream && ret == VDECODE_RESULT_NO_BITSTREAM)
    {
        //* stream end, let the low level decoder return all decoded pictures.
        if((veVersion==0x1681 || veVersion==0x1689) && (p->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
    		AdapterLockVideoEngine(1);
    	else
        	AdapterLockVideoEngine(0);
        VideoEngineReset(p->pVideoEngine);
        if((veVersion==0x1681 || veVersion==0x1689) && (p->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
    		AdapterUnLockVideoEngine(1);
    	else
        	AdapterUnLockVideoEngine(0);
    }
    
    return ret;
}


int GetVideoStreamInfo(VideoDecoder* pDecoder, VideoStreamInfo* pVideoInfo)
{
    VideoDecoderContext* p;
    
    logv("GetVideoStreamInfo, pDecoder=%p, pVideoInfo=%p", pDecoder, pVideoInfo);
    
    p = (VideoDecoderContext*)pDecoder;
    
    //* set the video stream information.
    pVideoInfo->eCodecFormat   = p->videoStreamInfo.eCodecFormat;
    pVideoInfo->nWidth         = p->videoStreamInfo.nWidth;
    pVideoInfo->nHeight        = p->videoStreamInfo.nHeight;
    pVideoInfo->nFrameRate     = p->videoStreamInfo.nFrameRate;
    pVideoInfo->nFrameDuration = p->videoStreamInfo.nFrameDuration;
    pVideoInfo->nAspectRatio   = p->videoStreamInfo.nAspectRatio;
    pVideoInfo->bIs3DStream    = p->videoStreamInfo.bIs3DStream;
    
    //* print video stream information.
    {
        logi("Video Stream Information:");
        logi("     codec          = %s",        strCodecFormat[pVideoInfo->eCodecFormat - VIDEO_CODEC_FORMAT_MIN]);
        logi("     width          = %d pixels", pVideoInfo->nWidth);
        logi("     height         = %d pixels", pVideoInfo->nHeight);
        logi("     frame rate     = %d",        pVideoInfo->nFrameRate);
        logi("     frame duration = %d us",     pVideoInfo->nFrameDuration);
        logi("     aspect ratio   = %d",        pVideoInfo->nAspectRatio);
        logi("     is 3D stream   = %s",        pVideoInfo->bIs3DStream ? "yes" : "no");
    }
    
    return 0;
}


int RequestVideoStreamBuffer(VideoDecoder* pDecoder,
                             int           nRequireSize,
                             char**        ppBuf,
                             int*          pBufSize,
                             char**        ppRingBuf,
                             int*          pRingBufSize,
                             int           nStreamBufIndex)
{
    char*                pStart;
    char*                pStreamBufEnd;
    char*                pMem;
    int                  nFreeSize;
    Sbm*                 pSbm;
    VideoDecoderContext* p;
    
    logi("RequestVideoStreamBuffer, pDecoder=%p, nRequireSize=%d, nStreamBufIndex=%d", 
            pDecoder, nRequireSize, nStreamBufIndex);
    
    p = (VideoDecoderContext*)pDecoder;
    
    *ppBuf        = NULL;
    *ppRingBuf    = NULL;
    *pBufSize     = 0;
    *pRingBufSize = 0;
    
    pSbm          = p->pSbm[nStreamBufIndex];
    
    if(pSbm == NULL)
    {
        logw("pSbm of video stream %d is NULL, RequestVideoStreamBuffer fail.", nStreamBufIndex);
        return -1;
    }

    //* sometimes AVI parser will pass empty stream frame to help pts calculation.
    //* in this case give four bytes even the parser does not need.
    if(nRequireSize == 0)
        nRequireSize = 4;
    
    //* we've filled partial frame data but not added to the SBM before, 
    //* we need to calculate the actual buffer pointer by self.
    nRequireSize += p->partialStreamDataInfo[nStreamBufIndex].nLength;
    if(SbmRequestBuffer(pSbm, nRequireSize, &pMem, &nFreeSize) < 0)
    {
        logi("request stream buffer fail, %d bytes valid data in SBM[%d], total buffer size is %d bytes.",
                SbmStreamDataSize(pSbm), 
                nStreamBufIndex, 
                SbmBufferSize(pSbm));
                
        return -1;
    }
    
    //* check the free buffer is larger than the partial data we filled before.
    if(nFreeSize <= p->partialStreamDataInfo[nStreamBufIndex].nLength)
    {
        logi("require stream buffer get %d bytes, but this buffer has been filled with partial \
                frame data of %d bytes before, nStreamBufIndex=%d.",
                nFreeSize, p->partialStreamDataInfo[nStreamBufIndex].nLength, nStreamBufIndex);
        
        return -1;
    }
    
    //* calculate the output buffer pos.
    pStreamBufEnd = (char*)SbmBufferAddress(pSbm) + SbmBufferSize(pSbm);
    pStart        = pMem + p->partialStreamDataInfo[nStreamBufIndex].nLength;
    if(pStart >= pStreamBufEnd)
        pStart -= SbmBufferSize(pSbm);
    nFreeSize -= p->partialStreamDataInfo[nStreamBufIndex].nLength;
    
    if(pStart + nFreeSize <= pStreamBufEnd) //* check if buffer ring back.
    {
        *ppBuf    = pStart;
        *pBufSize = nFreeSize;
    }
    else
    {
        //* the buffer ring back.
        *ppBuf        = pStart;
        *pBufSize     = pStreamBufEnd - pStart;
        *ppRingBuf    = SbmBufferAddress(pSbm);
        *pRingBufSize = nFreeSize - *pBufSize;
        logi("stream buffer %d ring back.", nStreamBufIndex);
    }

	return 0;
}


int SubmitVideoStreamData(VideoDecoder*        pDecoder,
                          VideoStreamDataInfo* pDataInfo,
                          int                  nStreamBufIndex)
{
    Sbm*                 pSbm;
    VideoDecoderContext* p;
    VideoStreamDataInfo* pPartialStreamDataInfo;
    
    logi("SubmitVideoStreamData, pDecoder=%p, pDataInfo=%p, nStreamBufIndex=%d", 
            pDecoder, pDataInfo, nStreamBufIndex);
    
    p = (VideoDecoderContext*)pDecoder;
    
    pSbm = p->pSbm[nStreamBufIndex];
    
    if(pSbm == NULL)
    {
        logw("pSbm of video stream %d is NULL, SubmitVideoStreamData fail.", nStreamBufIndex);
        return -1;
    }
    
    pPartialStreamDataInfo = &p->partialStreamDataInfo[nStreamBufIndex];
    
    //* chech wheter a new stream frame.
    if(pDataInfo->bIsFirstPart)
    {
        if(pPartialStreamDataInfo->nLength != 0)    //* last frame is not complete yet.
        {
            logv("stream data frame uncomplete.");
#if DEBUG_SAVE_BITSTREAM
            logd("lenth=%d\n", pPartialStreamDataInfo->nLength);
            logd("pts=%lld\n", pPartialStreamDataInfo->nPts);
            if(pSbm->pWriteAddr+pPartialStreamDataInfo->nLength <= pSbm->pStreamBufferEnd)
            {
            	fwrite(pSbm->pWriteAddr, 1, pPartialStreamDataInfo->nLength, fpStream);
            }
            else
            {
              	fwrite(pSbm->pWriteAddr, 1, pSbm->pStreamBufferEnd-pSbm->pWriteAddr+1, fpStream);
              	fwrite(pSbm->pStreamBuffer, 1, pPartialStreamDataInfo->nLength-(pSbm->pStreamBufferEnd-pSbm->pWriteAddr+1), fpStream);
            }
#endif

#if DEBUG_MAKE_SPECIAL_STREAM
            if(1)
            {
                FILE *fp = fopen(SPECIAL_STREAM_FILE, "ab");
                if(fp == NULL)
                {
                    loge(" make special stream open file error, save stream ");
                }
                else
                {
                    logd(" make special stream save data size: %d ", pPartialStreamDataInfo->nLength);
                    fwrite(&nStreamBufIndex, 1, sizeof(int), fp);
                    fwrite(&pPartialStreamDataInfo->nLength, 1, sizeof(int), fp);
                    fwrite(&pPartialStreamDataInfo->nPts, 1, sizeof(int64_t), fp);
                    if(pSbm->pWriteAddr+pPartialStreamDataInfo->nLength <= pSbm->pStreamBufferEnd)
                    {
                        fwrite(pSbm->pWriteAddr, 1, pPartialStreamDataInfo->nLength, fp);
                    }
                    else
                    {
                        fwrite(pSbm->pWriteAddr, 1, pSbm->pStreamBufferEnd-pSbm->pWriteAddr+1, fp);
                        fwrite(pSbm->pStreamBuffer, 1, pPartialStreamDataInfo->nLength-(pSbm->pStreamBufferEnd-pSbm->pWriteAddr+1), fp);
                    }
                    fclose(fp);
                }
            }
#endif //DEBUG_MAKE_SPECIAL_STREAM

            SbmAddStream(pSbm, pPartialStreamDataInfo);
        }
        
        //* set the data address and pts.
        pPartialStreamDataInfo->pData        = pDataInfo->pData;
        pPartialStreamDataInfo->nLength      = pDataInfo->nLength;
        pPartialStreamDataInfo->nPts         = pDataInfo->nPts;
        pPartialStreamDataInfo->nPcr         = pDataInfo->nPcr;
        pPartialStreamDataInfo->bIsFirstPart = pDataInfo->bIsFirstPart;
        pPartialStreamDataInfo->bIsLastPart  = 0;
        pPartialStreamDataInfo->bVideoInfoFlag = pDataInfo->bVideoInfoFlag;
        pPartialStreamDataInfo->pVideoInfo     = pDataInfo->pVideoInfo;
    }
    else
    {
        pPartialStreamDataInfo->nLength += pDataInfo->nLength;
        if(pPartialStreamDataInfo->nPts == -1 && pDataInfo->nPts != -1)
            pPartialStreamDataInfo->nPts = pDataInfo->nPts;
    }
    
    //* check whether a stream frame complete.
    if(pDataInfo->bIsLastPart)
    {
        if(pPartialStreamDataInfo->pData != NULL && pPartialStreamDataInfo->nLength != 0)
        {
        	if(p->vconfig.bVirMallocSbm == 0)
        	{
	            //* we need to flush data from cache to memory for the VE hardware module.
	            char* pStreamBufEnd = (char*)SbmBufferAddress(pSbm) + SbmBufferSize(pSbm);
	            if(pPartialStreamDataInfo->pData + pPartialStreamDataInfo->nLength <= pStreamBufEnd)
	            {
	            	if(p->vconfig.bSecureosEn == 0)
	            	{
	            		AdapterMemFlushCache(pPartialStreamDataInfo->pData, pPartialStreamDataInfo->nLength);
	            	}
	            	else
	            	{
	            		SecureMemAdapterFlushCache(pPartialStreamDataInfo->pData, pPartialStreamDataInfo->nLength);
	            	}
	            }
	            else
	            {
	                //* buffer ring back.
	                int nPartialLen = pStreamBufEnd - pPartialStreamDataInfo->pData;
	                if(p->vconfig.bSecureosEn == 0)
	                {
	                	AdapterMemFlushCache(pPartialStreamDataInfo->pData, nPartialLen);
	                	AdapterMemFlushCache(SbmBufferAddress(pSbm), pPartialStreamDataInfo->nLength - nPartialLen);
	                }
	                else
	                {
	                	SecureMemAdapterFlushCache(pPartialStreamDataInfo->pData, nPartialLen);
	                	SecureMemAdapterFlushCache(SbmBufferAddress(pSbm), pPartialStreamDataInfo->nLength - nPartialLen);
	                }
	            }
            }
        }
        else
        {
            //* maybe it is a empty frame for MPEG4 decoder from AVI parser.
            logw("empty stream data frame submitted, pData=%p, nLength=%d",
                pPartialStreamDataInfo->pData, pPartialStreamDataInfo->nLength);
        }
        
        //* submit stream frame to the SBM.
#if DEBUG_SAVE_BITSTREAM

            if(pSbm->pWriteAddr+pPartialStreamDataInfo->nLength <= pSbm->pStreamBufferEnd)
            {
            	fwrite(pSbm->pWriteAddr, 1, pPartialStreamDataInfo->nLength, fpStream);
            }
            else
            {
              	fwrite(pSbm->pWriteAddr, 1, pSbm->pStreamBufferEnd-pSbm->pWriteAddr+1, fpStream);
              	fwrite(pSbm->pStreamBuffer, 1, pPartialStreamDataInfo->nLength-(pSbm->pStreamBufferEnd-pSbm->pWriteAddr+1), fpStream);
            }
            logd("lenth=%d\n", pPartialStreamDataInfo->nLength);
            logd("pts=%lld\n", pPartialStreamDataInfo->nPts);
#endif

#if DEBUG_MAKE_SPECIAL_STREAM
            if(1)
            {
                FILE *fp = fopen(SPECIAL_STREAM_FILE, "ab");
                if(fp == NULL)
                {
                    loge(" make special stream open file error, save stream ");
                }
                else
                {
                    logd(" make special stream save data size: %d ", pPartialStreamDataInfo->nLength);
                    fwrite(&nStreamBufIndex, 1, sizeof(int), fp);
                    fwrite(&pPartialStreamDataInfo->nLength, 1, sizeof(int), fp);
                    fwrite(&pPartialStreamDataInfo->nPts, 1, sizeof(int64_t), fp);
                    if(pSbm->pWriteAddr+pPartialStreamDataInfo->nLength <= pSbm->pStreamBufferEnd)
                    {
                        fwrite(pSbm->pWriteAddr, 1, pPartialStreamDataInfo->nLength, fp);
                    }
                    else
                    {
                        fwrite(pSbm->pWriteAddr, 1, pSbm->pStreamBufferEnd-pSbm->pWriteAddr+1, fp);
                        fwrite(pSbm->pStreamBuffer, 1, pPartialStreamDataInfo->nLength-(pSbm->pStreamBufferEnd-pSbm->pWriteAddr+1), fp);
                    }
                    fclose(fp);
                }
            }
#endif// DEBUG_MAKE_SPECIAL_STREAM

        SbmAddStream(pSbm, pPartialStreamDataInfo);
        
        //* clear status of stream data info.
        pPartialStreamDataInfo->pData        = NULL;
        pPartialStreamDataInfo->nLength      = 0;
        pPartialStreamDataInfo->nPts         = -1;
        pPartialStreamDataInfo->nPcr         = -1;
        pPartialStreamDataInfo->bIsLastPart  = 0;
        pPartialStreamDataInfo->bIsFirstPart = 0;
        pPartialStreamDataInfo->bVideoInfoFlag = 0;
        pPartialStreamDataInfo->pVideoInfo     = NULL;
    }

    return 0;
}


int VideoStreamBufferSize(VideoDecoder* pDecoder, int nStreamBufIndex)
{
    VideoDecoderContext* p = (VideoDecoderContext*)pDecoder;
    
    logi("VideoStreamBufferSize, nStreamBufIndex=%d", nStreamBufIndex);
    
    if(p->pSbm[nStreamBufIndex] == NULL)
        return 0;
    
    return SbmBufferSize(p->pSbm[nStreamBufIndex]);
}


int VideoStreamDataSize(VideoDecoder* pDecoder, int nStreamBufIndex)
{
    VideoDecoderContext* p = (VideoDecoderContext*)pDecoder;
    
    logi("VideoStreamDataSize, nStreamBufIndex=%d", nStreamBufIndex);
    
    if(p->pSbm[nStreamBufIndex] == NULL)
        return 0;
    
    return SbmStreamDataSize(p->pSbm[nStreamBufIndex]);
}


int VideoStreamFrameNum(VideoDecoder* pDecoder, int nStreamBufIndex)
{
    VideoDecoderContext* p = (VideoDecoderContext*)pDecoder;
    
    logi("VideoStreamFrameNum, nStreamBufIndex=%d", nStreamBufIndex);
    
    if(p->pSbm[nStreamBufIndex] == NULL)
        return 0;
    
    return SbmStreamFrameNum(p->pSbm[nStreamBufIndex]);
}

void* VideoStreamDataInfoPointer(VideoDecoder* pDecoder, int nStreamBufIndex)
{
    VideoDecoderContext* p = (VideoDecoderContext*)pDecoder;

    logi("VideoStreamDataSize, nStreamBufIndex=%d", nStreamBufIndex);

    if(p->pSbm[nStreamBufIndex] == NULL)
        return NULL;

    return SbmBufferDataInfo(p->pSbm[nStreamBufIndex]);
}


VideoPicture* RequestPicture(VideoDecoder* pDecoder, int nStreamIndex)
{
    VideoDecoderContext* p;
    Fbm*                 pFbm;
    VideoPicture*        pPicture;
    
    logi("RequestPicture, nStreamIndex=%d", nStreamIndex);
    
    p = (VideoDecoderContext*)pDecoder;
    
    pFbm = p->pFbm[nStreamIndex];
    if(pFbm == NULL)
    {
        //* get FBM handle from video engine.
        if(p->pVideoEngine != NULL)
            pFbm = p->pFbm[nStreamIndex] = VideoEngineGetFbm(p->pVideoEngine, nStreamIndex);
        
        if(pFbm == NULL)
        {
        	logi("Fbm module of video stream %d not create yet, RequestPicture fail.", nStreamIndex);
        	return NULL;
        }
    }
    
    //* request picture.
    pPicture = FbmRequestPicture(pFbm);
    
    if(pPicture != NULL)
    {
        //* set stream index to the picture, it will be useful when picture returned.
        pPicture->nStreamIndex = nStreamIndex;
        
        //* update video stream information.
        UpdateVideoStreamInfo(p, pPicture);
        logv("picture w: %d,  h: %d, addr: %x, top: %d, bottom: %d, left: %d, right: %d, stride: %d",
        		pPicture->nWidth, pPicture->nHeight, (size_addr)pPicture,
        		pPicture->nTopOffset, pPicture->nBottomOffset,
        		pPicture->nLeftOffset, pPicture->nRightOffset, pPicture->nLineStride);

#if DEBUG_SAVE_PICTURE
#if 0
        if(count == 0)
        {
        	logd("width=%d, height=%d\n", pPicture->nWidth, pPicture->nHeight);
        	FILE*fp1 = NULL;
        	logd("  vdecoder. saving y. size: %d", pPicture->nWidth*pPicture->nHeight);
        	fp1 = fopen("/data/camera/y.dat", "wb");
        	fwrite(pPicture->pData0, 1, pPicture->nWidth*pPicture->nHeight, fp1);

            fclose(fp1);
           	fp1 = fopen("/data/camera/u.dat", "wb");
           	logd("  vdecoder. saving u. addr: %x", pPicture->pData1);
            fwrite(pPicture->pData1, 1, pPicture->nWidth*pPicture->nHeight, fp1);
            fclose(fp1);

        }
#else
        if(count < 30)
        {
        	char name[128];
        	FILE *fp;

        	sprintf(name, "/data/camera/cts_%d.nv12", count);
//        	sprintf(name, "/data/camera/cts.nv12");
        	fp = fopen(name, "ab");
        	if(fp != NULL)
        	{
        		logd("saving picture, size %dx%d, format: %d",
        				pPicture->nWidth, pPicture->nHeight, pPicture->ePixelFormat);
        		fwrite(pPicture->pData0, 1, pPicture->nWidth * pPicture->nHeight * 3/2, fp);
        		fclose(fp);
        	}
        	else
        	{
        		loge("saving picture open file error, frame number: %d", count);
        	}
        }
#endif
        count++;

#endif
    }
    
    return pPicture;
}


int ReturnPicture(VideoDecoder* pDecoder, VideoPicture* pPicture)
{
    VideoDecoderContext* p;
    Fbm*                 pFbm;
    int                  nStreamIndex;
    int                  ret;
    
    logi("ReturnPicture, pPicture=%p", pPicture);
    
    p = (VideoDecoderContext*)pDecoder;
    
    nStreamIndex = pPicture->nStreamIndex;
    if(nStreamIndex < 0 || nStreamIndex > 2)
    {
        loge("invalid stream index(%d), pPicture->nStreamIndex must had been \
                changed by someone incorrectly, ReturnPicture fail.", nStreamIndex);
        return -1;
    }
    
    pFbm = p->pFbm[nStreamIndex];
    if(pFbm == NULL)
    {
        logw("pFbm is NULL when returning a picture, ReturnPicture fail.");
        return -1;
    }
    
    ret = FbmReturnPicture(pFbm, pPicture);
    if(ret != 0)
    {
        logw("FbmReturnPicture return fail, it means the picture being returned it not one of this FBM.");
    }
    
    return ret;
}


VideoPicture* NextPictureInfo(VideoDecoder* pDecoder, int nStreamIndex)
{
    VideoDecoderContext* p;
    Fbm*                 pFbm;
    VideoPicture*        pPicture;
    
    logi("RequestPicture, nStreamIndex=%d", nStreamIndex);
    
    p = (VideoDecoderContext*)pDecoder;
    
    pFbm = p->pFbm[nStreamIndex];
    if(pFbm == NULL)
    {
        //* get FBM handle from video engine.
        if(p->pVideoEngine != NULL)
            pFbm = p->pFbm[nStreamIndex] = VideoEngineGetFbm(p->pVideoEngine, nStreamIndex);
        
        logi("Fbm module of video stream %d not create yet, NextPictureInfo() fail.", nStreamIndex);
        return NULL;
    }
    
    //* request picture.
    pPicture = FbmNextPictureInfo(pFbm);
    
    if(pPicture != NULL)
    {
        //* set stream index to the picture, it will be useful when picture returned.
        pPicture->nStreamIndex = nStreamIndex;
        
        //* update video stream information.
        UpdateVideoStreamInfo(p, pPicture);
    }
    
    return pPicture;
}


int TotalPictureBufferNum(VideoDecoder* pDecoder, int nStreamIndex)
{
    VideoDecoderContext* p;
    Fbm*                 pFbm;
    
    logv("TotalPictureBufferNum, nStreamIndex=%d", nStreamIndex);
    
    p    = (VideoDecoderContext*)pDecoder;
    pFbm = p->pFbm[nStreamIndex];
    if(pFbm == NULL)
    {
        //* get FBM handle from video engine.
        if(p->pVideoEngine != NULL)
            pFbm = p->pFbm[nStreamIndex] = VideoEngineGetFbm(p->pVideoEngine, nStreamIndex);
        
        logi("Fbm module of video stream %d not create yet, TotalPictureBufferNum() fail.", nStreamIndex);
        return 0;
    }
    
    return FbmTotalBufferNum(pFbm);
}


int EmptyPictureBufferNum(VideoDecoder* pDecoder, int nStreamIndex)
{
    VideoDecoderContext* p;
    Fbm*                 pFbm;
    
    logv("EmptyPictureBufferNum, nStreamIndex=%d", nStreamIndex);
    
    p    = (VideoDecoderContext*)pDecoder;
    pFbm = p->pFbm[nStreamIndex];
    if(pFbm == NULL)
    {
        //* get FBM handle from video engine.
        if(p->pVideoEngine != NULL)
            pFbm = p->pFbm[nStreamIndex] = VideoEngineGetFbm(p->pVideoEngine, nStreamIndex);
        
        logi("Fbm module of video stream %d not create yet, EmptyPictureBufferNum() fail.", nStreamIndex);
        return 0;
    }
    
    return FbmEmptyBufferNum(pFbm);
}


int ValidPictureNum(VideoDecoder* pDecoder, int nStreamIndex)
{
    VideoDecoderContext* p;
    Fbm*                 pFbm;
    
//    logv("ValidPictureNum, nStreamIndex=%d", nStreamIndex);
    
    p    = (VideoDecoderContext*)pDecoder;
    pFbm = p->pFbm[nStreamIndex];
    if(pFbm == NULL)
    {
        //* get FBM handle from video engine.
        if(p->pVideoEngine != NULL)
            pFbm = p->pFbm[nStreamIndex] = VideoEngineGetFbm(p->pVideoEngine, nStreamIndex);
        
        logi("Fbm module of video stream %d not create yet, ValidPictureNum() fail.", nStreamIndex);
        return 0;
    }
    
    return FbmValidPictureNum(pFbm);
}

int DecoderSendToDisplayPictureNum(VideoDecoder* pDecoder, int nStreamIndex)
{
    VideoDecoderContext* p;
    Fbm*                 pFbm;

    p    = (VideoDecoderContext*)pDecoder;
    pFbm = p->pFbm[nStreamIndex];
    if(pFbm == NULL)
    {
        if(p->pVideoEngine != NULL)
            pFbm = p->pFbm[nStreamIndex] = VideoEngineGetFbm(p->pVideoEngine, nStreamIndex);
        logi("Fbm module of video stream %d not create yet, ValidPictureNum() fail.", nStreamIndex);
        return 0;
    }
    return FbmGetDisplayBufferNum(pFbm);
}
int ConfigHorizonScaleDownRatio(VideoDecoder* pDecoder, int nScaleDownRatio)
{
    VideoDecoderContext* p;
    
    logv("ConfigHorizonScaleDownRatio, nScaleDownRatio=%d", nScaleDownRatio);
    
    p = (VideoDecoderContext*)pDecoder;
    if(p->pVideoEngine != NULL)
    {
        logw("video decoder has been initialized, connot config horizon scaledown.");
        return -1;
    }
    
    p->vconfig.nHorizonScaleDownRatio = nScaleDownRatio;
    if(p->vconfig.nHorizonScaleDownRatio != 0 || p->vconfig.nVerticalScaleDownRatio != 0)
        p->vconfig.bScaleDownEn = 1;
    else
        p->vconfig.bScaleDownEn = 0;
    
    return 0;
}


int ConfigVerticalScaleDownRatio(VideoDecoder* pDecoder, int nScaleDownRatio)
{
    VideoDecoderContext* p;
    
    logv("ConfigVerticalScaleDownRatio, nScaleDownRatio=%d", nScaleDownRatio);
    
    p = (VideoDecoderContext*)pDecoder;
    if(p->pVideoEngine != NULL)
    {
        logw("video decoder has been initialized, connot config vertical scaledown.");
        return -1;
    }
    
    p->vconfig.nVerticalScaleDownRatio = nScaleDownRatio;
    if(p->vconfig.nHorizonScaleDownRatio != 0 || p->vconfig.nVerticalScaleDownRatio != 0)
        p->vconfig.bScaleDownEn = 1;
    else
        p->vconfig.bScaleDownEn = 0;
    
    return 0;
}

int ConfigSecHorizonScaleDownRatio(VideoDecoder* pDecoder, int nScaleDownRatio)
{
    VideoDecoderContext* p;

    logv("ConfigSecHorizonScaleDownRatio, nScaleDownRatio=%d", nScaleDownRatio);

    p = (VideoDecoderContext*)pDecoder;
    if(p->pVideoEngine != NULL)
    {
        logw("video decoder has been initialized, connot config horizon scaledown.");
        return -1;
    }

    p->vconfig.nSecHorizonScaleDownRatio = nScaleDownRatio;
    if(p->vconfig.nSecHorizonScaleDownRatio != 0 || p->vconfig.nSecVerticalScaleDownRatio != 0)
        p->vconfig.bScaleDownEn = 1;
    else
        p->vconfig.bScaleDownEn = 0;

    return 0;
}


int ConfigSecVerticalScaleDownRatio(VideoDecoder* pDecoder, int nScaleDownRatio)
{
    VideoDecoderContext* p;

    logv("ConfigSecVerticalScaleDownRatio, nScaleDownRatio=%d", nScaleDownRatio);

    p = (VideoDecoderContext*)pDecoder;
    if(p->pVideoEngine != NULL)
    {
        logw("video decoder has been initialized, connot config vertical scaledown.");
        return -1;
    }

    p->vconfig.nSecVerticalScaleDownRatio = nScaleDownRatio;
    if(p->vconfig.nSecHorizonScaleDownRatio != 0 || p->vconfig.nSecVerticalScaleDownRatio != 0)
        p->vconfig.bScaleDownEn = 1;
    else
        p->vconfig.bScaleDownEn = 0;

    return 0;
}


int ConfigRotation(VideoDecoder* pDecoder, int nRotateDegree)
{
    VideoDecoderContext* p;
    
    logv("ConfigRotation, nRotateDegree=%d", nRotateDegree);
    
    p = (VideoDecoderContext*)pDecoder;
    if(p->pVideoEngine != NULL)
    {
        logw("video decoder has been initialized, connot config rotation.");
        return -1;
    }
    
    p->vconfig.nRotateDegree = nRotateDegree;
    if(p->vconfig.nRotateDegree != 0)
        p->vconfig.bRotationEn = 1;
    
    return 0;
}

int ConfigDeinterlace(VideoDecoder* pDecoder, int bDeinterlace)
{
    VideoDecoderContext* p;

    logv("ConfigDeinterlace, bDeinterlace=%d", bDeinterlace);

    p = (VideoDecoderContext*)pDecoder;
    if(p->pVideoEngine != NULL)
    {
        logw("video decoder has been initialized, connot config deinterlace flag.");
        return -1;
    }

    p->vconfig.bSupportMaf = bDeinterlace;
    return 0;
}


int ConfigThumbnailMode(VideoDecoder* pDecoder, int bOpenThumbnailMode)
{
    VideoDecoderContext* p;
    
    logv("ConfigThumbnailMode, bOpenThumbnailMode=%d", bOpenThumbnailMode);
    
    p = (VideoDecoderContext*)pDecoder;
    if(p->pVideoEngine != NULL)
    {
        logw("video decoder has been initialized, connot config thumbmail mode.");
        return -1;
    }
    
    p->vconfig.bThumbnailMode = bOpenThumbnailMode;
    
    return 0;
}


int ConfigOutputPicturePixelFormat(VideoDecoder* pDecoder, int ePixelFormat)
{
    VideoDecoderContext* p;
    
    if(ePixelFormat>PIXEL_FORMAT_MAX || ePixelFormat<PIXEL_FORMAT_MIN)
    {
        loge("ConfigOutputPicturePixelFormat, invalid pixel format %d", ePixelFormat);
        return -1;
    }
    
    logv("ConfigOutputPicturePixelFormat, ePixelFormat=%s", strPixelFormat[ePixelFormat]);
    
    p = (VideoDecoderContext*)pDecoder;
    if(p == NULL)
    {
        logw("video decoder has been initialized, connot config output pixel format.");
        return -1;
    }
    
    if(ePixelFormat != PIXEL_FORMAT_DEFAULT && 
       ePixelFormat != PIXEL_FORMAT_YV12 && 
       ePixelFormat != PIXEL_FORMAT_NV21 &&
       ePixelFormat != PIXEL_FORMAT_YUV_MB32_420)
    {
        if(ePixelFormat<PIXEL_FORMAT_MIN || ePixelFormat>PIXEL_FORMAT_MAX)
            logw("can not support transform picture to output %d pixel format.", ePixelFormat);
        else
            logw("can not support transform picture to output %s pixel format.", strPixelFormat[ePixelFormat]);
        logw("we support %s/%s/%s formats currently.",
                strPixelFormat[PIXEL_FORMAT_DEFAULT], 
                strPixelFormat[PIXEL_FORMAT_YV12], 
                strPixelFormat[PIXEL_FORMAT_NV21]);
        return -1;
    }
    p->vconfig.eOutputPixelFormat = ePixelFormat;
    
    return 0;
}


int ConfigNoBFrames(VideoDecoder* pDecoder, int bNoBFrames)
{
    VideoDecoderContext* p;
    
    logv("ConfigNoBFrames, bNoBFrames=%d", bNoBFrames);
    
    p = (VideoDecoderContext*)pDecoder;
    if(p->pVideoEngine != NULL)
    {
        logw("video decoder has been initialized, connot config no B frame mode.");
        return -1;
    }
    
    p->vconfig.bNoBFrames = bNoBFrames;
    
    return 0;
}


int ConfigDisable3D(VideoDecoder* pDecoder, int bDisable3D)
{
    VideoDecoderContext* p;
    
    logv("ConfigDisable3D, bDisable3D=%d", bDisable3D);
    
    p = (VideoDecoderContext*)pDecoder;
    if(p->pVideoEngine != NULL)
    {
        logw("video decoder has been initialized, connot config disable 3D mode.");
        return -1;
    }
    
    p->vconfig.bDisable3D = bDisable3D;
    
    return 0;
}

int ConfigVeMemoryThresh(VideoDecoder* pDecoder, int nMemoryThresh)
{
    VideoDecoderContext* p;


    p = (VideoDecoderContext*)pDecoder;
    AdapterMemThresh(nMemoryThresh);
    return 0;
}

//********************************************************************************************//
//********************************************************************************************//
//************************added by xyliu at 2015-12-23****************************************************//
/*if you want to scale the output video frame, but you donot know the real size of video,
 cannot set the scale params (nHorizonScaleDownRatio,nVerticalScaleDownRatio,
 bRotationEn), so you can set value to ExtraVidScaleInfo
*/
int ConfigExtraScaleInfo(VideoDecoder* pDecoder, int nWidthTh, int nHeightTh, int nHorizonScaleRatio, int nVerticalScaleRatio)
{
    VideoDecoderContext* p;
    int ret;
    DecoderInterface *pDecoderInterface;
    p = (VideoDecoderContext*)pDecoder;

    logd("*************config ConfigExtraScaleInfo\n");

    if(p == NULL ||p->pVideoEngine == NULL ||
    		p->pVideoEngine->pDecoderInterface == NULL)
    {
    	loge(" set decoder extra scale info fail ");
        return VDECODE_RESULT_UNSUPPORTED;
    }
    pDecoderInterface = p->pVideoEngine->pDecoderInterface;
    ret = 0;
    if(pDecoderInterface->SetExtraScaleInfo != NULL)
    {
    	ret = pDecoderInterface->SetExtraScaleInfo(pDecoderInterface, nWidthTh, nHeightTh,nHorizonScaleRatio, nVerticalScaleRatio);
    }
    return ret;
}
int ReopenVideoEngine(VideoDecoder* pDecoder, VConfig* pVConfig, VideoStreamInfo* pStreamInfo)
{
    int                  i;
    VideoDecoderContext* p;
    unsigned int         veVersion;
    
    logv("ReopenVideoEngine");
    
    p = (VideoDecoderContext*)pDecoder;
    if(p->pVideoEngine == NULL)
    {
        logw("video decoder is not initialized yet, ReopenVideoEngine() fail.");
        return -1;
    }

    veVersion = VeGetIcVersion();
    //* destroy the video engine.
    if((veVersion==0x1681 || veVersion==0x1689) && (p->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
		AdapterLockVideoEngine(1);
	else
    	AdapterLockVideoEngine(0);
    VideoEngineDestroy(p->pVideoEngine);
    if((veVersion==0x1681 || veVersion==0x1689) && (p->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
    		AdapterUnLockVideoEngine(1);
    	else
        	AdapterUnLockVideoEngine(0);
    
    //* FBM is destroyed when video engine is destroyed, so clear the handles.
    p->pFbm[0] = p->pFbm[1] = NULL;
    p->nFbmNum = 0;
    p->pVideoEngine = NULL;
    
    //* create a video engine again, 
    //* before that, we should set vconfig and videoStreamInfo again.
    if(p->videoStreamInfo.pCodecSpecificData != NULL)
    {
        free(p->videoStreamInfo.pCodecSpecificData);
        p->videoStreamInfo.pCodecSpecificData = NULL;
    }

    memcpy(&p->vconfig,pVConfig,sizeof(VConfig));
    memcpy(&p->videoStreamInfo,pStreamInfo,sizeof(VideoStreamInfo));
	pStreamInfo->bReOpenEngine = 1;
    if(pStreamInfo->pCodecSpecificData)
    {
        p->videoStreamInfo.pCodecSpecificData = (char*)malloc(pStreamInfo->nCodecSpecificDataLen);
        if(p->videoStreamInfo.pCodecSpecificData == NULL)
        {
            loge("malloc video codec specific data fail!");
            return -1;
        }
        memcpy(p->videoStreamInfo.pCodecSpecificData,
               pStreamInfo->pCodecSpecificData,
               pStreamInfo->nCodecSpecificDataLen);
        p->videoStreamInfo.nCodecSpecificDataLen = pStreamInfo->nCodecSpecificDataLen;
    }
    else
    {
       p->videoStreamInfo.pCodecSpecificData    = NULL;
       p->videoStreamInfo.nCodecSpecificDataLen = 0;
    }

    if((veVersion==0x1681 || veVersion==0x1689) && (p->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
		AdapterLockVideoEngine(1);
	else
    	AdapterLockVideoEngine(0);
    p->pVideoEngine = VideoEngineCreate(&p->vconfig, &p->videoStreamInfo);
    if((veVersion==0x1681 || veVersion==0x1689) && (p->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
		AdapterUnLockVideoEngine(1);
	else
    	AdapterUnLockVideoEngine(0);
    if(p->pVideoEngine == NULL)
    {
        loge("VideoEngineCreate fail, can not reopen the video engine.");
        return -1;
    }
    
    //* set sbm module to the video engine.
    if((veVersion==0x1681 || veVersion==0x1689) && (p->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
		AdapterLockVideoEngine(1);
	else
    	AdapterLockVideoEngine(0);
    for(i=0; i<p->nSbmNum; i++)
        VideoEngineSetSbm(p->pVideoEngine, p->pSbm[i], i);
    if((veVersion==0x1681 || veVersion==0x1689) && (p->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
		AdapterUnLockVideoEngine(1);
	else
    	AdapterUnLockVideoEngine(0);
    
    return 0;
}


int RotatePictureHw(VideoDecoder* pDecoder,
                    VideoPicture* pPictureIn, 
                    VideoPicture* pPictureOut, 
                    int           nRotateDegree)
{
    
setup_tr:
    logv("RotatePictureHw()");
    
    VideoDecoderContext* p;
    size_addr arg[6] = {0};
    tr_info tTrInfo;

    p = (VideoDecoderContext*)pDecoder;
    if(pPictureIn == NULL || pPictureOut == NULL || p == NULL)
    {
        loge("**the pPicturnIn or pPictureOut is NULL: [%p] ,[%p],[%p]",pPictureIn,pPictureOut,p);
        return -1;
    }
    
    if(p->bInitTrFlag == 0)
    {
        //* open the tr driver
        p->nTrhandle = open(TRANSFORM_DRIVE_NAME,O_RDWR);
        logv("open transform drive ret = %d",p->nTrhandle);
        
        if(p->nTrhandle < 0)
        {
            loge("*****open tr driver fail!");
            return -1;
        }

        //* request tr channel
        size_addr arg[6] = {0};
        p->nTrChannel = ioctl(p->nTrhandle,TR_REQUEST,(void*)arg);
        logv("request tr channel  = 0x%x",p->nTrChannel);
        
        if(p->nTrChannel == 0)
        {
            loge("#######error: tr_request failed!");
            return -1;
        }

        //* set tr timeout
        arg[0] = p->nTrChannel;
        arg[1] = TRANSFORM_DEV_TIMEOUT;
        if(ioctl(p->nTrhandle,TR_SET_TIMEOUT,(void*)arg) != 0)
        {
            loge("#######error: tr_set_timeout failed!");
            return -1;
        }

        p->bInitTrFlag = 1;
    }
    
    if(p->nTrhandle < 0)
    {
        loge("the p->nTrhandle is invalid : %d",p->nTrhandle);
        return -1;
    }
    
    pPictureOut->nPts = pPictureIn->nPts;

    //* set rotation angle 
    if(nRotateDegree == 0)
        tTrInfo.mode = TR_ROT_0;
    else if(nRotateDegree == 90)
        tTrInfo.mode = TR_ROT_90;
    else if(nRotateDegree == 180)
        tTrInfo.mode = TR_ROT_180;
    else if(nRotateDegree == 270)
        tTrInfo.mode = TR_ROT_270;
    else
        tTrInfo.mode = TR_ROT_0;
    
    //* set in picture
    if(pPictureIn->ePixelFormat == PIXEL_FORMAT_YV12)
        tTrInfo.src_frame.fmt = TR_FORMAT_YUV420_P;
    else if(pPictureIn->ePixelFormat == PIXEL_FORMAT_NV21)
        tTrInfo.src_frame.fmt = TR_FORMAT_YUV420_SP_UVUV;
    else
    {
        loge("***the ePixelFormat[0x%x] is not support by tr driver",pPictureIn->ePixelFormat);
        return -1;
    }

    tTrInfo.src_frame.haddr[0]  = 0;
    tTrInfo.src_frame.haddr[1]  = 0;
    tTrInfo.src_frame.haddr[2]  = 0;
    if(pPictureIn->ePixelFormat == PIXEL_FORMAT_YV12)
    {
        tTrInfo.src_frame.laddr[0]  = (size_addr)MemAdapterGetPhysicAddressCpu((void*)pPictureIn->pData0);
        tTrInfo.src_frame.laddr[1]  = (size_addr)MemAdapterGetPhysicAddressCpu((void*)pPictureIn->pData1);
        tTrInfo.src_frame.laddr[2]  = (size_addr)MemAdapterGetPhysicAddressCpu((void*)pPictureIn->pData2);
        tTrInfo.src_frame.pitch[0]  = (pPictureIn->nLineStride + 15)& ~15; 
        tTrInfo.src_frame.pitch[1]  = ((pPictureIn->nLineStride/2 + 15) & ~15);
        tTrInfo.src_frame.pitch[2]  = ((pPictureIn->nLineStride/2 + 15) & ~15);
        tTrInfo.src_frame.height[0] = (pPictureIn->nHeight + 15) & ~15;
        tTrInfo.src_frame.height[1] = ((pPictureIn->nHeight + 15) & ~15)/2;
        tTrInfo.src_frame.height[2] = ((pPictureIn->nHeight + 15) & ~15)/2;
    }
    else
    {
        tTrInfo.src_frame.laddr[0]  = (size_addr)MemAdapterGetPhysicAddressCpu((void*)pPictureIn->pData0);
        tTrInfo.src_frame.laddr[1]  = (size_addr)MemAdapterGetPhysicAddressCpu((void*)pPictureIn->pData1);
        tTrInfo.src_frame.laddr[2]  = 0;
        tTrInfo.src_frame.pitch[0]  = (pPictureIn->nLineStride + 15)& ~15; 
        tTrInfo.src_frame.pitch[1]  = ((pPictureIn->nLineStride + 15) & ~15)/2;
        tTrInfo.src_frame.pitch[2]  = 0;
        tTrInfo.src_frame.height[0] = (pPictureIn->nHeight + 15) & ~15;
        tTrInfo.src_frame.height[1] = ((pPictureIn->nHeight + 15) & ~15)/2;
        tTrInfo.src_frame.height[2] = 0;
    }
    
    tTrInfo.src_rect.x = 0;
    tTrInfo.src_rect.y = 0;
    tTrInfo.src_rect.w = pPictureIn->nWidth/*(pPictureIn->nLineStride + 15)& ~15*/;
    tTrInfo.src_rect.h = pPictureIn->nHeight/*(pPictureIn->nHeight + 15) & ~15*/;
    
    tTrInfo.dst_frame.fmt       = TR_FORMAT_YUV420_P; //* just can output yv12
    tTrInfo.dst_frame.haddr[0]  = 0;
    tTrInfo.dst_frame.haddr[1]  = 0;
    tTrInfo.dst_frame.haddr[2]  = 0;
    tTrInfo.dst_frame.laddr[0]  = (size_addr)pPictureOut->pData0;
    tTrInfo.dst_frame.laddr[1]  = (size_addr)pPictureOut->pData1;
    tTrInfo.dst_frame.laddr[2]  = (size_addr)pPictureOut->pData2;

    tTrInfo.dst_frame.pitch[0]  = (pPictureOut->nLineStride + 31) & ~31;
    tTrInfo.dst_frame.pitch[1]  = ((pPictureOut->nLineStride + 31) & ~31)/2;
    tTrInfo.dst_frame.pitch[2]  = ((pPictureOut->nLineStride + 31) & ~31)/2;
    tTrInfo.dst_frame.height[0] = pPictureOut->nHeight;
    tTrInfo.dst_frame.height[1] = pPictureOut->nHeight/2;
    tTrInfo.dst_frame.height[2] = pPictureOut->nHeight/2;
    
    tTrInfo.dst_rect.x = 0;
    tTrInfo.dst_rect.y = 0;
    tTrInfo.dst_rect.w = pPictureOut->nWidth/*(pPictureOut->nLineStride + 31) & ~31*/;
    tTrInfo.dst_rect.h = pPictureOut->nHeight;

    //* printf the tr info
    #if 0
    logd("***********************************************");
    logd("*********************tr info*******************");
    logd("    tTrInfo.mode(rotationAngle) = 0x%x",tTrInfo.mode);
    logd("    src_frame.fmt(ePixelFormat) = 0x%x",tTrInfo.src_frame.fmt);
    logd("### pictureIn size: nLineStride = %d, nWidht = %d, nHeight = %d, offset : %d, %d, %d,%d",
            pPictureIn->nLineStride,
            pPictureIn->nWidth,
            pPictureIn->nHeight,
            pPictureIn->nLeftOffset,
            pPictureIn->nRightOffset,
            pPictureIn->nTopOffset,
            pPictureIn->nBottomOffset);
    logd("###pictureOut size: nLineStride = %d, nWidht = %d, nHeight = %d",
      pPictureOut->nLineStride,
      pPictureOut->nWidth,
      pPictureOut->nHeight);
    logd("src_frame -- laddr: 0x%x 0x%x 0x%x, pitch: %d %d %d, heitht: %d %d %d, w[%d],h[%d]",
          tTrInfo.src_frame.laddr[0],
          tTrInfo.src_frame.laddr[1],
          tTrInfo.src_frame.laddr[2],
          tTrInfo.src_frame.pitch[0],
          tTrInfo.src_frame.pitch[1],
          tTrInfo.src_frame.pitch[2],
          tTrInfo.src_frame.height[0],
          tTrInfo.src_frame.height[1],
          tTrInfo.src_frame.height[2],
          tTrInfo.src_rect.w,
          tTrInfo.src_rect.h);
    logd("dst_frame -- laddr: 0x%x 0x%x 0x%x, pitch: %d %d %d, heitht: %d %d %d, w[%d],h[%d]",
          tTrInfo.dst_frame.laddr[0],
          tTrInfo.dst_frame.laddr[1],
          tTrInfo.dst_frame.laddr[2],
          tTrInfo.dst_frame.pitch[0],
          tTrInfo.dst_frame.pitch[1],
          tTrInfo.dst_frame.pitch[2],
          tTrInfo.dst_frame.height[0],
          tTrInfo.dst_frame.height[1],
          tTrInfo.dst_frame.height[2],
          tTrInfo.dst_rect.w,
          tTrInfo.dst_rect.h);
    logd("*******************end*************************");
    logd("***********************************************\n\n");
    #endif
    //* setup rotate
    arg[0] = p->nTrChannel;
    arg[1] = (size_addr)&tTrInfo;
    arg[2] = 0;
    arg[3] = 0;
    
    logv("***setup rotate start!: arg: 0x%x, 0x%x",arg[0],arg[1]);
    if(ioctl(p->nTrhandle,TR_COMMIT,(void*)arg) != 0)
    {
        loge("######error: setup rotate failed!");
        return -1;
    }
    logv("***setup rotate finish!");
    
    //* wait 
    arg[0] = p->nTrChannel;
    arg[1] = 0;
    arg[2] = 0;
    arg[3] = 0;
    
    logv("***tr query start!");
    int nTimeOut = 0;
    int ret = ioctl(p->nTrhandle,TR_QUERY,(void*)arg);
    
    while(ret == 1)// 0 : suceef; 1:busy ; -1:timeOut 
    {
    	logv("***tr sleep!");
        usleep(1*1000);
    	ret = ioctl(p->nTrhandle,TR_QUERY,(void*)arg);
    }

    //* if the tr is timeout ,we should setup tr again
    if(ret == -1)
        goto setup_tr;
    
    logv("***tr query finish!");
    
    return 0;
}

int RotatePicture(VideoPicture* pPictureIn, 
                  VideoPicture* pPictureOut, 
                  int           nRotateDegree,
                  int           nGpuYAlign,
                  int           nGpuCAlign)
{

	VideoPicture* pOrgPicture = NULL;
	VideoPicture* pPictureMid = NULL;
	int bMallocBuffer = 0;
	int nMemSizeY = 0;
	int nMemSizeC = 0;
	int ret = 0;

    if(pPictureOut == NULL)
    {
    	return -1;
    }
    GetBufferSize(pPictureIn->ePixelFormat, pPictureIn->nWidth, pPictureIn->nHeight, &nMemSizeY, &nMemSizeC, NULL, NULL, 0);
    AdapterMemFlushCache((void*)pPictureIn->pData0, nMemSizeY+2*nMemSizeC);
    pPictureOut->nPts = pPictureIn->nPts;
    pOrgPicture = pPictureIn;


    if(pPictureOut->ePixelFormat != pPictureIn->ePixelFormat)
    {
    	if(nRotateDegree == 0)
    	{
    		ConvertPixelFormat(pPictureIn, pPictureOut);
    		return 0;
    	}
    	else
    	{
    	    int nMemSizeY = 0;
    	    int nMemSizeC = 0;
    	    int nHeight16Align = 0;

    		pPictureMid = malloc(sizeof(VideoPicture));
    		pPictureMid->ePixelFormat =  pPictureOut->ePixelFormat;
    		pPictureMid->nWidth =  pPictureIn->nWidth;
       		pPictureMid->nHeight =  pPictureIn->nHeight;
       		if((pPictureMid->ePixelFormat==PIXEL_FORMAT_YV12)
       				|| (pPictureMid->ePixelFormat==PIXEL_FORMAT_NV21))
       	    {

       			nHeight16Align = (pPictureIn->nHeight+15) &~ 15;
       			nMemSizeY = pPictureIn->nLineStride*nHeight16Align;
       			nMemSizeC = nMemSizeY>>2;
       			pPictureMid->pData0 = malloc(nMemSizeY+nMemSizeC*2);
       			if(pPictureMid->pData0 == NULL)
       			{
       				logd("malloc memory for pPictureMid failed\n");
       			}
       	  		ConvertPixelFormat(pPictureIn, pPictureMid);
       	  		pOrgPicture = pPictureMid;
       	  		bMallocBuffer = 1;
       	    }
    	}
    }

    if(nRotateDegree == 0)
    {
    	ret = RotatePicture0Degree(pOrgPicture, pPictureOut,nGpuYAlign,nGpuCAlign);
    }
    else if(nRotateDegree == 90)
    {
    	ret = RotatePicture90Degree(pOrgPicture, pPictureOut);
    }
    else if(nRotateDegree == 180)
    {
    	ret = RotatePicture180Degree(pOrgPicture, pPictureOut);
    }
    else if(nRotateDegree == 270)
    {
    	ret = RotatePicture270Degree(pOrgPicture, pPictureOut);
    }
    if(pPictureMid != NULL)
    {
    	if(bMallocBuffer)
    		free(pPictureMid->pData0);
    	free(pPictureMid);
    }
    return ret;
}


VideoPicture* AllocatePictureBuffer(int nWidth, int nHeight, int nLineStride, int ePixelFormat)
{
    VideoPicture* p;
    
    p = (VideoPicture*)malloc(sizeof(VideoPicture));
    
    logi("AllocatePictureBuffer, nWidth=%d, nHeight=%d, nLineStride=%d, ePixelFormat=%s",
            nWidth, nHeight, nLineStride, strPixelFormat[ePixelFormat]);
            
    if(p == NULL)
    {
        logw("memory alloc fail, allocate %d bytes in AllocatePictureBuffer()", (int)sizeof(VideoPicture));
        return NULL;
    }
    
    memset(p, 0, sizeof(VideoPicture));
    p->nWidth       = nWidth;
    p->nHeight      = nHeight;
    p->nLineStride  = nLineStride;
    p->ePixelFormat = ePixelFormat;
    int nAlignValue = 0;
    
    if(FbmAllocatePictureBuffer(p, &nAlignValue, nWidth, nHeight) != 0)
    {
        logw("memory alloc fail, FbmAllocatePictureBuffer() fail in AllocatePictureBuffer().");
        free(p);
        p = NULL;
    }
    
    return p;
}


int FreePictureBuffer(VideoPicture* pPicture)
{
    logi("FreePictureBuffer, pPicture=%p", pPicture);
    
    if(pPicture == NULL)
    {
        loge("invaid picture, FreePictureBuffer fail.");
        return -1;
    }
    
    FbmFreePictureBuffer(pPicture);
    free(pPicture);
    return 0;
}


static int DecideStreamBufferSize(VideoDecoderContext* p)
{
    int nVideoHeight;
    int eCodecFormat;
    int nBufferSize;
    
    if(p->vconfig.nVbvBufferSize>=0x10000 && p->vconfig.nVbvBufferSize<=0x800000)
    {
    	nBufferSize = p->vconfig.nVbvBufferSize;
    	return nBufferSize;
    }
    //* we decide stream buffer size by resolution and codec format.
    nVideoHeight = p->videoStreamInfo.nHeight;
    eCodecFormat = p->videoStreamInfo.eCodecFormat;

    //* for skia create sbm
    if((p->vconfig.eOutputPixelFormat == PIXEL_FORMAT_RGBA) 
    	&& (eCodecFormat == VIDEO_CODEC_FORMAT_MJPEG)
    	&& p->vconfig.nVbvBufferSize)
    {
    	nBufferSize = p->vconfig.nVbvBufferSize;
    	return nBufferSize;
    }
    
    //* if resolution is unknown, treat it as full HD source.
    if(nVideoHeight == 0)
        nVideoHeight = 1080;
        
    if(nVideoHeight < 480)
        nBufferSize = 2*1024*1024;
    else if (nVideoHeight < 720)
        nBufferSize = 4*1024*1024;
    else if(nVideoHeight < 1080)
        nBufferSize = 6*1024*1024;
    else if(nVideoHeight < 2160)
        nBufferSize = 8*1024*1024;
    else
    	nBufferSize = 12*1024*1024;
    
    if(eCodecFormat == VIDEO_CODEC_FORMAT_MJPEG ||
       eCodecFormat == VIDEO_CODEC_FORMAT_MPEG1 ||
       eCodecFormat == VIDEO_CODEC_FORMAT_MPEG2)
    {
        nBufferSize += 2*1024*1024; //* for old codec format, compress rate is low.
    }
    if(eCodecFormat == VIDEO_CODEC_FORMAT_RX)
    {
    	nBufferSize = 16*1024*1024;
    }
    return nBufferSize;
}


static void UpdateVideoStreamInfo(VideoDecoderContext* p, VideoPicture* pPicture)
{
    //* update video resolution.
    if(p->vconfig.bRotationEn   == 0 || 
       p->vconfig.nRotateDegree == 0 || 
       p->vconfig.nRotateDegree == 3)
    {
        //* the picture was not rotated or rotated by 180 degree.
        if(p->vconfig.bScaleDownEn == 0)
        {
            //* the picture was not scaled.
            if(p->videoStreamInfo.nWidth  != pPicture->nWidth ||
               p->videoStreamInfo.nHeight != pPicture->nHeight)
            {
                p->videoStreamInfo.nWidth  = pPicture->nWidth;
                p->videoStreamInfo.nHeight = pPicture->nHeight;
            }
        }
        else
        {
            //* the picture was scaled.
            if(p->videoStreamInfo.nWidth  != (pPicture->nWidth<<p->vconfig.nHorizonScaleDownRatio) ||
               p->videoStreamInfo.nHeight != (pPicture->nHeight<<p->vconfig.nVerticalScaleDownRatio))
            {
                p->videoStreamInfo.nWidth  = pPicture->nWidth << p->vconfig.nHorizonScaleDownRatio;
                p->videoStreamInfo.nHeight = pPicture->nHeight << p->vconfig.nVerticalScaleDownRatio;
            }
        }
    }
    else
    {
        //* the picture was rotated by 90 or 270 degree.
        if(p->vconfig.bScaleDownEn == 0)
        {
            //* the picture was not scaled.
            if(p->videoStreamInfo.nWidth  != pPicture->nHeight ||
               p->videoStreamInfo.nHeight != pPicture->nWidth)
            {
                p->videoStreamInfo.nWidth  = pPicture->nHeight;
                p->videoStreamInfo.nHeight = pPicture->nWidth;
            }
        }
        else
        {
            //* the picture was scaled.
            if(p->videoStreamInfo.nWidth  != (pPicture->nHeight<<p->vconfig.nHorizonScaleDownRatio) ||
               p->videoStreamInfo.nHeight != (pPicture->nWidth<<p->vconfig.nVerticalScaleDownRatio))
            {
                p->videoStreamInfo.nWidth  = pPicture->nHeight << p->vconfig.nHorizonScaleDownRatio;
                p->videoStreamInfo.nHeight = pPicture->nWidth << p->vconfig.nVerticalScaleDownRatio;
            }
        }
    }
    
    //* update frame rate.
    if(p->videoStreamInfo.nFrameRate != pPicture->nFrameRate && pPicture->nFrameRate != 0)
        p->videoStreamInfo.nFrameRate = pPicture->nFrameRate;
    
    return;
}


static void CheckConfiguration(VideoDecoderContext* p)
{
    //* Decoder can not support rotation, scaledown or output pixel format configuration
    //* in some special cases.
    //* we need to check these cases and fix the configuration.
	VideoDecoderContext* pp = p;
    
    return;
}

char* VideoRequestSecureBuffer(VideoDecoder* pDecoder,int nBufferSize)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when request secure buffer");
        return NULL;
    }
    return (char*)AdapterSecureMemAdapterAlloc(nBufferSize);
}

void VideoReleaseSecureBuffer(VideoDecoder* pDecoder,char* pBuf)
{
    if(pDecoder == NULL)
    {
        loge("the pDecoder is null when release secure buffer");
        return ;
    }
    
    AdapterSecureMemAdapterFree((void*)pBuf);

    return ;
}

//*****************************************************************************************//
//***added new interface function for new display structure********************************//
//******************************************************************************************//
FbmBufInfo* GetVideoFbmBufInfo(VideoDecoder* pDecoder)
{
	 VideoDecoderContext* p;
	 int nStreamIndex = 0;

	 p = (VideoDecoderContext*)pDecoder;

	 if(p == NULL)
	 {
		 return NULL;
	 }
	 if(p->pVideoEngine != NULL)
	 {
		 if(VideoEngineGetFbm(p->pVideoEngine, nStreamIndex) == NULL)
		 {
			 return NULL;
		 }
		 return FbmGetVideoBufferInfo(&(p->pVideoEngine->fbmInfo));
	 }
	 return NULL;
}


VideoPicture* SetVideoFbmBufAddress(VideoDecoder* pDecoder, VideoPicture* pVideoPicture, int bForbidUseFlag)
{
	VideoDecoderContext* p;
	p = (VideoDecoderContext*)pDecoder;

	if(p == NULL)
	{
		return NULL;
	}
	if(p->pVideoEngine != NULL)
	{
		return FbmSetFbmBufAddress(&(p->pVideoEngine->fbmInfo), pVideoPicture, bForbidUseFlag);
	}
	return NULL;
}

int SetVideoFbmBufRelease(VideoDecoder* pDecoder)
{
	VideoDecoderContext* p;
	p = (VideoDecoderContext*)pDecoder;

	if(p == NULL)
	{
		return -1;
	}
	if(p->pVideoEngine != NULL)
	{
		return FbmSetNewDispRelease(&(p->pVideoEngine->fbmInfo));
	}
	return -1;
}

VideoPicture* RequestReleasePicture(VideoDecoder* pDecoder)
{
	VideoDecoderContext* p;
	p = (VideoDecoderContext*)pDecoder;

	if(p == NULL)
	{
		return NULL;
	}
	if(p->pVideoEngine != NULL)
	{
		return FbmRequestReleasePicture(&(p->pVideoEngine->fbmInfo));
	}
	return NULL;
}

VideoPicture*  ReturnRelasePicture(VideoDecoder* pDecoder, VideoPicture* pVpicture, int bForbidUseFlag)
{
	VideoDecoderContext* p;
	p = (VideoDecoderContext*)pDecoder;

	if(p == NULL)
	{
		return NULL;
	}
	if(p->pVideoEngine != NULL)
	{
		return FbmReturnReleasePicture(&(p->pVideoEngine->fbmInfo), pVpicture, bForbidUseFlag);
	}
	return NULL;
}

int SetDecodePerformCmd(VideoDecoder* pDecoder, enum EVDECODERSETPERFORMCMD performCmd)
{
	 VideoDecoderContext* p;
	 int ret;
	 DecoderInterface *pDecoderInterface;
	 unsigned int veVersion = 0;
	 p = (VideoDecoderContext*)pDecoder;

	 if(p == NULL ||p->pVideoEngine == NULL ||
	      p->pVideoEngine->pDecoderInterface == NULL)
	 {
	    	loge("SetDecodeDebugCmd fail\n");
	        return VDECODE_RESULT_UNSUPPORTED;
	 }
	 veVersion = VeGetIcVersion();
	 if((veVersion==0x1681 || veVersion==0x1689) && (p->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
		 AdapterLockVideoEngine(1);
	 else
	     AdapterLockVideoEngine(0);
	 pDecoderInterface = p->pVideoEngine->pDecoderInterface;
	 ret = 0;
	 if(pDecoderInterface->SetExtraScaleInfo != NULL)
	 {
		 ret = pDecoderInterface->SetPerformCmd(pDecoderInterface,performCmd);
	 }
	 if((veVersion==0x1681 || veVersion==0x1689) && (p->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
		 AdapterUnLockVideoEngine(1);
	 else
	     AdapterUnLockVideoEngine(0);
	 return ret;
}


int GetDecodePerformInfo(VideoDecoder* pDecoder, enum EVDECODERGETPERFORMCMD performCmd, VDecodePerformaceInfo** performInfo)
{
	 VideoDecoderContext* p;
	 int ret;
	 DecoderInterface *pDecoderInterface;
	 p = (VideoDecoderContext*)pDecoder;

	 if(p == NULL ||p->pVideoEngine == NULL ||
	      p->pVideoEngine->pDecoderInterface == NULL)
	 {
	    	loge("GetDecodePerformInfo fail\n");
	        return VDECODE_RESULT_UNSUPPORTED;
	 }
	 pDecoderInterface = p->pVideoEngine->pDecoderInterface;
	 ret = 0;
	 if(pDecoderInterface->SetExtraScaleInfo != NULL)
	 {
		 ret = pDecoderInterface->GetPerformInfo(pDecoderInterface,performCmd, performInfo);
	 }
	 return ret;
}
