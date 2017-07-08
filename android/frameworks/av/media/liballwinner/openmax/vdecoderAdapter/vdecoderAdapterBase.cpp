
//#define CONFIG_LOG_LEVEL    OPTION_LOG_LEVEL_DETAIL
#define LOG_TAG "vdecoderAdapterBase"
#include <pthread.h>
#include <utils/Vector.h>
#include <dlfcn.h>           // dynamic library
#include <semaphore.h>
#include <cutils/properties.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <malloc.h>
#include <string.h>
#include <sys/time.h>

#include "HardwareAPI.h"
#include "log.h"
#include "OMX_Core.h"
#include "aw_omx_component.h"

#include "vdecoderAdapterBase.h"

//* for message 
static MessageQueue* VideoMessageQueueCreate(int nMaxMessageNum, const char* pName = "unknown");

static void VideoMessageQueueDestroy(MessageQueue* mq);

static int VideoMessageQueuePostMessage(MessageQueue* mq, Message* m);

static int VideoMessageQueueGetMessage(MessageQueue* mq, Message* m);

static int VideoMessageQueueTryGetMessage(MessageQueue* mq, Message* m, int64_t timeout);

static int VideoMessageQueueFlush(MessageQueue* mq);

static int VideoMessageQueueGetCount(MessageQueue* mq);

//*end (for message)

template<class T>
static void InitOMXParams(T *pParams) {
    pParams->nSize = sizeof(T);
    pParams->nVersion.s.nVersionMajor = 1;
    pParams->nVersion.s.nVersionMinor = 0;
    pParams->nVersion.s.nRevision = 0;
    pParams->nVersion.s.nStep = 0;
}

static int setComponentName(VdecoderAdapterBaseContext* p, 
                            enum EVIDEOCODECFORMAT eCodecFormat);

static int setComponentRole(VdecoderAdapterBaseContext* p, 
                            enum EVIDEOCODECFORMAT eCodecFormat);

static int setVideoCompressionFormatType(VdecoderAdapterBaseContext* p, 
                                         enum EVIDEOCODECFORMAT eCodecFormat);

static int setVideoFormatOnPort(VdecoderAdapterBaseContext* p,
                                OMX_U32 nPortIndex,
                                int nWidth, 
                                int nHeight, 
                                int eCompressionFormat); 

static int setSupportedOutputFormat(VdecoderAdapterBaseContext* p);

static int EnableAndroidNativeBuffers(VdecoderAdapterBaseContext* p);

static int AllocateInBuffer(VdecoderAdapterBaseContext* p);

static int AllocateOutBuffer(VdecoderAdapterBaseContext* p);

static int StartComponent(VdecoderAdapterBaseContext* p);

static int StopComponent(VdecoderAdapterBaseContext* p);

static int DecideStreamBufferSize(VideoStreamInfo* pVideoInfo,
                                  VConfig* pConfig);

static void* VideoNativeThread(void* arg);

static void SavaPictureData(char* pData, int nWidth, int nHeight, int nNum)
{
    char path[1024] = {0};
    static FILE* fpPicture = NULL;
    int nLen = nWidth*nHeight*3/2;

    sprintf (path,"/data/camera/Data%d.dat",nNum);
    fpPicture = fopen(path, "wb");

    logd("SavaPictureData: fpPicture = %p, pData = %p, nWidht = %d, nHeight = %d, nNum = %d",
    		fpPicture,pData,nWidth,nHeight,nNum);

    if(fpPicture != NULL)
    {
	    fwrite(pData,1,nLen, fpPicture);
	    fclose(fpPicture);
    }

    return;
}

static OMX_ERRORTYPE OnEvent(
        OMX_IN OMX_HANDLETYPE pHComponent,
        OMX_IN OMX_PTR pAwAppData,
        OMX_IN OMX_EVENTTYPE eAwEvent,
        OMX_IN OMX_U32 nAwData1,
        OMX_IN OMX_U32 nAwData2,
        OMX_IN OMX_PTR pAwEventData) 
{
    CEDARX_UNUSE(pHComponent);
    CEDARX_UNUSE(pAwEventData);
    
    VdecoderAdapterBaseContext* p = NULL;
    OMX_ERRORTYPE eError = OMX_ErrorNone;
    
    OMX_U32 i = 0;

    p = (VdecoderAdapterBaseContext*)pAwAppData;

    logv("*** eAwEvent = %d, data1 = %d, data2 = 0x%x",(int)eAwEvent,(int)nAwData1,(int)nAwData2);
    switch(eAwEvent)
    {
        case OMX_EventCmdComplete:
            if(nAwData1 == OMX_CommandFlush)
            {
                if(nAwData2 == kPortIndexInput || nAwData2 == kPortIndexOutput)
                {
                    p->mFlushComplete[nAwData2] = 1;
                }

                if(p->mFlushComplete[0] == 1 && p->mFlushComplete[1] == 1)
                {
                    p->mFlushComplete[0] = 0;
                    p->mFlushComplete[1] = 0;
                    logd("sem post mFlushSem");
                    sem_post(&p->mFlushSem);
                }
            }
            else if(nAwData1 == OMX_CommandStateSet)
            {
                if(nAwData2 == OMX_StateLoaded)
                {
                    logd("sem post mIdleToLoadSem");
                    sem_post(&p->mIdleToLoadSem);
                }
                else if(nAwData2 == OMX_StateIdle)
                {
                    if(p->mState == OMX_StateLoaded)
                    {
                        logd("sem post mLoadToIdleSem");
                        sem_post(&p->mLoadToIdleSem);
                    }
                    else if(p->mState == OMX_StateExecuting)
                    {
                        logd("sem post mExecuteToIdleSem");
                        sem_post(&p->mExecuteToIdleSem);
                    }
                }
                else if(nAwData2 == OMX_StateExecuting)
                {
                    logd("sem post mIdleToExecuteSem");
                    sem_post(&p->mIdleToExecuteSem);
                }
            }
            else if(nAwData1 == OMX_CommandPortDisable)
            {
                if(nAwData2 != kPortIndexOutput)
                {
                    loge("** it is wrong, not the output port, nAwData2 = %u",
                          (unsigned int)nAwData2);
                }

                logd("sem post mDisablePortSem");
                sem_post(&p->mDisablePortSem);
            }
            else if(nAwData1 == OMX_CommandPortEnable)
            {
                logd("command complete, OMX_CommandPortEnable");
                //* send output buffer to component
                if(p->mState == OMX_StateReopen_Add)
                {
                    pthread_mutex_lock(&p->mOutBufferMutex);
                    for(i = 0; i < MAX_BUFFER_NUM; i++)
                    {
                        if(p->mOutBufferNode[i].eStatus == OWN_BY_US)
                        {
                            p->pOmx->fill_this_buffer(p->pHComponent, p->mOutBufferNode[i].pBufferInfo);
                            p->mOutBufferNode[i].eStatus = OWN_BY_COMPONENT;
                        }
                    }
                    pthread_mutex_unlock(&p->mOutBufferMutex);

                    p->mState = OMX_StateExecuting;
                }
            }
            break;
        case OMX_EventPortSettingsChanged:
             if(nAwData2 == OMX_IndexParamPortDefinition)
             {
                OMX_PARAM_PORTDEFINITIONTYPE def;
                InitOMXParams(&def);
                def.nPortIndex = kPortIndexOutput;

                eError = p->pOmx->get_parameter(NULL, OMX_IndexParamPortDefinition, &def);

                if (eError != OMX_ErrorNone) 
                {
                    return eError;
                }

                p->mFbmBufInfo.nBufWidth    = def.format.video.nFrameWidth;
                p->mFbmBufInfo.nBufHeight   = def.format.video.nFrameHeight;
                p->mFbmBufInfo.ePixelFormat = def.format.video.eColorFormat;
                p->mFbmBufInfo.nBufNum      = def.nBufferCountActual;

                logd("*** port definition change, w = %d, h = %d, cF = 0x%x, n = %d, size = %d",
                     (int)def.format.video.nFrameWidth,
                     (int)def.format.video.nFrameHeight,
                     def.format.video.eColorFormat,
                     (int)def.nBufferCountActual,
                     (int)def.nBufferSize);

                p->mState = OMX_StateReopen_Add;
                p->bOutPortSettingChangeFlag = 1;
             }
             else if(nAwData2 == OMX_IndexConfigCommonOutputCrop)
             {
                p->mDisplayCrop.nPortIndex = kPortIndexOutput;
                eError = p->pOmx->get_config(NULL, OMX_IndexConfigCommonOutputCrop, &p->mDisplayCrop);
                logd("*** update display crop on event: %d, %d, %d, %d, err = %d",
                      (int)p->mDisplayCrop.nLeft,(int)p->mDisplayCrop.nTop,
                      (int)p->mDisplayCrop.nHeight,(int)p->mDisplayCrop.nWidth,eError);

                OMX_PARAM_PORTDEFINITIONTYPE def;
				InitOMXParams(&def);
				def.nPortIndex = kPortIndexOutput;

				eError = p->pOmx->get_parameter(NULL, OMX_IndexParamPortDefinition, &def);

				if (eError != OMX_ErrorNone)
				{
					return eError;
				}

				VideoPicture* pVideoPicture = NULL;
				for(i = 0; i < def.nBufferCountActual; i++)
				{
					pVideoPicture = &p->mOutBufferNode[i].mVideoPicture;
					pVideoPicture->nWidth  = def.format.video.nFrameWidth;
					pVideoPicture->nHeight = def.format.video.nFrameHeight;
				}

				logd("*** update picture width and height also, w = %d, h = %d, cF = 0x%x, n = %d, size = %d",
					 (int)def.format.video.nFrameWidth,(int)def.format.video.nFrameHeight,
					 def.format.video.eColorFormat,(int)def.nBufferCountActual,
					 (int)def.nBufferSize);

             }
             break;
        default:
            logd("OnEvent is = %d",eAwEvent);
            break;
    }
    return OMX_ErrorNone;        
}

static OMX_ERRORTYPE OnEmptyBufferDone(
        OMX_IN OMX_HANDLETYPE pHComponent,
        OMX_IN OMX_PTR pAwAppData,
        OMX_IN OMX_BUFFERHEADERTYPE* pBuffer) 
{
    CEDARX_UNUSE(pHComponent);
    
    logv("*** OnEmptyBufferDone, buffer = %p",pBuffer->pBuffer);
    int i = 0;
    VdecoderAdapterBaseContext* p = NULL;
    OmxInBufferNode* pInBufferNodeTail = NULL;    

    p = (VdecoderAdapterBaseContext*)pAwAppData;

    pthread_mutex_lock(&p->mInBufferMutex);
    logv("*** OnEmptyBufferDone, buffer = %p, get lock ok",pBuffer->pBuffer);
    
    for(i = 0; i < MAX_BUFFER_NUM; i++)
    {
        if(p->mInBufferNode[i].pBufferInfo == pBuffer)
            break;
    }

    if(i >= MAX_BUFFER_NUM)
    {
        loge("mInBufferNode is overflow");
        abort();
    }

    p->mInBufferNode[i].pNext       = NULL;
    p->mInBufferNode[i].eStatus     = OWN_BY_US;

    //*put the buffer node to tail
    if(p->pInBufferNodeHeader == NULL)
    {
        p->pInBufferNodeHeader = &p->mInBufferNode[i];
    }
    else
    {
        pInBufferNodeTail = p->pInBufferNodeHeader;
        while(pInBufferNodeTail->pNext != NULL)
        {
            pInBufferNodeTail = pInBufferNodeTail->pNext;
        }
        pInBufferNodeTail->pNext = &p->mInBufferNode[i];
    }

    pthread_mutex_unlock(&p->mInBufferMutex);
    return OMX_ErrorNone; 
}

