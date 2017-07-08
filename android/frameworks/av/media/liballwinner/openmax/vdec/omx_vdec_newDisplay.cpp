

/*============================================================================
                            O p e n M A X   w r a p p e r s
                             O p e n  M A X   C o r e

*//** @file omx_vdec.cpp
  This module contains the implementation of the OpenMAX core & component.

*//*========================================================================*/

//////////////////////////////////////////////////////////////////////////////
//                             Include Files
//////////////////////////////////////////////////////////////////////////////

//#define CONFIG_LOG_LEVEL    OPTION_LOG_LEVEL_DETAIL
//#define LOG_TAG "omx_vdec_newDisplay"
#include "log.h"

#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include "omx_vdec_newDisplay.h"
#include <fcntl.h>
#include "AWOMX_VideoIndexExtension.h"
#include "transform_color_format.h"

#include "memoryAdapter.h"
#include "vdecoder.h"

#include <hardware/hal_public.h>
#include <linux/ion.h>
#include <ion/ion.h>

#include "secureMemoryAdapter.h"

//* the values must be check with ACodec when update android 
#define DISPLAY_HOLH_BUFFER_NUM_DEFAULT (4) //* this is for secure video, as it is difficult to 
                                    //* get the num from ACodec because of multi thread.
                                    //* if you are not agree with the solution, just discuss
                                    //* with me. but who I am , haha


#if (CONFIG_OS_VERSION >= OPTION_OS_VERSION_ANDROID_5_0 && CONFIG_CHIP == OPTION_CHIP_1667)
#include "gralloc_priv.h"
#endif


#if CONFIG_OS == OPTION_OS_ANDROID
    #include <binder/IPCThreadState.h>
    #include <media/stagefright/foundation/ADebug.h>
    #include <ui/GraphicBufferMapper.h>
    #include <ui/Rect.h>
    #include <HardwareAPI.h>
#endif

#if CONFIG_CHIP == OPTION_CHIP_1639
#define PHY_OFFSET 0x20000000
#else
#define PHY_OFFSET 0x40000000
#endif

#define debug logi("LINE %d, FUNCTION %s", __LINE__, __FUNCTION__);
#define OPEN_STATISTICS (0)
#define SAVE_PICTURE    (0)

#if CONFIG_OS == OPTION_OS_ANDROID
    using namespace android;
#endif

/* H.263 Supported Levels & profiles */
static VIDEO_PROFILE_LEVEL_TYPE SupportedH263ProfileLevels[] = {
  {OMX_VIDEO_H263ProfileBaseline, OMX_VIDEO_H263Level10},
  {OMX_VIDEO_H263ProfileBaseline, OMX_VIDEO_H263Level20},
  {OMX_VIDEO_H263ProfileBaseline, OMX_VIDEO_H263Level30},
  {OMX_VIDEO_H263ProfileBaseline, OMX_VIDEO_H263Level40},
  {OMX_VIDEO_H263ProfileBaseline, OMX_VIDEO_H263Level45},
  {OMX_VIDEO_H263ProfileBaseline, OMX_VIDEO_H263Level50},
  {OMX_VIDEO_H263ProfileBaseline, OMX_VIDEO_H263Level60},
  {OMX_VIDEO_H263ProfileBaseline, OMX_VIDEO_H263Level70},
  {-1, -1}};

/* MPEG4 Supported Levels & profiles */
static VIDEO_PROFILE_LEVEL_TYPE SupportedMPEG4ProfileLevels[] ={
  {OMX_VIDEO_MPEG4ProfileSimple, OMX_VIDEO_MPEG4Level0},
  {OMX_VIDEO_MPEG4ProfileSimple, OMX_VIDEO_MPEG4Level0b},
  {OMX_VIDEO_MPEG4ProfileSimple, OMX_VIDEO_MPEG4Level1},
  {OMX_VIDEO_MPEG4ProfileSimple, OMX_VIDEO_MPEG4Level2},
  {OMX_VIDEO_MPEG4ProfileSimple, OMX_VIDEO_MPEG4Level3},
  {OMX_VIDEO_MPEG4ProfileSimple, OMX_VIDEO_MPEG4Level4},
  {OMX_VIDEO_MPEG4ProfileSimple, OMX_VIDEO_MPEG4Level4a},
  {OMX_VIDEO_MPEG4ProfileSimple, OMX_VIDEO_MPEG4Level5},
  {OMX_VIDEO_MPEG4ProfileAdvancedSimple, OMX_VIDEO_MPEG4Level0},
  {OMX_VIDEO_MPEG4ProfileAdvancedSimple, OMX_VIDEO_MPEG4Level0b},
  {OMX_VIDEO_MPEG4ProfileAdvancedSimple, OMX_VIDEO_MPEG4Level1},
  {OMX_VIDEO_MPEG4ProfileAdvancedSimple, OMX_VIDEO_MPEG4Level2},
  {OMX_VIDEO_MPEG4ProfileAdvancedSimple, OMX_VIDEO_MPEG4Level3},
  {OMX_VIDEO_MPEG4ProfileAdvancedSimple, OMX_VIDEO_MPEG4Level4},
  {OMX_VIDEO_MPEG4ProfileAdvancedSimple, OMX_VIDEO_MPEG4Level5},
  {-1,-1}};

/* AVC Supported Levels & profiles */
static VIDEO_PROFILE_LEVEL_TYPE SupportedAVCProfileLevels[] ={
  {OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel1},
  {OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel1b},
  {OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel11},
  {OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel12},
  {OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel13},
  {OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel2},
  {OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel21},
  {OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel22},
  {OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel3},
  {OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel31},
  {OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel32},
  {OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel4},
  {OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel41},
  {OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel42},
  {OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel5},
  {OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel51},

  {OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel1},
  {OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel1b},
  {OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel11},
  {OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel12},
  {OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel13},
  {OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel2 },
  {OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel21},
  {OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel22},
  {OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel3 },
  {OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel31},
  {OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel32},
  {OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel4},
  {OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel41},
  {OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel42},
  {OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel5},
  {OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel51},
  
  {OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel1 },        
  {OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel1b},      
  {OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel11},     
  {OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel12},     
  {OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel13},    
  {OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel2 },    
  {OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel21},   
  {OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel22},  
  {OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel3 },  
  {OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel31},
  {OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel32},
  {OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel4},
  {OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel41},
  {OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel42},
  {OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel5},
  {OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel51},

  {-1,-1}};
/*
 *     M A C R O S
 */

/*
 * Initializes a data structure using a pointer to the structure.
 * The initialization of OMX structures always sets up the nSize and nVersion fields
 *   of the structure.
 */
#define OMX_CONF_INIT_STRUCT_PTR(_variable_, _struct_name_)	\
    memset((_variable_), 0x0, sizeof(_struct_name_));	\
    (_variable_)->nSize = sizeof(_struct_name_);		\
    (_variable_)->nVersion.s.nVersionMajor = 0x1;	\
    (_variable_)->nVersion.s.nVersionMinor = 0x1;	\
    (_variable_)->nVersion.s.nRevision = 0x0;		\
    (_variable_)->nVersion.s.nStep = 0x0



static VIDDEC_CUSTOM_PARAM sVideoDecCustomParams[] =
{
	{VIDDEC_CUSTOMPARAM_ENABLE_ANDROID_NATIVE_BUFFER, (OMX_INDEXTYPE)AWOMX_IndexParamVideoEnableAndroidNativeBuffers},
	{VIDDEC_CUSTOMPARAM_GET_ANDROID_NATIVE_BUFFER_USAGE, (OMX_INDEXTYPE)AWOMX_IndexParamVideoGetAndroidNativeBufferUsage},
	{VIDDEC_CUSTOMPARAM_USE_ANDROID_NATIVE_BUFFER2, (OMX_INDEXTYPE)AWOMX_IndexParamVideoUseAndroidNativeBuffer2},
    {VIDDEC_CUSTOMPARAM_STORE_META_DATA_IN_BUFFER,(OMX_INDEXTYPE)AWOMX_IndexParamVideoUseStoreMetaDataInBuffer},
	{VIDDEC_CUSTOMPARAM_PREPARE_FOR_ADAPTIVE_PLAYBACK,(OMX_INDEXTYPE)AWOMX_IndexParamVideoUsePrepareForAdaptivePlayback}
};


static void* ComponentThread(void* pThreadData);
static void* ComponentVdrvThread(void* pThreadData);

#if CONFIG_OS == OPTION_OS_ANDROID
    #define GET_CALLING_PID	(IPCThreadState::self()->getCallingPid())
    static void getCallingProcessName(char *name)
    {
    	char proc_node[128];

    	if (name == 0)
    	{
    		logd("error in params");
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
    		logd("Obtain calling process failed");
    	}
    }
#endif

//* factory function executed by the core to create instances
void *get_omx_component_factory_fn(void)
{
	return (new omx_vdec);
}

#if (OPEN_STATISTICS)
static int64_t OMX_GetNowUs() {
    struct timeval tv;
    gettimeofday(&tv, NULL);

    return (int64_t)tv.tv_sec * 1000000ll + tv.tv_usec;
}
#endif

typedef enum OMX_VDRV_COMMANDTYPE
{
    OMX_VdrvCommand_PrepareVdecLib,
    OMX_VdrvCommand_CloseVdecLib,
    OMX_VdrvCommand_FlushVdecLib,
    OMX_VdrvCommand_Stop,
    OMX_VdrvCommand_Max = 0X7FFFFFFF,
} OMX_VDRV_COMMANDTYPE;

void post_message_to_vdrv(omx_vdec *omx, OMX_S32 id)
{
      int ret_value;
      logi("omx_vdec: post_message %d pipe out%d to vdrv\n", (int)id,omx->m_vdrv_cmdpipe[1]);
      ret_value = write(omx->m_vdrv_cmdpipe[1], &id, sizeof(OMX_S32));
      logi("post_message to pipe done %d\n",ret_value);
}

omx_vdec::omx_vdec()
{
    logd("(f:%s, l:%d) ", __FUNCTION__, __LINE__);
	m_state               = OMX_StateLoaded;
	m_cRole[0]            = 0;
	m_cName[0]            = 0;
	m_eCompressionFormat  = OMX_VIDEO_CodingUnused;
	m_pAppData            = NULL;
	m_thread_id           = 0;
    m_vdrv_thread_id      = 0;
	m_cmdpipe[0]          = 0;
	m_cmdpipe[1]          = 0;
	m_cmddatapipe[0]      = 0;
	m_cmddatapipe[1]      = 0;
    m_vdrv_cmdpipe[0]     = 0;
    m_vdrv_cmdpipe[1]     = 0;
	m_InputNum            = 0;
	m_OutputNum           = 0;
    m_maxWidth            = 0;
    m_maxHeight           = 0;
	m_decoder             = NULL;
    mPicNum               = 0;
    mCodecSpecificDataLen = 0;

    m_storeOutputMetaDataFlag = OMX_FALSE;
    m_useAndroidBuffer        = OMX_FALSE;
    mIsFromCts                = OMX_FALSE;
    mVp9orH265SoftDecodeFlag  = OMX_FALSE;
    mResolutionChangeFlag     = OMX_FALSE;
    pMarkData                 = NULL;
    hMarkTargetComponent      = NULL;
    bPortSettingMatchFlag     = OMX_TRUE;
    mIs4KAlignFlag            = OMX_FALSE;
    mHadInitDecoderFlag       = OMX_FALSE;
    mIsSecureVideoFlag        = OMX_FALSE;
    mIsFlushingFlag           = OMX_FALSE;
    mIsSoftwareDecoderFlag    = OMX_FALSE;
    mInputEosFlag             = OMX_FALSE;
    mFirstPictureFlag         = OMX_TRUE;
    mVdrvNotifyEosFlag        = OMX_FALSE;
	
    mHadGetVideoFbmBufInfoFlag   = OMX_FALSE;
	mSetToDecoderBufferNum       = 0;
	mNeedReSetToDecoderBufferNum = 0;

    mExtraOutBufferNum           = 0;
    
	memset(mCallingProcess,0,sizeof(mCallingProcess));
		
#if CONFIG_OS == OPTION_OS_ANDROID    
	getCallingProcessName(mCallingProcess);
	if((strcmp(mCallingProcess, "com.android.cts.media") == 0) || (strcmp(mCallingProcess, "com.android.cts.videoperf") == 0) || (strcmp(mCallingProcess, "com.android.pts.videoperf") == 0))
	{
		mIsFromCts           = true;
	}
#endif

    //* we set gpu align to 16 as defual, maybe we should set it 
    //* rely on different chip(as 1673, 1680) if they are not the same
#if(CONFIG_CHIP == OPTION_CHIP_1673 || GPU_TYPE_MALI == 1)    
    mGpuAlignStride = 32;
#else
    mGpuAlignStride = 16;
#endif

	memset(&m_Callbacks, 0, sizeof(m_Callbacks));
	memset(&m_sInPortDefType, 0, sizeof(m_sInPortDefType));
	memset(&m_sOutPortDefType, 0, sizeof(m_sOutPortDefType));
	memset(&m_sInPortFormatType, 0, sizeof(m_sInPortFormatType));
	memset(&m_sOutPortFormatType, 0, sizeof(m_sOutPortFormatType));
	memset(&m_sPriorityMgmtType, 0, sizeof(m_sPriorityMgmtType));
	memset(&m_sInBufSupplierType, 0, sizeof(m_sInBufSupplierType));
	memset(&m_sOutBufSupplierType, 0, sizeof(m_sOutBufSupplierType));
	memset(&m_sInBufList, 0, sizeof(m_sInBufList));
	memset(&m_sOutBufList, 0, sizeof(m_sOutBufList));
	memset(&m_streamInfo, 0, sizeof(m_streamInfo));
    memset(&mOutputBufferInfo, 0, sizeof(OMXOutputBufferInfoT)*OMX_MAX_VIDEO_BUFFER_NUM);
    memset(&mVideoRect, 0 , sizeof(OMX_CONFIG_RECTTYPE));
    memset(mCodecSpecificData, 0 , CODEC_SPECIFIC_DATA_LENGTH);
	
	pthread_mutex_init(&m_inBufMutex, NULL);
	pthread_mutex_init(&m_outBufMutex, NULL);
    pthread_mutex_init(&m_pipeMutex, NULL);
    
    sem_init(&m_vdrv_cmd_lock,0,0);
    sem_init(&m_semExtraOutBufferNum,0,0);

    mIonFd = -1;
    mIonFd = ion_open();

    logd("ion open fd = %d",(int)mIonFd);
    if(mIonFd < 0)
    {
        loge("ion open fail ! ");
    }

}


omx_vdec::~omx_vdec()
{
	OMX_S32 nIndex;
    // In case the client crashes, check for nAllocSize parameter.
    // If this is greater than zero, there are elements in the list that are not free'd.
    // In that case, free the elements.
    logi("(f:%s, l:%d) ", __FUNCTION__, __LINE__);
	pthread_mutex_lock(&m_inBufMutex);

    for(nIndex=0; nIndex<m_sInBufList.nBufArrSize; nIndex++)
    {
        if(m_sInBufList.pBufArr==NULL)
        {
            break;
        }
        
        if(m_sInBufList.pBufArr[nIndex].pBuffer != NULL)
        {
            if(m_sInBufList.nAllocBySelfFlags & (1<<nIndex))
            {
                if(mIsSecureVideoFlag == OMX_TRUE)
                {
                    #if(ADJUST_ADDRESS_FOR_SECURE_OS_OPTEE)
                    char*   pVirtAddr = (char*)m_sInBufList.pBufArr[nIndex].pBuffer;
                    #else
                    OMX_U8* pPhyAddr  = m_sInBufList.pBufArr[nIndex].pBuffer;
                    char*   pVirtAddr = (char*)SecureMemAdapterGetVirtualAddressCpu(pPhyAddr);
                    #endif
                    
                    VideoReleaseSecureBuffer(m_decoder,pVirtAddr);
                }
                else
                    free(m_sInBufList.pBufArr[nIndex].pBuffer);
                
                m_sInBufList.pBufArr[nIndex].pBuffer = NULL;
            }
        }
    }

    if (m_sInBufList.pBufArr != NULL)
    	free(m_sInBufList.pBufArr);

    if (m_sInBufList.pBufHdrList != NULL)
    	free(m_sInBufList.pBufHdrList);

	memset(&m_sInBufList, 0, sizeof(struct _BufferList));
	m_sInBufList.nBufArrSize = m_sInPortDefType.nBufferCountActual;

	pthread_mutex_unlock(&m_inBufMutex);


	pthread_mutex_lock(&m_outBufMutex);

	for(nIndex=0; nIndex<m_sOutBufList.nBufArrSize; nIndex++)
	{
		if(m_sOutBufList.pBufArr[nIndex].pBuffer != NULL)
		{
			if(m_sOutBufList.nAllocBySelfFlags & (1<<nIndex))
			{
				free(m_sOutBufList.pBufArr[nIndex].pBuffer);
				m_sOutBufList.pBufArr[nIndex].pBuffer = NULL;
			}
		}
	}

    if (m_sOutBufList.pBufArr != NULL)
    	free(m_sOutBufList.pBufArr);

    if (m_sOutBufList.pBufHdrList != NULL)
    	free(m_sOutBufList.pBufHdrList);

	memset(&m_sOutBufList, 0, sizeof(struct _BufferList));
	m_sOutBufList.nBufArrSize = m_sOutPortDefType.nBufferCountActual;

	pthread_mutex_unlock(&m_outBufMutex);

    if(m_decoder != NULL)
    {
    	DestroyVideoDecoder(m_decoder);
    	m_decoder = NULL;
    }

    pthread_mutex_destroy(&m_inBufMutex);
    pthread_mutex_destroy(&m_outBufMutex);
    
    pthread_mutex_destroy(&m_pipeMutex);
    sem_destroy(&m_vdrv_cmd_lock);
    sem_destroy(&m_semExtraOutBufferNum);

    if(m_streamInfo.pCodecSpecificData)
        free(m_streamInfo.pCodecSpecificData);

    //* free all gpu buffer 
    int i;
    for(i = 0; i < OMX_MAX_VIDEO_BUFFER_NUM; i++)
    {
 
#if(CONFIG_OS_VERSION >= OPTION_OS_VERSION_ANDROID_5_0)
        if(mOutputBufferInfo[i].handle_ion != 0)
        {
            logd("ion_free: handle_ion[%p],i[%d]",mOutputBufferInfo[i].handle_ion,i);
            ion_free(mIonFd,mOutputBufferInfo[i].handle_ion);
        }
#else
        if(mOutputBufferInfo[i].handle_ion != NULL)
        {
            logd("ion_free: handle_ion[%p],i[%d]",mOutputBufferInfo[i].handle_ion,i);
            ion_free(mIonFd,mOutputBufferInfo[i].handle_ion);
        }		
#endif

    }

    
    if(mIonFd > 0)
        ion_close(mIonFd);

#if(OPEN_STATISTICS)
    if(mDecodeFrameTotalCount!=0 && mConvertTotalCount!=0)
    {
        mDecodeFrameSmallAverageDuration = (mDecodeFrameTotalDuration + mDecodeOKTotalDuration)/(mDecodeFrameTotalCount);
        mDecodeFrameBigAverageDuration = (mDecodeFrameTotalDuration + mDecodeOKTotalDuration + mDecodeNoFrameTotalDuration + mDecodeNoBitstreamTotalDuration + mDecodeOtherTotalDuration)/(mDecodeFrameTotalCount);
        if(mDecodeNoFrameTotalCount > 0)
        {
            mDecodeNoFrameAverageDuration = (mDecodeNoFrameTotalDuration)/(mDecodeNoFrameTotalCount);
        }
        else
        {
            mDecodeNoFrameAverageDuration = 0;
        }
        if(mDecodeNoBitstreamTotalCount > 0)
        {
            mDecodeNoBitstreamAverageDuration = (mDecodeNoBitstreamTotalDuration)/(mDecodeNoBitstreamTotalCount);
        }
        else
        {
            mDecodeNoBitstreamAverageDuration = 0;
        }
        mConvertAverageDuration = mConvertTotalDuration/mConvertTotalCount;
        logd("decode and convert statistics:\
            \n mDecodeFrameTotalDuration[%lld]ms, mDecodeOKTotalDuration[%lld]ms, mDecodeNoFrameTotalDuration[%lld]ms, mDecodeNoBitstreamTotalDuration[%lld]ms, mDecodeOtherTotalDuration[%lld]ms,\
            \n mDecodeFrameTotalCount[%lld], mDecodeOKTotalCount[%lld], mDecodeNoFrameTotalCount[%lld], mDecodeNoBitstreamTotalCount[%lld], mDecodeOtherTotalCount[%lld],\
            \n mDecodeFrameSmallAverageDuration[%lld]ms, mDecodeFrameBigAverageDuration[%lld]ms, mDecodeNoFrameAverageDuration[%lld]ms, mDecodeNoBitstreamAverageDuration[%lld]ms\
            \n mConvertTotalDuration[%lld]ms, mConvertTotalCount[%lld], mConvertAverageDuration[%lld]ms",
            mDecodeFrameTotalDuration/1000, mDecodeOKTotalDuration/1000, mDecodeNoFrameTotalDuration/1000, mDecodeNoBitstreamTotalDuration/1000, mDecodeOtherTotalDuration/1000,
            mDecodeFrameTotalCount, mDecodeOKTotalCount, mDecodeNoFrameTotalCount, mDecodeNoBitstreamTotalCount, mDecodeOtherTotalCount,
            mDecodeFrameSmallAverageDuration/1000, mDecodeFrameBigAverageDuration/1000, mDecodeNoFrameAverageDuration/1000, mDecodeNoBitstreamAverageDuration/1000,
            mConvertTotalDuration/1000, mConvertTotalCount, mConvertAverageDuration/1000);
    }
#endif

    logd("~omx_dec done!");
}