static OMX_ERRORTYPE OnFillBufferDone(
        OMX_IN OMX_HANDLETYPE pHComponent,
        OMX_IN OMX_PTR pAwAppData,
        OMX_IN OMX_BUFFERHEADERTYPE* pBuffer) 
{
    CEDARX_UNUSE(pHComponent);
    
    logv("OnFillBufferDone, pBuffer = %p, pts = %lld, flag = 0x%x, fillLen = %d, allLen = %d, nTickCount = %d",
         pBuffer,pBuffer->nTimeStamp,
         (int)pBuffer->nFlags,(int)pBuffer->nFilledLen,(int)pBuffer->nAllocLen,
         (int)pBuffer->nTickCount);

    int i = 0;
    VdecoderAdapterBaseContext* p = NULL;
    OmxOutBufferNode* pOutBufferNodeTail = NULL;    
    VideoPicture*  pVideoPicture = NULL;

    p = (VdecoderAdapterBaseContext*)pAwAppData;

    if(p->mState == OMX_StateReopen_Add)
    {
        Message msg;
        msg.messageId = QCOM_MESSAGE_ID_FREE_OUT_BUFFER;
        msg.params[0] = (uintptr_t)pBuffer;
        msg.params[1] = msg.params[2] = msg.params[3] = 0;
        if(VideoMessageQueuePostMessage(p->mq, &msg) != 0)
        {
            loge("fatal error, vdecoder_qcom post message fail.");
            abort();
        }
        
        return OMX_ErrorNone;
    }

    if(p->bNeedUpdataDisplayCropFlag == 1)
    {
        p->bNeedUpdataDisplayCropFlag = 0;
        p->mDisplayCrop.nPortIndex = kPortIndexOutput;
        int err = p->pOmx->get_config(NULL, OMX_IndexConfigCommonOutputCrop, &p->mDisplayCrop);
        logd("update display crop: %d, %d, %d, %d, err = %d",
              (int)p->mDisplayCrop.nLeft,
              (int)p->mDisplayCrop.nTop,
              (int)p->mDisplayCrop.nHeight,
              (int)p->mDisplayCrop.nWidth,err);
    }
	
    logv("OnFillBufferDone, get lock");
    pthread_mutex_lock(&p->mOutBufferMutex);
    logv("OnFillBufferDone, get lock ok");
    
    for(i = 0; i < MAX_BUFFER_NUM; i++)
    {
        if(p->mOutBufferNode[i].pBufferInfo == pBuffer)
            break;
    }

    if(i >= MAX_BUFFER_NUM)
    {
        loge("mOutBufferNode is overflow");
        abort();
    }

    pVideoPicture = &p->mOutBufferNode[i].mVideoPicture;
    
    p->mOutBufferNode[i].pNext       = NULL;
    p->mOutBufferNode[i].eStatus     = OWN_BY_US;

    //* transform to VideoPicture
    pVideoPicture->nPts         = pBuffer->nTimeStamp;

#if DEBUG_QCOM_SAVE_PICTURE
    if(pBuffer->nFilledLen != 0)
    {
        p->nSavePicNum++;
        SavaPictureData(pVideoPicture->pData0,
        				pVideoPicture->nWidth,
        				pVideoPicture->nHeight,
        				p->nSavePicNum);
    }
#endif

    if(p->mDisplayCrop.nWidth != 0 && p->mDisplayCrop.nHeight != 0)
    {
        pVideoPicture->nTopOffset    = p->mDisplayCrop.nTop;
        pVideoPicture->nLeftOffset   = p->mDisplayCrop.nLeft;
        pVideoPicture->nBottomOffset = p->mDisplayCrop.nTop + p->mDisplayCrop.nHeight;
        pVideoPicture->nRightOffset  = p->mDisplayCrop.nLeft + p->mDisplayCrop.nWidth;
    }

    //*put the buffer node to tail
    if(p->pOutBufferNodeHeader == NULL)
    {
        p->pOutBufferNodeHeader = &p->mOutBufferNode[i];
    }
    else
    {
        pOutBufferNodeTail = p->pOutBufferNodeHeader;
        while(pOutBufferNodeTail->pNext != NULL)
        {
            pOutBufferNodeTail = pOutBufferNodeTail->pNext;
        }
        pOutBufferNodeTail->pNext = &p->mOutBufferNode[i];
    }

    pthread_mutex_unlock(&p->mOutBufferMutex);

    if(pBuffer->nFlags & OMX_BUFFERFLAG_EOS)
    {
        p->nOutEosFlag = 1;
    }

    return OMX_ErrorNone; 
}

static OMX_CALLBACKTYPE mCallbacks = {
    &OnEvent, &OnEmptyBufferDone, &OnFillBufferDone
};

typedef void * (*CreateOmxComponent)(void);

VideoDecoder* AdbCreateVideoDecoder(void)
{
    logv("CreateVideoDecoder");
    
    VdecoderAdapterBaseContext* p = NULL;
    int err = -1;

    p = (VdecoderAdapterBaseContext*)malloc(sizeof(VdecoderAdapterBaseContext));
    if(p == NULL)
    {
        loge("malloc for VdecoderAdapterBaseContext failed");
        return NULL;
    }

    memset(p, 0, sizeof(VdecoderAdapterBaseContext));

    p->bNeedUpdataDisplayCropFlag = 1;
    p->pHComponent = (void*)1; //* just not set to null

    p->mq = VideoMessageQueueCreate(MAX_BUFFER_NUM, "vdecoder_qcom");
    if(p->mq == NULL)
    {
        loge("vdecoder_qcom create message queue fail.");
        free(p);
        return NULL;
    }

    pthread_mutex_init(&p->mInBufferMutex, NULL);
    pthread_mutex_init(&p->mOutBufferMutex, NULL);

    sem_init(&p->mFlushSem,0,0);
    sem_init(&p->mLoadToIdleSem,0,0);
    sem_init(&p->mIdleToExecuteSem,0,0);
    sem_init(&p->mExecuteToIdleSem,0,0);
    sem_init(&p->mIdleToLoadSem,0,0);
    sem_init(&p->mDisablePortSem,0,0);

    p->mState = OMX_StateLoaded;

    err = pthread_create(&p->sNativeThread, NULL, VideoNativeThread, p);
    if(err != 0)
    {
        logd("create videoNativeThread failed");
        sem_destroy(&p->mFlushSem);
        sem_destroy(&p->mLoadToIdleSem);
        sem_destroy(&p->mIdleToExecuteSem);
        sem_destroy(&p->mExecuteToIdleSem);
        sem_destroy(&p->mIdleToLoadSem);
        sem_destroy(&p->mDisablePortSem);

        VideoMessageQueueDestroy(p->mq);
        free(p);
        return NULL;
    }

    #if DEBUG_QCOM_SAVE_BITSTREAM
    p->fpStream = fopen("/data/camera/stream.dat", "wb");
    logd("open fpstream = %p",p->fpStream);
	#endif
    
    return (VideoDecoder*)p;
}

void AdbDestroyVideoDecoder(VideoDecoder* pDecoder)
{
    logv("QcomDestroyVideoDecoder, pDecoder = %p",pDecoder);

    VdecoderAdapterBaseContext* p = NULL;
    void*                   status;
    Message                 msg;

    p = (VdecoderAdapterBaseContext*)pDecoder;
    if(p == NULL)
    {
        loge("p is null when DestroyVideoDecoder()");
        return;
    }

    msg.messageId = QCOM_MESSAGE_ID_QUIT;
    msg.params[0] =msg.params[1] = msg.params[2] = msg.params[3] = 0;
    
    if(VideoMessageQueuePostMessage(p->mq, &msg) != 0)
    {
        loge("fatal error, video render component post message fail.");
        abort();
    }
    
    pthread_join(p->sNativeThread, &status);
    logd("pthread_join finish");
    StopComponent(p);

    if(p->mq)
    {
    	VideoMessageQueueDestroy(p->mq);
    }

    if(p->pOmx)
    {
    	p->pOmx->component_deinit(NULL);
    	delete p->pOmx;
    }

    pthread_mutex_destroy(&p->mInBufferMutex);
    pthread_mutex_destroy(&p->mOutBufferMutex);

    sem_destroy(&p->mFlushSem);
    sem_destroy(&p->mLoadToIdleSem);
    sem_destroy(&p->mIdleToExecuteSem);
    sem_destroy(&p->mExecuteToIdleSem);
    sem_destroy(&p->mIdleToLoadSem);
    sem_destroy(&p->mDisablePortSem);

    if(p->pSpecificData != NULL)
        free(p->pSpecificData);

    if(p->pSbm[0] != NULL)
        SbmVirDestroy(p->pSbm[0]);

    if(p->pSbm[1] != NULL)
        SbmVirDestroy(p->pSbm[1]);

    if(p->pKCodecFormatToName != NULL)
        free(p->pKCodecFormatToName);
    
    #if DEBUG_QCOM_SAVE_BITSTREAM
    if(p->fpStream != NULL)
        fclose(p->fpStream);
    #endif
    
    free(p);
    return ;
}

int AdbInitializeVideoDecoder(VideoDecoder* pDecoder, VideoStreamInfo* pVideoInfo, VConfig* pVconfig)
{
    logd("QcomInitializeVideoDecoder -- bGpuBufValid = %d,nGpuAlignStride = %d ",
		 pVconfig->bGpuBufValid,pVconfig->nAlignStride);
    
    logd("** nCodecSpecificDataLen = %d",pVideoInfo->nCodecSpecificDataLen);
    VdecoderAdapterBaseContext* p = NULL;
    int nReturnValue = -1;
    int nStreamBufferSize = 0;

    p = (VdecoderAdapterBaseContext*)pDecoder;

    #if 0
    //*check whether support the size
    if(pVideoInfo->nWidth > MAX_SURPPORT_RESOLUTION_WIDTH
       || pVideoInfo->nHeight > MAX_SURPPORT_RESOLUTION_HEIGHT)
    {
    	logw("the resolution is not support, w&h: %d, %d, MAX: %d, %d",
    			pVideoInfo->nWidth, pVideoInfo->nHeight,
    			MAX_SURPPORT_RESOLUTION_WIDTH, MAX_SURPPORT_RESOLUTION_HEIGHT);
    	return -1;
    }

    memcpy(&p->mConfig, pVconfig, sizeof(VConfig));

    p->mConfig.eOutputPixelFormat = OMX_COLOR_FormatYUV420Planar;

    p->eCodecFormat = pVideoInfo->eCodecFormat;
    
    p->OmxSoHandle = dlopen("libOmxVdec.so", RTLD_NOW);

    if(p->OmxSoHandle == NULL)
    {
        loge("open libOmxVdec.so fail");
        free(p);
        return -1;
    }

    CreateOmxComponent FCreateOmxComponent = (CreateOmxComponent)dlsym(p->OmxSoHandle, "get_omx_component_factory_fn");
    
    p->pOmx = (aw_omx_component*)(*FCreateOmxComponent)();

    #endif
    
    nReturnValue = setComponentName(p, (enum EVIDEOCODECFORMAT)pVideoInfo->eCodecFormat);
    if(nReturnValue != 0)
    {
    	loge("setComponentName failed, nReturnValue = %d, 0x%x",
    		  nReturnValue,nReturnValue);
    	return -1;
    }

    p->pOmx->set_callbacks(p->pHComponent, &mCallbacks, p);
    
    //*set component role
    setComponentRole(p, (enum EVIDEOCODECFORMAT)pVideoInfo->eCodecFormat);

    if(pVconfig->bGpuBufValid == 1)
    {
        EnableAndroidNativeBuffers(p);
    }

    logd("--------call setVideoCompressionFormatType, codecFormat = %d",pVideoInfo->eCodecFormat);
    nReturnValue = setVideoCompressionFormatType(p, (enum EVIDEOCODECFORMAT)pVideoInfo->eCodecFormat);
    if(nReturnValue != 0)
    {
        loge("setVideoCompressionFormatType fail, ret = %d, 0x%x",nReturnValue,nReturnValue);
        return -1;
    }
    
    nReturnValue = setSupportedOutputFormat(p);
    if(nReturnValue != 0)
    {
        loge("setSupportedOutputFormat fail, ret = %d, 0x%x",nReturnValue,nReturnValue);
        return -1;
    }
    
    nReturnValue = setVideoFormatOnPort(p, kPortIndexInput,
                         pVideoInfo->nWidth, pVideoInfo->nHeight, 
                         p->eOmxVideoCodingType);
    if(nReturnValue != 0)
    {
        loge("setVideoFormatOnPort(in) fail, ret = %d, 0x%x",nReturnValue,nReturnValue);
        return -1;
    }

    nReturnValue = setVideoFormatOnPort(p, kPortIndexOutput,
                         pVideoInfo->nWidth, pVideoInfo->nHeight, 
                         p->eOmxVideoCodingType);
    if(nReturnValue != 0)
    {
        loge("setVideoFormatOnPort(out) fail, ret = %d, 0x%x",nReturnValue,nReturnValue);
        return -1;
    }
     
    //* create sbm
    nStreamBufferSize = DecideStreamBufferSize(pVideoInfo, pVconfig);

    logd("create sbm, size = %d",nStreamBufferSize);
    p->pSbm[0] = SbmVirCreate(nStreamBufferSize);

    if(pVideoInfo->bIs3DStream)
    {
        loge("error: not surpport 3D now");
        return -1;
    }

    //* save specific data
    if(pVideoInfo->nCodecSpecificDataLen > 0 && pVideoInfo->pCodecSpecificData != NULL)
    {
        if(p->pSpecificData != NULL)
            free(p->pSpecificData);

        p->pSpecificData = (char*)malloc(pVideoInfo->nCodecSpecificDataLen);
        if(p->pSpecificData == NULL)
        {
            loge("malloc for pSpecificData fail");
            return -1;
        }

        memcpy(p->pSpecificData, pVideoInfo->pCodecSpecificData, pVideoInfo->nCodecSpecificDataLen);
        p->nSpecificDataLen = pVideoInfo->nCodecSpecificDataLen;

        #if DEBUG_QCOM_SAVE_BITSTREAM
        logd("save specifi data to file, fp = %p, len = %d",
              p->fpStream, p->nSpecificDataLen);
        if(p->fpStream != NULL)
        {
            fwrite(p->pSpecificData, 1, p->nSpecificDataLen, p->fpStream);
        }
        #endif
    }

    //* we must allocate buffer and start component here 
    //* when the out buffer is not come from gpu
    if(pVconfig->bGpuBufValid == 0)
    {
        AllocateInBuffer(p);
        AllocateOutBuffer(p);
        StartComponent(p);
    }
    
    return 0;
}

void AdbResetVideoDecoder(VideoDecoder* pDecoder)
{
    logd("AdbResetVideoDecoder");
    VdecoderAdapterBaseContext* p = NULL;

    p = (VdecoderAdapterBaseContext*)pDecoder;

    p->bFlushPending = 1;
    
    if(p->pSbm[0] != NULL)
        SbmReset(p->pSbm[0]);

    memset(p->partialStreamDataInfo, 0, sizeof(VideoStreamDataInfo)*2);
    
    //* set component status to execute
    if(p->mState == OMX_StateExecuting)
    {
        p->pOmx->send_command(NULL, OMX_CommandFlush, OMX_ALL, NULL);

        logd("sem wait for mFlushSem start");
        sem_wait(&p->mFlushSem);
        logd("sem wait for mFlushSem finish");
    }

    //* send output buffer to component
    pthread_mutex_lock(&p->mOutBufferMutex);
    while(p->pOutBufferNodeHeader != NULL)
    {
        p->pOmx->fill_this_buffer(p->pHComponent, p->pOutBufferNodeHeader->pBufferInfo);
        p->pOutBufferNodeHeader->eStatus = OWN_BY_COMPONENT;

        p->pOutBufferNodeHeader = p->pOutBufferNodeHeader->pNext;
    }

    pthread_mutex_unlock(&p->mOutBufferMutex);

    p->bFlushPending = 0;

    return;
}

int AdbDecodeVideoStream(VideoDecoder* pDecoder, 
                      int           bEndOfStream,
                      int           bDecodeKeyFrameOnly,
                      int           bDropBFrameIfDelay,
                      int64_t       nCurrentTimeUs)
{
    CEDARX_UNUSE(bEndOfStream);
    CEDARX_UNUSE(bDecodeKeyFrameOnly);
    CEDARX_UNUSE(bDropBFrameIfDelay);
    CEDARX_UNUSE(nCurrentTimeUs);
    
    VdecoderAdapterBaseContext* p = NULL;
    OMX_BUFFERHEADERTYPE* pInBufferInfo = NULL;
    VideoStreamDataInfo*  pStreamData = NULL;
    char* pStreamBufEnd = NULL;
    char* pStreamBufStart = NULL;
    char* nDstBuffer = NULL;
    int   nCopyLen = 0;
    Sbm*  pSbm = NULL;

    p = (VdecoderAdapterBaseContext*)pDecoder;
    pSbm = p->pSbm[0];

    if(p->bOutPortSettingChangeFlag == 1)
    {
        return VDECODE_RESULT_RESOLUTION_CHANGE;
    }
    
    if(p->mState != OMX_StateExecuting)
    {
        logw("the state is not executing");
        return VDECODE_RESULT_OK;
    }

    pthread_mutex_lock(&p->mInBufferMutex);

    logv("*** p->pInBufferNodeHeader = %p, streamDataSize = %d",
         p->pInBufferNodeHeader,SbmStreamDataSize(pSbm));
    if(p->pInBufferNodeHeader == NULL)
    {
        logv("have no input buffer");
        pthread_mutex_unlock(&p->mInBufferMutex);

        if(SbmStreamDataSize(pSbm) <= 0)
        {
            if(p->mConfig.bGpuBufValid == 0
               && p->pOutBufferNodeHeader != NULL)
            {
                return VDECODE_RESULT_FRAME_DECODED;
            }
            else
            {
                return VDECODE_RESULT_NO_BITSTREAM;
            }
        }
        else
        {
            return VDECODE_RESULT_OK;
        }
    }

    pInBufferInfo = p->pInBufferNodeHeader->pBufferInfo;

    pStreamData = SbmRequestStream(pSbm);

    logv("*** pStreamData = %p", pStreamData);

    if(pStreamData == NULL)
    {
        logv("the sbm buffer is empty");

        if(p->nInEosFlag == 1)
        {
            p->nInEosFlag = 0;
            pInBufferInfo->nFilledLen = 0;
            pInBufferInfo->nTimeStamp = -1;
            pInBufferInfo->nOffset    = 0;
            pInBufferInfo->nFlags     = OMX_BUFFERFLAG_EOS;
            goto empty_buffer;
        }
        
        pthread_mutex_unlock(&p->mInBufferMutex);

        if(p->mConfig.bGpuBufValid == 0
           && p->pOutBufferNodeHeader != NULL)
        {
            return VDECODE_RESULT_FRAME_DECODED;
        }
        else
        {
            return VDECODE_RESULT_NO_BITSTREAM;
        }
    }

    pStreamBufEnd = (char*)SbmBufferAddress(pSbm) + SbmBufferSize(pSbm);
    if((pStreamData->pData + pStreamData->nLength) <= pStreamBufEnd)
    {
        memcpy(pInBufferInfo->pBuffer, pStreamData->pData, pStreamData->nLength);
    }
    else
    {
        nDstBuffer = (char*)pInBufferInfo->pBuffer;
        nCopyLen   = pStreamBufEnd - pStreamData->pData;
        pStreamBufStart = (char*)SbmBufferAddress(pSbm);

        memcpy(nDstBuffer, pStreamData->pData, nCopyLen);
        nDstBuffer += nCopyLen;
        nCopyLen    = pStreamData->nLength - nCopyLen;
        memcpy(nDstBuffer , pStreamBufStart, nCopyLen);
    }

    pInBufferInfo->nFilledLen = pStreamData->nLength;
    pInBufferInfo->nTimeStamp = pStreamData->nPts;
    pInBufferInfo->nOffset    = 0;
    pInBufferInfo->nFlags     = OMX_BUFFERFLAG_ENDOFFRAME;

    SbmFlushStream(pSbm, pStreamData);

empty_buffer:
    
    logv("*** call empty_this_buffer, len  = %d, pts = %lld",
         pInBufferInfo->nFilledLen,pInBufferInfo->nTimeStamp);
    p->pOmx->empty_this_buffer(p->pHComponent, pInBufferInfo);
    
    
    p->pInBufferNodeHeader->eStatus = OWN_BY_COMPONENT;
    
    if(p->pInBufferNodeHeader->pNext != NULL)
    {
        p->pInBufferNodeHeader = p->pInBufferNodeHeader->pNext;
    }
    else
    {
        p->pInBufferNodeHeader = NULL;
    }
    
    pthread_mutex_unlock(&p->mInBufferMutex);


    //* we must tell the caller had decode a picture when
    //* the out buffer is not come from gpu
    if(p->mConfig.bGpuBufValid == 0)
    {
        if(p->pOutBufferNodeHeader != NULL)
            return VDECODE_RESULT_FRAME_DECODED;
    }
    
    return VDECODE_RESULT_OK;
}