OMX_ERRORTYPE omx_vdec::component_init(OMX_STRING pName)
{
	OMX_ERRORTYPE eRet = OMX_ErrorNone;
    int           err;
    OMX_U32       nIndex;

    logd("(f:%s, l:%d) name = %s", __FUNCTION__, __LINE__, pName);
	strncpy((char*)m_cName, pName, OMX_MAX_STRINGNAME_SIZE);

    
    if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.mjpeg", OMX_MAX_STRINGNAME_SIZE))
	{
		strncpy((char *)m_cRole, "video_decoder.mjpeg", OMX_MAX_STRINGNAME_SIZE);
		m_eCompressionFormat      = OMX_VIDEO_CodingMJPEG;
		m_streamInfo.eCodecFormat = VIDEO_CODEC_FORMAT_MJPEG;
	}
    else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.mpeg1", OMX_MAX_STRINGNAME_SIZE))
	{
		strncpy((char *)m_cRole, "video_decoder.mpeg1", OMX_MAX_STRINGNAME_SIZE);
		m_eCompressionFormat      = OMX_VIDEO_CodingMPEG1;
        m_streamInfo.eCodecFormat = VIDEO_CODEC_FORMAT_MPEG1;
	}
    else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.mpeg2", OMX_MAX_STRINGNAME_SIZE))
	{
		strncpy((char *)m_cRole, "video_decoder.mpeg2", OMX_MAX_STRINGNAME_SIZE);
		m_eCompressionFormat      = OMX_VIDEO_CodingMPEG2;
        m_streamInfo.eCodecFormat = VIDEO_CODEC_FORMAT_MPEG2;
	}
	else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.mpeg4", OMX_MAX_STRINGNAME_SIZE))
	{
		strncpy((char*)m_cRole, "video_decoder.mpeg4", OMX_MAX_STRINGNAME_SIZE);
		m_eCompressionFormat      = OMX_VIDEO_CodingMPEG4;
		m_streamInfo.eCodecFormat = VIDEO_CODEC_FORMAT_XVID;
	}
    else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.msmpeg4v1", OMX_MAX_STRINGNAME_SIZE))
	{
		strncpy((char*)m_cRole, "video_decoder.msmpeg4v1", OMX_MAX_STRINGNAME_SIZE);
		m_eCompressionFormat      = OMX_VIDEO_CodingMSMPEG4V1;
		m_streamInfo.eCodecFormat = VIDEO_CODEC_FORMAT_MSMPEG4V1;
	}
    else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.msmpeg4v2", OMX_MAX_STRINGNAME_SIZE))
	{
		strncpy((char*)m_cRole, "video_decoder.msmpeg4v2", OMX_MAX_STRINGNAME_SIZE);
		m_eCompressionFormat      = OMX_VIDEO_CodingMSMPEG4V2;
		m_streamInfo.eCodecFormat = VIDEO_CODEC_FORMAT_MSMPEG4V2;
	}
    else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.divx", OMX_MAX_STRINGNAME_SIZE))
	{
		strncpy((char*)m_cRole, "video_decoder.divx", OMX_MAX_STRINGNAME_SIZE);
		m_eCompressionFormat      = OMX_VIDEO_CodingDIVX;
		m_streamInfo.eCodecFormat = VIDEO_CODEC_FORMAT_DIVX5;
	}
    else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.xvid", OMX_MAX_STRINGNAME_SIZE))
	{
		strncpy((char*)m_cRole, "video_decoder.xvid", OMX_MAX_STRINGNAME_SIZE);
		m_eCompressionFormat      = OMX_VIDEO_CodingXVID;
		m_streamInfo.eCodecFormat = VIDEO_CODEC_FORMAT_XVID;
	}
	else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.h263", OMX_MAX_STRINGNAME_SIZE))
	{
		strncpy((char *)m_cRole, "video_decoder.h263", OMX_MAX_STRINGNAME_SIZE);
		logi("\n H263 Decoder selected");
		m_eCompressionFormat      = OMX_VIDEO_CodingH263;
		m_streamInfo.eCodecFormat = VIDEO_CODEC_FORMAT_H263;
	}
    else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.s263", OMX_MAX_STRINGNAME_SIZE))
	{
		strncpy((char *)m_cRole, "video_decoder.s263", OMX_MAX_STRINGNAME_SIZE);
		logi("\n H263 Decoder selected");
		m_eCompressionFormat      = OMX_VIDEO_CodingS263;
		m_streamInfo.eCodecFormat = VIDEO_CODEC_FORMAT_SORENSSON_H263;
	}
    else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.rxg2", OMX_MAX_STRINGNAME_SIZE))
	{
		strncpy((char *)m_cRole, "video_decoder.rxg2", OMX_MAX_STRINGNAME_SIZE);
		logi("\n H263 Decoder selected");
		m_eCompressionFormat      = OMX_VIDEO_CodingRXG2;
		m_streamInfo.eCodecFormat = VIDEO_CODEC_FORMAT_RXG2;
	}
    else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.wmv1", OMX_MAX_STRINGNAME_SIZE))
	{
		strncpy((char *)m_cRole, "video_decoder.wmv1", OMX_MAX_STRINGNAME_SIZE);
		m_eCompressionFormat      = OMX_VIDEO_CodingWMV1;
		m_streamInfo.eCodecFormat = VIDEO_CODEC_FORMAT_WMV1;
        mIsSoftwareDecoderFlag    = OMX_TRUE;
	}
    else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.wmv2", OMX_MAX_STRINGNAME_SIZE))
	{
		strncpy((char *)m_cRole, "video_decoder.wmv2", OMX_MAX_STRINGNAME_SIZE);
		m_eCompressionFormat      = OMX_VIDEO_CodingWMV2;
		m_streamInfo.eCodecFormat = VIDEO_CODEC_FORMAT_WMV2;
        mIsSoftwareDecoderFlag    = OMX_TRUE;
	}
    else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.vc1", OMX_MAX_STRINGNAME_SIZE))
	{
		strncpy((char *)m_cRole, "video_decoder.vc1", OMX_MAX_STRINGNAME_SIZE);
		m_eCompressionFormat      = OMX_VIDEO_CodingWMV;
		m_streamInfo.eCodecFormat = VIDEO_CODEC_FORMAT_WMV3;
	}
    else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.vp6", OMX_MAX_STRINGNAME_SIZE))
	{
		strncpy((char *)m_cRole, "video_decoder.vp6", OMX_MAX_STRINGNAME_SIZE);
		m_eCompressionFormat      = OMX_VIDEO_CodingVP6; //OMX_VIDEO_CodingVPX
        m_streamInfo.eCodecFormat = VIDEO_CODEC_FORMAT_VP6;
        mIsSoftwareDecoderFlag    = OMX_TRUE;
	}
    else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.vp8", OMX_MAX_STRINGNAME_SIZE))
	{
		strncpy((char *)m_cRole, "video_decoder.vp8", OMX_MAX_STRINGNAME_SIZE);
		m_eCompressionFormat      = OMX_VIDEO_CodingVP8; //OMX_VIDEO_CodingVPX
        m_streamInfo.eCodecFormat = VIDEO_CODEC_FORMAT_VP8;
	}
    else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.vp9", OMX_MAX_STRINGNAME_SIZE))
	{
		strncpy((char *)m_cRole, "video_decoder.vp9", OMX_MAX_STRINGNAME_SIZE);
		m_eCompressionFormat      = OMX_VIDEO_CodingVP9; //OMX_VIDEO_CodingVPX
        m_streamInfo.eCodecFormat = VIDEO_CODEC_FORMAT_VP9;
        mVp9orH265SoftDecodeFlag  = OMX_TRUE;
        mIsSoftwareDecoderFlag    = OMX_TRUE;
	}
    else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.avc.secure", OMX_MAX_STRINGNAME_SIZE))
	{
		strncpy((char *)m_cRole, "video_decoder.avc.secure", OMX_MAX_STRINGNAME_SIZE);
		m_eCompressionFormat       = OMX_VIDEO_CodingAVC;
		m_streamInfo.eCodecFormat  = VIDEO_CODEC_FORMAT_H264;
        mIsSecureVideoFlag         = OMX_TRUE;
	}
    else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.avc", OMX_MAX_STRINGNAME_SIZE))
	{
		strncpy((char *)m_cRole, "video_decoder.avc", OMX_MAX_STRINGNAME_SIZE);
		m_eCompressionFormat      = OMX_VIDEO_CodingAVC;
		m_streamInfo.eCodecFormat = VIDEO_CODEC_FORMAT_H264;
	}
    else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.hevc", OMX_MAX_STRINGNAME_SIZE))
	{
		strncpy((char *)m_cRole, "video_decoder.hevc", OMX_MAX_STRINGNAME_SIZE);
		logi("\n H263 Decoder selected");
		m_eCompressionFormat      = OMX_VIDEO_CodingHEVC;
		m_streamInfo.eCodecFormat = VIDEO_CODEC_FORMAT_H265;
        mVp9orH265SoftDecodeFlag  = OMX_TRUE;
        mIsSoftwareDecoderFlag    = OMX_TRUE;
	}
	else
	{
		logi("\nERROR:Unknown Component\n");
	    eRet = OMX_ErrorInvalidComponentName;
	    return eRet;
	}

    // Initialize component data structures to default values
    OMX_CONF_INIT_STRUCT_PTR(&m_sPortParam, OMX_PORT_PARAM_TYPE);
    m_sPortParam.nPorts           = 0x2;
    m_sPortParam.nStartPortNumber = 0x0;

    // Initialize the video parameters for input port
    OMX_CONF_INIT_STRUCT_PTR(&m_sInPortDefType, OMX_PARAM_PORTDEFINITIONTYPE);
    m_sInPortDefType.nPortIndex 					 = 0x0;
    m_sInPortDefType.bEnabled 						 = OMX_TRUE;
    m_sInPortDefType.bPopulated 					 = OMX_FALSE;
    m_sInPortDefType.eDomain 						 = OMX_PortDomainVideo;
    m_sInPortDefType.format.video.nFrameWidth 		 = 0;
    m_sInPortDefType.format.video.nFrameHeight 		 = 0;
    m_sInPortDefType.eDir 							 = OMX_DirInput;
    m_sInPortDefType.format.video.eCompressionFormat = m_eCompressionFormat;
    m_sInPortDefType.format.video.cMIMEType 		 = (OMX_STRING)"";

    if(mIsSecureVideoFlag == OMX_TRUE)
    {
        m_sInPortDefType.nBufferCountMin 	= NUM_IN_BUFFERS_SECURE;
        m_sInPortDefType.nBufferCountActual = NUM_IN_BUFFERS_SECURE;
        m_sInPortDefType.nBufferSize        = OMX_VIDEO_DEC_INPUT_BUFFER_SIZE_SECURE;
    }
    else
    {
        m_sInPortDefType.nBufferCountMin 	= NUM_IN_BUFFERS;
        m_sInPortDefType.nBufferCountActual = NUM_IN_BUFFERS;
        m_sInPortDefType.nBufferSize        = OMX_VIDEO_DEC_INPUT_BUFFER_SIZE;
    }

    // Initialize the video parameters for output port
    OMX_CONF_INIT_STRUCT_PTR(&m_sOutPortDefType, OMX_PARAM_PORTDEFINITIONTYPE);
    m_sOutPortDefType.nPortIndex 				     = 0x1;
    m_sOutPortDefType.bEnabled 					     = OMX_TRUE;
    m_sOutPortDefType.bPopulated 				     = OMX_FALSE;
    m_sOutPortDefType.eDomain 					     = OMX_PortDomainVideo;
    m_sOutPortDefType.format.video.cMIMEType 	     = (OMX_STRING)"YUV420";
    m_sOutPortDefType.format.video.nFrameWidth 	     = 176;
    m_sOutPortDefType.format.video.nFrameHeight      = 144;
    m_sOutPortDefType.eDir 						     = OMX_DirOutput;
    m_sOutPortDefType.nBufferCountMin 			     = NUM_OUT_BUFFERS;
    m_sOutPortDefType.nBufferCountActual 		     = NUM_OUT_BUFFERS;
    m_sOutPortDefType.nBufferSize 				     = (OMX_U32)(m_sOutPortDefType.format.video.nFrameWidth*m_sOutPortDefType.format.video.nFrameHeight*3/2);
    m_sOutPortDefType.format.video.eColorFormat      = OMX_COLOR_FormatYUV420Planar;

    // Initialize the video compression format for input port
    OMX_CONF_INIT_STRUCT_PTR(&m_sInPortFormatType, OMX_VIDEO_PARAM_PORTFORMATTYPE);
    m_sInPortFormatType.nPortIndex         = 0x0;
    m_sInPortFormatType.nIndex             = 0x0;
    m_sInPortFormatType.eCompressionFormat = m_eCompressionFormat;

    // Initialize the compression format for output port
    OMX_CONF_INIT_STRUCT_PTR(&m_sOutPortFormatType, OMX_VIDEO_PARAM_PORTFORMATTYPE);
    m_sOutPortFormatType.nPortIndex        = 0x1;
    m_sOutPortFormatType.nIndex            = 0x0;
    m_sOutPortFormatType.eColorFormat      = OMX_COLOR_FormatYUV420Planar;

    OMX_CONF_INIT_STRUCT_PTR(&m_sPriorityMgmtType, OMX_PRIORITYMGMTTYPE);

    OMX_CONF_INIT_STRUCT_PTR(&m_sInBufSupplierType, OMX_PARAM_BUFFERSUPPLIERTYPE );
    m_sInBufSupplierType.nPortIndex  = 0x0;

    OMX_CONF_INIT_STRUCT_PTR(&m_sOutBufSupplierType, OMX_PARAM_BUFFERSUPPLIERTYPE );
    m_sOutBufSupplierType.nPortIndex = 0x1;

    // Initialize the input buffer list
    memset(&(m_sInBufList), 0x0, sizeof(BufferList));

    m_sInBufList.pBufArr = (OMX_BUFFERHEADERTYPE*)malloc(sizeof(OMX_BUFFERHEADERTYPE) * m_sInPortDefType.nBufferCountActual);
    if(m_sInBufList.pBufArr == NULL)
    {
    	eRet = OMX_ErrorInsufficientResources;
    	goto EXIT;
    }

    memset(m_sInBufList.pBufArr, 0,sizeof(OMX_BUFFERHEADERTYPE) * m_sInPortDefType.nBufferCountActual);
    for (nIndex = 0; nIndex < m_sInPortDefType.nBufferCountActual; nIndex++)
    {
        OMX_CONF_INIT_STRUCT_PTR (&m_sInBufList.pBufArr[nIndex], OMX_BUFFERHEADERTYPE);
    }


    m_sInBufList.pBufHdrList = (OMX_BUFFERHEADERTYPE**)malloc(sizeof(OMX_BUFFERHEADERTYPE*) * m_sInPortDefType.nBufferCountActual);
    if(m_sInBufList.pBufHdrList == NULL)
    {
    	eRet = OMX_ErrorInsufficientResources;
    	goto EXIT;
    }

    m_sInBufList.nSizeOfList       = 0;
    m_sInBufList.nAllocSize        = 0;
    m_sInBufList.nWritePos         = 0;
    m_sInBufList.nReadPos          = 0;
    m_sInBufList.nAllocBySelfFlags = 0;
    m_sInBufList.nSizeOfList       = 0;
    m_sInBufList.nBufArrSize       = m_sInPortDefType.nBufferCountActual;
    m_sInBufList.eDir              = OMX_DirInput;

    // Initialize the output buffer list
    memset(&m_sOutBufList, 0x0, sizeof(BufferList));

    m_sOutBufList.pBufArr = (OMX_BUFFERHEADERTYPE*)malloc(sizeof(OMX_BUFFERHEADERTYPE) * m_sOutPortDefType.nBufferCountActual);
    if(m_sOutBufList.pBufArr == NULL)
    {
    	eRet = OMX_ErrorInsufficientResources;
    	goto EXIT;
    }

    memset(m_sOutBufList.pBufArr, 0,sizeof(OMX_BUFFERHEADERTYPE) * m_sOutPortDefType.nBufferCountActual);
    for (nIndex = 0; nIndex < m_sOutPortDefType.nBufferCountActual; nIndex++)
    {
        OMX_CONF_INIT_STRUCT_PTR(&m_sOutBufList.pBufArr[nIndex], OMX_BUFFERHEADERTYPE);
    }

    m_sOutBufList.pBufHdrList = (OMX_BUFFERHEADERTYPE**)malloc(sizeof(OMX_BUFFERHEADERTYPE*) * m_sOutPortDefType.nBufferCountActual);
    if(m_sOutBufList.pBufHdrList == NULL)
    {
    	eRet = OMX_ErrorInsufficientResources;
    	goto EXIT;
    }

    m_sOutBufList.nSizeOfList       = 0;
    m_sOutBufList.nAllocSize        = 0;
    m_sOutBufList.nWritePos         = 0;
    m_sOutBufList.nReadPos          = 0;
    m_sOutBufList.nAllocBySelfFlags = 0;
    m_sOutBufList.nSizeOfList       = 0;
    m_sOutBufList.nBufArrSize       = m_sOutPortDefType.nBufferCountActual;
    m_sOutBufList.eDir              = OMX_DirOutput;

    // Create the pipe used to send commands to the thread
    err = pipe(m_cmdpipe);
    if (err)
    {
    	eRet = OMX_ErrorInsufficientResources;
        goto EXIT;
    }
    // Create the pipe used to send command to the vdrv_thread
    err = pipe(m_vdrv_cmdpipe);
    if (err)
    {
    	eRet = OMX_ErrorInsufficientResources;
        goto EXIT;
    }

    // Create the pipe used to send command data to the thread
    err = pipe(m_cmddatapipe);
    if (err)
    {
    	eRet = OMX_ErrorInsufficientResources;
        goto EXIT;
    }
    //* create a decoder.

    /*
    m_decoder = CreateVideoDecoder();
    if(m_decoder == NULL)
    {
    	logi(" can not create video decoder.");
    	eRet = OMX_ErrorInsufficientResources;
    	goto EXIT;
    }
    */
    
	//*set omx cts flag to flush the last frame in h264
	//m_decoder->ioctrl(m_decoder, CEDARV_COMMAND_SET_OMXCTS_DECODER, 1);
	
    // Create the component thread
    err = pthread_create(&m_thread_id, NULL, ComponentThread, this);
    if( err || !m_thread_id )
    {
    	eRet = OMX_ErrorInsufficientResources;
        goto EXIT;
    }
    
    // Create vdrv thread
    err = pthread_create(&m_vdrv_thread_id, NULL, ComponentVdrvThread, this);
    if( err || !m_vdrv_thread_id )
    {
    	eRet = OMX_ErrorInsufficientResources;
        goto EXIT;
    }

    mDecodeFrameTotalDuration           = 0;
    mDecodeOKTotalDuration              = 0;
    mDecodeNoFrameTotalDuration         = 0;
    mDecodeNoBitstreamTotalDuration     = 0;
    mDecodeOtherTotalDuration           = 0;
    mDecodeFrameTotalCount              = 0;
    mDecodeOKTotalCount                 = 0;
    mDecodeNoFrameTotalCount            = 0;
    mDecodeNoBitstreamTotalCount        = 0;
    mDecodeOtherTotalCount              = 0;
    mDecodeFrameSmallAverageDuration    = 0;
    mDecodeFrameBigAverageDuration      = 0;
    mDecodeNoFrameAverageDuration       = 0;
    mDecodeNoBitstreamAverageDuration   = 0;
    
    mConvertTotalDuration               = 0;
    mConvertTotalCount                  = 0;
    mConvertAverageDuration             = 0;
    
EXIT:
    return eRet;
}


OMX_ERRORTYPE  omx_vdec::get_component_version(OMX_IN OMX_HANDLETYPE pHComp,
                                               OMX_OUT OMX_STRING pComponentName,
                                               OMX_OUT OMX_VERSIONTYPE* pComponentVersion,
                                               OMX_OUT OMX_VERSIONTYPE* pSpecVersion,
                                               OMX_OUT OMX_UUIDTYPE* pComponentUUID)
{
	CEDARX_UNUSE(pComponentUUID);

    logi("(f:%s, l:%d) ", __FUNCTION__, __LINE__);
    if (!pHComp || !pComponentName || !pComponentVersion || !pSpecVersion)
    {
        return OMX_ErrorBadParameter;
    }

    strcpy((char*)pComponentName, (char*)m_cName);

    pComponentVersion->s.nVersionMajor = 1;
    pComponentVersion->s.nVersionMinor = 1;
    pComponentVersion->s.nRevision     = 0;
    pComponentVersion->s.nStep         = 0;

    pSpecVersion->s.nVersionMajor = 1;
    pSpecVersion->s.nVersionMinor = 1;
    pSpecVersion->s.nRevision     = 0;
    pSpecVersion->s.nStep         = 0;

	return OMX_ErrorNone;
}


OMX_ERRORTYPE  omx_vdec::send_command(OMX_IN OMX_HANDLETYPE  pHComp,
                                      OMX_IN OMX_COMMANDTYPE eCmd,
                                      OMX_IN OMX_U32         uParam1,
                                      OMX_IN OMX_PTR         pCmdData)
{
    ThrCmdType    eCmdNative;
    OMX_ERRORTYPE eError = OMX_ErrorNone;
    logi("(f:%s, l:%d) ", __FUNCTION__, __LINE__);

	CEDARX_UNUSE(pHComp);

    if(m_state == OMX_StateInvalid)
    {
    	logd("ERROR: Send Command in Invalid State\n");
        return OMX_ErrorInvalidState;
    }

    if (eCmd == OMX_CommandMarkBuffer && pCmdData == NULL)
    {
    	logd("ERROR: Send OMX_CommandMarkBuffer command but pCmdData invalid.");
    	return OMX_ErrorBadParameter;
    }

    switch (eCmd)
    {
        case OMX_CommandStateSet:
        	logi(" COMPONENT_SEND_COMMAND: OMX_CommandStateSet");
            eCmdNative = SetState;
	        break;

        case OMX_CommandFlush:
        	logi(" COMPONENT_SEND_COMMAND: OMX_CommandFlush");
	        eCmdNative = Flush;
	        if ((int)uParam1 > 1 && (int)uParam1 != -1)
	        {
	        	logd("Error: Send OMX_CommandFlush command but uParam1 invalid.");
	        	return OMX_ErrorBadPortIndex;
	        }
	        break;

        case OMX_CommandPortDisable:
        	logi(" COMPONENT_SEND_COMMAND: OMX_CommandPortDisable");
	        eCmdNative = StopPort;
	        break;

        case OMX_CommandPortEnable:
        	logi(" COMPONENT_SEND_COMMAND: OMX_CommandPortEnable");
	        eCmdNative = RestartPort;
	        break;

        case OMX_CommandMarkBuffer:
        	logi(" COMPONENT_SEND_COMMAND: OMX_CommandMarkBuffer");
	        eCmdNative = MarkBuf;
 	        if (uParam1 > 0)
	        {
	        	logd("Error: Send OMX_CommandMarkBuffer command but uParam1 invalid.");
	        	return OMX_ErrorBadPortIndex;
	        }
            break;
            
        default:
            logw("(f:%s, l:%d) ignore other command[0x%x]", __FUNCTION__, __LINE__, eCmd);
            return OMX_ErrorBadParameter;
    }
    
    post_event(eCmdNative, uParam1, pCmdData);

    return eError;
}