int AdbDecoderSetSpecialData(VideoDecoder* pDecoder, void *pArg)
{
    CEDARX_UNUSE(pDecoder);
    CEDARX_UNUSE(pArg);
    return -1;
}

int AdbGetVideoStreamInfo(VideoDecoder* pDecoder, VideoStreamInfo* pVideoInfo)
{
    CEDARX_UNUSE(pDecoder);
    CEDARX_UNUSE(pVideoInfo);
    return 0;
}

int AdbRequestVideoStreamBuffer(VideoDecoder* pDecoder,
                             int           nRequireSize,
                             char**        ppBuf,
                             int*          pBufSize,
                             char**        ppRingBuf,
                             int*          pRingBufSize,
                             int           nStreamBufIndex)
{
    #if 0
    VdecoderAdapterBaseContext* p = NULL;
    OMX_BUFFERHEADERTYPE* pInBufferInfo = NULL;

    p = (VdecoderAdapterBaseContext*)pDecoder;

    logv("RequestVideoStreamBuffer, nRequireSize = %d,p->pInBufferNodeHeader = %p",
          nRequireSize,p->pInBufferNodeHeader);

    if(p->mState != OMX_StateExecuting)
    {
        logw("the state is not executing");
        return -1;
    }
    pthread_mutex_lock(&p->mInBufferMutex);

    if(p->pInBufferNodeHeader == NULL)
    {
        logv("have no input buffer");
        pthread_mutex_unlock(&p->mInBufferMutex);
        return -1;
    }

    pInBufferInfo = p->pInBufferNodeHeader->pBufferInfo;

    logv("*** nRequireSize = %d, pInBufferInfo->nAllocLen = %d",
         nRequireSize,(int)pInBufferInfo->nAllocLen);
    if(nRequireSize > (int)pInBufferInfo->nAllocLen)
    {
        logd("nRequireSize is too overflow, %d, %d",
              nRequireSize, (int)pInBufferInfo->nAllocLen);
        pthread_mutex_unlock(&p->mInBufferMutex);
        return -1;
    }

    *ppBuf        = (char*)pInBufferInfo->pBuffer;
    *pBufSize     = nRequireSize;
    *ppRingBuf    = NULL;
    *pRingBufSize = 0;

    p->pInBufferNodeHeader->eStatus = OWN_BY_RENDER;
    
    pthread_mutex_unlock(&p->mInBufferMutex);
    #else
    VdecoderAdapterBaseContext* p = NULL;

    p = (VdecoderAdapterBaseContext*)pDecoder;
    
    char*                pStart;
    char*                pStreamBufEnd;
    char*                pMem;
    int                  nFreeSize;
    Sbm*                 pSbm;
    
    logi("RequestVideoStreamBuffer, pDecoder=%p, nRequireSize=%d, nStreamBufIndex=%d", 
            pDecoder, nRequireSize, nStreamBufIndex);
    
    
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
        *ppRingBuf    = (char*)SbmBufferAddress(pSbm);
        *pRingBufSize = nFreeSize - *pBufSize;
        logi("stream buffer %d ring back.", nStreamBufIndex);
    }
    
    #endif
    
    
    return 0;
}

int AdbSubmitVideoStreamData(VideoDecoder*        pDecoder,
                            VideoStreamDataInfo* pDataInfo,
                            int                  nStreamBufIndex)
{
    #if 0
	logv("submit data, pts = %lld",pDataInfo->nPts);
    VdecoderAdapterBaseContext* p = NULL;
    OMX_BUFFERHEADERTYPE* pInBufferInfo = NULL;

    p = (VdecoderAdapterBaseContext*)pDecoder;

    pthread_mutex_lock(&p->mInBufferMutex);
    
    if(p->pInBufferNodeHeader == NULL)
    {
        loge("error: p->pInBufferNodeHeader should not be null");
        abort();
    }

    pInBufferInfo = p->pInBufferNodeHeader->pBufferInfo;
    if(pInBufferInfo->pBuffer != (OMX_U8*)pDataInfo->pData)
    {
        loge("the in buffer addr is not match, %p, %p",
              pInBufferInfo->pBuffer, pDataInfo->pData);
        abort();
    }
    
    pInBufferInfo->nFilledLen = pDataInfo->nLength;
    pInBufferInfo->nTimeStamp = pDataInfo->nPts;
    pInBufferInfo->nOffset    = 0;
    pInBufferInfo->nFlags     = OMX_BUFFERFLAG_ENDOFFRAME;

    p->pOmx->empty_this_buffer(NULL, pInBufferInfo);

    p->pInBufferNodeHeader->eStatus = OWN_BY_COMPONENT;
    
    if(p->pInBufferNodeHeader->pNext != NULL)
    {
        p->pInBufferNodeHeader = p->pInBufferNodeHeader->pNext;
    }
    else
    {
        p->pInBufferNodeHeader = NULL;
    }

    pthread_mutex_unlock(&p->mInBufferMutex);
    #else

    Sbm*                 pSbm;
    VideoStreamDataInfo* pPartialStreamDataInfo;
    VdecoderAdapterBaseContext*   p = NULL;

    logv("SubmitVideoStreamData, pDecoder=%p, pDataInfo=%p, nStreamBufIndex=%d", 
            pDecoder, pDataInfo, nStreamBufIndex);
    
    p = (VdecoderAdapterBaseContext*)pDecoder;
    
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
            logw("stream data frame uncomplete.");
            
#if DEBUG_QCOM_SAVE_BITSTREAM
            logd("sava video stream , fp = %p, lenth=%d, pts = %lld\n",
                  p->fpStream,
            	  pPartialStreamDataInfo->nLength,
            	  pPartialStreamDataInfo->nPts);

            if(p->fpStream != NULL)
            {
                if(pSbm->pWriteAddr+pPartialStreamDataInfo->nLength <= pSbm->pStreamBufferEnd)
                {
                	fwrite(pSbm->pWriteAddr, 1, pPartialStreamDataInfo->nLength, p->fpStream);
                }
                else
                {
                  	fwrite(pSbm->pWriteAddr, 1, pSbm->pStreamBufferEnd-pSbm->pWriteAddr+1, p->fpStream);
                  	fwrite(pSbm->pStreamBuffer,
                  		   1,
                  		   pPartialStreamDataInfo->nLength-(pSbm->pStreamBufferEnd-pSbm->pWriteAddr+1),
                  		   p->fpStream);
                }
            }
#endif
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
            #if 0
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
            #endif
        }
        else
        {
            //* maybe it is a empty frame for MPEG4 decoder from AVI parser.
            logw("empty stream data frame submitted, pData=%p, nLength=%d",
                pPartialStreamDataInfo->pData, pPartialStreamDataInfo->nLength);
        }

#if DEBUG_QCOM_SAVE_BITSTREAM
		logd("sava video stream , fp = %p, lenth=%d, pts = %lld\n",
                  p->fpStream,
            	  pPartialStreamDataInfo->nLength,
            	  pPartialStreamDataInfo->nPts);

        if(p->fpStream != NULL)
        {
            if(pSbm->pWriteAddr+pPartialStreamDataInfo->nLength <= pSbm->pStreamBufferEnd)
            {
            	fwrite(pSbm->pWriteAddr, 1, pPartialStreamDataInfo->nLength, p->fpStream);
            }
            else
            {
              	fwrite(pSbm->pWriteAddr, 1, pSbm->pStreamBufferEnd-pSbm->pWriteAddr+1, p->fpStream);
              	fwrite(pSbm->pStreamBuffer,
              		   1,
              		   pPartialStreamDataInfo->nLength-(pSbm->pStreamBufferEnd-pSbm->pWriteAddr+1),
              		   p->fpStream);
            }
        }
#endif
        //* submit stream frame to the SBM.
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
    
    #endif
    return 0;
}

int AdbVideoStreamBufferSize(VideoDecoder* pDecoder, int nStreamBufIndex)
{
    VdecoderAdapterBaseContext* p;
    
    p = (VdecoderAdapterBaseContext*)pDecoder;
    
    logi("VideoStreamBufferSize, nStreamBufIndex=%d", nStreamBufIndex);
    
    if(p->pSbm[nStreamBufIndex] == NULL)
        return 0;
    
    return SbmBufferSize(p->pSbm[nStreamBufIndex]);
}

int AdbVideoStreamDataSize(VideoDecoder* pDecoder, int nStreamBufIndex)
{
    VdecoderAdapterBaseContext* p;
    
    p = (VdecoderAdapterBaseContext*)pDecoder;
    
    logi("QcomVideoStreamDataSize, nStreamBufIndex=%d", nStreamBufIndex);
    
    if(p->pSbm[nStreamBufIndex] == NULL)
        return 0;

    return SbmStreamDataSize(p->pSbm[nStreamBufIndex]);
}

int AdbVideoStreamFrameNum(VideoDecoder* pDecoder, int nStreamBufIndex)
{
    VdecoderAdapterBaseContext* p;
    
    p = (VdecoderAdapterBaseContext*)pDecoder;
    
    logi("QcomVideoStreamFrameNum, nStreamBufIndex=%d", nStreamBufIndex);
    
    if(p->pSbm[nStreamBufIndex] == NULL)
        return 0;

    return SbmStreamFrameNum(p->pSbm[nStreamBufIndex]);
}

void* AdbVideoStreamDataInfoPointer(VideoDecoder* pDecoder, int nStreamBufIndex)
{
    CEDARX_UNUSE(pDecoder);
    CEDARX_UNUSE(nStreamBufIndex);
    return NULL;
}

VideoPicture* AdbRequestPicture(VideoDecoder* pDecoder, int nStreamIndex)
{
    CEDARX_UNUSE(nStreamIndex);
    
    VdecoderAdapterBaseContext* p = NULL;
    VideoPicture*  pVideoPicture = NULL;
    OMX_BUFFERHEADERTYPE* pBufferInfo = NULL;

    p = (VdecoderAdapterBaseContext*)pDecoder;

    if(p->bFlushPending == 1)
    {
        logv("it is flushPending, request failed");
        return NULL;
    }
    logv("RequestPicture, p->pOutBufferNodeHeader = %p",p->pOutBufferNodeHeader);

    pthread_mutex_lock(&p->mOutBufferMutex);

    if(p->pOutBufferNodeHeader != NULL)
    {
        pVideoPicture = &p->pOutBufferNodeHeader->mVideoPicture;
        pBufferInfo   = p->pOutBufferNodeHeader->pBufferInfo;
        p->pOutBufferNodeHeader->eStatus = OWN_BY_RENDER;

        //*remove the head of queue
        if(p->pOutBufferNodeHeader->pNext != NULL)
        {
            p->pOutBufferNodeHeader = p->pOutBufferNodeHeader->pNext;
        }
        else
        {
            p->pOutBufferNodeHeader = NULL;
        }
    }
    
    pthread_mutex_unlock(&p->mOutBufferMutex);

    //*we should return buffer to component when the nFilledLen is 0
    if(pBufferInfo != NULL)
    {
        if(pBufferInfo->nFilledLen == 0)
        {
            p->pOmx->fill_this_buffer(p->pHComponent, pBufferInfo);
            pVideoPicture = NULL;
        }
    }

    return pVideoPicture;
}