OMX_ERRORTYPE  omx_vdec::get_parameter(OMX_IN OMX_HANDLETYPE pHComp,
                                       OMX_IN OMX_INDEXTYPE  eParamIndex,
                                       OMX_INOUT OMX_PTR     pParamData)
{
    OMX_ERRORTYPE eError = OMX_ErrorNone;

	CEDARX_UNUSE(pHComp);

    logi("(f:%s, l:%d) eParamIndex = 0x%x", __FUNCTION__, __LINE__, eParamIndex);
    if(m_state == OMX_StateInvalid)
    {
    	logi("Get Param in Invalid State\n");
        return OMX_ErrorInvalidState;
    }

    if(pParamData == NULL)
    {
    	logi("Get Param in Invalid pParamData \n");
        return OMX_ErrorBadParameter;
    }
    
    switch(eParamIndex)
    {
    	case OMX_IndexParamVideoInit:
    	{
    		logi(" COMPONENT_GET_PARAMETER: OMX_IndexParamVideoInit");
	        memcpy(pParamData, &m_sPortParam, sizeof(OMX_PORT_PARAM_TYPE));
    		break;
    	}

    	case OMX_IndexParamPortDefinition:
    	{
    		logi(" COMPONENT_GET_PARAMETER: OMX_IndexParamPortDefinition");
	        if (((OMX_PARAM_PORTDEFINITIONTYPE *)(pParamData))->nPortIndex == m_sInPortDefType.nPortIndex)
	        {
	            memcpy(pParamData, &m_sInPortDefType, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
                logi(" get_OMX_IndexParamPortDefinition: m_sInPortDefType.nPortIndex[%d]", (int)m_sInPortDefType.nPortIndex);
	        }
	        else if (((OMX_PARAM_PORTDEFINITIONTYPE*)(pParamData))->nPortIndex == m_sOutPortDefType.nPortIndex)
	        {
	            memcpy(pParamData, &m_sOutPortDefType, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
	            logi("(omx_vdec, f:%s, l:%d) OMX_IndexParamPortDefinition, width = %d, height = %d, nPortIndex[%d], nBufferCountActual[%d], nBufferCountMin[%d], nBufferSize[%d]", __FUNCTION__, __LINE__, 
                    (int)m_sOutPortDefType.format.video.nFrameWidth, (int)m_sOutPortDefType.format.video.nFrameHeight,
                    (int)m_sOutPortDefType.nPortIndex, (int)m_sOutPortDefType.nBufferCountActual, (int)m_sOutPortDefType.nBufferCountMin, (int)m_sOutPortDefType.nBufferSize);
	        }
	        else
	        {
	            eError = OMX_ErrorBadPortIndex;
                logw(" get_OMX_IndexParamPortDefinition: error. pParamData->nPortIndex=[%d]", (int)((OMX_PARAM_PORTDEFINITIONTYPE*)(pParamData))->nPortIndex);
	        }

    		break;
    	}

    	case OMX_IndexParamVideoPortFormat:
    	{
    		logi(" COMPONENT_GET_PARAMETER: OMX_IndexParamVideoPortFormat");
 	        if (((OMX_VIDEO_PARAM_PORTFORMATTYPE *)(pParamData))->nPortIndex == m_sInPortFormatType.nPortIndex)
 	        {
 	            if (((OMX_VIDEO_PARAM_PORTFORMATTYPE*)(pParamData))->nIndex > m_sInPortFormatType.nIndex)
		            eError = OMX_ErrorNoMore;
		        else
		            memcpy(pParamData, &m_sInPortFormatType, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
		    }
	        else if (((OMX_VIDEO_PARAM_PORTFORMATTYPE*)(pParamData))->nPortIndex == m_sOutPortFormatType.nPortIndex)
	        {
	            if (((OMX_VIDEO_PARAM_PORTFORMATTYPE*)(pParamData))->nIndex > m_sOutPortFormatType.nIndex)
		            eError = OMX_ErrorNoMore;
		        else
		            memcpy(pParamData, &m_sOutPortFormatType, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
		    }
		    else
		        eError = OMX_ErrorBadPortIndex;

            logi("OMX_IndexParamVideoPortFormat, eError[0x%x]", eError);
	        break;
    	}

    	case OMX_IndexParamStandardComponentRole:
    	{
    		logi(" COMPONENT_GET_PARAMETER: OMX_IndexParamStandardComponentRole");
    		OMX_PARAM_COMPONENTROLETYPE* comp_role;

    		comp_role                    = (OMX_PARAM_COMPONENTROLETYPE *) pParamData;
    		comp_role->nVersion.nVersion = OMX_SPEC_VERSION;
    		comp_role->nSize             = sizeof(*comp_role);

    		strncpy((char*)comp_role->cRole, (const char*)m_cRole, OMX_MAX_STRINGNAME_SIZE);
    		break;
    	}

    	case OMX_IndexParamPriorityMgmt:
    	{
    		logi(" COMPONENT_GET_PARAMETER: OMX_IndexParamPriorityMgmt");
	        memcpy(pParamData, &m_sPriorityMgmtType, sizeof(OMX_PRIORITYMGMTTYPE));
    		break;
    	}

    	case OMX_IndexParamCompBufferSupplier:
    	{
    		logi(" COMPONENT_GET_PARAMETER: OMX_IndexParamCompBufferSupplier");
            OMX_PARAM_BUFFERSUPPLIERTYPE* pBuffSupplierParam = (OMX_PARAM_BUFFERSUPPLIERTYPE*)pParamData;

            if (pBuffSupplierParam->nPortIndex == 1)
            {
                pBuffSupplierParam->eBufferSupplier = m_sOutBufSupplierType.eBufferSupplier;
            }
            else if (pBuffSupplierParam->nPortIndex == 0)
            {
                pBuffSupplierParam->eBufferSupplier = m_sInBufSupplierType.eBufferSupplier;
            }
            else
            {
                eError = OMX_ErrorBadPortIndex;
            }

    		break;
    	}

    	case OMX_IndexParamAudioInit:
    	{
    		logi(" COMPONENT_GET_PARAMETER: OMX_IndexParamAudioInit");
    		OMX_PORT_PARAM_TYPE *audioPortParamType = (OMX_PORT_PARAM_TYPE *) pParamData;

    		audioPortParamType->nVersion.nVersion = OMX_SPEC_VERSION;
    		audioPortParamType->nSize             = sizeof(OMX_PORT_PARAM_TYPE);
    		audioPortParamType->nPorts            = 0;
    		audioPortParamType->nStartPortNumber  = 0;

    		break;
    	}

    	case OMX_IndexParamImageInit:
    	{
    		logi(" COMPONENT_GET_PARAMETER: OMX_IndexParamImageInit");
    		OMX_PORT_PARAM_TYPE *imagePortParamType = (OMX_PORT_PARAM_TYPE *) pParamData;

    		imagePortParamType->nVersion.nVersion = OMX_SPEC_VERSION;
    		imagePortParamType->nSize             = sizeof(OMX_PORT_PARAM_TYPE);
    		imagePortParamType->nPorts            = 0;
    		imagePortParamType->nStartPortNumber  = 0;

    		break;
    	}

    	case OMX_IndexParamOtherInit:
    	{
    		logi(" COMPONENT_GET_PARAMETER: OMX_IndexParamOtherInit");
    		OMX_PORT_PARAM_TYPE *otherPortParamType = (OMX_PORT_PARAM_TYPE *) pParamData;

    		otherPortParamType->nVersion.nVersion = OMX_SPEC_VERSION;
    		otherPortParamType->nSize             = sizeof(OMX_PORT_PARAM_TYPE);
    		otherPortParamType->nPorts            = 0;
    		otherPortParamType->nStartPortNumber  = 0;

    		break;
    	}

    	case OMX_IndexParamVideoAvc:
    	{
    		logi(" COMPONENT_GET_PARAMETER: OMX_IndexParamVideoAvc");
        	logi("get_parameter: OMX_IndexParamVideoAvc, do nothing.\n");
            break;
    	}

    	case OMX_IndexParamVideoH263:
    	{
    		logi(" COMPONENT_GET_PARAMETER: OMX_IndexParamVideoH263");
        	logi("get_parameter: OMX_IndexParamVideoH263, do nothing.\n");
            break;
    	}

    	case OMX_IndexParamVideoMpeg4:
    	{
    		logi(" COMPONENT_GET_PARAMETER: OMX_IndexParamVideoMpeg4");
        	logi("get_parameter: OMX_IndexParamVideoMpeg4, do nothing.\n");
            break;
    	}
        case OMX_IndexParamVideoProfileLevelQuerySupported:
        {
        	VIDEO_PROFILE_LEVEL_TYPE* pProfileLevel = NULL;
        	OMX_U32 nNumberOfProfiles = 0;
        	OMX_VIDEO_PARAM_PROFILELEVELTYPE *pParamProfileLevel = (OMX_VIDEO_PARAM_PROFILELEVELTYPE *)pParamData;

        	pParamProfileLevel->nPortIndex = m_sInPortDefType.nPortIndex;

        	/* Choose table based on compression format */
        	switch(m_sInPortDefType.format.video.eCompressionFormat)
        	{
        	case OMX_VIDEO_CodingH263:
        		pProfileLevel = SupportedH263ProfileLevels;
        		nNumberOfProfiles = sizeof(SupportedH263ProfileLevels) / sizeof (VIDEO_PROFILE_LEVEL_TYPE);
        		break;

        	case OMX_VIDEO_CodingMPEG4:
        		pProfileLevel = SupportedMPEG4ProfileLevels;
        		nNumberOfProfiles = sizeof(SupportedMPEG4ProfileLevels) / sizeof (VIDEO_PROFILE_LEVEL_TYPE);
        		break;

        	case OMX_VIDEO_CodingAVC:
        		pProfileLevel = SupportedAVCProfileLevels;
        		nNumberOfProfiles = sizeof(SupportedAVCProfileLevels) / sizeof (VIDEO_PROFILE_LEVEL_TYPE);
        		break;

        	default:
                logw("OMX_IndexParamVideoProfileLevelQuerySupported, Format[0x%x] not support", m_sInPortDefType.format.video.eCompressionFormat);
        		return OMX_ErrorBadParameter;
        	}

        	if(((int)pParamProfileLevel->nProfileIndex < 0) || (pParamProfileLevel->nProfileIndex >= (nNumberOfProfiles - 1)))
        	{
                logw("pParamProfileLevel->nProfileIndex[0x%x] error!", (unsigned int)pParamProfileLevel->nProfileIndex);
        		return OMX_ErrorBadParameter;
        	}

        	/* Point to table entry based on index */
        	pProfileLevel += pParamProfileLevel->nProfileIndex;

        	/* -1 indicates end of table */
        	if(pProfileLevel->nProfile != -1)
        	{
        		pParamProfileLevel->eProfile = pProfileLevel->nProfile;
        		pParamProfileLevel->eLevel = pProfileLevel->nLevel;
        		eError = OMX_ErrorNone;
        	}
        	else
        	{
                logw("pProfileLevel->nProfile error!");
        		eError = OMX_ErrorNoMore;
        	}

        	break;
        }
    	default:
    	{
    		if((AW_VIDEO_EXTENSIONS_INDEXTYPE)eParamIndex == AWOMX_IndexParamVideoGetAndroidNativeBufferUsage)
    		{
        		logi(" COMPONENT_GET_PARAMETER: AWOMX_IndexParamVideoGetAndroidNativeBufferUsage");
                break;
    		}
    		else
    		{
        		logi("get_parameter: unknown param %08x\n", eParamIndex);
        		eError =OMX_ErrorUnsupportedIndex;
        		break;
    		}
    	}
    }

    return eError;
}

OMX_ERRORTYPE  omx_vdec::set_parameter(OMX_IN OMX_HANDLETYPE pHComp, OMX_IN OMX_INDEXTYPE eParamIndex,  OMX_IN OMX_PTR pParamData)
{
    OMX_ERRORTYPE         eError      = OMX_ErrorNone;
    unsigned int          alignment   = 0;
    unsigned int          buffer_size = 0;
    int                   i;

	CEDARX_UNUSE(pHComp);

    logi("(f:%s, l:%d) eParamIndex = 0x%x", __FUNCTION__, __LINE__, eParamIndex);
    if(m_state == OMX_StateInvalid)
    {
        logi("Set Param in Invalid State\n");
        return OMX_ErrorInvalidState;
    }

    if(pParamData == NULL)
    {
    	logi("Get Param in Invalid pParamData \n");
    	return OMX_ErrorBadParameter;
    }

    switch(eParamIndex)
    {
	    case OMX_IndexParamPortDefinition:
	    {
	    	logi(" COMPONENT_SET_PARAMETER: OMX_IndexParamPortDefinition");
            if (((OMX_PARAM_PORTDEFINITIONTYPE *)(pParamData))->nPortIndex == m_sInPortDefType.nPortIndex)
            {
                logi("set_OMX_IndexParamPortDefinition, m_sInPortDefType.nPortIndex=%d", (int)m_sInPortDefType.nPortIndex);
	        	if(((OMX_PARAM_PORTDEFINITIONTYPE *)(pParamData))->nBufferCountActual != m_sInPortDefType.nBufferCountActual)
	        	{
	        		int nBufCnt;
	        		int nIndex;


	        		pthread_mutex_lock(&m_inBufMutex);

	        		if(m_sInBufList.pBufArr != NULL)
	        			free(m_sInBufList.pBufArr);

	        		if(m_sInBufList.pBufHdrList != NULL)
	        			free(m_sInBufList.pBufHdrList);

	        		nBufCnt = ((OMX_PARAM_PORTDEFINITIONTYPE *)(pParamData))->nBufferCountActual;
	        		logi("x allocate %d buffers.", nBufCnt);

	        	    m_sInBufList.pBufArr = (OMX_BUFFERHEADERTYPE*)malloc(sizeof(OMX_BUFFERHEADERTYPE)* nBufCnt);
	        	    m_sInBufList.pBufHdrList = (OMX_BUFFERHEADERTYPE**)malloc(sizeof(OMX_BUFFERHEADERTYPE*)* nBufCnt);
	        	    for (nIndex = 0; nIndex < nBufCnt; nIndex++)
	        	    {
	        	        OMX_CONF_INIT_STRUCT_PTR (&m_sInBufList.pBufArr[nIndex], OMX_BUFFERHEADERTYPE);
	        	    }

	        	    m_sInBufList.nSizeOfList       = 0;
	        	    m_sInBufList.nAllocSize        = 0;
	        	    m_sInBufList.nWritePos         = 0;
	        	    m_sInBufList.nReadPos          = 0;
	        	    m_sInBufList.nAllocBySelfFlags = 0;
	        	    m_sInBufList.nSizeOfList       = 0;
	        	    m_sInBufList.nBufArrSize       = nBufCnt;
	        	    m_sInBufList.eDir              = OMX_DirInput;

	        		pthread_mutex_unlock(&m_inBufMutex);
	        	}

	            memcpy(&m_sInPortDefType, pParamData, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));

                m_streamInfo.nWidth  = m_sInPortDefType.format.video.nFrameWidth;
                m_streamInfo.nHeight = m_sInPortDefType.format.video.nFrameHeight;
            }
	        else if (((OMX_PARAM_PORTDEFINITIONTYPE *)(pParamData))->nPortIndex == m_sOutPortDefType.nPortIndex)
	        {
                logi("set_OMX_IndexParamPortDefinition, m_sOutPortDefType.nPortIndex=%d", (int)m_sOutPortDefType.nPortIndex);
	        	if(((OMX_PARAM_PORTDEFINITIONTYPE *)(pParamData))->nBufferCountActual != m_sOutPortDefType.nBufferCountActual)
	        	{
	        		int nBufCnt;
	        		int nIndex;

	        		pthread_mutex_lock(&m_outBufMutex);

	        		if(m_sOutBufList.pBufArr != NULL)
	        			free(m_sOutBufList.pBufArr);

	        		if(m_sOutBufList.pBufHdrList != NULL)
	        			free(m_sOutBufList.pBufHdrList);

	        		nBufCnt = ((OMX_PARAM_PORTDEFINITIONTYPE *)(pParamData))->nBufferCountActual;
	        		logi("x allocate %d buffers.", nBufCnt);

                    //*Initialize the output buffer list
	        	    m_sOutBufList.pBufArr = (OMX_BUFFERHEADERTYPE*) malloc(sizeof(OMX_BUFFERHEADERTYPE) * nBufCnt);
	        	    m_sOutBufList.pBufHdrList = (OMX_BUFFERHEADERTYPE**) malloc(sizeof(OMX_BUFFERHEADERTYPE*) * nBufCnt);
	        	    for (nIndex = 0; nIndex < nBufCnt; nIndex++)
	        	    {
	        	        OMX_CONF_INIT_STRUCT_PTR (&m_sOutBufList.pBufArr[nIndex], OMX_BUFFERHEADERTYPE);
	        	    }

	        	    m_sOutBufList.nSizeOfList       = 0;
	        	    m_sOutBufList.nAllocSize        = 0;
	        	    m_sOutBufList.nWritePos         = 0;
	        	    m_sOutBufList.nReadPos          = 0;
	        	    m_sOutBufList.nAllocBySelfFlags = 0;
	        	    m_sOutBufList.nSizeOfList       = 0;
	        	    m_sOutBufList.nBufArrSize       = nBufCnt;
	        	    m_sOutBufList.eDir              = OMX_DirOutput;

                    //* compute the extra outbuffer num needed by nativeWindow
                    if(mExtraOutBufferNum == 0)
                    {
                        mExtraOutBufferNum = ((OMX_PARAM_PORTDEFINITIONTYPE *)(pParamData))->nBufferCountActual - 
                                             m_sOutPortDefType.nBufferCountActual;

                    }
	        		pthread_mutex_unlock(&m_outBufMutex);
	        	}

                //* we post the sem if ComponentVdrvThread wait for m_semExtraOutBufferNum
                if(mExtraOutBufferNum != 0)
                {
                    int nSemVal;
                    int nRetSemGetValue;
                    nRetSemGetValue= sem_getvalue(&m_semExtraOutBufferNum, &nSemVal);
                    if(0 == nRetSemGetValue)
                    {
                        if(0 == nSemVal)
                        {
                            logd("sem_post m_semExtraOutBufferNum, num = %d",mExtraOutBufferNum);
                            sem_post(&m_semExtraOutBufferNum);
                        }
                        else
                        {
                            logd("the semVal[%d] of m_semExtraOutBufferNum is not 0",
                                 nSemVal);
                        }
                    }   
                }

	            memcpy(&m_sOutPortDefType, pParamData, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
                mVideoRect.nLeft   = 0;
                mVideoRect.nTop    = 0;
                mVideoRect.nWidth  = m_sOutPortDefType.format.video.nFrameWidth;
                mVideoRect.nHeight = m_sOutPortDefType.format.video.nFrameHeight;
                m_sOutPortDefType.format.video.nStride      = m_sOutPortDefType.format.video.nFrameWidth;
                m_sOutPortDefType.format.video.nSliceHeight = m_sOutPortDefType.format.video.nFrameHeight;
                //check some key parameter
                if(m_sOutPortDefType.format.video.nFrameWidth * m_sOutPortDefType.format.video.nFrameHeight * 3 / 2 != m_sOutPortDefType.nBufferSize)
                {
					logw("set_parameter, OMX_IndexParamPortDefinition, OutPortDef : change nBufferSize[%d] to [%d] to suit frame width[%d] and height[%d]",
                        (int)m_sOutPortDefType.nBufferSize, 
                        (int)(m_sOutPortDefType.format.video.nFrameWidth * m_sOutPortDefType.format.video.nFrameHeight * 3 / 2), 
                        (int)m_sOutPortDefType.format.video.nFrameWidth, 
                        (int)m_sOutPortDefType.format.video.nFrameHeight);
                    m_sOutPortDefType.nBufferSize = m_sOutPortDefType.format.video.nFrameWidth * m_sOutPortDefType.format.video.nFrameHeight * 3 / 2;
                }

                logd("(omx_vdec, f:%s, l:%d) OMX_IndexParamPortDefinition, width = %d, height = %d, nPortIndex[%d], nBufferCountActual[%d], nBufferCountMin[%d], nBufferSize[%d],AlloSize[%d]", __FUNCTION__, __LINE__, 
                    (int)m_sOutPortDefType.format.video.nFrameWidth, (int)m_sOutPortDefType.format.video.nFrameHeight,
                    (int)m_sOutPortDefType.nPortIndex, (int)m_sOutPortDefType.nBufferCountActual, (int)m_sOutPortDefType.nBufferCountMin, (int)m_sOutPortDefType.nBufferSize,
                    (int)m_sOutBufList.nAllocSize);
	        }
	        else
	        {
                logw("set_OMX_IndexParamPortDefinition, error, paramPortIndex=%d", (int)((OMX_PARAM_PORTDEFINITIONTYPE *)(pParamData))->nPortIndex);
	            eError = OMX_ErrorBadPortIndex;
	        }

	       break;
	    }

	    case OMX_IndexParamVideoPortFormat:
	    {
	    	logi(" COMPONENT_SET_PARAMETER: OMX_IndexParamVideoPortFormat");

	        if (((OMX_VIDEO_PARAM_PORTFORMATTYPE *)(pParamData))->nPortIndex == m_sInPortFormatType.nPortIndex)
	        {
	            if (((OMX_VIDEO_PARAM_PORTFORMATTYPE *)(pParamData))->nIndex > m_sInPortFormatType.nIndex)
		            eError = OMX_ErrorNoMore;
		        else
		            memcpy(&m_sInPortFormatType, pParamData, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
	        }
	        else if (((OMX_VIDEO_PARAM_PORTFORMATTYPE*)(pParamData))->nPortIndex == m_sOutPortFormatType.nPortIndex)
	        {
	            if (((OMX_VIDEO_PARAM_PORTFORMATTYPE*)(pParamData))->nIndex > m_sOutPortFormatType.nIndex)
		            eError = OMX_ErrorNoMore;
		        else
		            memcpy(&m_sOutPortFormatType, pParamData, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
	        }
	        else
	            eError = OMX_ErrorBadPortIndex;
	        break;
	    }

	    case OMX_IndexParamStandardComponentRole:
	    {
	    	logi(" COMPONENT_SET_PARAMETER: OMX_IndexParamStandardComponentRole");
    		OMX_PARAM_COMPONENTROLETYPE *comp_role;
    		comp_role = (OMX_PARAM_COMPONENTROLETYPE *) pParamData;
    		logi("set_parameter: OMX_IndexParamStandardComponentRole %s\n", comp_role->cRole);

    		if((m_state == OMX_StateLoaded)/* && !BITMASK_PRESENT(&m_flags,OMX_COMPONENT_IDLE_PENDING)*/)
    		{
    			logi("Set Parameter called in valid state");
    		}
    		else
    		{
    			logi("Set Parameter called in Invalid State\n");
    			return OMX_ErrorIncorrectStateOperation;
    		}

            if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.mjpeg", OMX_MAX_STRINGNAME_SIZE))
    		{
    			if(!strncmp((const char*)comp_role->cRole,"video_decoder.mjpeg",OMX_MAX_STRINGNAME_SIZE))
    			{
    				strncpy((char*)m_cRole,"video_decoder.mjpeg",OMX_MAX_STRINGNAME_SIZE);
    			}
    			else
    			{
    				logi("Setparameter: unknown Index %s\n", comp_role->cRole);
    				eError =OMX_ErrorUnsupportedSetting;
    			}
    		}
            else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.mpeg1", OMX_MAX_STRINGNAME_SIZE))
    		{
    			if(!strncmp((char*)comp_role->cRole, "video_decoder.mpeg1", OMX_MAX_STRINGNAME_SIZE))
    			{
    				strncpy((char*)m_cRole,"video_decoder.mpeg1", OMX_MAX_STRINGNAME_SIZE);
    			}
    			else
    			{
                  	logi("Setparameter: unknown Index %s\n", comp_role->cRole);
                  	eError =OMX_ErrorUnsupportedSetting;
    			}
    		}
            else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.mpeg2", OMX_MAX_STRINGNAME_SIZE))
    		{
    			if(!strncmp((char*)comp_role->cRole, "video_decoder.mpeg2", OMX_MAX_STRINGNAME_SIZE))
    			{
    				strncpy((char*)m_cRole,"video_decoder.mpeg2", OMX_MAX_STRINGNAME_SIZE);
    			}
    			else
    			{
                  	logi("Setparameter: unknown Index %s\n", comp_role->cRole);
                  	eError =OMX_ErrorUnsupportedSetting;
    			}
    		}
    		else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.mpeg4", OMX_MAX_STRINGNAME_SIZE))
    		{
    			if(!strncmp((const char*)comp_role->cRole,"video_decoder.mpeg4",OMX_MAX_STRINGNAME_SIZE))
    			{
    				strncpy((char*)m_cRole,"video_decoder.mpeg4",OMX_MAX_STRINGNAME_SIZE);
    			}
    			else
    			{
    				logi("Setparameter: unknown Index %s\n", comp_role->cRole);
    				eError = OMX_ErrorUnsupportedSetting;
    			}
    		}
            else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.msmpeg4v1", OMX_MAX_STRINGNAME_SIZE))
    		{
    			if(!strncmp((const char*)comp_role->cRole,"video_decoder.msmpeg4v1",OMX_MAX_STRINGNAME_SIZE))
    			{
    				strncpy((char*)m_cRole,"video_decoder.msmpeg4v1",OMX_MAX_STRINGNAME_SIZE);
    			}
    			else
    			{
    				logi("Setparameter: unknown Index %s\n", comp_role->cRole);
    				eError = OMX_ErrorUnsupportedSetting;
    			}
    		}
            else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.msmpeg4v2", OMX_MAX_STRINGNAME_SIZE))
    		{
    			if(!strncmp((const char*)comp_role->cRole,"video_decoder.msmpeg4v2",OMX_MAX_STRINGNAME_SIZE))
    			{
    				strncpy((char*)m_cRole,"video_decoder.msmpeg4v2",OMX_MAX_STRINGNAME_SIZE);
    			}
    			else
    			{
    				logi("Setparameter: unknown Index %s\n", comp_role->cRole);
    				eError = OMX_ErrorUnsupportedSetting;
    			}
    		}
            else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.divx", OMX_MAX_STRINGNAME_SIZE))
    		{
    			if(!strncmp((const char*)comp_role->cRole,"video_decoder.divx",OMX_MAX_STRINGNAME_SIZE))
    			{
    				strncpy((char*)m_cRole,"video_decoder.divx",OMX_MAX_STRINGNAME_SIZE);
    			}
    			else
    			{
    				logi("Setparameter: unknown Index %s\n", comp_role->cRole);
    				eError = OMX_ErrorUnsupportedSetting;
    			}
    		}
            else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.xvid", OMX_MAX_STRINGNAME_SIZE))
    		{
    			if(!strncmp((const char*)comp_role->cRole,"video_decoder.xvid",OMX_MAX_STRINGNAME_SIZE))
    			{
    				strncpy((char*)m_cRole,"video_decoder.xvid",OMX_MAX_STRINGNAME_SIZE);
    			}
    			else
    			{
    				logi("Setparameter: unknown Index %s\n", comp_role->cRole);
    				eError = OMX_ErrorUnsupportedSetting;
    			}
    		}
    		else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.h263", OMX_MAX_STRINGNAME_SIZE))
    		{
    			if(!strncmp((const char*)comp_role->cRole,"video_decoder.h263", OMX_MAX_STRINGNAME_SIZE))
    			{
    				strncpy((char*)m_cRole,"video_decoder.h263", OMX_MAX_STRINGNAME_SIZE);
    			}
    			else
    			{
    				logi("Setparameter: unknown Index %s\n", comp_role->cRole);
    				eError =OMX_ErrorUnsupportedSetting;
    			}
    		}
            else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.s263", OMX_MAX_STRINGNAME_SIZE))
    		{
    			if(!strncmp((const char*)comp_role->cRole,"video_decoder.s263", OMX_MAX_STRINGNAME_SIZE))
    			{
    				strncpy((char*)m_cRole,"video_decoder.s263", OMX_MAX_STRINGNAME_SIZE);
    			}
    			else
    			{
    				logi("Setparameter: unknown Index %s\n", comp_role->cRole);
    				eError =OMX_ErrorUnsupportedSetting;
    			}
    		}
            else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.rxg2", OMX_MAX_STRINGNAME_SIZE))
    		{
    			if(!strncmp((const char*)comp_role->cRole,"video_decoder.rxg2", OMX_MAX_STRINGNAME_SIZE))
    			{
    				strncpy((char*)m_cRole,"video_decoder.rxg2", OMX_MAX_STRINGNAME_SIZE);
    			}
    			else
    			{
    				logi("Setparameter: unknown Index %s\n", comp_role->cRole);
    				eError =OMX_ErrorUnsupportedSetting;
    			}
    		}
            else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.wmv1", OMX_MAX_STRINGNAME_SIZE))
    		{
    			if(!strncmp((const char*)comp_role->cRole,"video_decoder.wmv1",OMX_MAX_STRINGNAME_SIZE))
    			{
    				strncpy((char*)m_cRole,"video_decoder.wmv1",OMX_MAX_STRINGNAME_SIZE);
    			}
    			else
    			{
    				logi("Setparameter: unknown Index %s\n", comp_role->cRole);
    				eError =OMX_ErrorUnsupportedSetting;
    			}
    		}
            else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.wmv2", OMX_MAX_STRINGNAME_SIZE))
    		{
    			if(!strncmp((const char*)comp_role->cRole,"video_decoder.wmv2",OMX_MAX_STRINGNAME_SIZE))
    			{
    				strncpy((char*)m_cRole,"video_decoder.wmv2",OMX_MAX_STRINGNAME_SIZE);
    			}
    			else
    			{
    				logi("Setparameter: unknown Index %s\n", comp_role->cRole);
    				eError =OMX_ErrorUnsupportedSetting;
    			}
    		}
            else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.vc1", OMX_MAX_STRINGNAME_SIZE))
    		{
    			if(!strncmp((const char*)comp_role->cRole,"video_decoder.vc1",OMX_MAX_STRINGNAME_SIZE))
    			{
    				strncpy((char*)m_cRole,"video_decoder.vc1",OMX_MAX_STRINGNAME_SIZE);
    			}
    			else
    			{
    				logi("Setparameter: unknown Index %s\n", comp_role->cRole);
    				eError =OMX_ErrorUnsupportedSetting;
    			}
    		}
            else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.vp6", OMX_MAX_STRINGNAME_SIZE))
    		{
    			if(!strncmp((char*)comp_role->cRole, "video_decoder.vp6", OMX_MAX_STRINGNAME_SIZE))
    			{
    				strncpy((char*)m_cRole,"video_decoder.vp6", OMX_MAX_STRINGNAME_SIZE);
    			}
    			else
    			{
                  	logi("Setparameter: unknown Index %s\n", comp_role->cRole);
                  	eError =OMX_ErrorUnsupportedSetting;
    			}
    		}
            else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.vp8", OMX_MAX_STRINGNAME_SIZE))
    		{
    			if(!strncmp((char*)comp_role->cRole, "video_decoder.vp8", OMX_MAX_STRINGNAME_SIZE))
    			{
    				strncpy((char*)m_cRole,"video_decoder.vp8", OMX_MAX_STRINGNAME_SIZE);
    			}
    			else
    			{
                  	logi("Setparameter: unknown Index %s\n", comp_role->cRole);
                  	eError =OMX_ErrorUnsupportedSetting;
    			}
    		}
            else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.vp9", OMX_MAX_STRINGNAME_SIZE))
    		{
    			if(!strncmp((char*)comp_role->cRole, "video_decoder.vp9", OMX_MAX_STRINGNAME_SIZE))
    			{
    				strncpy((char*)m_cRole,"video_decoder.vp9", OMX_MAX_STRINGNAME_SIZE);
    			}
    			else
    			{
                  	logi("Setparameter: unknown Index %s\n", comp_role->cRole);
                  	eError =OMX_ErrorUnsupportedSetting;
    			}
    		}
            else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.avc", OMX_MAX_STRINGNAME_SIZE))
    		{
    			if(!strncmp((char*)comp_role->cRole, "video_decoder.avc", OMX_MAX_STRINGNAME_SIZE))
    			{
    				strncpy((char*)m_cRole,"video_decoder.avc", OMX_MAX_STRINGNAME_SIZE);
    			}
    			else
    			{
                  	logi("Setparameter: unknown Index %s\n", comp_role->cRole);
                  	eError =OMX_ErrorUnsupportedSetting;
    			}
    		}
            else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.avc.secure", OMX_MAX_STRINGNAME_SIZE))
    		{
    			if(!strncmp((char*)comp_role->cRole, "video_decoder.avc", OMX_MAX_STRINGNAME_SIZE))
    			{
    				strncpy((char*)m_cRole,"video_decoder.avc", OMX_MAX_STRINGNAME_SIZE);
    			}
    			else
    			{
                  	logi("Setparameter: unknown Index %s\n", comp_role->cRole);
                  	eError =OMX_ErrorUnsupportedSetting;
    			}
    		}
            else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.hevc", OMX_MAX_STRINGNAME_SIZE))
    		{
    			if(!strncmp((const char*)comp_role->cRole,"video_decoder.hevc", OMX_MAX_STRINGNAME_SIZE))
    			{
    				strncpy((char*)m_cRole,"video_decoder.hevc", OMX_MAX_STRINGNAME_SIZE);
    			}
    			else
    			{
    				logi("Setparameter: unknown Index %s\n", comp_role->cRole);
    				eError =OMX_ErrorUnsupportedSetting;
    			}
    		}
    		else
    		{
    			logi("Setparameter: unknown param %s\n", m_cName);
    			eError = OMX_ErrorInvalidComponentName;
    		}

    		break;
	    }

	    case OMX_IndexParamPriorityMgmt:
	    {
	    	logi(" COMPONENT_SET_PARAMETER: OMX_IndexParamPriorityMgmt");
            if(m_state != OMX_StateLoaded)
            {
            	logi("Set Parameter called in Invalid State\n");
            	return OMX_ErrorIncorrectStateOperation;
            }

            OMX_PRIORITYMGMTTYPE *priorityMgmtype = (OMX_PRIORITYMGMTTYPE*) pParamData;

            m_sPriorityMgmtType.nGroupID = priorityMgmtype->nGroupID;
            m_sPriorityMgmtType.nGroupPriority = priorityMgmtype->nGroupPriority;

            break;
	    }

	    case OMX_IndexParamCompBufferSupplier:
	    {
	    	logi(" COMPONENT_SET_PARAMETER: OMX_IndexParamCompBufferSupplier");
    		OMX_PARAM_BUFFERSUPPLIERTYPE *bufferSupplierType = (OMX_PARAM_BUFFERSUPPLIERTYPE*) pParamData;

            logi("set_parameter: OMX_IndexParamCompBufferSupplier %d\n", bufferSupplierType->eBufferSupplier);
            if(bufferSupplierType->nPortIndex == 0)
            	m_sInBufSupplierType.eBufferSupplier = bufferSupplierType->eBufferSupplier;
            else if(bufferSupplierType->nPortIndex == 1)
            	m_sOutBufSupplierType.eBufferSupplier = bufferSupplierType->eBufferSupplier;
        	else
        		eError = OMX_ErrorBadPortIndex;

        	break;
	    }

	    case OMX_IndexParamVideoAvc:
	    {
	    	logi(" COMPONENT_SET_PARAMETER: OMX_IndexParamVideoAvc");
      		logi("set_parameter: OMX_IndexParamVideoAvc, do nothing.\n");
	    	break;
	    }

	    case OMX_IndexParamVideoH263:
	    {
	    	logi(" COMPONENT_SET_PARAMETER: OMX_IndexParamVideoH263");
     		logi("set_parameter: OMX_IndexParamVideoH263, do nothing.\n");
	    	break;
	    }

	    case OMX_IndexParamVideoMpeg4:
	    {
	    	logi(" COMPONENT_SET_PARAMETER: OMX_IndexParamVideoMpeg4");
     		logi("set_parameter: OMX_IndexParamVideoMpeg4, do nothing.\n");
	    	break;
	    }

	    default:
	    {
#if CONFIG_OS == OPTION_OS_ANDROID            
	    	if((AW_VIDEO_EXTENSIONS_INDEXTYPE)eParamIndex == AWOMX_IndexParamVideoUseAndroidNativeBuffer2)
	    	{
		    	logi(" COMPONENT_SET_PARAMETER: AWOMX_IndexParamVideoUseAndroidNativeBuffer2");
	     		logi("set_parameter: AWOMX_IndexParamVideoUseAndroidNativeBuffer2, do nothing.\n");
	     		m_useAndroidBuffer = OMX_TRUE;
		    	break;
	    	}
	    	else if((AW_VIDEO_EXTENSIONS_INDEXTYPE)eParamIndex == AWOMX_IndexParamVideoEnableAndroidNativeBuffers)
	    	{
		    	logi(" COMPONENT_SET_PARAMETER: AWOMX_IndexParamVideoEnableAndroidNativeBuffers");
	     		logi("set_parameter: AWOMX_IndexParamVideoEnableAndroidNativeBuffers, set m_useAndroidBuffer to OMX_TRUE\n");

                EnableAndroidNativeBuffersParams *EnableAndroidBufferParams =  (EnableAndroidNativeBuffersParams*) pParamData;
                logi(" enbleParam = %d\n",EnableAndroidBufferParams->enable);
                if(1==EnableAndroidBufferParams->enable)
                {
                    m_useAndroidBuffer = OMX_TRUE;
                }         
		    	break;
	    	}
			#if 1
            else if((AW_VIDEO_EXTENSIONS_INDEXTYPE)eParamIndex==AWOMX_IndexParamVideoUseStoreMetaDataInBuffer)
            {
                logi(" COMPONENT_SET_PARAMETER: AWOMX_IndexParamVideoUseStoreMetaDataInBuffer");

                StoreMetaDataInBuffersParams *pStoreMetaData = (StoreMetaDataInBuffersParams*)pParamData;
                if(pStoreMetaData->nPortIndex != 1)
                {
                    logd("error: not support set AWOMX_IndexParamVideoUseStoreMetaDataInBuffer for inputPort");
                    eError = OMX_ErrorUnsupportedIndex;
                }
                if(pStoreMetaData->nPortIndex==1 && pStoreMetaData->bStoreMetaData==OMX_TRUE)
                {
                    logi("***set m_storeOutputMetaDataFlag to TRUE");
                    m_storeOutputMetaDataFlag = OMX_TRUE;
                }
            }
			#endif
            #if(CONFIG_OS_VERSION >= OPTION_OS_VERSION_ANDROID_4_4)  
            else if((AW_VIDEO_EXTENSIONS_INDEXTYPE)eParamIndex==AWOMX_IndexParamVideoUsePrepareForAdaptivePlayback)
            {
                logi(" COMPONENT_SET_PARAMETER: AWOMX_IndexParamVideoUsePrepareForAdaptivePlayback");

                PrepareForAdaptivePlaybackParams *pPlaybackParams;
                pPlaybackParams = (PrepareForAdaptivePlaybackParams *)pParamData;

                if(pPlaybackParams->nPortIndex==1 && pPlaybackParams->bEnable==OMX_TRUE)
                {
                    logi("set adaptive playback ,maxWidth = %d, maxHeight = %d",
                           (int)pPlaybackParams->nMaxFrameWidth,
                           (int)pPlaybackParams->nMaxFrameHeight);
                    
                    m_maxWidth  = pPlaybackParams->nMaxFrameWidth;
                    m_maxHeight = pPlaybackParams->nMaxFrameHeight;
                }
            }
            #endif            
	    	else
	    	{
	    		logi("Setparameter: unknown param %d\n", eParamIndex);
	    		eError = OMX_ErrorUnsupportedIndex;
	    		break;
	    	}
#else
            logi("Setparameter: unknown param %d\n", eParamIndex);
    		eError = OMX_ErrorUnsupportedIndex;
    		break;
#endif
	    }
    }

    return eError;
}

OMX_ERRORTYPE  omx_vdec::get_config(OMX_IN OMX_HANDLETYPE pHComp, OMX_IN OMX_INDEXTYPE eConfigIndex, OMX_INOUT OMX_PTR pConfigData)
{
	OMX_ERRORTYPE eError = OMX_ErrorNone;

	CEDARX_UNUSE(pHComp);

    logi("(f:%s, l:%d) index = %d", __FUNCTION__, __LINE__, eConfigIndex);
	if (m_state == OMX_StateInvalid)
	{
		logi("get_config in Invalid State\n");
		return OMX_ErrorInvalidState;
	}

	switch (eConfigIndex)
	{
        case OMX_IndexConfigCommonOutputCrop:
        {
            OMX_CONFIG_RECTTYPE *pRect = (OMX_CONFIG_RECTTYPE *)pConfigData;

            logw("+++++ get display crop: top[%d],left[%d],width[%d],height[%d]",
                  (int)mVideoRect.nTop,(int)mVideoRect.nLeft,
                  (int)mVideoRect.nWidth,(int)mVideoRect.nHeight);
            
            if(mVideoRect.nHeight != 0)
            {
                memcpy(pRect,&mVideoRect,sizeof(OMX_CONFIG_RECTTYPE));
            }
            else
            {
                logw("the crop is invalid!");
                eError = OMX_ErrorUnsupportedIndex;
            }
            break;
        }
    	default:
    	{
    		logi("get_config: unknown param %d\n",eConfigIndex);
    		eError = OMX_ErrorUnsupportedIndex;
    	}
	}

	return eError;
}

OMX_ERRORTYPE omx_vdec::set_config(OMX_IN OMX_HANDLETYPE pHComp, OMX_IN OMX_INDEXTYPE eConfigIndex, OMX_IN OMX_PTR pConfigData)
{

	logi("(f:%s, l:%d) index = %d", __FUNCTION__, __LINE__, eConfigIndex);

	CEDARX_UNUSE(pHComp);
	CEDARX_UNUSE(pConfigData);

	if(m_state == OMX_StateInvalid)
	{
		logi("set_config in Invalid State\n");
		return OMX_ErrorInvalidState;
	}

	OMX_ERRORTYPE eError = OMX_ErrorNone;

	if (m_state == OMX_StateExecuting)
	{
		logi("set_config: Ignore in Executing state\n");
		return eError;
	}

	switch(eConfigIndex)
	{
		default:
		{
			eError = OMX_ErrorUnsupportedIndex;
		}
	}

	return eError;
}


OMX_ERRORTYPE  omx_vdec::get_extension_index(OMX_IN OMX_HANDLETYPE pHComp, OMX_IN OMX_STRING pParamName, OMX_OUT OMX_INDEXTYPE* pIndexType)
{
	unsigned int  nIndex;
    OMX_ERRORTYPE eError = OMX_ErrorUndefined;

    logi("(f:%s, l:%d) param name = %s", __FUNCTION__, __LINE__, pParamName);
    if(m_state == OMX_StateInvalid)
    {
    	logi("Get Extension Index in Invalid State\n");
        return OMX_ErrorInvalidState;
    }

    if(pHComp == NULL)
    	return OMX_ErrorBadParameter;

    for(nIndex = 0; nIndex < sizeof(sVideoDecCustomParams)/sizeof(VIDDEC_CUSTOM_PARAM); nIndex++)
    {
        if(strcmp((char *)pParamName, (char *)&(sVideoDecCustomParams[nIndex].cCustomParamName)) == 0)
        {
            *pIndexType = sVideoDecCustomParams[nIndex].nCustomParamIndex;
            eError = OMX_ErrorNone;
            break;
        }
    }

    return eError;
}



OMX_ERRORTYPE omx_vdec::get_state(OMX_IN OMX_HANDLETYPE pHComp, OMX_OUT OMX_STATETYPE* pState)
{
	logi("(f:%s, l:%d) ", __FUNCTION__, __LINE__);

	if(pHComp == NULL || pState == NULL)
		return OMX_ErrorBadParameter;

	*pState = m_state;
    logi("COMPONENT_GET_STATE, state[0x%x]", m_state);
	return OMX_ErrorNone;
}


OMX_ERRORTYPE omx_vdec::component_tunnel_request(OMX_IN    OMX_HANDLETYPE       pHComp,
                                                 OMX_IN    OMX_U32              uPort,
                                                 OMX_IN    OMX_HANDLETYPE       pPeerComponent,
                                                 OMX_IN    OMX_U32              uPeerPort,
                                                 OMX_INOUT OMX_TUNNELSETUPTYPE* pTunnelSetup)
{
	logi(" COMPONENT_TUNNEL_REQUEST");

	logw("Error: component_tunnel_request Not Implemented\n");

	CEDARX_UNUSE(pHComp);
	CEDARX_UNUSE(uPort);
	CEDARX_UNUSE(pPeerComponent);
	CEDARX_UNUSE(uPeerPort);
	CEDARX_UNUSE(pTunnelSetup);
	return OMX_ErrorNotImplemented;
}



OMX_ERRORTYPE  omx_vdec::use_buffer(OMX_IN    OMX_HANDLETYPE          hComponent,
                  			        OMX_INOUT OMX_BUFFERHEADERTYPE**  ppBufferHdr,
                  			        OMX_IN    OMX_U32                 nPortIndex,
                  			        OMX_IN    OMX_PTR                 pAppPrivate,
                  			        OMX_IN    OMX_U32                 nSizeBytes,
                  			        OMX_IN    OMX_U8*                 pBuffer)
{
    OMX_PARAM_PORTDEFINITIONTYPE*   pPortDef;
    OMX_U32                         nIndex = 0x0;

    logi("(f:%s, l:%d) PortIndex[%d], nSizeBytes[%d], pBuffer[%p]", __FUNCTION__, __LINE__, (int)nPortIndex, (int)nSizeBytes, pBuffer);
	if(hComponent == NULL || ppBufferHdr == NULL || pBuffer == NULL)
	{
		return OMX_ErrorBadParameter;
	}

    if (nPortIndex == m_sInPortDefType.nPortIndex)
        pPortDef = &m_sInPortDefType;
    else if (nPortIndex == m_sOutPortDefType.nPortIndex)
        pPortDef = &m_sOutPortDefType;
    else
    	return OMX_ErrorBadParameter;

    if (m_state!=OMX_StateLoaded && m_state!=OMX_StateWaitForResources && pPortDef->bEnabled!=OMX_FALSE)
    {
        logw("pPortDef[%d]->bEnabled=%d, m_state=0x%x, Can't use_buffer!", (int)nPortIndex, pPortDef->bEnabled, m_state);
    	return OMX_ErrorIncorrectStateOperation;
    }
    logi("pPortDef[%d]->bEnabled=%d, m_state=0x%x, can use_buffer.", (int)nPortIndex, pPortDef->bEnabled, m_state);
    
    

    if(pPortDef->bPopulated)
        return OMX_ErrorBadParameter;

    // Find an empty position in the BufferList and allocate memory for the buffer header.
    // Use the buffer passed by the client to initialize the actual buffer
    // inside the buffer header.
    if (nPortIndex == m_sInPortDefType.nPortIndex)
    {
        if (nSizeBytes != pPortDef->nBufferSize)
          	return OMX_ErrorBadParameter;
    
        logi("use_buffer, m_sInPortDefType.nPortIndex=[%d]", (int)m_sInPortDefType.nPortIndex);
    	pthread_mutex_lock(&m_inBufMutex);

    	if((OMX_S32)m_sInBufList.nAllocSize >= m_sInBufList.nBufArrSize)
        {
        	pthread_mutex_unlock(&m_inBufMutex);
        	return OMX_ErrorInsufficientResources;
        }

    	nIndex = m_sInBufList.nAllocSize;
    	m_sInBufList.nAllocSize++;

    	m_sInBufList.pBufArr[nIndex].pBuffer          = pBuffer;
    	m_sInBufList.pBufArr[nIndex].nAllocLen        = nSizeBytes;
    	m_sInBufList.pBufArr[nIndex].pAppPrivate      = pAppPrivate;
    	m_sInBufList.pBufArr[nIndex].nInputPortIndex  = nPortIndex;
    	m_sInBufList.pBufArr[nIndex].nOutputPortIndex = 0xFFFFFFFE;
        *ppBufferHdr = &m_sInBufList.pBufArr[nIndex];
        if (m_sInBufList.nAllocSize == pPortDef->nBufferCountActual)
        	pPortDef->bPopulated = OMX_TRUE;

    	pthread_mutex_unlock(&m_inBufMutex);
    }
    else
    {
#if(CONFIG_OS == OPTION_OS_ANDROID && CONFIG_OS_VERSION>=OPTION_OS_VERSION_ANDROID_4_4)       
        if(m_storeOutputMetaDataFlag==OMX_TRUE)
        {
            //*on A64 becaseu the so is  unbelievable,
            //*lib64/libstagefright.so and lib/libOmxVdec.so, so the size is not match
            //*we just not return error here.
            if(nSizeBytes != sizeof(VideoDecoderOutputMetaData))
            {
                logw("warning: the nSizeBytes is not match: %d, %d",
                      nSizeBytes,sizeof(VideoDecoderOutputMetaData)); 
                //return OMX_ErrorBadParameter;
            }
        }
        else
        {
            if(nSizeBytes != pPortDef->nBufferSize)
          	    return OMX_ErrorBadParameter;
        }
#else
        if(nSizeBytes != pPortDef->nBufferSize)
          	    return OMX_ErrorBadParameter;
#endif
        
        logi("use_buffer, m_sOutPortDefType.nPortIndex=[%d]", (int)m_sOutPortDefType.nPortIndex);
    	pthread_mutex_lock(&m_outBufMutex);

    	if((OMX_S32)m_sOutBufList.nAllocSize >= m_sOutBufList.nBufArrSize)
        {
        	pthread_mutex_unlock(&m_outBufMutex);
        	return OMX_ErrorInsufficientResources;
        }
        pPortDef->nBufferSize = nSizeBytes;
    	nIndex = m_sOutBufList.nAllocSize;
    	m_sOutBufList.nAllocSize++;

    	m_sOutBufList.pBufArr[nIndex].pBuffer          = pBuffer;
    	m_sOutBufList.pBufArr[nIndex].nAllocLen        = nSizeBytes;
    	m_sOutBufList.pBufArr[nIndex].pAppPrivate      = pAppPrivate;
    	m_sOutBufList.pBufArr[nIndex].nInputPortIndex  = 0xFFFFFFFE;
    	m_sOutBufList.pBufArr[nIndex].nOutputPortIndex = nPortIndex;
        *ppBufferHdr = &m_sOutBufList.pBufArr[nIndex];
        if (m_sOutBufList.nAllocSize == pPortDef->nBufferCountActual)
        {
        	pPortDef->bPopulated      = OMX_TRUE;
            if(m_useAndroidBuffer == OMX_TRUE)
            {
                //* clear the output buffer info
                memset(&mOutputBufferInfo, 0, sizeof(OMXOutputBufferInfoT)*OMX_MAX_VIDEO_BUFFER_NUM);
            }
        }

    	pthread_mutex_unlock(&m_outBufMutex);
    }

    return OMX_ErrorNone;
}

OMX_ERRORTYPE omx_vdec::allocate_buffer(OMX_IN    OMX_HANDLETYPE         hComponent,
        								OMX_INOUT OMX_BUFFERHEADERTYPE** ppBufferHdr,
        								OMX_IN    OMX_U32                nPortIndex,
        								OMX_IN    OMX_PTR                pAppPrivate,
        								OMX_IN    OMX_U32                nSizeBytes)
{
	OMX_S8                        nIndex = 0x0;
	OMX_PARAM_PORTDEFINITIONTYPE* pPortDef;

	//logi(" COMPONENT_ALLOCATE_BUFFER");
    logi("(f:%s, l:%d) nPortIndex[%d], nSizeBytes[%d]", __FUNCTION__, __LINE__, (int)nPortIndex, (int)nSizeBytes);
	if(hComponent == NULL || ppBufferHdr == NULL)
		return OMX_ErrorBadParameter;

    if (nPortIndex == m_sInPortDefType.nPortIndex)
        pPortDef = &m_sInPortDefType;
    else
    {
        if (nPortIndex == m_sOutPortDefType.nPortIndex)
	        pPortDef = &m_sOutPortDefType;
        else
        	return OMX_ErrorBadParameter;
    }

//    if (!pPortDef->bEnabled)
//    	return OMX_ErrorIncorrectStateOperation;

    if (m_state!=OMX_StateLoaded && m_state!=OMX_StateWaitForResources && pPortDef->bEnabled!=OMX_FALSE)
    {
        logw("pPortDef[%d]->bEnabled=%d, m_state=0x%x, Can't allocate_buffer!", (int)nPortIndex, pPortDef->bEnabled, m_state);
    	return OMX_ErrorIncorrectStateOperation;
    }
    logi("pPortDef[%d]->bEnabled=%d, m_state=0x%x, can allocate_buffer.", (int)nPortIndex, pPortDef->bEnabled, m_state);

    if (nSizeBytes != pPortDef->nBufferSize || pPortDef->bPopulated)
    	return OMX_ErrorBadParameter;

    // Find an empty position in the BufferList and allocate memory for the buffer header
    // and the actual buffer
    if (nPortIndex == m_sInPortDefType.nPortIndex)
    {
        logi("allocate_buffer, m_sInPortDefType.nPortIndex[%d]", (int)m_sInPortDefType.nPortIndex);
    	pthread_mutex_lock(&m_inBufMutex);

    	if((OMX_S32)m_sInBufList.nAllocSize >= m_sInBufList.nBufArrSize)
        {
        	pthread_mutex_unlock(&m_inBufMutex);
        	return OMX_ErrorInsufficientResources;
        }

    	nIndex = m_sInBufList.nAllocSize;

        if(mIsSecureVideoFlag == OMX_TRUE)
        {
            while(mHadInitDecoderFlag == OMX_FALSE)
            {
                logd("+++m_decoder is null when allocate secure buffer,we sleep here untill it had inited");
                usleep(10*1000);
            }

            #if(ADJUST_ADDRESS_FOR_SECURE_OS_OPTEE)
            m_sInBufList.pBufArr[nIndex].pBuffer = (OMX_U8*)VideoRequestSecureBuffer(m_decoder,nSizeBytes);
            #else
            char* pSecureBuf = VideoRequestSecureBuffer(m_decoder,nSizeBytes);
            m_sInBufList.pBufArr[nIndex].pBuffer = (OMX_U8*)SecureMemAdapterGetPhysicAddressCpu(pSecureBuf);
            #endif

            logd("+++secure input buffer[%p], nSizeBytes[%d]",
                 m_sInBufList.pBufArr[nIndex].pBuffer,(int)nSizeBytes);
        }
        else
        {
            m_sInBufList.pBufArr[nIndex].pBuffer = (OMX_U8*)malloc(nSizeBytes);
        }

        if (!m_sInBufList.pBufArr[nIndex].pBuffer)
        {
            logd("+++++error: allocate input buffer failed!");
        	pthread_mutex_unlock(&m_inBufMutex);
            return OMX_ErrorInsufficientResources;
        }

        m_sInBufList.nAllocBySelfFlags |= (1<<nIndex);

    	m_sInBufList.pBufArr[nIndex].nAllocLen        = nSizeBytes;
    	m_sInBufList.pBufArr[nIndex].pAppPrivate      = pAppPrivate;
    	m_sInBufList.pBufArr[nIndex].nInputPortIndex  = nPortIndex;
    	m_sInBufList.pBufArr[nIndex].nOutputPortIndex = 0xFFFFFFFE;
        *ppBufferHdr = &m_sInBufList.pBufArr[nIndex];

        m_sInBufList.nAllocSize++;
        
        if (m_sInBufList.nAllocSize == pPortDef->nBufferCountActual)
        	pPortDef->bPopulated = OMX_TRUE;

    	pthread_mutex_unlock(&m_inBufMutex);
    }
    else
    {
        logi("allocate_buffer, m_sOutPortDefType.nPortIndex[%d]", (int)m_sOutPortDefType.nPortIndex);
    	pthread_mutex_lock(&m_outBufMutex);

    	if((OMX_S32)m_sOutBufList.nAllocSize >= m_sOutBufList.nBufArrSize)
        {
        	pthread_mutex_unlock(&m_outBufMutex);
        	return OMX_ErrorInsufficientResources;
        }

    	nIndex = m_sOutBufList.nAllocSize;

    	m_sOutBufList.pBufArr[nIndex].pBuffer = (OMX_U8*)malloc(nSizeBytes);

        if (!m_sOutBufList.pBufArr[nIndex].pBuffer)
        {
        	pthread_mutex_unlock(&m_outBufMutex);
            return OMX_ErrorInsufficientResources;
        }

        m_sOutBufList.nAllocBySelfFlags |= (1<<nIndex);

        m_sOutBufList.pBufArr[nIndex].nAllocLen        = nSizeBytes;
        m_sOutBufList.pBufArr[nIndex].pAppPrivate      = pAppPrivate;
        m_sOutBufList.pBufArr[nIndex].nInputPortIndex  = 0xFFFFFFFE;
        m_sOutBufList.pBufArr[nIndex].nOutputPortIndex = nPortIndex;
        *ppBufferHdr = &m_sOutBufList.pBufArr[nIndex];

        m_sOutBufList.nAllocSize++;
        
        if (m_sOutBufList.nAllocSize == pPortDef->nBufferCountActual)
        	pPortDef->bPopulated = OMX_TRUE;

    	pthread_mutex_unlock(&m_outBufMutex);
    }

    return OMX_ErrorNone;
}

OMX_ERRORTYPE omx_vdec::free_buffer(OMX_IN  OMX_HANDLETYPE        hComponent,
        							OMX_IN  OMX_U32               nPortIndex,
        							OMX_IN  OMX_BUFFERHEADERTYPE* pBufferHdr)
{
    OMX_PARAM_PORTDEFINITIONTYPE* pPortDef;
    OMX_S32                       nIndex;

	logi("(f:%s, l:%d) nPortIndex = %d, pBufferHdr = %p, m_state=0x%x", __FUNCTION__, __LINE__, (int)nPortIndex, pBufferHdr, m_state);
    if(hComponent == NULL || pBufferHdr == NULL)
    	return OMX_ErrorBadParameter;

    // Match the pBufferHdr to the appropriate entry in the BufferList
    // and free the allocated memory
    if (nPortIndex == m_sInPortDefType.nPortIndex)
    {
        pPortDef = &m_sInPortDefType;

    	pthread_mutex_lock(&m_inBufMutex);

    	for(nIndex = 0; nIndex < m_sInBufList.nBufArrSize; nIndex++)
    	{
    		if(pBufferHdr == &m_sInBufList.pBufArr[nIndex])
    			break;
    	}

    	if(nIndex == m_sInBufList.nBufArrSize)
    	{
    		pthread_mutex_unlock(&m_inBufMutex);
    		return OMX_ErrorBadParameter;
    	}

    	if(m_sInBufList.nAllocBySelfFlags & (1<<nIndex))
    	{
            if(mIsSecureVideoFlag == OMX_TRUE)
            {
                #if(ADJUST_ADDRESS_FOR_SECURE_OS_OPTEE)
                char*   pVirtAddr = (char*)m_sInBufList.pBufArr[nIndex].pBuffer;
                #else
                OMX_U8* pPhyAddr  = m_sInBufList.pBufArr[nIndex].pBuffer;
                char*   pVirtAddr = (char*)SecureMemAdapterGetVirtualAddressCpu(pPhyAddr);
                #endif
                
                logd("++++++++++free secure input buffer = %p",pVirtAddr);
                VideoReleaseSecureBuffer(m_decoder,pVirtAddr);
            }
            else
    		    free(m_sInBufList.pBufArr[nIndex].pBuffer);
            
    		m_sInBufList.pBufArr[nIndex].pBuffer = NULL;
    		m_sInBufList.nAllocBySelfFlags &= ~(1<<nIndex);
    	}

    	m_sInBufList.nAllocSize--;
        
    	if(m_sInBufList.nAllocSize == 0)
    		pPortDef->bPopulated = OMX_FALSE;

    	pthread_mutex_unlock(&m_inBufMutex);
    }
    else if (nPortIndex == m_sOutPortDefType.nPortIndex)
    {
	    pPortDef = &m_sOutPortDefType;

    	pthread_mutex_lock(&m_outBufMutex);

    	for(nIndex = 0; nIndex < m_sOutBufList.nBufArrSize; nIndex++)
    	{
    		logi("pBufferHdr = %p, &m_sOutBufList.pBufArr[%d] = %p", pBufferHdr, (int)nIndex, &m_sOutBufList.pBufArr[nIndex]);
    		if(pBufferHdr == &m_sOutBufList.pBufArr[nIndex])
    			break;
    	}

    	logi("index = %d", (int)nIndex);

    	if(nIndex == m_sOutBufList.nBufArrSize)
    	{
    		pthread_mutex_unlock(&m_outBufMutex);
    		return OMX_ErrorBadParameter;
    	}

    	if(m_sOutBufList.nAllocBySelfFlags & (1<<nIndex))
    	{
    		free(m_sOutBufList.pBufArr[nIndex].pBuffer);
    		m_sOutBufList.pBufArr[nIndex].pBuffer = NULL;
    		m_sOutBufList.nAllocBySelfFlags &= ~(1<<nIndex);
    	}

    	m_sOutBufList.nAllocSize--;
        
    	if(m_sOutBufList.nAllocSize == 0)
    	{
    		pPortDef->bPopulated = OMX_FALSE;

            if(m_useAndroidBuffer == OMX_TRUE)
            {
                //* free all gpu buffer 
                int i;
                for(i = 0; i < OMX_MAX_VIDEO_BUFFER_NUM; i++)
                {

#if(CONFIG_OS_VERSION >= OPTION_OS_VERSION_ANDROID_5_0)

                    if(mOutputBufferInfo[i].handle_ion != 0)
                    {
                        logd("ion_free: handle_ion[%p],i[%d]",
                              mOutputBufferInfo[i].handle_ion,i);
                        ion_free(mIonFd,mOutputBufferInfo[i].handle_ion);
                        mOutputBufferInfo[i].handle_ion = 0;
                    }
#else
					if(mOutputBufferInfo[i].handle_ion != NULL)
					{
						logd("ion_free: handle_ion[%p],i[%d]",
							  mOutputBufferInfo[i].handle_ion,i);
						ion_free(mIonFd,mOutputBufferInfo[i].handle_ion);
						mOutputBufferInfo[i].handle_ion = NULL;
					}
#endif
                }
            }
    	}

    	pthread_mutex_unlock(&m_outBufMutex);
    }
    else
        return OMX_ErrorBadParameter;

    return OMX_ErrorNone;
}




OMX_ERRORTYPE  omx_vdec::empty_this_buffer(OMX_IN OMX_HANDLETYPE hComponent, OMX_IN OMX_BUFFERHEADERTYPE* pBufferHdr)
{
    logv("***emptyThisBuffer: pts = %lld , videoFormat = %d",
         pBufferHdr->nTimeStamp,
         m_eCompressionFormat);
    
    ThrCmdType eCmdNative   = EmptyBuf;
    if(hComponent == NULL || pBufferHdr == NULL)
    	return OMX_ErrorBadParameter;

    if (!m_sInPortDefType.bEnabled)
    	return OMX_ErrorIncorrectStateOperation;

    if (pBufferHdr->nInputPortIndex != 0x0  || pBufferHdr->nOutputPortIndex != OMX_NOPORT)
        return OMX_ErrorBadPortIndex;

    if (m_state != OMX_StateExecuting && m_state != OMX_StatePause)
        return OMX_ErrorIncorrectStateOperation;

    post_event(eCmdNative, 0, (OMX_PTR)pBufferHdr);

    return OMX_ErrorNone;
}



OMX_ERRORTYPE  omx_vdec::fill_this_buffer(OMX_IN OMX_HANDLETYPE hComponent, OMX_IN OMX_BUFFERHEADERTYPE* pBufferHdr)
{
    ThrCmdType eCmdNative = FillBuf;

    logv("(f:%s, l:%d) ", __FUNCTION__, __LINE__);
    if(hComponent == NULL || pBufferHdr == NULL)
    	return OMX_ErrorBadParameter;

    if (!m_sOutPortDefType.bEnabled)
    	return OMX_ErrorIncorrectStateOperation;

    if (pBufferHdr->nOutputPortIndex != 0x1 || pBufferHdr->nInputPortIndex != OMX_NOPORT)
        return OMX_ErrorBadPortIndex;

    if (m_state != OMX_StateExecuting && m_state != OMX_StatePause)
        return OMX_ErrorIncorrectStateOperation;

    post_event(eCmdNative, 0, (OMX_PTR)pBufferHdr);
    return OMX_ErrorNone;
}



OMX_ERRORTYPE  omx_vdec::set_callbacks(OMX_IN OMX_HANDLETYPE        pHComp,
                                           OMX_IN OMX_CALLBACKTYPE* pCallbacks,
                                           OMX_IN OMX_PTR           pAppData)
{
	logi("(f:%s, l:%d) ", __FUNCTION__, __LINE__);

    if(pHComp == NULL || pCallbacks == NULL || pAppData == NULL)
    	return OMX_ErrorBadParameter;
    memcpy(&m_Callbacks, pCallbacks, sizeof(OMX_CALLBACKTYPE));
    m_pAppData = pAppData;

    return OMX_ErrorNone;
}



OMX_ERRORTYPE  omx_vdec::component_deinit(OMX_IN OMX_HANDLETYPE pHComp)
{
	//logi(" COMPONENT_DEINIT");
    logi("(f:%s, l:%d) ", __FUNCTION__, __LINE__);
    OMX_ERRORTYPE eError = OMX_ErrorNone;
    ThrCmdType    eCmdNative   = Stop;
    OMX_S32       nIndex = 0;

	CEDARX_UNUSE(pHComp);

    // In case the client crashes, check for nAllocSize parameter.
    // If this is greater than zero, there are elements in the list that are not free'd.
    // In that case, free the elements.
    if (m_sInBufList.nAllocSize > 0)
    {
    	for(nIndex=0; nIndex<m_sInBufList.nBufArrSize; nIndex++)
    	{
    		if(m_sInBufList.pBufArr[nIndex].pBuffer != NULL)
    		{
    			if(m_sInBufList.nAllocBySelfFlags & (1<<nIndex))
    			{
                    if(mIsSecureVideoFlag == OMX_TRUE)
                    {
                        #if(ADJUST_ADDRESS_FOR_SECURE_OS_OPTEE)
                        char*   pVirtAddr  = (char*)m_sInBufList.pBufArr[nIndex].pBuffer;
                        #else
                        OMX_U8* pPhyAddr  = m_sInBufList.pBufArr[nIndex].pBuffer;
                        char*   pVirtAddr = (char*)SecureMemAdapterGetVirtualAddressCpu(pPhyAddr);
                        #endif
                        VideoReleaseSecureBuffer(m_decoder,pVirtAddr);
                    }
                    else
    				    free(m_sInBufList.pBufArr[nIndex].pBuffer);
                    
    				m_sInBufList.pBufArr[nIndex].pBuffer = NULL;
    			}
    		}
    	}

        if (m_sInBufList.pBufArr != NULL)
        	free(m_sInBufList.pBufArr);

        if (m_sInBufList.pBufHdrList != NULL)
        	free(m_sInBufList.pBufHdrList);

    	memset(&m_sInBufList, 0, sizeof(struct _BufferList));
    	m_sInBufList.nBufArrSize = m_sInPortDefType.nBufferCountActual;
    }

    if (m_sOutBufList.nAllocSize > 0)
    {
    	for(nIndex=0; nIndex<m_sOutBufList.nBufArrSize; nIndex++)
    	{
    		if(m_sOutBufList.pBufArr[nIndex].pBuffer != NULL)
    		{
    			if(m_sOutBufList.nAllocBySelfFlags & (1<<nIndex))
    			{
    				free(m_sOutBufList.pBufArr[nIndex].pBuffer);
    				m_sOutBufList.pBufArr[nIndex].pBuffer = NULL;
    			}
    		}
    	}

        if (m_sOutBufList.pBufArr != NULL)
        	free(m_sOutBufList.pBufArr);

        if (m_sOutBufList.pBufHdrList != NULL)
        	free(m_sOutBufList.pBufHdrList);

    	memset(&m_sOutBufList, 0, sizeof(struct _BufferList));
    	m_sOutBufList.nBufArrSize = m_sOutPortDefType.nBufferCountActual;
    }

    post_event(eCmdNative, eCmdNative, NULL);

    // Wait for thread to exit so we can get the status into "error"
    pthread_join(m_thread_id, (void**)&eError);
    pthread_join(m_vdrv_thread_id, (void**)&eError);
    
    logd("(f:%s, l:%d) two threads exit!", __FUNCTION__, __LINE__);
    
    // close the pipe handles
    close(m_cmdpipe[0]);
    close(m_cmdpipe[1]);
    close(m_cmddatapipe[0]);
    close(m_cmddatapipe[1]);
    close(m_vdrv_cmdpipe[0]);
    close(m_vdrv_cmdpipe[1]);

    if(m_decoder != NULL)
    {
    	DestroyVideoDecoder(m_decoder);
    	m_decoder = NULL;
    }
    
    return eError;
}


OMX_ERRORTYPE  omx_vdec::use_EGL_image(OMX_IN OMX_HANDLETYPE               pHComp,
                                          OMX_INOUT OMX_BUFFERHEADERTYPE** ppBufferHdr,
                                          OMX_IN OMX_U32                   uPort,
                                          OMX_IN OMX_PTR                   pAppData,
                                          OMX_IN void*                     pEglImage)
{
	logw("Error : use_EGL_image:  Not Implemented \n");

	CEDARX_UNUSE(pHComp);
	CEDARX_UNUSE(ppBufferHdr);
	CEDARX_UNUSE(uPort);
	CEDARX_UNUSE(pAppData);
	CEDARX_UNUSE(pEglImage);

    return OMX_ErrorNotImplemented;
}


OMX_ERRORTYPE  omx_vdec::component_role_enum(OMX_IN  OMX_HANDLETYPE pHComp,
                                             OMX_OUT OMX_U8*        pRole,
                                             OMX_IN  OMX_U32        uIndex)
{
	//logi(" COMPONENT_ROLE_ENUM");
    logi("(f:%s, l:%d) ", __FUNCTION__, __LINE__);
	OMX_ERRORTYPE eRet = OMX_ErrorNone;

	CEDARX_UNUSE(pHComp);

	if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.mpeg4", OMX_MAX_STRINGNAME_SIZE))
	{
		if((0 == uIndex) && pRole)
		{
			strncpy((char *)pRole, "video_decoder.mpeg4", OMX_MAX_STRINGNAME_SIZE);
			logi("component_role_enum: pRole %s\n", pRole);
		}
		else
		{
			eRet = OMX_ErrorNoMore;
		}
	}
	else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.h263", OMX_MAX_STRINGNAME_SIZE))
	{
		if((0 == uIndex) && pRole)
		{
			strncpy((char *)pRole, "video_decoder.h263",OMX_MAX_STRINGNAME_SIZE);
			logi("component_role_enum: pRole %s\n",pRole);
		}
		else
		{
			logi("\n No more roles \n");
			eRet = OMX_ErrorNoMore;
		}
	}
	else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.avc", OMX_MAX_STRINGNAME_SIZE))
	{
		if((0 == uIndex) && pRole)
		{
			strncpy((char *)pRole, "video_decoder.avc",OMX_MAX_STRINGNAME_SIZE);
			logi("component_role_enum: pRole %s\n",pRole);
		}
		else
		{
			logi("\n No more roles \n");
			eRet = OMX_ErrorNoMore;
		}
	}
    else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.avc.secure", OMX_MAX_STRINGNAME_SIZE))
	{
		if((0 == uIndex) && pRole)
		{
			strncpy((char *)pRole, "video_decoder.avc.secure",OMX_MAX_STRINGNAME_SIZE);
			logi("component_role_enum: pRole %s\n",pRole);
		}
		else
		{
			logi("\n No more roles \n");
			eRet = OMX_ErrorNoMore;
		}
	}
	else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.vc1", OMX_MAX_STRINGNAME_SIZE))
	{
		if((0 == uIndex) && pRole)
		{
			strncpy((char *)pRole, "video_decoder.vc1",OMX_MAX_STRINGNAME_SIZE);
			logi("component_role_enum: pRole %s\n",pRole);
		}
		else
		{
			logi("\n No more roles \n");
			eRet = OMX_ErrorNoMore;
		}
	}
    else if(!strncmp((char*)m_cName, "OMX.allwinner.video.decoder.vp8", OMX_MAX_STRINGNAME_SIZE))
	{
		if((0 == uIndex) && pRole)
		{
			strncpy((char *)pRole, "video_decoder.vp8",OMX_MAX_STRINGNAME_SIZE);
			logi("component_role_enum: pRole %s\n",pRole);
		}
		else
		{
			logi("\n No more roles \n");
			eRet = OMX_ErrorNoMore;
		}
	}
	else
	{
		logd("\nERROR:Querying pRole on Unknown Component\n");
		eRet = OMX_ErrorInvalidComponentName;
	}

	return eRet;
}

OMX_ERRORTYPE omx_vdec::send_vdrv_feedback_msg(OMX_IN OMX_VDRV_FEEDBACK_MSGTYPE nMsg,
                                                   OMX_IN OMX_U32         uParam1,
                                                   OMX_IN OMX_PTR         pCmdData)
{
    ThrCmdType    eCmdNative;
    OMX_ERRORTYPE eError = OMX_ErrorNone;
    logi("(f:%s, l:%d) ", __FUNCTION__, __LINE__);

    if(m_state == OMX_StateInvalid)
    {
    	logd("ERROR: Send Command in Invalid State\n");
        return OMX_ErrorInvalidState;
    }

    switch (nMsg)
    {
        case OMX_VdrvFeedbackMsg_NotifyEos:
        	logi("(omx_vdec, f:%s, l:%d) send OMX_VdrvFeedbackMsg_NotifyEos", __FUNCTION__, __LINE__);
            eCmdNative = VdrvNotifyEos;
	        break;

        case OMX_VdrvFeedbackMsg_ResolutionChange:
        	logi("(omx_vdec, f:%s, l:%d) send OMX_VdrvFeedbackMsg_ResolutionChange", __FUNCTION__, __LINE__);
            eCmdNative = VdrvResolutionChange;
            break;

        default:
            logw("(omx_vdec, f:%s, l:%d) send unknown feedback message[0x%x]", __FUNCTION__, __LINE__, nMsg);
            return OMX_ErrorUndefined;
    }
    
    post_event(eCmdNative, uParam1, pCmdData);

    return eError;
}

OMX_ERRORTYPE omx_vdec::post_event(OMX_IN ThrCmdType eCmdNative,
                                   OMX_IN OMX_U32         uParam1,
                                   OMX_IN OMX_PTR         pCmdData)
{
    ssize_t ret;
    pthread_mutex_lock(&m_pipeMutex);
    ret = write(m_cmdpipe[1], &eCmdNative, sizeof(eCmdNative));
    if(ret < 0)
    {
       logd("error: write data to pipe failed!,ret = %d",(int)ret);
       return OMX_ErrorNone;
    }
    // In case of MarkBuf, the pCmdData parameter is used to carry the data.
    // In other cases, the nParam1 parameter carries the data.
    if(eCmdNative == MarkBuf || eCmdNative == EmptyBuf || eCmdNative == FillBuf)
    {
        ret = write(m_cmddatapipe[1], &pCmdData, sizeof(OMX_PTR));
        if(ret < 0)
        {
           logd("error: write data to pipe failed!,ret = %d",(int)ret);
           return OMX_ErrorNone;
        }
    }
    else
    {
        ret = write(m_cmddatapipe[1], &uParam1, sizeof(uParam1));
        if(ret < 0)
        {
           logd("error: write data to pipe failed!,ret = %d",(int)ret);
           return OMX_ErrorNone;
        }
    }
    
    pthread_mutex_unlock(&m_pipeMutex);

    return OMX_ErrorNone;
}

static void OmxSavePictureForDebug(omx_vdec* pSelf,VideoPicture* pVideoPicture)
{
    pSelf->mPicNum ++;
    if(pSelf->mPicNum == 100)
    {
        logd("save picture: w[%d],h[%d],pVirBuf[%p]",
              (int)pSelf->m_sOutPortDefType.format.video.nFrameWidth,
              (int)pSelf->m_sOutPortDefType.format.video.nFrameHeight,
              pVideoPicture->pData0);
        
        char  path[1024] = {0};
        FILE* fpStream   = NULL;
        int   len = 0;
        
	    sprintf (path,"/data/camera/pic%d.dat",(int)pSelf->mPicNum);
	    fpStream = fopen(path, "wb");
	    len      = (pSelf->m_sOutPortDefType.format.video.nFrameWidth* pSelf->m_sOutPortDefType.format.video.nFrameHeight)*3/2;
        if(fpStream != NULL)
        {
            fwrite(pVideoPicture->pData0,1,len, fpStream);
    	    fclose(fpStream);
        }
        else
        {
            logd("++the fpStream is null when save picture");
        }
    }
    
    return;
}
static inline int OmxUnLockGpuBuffer(omx_vdec* pSelf,OMX_BUFFERHEADERTYPE* pOutBufHdr)
{
    buffer_handle_t         pBufferHandle = NULL; 

	android::GraphicBufferMapper &mapper = android::GraphicBufferMapper::get();

    //* get gpu buffer handle
    #if(CONFIG_OS_VERSION >= OPTION_OS_VERSION_ANDROID_4_4)
        logv("pSelf->m_storeOutputMetaDataFlag = %d",pSelf->m_storeOutputMetaDataFlag);
        if(pSelf->m_storeOutputMetaDataFlag ==OMX_TRUE)
        {
            VideoDecoderOutputMetaData *pMetaData = (VideoDecoderOutputMetaData*)pOutBufHdr->pBuffer;
            pBufferHandle = pMetaData->pHandle;
        }
        else
        {
            pBufferHandle = (buffer_handle_t)pOutBufHdr->pBuffer;
        }
    #elif(CONFIG_OS_VERSION == OPTION_OS_VERSION_ANDROID_4_2)
        pBufferHandle = (buffer_handle_t)pOutBufHdr->pBuffer;
    #endif
            
    if(0 != mapper.unlock(pBufferHandle))
    {
        logw("Unlock GUIBuf fail!");
    }
    return 0;
}

/*******************************************************************************
Function name: detectPipeDataToRead
Description: 
    
Parameters: 
    
Return: 
    1:data ready
    0:no data
Time: 2013/9/30
*******************************************************************************/
int OmxWaitPipeDataToRead(int nPipeFd, int nTimeUs)
{
    int                     i;
    fd_set                  rfds;
    struct timeval          timeout;
    FD_ZERO(&rfds);
    FD_SET(nPipeFd, &rfds);

    // Check for new command
    timeout.tv_sec  = 0;
    timeout.tv_usec = nTimeUs;

    i = select(nPipeFd+1, &rfds, NULL, NULL, &timeout);

    if (FD_ISSET(nPipeFd, &rfds))
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

int OmxCopyInputDataToDecoder(omx_vdec* pSelf)
{
    logi("OmxCopyInputDataToDecoder()"); 
    
	char* pBuf0;
	char* pBuf1;
	int   size0;
	int   size1;
	int   require_size;
    int   nSemVal;
    int   nRetSemGetValue;
	unsigned char*   pData;
    VideoStreamDataInfo DataInfo;
    OMX_BUFFERHEADERTYPE*   pInBufHdr   = NULL;
    
    memset(&DataInfo, 0, sizeof(VideoStreamDataInfo));

	pthread_mutex_lock(&pSelf->m_inBufMutex);
	
	pInBufHdr = pSelf->m_sInBufList.pBufHdrList[pSelf->m_sInBufList.nReadPos];

    if(pInBufHdr == NULL)
    {
        logd("(f:%s, l:%d) fatal error! pInBufHdr is NULL, check code!", __FUNCTION__, __LINE__);
		pthread_mutex_unlock(&pSelf->m_inBufMutex);
		return -1;
    }

    //* request buffer from decoder
    require_size = pInBufHdr->nFilledLen;

    //* if the size is 0, we should not copy it to decoder
    if(require_size <= 0)
        goto check_eos;
    
    if(RequestVideoStreamBuffer(pSelf->m_decoder, require_size, &pBuf0, &size0, &pBuf1, &size1,0) != 0)
    {
        logi("(f:%s, l:%d)req vbs fail! maybe vbs buffer is full! require_size[%d]", __FUNCTION__, __LINE__, require_size);
		pthread_mutex_unlock(&pSelf->m_inBufMutex);
		return -1;
    }

    if(require_size != (size0 + size1))
    {
        logw(" the requestSize[%d] is not equal to needSize[%d]!",(size0+size1),require_size);
		pthread_mutex_unlock(&pSelf->m_inBufMutex);
		return -1;
    }

    //* set data info
    DataInfo.nLength      = require_size;
    DataInfo.bIsFirstPart = 1;
    DataInfo.bIsLastPart  = 1;
    DataInfo.pData        = pBuf0;
    if(pInBufHdr->nTimeStamp >= 0)
    {
        DataInfo.nPts   = pInBufHdr->nTimeStamp;
        DataInfo.bValid = 1;
    }
    else
    {
        DataInfo.nPts   = -1;
        DataInfo.bValid = 0;
    }

    //* copy input data
    if(pSelf->mIsSecureVideoFlag == OMX_TRUE)
    {
        #if(ADJUST_ADDRESS_FOR_SECURE_OS_OPTEE)
        pData  = (unsigned char*)pInBufHdr->pBuffer;
        #else
        pData  = (unsigned char*)SecureMemAdapterGetVirtualAddressCpu(pInBufHdr->pBuffer);
        #endif
        
        pData += pInBufHdr->nOffset;
        if(require_size <= size0)
        {
            SecureMemAdapterCopy(pBuf0,pData,require_size);
        }
        else
        {
        	SecureMemAdapterCopy(pBuf0, pData, size0);
        	pData += size0;
        	SecureMemAdapterCopy(pBuf1, pData, require_size - size0);
        }
	}
	else
	{
        pData = pInBufHdr->pBuffer + pInBufHdr->nOffset;
        if(require_size <= size0)
        {
        	memcpy(pBuf0, pData, require_size);
        }
        else
        {
        	memcpy(pBuf0, pData, size0);
        	pData += size0;
        	memcpy(pBuf1, pData, require_size - size0);
        }
	}
    SubmitVideoStreamData(pSelf->m_decoder, &DataInfo,0);

check_eos:
    //* Check for EOS flag
    if (pInBufHdr->nFlags & OMX_BUFFERFLAG_EOS)
    {
        //*Copy flag to output buffer header
        pSelf->mInputEosFlag = OMX_TRUE;
        
    	logd("found eos flag in input data");

        //*Trigger event handler
        pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventBufferFlag, 0x1, pInBufHdr->nFlags, NULL);

        //*Clear flag
        pInBufHdr->nFlags = 0;
    }

    //* Check for mark buffers
    if (pInBufHdr->pMarkData)
    {
        //*Copy handle to output buffer header
        pSelf->pMarkData            = pInBufHdr->pMarkData;
        pSelf->hMarkTargetComponent = pInBufHdr->hMarkTargetComponent;
    }

    //* Trigger event handler
    if (pInBufHdr->hMarkTargetComponent == &pSelf->mOmxCmp && pInBufHdr->pMarkData)
    	pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventMark, 0, 0, pInBufHdr->pMarkData);
    
    pSelf->m_Callbacks.EmptyBufferDone(&pSelf->mOmxCmp, pSelf->m_pAppData, pInBufHdr);

    //* update m_sInBufList
	pSelf->m_sInBufList.nSizeOfList--;
    pSelf->m_sInBufList.nReadPos++;
	if (pSelf->m_sInBufList.nReadPos >= (OMX_S32)pSelf->m_sInBufList.nAllocSize)
		pSelf->m_sInBufList.nReadPos = 0;

	pthread_mutex_unlock(&pSelf->m_inBufMutex);

	/* for cts, using for synchronization between inputbuffer and outputbuffer*/
	pSelf->m_InputNum ++;
	if ((pSelf->mIsFromCts == true)/* && ((pSelf->m_InputNum - pSelf->m_OutputNum) > 3)*/) 
    {
		usleep(20000);
	}

    return 0 ;
}

void OmxDrainVideoPictureToOutputBuffer(omx_vdec* pSelf)
{
	OMX_U32      i = 0;
    int      nPicRealWidth;
    int      nPicRealHeight;
    int      nSemVal;
    int      nRetSemGetValue;
    int64_t  nTransformTimeBefore;
    int64_t  nTransformTimeAfter;
    VideoPicture*           pPicture     = NULL;
    OMX_BUFFERHEADERTYPE*   pOutBufHdr  = NULL;
    OMXOutputBufferInfoT* pOutputBufferInfo = NULL;

    if(pSelf->m_useAndroidBuffer == OMX_TRUE)
    {
        //* get the output buffer
        pPicture = RequestPicture(pSelf->m_decoder, 0);
		logv("*** get picture[%p]",pPicture);
		if(pPicture == NULL)
    	{
            logw("the pPicture is null when request displayer picture!");
            return ;
        }
		logv("*** picture info: w(%d),h(%d),offset,t(%d),b(%d),l(%d),r(%d)",
			 pPicture->nWidth,pPicture->nHeight,
			 pPicture->nTopOffset,pPicture->nBottomOffset,
			 pPicture->nLeftOffset,pPicture->nRightOffset);

		pSelf->mVideoRect.nLeft   = pPicture->nLeftOffset;
		pSelf->mVideoRect.nTop    = pPicture->nTopOffset;
		pSelf->mVideoRect.nWidth  = pPicture->nRightOffset - pPicture->nLeftOffset;
		pSelf->mVideoRect.nHeight = pPicture->nBottomOffset - pPicture->nTopOffset;

        #if(SAVE_PICTURE)
            OmxSavePictureForDebug(pSelf,pPicture);
        #endif

		for(i = 0;i<pSelf->m_sOutBufList.nAllocSize;i++)
		{
			if(pSelf->mOutputBufferInfo[i].pVideoPicture == pPicture)
				break;
		}

        if(i == pSelf->m_sOutBufList.nAllocSize)
        {
            loge("the picture request from decoder is not match");
            abort();
        }

        //* we should return buffer to decoder if it was used as set eos to render
        if(pSelf->mOutputBufferInfo[i].mUseDecoderBufferToSetEosFlag == OMX_TRUE)
        {
            logw("detect the buffer had been used to set eos to render, here, we not callback again!");
            pSelf->mOutputBufferInfo[i].mStatus = OWNED_BY_RENDER;
            pSelf->mOutputBufferInfo[i].mUseDecoderBufferToSetEosFlag = OMX_FALSE;
            return ;
        }
        
        pOutBufHdr =  pSelf->mOutputBufferInfo[i].pHeadBufInfo;

		pSelf->mOutputBufferInfo[i].mStatus = OWNED_BY_RENDER;
        
    	//* unLock gpu buffer
        #if CONFIG_OS == OPTION_OS_ANDROID       
        	OmxUnLockGpuBuffer(pSelf,pOutBufHdr);
        #endif                  

        //* set output buffer info
    	pOutBufHdr->nTimeStamp = pPicture->nPts;
    	pOutBufHdr->nOffset    = 0;
    }
    else
    {
        pPicture = NextPictureInfo(pSelf->m_decoder,0);
        
        //* we use offset to compute width and height
        nPicRealWidth  = pPicture->nRightOffset  - pPicture->nLeftOffset;
        nPicRealHeight = pPicture->nBottomOffset - pPicture->nTopOffset;

        //* if the offset is not right, we should not use them to compute width and height
        if(nPicRealWidth <= 0 || nPicRealHeight <= 0)
        {
            nPicRealWidth  = pPicture->nWidth;
            nPicRealHeight = pPicture->nHeight;
        }

        //* check whether the picture size change.
    	if((OMX_U32)nPicRealWidth != pSelf->m_sOutPortDefType.format.video.nFrameWidth 
            || (OMX_U32)nPicRealHeight != pSelf->m_sOutPortDefType.format.video.nFrameHeight)
    	{
    		logw(" video picture size changed:  org_height = %d, org_width = %d, new_height = %d, new_width = %d.",
    				(int)pSelf->m_sOutPortDefType.format.video.nFrameHeight,
    				(int)pSelf->m_sOutPortDefType.format.video.nFrameWidth,
    				(int)nPicRealHeight, (int)nPicRealWidth);

    		pSelf->m_sOutPortDefType.format.video.nFrameHeight = nPicRealHeight;
    		pSelf->m_sOutPortDefType.format.video.nFrameWidth  = nPicRealWidth;
            pSelf->m_sOutPortDefType.nBufferSize = nPicRealHeight*nPicRealWidth *3/2;
    		pSelf->bPortSettingMatchFlag = OMX_FALSE;
    		pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventPortSettingsChanged, 0x01, 0, NULL);
    		return;
    	}

        pthread_mutex_lock(&pSelf->m_outBufMutex);
        
        if(pSelf->m_sOutBufList.nSizeOfList > 0)
        {
        	pSelf->m_sOutBufList.nSizeOfList--;
        	pOutBufHdr = pSelf->m_sOutBufList.pBufHdrList[pSelf->m_sOutBufList.nReadPos++];
        	if (pSelf->m_sOutBufList.nReadPos >= (OMX_S32)pSelf->m_sOutBufList.nAllocSize)
        		pSelf->m_sOutBufList.nReadPos = 0;
        }
        else
        	pOutBufHdr = NULL;

        pthread_mutex_unlock(&pSelf->m_outBufMutex);

        if(pOutBufHdr == NULL)
        {
            OmxWaitPipeDataToRead(pSelf->m_cmdpipe[0], 10*1000);
            return;
        }

        pPicture = RequestPicture(pSelf->m_decoder, 0);

        #if (OPEN_STATISTICS)
            nTransformTimeBefore = OMX_GetNowUs();
        #endif

        OutPutBufferInfo mOutPutBufferInfo;
		mOutPutBufferInfo.pAddr   = pOutBufHdr->pBuffer;
		mOutPutBufferInfo.nWidth  = pSelf->m_sOutPortDefType.format.video.nFrameWidth;
		mOutPutBufferInfo.nHeight = pSelf->m_sOutPortDefType.format.video.nFrameHeight;

        MemAdapterFlushCache(pPicture->pData0,pPicture->nWidth*pPicture->nHeight*3/2);
        if(pSelf->mVp9orH265SoftDecodeFlag==OMX_TRUE)
            TransformYV12ToYUV420Soft(pPicture, &mOutPutBufferInfo);
        else
            TransformYV12ToYUV420(pPicture, &mOutPutBufferInfo);	// YUV420 planar

        #if (OPEN_STATISTICS)
            nTransformTimeAfter = OMX_GetNowUs();
            pSelf->mConvertTotalDuration += (nTransformTimeAfter - nTransformTimeBefore);
            pSelf->mConvertTotalCount++;
        #endif

        pOutBufHdr->nTimeStamp = pPicture->nPts;
    	pOutBufHdr->nOffset    = 0;

        ReturnPicture(pSelf->m_decoder, pPicture);
        
    }

    //* compute the lenght
    #if( CONFIG_OS == OPTION_OS_ANDROID && CONFIG_OS_VERSION>=OPTION_OS_VERSION_ANDROID_4_4)              
        if(pSelf->m_storeOutputMetaDataFlag==OMX_TRUE)
        {
            pOutBufHdr->nFilledLen = sizeof(VideoDecoderOutputMetaData);
        }
        else
        {
            pOutBufHdr->nFilledLen = pSelf->m_sOutPortDefType.format.video.nFrameWidth * pSelf->m_sOutPortDefType.format.video.nFrameHeight * 3 / 2;
        }
    #else
        pOutBufHdr->nFilledLen = pSelf->m_sOutPortDefType.format.video.nFrameWidth * pSelf->m_sOutPortDefType.format.video.nFrameHeight * 3 / 2;
    #endif

    //* Check for mark buffers
    if (pSelf->pMarkData != NULL && pSelf->hMarkTargetComponent != NULL)
    {
    	if(!ValidPictureNum(pSelf->m_decoder, 0))
    	{
    		//*Copy mark to output buffer header
    		pOutBufHdr->pMarkData = pSelf->pMarkData;
    		pOutBufHdr->hMarkTargetComponent = pSelf->hMarkTargetComponent;
    		pSelf->pMarkData = NULL;
    		pSelf->hMarkTargetComponent = NULL;
    	}
    }

    logv("****FillBufferDone is called, pOutBufHdr[%p],nSizeOfList[%d], pts[%lld], nAllocLen[%d], nFilledLen[%d], nOffset[%d], nFlags[0x%x], nOutputPortIndex[%d], nInputPortIndex[%d]",
        pOutBufHdr,(int)pSelf->m_sOutBufList.nSizeOfList,pOutBufHdr->nTimeStamp, 
        (int)pOutBufHdr->nAllocLen,(int)pOutBufHdr->nFilledLen,(int)pOutBufHdr->nOffset, 
        (int)pOutBufHdr->nFlags,(int)pOutBufHdr->nOutputPortIndex,(int)pOutBufHdr->nInputPortIndex);

	pSelf->m_OutputNum ++;
    pSelf->m_Callbacks.FillBufferDone(&pSelf->mOmxCmp, pSelf->m_pAppData, pOutBufHdr);
    pOutBufHdr = NULL;
    
    return;
}

static int OmxVideoRequestOutputBuffer(omx_vdec* pSelf,
													VideoPicture* pPicBufInfo, 
													OMX_BUFFERHEADERTYPE*   pOutBufHdr,
													OMX_BOOL mInitBufferFlag)
{
	logv("*** OmxVideoRequestOutputBuffer: mInitBufferFlag(%d)",mInitBufferFlag);
	
    OMX_U32 i;
    int mYsize;
    
    if(pOutBufHdr == NULL)
	{
		loge(" the pOutBufHdr is null when request OutPut Buffer");
		return -1;
    }
	
    //* lock the buffer!
    void* dst;
    buffer_handle_t         pBufferHandle = NULL; 

	android::GraphicBufferMapper &mapper = android::GraphicBufferMapper::get();
    android::Rect bounds((int)pSelf->m_sOutPortDefType.format.video.nFrameWidth, (int)pSelf->m_sOutPortDefType.format.video.nFrameHeight);
    
    #if (CONFIG_OS_VERSION >= OPTION_OS_VERSION_ANDROID_4_4)
        if(pSelf->m_storeOutputMetaDataFlag ==OMX_TRUE)
        {
            VideoDecoderOutputMetaData *pMetaData = (VideoDecoderOutputMetaData*)pOutBufHdr->pBuffer;
            pBufferHandle = pMetaData->pHandle;
        }
        else
        {
            pBufferHandle = (buffer_handle_t)pOutBufHdr->pBuffer;
        }
    #elif(CONFIG_OS_VERSION == OPTION_OS_VERSION_ANDROID_4_2)
        pBufferHandle = (buffer_handle_t)pOutBufHdr->pBuffer;
    #endif

    int nUsage = 0;

    if(pSelf->mIsSecureVideoFlag == OMX_TRUE)
    {
        nUsage = GRALLOC_USAGE_PROTECTED;
    }
    else
    {
        nUsage = GRALLOC_USAGE_SW_WRITE_OFTEN;
    }
        
    if(0 != mapper.lock(pBufferHandle, nUsage, bounds, &dst))
    {
        logw("Lock GUIBuf fail!");
    }

	//* init the buffer
	if(mInitBufferFlag == OMX_TRUE)
	{
								//for mali GPU
#if(GPU_TYPE_MALI == 1)
		private_handle_t* hnd = (private_handle_t *)(pBufferHandle);
#else 
		IMG_native_handle_t* hnd = (IMG_native_handle_t*)(pBufferHandle);
#endif


#if ((LINUX_VERSION == LINUX_VERSION_3_10) || (CONFIG_OS_VERSION >= OPTION_OS_VERSION_ANDROID_5_0))
					ion_user_handle_t handle_ion = 0;
#else
					struct ion_handle *handle_ion = NULL;
#endif


	    uintptr_t nPhyaddress = -1;

	    if(hnd != NULL)
	    {
#if(GPU_TYPE_MALI == 1)
			ion_import(pSelf->mIonFd, hnd->share_fd, &handle_ion);
#else
			ion_import(pSelf->mIonFd, hnd->fd[0], &handle_ion);
#endif
	    }
	    else
	    {
	        logd("the hnd is wrong : hnd = %p",hnd);
	        return -1;
	    }
	    
	    if(pSelf->mIonFd > 0)
	        nPhyaddress = ion_getphyadr(pSelf->mIonFd, handle_ion);
	    else
	    {
	        logd("the ion fd is wrong : fd = %d",(int)pSelf->mIonFd);
	        return -1;
	    }

	    nPhyaddress -= PHY_OFFSET;

		for(i = 0;i<pSelf->m_sOutBufList.nAllocSize;i++)
		{
			if(pSelf->mOutputBufferInfo[i].pHeadBufInfo == NULL)
				break;
		}
	    //lc->mRelateGpuInfo[i].pWindowBuf   = pWindowBuf;
	    pSelf->mOutputBufferInfo[i].handle_ion   = handle_ion;
	    pSelf->mOutputBufferInfo[i].pBufVirAddr  = (char*)dst;
	    pSelf->mOutputBufferInfo[i].pBufPhyAddr  = (char*)nPhyaddress;
	    pSelf->mOutputBufferInfo[i].pHeadBufInfo = pOutBufHdr;
	}

	for(i = 0;i<pSelf->m_sOutBufList.nAllocSize;i++)
	{
		if(pSelf->mOutputBufferInfo[i].pHeadBufInfo == pOutBufHdr)
			break;
	}

	if(i == pSelf->m_sOutBufList.nAllocSize)
	{
		loge("*** the buffer address is not match, bufVirAddr(%p)",dst);
		abort();
	}
    //* set the buffer address
    mYsize = pSelf->m_sOutPortDefType.format.video.nFrameWidth*pSelf->m_sOutPortDefType.format.video.nFrameHeight;
    pPicBufInfo->pData0      = pSelf->mOutputBufferInfo[i].pBufVirAddr;
    pPicBufInfo->pData1      = pPicBufInfo->pData0 + mYsize;
    pPicBufInfo->phyYBufAddr = (uintptr_t)pSelf->mOutputBufferInfo[i].pBufPhyAddr;
    pPicBufInfo->phyCBufAddr = pPicBufInfo->phyYBufAddr + mYsize;
    pPicBufInfo->nBufId      = i;
    pPicBufInfo->pPrivate    = (void*)(uintptr_t)pSelf->mOutputBufferInfo[i].handle_ion;

	pSelf->mOutputBufferInfo[i].mStatus = OWNED_BY_DECODER;
	
    if(pSelf->mIs4KAlignFlag == OMX_TRUE)
    {
        uintptr_t tmpAddr = (uintptr_t)pPicBufInfo->pData1;
        tmpAddr     = (tmpAddr + 4095) & ~4095;
        
        pPicBufInfo->pData1      = (char *)tmpAddr;
        pPicBufInfo->phyCBufAddr = (pPicBufInfo->phyCBufAddr + 4095) & ~4095;
    }
    
    return 0;
}

static inline int OmxDealWithInitData(omx_vdec* pSelf)
{
	OMX_BUFFERHEADERTYPE*	pInBufHdr  = NULL;

	pthread_mutex_lock(&pSelf->m_inBufMutex);

	pInBufHdr = pSelf->m_sInBufList.pBufHdrList[pSelf->m_sInBufList.nReadPos];

	if(pInBufHdr == NULL)
	{
		logd("(f:%s, l:%d) fatal error! pInBufHdr is NULL, check code!", __FUNCTION__, __LINE__);
		pthread_mutex_unlock(&pSelf->m_inBufMutex);
		return -1;
	}

	if(pInBufHdr->nFlags & OMX_BUFFERFLAG_CODECCONFIG)
	{
		if((pInBufHdr->nFilledLen + pSelf->mCodecSpecificDataLen) > CODEC_SPECIFIC_DATA_LENGTH)
		{
			loge("error: mCodecSpecificDataLen is too small, len[%d], requestSize[%d]",
				  CODEC_SPECIFIC_DATA_LENGTH,
				  (int)(pInBufHdr->nFilledLen + pSelf->mCodecSpecificDataLen));
			abort();
		}
		
		memcpy(pSelf->mCodecSpecificData + pSelf->mCodecSpecificDataLen,
			   pInBufHdr->pBuffer,
			   pInBufHdr->nFilledLen);
		pSelf->mCodecSpecificDataLen += pInBufHdr->nFilledLen;

		pSelf->m_sInBufList.nSizeOfList--;
		pSelf->m_sInBufList.nReadPos++;
		if (pSelf->m_sInBufList.nReadPos >= (OMX_S32)pSelf->m_sInBufList.nAllocSize)
			pSelf->m_sInBufList.nReadPos = 0;

		pthread_mutex_unlock(&pSelf->m_inBufMutex);
		
		pSelf->m_Callbacks.EmptyBufferDone(&pSelf->mOmxCmp, pSelf->m_pAppData, pInBufHdr);
	}
	else
	{
		pthread_mutex_unlock(&pSelf->m_inBufMutex);
		
		logd("++++++++++++++++pSelf->mCodecSpecificDataLen[%d]",(int)pSelf->mCodecSpecificDataLen);
		if(pSelf->mCodecSpecificDataLen > 0)
		{
			if(pSelf->m_streamInfo.pCodecSpecificData)
				free(pSelf->m_streamInfo.pCodecSpecificData);
			pSelf->m_streamInfo.nCodecSpecificDataLen = pSelf->mCodecSpecificDataLen;
			pSelf->m_streamInfo.pCodecSpecificData	  = (char*)malloc(pSelf->mCodecSpecificDataLen);
			memset(pSelf->m_streamInfo.pCodecSpecificData, 0, pSelf->mCodecSpecificDataLen);
			memcpy(pSelf->m_streamInfo.pCodecSpecificData, pSelf->mCodecSpecificData, pSelf->mCodecSpecificDataLen);
		}
		else
		{
			pSelf->m_streamInfo.pCodecSpecificData	  = NULL;
			pSelf->m_streamInfo.nCodecSpecificDataLen = 0;
		}

		post_message_to_vdrv(pSelf, OMX_VdrvCommand_PrepareVdecLib);
		logd("(f:%s, l:%d) wait for OMX_VdrvCommand_PrepareVdecLib", __FUNCTION__, __LINE__);
		sem_wait(&pSelf->m_vdrv_cmd_lock);
		logd("(f:%s, l:%d) wait for OMX_VdrvCommand_PrepareVdecLib done!", __FUNCTION__, __LINE__);
	}

	return 0;
}

static inline int OmxGetVideoFbmBufInfo(omx_vdec* pSelf)
{
	FbmBufInfo* pFbmBufInfo =  GetVideoFbmBufInfo(pSelf->m_decoder);

	logv("pFbmBufInfo = %p, m_decoder(%p)",pFbmBufInfo,pSelf->m_decoder);
	
	if(pFbmBufInfo != NULL)
	{
		logd("video buffer info: nWidth[%d],nHeight[%d],nBufferCount[%d],ePixelFormat[%d]",
              pFbmBufInfo->nBufWidth,pFbmBufInfo->nBufHeight,
              pFbmBufInfo->nBufNum,pFbmBufInfo->ePixelFormat);
        logd("video buffer info: nAlignValue[%d],bProgressiveFlag[%d],bIsSoftDecoderFlag[%d]",
              pFbmBufInfo->nAlignValue,pFbmBufInfo->bProgressiveFlag,
              pFbmBufInfo->bIsSoftDecoderFlag);
        
		if(pFbmBufInfo->nBufNum > (OMX_MAX_VIDEO_BUFFER_NUM - 2))
	        pFbmBufInfo->nBufNum = OMX_MAX_VIDEO_BUFFER_NUM - 2;

		pSelf->bPortSettingMatchFlag = OMX_FALSE;
        //*ACodec will add the mExtraOutBufferNum to nBufferCountActual,
        //*so we decrease it here
        pSelf->m_sOutPortDefType.nBufferCountMin 			= pFbmBufInfo->nBufNum - pSelf->mExtraOutBufferNum;
        pSelf->m_sOutPortDefType.nBufferCountActual 		= pFbmBufInfo->nBufNum - pSelf->mExtraOutBufferNum;
        pSelf->m_sOutPortDefType.format.video.nFrameWidth   = pFbmBufInfo->nBufWidth;
		pSelf->m_sOutPortDefType.format.video.nFrameHeight  = pFbmBufInfo->nBufHeight;
        pSelf->m_sOutPortDefType.nBufferSize = pFbmBufInfo->nBufWidth*pFbmBufInfo->nBufHeight *3/2;
		pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventPortSettingsChanged, 0x01, 0, NULL);
	}
	else
	{
		return -1;
	}

	return 0;
}

static inline int OmxSetGpuBufferToDecoder(omx_vdec* pSelf)
{
	logd("OmxSetGpuBufferToDecoder: m_sOutBufList.nSizeOfList(%d)",pSelf->m_sOutBufList.nSizeOfList);
	OMX_U32 i = 0;
	OMX_S32 mRet  = -1;
	OMX_BUFFERHEADERTYPE*  pOutBufHdr = NULL;
	VideoPicture mVideoPicture;
	memset(&mVideoPicture, 0, sizeof(VideoPicture));

	pthread_mutex_lock(&pSelf->m_outBufMutex);

	OMX_U32 mOutBufSizeOfList = pSelf->m_sOutBufList.nSizeOfList;
	if(mOutBufSizeOfList == 0)
	{
		pthread_mutex_unlock(&pSelf->m_outBufMutex);
		return -1;
	}
	
	for(i = 0; i<mOutBufSizeOfList;i++)
	{
        if(pSelf->m_sOutBufList.nSizeOfList > 0)
        {
        	pSelf->m_sOutBufList.nSizeOfList--;
        	pOutBufHdr = pSelf->m_sOutBufList.pBufHdrList[pSelf->m_sOutBufList.nReadPos++];
			if (pSelf->m_sOutBufList.nReadPos >= (OMX_S32)pSelf->m_sOutBufList.nAllocSize)
        		pSelf->m_sOutBufList.nReadPos = 0;
        }
        else
        	pOutBufHdr = NULL;

		OMX_BOOL mInitBufferFlag = OMX_TRUE;
		mRet = OmxVideoRequestOutputBuffer(pSelf, &mVideoPicture, pOutBufHdr, mInitBufferFlag);
		if(mRet == 0)
		{
			VideoPicture* pVideoPicture = NULL;
			pVideoPicture = SetVideoFbmBufAddress(pSelf->m_decoder, &mVideoPicture, 0);
			pSelf->mOutputBufferInfo[mVideoPicture.nBufId].pVideoPicture = pVideoPicture;
			pSelf->mSetToDecoderBufferNum++;
		}
		else
		{
			loge("request output buffer error!");
 		    pthread_mutex_unlock(&pSelf->m_outBufMutex);
			return -1;
		}
	}

	pthread_mutex_unlock(&pSelf->m_outBufMutex);

	return 0;
}

static inline int OmxReturnBufferToDecoder(omx_vdec* pSelf)
{
	OMX_U32 i = 0;
	int mRet = -1;
	OMX_BOOL mInitBufferFlag = OMX_FALSE;
	VideoPicture mVideoPicture;
	VideoPicture* pVideoPicture = NULL;
	OMX_BUFFERHEADERTYPE*  pOutBufHdr = NULL;
	memset(&mVideoPicture, 0, sizeof(VideoPicture));

	if(pSelf->mNeedReSetToDecoderBufferNum > 0)
	{
		pthread_mutex_lock(&pSelf->m_outBufMutex);
		
		pOutBufHdr = pSelf->m_sOutBufList.pBufHdrList[pSelf->m_sOutBufList.nReadPos];

		for(i=0; i< pSelf->m_sOutBufList.nAllocSize; i++)
		{
			if(pOutBufHdr == pSelf->mOutputBufferInfo[i].pHeadBufInfo)
				break;
		}
		
		if(i < pSelf->m_sOutBufList.nAllocSize)
			mInitBufferFlag = OMX_FALSE;
		else
			mInitBufferFlag = OMX_TRUE;

		if(mInitBufferFlag == OMX_TRUE)
		{
			if(RequestReleasePicture(pSelf->m_decoder) != NULL)
			{
				mRet = OmxVideoRequestOutputBuffer(pSelf, &mVideoPicture, pOutBufHdr, mInitBufferFlag);
				if(mRet == 0)
				{
					pSelf->mSetToDecoderBufferNum++;
					pSelf->mNeedReSetToDecoderBufferNum--;
					pVideoPicture = ReturnRelasePicture(pSelf->m_decoder, &mVideoPicture, 0);
					pSelf->mOutputBufferInfo[mVideoPicture.nBufId].pVideoPicture = pVideoPicture;
				}
				else
				{
					loge("request output buffer error!");
					abort();
				}
			}
			else
			{
				logw("** can not reqeust release picture");
				pthread_mutex_unlock(&pSelf->m_outBufMutex);
				return -1;
			}
		}
		else
		{
			mRet = OmxVideoRequestOutputBuffer(pSelf, &mVideoPicture, pOutBufHdr, mInitBufferFlag);
			if(mRet == 0)
			{
                //* we should not return to decoder again if it had been used to set eos to render
                if(pSelf->mOutputBufferInfo[i].mUseDecoderBufferToSetEosFlag == OMX_TRUE)
                {
                    pSelf->mOutputBufferInfo[i].mUseDecoderBufferToSetEosFlag = OMX_FALSE;
                    pthread_mutex_unlock(&pSelf->m_outBufMutex);
                    return 0;
                }
				pVideoPicture = pSelf->mOutputBufferInfo[i].pVideoPicture;
				ReturnPicture(pSelf->m_decoder, pVideoPicture);
			}
		}

		pSelf->m_sOutBufList.nSizeOfList--;
		pSelf->m_sOutBufList.nReadPos++;
		if (pSelf->m_sOutBufList.nReadPos >= (OMX_S32)pSelf->m_sOutBufList.nAllocSize)
				pSelf->m_sOutBufList.nReadPos = 0;

		pthread_mutex_unlock(&pSelf->m_outBufMutex);
		
	}
	else
	{
		pthread_mutex_lock(&pSelf->m_outBufMutex);
		
		if(pSelf->m_sOutBufList.nSizeOfList > 0)
		{
			pSelf->m_sOutBufList.nSizeOfList--;
			pOutBufHdr = pSelf->m_sOutBufList.pBufHdrList[pSelf->m_sOutBufList.nReadPos++];
			if (pSelf->m_sOutBufList.nReadPos >= (OMX_S32)pSelf->m_sOutBufList.nAllocSize)
				pSelf->m_sOutBufList.nReadPos = 0;
		}
		else
			pOutBufHdr = NULL;
		
		pthread_mutex_unlock(&pSelf->m_outBufMutex);
		
		if(pOutBufHdr != NULL)
		{
			for(i=0; i< pSelf->m_sOutBufList.nAllocSize; i++)
			{
				if(pOutBufHdr == pSelf->mOutputBufferInfo[i].pHeadBufInfo)
					break;
			}
			
			if(i < pSelf->m_sOutBufList.nAllocSize)
				mInitBufferFlag = OMX_FALSE;
			else
				mInitBufferFlag = OMX_TRUE;
			
			mRet = OmxVideoRequestOutputBuffer(pSelf, &mVideoPicture, pOutBufHdr, mInitBufferFlag);
			if(mRet == 0)
			{
				if(mInitBufferFlag == OMX_TRUE)
				{
					pSelf->mSetToDecoderBufferNum++;
					pVideoPicture = SetVideoFbmBufAddress(pSelf->m_decoder, &mVideoPicture, 0);
					pSelf->mOutputBufferInfo[mVideoPicture.nBufId].pVideoPicture = pVideoPicture;
					logd("*** call SetVideoFbmBufAddress: pVideoPicture(%p)",pVideoPicture);
				}
				else
				{
                    //* we should not return to decoder again if it had been used to set eos to render
                    if(pSelf->mOutputBufferInfo[i].mUseDecoderBufferToSetEosFlag == OMX_TRUE)
                    {
                        pSelf->mOutputBufferInfo[i].mUseDecoderBufferToSetEosFlag = OMX_FALSE;
                        return 0;
                    }
					pVideoPicture = pSelf->mOutputBufferInfo[i].pVideoPicture;
					ReturnPicture(pSelf->m_decoder, pVideoPicture);
				}
			}
		}
	}
	return 0;
}

static inline int OmxSetOutEos(omx_vdec* pSelf)
{
    //*set eos flag, MediaCodec use this flag
    // to determine whether a playback is finished.
    pthread_mutex_lock(&pSelf->m_outBufMutex);
    logv("*** pSelf->m_sOutBufList.nSizeOfList = %d",pSelf->m_sOutBufList.nSizeOfList);
    if(pSelf->m_sOutBufList.nSizeOfList > 0)
    {
    	while (pSelf->m_sOutBufList.nSizeOfList > 0) 
        {
            OMX_BUFFERHEADERTYPE*   pOutBufHdr  = NULL;
    		pSelf->m_sOutBufList.nSizeOfList--;
    		pOutBufHdr = pSelf->m_sOutBufList.pBufHdrList[pSelf->m_sOutBufList.nReadPos++];
    		if (pSelf->m_sOutBufList.nReadPos >= (OMX_S32)pSelf->m_sOutBufList.nAllocSize) 
            {
    			pSelf->m_sOutBufList.nReadPos = 0;
    		}

            if(pOutBufHdr==NULL)
                continue;
            
    		if (pOutBufHdr->nFilledLen != 0) 
            {
        		pSelf->m_Callbacks.FillBufferDone(&pSelf->mOmxCmp, pSelf->m_pAppData, pOutBufHdr);
        		pOutBufHdr = NULL;
    		}
    		else 
            {
                logd("++++ set output eos(normal)");
    			pOutBufHdr->nFlags |= OMX_BUFFERFLAG_EOS;
    			pSelf->m_Callbacks.FillBufferDone(&pSelf->mOmxCmp, pSelf->m_pAppData, pOutBufHdr);
        		pOutBufHdr = NULL;
        		pSelf->mVdrvNotifyEosFlag = OMX_FALSE;
        		break;
    		}
    	}
    }
    else
    {
        if(pSelf->m_useAndroidBuffer == OMX_TRUE)
        {
            OMX_U32 i = 0;
            for(i = 0; i<pSelf->m_sOutBufList.nAllocSize; i++)
            {
                if(pSelf->mOutputBufferInfo[i].mStatus == OWNED_BY_DECODER)
                    break;
            }
            if(i == pSelf->m_sOutBufList.nAllocSize)
            {
                logw("** have no buffer to set eos, try again");
                pthread_mutex_unlock(&pSelf->m_outBufMutex);
                return -1;
                //abort();
            }
            logd("*** set out eos(use buffer owned by decoder), pic = %p",
                 pSelf->mOutputBufferInfo[i].pVideoPicture);
            OMX_BUFFERHEADERTYPE* pOutBufHdr = pSelf->mOutputBufferInfo[i].pHeadBufInfo;
            pOutBufHdr->nFilledLen = 0;
            pOutBufHdr->nFlags |= OMX_BUFFERFLAG_EOS;
            #if CONFIG_OS == OPTION_OS_ANDROID       
        	OmxUnLockGpuBuffer(pSelf,pOutBufHdr);
            #endif    
            pSelf->m_Callbacks.FillBufferDone(&pSelf->mOmxCmp, pSelf->m_pAppData, pOutBufHdr);
            pSelf->mVdrvNotifyEosFlag = OMX_FALSE;
            pSelf->mOutputBufferInfo[i].mStatus = OWNED_BY_RENDER;
            pSelf->mOutputBufferInfo[i].mUseDecoderBufferToSetEosFlag = OMX_TRUE;
        }
    }
    pthread_mutex_unlock(&pSelf->m_outBufMutex);

    return 0;
}

/*
 *  Component Thread
 *    The ComponentThread function is exeuted in a separate pThread and
 *    is used to implement the actual component functions.
 */
 /*****************************************************************************/
static void* ComponentThread(void* pThreadData)
{
	int                     decodeResult;
    VideoPicture*           picture;

    int                     i;
    int                     fd1;
    fd_set                  rfds;
    OMX_U32                 cmddata;
    ThrCmdType              cmd;
    ssize_t                 readRet = -1;

    // Variables related to decoder buffer handling
    OMX_MARKTYPE*           pMarkBuf    = NULL;
    OMX_U8*                 pInBuf      = NULL;
    OMX_U32                 nInBufSize;

    // Variables related to decoder timeouts
    OMX_U32                 nTimeout;
    struct timeval          timeout;

    int64_t         nTimeUs1;
    int64_t         nTimeUs2;

    int nSemVal;
    int nRetSemGetValue;
    OMX_BOOL nResolutionChangeNativeThreadFlag = OMX_FALSE;
    
    // Recover the pointer to my component specific data
    omx_vdec* pSelf = (omx_vdec*)pThreadData;

    while (1)
    {
get_new_command:
	
        fd1 = pSelf->m_cmdpipe[0];
        FD_ZERO(&rfds);
        FD_SET(fd1, &rfds);

        //*Check for new command
        timeout.tv_sec  = 0;
        timeout.tv_usec = 0;

        i = select(pSelf->m_cmdpipe[0]+1, &rfds, NULL, NULL, &timeout);

        if (FD_ISSET(pSelf->m_cmdpipe[0], &rfds))
        {
            //*retrieve command and data from pipe
            readRet = read(pSelf->m_cmdpipe[0], &cmd, sizeof(cmd));
            if(readRet<0)
            {
                logd("error: read pipe data failed!,ret = %d",(int)readRet);
                goto EXIT;
            }
            
	        readRet = read(pSelf->m_cmddatapipe[0], &cmddata, sizeof(cmddata));
            if(readRet<0)
            {
                logd("error: read pipe data failed!,ret = %d",(int)readRet);
                goto EXIT;
            }
            
            //*State transition command
	        if (cmd == SetState)
	        {
	        	logd(" set state command, cmd = %d, cmddata = %d.", (int)cmd, (int)cmddata);
                //*If the parameter states a transition to the same state
                // raise a same state transition error.
                if (pSelf->m_state == (OMX_STATETYPE)(cmddata))
                {
                	pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventError, OMX_ErrorSameState, 0 , NULL);
                }
                else
                {
	                //*transitions/pCallbacks made based on state transition table
                    // cmddata contains the target state
	                switch ((OMX_STATETYPE)(cmddata))
	                {
             	        case OMX_StateInvalid:
             	        	pSelf->m_state = OMX_StateInvalid;
             	        	pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventError, OMX_ErrorInvalidState, 0 , NULL);
             	        	pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventCmdComplete, OMX_CommandStateSet, pSelf->m_state, NULL);
			                break;

		                case OMX_StateLoaded:
                            if (pSelf->m_state == OMX_StateIdle || pSelf->m_state == OMX_StateWaitForResources)
                            {
			                    nTimeout = 0x0;
			                    while (1)
			                    {
                                    //*Transition happens only when the ports are unpopulated
			                        if (!pSelf->m_sInPortDefType.bPopulated && !pSelf->m_sOutPortDefType.bPopulated)
			                        {
			                        	pSelf->m_state = OMX_StateLoaded;
			                        	pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventCmdComplete, OMX_CommandStateSet, pSelf->m_state, NULL);

 			                            //* close decoder
			                        	//* TODO.

				                        break;
 	                                }
				                    else if (nTimeout++ > OMX_MAX_TIMEOUTS)
				                    {
				                    	pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventError, OMX_ErrorInsufficientResources, 0 , NULL);
 	                                    logw("Transition to loaded failed\n");
				                        break;
				                    }

			                        usleep(OMX_TIMEOUT*1000);
			                    }

                                if(pSelf->mIsSecureVideoFlag == OMX_TRUE)
                                {
                                    post_message_to_vdrv(pSelf, OMX_VdrvCommand_CloseVdecLib);
                                    logd("(f:%s, l:%d) wait for OMX_VdrvCommand_CloseVdecLib", __FUNCTION__, __LINE__);
                                    sem_wait(&pSelf->m_vdrv_cmd_lock);
                                    logd("(f:%s, l:%d) wait for OMX_VdrvCommand_CloseVdecLib done!", __FUNCTION__, __LINE__);
                                }
                            }
			                else
			                {
			                	pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventError, OMX_ErrorIncorrectStateTransition, 0 , NULL);
			                }

			                break;

                        case OMX_StateIdle:
		                    if (pSelf->m_state == OMX_StateInvalid)
		                    	pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventError, OMX_ErrorIncorrectStateTransition, 0 , NULL);
		                    else
		                    {
			                    //*Return buffers if currently in pause and executing
			                    if (pSelf->m_state == OMX_StatePause || pSelf->m_state == OMX_StateExecuting)
			                    {
			                    	pthread_mutex_lock(&pSelf->m_inBufMutex);

                                    while (pSelf->m_sInBufList.nSizeOfList > 0)
                                    {
                                    	pSelf->m_sInBufList.nSizeOfList--;
                                    	pSelf->m_Callbacks.EmptyBufferDone(&pSelf->mOmxCmp,
                                    			                           pSelf->m_pAppData,
                                    			                           pSelf->m_sInBufList.pBufHdrList[pSelf->m_sInBufList.nReadPos++]);

                                    	if (pSelf->m_sInBufList.nReadPos >= pSelf->m_sInBufList.nBufArrSize)
                                    		pSelf->m_sInBufList.nReadPos = 0;
                                    }

                                    pSelf->m_sInBufList.nReadPos  = 0;
                                    pSelf->m_sInBufList.nWritePos = 0;

			                    	pthread_mutex_unlock(&pSelf->m_inBufMutex);


			                    	pthread_mutex_lock(&pSelf->m_outBufMutex);

                                    while (pSelf->m_sOutBufList.nSizeOfList > 0)
                                    {
                                    	pSelf->m_sOutBufList.nSizeOfList--;
                                    	pSelf->m_Callbacks.FillBufferDone(&pSelf->mOmxCmp,
                                    			                           pSelf->m_pAppData,
                                    			                           pSelf->m_sOutBufList.pBufHdrList[pSelf->m_sOutBufList.nReadPos++]);

                                    	if (pSelf->m_sOutBufList.nReadPos >= pSelf->m_sOutBufList.nBufArrSize)
                                    		pSelf->m_sOutBufList.nReadPos = 0;
                                    }

                                    pSelf->m_sOutBufList.nReadPos  = 0;
                                    pSelf->m_sOutBufList.nWritePos = 0;

			                    	pthread_mutex_unlock(&pSelf->m_outBufMutex);

                                    logd("****close decoder: mIsSecureVideoFlag = %d",pSelf->mIsSecureVideoFlag);
                                    if(pSelf->mIsSecureVideoFlag == OMX_FALSE)
                                    {
                                        //* We should close vdecoder here if it is Software decoder to
                                        //* avoiding decoder use output buffer after ACodec had freed all output buffer.
                                        post_message_to_vdrv(pSelf, OMX_VdrvCommand_CloseVdecLib);
                                        logd("(f:%s, l:%d) wait for close decoder ", __FUNCTION__, __LINE__);
                                        sem_wait(&pSelf->m_vdrv_cmd_lock);
                                        logd("(f:%s, l:%d) wait for close decoder done!", __FUNCTION__, __LINE__);
                                    }

                                    if(pSelf->m_useAndroidBuffer == OMX_TRUE)
                                    {
                                        OMX_U32 i;
                                        for(i = 0; i < OMX_MAX_VIDEO_BUFFER_NUM; i++)
                                        {
                                            if(pSelf->mOutputBufferInfo[i].mStatus == OWNED_BY_DECODER)
                                            {
                                                logd("fillBufferDone when quit: i[%d],pHeadBufInfo[%p],mStatus[%d]",
                                                     (int)i,pSelf->mOutputBufferInfo[i].pHeadBufInfo,
                                                     (int)pSelf->mOutputBufferInfo[i].mStatus);
                                                OmxUnLockGpuBuffer(pSelf,pSelf->mOutputBufferInfo[i].pHeadBufInfo);
                                                pSelf->m_Callbacks.FillBufferDone(&pSelf->mOmxCmp,
                                        			                           pSelf->m_pAppData,
                                        			                           pSelf->mOutputBufferInfo[i].pHeadBufInfo);
                                            }

                                            if(pSelf->mOutputBufferInfo[i].mStatus != 0)
                                                pSelf->mOutputBufferInfo[i].mStatus = OWNED_BY_RENDER;
                                        }
                                    }

			                    }
			                    else    //OMX_StateLoaded -> OMX_StateIdle
			                    {
                                    //* We init decoder here if it is secure video, 
                                    //* because we allocate input buffer rely on decoder.
                                    //* In this case we will not send CodecSpecifialData to Decoder,
                                    //* it will be ok when it is secure video.
                                    if(pSelf->mIsSecureVideoFlag == OMX_TRUE
                                       || pSelf->m_eCompressionFormat == OMX_VIDEO_CodingAVC)//* avc video also init decoder here
                                    {
                                        post_message_to_vdrv(pSelf, OMX_VdrvCommand_PrepareVdecLib);
                                        logd("(f:%s, l:%d) wait for OMX_VdrvCommand_PrepareVdecLib", __FUNCTION__, __LINE__);
                                        sem_wait(&pSelf->m_vdrv_cmd_lock);
                                        logd("(f:%s, l:%d) wait for OMX_VdrvCommand_PrepareVdecLib done!", __FUNCTION__, __LINE__);
                                    }
                                }

			                    nTimeout = 0x0;
			                    while (1)
			                    {
                                    logd("bEnabled[%d],[%d],bPopulated[%d],[%d]",
                                          pSelf->m_sInPortDefType.bEnabled,
                                          pSelf->m_sOutPortDefType.bEnabled,
                                          pSelf->m_sInPortDefType.bPopulated,
                                          pSelf->m_sOutPortDefType.bPopulated);
			                        //*Ports have to be populated before transition completes
			                        if ((!pSelf->m_sInPortDefType.bEnabled && !pSelf->m_sOutPortDefType.bEnabled)   ||
                                        (pSelf->m_sInPortDefType.bPopulated && pSelf->m_sOutPortDefType.bPopulated))
                                    {
			                        	pSelf->m_state = OMX_StateIdle;
			                        	pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventCmdComplete, OMX_CommandStateSet, pSelf->m_state, NULL);
                                        //* Open decoder
			                        	//* TODO.
 		                                break;
			                        }
			                        else if (nTimeout++ > OMX_MAX_TIMEOUTS)
			                        {
			                        	pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventError, OMX_ErrorInsufficientResources, 0 , NULL);
                		                logw("Idle transition failed\n");
	                                    break;
				                    }

			                        usleep(OMX_TIMEOUT*1000);
			                    }
		                    }

		                    break;

		                case OMX_StateExecuting:
                            //*Transition can only happen from pause or idle state
                            if (pSelf->m_state == OMX_StateIdle || pSelf->m_state == OMX_StatePause)
                            {
                                //*Return buffers if currently in pause
			                    if (pSelf->m_state == OMX_StatePause)
			                    {
			                    	pthread_mutex_lock(&pSelf->m_inBufMutex);

                                    while (pSelf->m_sInBufList.nSizeOfList > 0)
                                    {
                                    	pSelf->m_sInBufList.nSizeOfList--;
                                    	pSelf->m_Callbacks.EmptyBufferDone(&pSelf->mOmxCmp,
                                    			                           pSelf->m_pAppData,
                                    			                           pSelf->m_sInBufList.pBufHdrList[pSelf->m_sInBufList.nReadPos++]);

                                    	if (pSelf->m_sInBufList.nReadPos >= pSelf->m_sInBufList.nBufArrSize)
                                    		pSelf->m_sInBufList.nReadPos = 0;
                                    }

                                    pSelf->m_sInBufList.nReadPos  = 0;
                                    pSelf->m_sInBufList.nWritePos = 0;

			                    	pthread_mutex_unlock(&pSelf->m_inBufMutex);

			                    	pthread_mutex_lock(&pSelf->m_outBufMutex);

                                    while (pSelf->m_sOutBufList.nSizeOfList > 0)
                                    {
                                    	pSelf->m_sOutBufList.nSizeOfList--;
                                    	pSelf->m_Callbacks.FillBufferDone(&pSelf->mOmxCmp,
                                    			                           pSelf->m_pAppData,
                                    			                           pSelf->m_sOutBufList.pBufHdrList[pSelf->m_sOutBufList.nReadPos++]);

                                    	if (pSelf->m_sOutBufList.nReadPos >= pSelf->m_sOutBufList.nBufArrSize)
                                    		pSelf->m_sOutBufList.nReadPos = 0;
                                    }

                                    pSelf->m_sOutBufList.nReadPos  = 0;
                                    pSelf->m_sOutBufList.nWritePos = 0;

			                    	pthread_mutex_unlock(&pSelf->m_outBufMutex);
			                    }

			                    pSelf->m_state = OMX_StateExecuting;
			                    pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventCmdComplete, OMX_CommandStateSet, pSelf->m_state, NULL);
			                }
			                else
			                	pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventError, OMX_ErrorIncorrectStateTransition, 0 , NULL);

                            pSelf->pMarkData            = NULL;
                            pSelf->hMarkTargetComponent = NULL;

			                break;

                        case OMX_StatePause:
                            // Transition can only happen from idle or executing state
		                    if (pSelf->m_state == OMX_StateIdle || pSelf->m_state == OMX_StateExecuting)
		                    {
		                    	pSelf->m_state = OMX_StatePause;
		                    	pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventCmdComplete, OMX_CommandStateSet, pSelf->m_state, NULL);
		                    }
			                else
			                	pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventError, OMX_ErrorIncorrectStateTransition, 0 , NULL);

		                    break;

                        case OMX_StateWaitForResources:
		                    if (pSelf->m_state == OMX_StateLoaded)
		                    {
		                    	pSelf->m_state = OMX_StateWaitForResources;
		                    	pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventCmdComplete, OMX_CommandStateSet, pSelf->m_state, NULL);
			                }
			                else
			                	pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventError, OMX_ErrorIncorrectStateTransition, 0 , NULL);

		                    break;

                        default:
		                	pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventError, OMX_ErrorIncorrectStateTransition, 0 , NULL);
		                	break;

		            }
                }
	        }
	        else if (cmd == StopPort)
	        {
	        	logd(" stop port command, cmddata = %d.", (int)cmddata);
                
	            //*Stop Port(s)
	            // cmddata contains the port index to be stopped.
                // It is assumed that 0 is input and 1 is output port for this component
                // The cmddata value -1 means that both input and output ports will be stopped.
	            if (cmddata == 0x0 || (OMX_S32)cmddata == -1)
	            {
	                //*Return all input buffers
                	pthread_mutex_lock(&pSelf->m_inBufMutex);

                    while (pSelf->m_sInBufList.nSizeOfList > 0)
                    {
                    	pSelf->m_sInBufList.nSizeOfList--;
                    	pSelf->m_Callbacks.EmptyBufferDone(&pSelf->mOmxCmp,
                    			                           pSelf->m_pAppData,
                    			                           pSelf->m_sInBufList.pBufHdrList[pSelf->m_sInBufList.nReadPos++]);

                    	if (pSelf->m_sInBufList.nReadPos >= pSelf->m_sInBufList.nBufArrSize)
                    		pSelf->m_sInBufList.nReadPos = 0;
                    }

                    pSelf->m_sInBufList.nReadPos  = 0;
                    pSelf->m_sInBufList.nWritePos = 0;
                    
                	pthread_mutex_unlock(&pSelf->m_inBufMutex);

 		            //*Disable port
					pSelf->m_sInPortDefType.bEnabled = OMX_FALSE;
		        }

	            if (cmddata == 0x1 || (OMX_S32)cmddata == -1)
	            {
		            //*Return all output buffers
                	pthread_mutex_lock(&pSelf->m_outBufMutex);

                    while (pSelf->m_sOutBufList.nSizeOfList > 0)
                    {
                    	pSelf->m_sOutBufList.nSizeOfList--;
                    	pSelf->m_Callbacks.FillBufferDone(&pSelf->mOmxCmp,
                    			                           pSelf->m_pAppData,
                    			                           pSelf->m_sOutBufList.pBufHdrList[pSelf->m_sOutBufList.nReadPos++]);

                    	if (pSelf->m_sOutBufList.nReadPos >= pSelf->m_sOutBufList.nBufArrSize)
                    		pSelf->m_sOutBufList.nReadPos = 0;
                    }

                    //* return the output buffer in mOutputBufferInfo
                    if(pSelf->m_useAndroidBuffer == OMX_TRUE)
                    {
                        OMX_U32 i;
                        for(i = 0; i < OMX_MAX_VIDEO_BUFFER_NUM/*pSelf->m_sOutBufList.nAllocSize*/; i++)
                        {
                            if(pSelf->mOutputBufferInfo[i].mStatus == OWNED_BY_DECODER)
                            {
                                logd("fillBufferDone when stop: i[%d],pHeadBufInfo[%p],mStatus[%d]",
                                                     (int)i,pSelf->mOutputBufferInfo[i].pHeadBufInfo,
                                                     (int)pSelf->mOutputBufferInfo[i].mStatus);
                                OmxUnLockGpuBuffer(pSelf,pSelf->mOutputBufferInfo[i].pHeadBufInfo);
                                pSelf->m_Callbacks.FillBufferDone(&pSelf->mOmxCmp,
                        			                           pSelf->m_pAppData,
                        			                           pSelf->mOutputBufferInfo[i].pHeadBufInfo);
                            }
                        }

                        pSelf->m_sOutBufList.nReadPos  = 0;
                        pSelf->m_sOutBufList.nWritePos = 0;

    					//*we ion_free  the buffer here, we should ion_import buffer when flush-restart
    					for(i = 0; i < OMX_MAX_VIDEO_BUFFER_NUM; i++)
    	                {

    						#if(CONFIG_OS_VERSION >= OPTION_OS_VERSION_ANDROID_5_0)
    	                    if(pSelf->mOutputBufferInfo[i].handle_ion != 0)
    	                    {
    	                        logd("ion_free: handle_ion[%p],i[%d]",
    	                              pSelf->mOutputBufferInfo[i].handle_ion,i);
    	                        ion_free(pSelf->mIonFd,pSelf->mOutputBufferInfo[i].handle_ion);
    	                        pSelf->mOutputBufferInfo[i].handle_ion = 0;
    	                    }
    						#else
    						if(pSelf->mOutputBufferInfo[i].handle_ion != NULL)
    						{
    							logd("ion_free: handle_ion[%p],i[%d]",
    								  pSelf->mOutputBufferInfo[i].handle_ion,i);
    							ion_free(pSelf->mIonFd,pSelf->mOutputBufferInfo[i].handle_ion);
    							pSelf->mOutputBufferInfo[i].handle_ion = NULL;
    						}
    						#endif
    	                }

    					memset(&pSelf->mOutputBufferInfo, 0, sizeof(OMXOutputBufferInfoT)*OMX_MAX_VIDEO_BUFFER_NUM);
    					pSelf->mNeedReSetToDecoderBufferNum = 0;
    					pSelf->mSetToDecoderBufferNum       = 0;
                    }
					

                    pSelf->m_sOutBufList.nReadPos  = 0;
                    pSelf->m_sOutBufList.nWritePos = 0;
                	pthread_mutex_unlock(&pSelf->m_outBufMutex);

       	            // Disable port
					pSelf->m_sOutPortDefType.bEnabled = OMX_FALSE;
		        }

		        // Wait for all buffers to be freed
		        nTimeout = 0x0;
                OMX_U32 nPerSecond = 100;
		        while (1)
		        {
		            if (cmddata == 0x0 && !pSelf->m_sInPortDefType.bPopulated)
		            {
		                //*Return cmdcomplete event if input unpopulated
		            	pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventCmdComplete, OMX_CommandPortDisable, 0x0, NULL);
			            break;
		            }

		            if (cmddata == 0x1 && !pSelf->m_sOutPortDefType.bPopulated)
		            {
		                //*Return cmdcomplete event if output unpopulated
		            	pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventCmdComplete, OMX_CommandPortDisable, 0x1, NULL);
			            break;
		            }

		            if ((OMX_S32)cmddata == -1 &&  !pSelf->m_sInPortDefType.bPopulated && !pSelf->m_sOutPortDefType.bPopulated)
		            {
            		    //*Return cmdcomplete event if inout & output unpopulated
		            	pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventCmdComplete, OMX_CommandPortDisable, 0x0, NULL);
		            	pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventCmdComplete, OMX_CommandPortDisable, 0x1, NULL);
			            break;
		            }
		            if (nTimeout++ > (nPerSecond*60)) //* sleep 60s here, or some gts not pass when the network is slow
		            {
                        logd("callback timeout when stop port, nTimeout = %d s",nTimeout/100);
		            	pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventError, OMX_ErrorPortUnresponsiveDuringDeallocation, 0 , NULL);
       		            break;
		            }
                    //* printf the wait time per 1s
                    if(nTimeout%(nPerSecond) == 0)
                    {
                        logd("we had sleep %d s here",nTimeout/100);
                    }

                    usleep(10*1000);
		        }
	        }
	        else if (cmd == RestartPort)
	        {
	        	logd(" restart port command.pSelf->m_state[%d]", pSelf->m_state);
                
                //*Restart Port(s)
                // cmddata contains the port index to be restarted.
                // It is assumed that 0 is input and 1 is output port for this component.
                // The cmddata value -1 means both input and output ports will be restarted.

 	            // Wait for port to be populated
		        nTimeout = 0x0;
		        while (1)
		        {
                    // Return cmdcomplete event if input port populated
		            if (cmddata == 0x0 && (pSelf->m_state == OMX_StateLoaded || pSelf->m_sInPortDefType.bPopulated))
		            {
                        pSelf->m_sInPortDefType.bEnabled = OMX_TRUE;
		            	pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventCmdComplete, OMX_CommandPortEnable, 0x0, NULL);
			            break;
		            }
                    // Return cmdcomplete event if output port populated
		            else if (cmddata == 0x1 && (pSelf->m_state == OMX_StateLoaded || pSelf->m_sOutPortDefType.bPopulated))
		            {
                        pSelf->m_sOutPortDefType.bEnabled = OMX_TRUE;
		            	pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventCmdComplete, OMX_CommandPortEnable, 0x1, NULL);
                        break;
		            }
                    // Return cmdcomplete event if input and output ports populated
		            else if ((OMX_S32)cmddata == -1 && (pSelf->m_state == OMX_StateLoaded || (pSelf->m_sInPortDefType.bPopulated && pSelf->m_sOutPortDefType.bPopulated)))
		            {
                        pSelf->m_sInPortDefType.bEnabled = OMX_TRUE;
                        pSelf->m_sOutPortDefType.bEnabled = OMX_TRUE;
		            	pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventCmdComplete, OMX_CommandPortEnable, 0x0, NULL);
		            	pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventCmdComplete, OMX_CommandPortEnable, 0x1, NULL);
			            break;
		            }
		            else if (nTimeout++ > OMX_MAX_TIMEOUTS)
		            {
		            	pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventError, OMX_ErrorPortUnresponsiveDuringAllocation, 0, NULL);
                        break;
	                }

                    usleep(OMX_TIMEOUT*1000);
		        }

		        if(pSelf->bPortSettingMatchFlag == OMX_FALSE)
		        	pSelf->bPortSettingMatchFlag = OMX_TRUE;
	        }
	        else if (cmd == Flush)
	        {
	        	logd(" flush command.");
                pSelf->mIsFlushingFlag = OMX_TRUE;
	            //*Flush port(s)
                // cmddata contains the port index to be flushed.
                // It is assumed that 0 is input and 1 is output port for this component
                // The cmddata value -1 means that both input and output ports will be flushed.
                
                if(cmddata == OMX_ALL || cmddata == 0x1 || (OMX_S32)cmddata == -1)   //if request flush input and output port, we reset decoder!
                {
                    logd(" flush all port! we reset decoder!");
                    post_message_to_vdrv(pSelf, OMX_VdrvCommand_FlushVdecLib);
                    logd("(f:%s, l:%d) wait for OMX_VdrvCommand_FlushVdecLib", __FUNCTION__, __LINE__);
                    sem_wait(&pSelf->m_vdrv_cmd_lock);
                    logd("(f:%s, l:%d) wait for OMX_VdrvCommand_FlushVdecLib done!", __FUNCTION__, __LINE__);
                }
	            if (cmddata == 0x0 || (OMX_S32)cmddata == -1)
	            {
	                // Return all input buffers and send cmdcomplete
                	pthread_mutex_lock(&pSelf->m_inBufMutex);

                    while (pSelf->m_sInBufList.nSizeOfList > 0)
                    {
                    	pSelf->m_sInBufList.nSizeOfList--;
                    	pSelf->m_Callbacks.EmptyBufferDone(&pSelf->mOmxCmp,
                    			                           pSelf->m_pAppData,
                    			                           pSelf->m_sInBufList.pBufHdrList[pSelf->m_sInBufList.nReadPos++]);

                    	if (pSelf->m_sInBufList.nReadPos >= pSelf->m_sInBufList.nBufArrSize)
                    		pSelf->m_sInBufList.nReadPos = 0;
                    }

                    pSelf->m_sInBufList.nReadPos  = 0;
                    pSelf->m_sInBufList.nWritePos = 0;

                	pthread_mutex_unlock(&pSelf->m_inBufMutex);

					pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventCmdComplete, OMX_CommandFlush, 0x0, NULL);
		        }

	            if (cmddata == 0x1 || (OMX_S32)cmddata == -1)
	            {
	                // Return all output buffers and send cmdcomplete
                	pthread_mutex_lock(&pSelf->m_outBufMutex);

                    while (pSelf->m_sOutBufList.nSizeOfList > 0)
                    {
                    	pSelf->m_sOutBufList.nSizeOfList--;
                    	pSelf->m_Callbacks.FillBufferDone(&pSelf->mOmxCmp,
                    			                           pSelf->m_pAppData,
                    			                           pSelf->m_sOutBufList.pBufHdrList[pSelf->m_sOutBufList.nReadPos++]);

                    	if (pSelf->m_sOutBufList.nReadPos >= pSelf->m_sOutBufList.nBufArrSize)
                    		pSelf->m_sOutBufList.nReadPos = 0;
                    }

                    pthread_mutex_unlock(&pSelf->m_outBufMutex);

                    //* return the output buffer in mOutputBufferInfo
                    if(pSelf->m_useAndroidBuffer == OMX_TRUE)
                    {
                        OMX_U32 i;
                        for(i = 0; i < OMX_MAX_VIDEO_BUFFER_NUM; i++)
                        {
                            if(pSelf->mOutputBufferInfo[i].mStatus == OWNED_BY_DECODER)
                            {
                                logd("fillBufferDone when flush: i[%d],pHeadBufInfo[%p],mStatus[%d]",
                                                     (int)i,pSelf->mOutputBufferInfo[i].pHeadBufInfo,
                                                     (int)pSelf->mOutputBufferInfo[i].mStatus);
                                OmxUnLockGpuBuffer(pSelf,pSelf->mOutputBufferInfo[i].pHeadBufInfo);
                                pSelf->m_Callbacks.FillBufferDone(&pSelf->mOmxCmp,
                        			                           pSelf->m_pAppData,
                        			                           pSelf->mOutputBufferInfo[i].pHeadBufInfo);
                            }
    						else if(pSelf->mOutputBufferInfo[i].mStatus == OWNED_BY_RENDER
                                    && pSelf->mOutputBufferInfo[i].mUseDecoderBufferToSetEosFlag == OMX_FALSE)
    						{
    							logw("** return pic when flush,i[%d],pPic[%p]",i,pSelf->mOutputBufferInfo[i].pVideoPicture);
    							if(pSelf->mOutputBufferInfo[i].pVideoPicture != NULL)
    							{
    								ReturnPicture(pSelf->m_decoder, pSelf->mOutputBufferInfo[i].pVideoPicture);
    							}
    						}
                        }

                        pSelf->m_sOutBufList.nReadPos  = 0;
                        pSelf->m_sOutBufList.nWritePos = 0;

    					//*we ion_free  the buffer here, we should ion_import buffer when flush-restart
    					for(i = 0; i < OMX_MAX_VIDEO_BUFFER_NUM; i++)
    	                {

    						#if(CONFIG_OS_VERSION >= OPTION_OS_VERSION_ANDROID_5_0)
    	                    if(pSelf->mOutputBufferInfo[i].handle_ion != 0)
    	                    {
    	                        logd("ion_free: handle_ion[%p],i[%d]",
    	                              pSelf->mOutputBufferInfo[i].handle_ion,i);
    	                        ion_free(pSelf->mIonFd,pSelf->mOutputBufferInfo[i].handle_ion);
    	                        pSelf->mOutputBufferInfo[i].handle_ion = 0;
    	                    }
    						#else
    						if(pSelf->mOutputBufferInfo[i].handle_ion != NULL)
    						{
    							logd("ion_free: handle_ion[%p],i[%d]",
    								  pSelf->mOutputBufferInfo[i].handle_ion,i);
    							ion_free(pSelf->mIonFd,pSelf->mOutputBufferInfo[i].handle_ion);
    							pSelf->mOutputBufferInfo[i].handle_ion = NULL;
    						}
    						#endif
    	                }

    					memset(&pSelf->mOutputBufferInfo, 0, sizeof(OMXOutputBufferInfoT)*OMX_MAX_VIDEO_BUFFER_NUM);
    					pSelf->mNeedReSetToDecoderBufferNum += pSelf->mSetToDecoderBufferNum;
    					pSelf->mSetToDecoderBufferNum = 0;
                        
    					SetVideoFbmBufRelease(pSelf->m_decoder);
                    }
					
					pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventCmdComplete, OMX_CommandFlush, 0x1, NULL);
		        }

                pSelf->mIsFlushingFlag = OMX_FALSE;
	        }
	        else if (cmd == Stop)
	        {
	        	logd(" stop command.");
                post_message_to_vdrv(pSelf, OMX_VdrvCommand_Stop);
                logd("(f:%s, l:%d) wait for OMX_VdrvCommand_Stop", __FUNCTION__, __LINE__);
                sem_wait(&pSelf->m_vdrv_cmd_lock);
                logd("(f:%s, l:%d) wait for OMX_VdrvCommand_Stop done!", __FUNCTION__, __LINE__);
  		        //*Kill thread
 	            goto EXIT;
	        }
	        else if (cmd == FillBuf)
	        {
	            //*Fill buffer
	        	pthread_mutex_lock(&pSelf->m_outBufMutex);
	        	if (pSelf->m_sOutBufList.nSizeOfList < pSelf->m_sOutBufList.nAllocSize)
	        	{
	        		pSelf->m_sOutBufList.nSizeOfList++;
	        		pSelf->m_sOutBufList.pBufHdrList[pSelf->m_sOutBufList.nWritePos++] = ((OMX_BUFFERHEADERTYPE*) cmddata);

	                if (pSelf->m_sOutBufList.nWritePos >= (OMX_S32)pSelf->m_sOutBufList.nAllocSize)
	                	pSelf->m_sOutBufList.nWritePos = 0;
	        	}
	        	pthread_mutex_unlock(&pSelf->m_outBufMutex);
            }
	        else if (cmd == EmptyBuf)
	        {
                OMX_BUFFERHEADERTYPE* pTmpInBufHeader = (OMX_BUFFERHEADERTYPE*) cmddata;
                OMX_TICKS   nInterval;

                //*Empty buffer
	    	    pthread_mutex_lock(&pSelf->m_inBufMutex);
				
                if (pSelf->m_sInBufList.nSizeOfList < pSelf->m_sInBufList.nAllocSize)
	        	{
	        		pSelf->m_sInBufList.nSizeOfList++;
	        		pSelf->m_sInBufList.pBufHdrList[pSelf->m_sInBufList.nWritePos++] = ((OMX_BUFFERHEADERTYPE*) cmddata);

	                if (pSelf->m_sInBufList.nWritePos >= (OMX_S32)pSelf->m_sInBufList.nAllocSize)
	                	pSelf->m_sInBufList.nWritePos = 0;
	        	}
                
                logi("(omx_vdec, f:%s, l:%d) nTimeStamp[%lld], nAllocLen[%d], nFilledLen[%d], nOffset[%d], nFlags[0x%x], nOutputPortIndex[%d], nInputPortIndex[%d]", __FUNCTION__, __LINE__, 
                    pTmpInBufHeader->nTimeStamp, 
                    (int)pTmpInBufHeader->nAllocLen, 
                    (int)pTmpInBufHeader->nFilledLen, 
                    (int)pTmpInBufHeader->nOffset, 
                    (int)pTmpInBufHeader->nFlags, 
                    (int)pTmpInBufHeader->nOutputPortIndex, 
                    (int)pTmpInBufHeader->nInputPortIndex);
                
	    	    pthread_mutex_unlock(&pSelf->m_inBufMutex);
                
        	    //*Mark current buffer if there is outstanding command
		        if (pMarkBuf)
		        {
		            ((OMX_BUFFERHEADERTYPE *)(cmddata))->hMarkTargetComponent = &pSelf->mOmxCmp;
		            ((OMX_BUFFERHEADERTYPE *)(cmddata))->pMarkData = pMarkBuf->pMarkData;
		            pMarkBuf = NULL;
		        }
		    }
	        else if (cmd == MarkBuf)
	        {
	            if (!pMarkBuf)
	                pMarkBuf = (OMX_MARKTYPE *)(cmddata);
	        }
            else if(cmd == VdrvNotifyEos)
            {
                pSelf->mVdrvNotifyEosFlag = OMX_TRUE;
            }
            else if(cmd == VdrvResolutionChange)
            {
                //pSelf->mResolutionChangeFlag = (OMX_BOOL)cmddata;
                nResolutionChangeNativeThreadFlag = (OMX_BOOL)cmddata;
                logd(" set nResolutionChangeNativeThreadFlag to %d",(int)nResolutionChangeNativeThreadFlag);
            }
            else
            {
                logw("(f:%s, l:%d) ", __FUNCTION__, __LINE__);
            }
	    }

        //*Buffer processing
        // Only happens when the component is in executing state.
        if (pSelf->m_state == OMX_StateExecuting  &&
        	pSelf->m_sInPortDefType.bEnabled          &&
        	pSelf->m_sOutPortDefType.bEnabled         &&
        	pSelf->bPortSettingMatchFlag          &&
        	(pSelf->mResolutionChangeFlag == OMX_FALSE))
        {
        	//*1. if the first-input-data is configure-data, we should cpy to decoder
            //   as init-data. here we send commad to VdrvThread to create decoder
            if(pSelf->mHadInitDecoderFlag == OMX_FALSE && pSelf->m_sInBufList.nSizeOfList > 0)
            {
                OmxDealWithInitData(pSelf);
            }

			if(pSelf->m_decoder == NULL)
				continue;
			
			//*2. fill bitstream data to decoder first
            //while(0 == ValidPictureNum(pSelf->m_decoder, 0) && pSelf->m_sInBufList.nSizeOfList > 0)
            if(pSelf->m_sInBufList.nSizeOfList > 0)
            {
                OmxCopyInputDataToDecoder(pSelf);
            }

			if(pSelf->m_useAndroidBuffer == OMX_TRUE)
			{
	        	//*3. get video fbm buf info
				while(pSelf->mHadGetVideoFbmBufInfoFlag == OMX_FALSE)
				{
					logw("*** get video fbm buf!");
					if(OmxGetVideoFbmBufInfo(pSelf) == 0)
					{
						pSelf->mHadGetVideoFbmBufInfoFlag = OMX_TRUE;
						goto get_new_command;
					}
					else
					{
						if(1 == OmxWaitPipeDataToRead(pSelf->m_cmdpipe[0], 5*1000))
						{
							logw("*** get new command when get video fbm-buf-info");
							goto get_new_command;
						}
					}
				}

				logv("***mSetToDecoderBufferNum(%d),nAllocSize(%d),min(%d),ac[%d]",
						 pSelf->mSetToDecoderBufferNum,pSelf->m_sOutBufList.nAllocSize,
						 pSelf->m_sOutPortDefType.nBufferCountMin,
						 pSelf->m_sOutPortDefType.nBufferCountActual);
				//*4. return buffer to decoder.
				//*    we should not return buffer to decoder when detect eos, 
				//*    or we cannot callback eos to ACodec.
				if(pSelf->mVdrvNotifyEosFlag == OMX_FALSE)
				{
					while(pSelf->m_sOutBufList.nSizeOfList > 0)
					{
						if(OmxReturnBufferToDecoder(pSelf) != 0)
							break;
					}
				}
			}
			
			logv("valid picture num[%d]",ValidPictureNum(pSelf->m_decoder, 0));
            //*5. callback display picture.
            if(ValidPictureNum(pSelf->m_decoder, 0))
            {
                OmxDrainVideoPictureToOutputBuffer(pSelf);
                continue;
            }
            else
            {
                //* We must callback all frame which had decoded 
                //* when set mResolutionChangeFlag to ture
                if(nResolutionChangeNativeThreadFlag == OMX_TRUE)
                {
                    logd("set pSelf->mResolutionChangeFlag to 1");
                    pSelf->mResolutionChangeFlag = OMX_TRUE;
                    nResolutionChangeNativeThreadFlag = OMX_FALSE;
                }

                //*6. process the eos                    
            	logi("LINE %d, nVdrvNotifyEosFlag %d", __LINE__, pSelf->mVdrvNotifyEosFlag);
            	if (pSelf->mVdrvNotifyEosFlag) 
            	{
                    OmxSetOutEos(pSelf);
                }

                //* wait for 20 ms untill the pipe have data come                         
                OmxWaitPipeDataToRead(pSelf->m_cmdpipe[0], 10*1000);
            }
        }
    }