int AdbReturnPicture(VideoDecoder* pDecoder, VideoPicture* pPicture)
{
    VdecoderAdapterBaseContext* p = NULL;
    VideoPicture*  pVideoPicture = NULL;
    OmxOutBufferNode* pOutBufferNode = NULL;

    p = (VdecoderAdapterBaseContext*)pDecoder;
    //pthread_mutex_lock(&p->mOutBufferMutex);

    pOutBufferNode = (OmxOutBufferNode*)pPicture->pPrivate;

    pOutBufferNode->pBufferInfo->nFilledLen = 0;
    p->pOmx->fill_this_buffer(p->pHComponent, pOutBufferNode->pBufferInfo);

    pOutBufferNode->eStatus = OWN_BY_COMPONENT;
    
    //pthread_mutex_unlock(&p->mOutBufferMutex);
    return 0;
}

VideoPicture* AdbNextPictureInfo(VideoDecoder* pDecoder, int nStreamIndex)
{
    CEDARX_UNUSE(nStreamIndex);
    VdecoderAdapterBaseContext* p = NULL;
    VideoPicture*  pVideoPicture = NULL;

    p = (VdecoderAdapterBaseContext*)pDecoder;
    
    pthread_mutex_lock(&p->mOutBufferMutex);

    if(p->pOutBufferNodeHeader != NULL)
    {
        pVideoPicture  = &p->pOutBufferNodeHeader->mVideoPicture;
    }
    
    pthread_mutex_unlock(&p->mOutBufferMutex);
    
    return pVideoPicture;
}

int AdbTotalPictureBufferNum(VideoDecoder* pDecoder, int nStreamIndex)
{
    CEDARX_UNUSE(pDecoder);
    CEDARX_UNUSE(nStreamIndex);
    return -1;
}

int AdbEmptyPictureBufferNum(VideoDecoder* pDecoder, int nStreamIndex)
{
    CEDARX_UNUSE(pDecoder);
    CEDARX_UNUSE(nStreamIndex);
    return -1;
}

int AdbValidPictureNum(VideoDecoder* pDecoder, int nStreamIndex)
{
    CEDARX_UNUSE(nStreamIndex);
    
    VdecoderAdapterBaseContext* p = NULL;
    OmxOutBufferNode* pBufferInfoNode = NULL;
    int nValidPicNum = 0;

    p = (VdecoderAdapterBaseContext*)pDecoder;

    pthread_mutex_lock(&p->mOutBufferMutex);
    
    if(p->pOutBufferNodeHeader != NULL)
    {
        nValidPicNum++;
        pBufferInfoNode = p->pOutBufferNodeHeader;
        while(pBufferInfoNode->pNext != NULL)
        {
            nValidPicNum++;
            pBufferInfoNode = pBufferInfoNode->pNext;
        }
    }
    
    pthread_mutex_unlock(&p->mOutBufferMutex);

    logv("QcomValidPictureNum, nValidPicNum = %d",nValidPicNum);

    return nValidPicNum;
}

int AdbConfigHorizonScaleDownRatio(VideoDecoder* pDecoder, int nScaleDownRatio)
{
    CEDARX_UNUSE(pDecoder);
    CEDARX_UNUSE(nScaleDownRatio);
    return -1;
}

int AdbConfigVerticalScaleDownRatio(VideoDecoder* pDecoder, int nScaleDownRatio)
{
    CEDARX_UNUSE(pDecoder);
    CEDARX_UNUSE(nScaleDownRatio);
    return -1;
}

int AdbConfigRotation(VideoDecoder* pDecoder, int nRotateDegree)
{
    CEDARX_UNUSE(pDecoder);
    CEDARX_UNUSE(nRotateDegree);
    return -1;
}

int AdbConfigDeinterlace(VideoDecoder* pDecoder, int bDeinterlace)
{
    CEDARX_UNUSE(pDecoder);
    CEDARX_UNUSE(bDeinterlace);
    return -1;
}

int AdbConfigThumbnailMode(VideoDecoder* pDecoder, int bOpenThumbnailMode)
{
    CEDARX_UNUSE(pDecoder);
    CEDARX_UNUSE(bOpenThumbnailMode);
    return -1;
}

int AdbConfigOutputPicturePixelFormat(VideoDecoder* pDecoder, int ePixelFormat)
{
    CEDARX_UNUSE(pDecoder);
    CEDARX_UNUSE(ePixelFormat);
    return -1;
}

int AdbConfigNoBFrames(VideoDecoder* pDecoder, int bNoBFrames)
{
    CEDARX_UNUSE(pDecoder);
    CEDARX_UNUSE(bNoBFrames);
    return -1;
}

int AdbConfigDisable3D(VideoDecoder* pDecoder, int bDisable3D)
{
    CEDARX_UNUSE(pDecoder);
    CEDARX_UNUSE(bDisable3D);
    return -1;
}

int AdbConfigVeMemoryThresh(VideoDecoder* pDecoder, int nMemoryThresh)
{
    CEDARX_UNUSE(pDecoder);
    CEDARX_UNUSE(nMemoryThresh);
    return -1;
}

int AdbReopenVideoEngine(VideoDecoder* pDecoder, VConfig* pVConfig, VideoStreamInfo* pStreamInfo)
{
    CEDARX_UNUSE(pVConfig);
    CEDARX_UNUSE(pStreamInfo);
    
    VdecoderAdapterBaseContext* p = NULL;
    
    p = (VdecoderAdapterBaseContext*)pDecoder;

    logd("QcomReopenVideoEngine");
    OMX_ERRORTYPE eError = OMX_ErrorNone;
    int i = 0;

    logd("** set OMX_CommandPortDisable start");
    eError = p->pOmx->send_command(NULL, OMX_CommandPortDisable, kPortIndexOutput, NULL);
    logd("** set OMX_CommandPortDisable finish");

    //*free out buffer not own by component
    for(i = 0; i < p->mFbmBufInfo.nBufNum; i++)
    {
        if(p->mOutBufferNode[i].eStatus != OWN_BY_COMPONENT)
        {
           p->pOmx->free_buffer(p->pHComponent, kPortIndexOutput, p->mOutBufferNode[i].pBufferInfo); 
        }

        p->mOutBufferNode[i].eStatus = OWN_BY_COMPONENT;
    }

    logd("wait for mDisablePortSem start");
    sem_wait(&p->mDisablePortSem);
    logd("wait for mDisablePortSem finish");

    memset(&p->mOutBufferNode, 0, sizeof(OmxOutBufferNode)*MAX_BUFFER_NUM);

    logd("** set OMX_CommandPortEnable start");
    eError = p->pOmx->send_command(NULL, OMX_CommandPortEnable, kPortIndexOutput, NULL);
    logd("** set OMX_CommandPortEnable finish");

    p->bOutPortSettingChangeFlag = 0;
    p->bNeedUpdataDisplayCropFlag  = 1;

    //* we must allocate buffer here when the buffer is not 
    //* come from gpu
    if(p->mConfig.bGpuBufValid == 0)
    {
        AllocateOutBuffer(p);
    }
    return 0;
}

int AdbRotatePicture(VideoPicture* pPictureIn, 
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

int AdbRotatePictureHw(VideoDecoder* pDecoder,
                    VideoPicture* pPictureIn, 
                    VideoPicture* pPictureOut, 
                    int           nRotateDegree)
{
    CEDARX_UNUSE(pDecoder);
    CEDARX_UNUSE(pPictureIn);
    CEDARX_UNUSE(pPictureOut);
    CEDARX_UNUSE(nRotateDegree);
    return -1;
}

VideoPicture* AdbAllocatePictureBuffer(int nWidth, int nHeight, int nLineStride, int ePixelFormat)
{
    CEDARX_UNUSE(nWidth);
    CEDARX_UNUSE(nHeight);
    CEDARX_UNUSE(nLineStride);
    CEDARX_UNUSE(ePixelFormat);
    return NULL;
}

int AdbFreePictureBuffer(VideoPicture* pPicture)
{
    CEDARX_UNUSE(pPicture);
    return -1;
}

char* AdbVideoRequestSecureBuffer(VideoDecoder* pDecoder,int nBufferSize)
{
    CEDARX_UNUSE(pDecoder);
    CEDARX_UNUSE(nBufferSize);
    return NULL;
}

void AdbVideoReleaseSecureBuffer(VideoDecoder* pDecoder,char* pBuf)
{
    CEDARX_UNUSE(pDecoder);
    CEDARX_UNUSE(pBuf);
    return ;
}

VideoPicture* AdbReturnRelasePicture(VideoDecoder* pDecoder, VideoPicture* pVpicture, int bForbidUseFlag)
{
    CEDARX_UNUSE(pDecoder);
    CEDARX_UNUSE(pVpicture);
    CEDARX_UNUSE(bForbidUseFlag);
    return NULL;
}

VideoPicture* AdbRequestReleasePicture(VideoDecoder* pDecoder)
{
    CEDARX_UNUSE(pDecoder);
    return NULL;
}

int AdbSetVideoFbmBufRelease(VideoDecoder* pDecoder)
{
    CEDARX_UNUSE(pDecoder);
    return -1;
}

VideoPicture* AdbSetVideoFbmBufAddress(VideoDecoder* pDecoder, VideoPicture* pVideoPicture,int bForbidUseFlag)
{
    int i = 0;
    int nPictureSize = 0;
    VdecoderAdapterBaseContext* p = NULL;

    p = (VdecoderAdapterBaseContext*)pDecoder;

    for(i = 0; i < MAX_BUFFER_NUM; i++)
    {
        if(p->mOutBufferNode[i].pBufferInfo == NULL)
            break;
    }

    if(i >= MAX_BUFFER_NUM)
    {
        loge("the mOutBufferNode is overflow, %d, %d", i, MAX_BUFFER_NUM);
        abort();
    }

    memcpy(&p->mOutBufferNode[i].mVideoPicture, pVideoPicture, sizeof(VideoPicture));

    #if 0
    logd("use_buffer--out, pData0 = %p, handle = %p, p->nOutBufferSize = %d",
         pVideoPicture->pData0,(OMX_U8*)pVideoPicture->handle,p->nOutBufferSize);
    p->pOmx->use_buffer(NULL, 
                        &p->mOutBufferNode[i].pBufferInfo, 
                        kPortIndexOutput, 
                        NULL, 
                        p->nOutBufferSize, 
                        (OMX_U8*)pVideoPicture->handle);
    #else
     logd("use_buffer--out, pData0 = %p, p->nOutBufferSize = %d",
         pVideoPicture->pData0,p->nOutBufferSize);
    p->pOmx->use_buffer(p->pHComponent, 
                        &p->mOutBufferNode[i].pBufferInfo, 
                        kPortIndexOutput, 
                        NULL, 
                        p->nOutBufferSize, 
                        (OMX_U8*)pVideoPicture->pData0);
    #endif

    p->mOutBufferNode[i].mVideoPicture.pPrivate = &p->mOutBufferNode[i];
    if(bForbidUseFlag == 1)
    {
        p->mOutBufferNode[i].eStatus = OWN_BY_RENDER;
    }
    else
    {
        p->mOutBufferNode[i].eStatus = OWN_BY_US;
    }

    p->nAllocateOutBufferNum++;

    if(p->nAllocateOutBufferNum == p->mFbmBufInfo.nBufNum
       && p->mState == OMX_StateLoaded)
    {
        AllocateInBuffer(p);
        StartComponent(p);
    }
    
    return &p->mOutBufferNode[i].mVideoPicture;
}

int AdbSetGpuBufferNum(VideoDecoder* pDecoder, int nGpuBufferNum)
{
    OMX_ERRORTYPE eError = OMX_ErrorNone;
    VdecoderAdapterBaseContext* p = NULL;

    p = (VdecoderAdapterBaseContext*)pDecoder;

    OMX_PARAM_PORTDEFINITIONTYPE def;
    InitOMXParams(&def);
    def.nPortIndex = kPortIndexOutput;

    eError = p->pOmx->get_parameter(NULL, OMX_IndexParamPortDefinition, &def);

    if (eError != OMX_ErrorNone) 
    {
        return -1;
    }

    def.nBufferCountActual = nGpuBufferNum;
    p->mFbmBufInfo.nBufNum = nGpuBufferNum;

    eError = p->pOmx->set_parameter(NULL, OMX_IndexParamPortDefinition, &def);

    if (eError != OMX_ErrorNone) 
    {
        return -1;
    }

    return 0;
}


FbmBufInfo* AdbGetVideoFbmBufInfo(VideoDecoder* pDecoder)
{
    //*get widht,height,colorFormat,bufferCount
    OMX_ERRORTYPE eError = OMX_ErrorNone;
    VdecoderAdapterBaseContext* p = NULL;

    p = (VdecoderAdapterBaseContext*)pDecoder;
    
    OMX_PARAM_PORTDEFINITIONTYPE def;
    InitOMXParams(&def);
    def.nPortIndex = kPortIndexOutput;

    eError = p->pOmx->get_parameter(NULL, OMX_IndexParamPortDefinition, &def);

    if (eError != OMX_ErrorNone) 
    {
        return NULL;
    }

    p->mFbmBufInfo.nBufWidth    = def.format.video.nFrameWidth;
    p->mFbmBufInfo.nBufHeight   = def.format.video.nFrameHeight;
    p->mFbmBufInfo.ePixelFormat = def.format.video.eColorFormat;
    p->mFbmBufInfo.nBufNum      = def.nBufferCountActual;
    //p->mFbmBufInfo.nBufSize     = def.nBufferSize;
    p->nOutBufferSize           = def.nBufferSize;

    ALOGE("def.format.video.eColorFormat = 0x%x, w = %d, h = %d, size = %d",
           def.format.video.eColorFormat,
           (int)def.format.video.nFrameWidth,
           (int)def.format.video.nFrameHeight,
           (int)def.nBufferSize);
    //* get nativeWindow usage
    OMX_INDEXTYPE eIndex;
    OMX_STRING pName = const_cast<OMX_STRING>(
            "OMX.google.android.index.getAndroidNativeBufferUsage");

    eError = p->pOmx->get_extension_index(NULL, pName, &eIndex);

    if (eError != OMX_ErrorNone) 
    {
        loge("OMX_GetExtensionIndex %s failed", pName);
        return NULL;
    }

    OMX_VERSIONTYPE mVersion;
    mVersion.s.nVersionMajor = 1;
    mVersion.s.nVersionMinor = 0;
    mVersion.s.nRevision = 0;
    mVersion.s.nStep = 0;
    android::GetAndroidNativeBufferUsageParams params = {
        sizeof(android::GetAndroidNativeBufferUsageParams), mVersion, kPortIndexOutput, 0
    };

    eError = p->pOmx->get_parameter(NULL, eIndex, &params);

    if (eError != OMX_ErrorNone) 
    {
        loge("OMX_GetAndroidNativeBufferUsage failed with error %d (0x%08x)",
                eError, eError);
        return NULL;
    }

    //p->mFbmBufInfo.nUsage = params.nUsage;

    return &p->mFbmBufInfo;
}

int AdbSetInEosFlag(VideoDecoder* pDecoder, int nEosFlag)
{
    logd("AdbSetInEosFlag, nEosFlag = %d",nEosFlag);
    VdecoderAdapterBaseContext* p = (VdecoderAdapterBaseContext*)pDecoder;

    p->nInEosFlag = nEosFlag;
    return 0;
}

int AdbGetOutEosFlag(VideoDecoder* pDecoder)
{
    VdecoderAdapterBaseContext* p = (VdecoderAdapterBaseContext*)pDecoder;

    return p->nOutEosFlag;
}

int AdbSetCodecFormatToName(VideoDecoder* pDecoder,
                            const CodecFormatToName* pCodecFormatToName, 
                            int nNum)
{
    VdecoderAdapterBaseContext* p = (VdecoderAdapterBaseContext*)pDecoder;
    int nSize = (nNum*sizeof(CodecFormatToName));
    logd("AdbSetCodecFormatToName, nsize = %d, nNum = %d",nSize, nNum);

    if(p->pKCodecFormatToName)
    {
        free(p->pKCodecFormatToName);
        p->pKCodecFormatToName = NULL;
    }

    p->pKCodecFormatToName = (CodecFormatToName*)malloc(nSize);
    if(p->pKCodecFormatToName == NULL)
    {
        loge("malloc for p->pKCodecFormatToName failed");
        return -1;
    }

    memcpy(p->pKCodecFormatToName, pCodecFormatToName, nSize);
    p->CodecFormatToNameNum = nNum;
    
    return 0;
}

static int setComponentName(VdecoderAdapterBaseContext* p, 
                            enum EVIDEOCODECFORMAT eCodecFormat)
{
    //*transform codecFormat to component name
    int i          = 0;
    int nArraySize = 0;

    nArraySize = p->CodecFormatToNameNum;
    for(i = 0; i < nArraySize; i++)
    {
        if(eCodecFormat == p->pKCodecFormatToName[i].eCodecFormat)
            break;
    }
    
    if(i == nArraySize)
    {
        loge("can not find component name, eCodecFormat = 0x%x",eCodecFormat);
        return -1;
    }

    return p->pOmx->component_init((char*)p->pKCodecFormatToName[i].pComponentName);
}

static int setComponentRole(VdecoderAdapterBaseContext* p, 
                            enum EVIDEOCODECFORMAT eCodecFormat)
{
    OMX_PARAM_COMPONENTROLETYPE mRoleParams;
    int i          = 0;
    int nRet       = -1;
    int nArraySize = 0;

    memset(&mRoleParams, 0, sizeof(OMX_PARAM_COMPONENTROLETYPE));

    nArraySize = p->CodecFormatToNameNum;
    logd("*********** nArraySize = %d, mallocSize = %d, %d",nArraySize,
            sizeof(p->pKCodecFormatToName), sizeof(CodecFormatToName));
    for(i = 0; i < nArraySize; i++)
    {
        if(eCodecFormat == p->pKCodecFormatToName[i].eCodecFormat)
            break;
    }
    
    if(i == nArraySize)
    {
        loge("can not find component role, eCodecFormat = 0x%x",eCodecFormat);
        return -1;
    }
    
    strncpy((char *)mRoleParams.cRole,
            p->pKCodecFormatToName[i].pComponentRole, OMX_MAX_STRINGNAME_SIZE - 1);

    mRoleParams.cRole[OMX_MAX_STRINGNAME_SIZE - 1] = '\0';

    ALOGE("***setParameter, setComponentRole, role = %s",mRoleParams.cRole);
    nRet = p->pOmx->set_parameter(NULL, OMX_IndexParamStandardComponentRole,&mRoleParams);

    if (nRet != OMX_ErrorNone)
    {
        logw("[%s] Failed to set standard component role '%s'.",
             p->pKCodecFormatToName[i].pComponentName, p->pKCodecFormatToName[i].pComponentRole);
    }

    return 0;
}

static int setVideoCompressionFormatType(VdecoderAdapterBaseContext* p, 
                                         enum EVIDEOCODECFORMAT eCodecFormat)
{
    int i          = 0;
    int nRet       = -1;
    int nArraySize = 0;
    bool bFound    = false;
    OMX_VIDEO_PARAM_PORTFORMATTYPE mFormat;

    memset(&mFormat, 0, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));

    nArraySize = p->CodecFormatToNameNum;
    for(i = 0; i < nArraySize; i++)
    {
        if(eCodecFormat == p->pKCodecFormatToName[i].eCodecFormat)
            break;
    }
    
    if(i == nArraySize)
    {
        loge("can not find component name, eCodecFormat = 0x%x",eCodecFormat);
        return -1;
    }

    p->eOmxVideoCodingType = p->pKCodecFormatToName[i].mVideoCodingType;
    
    mFormat.nPortIndex = kPortIndexInput;
    mFormat.nIndex     = 0;

    OMX_U32 nIndex = 0;
    for (;;) 
    {
        mFormat.nIndex = nIndex;
        nRet = p->pOmx->get_parameter(NULL, OMX_IndexParamVideoPortFormat,&mFormat);
        logd("nRet = %d, index = %d",nRet, nIndex);
        if(nRet != OMX_ErrorNone) 
        {
            return nRet;
        }
        logd("mFormat.eCompressionFormat = %d, o = %d, index = %d",
            mFormat.eCompressionFormat,p->pKCodecFormatToName[i].mVideoCodingType,nIndex);
        if (mFormat.eCompressionFormat == p->pKCodecFormatToName[i].mVideoCodingType
            && mFormat.eColorFormat == OMX_COLOR_FormatUnused) 
        {
            bFound = true;
            break;
        }

        ++nIndex;
    }

    if (!bFound) 
    {
        return -1;
    }

    ALOGE("***setParameter, setVideoPortFormatType, compressionFormat = %d",p->pKCodecFormatToName[i].mVideoCodingType);
    nRet = p->pOmx->set_parameter(NULL, OMX_IndexParamVideoPortFormat, &mFormat);

    return nRet;
}