EXIT:
    return (void*)OMX_ErrorNone;
}

static void* ComponentVdrvThread(void* pThreadData)
{
	int                     decodeResult;

    int                     i;
    int                     fd1;
    fd_set                  rfds;
    OMX_VDRV_COMMANDTYPE    cmd;
    struct timeval          timeout;
    int64_t                 nTimeUs1;
    int64_t                 nTimeUs2;

    int nSemVal;
    int nRetSemGetValue;
    int nStopFlag = 0;
    OMX_BOOL nResolutionChangeVdrvThreadFlag = OMX_FALSE;
    
    // Recover the pointer to my component specific data
    omx_vdec* pSelf = (omx_vdec*)pThreadData;

    while (1)
    {
        fd1 = pSelf->m_vdrv_cmdpipe[0];
        FD_ZERO(&rfds);
        FD_SET(fd1, &rfds);
        //*Check for new command
        timeout.tv_sec  = 0;
        timeout.tv_usec = 0;

        i = select(pSelf->m_vdrv_cmdpipe[0]+1, &rfds, NULL, NULL, &timeout);

        if (FD_ISSET(pSelf->m_vdrv_cmdpipe[0], &rfds))
        {
            // retrieve command and data from pipe
            ssize_t readRet = read(pSelf->m_vdrv_cmdpipe[0], &cmd, sizeof(cmd));
            if(readRet<0)
            {
                logd("error: read pipe failed!,ret = %d",(int)readRet);
                goto EXIT;
            }
            logd("(f:%s, l:%d) vdrvThread receive cmd[0x%x]", __FUNCTION__, __LINE__, cmd);

            // State transition command
	        if (cmd == OMX_VdrvCommand_PrepareVdecLib)  //now omx_vdec's m_state = OMX_StateLoaded, OMX_StateLoaded->OMX_StateIdle
	        {
            	logd("(f:%s, l:%d)(OMX_VdrvCommand_PrepareVdecLib)", __FUNCTION__, __LINE__);

                //*if mdecoder had closed before, we should create it 
    			if(pSelf->m_decoder==NULL)
                    pSelf->m_decoder = CreateVideoDecoder();
                
            	//* set video stream info.
            	VConfig m_videoConfig;
                memset(&m_videoConfig,0,sizeof(VConfig));

                if(pSelf->m_useAndroidBuffer == OMX_TRUE)
                    m_videoConfig.bGpuBufValid = 1;
                
                if(pSelf->mIsSecureVideoFlag == OMX_TRUE)
                    m_videoConfig.bSecureosEn = 1;

                m_videoConfig.nAlignStride       = pSelf->mGpuAlignStride;
                m_videoConfig.eOutputPixelFormat = PIXEL_FORMAT_YV12;
				
                #if (CONFIG_PRODUCT == OPTION_PRODUCT_TVBOX)					
				// omx decoder make out buffer no more than 1920x1080
				if (pSelf->m_streamInfo.nWidth > 1920
					&& pSelf->m_streamInfo.nHeight > 1088)
				{
					m_videoConfig.bScaleDownEn = 1;
					m_videoConfig.nHorizonScaleDownRatio = 1;
					m_videoConfig.nVerticalScaleDownRatio = 1;
				}
				#endif
				
				if(pSelf->m_streamInfo.eCodecFormat == VIDEO_CODEC_FORMAT_WMV3)
				{
					logd("*** pSelf->m_streamInfo.bIsFramePackage to 1 when it is vc1");
					pSelf->m_streamInfo.bIsFramePackage = 1;
				}
				pSelf->m_streamInfo.bIsFrameCtsTestFlag = pSelf->mIsFromCts;

                //* mExtraOutBufferNum can not 0 when gpu and decoder share the out buffer
                if(pSelf->m_useAndroidBuffer == OMX_TRUE && pSelf->mExtraOutBufferNum == 0)
                {
                    if(pSelf->mIsSecureVideoFlag == OMX_TRUE)
                    {
                        pSelf->mExtraOutBufferNum = DISPLAY_HOLH_BUFFER_NUM_DEFAULT;
                    }
                    else
                    {
                        logd(" wait for m_semExtraOutBufferNum");
                        sem_wait(&pSelf->m_semExtraOutBufferNum);
                        logd(" wait for m_semExtraOutBufferNum done");
                    }
                }

                m_videoConfig.bDispErrorFrame = 1;
                logd("set nDisplayHoldingFrameBufferNum : %d",
                      pSelf->mExtraOutBufferNum);
                m_videoConfig.nDeInterlaceHoldingFrameBufferNum = 0;//*not use deinterlace
                m_videoConfig.nDisplayHoldingFrameBufferNum     = pSelf->mExtraOutBufferNum; //* ACodec decide the value
                m_videoConfig.nRotateHoldingFrameBufferNum      = NUM_OF_PICTURES_KEEPPED_BY_ROTATE;
                m_videoConfig.nDecodeSmoothFrameBufferNum       = NUM_OF_PICTURES_FOR_EXTRA_SMOOTH;
            	if(InitializeVideoDecoder(pSelf->m_decoder, &(pSelf->m_streamInfo),&m_videoConfig) != 0)
            	{
            		DestroyVideoDecoder(pSelf->m_decoder);
                    pSelf->m_decoder           = NULL;
                	pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventError, OMX_ErrorHardware, 0 , NULL);
                    loge("Idle transition failed, set_vstream_info() return fail.\n");
                    goto __EXECUTE_CMD_DONE;
            	}

                pSelf->mHadInitDecoderFlag = OMX_TRUE;
	        }
	        else if (cmd == OMX_VdrvCommand_CloseVdecLib)// now omx_vdec's m_state = OMX_StateLoaded, OMX_StateIdle->OMX_StateLoaded
	        {
                logd("(f:%s, l:%d)(OMX_VdrvCommand_CloseVdecLib)", __FUNCTION__, __LINE__);

                //* stop and close decoder.
                if(pSelf->m_decoder != NULL)
                {
                	DestroyVideoDecoder(pSelf->m_decoder);
                    pSelf->m_decoder           = NULL;
                    pSelf->mHadInitDecoderFlag = OMX_FALSE;
                }
                pSelf->mFirstPictureFlag = OMX_TRUE ;
                //* clear specific data
                pSelf->mCodecSpecificDataLen = 0;
                memset(pSelf->mCodecSpecificData, 0 , CODEC_SPECIFIC_DATA_LENGTH);
                pSelf->mNeedReSetToDecoderBufferNum = 0;
                pSelf->mHadGetVideoFbmBufInfoFlag   = OMX_FALSE;
	        }
	        else if (cmd == OMX_VdrvCommand_FlushVdecLib)
	        {
                logd("(f:%s, l:%d)(OMX_VdrvCommand_FlushVdecLib)", __FUNCTION__, __LINE__);
                //* we should reset the mInputEosFlag when flush vdecoder
                pSelf->mInputEosFlag = OMX_FALSE;
                if(pSelf->m_decoder)
                {
                    ResetVideoDecoder(pSelf->m_decoder);
                }
                else
                {
                    logw(" fatal error, m_decoder is not malloc when flush all ports!");
                }
	        }
	        else if (cmd == OMX_VdrvCommand_Stop)
	        {
                logd("(f:%s, l:%d)(OMX_VdrvCommand_Stop)", __FUNCTION__, __LINE__);
                nStopFlag = 1;
	        }
	        else
	        {
                logw("(f:%s, l:%d)fatal error! unknown OMX_VDRV_COMMANDTYPE[0x%x]", __FUNCTION__, __LINE__, cmd);
	        }
            
__EXECUTE_CMD_DONE:
    
            nRetSemGetValue=sem_getvalue(&pSelf->m_vdrv_cmd_lock, &nSemVal);
            if(0 == nRetSemGetValue)
            {
                if(0 == nSemVal)
                {
                    sem_post(&pSelf->m_vdrv_cmd_lock);
                }
                else
                {
                    logw("(f:%s, l:%d) fatal error, why nSemVal[%d]!=0", __FUNCTION__, __LINE__, nSemVal);
                }
            }
            else
            {
                logw("(f:%s, l:%d) fatal error, why sem getvalue[%d] fail!", __FUNCTION__, __LINE__, nRetSemGetValue);
            }
	    }
        if(nStopFlag)
        {
            logd("vdrvThread detect nStopFlag[%d], exit!", nStopFlag);
            goto EXIT;
		}

        //*Buffer processing
        // Only happens when the component is in executing state.
        if ((pSelf->m_state == OMX_StateExecuting || pSelf->m_state == OMX_StatePause)
            &&(pSelf->m_decoder!=NULL)
            &&(nResolutionChangeVdrvThreadFlag == OMX_FALSE)
            &&(pSelf->mIsFlushingFlag == OMX_FALSE))
        {
            #if (OPEN_STATISTICS)
			nTimeUs1 = OMX_GetNowUs();
            #endif
          
			decodeResult = DecodeVideoStream(pSelf->m_decoder,0,0,0,0);
            logv("***decodeResult = %d",decodeResult);
            
            #if (OPEN_STATISTICS)
            nTimeUs2 = OMX_GetNowUs();
          
            if(decodeResult == CEDARV_RESULT_FRAME_DECODED || decodeResult == CEDARV_RESULT_KEYFRAME_DECODED)
            {
                pSelf->mDecodeFrameTotalDuration += (nTimeUs2-nTimeUs1);
                pSelf->mDecodeFrameTotalCount++;
            }
            else if(decodeResult == CEDARV_RESULT_OK)
            {
                pSelf->mDecodeOKTotalDuration += (nTimeUs2-nTimeUs1);
                pSelf->mDecodeOKTotalCount++;
            }
            else if(decodeResult == CEDARV_RESULT_NO_FRAME_BUFFER)
            {
                pSelf->mDecodeNoFrameTotalDuration += (nTimeUs2-nTimeUs1);
                pSelf->mDecodeNoFrameTotalCount++;
            }
            else if(decodeResult == CEDARV_RESULT_NO_BITSTREAM)
            {
                pSelf->mDecodeNoBitstreamTotalDuration += (nTimeUs2-nTimeUs1);
                pSelf->mDecodeNoBitstreamTotalCount++;
            }
            else
            {
                pSelf->mDecodeOtherTotalDuration += (nTimeUs2-nTimeUs1);
                pSelf->mDecodeOtherTotalCount++;
            }
            #endif

			if(decodeResult == VDECODE_RESULT_KEYFRAME_DECODED ||
			   decodeResult == VDECODE_RESULT_FRAME_DECODED ||
			   decodeResult == VDECODE_RESULT_OK)
			{
			    //*do nothing
			}
            else if(decodeResult == VDECODE_RESULT_NO_FRAME_BUFFER)
            {
                //* wait for 10 ms untill the pipe have data come 
                OmxWaitPipeDataToRead(pSelf->m_vdrv_cmdpipe[0], 10*1000);
            }
			else if(decodeResult == VDECODE_RESULT_NO_BITSTREAM)
			{
                if(pSelf->mInputEosFlag == OMX_TRUE)
                {
                    pSelf->mInputEosFlag = OMX_FALSE;

                    //*set eos to decoder ,decoder should flush all fbm
                    int mRet = 0;
                    int mDecodeCount = 0;
                    while(mRet != VDECODE_RESULT_NO_BITSTREAM)
                    {
                        mRet = DecodeVideoStream(pSelf->m_decoder,1,0,0,0);
                        usleep(5*1000);
                        mDecodeCount++;
                        if(mDecodeCount > 1000)
                        {
                            logw(" decoder timeOut when set eos to decoder!");
                            break;
                        }
                    }
                    
                	//*set eof flag, MediaCodec use this flag
                	// to determine whether a playback is finished.
                	logi("(f:%s, l:%d) when eos, vdrvtask meet no_bitstream, all frames have decoded, notify ComponentThread eos!", __FUNCTION__, __LINE__);
                    pSelf->send_vdrv_feedback_msg(OMX_VdrvFeedbackMsg_NotifyEos, 0, NULL);

                    //* wait for 10 ms untill the pipe have data come
                    OmxWaitPipeDataToRead(pSelf->m_vdrv_cmdpipe[0], 10*1000);
                }
                else
                {
                    //* wait for 10 ms untill the pipe have data come
                    OmxWaitPipeDataToRead(pSelf->m_vdrv_cmdpipe[0], 10*1000);
                }
			}
            else if(decodeResult == VDECODE_RESULT_RESOLUTION_CHANGE)
            {
                logd("set nResolutionChangeVdrvThreadFlag to 1, decode detect resolution change");
                nResolutionChangeVdrvThreadFlag = OMX_TRUE;
                pSelf->send_vdrv_feedback_msg(OMX_VdrvFeedbackMsg_ResolutionChange, 1, NULL);
            }
			else if(decodeResult < 0)
			{
                logw("decode fatal error[%d]", decodeResult);
				//* report a fatal error.
                pSelf->m_Callbacks.EventHandler(&pSelf->mOmxCmp, pSelf->m_pAppData, OMX_EventError, OMX_ErrorHardware, 0, NULL);
				//* wait for 20 ms untill the pipe have data come 
            	OmxWaitPipeDataToRead(pSelf->m_vdrv_cmdpipe[0], 20*1000);
				//break;
			}
            else
            {
                logd("decode ret[%d], ignore? why?", decodeResult);
            }
        }
        else
        {
            if(pSelf->mResolutionChangeFlag==OMX_TRUE)
            {
                logd("***ReopenVideoEngine!");
                VConfig m_videoConfig;
                memset(&m_videoConfig,0,sizeof(VConfig));

                if(pSelf->m_useAndroidBuffer == OMX_TRUE)
                    m_videoConfig.bGpuBufValid = 1;
                
                if(pSelf->mIsSecureVideoFlag == OMX_TRUE)
                    m_videoConfig.bSecureosEn = 1;

                m_videoConfig.nAlignStride       = pSelf->mGpuAlignStride;
                m_videoConfig.eOutputPixelFormat = PIXEL_FORMAT_YV12;
                m_videoConfig.bDispErrorFrame = 1;

                //for resolution changed. Be careful!
                {
                    m_videoConfig.nDeInterlaceHoldingFrameBufferNum = 0;//*not use deinterlace
                    m_videoConfig.nDisplayHoldingFrameBufferNum     = pSelf->mExtraOutBufferNum;
                    m_videoConfig.nRotateHoldingFrameBufferNum      = NUM_OF_PICTURES_KEEPPED_BY_ROTATE;
                    m_videoConfig.nDecodeSmoothFrameBufferNum       = NUM_OF_PICTURES_FOR_EXTRA_SMOOTH;
                }

                #if (CONFIG_PRODUCT == OPTION_PRODUCT_TVBOX)					
				// omx decoder make out buffer no more than 1920x1080
				if (pSelf->m_streamInfo.nWidth > 1920
					&& pSelf->m_streamInfo.nHeight > 1088)
				{
					m_videoConfig.bScaleDownEn = 1;
					m_videoConfig.nHorizonScaleDownRatio = 1;
					m_videoConfig.nVerticalScaleDownRatio = 1;
				}
				#endif

                if(pSelf->m_streamInfo.pCodecSpecificData)
                    free(pSelf->m_streamInfo.pCodecSpecificData);

                pSelf->m_streamInfo.pCodecSpecificData = NULL;
                pSelf->m_streamInfo.nCodecSpecificDataLen = 0;
                
                ReopenVideoEngine(pSelf->m_decoder, &m_videoConfig, &(pSelf->m_streamInfo));
                pSelf->mHadGetVideoFbmBufInfoFlag = OMX_FALSE;
                pSelf->mResolutionChangeFlag      = OMX_FALSE;
                nResolutionChangeVdrvThreadFlag   = OMX_FALSE;
                pSelf->mFirstPictureFlag          = OMX_TRUE;
            }
            
            //* wait for 20 ms untill the pipe have data come 
            OmxWaitPipeDataToRead(pSelf->m_vdrv_cmdpipe[0], 20*1000);
        }
    }

EXIT:
    logd("***notify: exit the componentVdrvThread!");
    return (void*)OMX_ErrorNone;
}