static int setVideoFormatOnPort(VdecoderAdapterBaseContext* p,
                                OMX_U32 nPortIndex,
                                int nWidth, 
                                int nHeight, 
                                int eCompressionFormat) 
{
    int nRet        = -1;
    OMX_PARAM_PORTDEFINITIONTYPE def;
    InitOMXParams(&def);
    def.nPortIndex = nPortIndex;
    def.eDir = (OMX_DIRTYPE)nPortIndex;

    OMX_VIDEO_PORTDEFINITIONTYPE *video_def = &def.format.video;

    nRet = p->pOmx->get_parameter(NULL, OMX_IndexParamPortDefinition, &def);

    if(nRet != OMX_ErrorNone)
    {
        loge("get OMX_IndexParamPortDefinition fail ");
        abort();
    }

    if (nPortIndex == kPortIndexInput) 
    {
        // XXX Need a (much) better heuristic to compute input buffer sizes.
        OMX_U32 nMinSize = 64 * 1024;
        if (def.nBufferSize < nMinSize) 
        {
            def.nBufferSize = nMinSize;
        }
    }

    if((int)def.eDomain != (int)OMX_PortDomainVideo)
    {
        loge("the def.eDomain is not  OMX_PortDomainVideo");
        abort();
    }

    video_def->nFrameWidth  = nWidth;
    video_def->nFrameHeight = nHeight;

    if (nPortIndex == kPortIndexInput) {
        video_def->eCompressionFormat = (OMX_VIDEO_CODINGTYPE)eCompressionFormat;
        video_def->eColorFormat = OMX_COLOR_FormatUnused;
    }

     logd("***setParameter, setVideoInputFormat,nW[%d],nH[%d],eCompressionFormat[0x%x],eColorFormat[0x%x],ef.nBufferSize[%d]",
        (int)video_def->nFrameWidth,
        (int)video_def->nFrameHeight,
        video_def->eCompressionFormat,
        video_def->eColorFormat,
        (int)def.nBufferSize);
        
    nRet = p->pOmx->set_parameter(NULL, OMX_IndexParamPortDefinition, &def);

    return nRet;
}

static int setSupportedOutputFormat(VdecoderAdapterBaseContext* p) 
{
    int i = 0;
    int nRet        = -1;
    OMX_VIDEO_PARAM_PORTFORMATTYPE format;
    InitOMXParams(&format);
    format.nPortIndex = kPortIndexOutput;
    format.nIndex = 0;

    //* we set color format to OMX_COLOR_FormatYUV420Planar when bGpuBufValid is 0
    if(p->mConfig.eOutputPixelFormat == 0)
    {
        nRet = p->pOmx->get_parameter(NULL, OMX_IndexParamVideoPortFormat, &format);
        if(nRet != OMX_ErrorNone)
        {
            loge("get OMX_IndexParamVideoPortFormat fail ");
            //abort();
            return -1;
        }
    }
    else
    {
        for(i = 0; i < 4; i++)
        {
            format.nIndex = i;
            nRet = p->pOmx->get_parameter(NULL, OMX_IndexParamVideoPortFormat, &format);
            if(nRet != OMX_ErrorNone)
            {
                loge("get OMX_IndexParamVideoPortFormat fail ");
                //abort();
                return -1;
            }
			
            if(format.eColorFormat == (OMX_COLOR_FORMATTYPE)p->mConfig.eOutputPixelFormat)
                break;
        }
    }
    
    if((int)format.eCompressionFormat != (int)OMX_VIDEO_CodingUnused)
    {
        loge("the format.eCompressionFormat is not  OMX_VIDEO_CodingUnused");
        abort();
    }

    logd("***setParameter, setVideoOutputFormat, format, eColorFormat = %d",
              format.eColorFormat);

    return p->pOmx->set_parameter(NULL, OMX_IndexParamVideoPortFormat, &format);
}

static int EnableAndroidNativeBuffers(VdecoderAdapterBaseContext* p)
{
    OMX_STRING pName = const_cast<OMX_STRING>(
            "OMX.google.android.index.enableAndroidNativeBuffers");

    OMX_INDEXTYPE eIndex;
    OMX_ERRORTYPE eError = p->pOmx->get_extension_index(NULL, pName, &eIndex);

    if (eError != OMX_ErrorNone) 
    {
        loge("get_extension_index fail, name = %s",pName);
        abort();
    }

    OMX_VERSIONTYPE mVersion;
    mVersion.s.nVersionMajor = 1;
    mVersion.s.nVersionMinor = 0;
    mVersion.s.nRevision = 0;
    mVersion.s.nStep = 0;
    OMX_BOOL enable = OMX_TRUE;
    OMX_U32 portIndex = kPortIndexOutput;
    
    android::EnableAndroidNativeBuffersParams params = {
        sizeof(android::EnableAndroidNativeBuffersParams), mVersion, portIndex, enable,
    };

    eError = p->pOmx->set_parameter(NULL, eIndex, &params);

    if (eError != OMX_ErrorNone) 
    {
        loge("OMX_EnableAndroidNativeBuffers failed with error %d (0x%08x)",
                eError, eError);

        return -1;
    }

    return 0;
}

static int AllocateInBuffer(VdecoderAdapterBaseContext* p)
{
    OMX_U32 i = 0;
    OMX_ERRORTYPE eError = OMX_ErrorNone;
    OMX_PARAM_PORTDEFINITIONTYPE def;
    InitOMXParams(&def);
    def.nPortIndex = kPortIndexInput;

    eError = p->pOmx->get_parameter(NULL, OMX_IndexParamPortDefinition, &def);

    if (eError != OMX_ErrorNone) 
    {
        return eError;
    }

    logv("intput buffer num = %d", def.nBufferCountActual);
    for(i = 0; i < def.nBufferCountActual; i++)
    {
        eError = p->pOmx->allocate_buffer(p->pHComponent, 
                                 &p->mInBufferNode[i].pBufferInfo, 
                                 kPortIndexInput, 
                                 NULL, 
                                 def.nBufferSize);
        if (eError != OMX_ErrorNone) 
        {
            loge("allocate_buffer fail");
            return eError;
        }
        p->mInBufferNode[i].eStatus = OWN_BY_US;
    }
    return 0;
}

static int AllocateOutBuffer(VdecoderAdapterBaseContext* p)
{
    OMX_U32 i = 0;
    OMX_ERRORTYPE eError = OMX_ErrorNone;
    OMX_PARAM_PORTDEFINITIONTYPE def;
    VideoPicture* pVideoPicture = NULL;
    InitOMXParams(&def);
    def.nPortIndex = kPortIndexOutput;

    eError = p->pOmx->get_parameter(NULL, OMX_IndexParamPortDefinition, &def);

    if (eError != OMX_ErrorNone) 
    {
        return eError;
    }

    logd("output buffer num = %d", (int)def.nBufferCountActual);
    for(i = 0; i < def.nBufferCountActual; i++)
    {
        eError = p->pOmx->allocate_buffer(p->pHComponent, 
                                 &p->mOutBufferNode[i].pBufferInfo, 
                                 kPortIndexOutput, 
                                 NULL, 
                                 def.nBufferSize);
        if (eError != OMX_ErrorNone) 
        {
            loge("allocate_buffer fail");
            return eError;
        }

        pVideoPicture = &p->mOutBufferNode[i].mVideoPicture;
        pVideoPicture->pData0  = (char*)p->mOutBufferNode[i].pBufferInfo->pBuffer;
        pVideoPicture->nWidth  = def.format.video.nFrameWidth;
        pVideoPicture->nHeight = def.format.video.nFrameHeight;
        pVideoPicture->pData1  = pVideoPicture->pData0 + 
                                 pVideoPicture->nWidth*pVideoPicture->nHeight;
        pVideoPicture->pData2  = pVideoPicture->pData1 + 
                                 pVideoPicture->nWidth*pVideoPicture->nHeight/4;

        pVideoPicture->nLineStride = def.format.video.nFrameWidth;

        pVideoPicture->ePixelFormat =  PIXEL_FORMAT_YUV_PLANER_420;
        pVideoPicture->bIsProgressive = 1;
        
        p->mOutBufferNode[i].mVideoPicture.pPrivate = &p->mOutBufferNode[i];

        p->mOutBufferNode[i].eStatus = OWN_BY_US;
    }
    return 0;
}

static int StartComponent(VdecoderAdapterBaseContext* p)
{
    int i = 0;
    OmxInBufferNode* pInBufferNodeTail = NULL;
    
    //* set component status to idle
    p->pOmx->send_command(NULL, OMX_CommandStateSet, OMX_StateIdle, NULL);
    logd("sem wait mLoadToIdleSem start");
    sem_wait(&p->mLoadToIdleSem);
    logd("sem wait mLoadToIdleSem finish");

    p->mState = OMX_StateIdle;

    //* put input buffer to queue
    pthread_mutex_lock(&p->mInBufferMutex);
    for(i = 0; i < MAX_BUFFER_NUM; i++)
    {
        if(p->mInBufferNode[i].eStatus == OWN_BY_US)
        {
            p->mInBufferNode[i].pNext = NULL;

            if(p->pInBufferNodeHeader == NULL)
            {
                p->pInBufferNodeHeader = &p->mInBufferNode[i];
            }
            else
            {
                pInBufferNodeTail = p->pInBufferNodeHeader;
                while(pInBufferNodeTail->pNext != NULL)
                {
                    pInBufferNodeTail = pInBufferNodeTail->pNext;
                }
                pInBufferNodeTail->pNext = &p->mInBufferNode[i];
            }
        }
    }
    pthread_mutex_unlock(&p->mInBufferMutex);


    //* set component status to execute
    p->pOmx->send_command(NULL, OMX_CommandStateSet, OMX_StateExecuting, NULL);
    logd("sem wait mIdleToExecuteSem start");
    sem_wait(&p->mIdleToExecuteSem);
    logd("sem wait mIdleToExecuteSem finish");

    p->mState = OMX_StateExecuting;

    //* send output buffer to component
    pthread_mutex_lock(&p->mOutBufferMutex);
    for(i = 0; i < MAX_BUFFER_NUM; i++)
    {
        if(p->mOutBufferNode[i].eStatus == OWN_BY_US)
        {
            p->pOmx->fill_this_buffer(p->pHComponent, p->mOutBufferNode[i].pBufferInfo);
            p->mOutBufferNode[i].eStatus = OWN_BY_COMPONENT;
        }
    }
    pthread_mutex_unlock(&p->mOutBufferMutex);

    //* send specificData to component
    #if 1
    if(p->nSpecificDataLen > 0 && p->pSpecificData != NULL)
    {
        pthread_mutex_lock(&p->mInBufferMutex);
        if(p->pInBufferNodeHeader != NULL)
        {
            OMX_BUFFERHEADERTYPE* pBufferInfo = p->pInBufferNodeHeader->pBufferInfo;
            if(p->nSpecificDataLen <= (int)pBufferInfo->nAllocLen)
            {
                memcpy(pBufferInfo->pBuffer, p->pSpecificData, p->nSpecificDataLen);
                pBufferInfo->nFilledLen = p->nSpecificDataLen;
                pBufferInfo->nTimeStamp = 0;
                pBufferInfo->nOffset    = 0;
                pBufferInfo->nFlags     = OMX_BUFFERFLAG_ENDOFFRAME; 
                 pBufferInfo->nFlags    |= OMX_BUFFERFLAG_CODECCONFIG;
                logd("send specical data, len = %d",p->nSpecificDataLen);
                p->pOmx->empty_this_buffer(p->pHComponent,pBufferInfo);

                p->pInBufferNodeHeader->eStatus = OWN_BY_COMPONENT;
                
                if(p->pInBufferNodeHeader->pNext != NULL)
                    p->pInBufferNodeHeader = p->pInBufferNodeHeader->pNext;
                else
                    p->pInBufferNodeHeader = NULL;
            }
            else
            {
                loge("the specificDataLen is too big, %d, %d",
                      p->nSpecificDataLen, (int)pBufferInfo->nAllocLen);
            }
        }
        else
        {
            loge("error: p->pInBufferNodeHeader is null when send specificData");
            pthread_mutex_unlock(&p->mInBufferMutex);
            return -1;
        }
        pthread_mutex_unlock(&p->mInBufferMutex);
    }
    #endif
    return 0;
}

static int StopComponent(VdecoderAdapterBaseContext* p)
{
    int i = 0;

    if(p->mState == OMX_StateExecuting)
    {
		//* set component status to idle
		p->pOmx->send_command(NULL, OMX_CommandStateSet, OMX_StateIdle, NULL);
		logd("sem wait mExecuteToIdleSem start");
		sem_wait(&p->mExecuteToIdleSem);
		logd("sem wait mExecuteToIdleSem finish");

		//* set component status to Load
		p->pOmx->send_command(NULL, OMX_CommandStateSet, OMX_StateLoaded, NULL);

        //* free in buffer
		for(i = 0; i < MAX_BUFFER_NUM; i++)
		{
			if(p->mInBufferNode[i].pBufferInfo != NULL)
			{
				logd("in buffer state when free, state = %d, i = %d",
					 p->mInBufferNode[i].eStatus, i);

				p->pOmx->free_buffer(p->pHComponent, kPortIndexInput, p->mInBufferNode[i].pBufferInfo);
				p->mInBufferNode[i].pBufferInfo = NULL;
			}
		}

		//* free out buffer
		for(i = 0; i < MAX_BUFFER_NUM; i++)
		{
			if(p->mOutBufferNode[i].pBufferInfo != NULL)
			{
				logd("out buffer state when free, state = %d, i = %d",
					 p->mInBufferNode[i].eStatus, i);

				p->pOmx->free_buffer(p->pHComponent, kPortIndexOutput, p->mOutBufferNode[i].pBufferInfo);
				p->mOutBufferNode[i].pBufferInfo = NULL;
			}
		}
        logd("sem wait mIdleToLoadSem start");
		sem_wait(&p->mIdleToLoadSem);
		logd("sem wait mIdleToLoadSem finish");

		p->mState = OMX_StateLoaded;
    }

    return 0;
}

static int DecideStreamBufferSize(VideoStreamInfo* pVideoInfo,
                                  VConfig* pConfig)
{
    int nVideoHeight;
    int eCodecFormat;
    int nBufferSize;
    
    if(pConfig->nVbvBufferSize>=0x10000 && pConfig->nVbvBufferSize<=0x800000)
    {
    	nBufferSize = pConfig->nVbvBufferSize;
    	return nBufferSize;
    }
    //* we decide stream buffer size by resolution and codec format.
    nVideoHeight = pVideoInfo->nHeight;
    eCodecFormat = pVideoInfo->eCodecFormat;

    //* for skia create sbm
    if((pConfig->eOutputPixelFormat == PIXEL_FORMAT_RGBA) 
    	&& (eCodecFormat == VIDEO_CODEC_FORMAT_MJPEG)
    	&& pConfig->nVbvBufferSize)
    {
    	nBufferSize = pConfig->nVbvBufferSize;
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

static void* VideoNativeThread(void* arg)
{
    VdecoderAdapterBaseContext* p = (VdecoderAdapterBaseContext*)arg;
    Message                 msg;

    while(1)
    {
        if(VideoMessageQueueTryGetMessage(p->mq, &msg, 200) < 0)
        {
            logv("have no message .");
            continue;
        }

        logv("read messageId = %d",msg.messageId);
        if(msg.messageId == QCOM_MESSAGE_ID_FREE_OUT_BUFFER)
        {
        	OMX_BUFFERHEADERTYPE* pBuffer;
        	pBuffer = (OMX_BUFFERHEADERTYPE*)msg.params[0];
        	p->pOmx->free_buffer(p->pHComponent, kPortIndexOutput, pBuffer);
        }
        else if(msg.messageId == QCOM_MESSAGE_ID_QUIT)
        {
        	break;
        }
    }
    
    int ret = 0;
    pthread_exit(&ret);

    return NULL;
}


//***********************function for message*************************************//

//* define a semaphore timedwait method for common use.
static int SemTimedWait(sem_t* sem, int64_t time_ms)
{
    int err;

    struct timeval  tv;
    struct timespec ts;

    if(time_ms == -1)
    {
        err = sem_wait(sem);
    }
    else
    {
        gettimeofday(&tv, NULL);
        ts.tv_sec  = tv.tv_sec;
        ts.tv_nsec = tv.tv_usec*1000 + time_ms*1000000;
        ts.tv_sec += ts.tv_nsec/(1000*1000*1000);
        ts.tv_nsec = ts.tv_nsec % (1000*1000*1000);
        
        err = sem_timedwait(sem, &ts);
    }

    return err;
}

MessageQueue* VideoMessageQueueCreate(int nMaxMessageNum, const char* pName)
{
    MessageQueueContext* mqCtx;
    
    mqCtx = (MessageQueueContext*)malloc(sizeof(MessageQueueContext));
    if(mqCtx == NULL)
    {
        loge("%s, allocate memory fail.", pName);
        return NULL;
    }
    memset(mqCtx, 0, sizeof(MessageQueueContext));
    
    if(pName != NULL)
        mqCtx->pName = strdup(pName);
    
    mqCtx->Nodes = (MessageNode*)malloc(nMaxMessageNum*sizeof(MessageNode));
    if(mqCtx->Nodes == NULL)
    {
        loge("%s, allocate memory for message nodes fail.", mqCtx->pName);
        free(mqCtx);
        return NULL;
    }
    memset(mqCtx->Nodes, 0, sizeof(MessageNode)*nMaxMessageNum);
    
    mqCtx->nMaxMessageNum = nMaxMessageNum;
    
    pthread_mutex_init(&mqCtx->mutex, NULL);
    sem_init(&mqCtx->sem, 0, 0);
    
    return (MessageQueue*)mqCtx;
}


void VideoMessageQueueDestroy(MessageQueue* mq)
{
    MessageQueueContext* mqCtx;
    
    mqCtx = (MessageQueueContext*)mq;
    
    if(mqCtx->Nodes != NULL)
    {
        free(mqCtx->Nodes);
    }
    
    pthread_mutex_destroy(&mqCtx->mutex);
    sem_destroy(&mqCtx->sem);
    
    if(mqCtx->pName != NULL)
        free(mqCtx->pName);
    
    free(mqCtx);
    
    return;
}


int VideoMessageQueuePostMessage(MessageQueue* mq, Message* m)
{
    MessageQueueContext* mqCtx;
    MessageNode*         node;
    MessageNode*         ptr;
    int                  i;
    
    mqCtx = (MessageQueueContext*)mq;
    
    pthread_mutex_lock(&mqCtx->mutex);
    
    if(mqCtx->nCount >= mqCtx->nMaxMessageNum)
    {
        loge("%s, message count exceed, current message count = %d, max message count = %d", 
                mqCtx->pName, mqCtx->nCount, mqCtx->nMaxMessageNum);
        pthread_mutex_unlock(&mqCtx->mutex);
        return -1;
    }
    
    node = NULL;
    ptr  = mqCtx->Nodes;
    for(i=0; i<mqCtx->nMaxMessageNum; i++, ptr++)
    {
        if(ptr->valid == 0)
        {
            node = ptr;
            break;
        }
    }
    
    node->msg.messageId = m->messageId;
    node->msg.params[0] = m->params[0];
    node->msg.params[1] = m->params[1];
    node->msg.params[2] = m->params[2];
    node->msg.params[3] = m->params[3];
    node->valid         = 1;
    node->next          = NULL;
    
    ptr = mqCtx->pHead;
    if(ptr == NULL)
        mqCtx->pHead = node;
    else
    {
        while(ptr->next != NULL)
            ptr = ptr->next;
        
        ptr->next = node;
    }
        
    mqCtx->nCount++;
    
    pthread_mutex_unlock(&mqCtx->mutex);
    
    sem_post(&mqCtx->sem);
    
    return 0;
}


int VideoMessageQueueGetMessage(MessageQueue* mq, Message* m)
{
    return VideoMessageQueueTryGetMessage(mq, m, -1);
}


int VideoMessageQueueTryGetMessage(MessageQueue* mq, Message* m, int64_t timeout)
{
    MessageQueueContext* mqCtx;
    MessageNode*         node;
    
    mqCtx = (MessageQueueContext*)mq;
    
    if(SemTimedWait(&mqCtx->sem, timeout) < 0)
    {
        return -1;
    }
    
    pthread_mutex_lock(&mqCtx->mutex);
    
    if(mqCtx->nCount <= 0)
    {
        logv("%s, no message.", mqCtx->pName);
        pthread_mutex_unlock(&mqCtx->mutex);
        return -1;
    }
    
    node = mqCtx->pHead;
    mqCtx->pHead = node->next;
    
    m->messageId = node->msg.messageId;
    m->params[0] = node->msg.params[0];
    m->params[1] = node->msg.params[1];
    m->params[2] = node->msg.params[2];
    m->params[3] = node->msg.params[3];
    node->valid  = 0;
        
    mqCtx->nCount--;
    
    pthread_mutex_unlock(&mqCtx->mutex);
    
    return 0;
}


int VideoMessageQueueFlush(MessageQueue* mq)
{
    MessageQueueContext* mqCtx;
    int                  i;
    
    mqCtx = (MessageQueueContext*)mq;
    
    logi("%s, flush messages.", mqCtx->pName);
    
    pthread_mutex_lock(&mqCtx->mutex);
    
    mqCtx->pHead  = NULL;
    mqCtx->nCount = 0;
    for(i=0; i<mqCtx->nMaxMessageNum; i++)
    {
        mqCtx->Nodes[i].valid = 0;
    }
    
    do
    {
        if(sem_getvalue(&mqCtx->sem, &i) != 0 || i == 0)
            break;
        
        sem_trywait(&mqCtx->sem);
        
    }while(1);
    
    pthread_mutex_unlock(&mqCtx->mutex);
    
    return 0;
}


int VideoMessageQueueGetCount(MessageQueue* mq)
{
    MessageQueueContext* mqCtx;
    
    mqCtx = (MessageQueueContext*)mq;
    
    return mqCtx->nCount;
}

//***********************end (function for message)*********************************//

