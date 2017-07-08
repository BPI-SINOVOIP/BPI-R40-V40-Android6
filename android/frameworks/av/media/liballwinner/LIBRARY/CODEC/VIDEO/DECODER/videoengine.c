
#include <unistd.h>
#include <stdlib.h>
#include <memory.h>
#include <malloc.h>
#include <dlfcn.h>
#include <errno.h>
#include <dirent.h>
#include "videoengine.h"
#include "log.h"
#include "veregister.h"
#include "config.h"
#include "vdecoder_config.h"
#include "DecoderList.h"
#include "DecoderTypes.h"

#define DEBUG_SCLALE_DOWN 0
unsigned int gVeVersion;
extern const char* strCodecFormat[];

static DecoderInterface* CreateSpecificDecoder(VideoEngine* p);
static void EnableVeSpecificDecoder(VideoEngine* p);
static void DisableVeSpecificDecoder(VideoEngine* p);
static void EnableJpegVeSpecificDecoder(VideoEngine* p);
static void DisableJpegVeSpecificDecoder(VideoEngine* p);

struct VDecoderNodeS
{
    DecoderListNodeT node;
    VDecoderCreator *creator;
    int  bIsSoftDecoder;
    char desc[64]; /* specific by mime type */
    enum EVIDEOCODECFORMAT format;

};

struct VDecoderListS
{
    DecoderListT list;
    int size;
    pthread_mutex_t mutex;
};

static struct VDecoderListS gVDecoderList = {{NULL, NULL}, 0, PTHREAD_MUTEX_INITIALIZER};

int VDecoderRegister(enum EVIDEOCODECFORMAT format, char *desc, VDecoderCreator *creator, int bIsSoft)
{
    struct VDecoderNodeS *newVDNode = NULL, *posVDNode = NULL;

    if(desc == NULL)
    {
    	loge("register decoder, type name == NULL");
    	return -1;
    }
    if(strlen(desc) > 63)
    {
        loge("type name '%s' too long", desc);
        return -1;
    }

    if (gVDecoderList.size == 0)
    {
        DecoderListInit(&gVDecoderList.list);
        pthread_mutex_init(&gVDecoderList.mutex, NULL);
    }

    pthread_mutex_lock(&gVDecoderList.mutex);

    /* check if conflict */
    DecoderListForEachEntry(posVDNode, &gVDecoderList.list, node)
    {
        if (posVDNode->format == format && strcmp(posVDNode->desc, desc) == 0)
        {
            loge("Add '%x:%s' fail! '%x:%s' already register!", format, desc, format, posVDNode->desc);
            return -1;
        }
    }
    logv("1117 register %x:%s", format, desc);
    newVDNode = malloc(sizeof(*newVDNode));
    newVDNode->creator = creator;
    strncpy(newVDNode->desc, desc, 63);
    newVDNode->format = format;
    newVDNode->bIsSoftDecoder = bIsSoft;

    DecoderListAdd(&newVDNode->node, &gVDecoderList.list);
    gVDecoderList.size++;

    pthread_mutex_unlock(&gVDecoderList.mutex);
    logw("register codec: '%x:%s' success.", format, desc);
    return 0;
}


void VideoInitTopRegister(void)
{

    vetop_reglist_t* vetop_reg_list;
    vetop_reg_list = (vetop_reglist_t*)ve_get_reglist(REG_GROUP_VETOP);

	vetop_reg_list->_c4_pri_chroma_buf_len.pri_chroma_buf_len = 0;
	vetop_reg_list->_c8_pri_buf_line_stride.pri_luma_line_stride = 0;
	vetop_reg_list->_c8_pri_buf_line_stride.pri_chroma_line_stride = 0;

	vetop_reg_list->_cc_sec_buf_line_stride.sec_luma_line_stride = 0;
    vetop_reg_list->_cc_sec_buf_line_stride.sec_chroma_line_stride = 0;

    vetop_reg_list->_e8_chroma_buf_len0.chroma_align_mode = 0;
	vetop_reg_list->_e8_chroma_buf_len0.luma_align_mode  = 0;
    vetop_reg_list->_e8_chroma_buf_len0.output_data_format = 0;
	vetop_reg_list->_e8_chroma_buf_len0.sdrt_chroma_buf_len = 0;

	vetop_reg_list->_ec_pri_output_format.primary_output_format = 0;
    vetop_reg_list->_ec_pri_output_format.second_special_output_format=0;

}


void SetVeTopLevelRegisters(VideoEngine* p)
{
    vetop_reglist_t* vetop_reg_list;

    vetop_reg_list = (vetop_reglist_t*)ve_get_reglist(REG_GROUP_VETOP);

#if CONFIG_USE_TIMEOUT_CONTROL

    //* set maximum cycle count for decoding one frame.
    vetop_reg_list->_0c_overtime.overtime_value = CONFIG_TIMEOUT_VALUE;

#if RV_CONFIG_USE_INTRRUPT
    //* enable timeout interrupt.
    vetop_reg_list->_1c_status.timeout_enable = 1;
#endif

#else   //* #if CONFIG_USE_TIMEOUT_CONTROL

    //* disable timeout interrupt.
    vetop_reg_list->_1c_status.timeout_enable = 0;

#endif   //* #if CONFIG_USE_TIMEOUT_CONTROL

	if (gVeVersion == 0x1619)
    	vetop_reg_list->_1c_status.graphic_intr_enable = 0;	//* disable graphic interrupt.

	VeDecoderWidthMode(p->videoStreamInfo.nWidth);
	VeEnableDecoder(p->videoStreamInfo.eCodecFormat);
	VideoInitTopRegister();
}

void SetJpegVeTopLevelRegisters(VideoEngine* p)
{
    vetop_reglist_t* vetop_reg_list;

    vetop_reg_list = (vetop_reglist_t*)ve_get_reglist(REG_GROUP_VETOP);

#if CONFIG_USE_TIMEOUT_CONTROL

    //* set maximum cycle count for decoding one frame.
    vetop_reg_list->_0c_overtime.overtime_value = CONFIG_TIMEOUT_VALUE;

#if RV_CONFIG_USE_INTRRUPT
    //* enable timeout interrupt.
    vetop_reg_list->_1c_status.timeout_enable = 1;
#endif

#else   //* #if CONFIG_USE_TIMEOUT_CONTROL

    //* disable timeout interrupt.
    vetop_reg_list->_1c_status.timeout_enable = 0;

#endif   //* #if CONFIG_USE_TIMEOUT_CONTROL

	if (gVeVersion == 0x1619)
    	vetop_reg_list->_1c_status.graphic_intr_enable = 0;	//* disable graphic interrupt.

	VeDecoderWidthMode(p->videoStreamInfo.nWidth);
	VeEnableJpegDecoder();
//	if (gVeVersion != 0x1681)
//		VideoInitTopRegister();
}

void ResetVeInternal(VideoEngine* p)
{
    volatile unsigned int dwVal;
    int i;
    vetop_reglist_t* vetop_reg_list;

    DisableVeSpecificDecoder(p);
   	VeResetDecoder();
	EnableVeSpecificDecoder(p);
    SetVeTopLevelRegisters(p);
}

void ResetJpegVeInternal(VideoEngine* p)
{
    volatile unsigned int dwVal;
    int i;
    vetop_reglist_t* vetop_reg_list;

    DisableJpegVeSpecificDecoder(p);
   	VeResetJpegDecoder();
	EnableJpegVeSpecificDecoder(p);
    SetJpegVeTopLevelRegisters(p);
}



static void EnableVeSpecificDecoder(VideoEngine* p)
{
    if(p->bIsSoftwareDecoder == 0)
    {
    	VeEnableDecoder(p->videoStreamInfo.eCodecFormat);
    }
    return;
}


static void DisableVeSpecificDecoder(VideoEngine* p)
{
    if(p->bIsSoftwareDecoder == 0)
    {
   		VeDisableDecoder();
    }
    return;
}

static void EnableJpegVeSpecificDecoder(VideoEngine* p)
{
    if(p->bIsSoftwareDecoder == 0)
    {
   		VeEnableJpegDecoder();
    }
    return;
}

static void DisableJpegVeSpecificDecoder(VideoEngine* p)
{
    if(p->bIsSoftwareDecoder == 0)
    {
    	VeDisableJpegDecoder();
    }
    return;
}

VideoEngine* VideoEngineCreate(VConfig* pVConfig, VideoStreamInfo* pVideoInfo)
{
    int          ret;
    VideoEngine* pEngine;
    
    pEngine = (VideoEngine*)malloc(sizeof(VideoEngine));
    if(pEngine == NULL)
    {
        loge("memory alloc fail, VideoEngineCreate() return fail.");
        return pEngine;
    }
    memset(pEngine, 0, sizeof(VideoEngine));
    memcpy(&pEngine->vconfig, pVConfig, sizeof(VConfig));
    memcpy(&pEngine->videoStreamInfo, pVideoInfo, sizeof(VideoStreamInfo));
    
    if(pVideoInfo->nCodecSpecificDataLen > 0 && pVideoInfo->pCodecSpecificData != NULL)
    {
        pEngine->videoStreamInfo.pCodecSpecificData = (char*)malloc(pVideoInfo->nCodecSpecificDataLen);
        if(pEngine->videoStreamInfo.pCodecSpecificData == NULL)
        {
            loge("memory alloc fail, allocate %d bytes, VideoEngineCreate() return fail.",
                    pVideoInfo->nCodecSpecificDataLen);
            free(pEngine);
            return NULL;
        }
        memcpy(pEngine->videoStreamInfo.pCodecSpecificData, 
               pVideoInfo->pCodecSpecificData,
               pVideoInfo->nCodecSpecificDataLen);
    }
    

    //* save the ve version.
    {
        unsigned char* ptr = AdapterVeGetBaseAddress();

        ptr += 0xf0;

        gVeVersion = *((unsigned int*)ptr);
        gVeVersion >>= 16;
    }

    pEngine->pDecoderInterface = CreateSpecificDecoder(pEngine);
    if(pEngine->pDecoderInterface == NULL)
    {
        loge("unsupported format %s", strCodecFormat[pVideoInfo->eCodecFormat-VIDEO_CODEC_FORMAT_MIN]);
        if(pEngine->videoStreamInfo.pCodecSpecificData != NULL &&
            pEngine->videoStreamInfo.nCodecSpecificDataLen > 0)
            free(pEngine->videoStreamInfo.pCodecSpecificData);
        free(pEngine);
        return NULL;
    }
    
    if(pEngine->bIsSoftwareDecoder == 0)
    {
        //* reset ve hardware and set up top level registers.
    	if((gVeVersion == 0x1681 || gVeVersion == 0x1689) && pEngine->videoStreamInfo.eCodecFormat == VIDEO_CODEC_FORMAT_MJPEG)
    	{
			ResetJpegVeInternal(pEngine);
			SetJpegVeTopLevelRegisters(pEngine);
    	}else
    	{
			ResetVeInternal(pEngine);
			SetVeTopLevelRegisters(pEngine);
    	}
        if(pEngine->vconfig.eOutputPixelFormat == PIXEL_FORMAT_DEFAULT)
        {
        	pEngine->vconfig.eOutputPixelFormat = PIXEL_FORMAT_YUV_MB32_420;
        	if(gVeVersion >= 0x1667)
        	{   
                //* on chip-1673-pad, we should set to NV21 to fix the rotateion-device
#if (CONFIG_PRODUCT == OPTION_PRODUCT_PAD)
				if (gVeVersion == 0x1673)
				{
       		    	pEngine->vconfig.eOutputPixelFormat = PIXEL_FORMAT_NV21;
				}
				else
				{
                   	pEngine->vconfig.eOutputPixelFormat = PIXEL_FORMAT_YV12;
				}
#else
                pEngine->vconfig.eOutputPixelFormat = PIXEL_FORMAT_YV12;
#endif
        	}
        }
    }else
    {
    	if ((gVeVersion == 0x1667 || gVeVersion == 0x1673) && pEngine->videoStreamInfo.eCodecFormat == VIDEO_CODEC_FORMAT_H265 && pEngine->vconfig.bThumbnailMode==0)
    		pEngine->vconfig.eOutputPixelFormat = PIXEL_FORMAT_NV21;
    }

    pEngine->vconfig.bIsSoftDecoderFlag = pEngine->bIsSoftwareDecoder;
    //* call specific function to open decoder.
    if((gVeVersion == 0x1681 || gVeVersion == 0x1689) && pEngine->videoStreamInfo.eCodecFormat == VIDEO_CODEC_FORMAT_MJPEG)
    	EnableJpegVeSpecificDecoder(pEngine);
    else
    	EnableVeSpecificDecoder(pEngine);
    pEngine->fbmInfo.bIs3DStream = pEngine->videoStreamInfo.bIs3DStream;   // added by xyliu
    pEngine->fbmInfo.bIsFrameCtsTestFlag = pEngine->videoStreamInfo.bIsFrameCtsTestFlag;//cts 
    pEngine->fbmInfo.nExtraFbmBufferNum = pEngine->vconfig.nDeInterlaceHoldingFrameBufferNum +
                                          pEngine->vconfig.nDisplayHoldingFrameBufferNum +
                                          pEngine->vconfig.nRotateHoldingFrameBufferNum +
                                          pEngine->vconfig.nDecodeSmoothFrameBufferNum;
	if(pEngine->fbmInfo.nExtraFbmBufferNum > 16)
		pEngine->fbmInfo.nExtraFbmBufferNum = 16;
	if(pEngine->fbmInfo.nExtraFbmBufferNum < 0)
    {
        pEngine->fbmInfo.nExtraFbmBufferNum = 0;
        logw(" extra fbm buffer == 0 ");
    }
#if DEBUG_SCLALE_DOWN
    {
        pEngine->vconfig.bScaleDownEn = 1;
        pEngine->vconfig.nHorizonScaleDownRatio = 1;
        pEngine->vconfig.nVerticalScaleDownRatio = 1;
    }
#endif
    ret = pEngine->pDecoderInterface->Init(pEngine->pDecoderInterface,
                                           &pEngine->vconfig,
                                           &pEngine->videoStreamInfo,
                                           &pEngine->fbmInfo);
    if((gVeVersion == 0x1681 || gVeVersion == 0x1689) && pEngine->videoStreamInfo.eCodecFormat == VIDEO_CODEC_FORMAT_MJPEG)
    	DisableJpegVeSpecificDecoder(pEngine);
    else
    	DisableVeSpecificDecoder(pEngine);
    
    if(ret != VDECODE_RESULT_OK)
    {
        loge("initial specific decoder fail.");
        
        if(pEngine->videoStreamInfo.pCodecSpecificData != NULL &&
            pEngine->videoStreamInfo.nCodecSpecificDataLen > 0)
            free(pEngine->videoStreamInfo.pCodecSpecificData);
        
        free(pEngine);
        return NULL;
    }
    
    return pEngine;
}


void VideoEngineDestroy(VideoEngine* pVideoEngine)
{
    //* close specific decoder.
	if((gVeVersion==0x1681 || gVeVersion == 0x1689) && (pVideoEngine->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
		EnableJpegVeSpecificDecoder(pVideoEngine);
	else
		EnableVeSpecificDecoder(pVideoEngine);
    pVideoEngine->pDecoderInterface->Destroy(pVideoEngine->pDecoderInterface);
    if((gVeVersion==0x1681 || gVeVersion == 0x1689) && (pVideoEngine->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
    	DisableJpegVeSpecificDecoder(pVideoEngine);
    else
    	DisableVeSpecificDecoder(pVideoEngine);
    
    //* free codec specific data.
    if(pVideoEngine->videoStreamInfo.pCodecSpecificData != NULL &&
        pVideoEngine->videoStreamInfo.nCodecSpecificDataLen > 0)
        free(pVideoEngine->videoStreamInfo.pCodecSpecificData);
    
    //* if use other decoder library, close the library.
    if(pVideoEngine->pLibHandle != NULL)
        dlclose(pVideoEngine->pLibHandle);
    
    free(pVideoEngine);
    
    return;
}


void VideoEngineReset(VideoEngine* pVideoEngine)
{
	if((gVeVersion==0x1681 || gVeVersion == 0x1689) && (pVideoEngine->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
		EnableJpegVeSpecificDecoder(pVideoEngine);
	else
		EnableVeSpecificDecoder(pVideoEngine);
    pVideoEngine->pDecoderInterface->Reset(pVideoEngine->pDecoderInterface);
    if((gVeVersion==0x1681 || gVeVersion == 0x1689) && (pVideoEngine->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
    	DisableJpegVeSpecificDecoder(pVideoEngine);
    else
    	DisableVeSpecificDecoder(pVideoEngine);
    return;
}


int VideoEngineSetSbm(VideoEngine* pVideoEngine, Sbm* pSbm, int nIndex)
{
    int ret;
	if((gVeVersion==0x1681 || gVeVersion == 0x1689) && (pVideoEngine->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
		EnableJpegVeSpecificDecoder(pVideoEngine);
	else
		EnableVeSpecificDecoder(pVideoEngine);
    ret = pVideoEngine->pDecoderInterface->SetSbm(pVideoEngine->pDecoderInterface,
                                                  pSbm,
                                                  nIndex);
    if((gVeVersion==0x1681 || gVeVersion == 0x1689) && (pVideoEngine->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
    	DisableJpegVeSpecificDecoder(pVideoEngine);
    else
    	DisableVeSpecificDecoder(pVideoEngine);
    return ret;
}

int VideoEngineGetFbmNum(VideoEngine* pVideoEngine)
{
    int ret;
   // EnableVeSpecificDecoder(pVideoEngine);
    ret = pVideoEngine->pDecoderInterface->GetFbmNum(pVideoEngine->pDecoderInterface);
   // DisableVeSpecificDecoder(pVideoEngine);
    return ret;
}

Fbm* VideoEngineGetFbm(VideoEngine* pVideoEngine, int nIndex)
{
    Fbm* pFbm;
  //  EnableVeSpecificDecoder(pVideoEngine);
    pFbm = pVideoEngine->pDecoderInterface->GetFbm(pVideoEngine->pDecoderInterface, nIndex);
  //  DisableVeSpecificDecoder(pVideoEngine);
    return pFbm;
}

#define VIDEO_ENGINE_CREATE_DECODER(lib,function)   \
{                                                           \
    do{                                                     \
        FUNC_CREATE_DECODER pFunc;                          \
        p->pLibHandle = dlopen(lib, RTLD_NOW);   \
        if(p->pLibHandle == NULL)                           \
        {                                                   \
            pInterface = NULL;                              \
            break;                                          \
        }                                                   \
        pFunc = (FUNC_CREATE_DECODER)dlsym(p->pLibHandle, function);\
        pInterface = pFunc(p);                              \
    }while(0);                                              \
}

int VideoEngineDecode(VideoEngine* pVideoEngine,
                      int          bEndOfStream,
                      int          bDecodeKeyFrameOnly,
                      int          bDropBFrameIfDelay,
                      int64_t      nCurrentTimeUs)
{
    int ret;
	if((gVeVersion==0x1681 || gVeVersion == 0x1689) && (pVideoEngine->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
		EnableJpegVeSpecificDecoder(pVideoEngine);
	else
		EnableVeSpecificDecoder(pVideoEngine);
    ret = pVideoEngine->pDecoderInterface->Decode(pVideoEngine->pDecoderInterface,
                                                  bEndOfStream,
                                                  bDecodeKeyFrameOnly,
                                                  bDropBFrameIfDelay,
                                                  nCurrentTimeUs);
    if((gVeVersion==0x1681 || gVeVersion == 0x1689) && (pVideoEngine->videoStreamInfo.eCodecFormat==VIDEO_CODEC_FORMAT_MJPEG))
    	DisableJpegVeSpecificDecoder(pVideoEngine);
    else
    	DisableVeSpecificDecoder(pVideoEngine);
    return ret;
}


typedef DecoderInterface* (*FUNC_CREATE_DECODER)(VideoEngine*);

#if 0
static DecoderInterface* CreateSpecificDecoder(VideoEngine* p)
{
    DecoderInterface* pInterface = NULL;

    p->bIsSoftwareDecoder = 0;
    switch(p->videoStreamInfo.eCodecFormat)
    {
        case VIDEO_CODEC_FORMAT_MJPEG:
        {
            if(CONFIG_ENABLE_VIDEO_DECODER_MJPEG)
            {
            	if (gVeVersion == 0x1681 || gVeVersion == 0x1689)
            	{
                    VIDEO_ENGINE_CREATE_DECODER("libawmjpegplus.so", "CreateMjpegPlusDecoder");
            	}
            	else
            	{
                    VIDEO_ENGINE_CREATE_DECODER("libawmjpeg.so", "CreateMjpegDecoder");
            	}
                p->fbmInfo.nDecoderNeededMiniFbmNum = MJPEG_DECODER_NEEDED_FRAME_NUM;
                p->fbmInfo.nDecoderNeededMiniFbmNumSD = MJPEG_DECODER_NEEDED_FRAME_NUM;
            }
            break;
        }
        case VIDEO_CODEC_FORMAT_H264:
        {
            if(CONFIG_ENABLE_VIDEO_DECODER_H264)
            {
                VIDEO_ENGINE_CREATE_DECODER("libawh264.so", "CreateH264Decoder");
                p->fbmInfo.nDecoderNeededMiniFbmNum = H264_DECODER_NEEDED_FRAME_NUM_MINUS_REF;
                p->fbmInfo.nDecoderNeededMiniFbmNumSD = H264_DISP_NEEDED_FRAME_NUM_SCALE_DOWN;
            }
            break;
        }
        case VIDEO_CODEC_FORMAT_MPEG1:
        {
            if(CONFIG_ENABLE_VIDEO_DECODER_MPEG1)
            {
                VIDEO_ENGINE_CREATE_DECODER("libawmpeg2.so", "CreateMpeg2Decoder");
                p->fbmInfo.nDecoderNeededMiniFbmNum = MPEG2_DECODER_NEEDED_FRAME_NUM;
                p->fbmInfo.nDecoderNeededMiniFbmNumSD = MPEG2_DECODER_NEEDED_FRAME_NUM;
            }
            break;
        }

        case VIDEO_CODEC_FORMAT_MPEG2:
        {
            if(CONFIG_ENABLE_VIDEO_DECODER_MPEG2)
            {
                VIDEO_ENGINE_CREATE_DECODER("libawmpeg2.so", "CreateMpeg2Decoder");
                p->fbmInfo.nDecoderNeededMiniFbmNum = MPEG2_DECODER_NEEDED_FRAME_NUM;
                p->fbmInfo.nDecoderNeededMiniFbmNumSD = MPEG2_DECODER_NEEDED_FRAME_NUM;
            }
            break;
        }

        case VIDEO_CODEC_FORMAT_MPEG4:
        {
            if(CONFIG_ENABLE_VIDEO_DECODER_MPEG4)
            {
                VIDEO_ENGINE_CREATE_DECODER("libawmpeg4h263.so", "CreateMpeg4H263Decoder");
                p->fbmInfo.nDecoderNeededMiniFbmNum = MPEG4_DECODER_NEEDED_FRAME_NUM;
                p->fbmInfo.nDecoderNeededMiniFbmNumSD = MPEG4_DECODER_NEEDED_FRAME_NUM;
            }
            break;
        }

        case VIDEO_CODEC_FORMAT_MSMPEG4V1:
        {
            if(CONFIG_ENABLE_VIDEO_DECODER_MSMPEG4V1)
            {
                VIDEO_ENGINE_CREATE_DECODER("libawmpeg4divx311.so", "CreateMpeg4Divx311Decoder");
                p->fbmInfo.nDecoderNeededMiniFbmNum = MPEG4_DECODER_NEEDED_FRAME_NUM;
                p->fbmInfo.nDecoderNeededMiniFbmNumSD = MPEG4_DECODER_NEEDED_FRAME_NUM;
            }
            break;
        }

        case VIDEO_CODEC_FORMAT_MSMPEG4V2:
        {
            if(CONFIG_ENABLE_VIDEO_DECODER_MSMPEG4V2)
            {
                VIDEO_ENGINE_CREATE_DECODER("libawmpeg4divx311.so", "CreateMpeg4Divx311Decoder");
                p->fbmInfo.nDecoderNeededMiniFbmNum = MPEG4_DECODER_NEEDED_FRAME_NUM;
                p->fbmInfo.nDecoderNeededMiniFbmNumSD = MPEG4_DECODER_NEEDED_FRAME_NUM;
            }
            break;
        }

        case VIDEO_CODEC_FORMAT_DIVX3:
        {
            if(CONFIG_ENABLE_VIDEO_DECODER_DIVX3)
            {
                VIDEO_ENGINE_CREATE_DECODER("libawmpeg4divx311.so", "CreateMpeg4Divx311Decoder");
                p->fbmInfo.nDecoderNeededMiniFbmNum = MPEG4_DECODER_NEEDED_FRAME_NUM;
                p->fbmInfo.nDecoderNeededMiniFbmNumSD = MPEG4_DECODER_NEEDED_FRAME_NUM;
            }
            break;
        }

        case VIDEO_CODEC_FORMAT_DIVX4:
        {
            if(CONFIG_ENABLE_VIDEO_DECODER_DIVX4)
            {
                VIDEO_ENGINE_CREATE_DECODER("libawmpeg4normal.so", "CreateMpeg4NormalDecoder");
                p->fbmInfo.nDecoderNeededMiniFbmNum = MPEG4_DECODER_NEEDED_FRAME_NUM;
                p->fbmInfo.nDecoderNeededMiniFbmNumSD = MPEG4_DECODER_NEEDED_FRAME_NUM;
            }
            break;
        }

        case VIDEO_CODEC_FORMAT_DIVX5:
        {
            if(CONFIG_ENABLE_VIDEO_DECODER_DIVX5)
            {
                VIDEO_ENGINE_CREATE_DECODER("libawmpeg4normal.so", "CreateMpeg4NormalDecoder");
                p->fbmInfo.nDecoderNeededMiniFbmNum = MPEG4_DECODER_NEEDED_FRAME_NUM;
                p->fbmInfo.nDecoderNeededMiniFbmNumSD = MPEG4_DECODER_NEEDED_FRAME_NUM;
            }
            break;
        }

        case VIDEO_CODEC_FORMAT_XVID:
        {
            if(CONFIG_ENABLE_VIDEO_DECODER_XVID)
            {
                VIDEO_ENGINE_CREATE_DECODER("libawmpeg4normal.so", "CreateMpeg4NormalDecoder");
                p->fbmInfo.nDecoderNeededMiniFbmNum = MPEG4_DECODER_NEEDED_FRAME_NUM;
                p->fbmInfo.nDecoderNeededMiniFbmNumSD = MPEG4_DECODER_NEEDED_FRAME_NUM;
            }
            break;
        }

        case VIDEO_CODEC_FORMAT_H263:
        {
            if(CONFIG_ENABLE_VIDEO_DECODER_H263)
            {
                VIDEO_ENGINE_CREATE_DECODER("libawmpeg4h263.so", "CreateMpeg4H263Decoder");
                p->fbmInfo.nDecoderNeededMiniFbmNum = MPEG4_DECODER_NEEDED_FRAME_NUM;
                p->fbmInfo.nDecoderNeededMiniFbmNumSD = MPEG4_DECODER_NEEDED_FRAME_NUM;
            }
            break;
        }

        case VIDEO_CODEC_FORMAT_SORENSSON_H263:
        {
            if(CONFIG_ENABLE_VIDEO_DECODER_SORENSSON_H263)
            {
                VIDEO_ENGINE_CREATE_DECODER("libawmpeg4normal.so", "CreateMpeg4NormalDecoder");
                p->fbmInfo.nDecoderNeededMiniFbmNum = MPEG4_DECODER_NEEDED_FRAME_NUM;
                p->fbmInfo.nDecoderNeededMiniFbmNumSD = MPEG4_DECODER_NEEDED_FRAME_NUM;
            }
            break;
        }

        case VIDEO_CODEC_FORMAT_RXG2:
        {
            if(CONFIG_ENABLE_VIDEO_DECODER_RXG2)
            {
                VIDEO_ENGINE_CREATE_DECODER("libawmpeg4h263.so", "CreateMpeg4H263Decoder");
                p->fbmInfo.nDecoderNeededMiniFbmNum = MPEG4_DECODER_NEEDED_FRAME_NUM;
                p->fbmInfo.nDecoderNeededMiniFbmNumSD = MPEG4_DECODER_NEEDED_FRAME_NUM;
            }
            break;
        }

        case VIDEO_CODEC_FORMAT_H265:
        {
            if(CONFIG_ENABLE_VIDEO_DECODER_H265)
            {
            	if (gVeVersion == 0x1680 || gVeVersion == 0x1689)
            	{
                    VIDEO_ENGINE_CREATE_DECODER("libawh265.so", "CreateH265Decoder");
            	}
            	else
            	{
                    VIDEO_ENGINE_CREATE_DECODER("libawh265soft.so", "CreateH265DecoderSoftL");
					p->bIsSoftwareDecoder = 1;
            	}
                p->fbmInfo.nDecoderNeededMiniFbmNum = H265_DECODER_NEEDED_FRAME_NUM_MINUS_REF;
                p->fbmInfo.nDecoderNeededMiniFbmNumSD = H265_DECODER_NEEDED_FRAME_NUM_MINUS_REF;
            }
            break;
        }
        case VIDEO_CODEC_FORMAT_AVS:
        {
        	if(CONFIG_ENABLE_VIDEO_DECODER_AVS)
        	{
        		if(gVeVersion >= 0x1680)
        		{
                    VIDEO_ENGINE_CREATE_DECODER("libawavs.so", "CreateAvsDecoder");
        		}
                p->fbmInfo.nDecoderNeededMiniFbmNum = AVS_DECODER_NEEDED_FRAME_NUM;
                p->fbmInfo.nDecoderNeededMiniFbmNumSD = AVS_DECODER_NEEDED_FRAME_NUM;
        	}
            break;
        }

        case VIDEO_CODEC_FORMAT_WMV3:
        {
            if(CONFIG_ENABLE_VIDEO_DECODER_WMV3)
            {
                VIDEO_ENGINE_CREATE_DECODER("libawwmv3.so", "CreateVc1Decoder");
                p->fbmInfo.nDecoderNeededMiniFbmNum = VC1_DECODER_NEEDED_FRAME_NUM;
                p->fbmInfo.nDecoderNeededMiniFbmNumSD = VC1_DECODER_NEEDED_FRAME_NUM;
            }
            break;
        }

        case VIDEO_CODEC_FORMAT_VP8:
        {
            if(CONFIG_ENABLE_VIDEO_DECODER_VP8)
            {
                VIDEO_ENGINE_CREATE_DECODER("libawvp8.so", "CreateVp8Decoder");
                p->fbmInfo.nDecoderNeededMiniFbmNum = VP8_DECODER_NEEDED_FRAME_NUM;
                p->fbmInfo.nDecoderNeededMiniFbmNumSD = VP8_DECODER_NEEDED_FRAME_NUM;
            }
            break;
        }

        case VIDEO_CODEC_FORMAT_RX:
        {
            if(CONFIG_ENABLE_VIDEO_DECODER_RX)
            {
                VIDEO_ENGINE_CREATE_DECODER("librv.so", "CreateRvDecoder");
                p->fbmInfo.nDecoderNeededMiniFbmNum = RV_DECODER_NEEDED_FRAME_NUM;
                p->fbmInfo.nDecoderNeededMiniFbmNumSD = RV_DECODER_NEEDED_FRAME_NUM;
            }
            break;
        }

        case VIDEO_CODEC_FORMAT_WMV1:
        {
            if(CONFIG_ENABLE_VIDEO_DECODER_WMV1)
            {
            	if (gVeVersion >= 0x1639)
            	{
                    VIDEO_ENGINE_CREATE_DECODER("libawwmv12soft.so", "CreateWmv12SoftDecoder");
                    p->bIsSoftwareDecoder = 1;
            	}
            	else
            	{
                    VIDEO_ENGINE_CREATE_DECODER("libawmpeg4divx311.so", "CreateMpeg4Divx311Decoder");
            	}
                p->fbmInfo.nDecoderNeededMiniFbmNum = MPEG4_DECODER_NEEDED_FRAME_NUM;
                p->fbmInfo.nDecoderNeededMiniFbmNumSD = MPEG4_DECODER_NEEDED_FRAME_NUM;
            }
            break;
        }

        case VIDEO_CODEC_FORMAT_WMV2:
        {
            if(CONFIG_ENABLE_VIDEO_DECODER_WMV2)
            {
            	if (gVeVersion >= 0x1639)
            	{
                    VIDEO_ENGINE_CREATE_DECODER("libawwmv12soft.so", "CreateWmv12SoftDecoder");
                    p->bIsSoftwareDecoder = 1;
            	}
            	else
            	{
                    VIDEO_ENGINE_CREATE_DECODER("libawmpeg4divx311.so", "CreateMpeg4Divx311Decoder");
            	}
                p->fbmInfo.nDecoderNeededMiniFbmNum = MPEG4_DECODER_NEEDED_FRAME_NUM;
                p->fbmInfo.nDecoderNeededMiniFbmNumSD = MPEG4_DECODER_NEEDED_FRAME_NUM;
            }
            break;
        }

        case VIDEO_CODEC_FORMAT_VP6:
        {
            if(CONFIG_ENABLE_VIDEO_DECODER_VP6)
            {
            	if (gVeVersion >= 0x1639)
            	{
                    VIDEO_ENGINE_CREATE_DECODER("libawvp6soft.so", "CreateVp6SoftDecoder");
                    p->bIsSoftwareDecoder = 1;
            	}
            	else
            	{
                    VIDEO_ENGINE_CREATE_DECODER("libawmpeg4vp6.so", "CreateMpeg4Vp6Decoder");
            	}
                p->fbmInfo.nDecoderNeededMiniFbmNum = MPEG4_DECODER_NEEDED_FRAME_NUM;
                p->fbmInfo.nDecoderNeededMiniFbmNumSD = MPEG4_DECODER_NEEDED_FRAME_NUM;
            }
            break;
        }

        case VIDEO_CODEC_FORMAT_VP9:
        {
            if(CONFIG_ENABLE_VIDEO_DECODER_VP9)
            {
                VIDEO_ENGINE_CREATE_DECODER("libawvp9soft.so", "CreateVp9SoftDecoder");
                p->bIsSoftwareDecoder = 1;
            }
            break;
        }
        default:
        {
            break;
        }
    }

    return pInterface;
}
#else //#if 0
/*
 * CheckVeVersionAndLib
 * return
 * 		 0, lib is ok
 * 		-1, need to search next lib
 * */
#define VIDEO_ENGINE_CHECK_OK 0
#define VIDEO_ENGINE_CHECK_FAIL -1
static int CheckVeVersionAndLib(struct VDecoderNodeS *posVDNode, VideoEngine* p)
{
	int ret = VIDEO_ENGINE_CHECK_FAIL;
	int eCodecFormat;

	eCodecFormat = (int)posVDNode->format;
	switch(eCodecFormat)
	{
		case VIDEO_CODEC_FORMAT_MJPEG:
		{
			int bIsMjpegPlus = (strcmp(posVDNode->desc, "mjpegplus") == 0);

			if (gVeVersion == 0x1681 || gVeVersion == 0x1689)
				ret = bIsMjpegPlus ? VIDEO_ENGINE_CHECK_OK : VIDEO_ENGINE_CHECK_FAIL;
			else
				ret = (bIsMjpegPlus == 0) ? VIDEO_ENGINE_CHECK_OK : VIDEO_ENGINE_CHECK_FAIL;
            p->fbmInfo.nDecoderNeededMiniFbmNum = MJPEG_DECODER_NEEDED_FRAME_NUM;
            p->fbmInfo.nDecoderNeededMiniFbmNumSD = MJPEG_DECODER_NEEDED_FRAME_NUM;
			break;
		}
		case VIDEO_CODEC_FORMAT_H264:
		{
			p->fbmInfo.nDecoderNeededMiniFbmNum = H264_DECODER_NEEDED_FRAME_NUM_MINUS_REF;
			p->fbmInfo.nDecoderNeededMiniFbmNumSD = H264_DISP_NEEDED_FRAME_NUM_SCALE_DOWN;
			ret = VIDEO_ENGINE_CHECK_OK;
			break;
		}
		case VIDEO_CODEC_FORMAT_MPEG1:
		case VIDEO_CODEC_FORMAT_MPEG2:
		{
			p->fbmInfo.nDecoderNeededMiniFbmNum = MPEG2_DECODER_NEEDED_FRAME_NUM;
			p->fbmInfo.nDecoderNeededMiniFbmNumSD = MPEG2_DECODER_NEEDED_FRAME_NUM;
			ret = VIDEO_ENGINE_CHECK_OK;
			break;
		}

		case VIDEO_CODEC_FORMAT_MPEG4:
		case VIDEO_CODEC_FORMAT_MSMPEG4V1:
		case VIDEO_CODEC_FORMAT_MSMPEG4V2:
		case VIDEO_CODEC_FORMAT_DIVX3:
		case VIDEO_CODEC_FORMAT_DIVX4:
		case VIDEO_CODEC_FORMAT_DIVX5:
		case VIDEO_CODEC_FORMAT_XVID:
		case VIDEO_CODEC_FORMAT_H263:
		case VIDEO_CODEC_FORMAT_SORENSSON_H263:
		case VIDEO_CODEC_FORMAT_RXG2:
		{
			p->fbmInfo.nDecoderNeededMiniFbmNum = MPEG4_DECODER_NEEDED_FRAME_NUM;
			p->fbmInfo.nDecoderNeededMiniFbmNumSD = MPEG4_DECODER_NEEDED_FRAME_NUM;
			ret = VIDEO_ENGINE_CHECK_OK;
			break;
		}

		case VIDEO_CODEC_FORMAT_H265:
		{
			int bIsH265Soft = posVDNode->bIsSoftDecoder;
			p->fbmInfo.nDecoderNeededMiniFbmNum = H265_DECODER_NEEDED_FRAME_NUM_MINUS_REF;
			p->fbmInfo.nDecoderNeededMiniFbmNumSD = H265_DECODER_NEEDED_FRAME_NUM_MINUS_REF;
			if(gVeVersion == 0x1680 || gVeVersion == 0x1689)
				ret = (bIsH265Soft == 0) ? VIDEO_ENGINE_CHECK_OK : VIDEO_ENGINE_CHECK_FAIL;
			else
				ret = (bIsH265Soft) ? VIDEO_ENGINE_CHECK_OK : VIDEO_ENGINE_CHECK_FAIL;
			break;
		}
		case VIDEO_CODEC_FORMAT_AVS:
		{
			p->fbmInfo.nDecoderNeededMiniFbmNum = AVS_DECODER_NEEDED_FRAME_NUM;
			p->fbmInfo.nDecoderNeededMiniFbmNumSD = AVS_DECODER_NEEDED_FRAME_NUM;
			ret = VIDEO_ENGINE_CHECK_OK;
			break;
		}

		case VIDEO_CODEC_FORMAT_WMV3:
		{
			p->fbmInfo.nDecoderNeededMiniFbmNum = VC1_DECODER_NEEDED_FRAME_NUM;
			p->fbmInfo.nDecoderNeededMiniFbmNumSD = VC1_DECODER_NEEDED_FRAME_NUM;
			ret = VIDEO_ENGINE_CHECK_OK;
			break;
		}

		case VIDEO_CODEC_FORMAT_VP8:
		{
			p->fbmInfo.nDecoderNeededMiniFbmNum = VP8_DECODER_NEEDED_FRAME_NUM;
			p->fbmInfo.nDecoderNeededMiniFbmNumSD = VP8_DECODER_NEEDED_FRAME_NUM;
			ret = VIDEO_ENGINE_CHECK_OK;
			break;
		}

		case VIDEO_CODEC_FORMAT_RX:
		{
			p->fbmInfo.nDecoderNeededMiniFbmNum = RV_DECODER_NEEDED_FRAME_NUM;
			p->fbmInfo.nDecoderNeededMiniFbmNumSD = RV_DECODER_NEEDED_FRAME_NUM;
			ret = VIDEO_ENGINE_CHECK_OK;
			break;
		}

		case VIDEO_CODEC_FORMAT_WMV1:
		case VIDEO_CODEC_FORMAT_WMV2:
		{
			int bIsWmv12Soft = posVDNode->bIsSoftDecoder;
			p->fbmInfo.nDecoderNeededMiniFbmNum = MPEG4_DECODER_NEEDED_FRAME_NUM;
			p->fbmInfo.nDecoderNeededMiniFbmNumSD = MPEG4_DECODER_NEEDED_FRAME_NUM;
			if(gVeVersion >= 0x1639)
				ret = bIsWmv12Soft ? VIDEO_ENGINE_CHECK_OK : VIDEO_ENGINE_CHECK_FAIL;
			else
				ret = (bIsWmv12Soft == 0) ? VIDEO_ENGINE_CHECK_OK : VIDEO_ENGINE_CHECK_FAIL;
			break;
		}

		case VIDEO_CODEC_FORMAT_VP6:
		{
			int bIsVp6Soft = posVDNode->bIsSoftDecoder;
			p->fbmInfo.nDecoderNeededMiniFbmNum = MPEG4_DECODER_NEEDED_FRAME_NUM;
			p->fbmInfo.nDecoderNeededMiniFbmNumSD = MPEG4_DECODER_NEEDED_FRAME_NUM;
			if(gVeVersion >= 0x1639)
				ret = bIsVp6Soft ? VIDEO_ENGINE_CHECK_OK : VIDEO_ENGINE_CHECK_FAIL;
			else
				ret = (bIsVp6Soft == 0) ? VIDEO_ENGINE_CHECK_OK : VIDEO_ENGINE_CHECK_FAIL;
			break;
		}

		case VIDEO_CODEC_FORMAT_VP9:
		{
			ret = VIDEO_ENGINE_CHECK_OK;
			break;
		}
		default:
		{
			break;
		}
	}
	return ret;
}

static DecoderInterface* CreateSpecificDecoder(VideoEngine* p)
{
    DecoderInterface* pInterface = NULL;
    struct VDecoderNodeS *posVDNode = NULL;
    int bCheckFlag = VIDEO_ENGINE_CHECK_FAIL;

    DecoderListForEachEntry(posVDNode, &gVDecoderList.list, node)
    {
        if((int)posVDNode->format == p->videoStreamInfo.eCodecFormat)
        {
        	bCheckFlag = CheckVeVersionAndLib(posVDNode, p);
        	if(bCheckFlag == VIDEO_ENGINE_CHECK_OK)
        	{
                logd("Create decoder '%x:%s'", posVDNode->format, posVDNode->desc);
                p->bIsSoftwareDecoder = posVDNode->bIsSoftDecoder;
                pInterface = posVDNode->creator(p);
                return pInterface;
        	}
        }
    }

    loge("format '%x' support!", p->videoStreamInfo.eCodecFormat);
    return NULL;
}
#endif //#if 0

int GetBufferSize(int ePixelFormat, int nWidth, int nHeight, int*nYBufferSize, int *nCBufferSize, int* nYLineStride, int* nCLineStride, int nAlignValue)
{
    int   nHeight16Align;
    int   nHeight32Align;
    int   nHeight64Align;
    int   nLineStride;
    int   nMemSizeY;
    int   nMemSizeC;

    nHeight16Align = (nHeight+15) & ~15;
    nHeight32Align = (nHeight+31) & ~31;
    nHeight64Align = (nHeight+63) & ~63;


    switch(ePixelFormat)
    {
        case PIXEL_FORMAT_YUV_PLANER_420:
        case PIXEL_FORMAT_YUV_PLANER_422:
        case PIXEL_FORMAT_YUV_PLANER_444:
        case PIXEL_FORMAT_YV12:
        case PIXEL_FORMAT_NV21:

            //* for decoder,
            //* height of Y component is required to be 16 aligned, for example, 1080 becomes to 1088.
            //* width and height of U or V component are both required to be 8 aligned.
            //* nLineStride should be 16 aligned.
        	if(nAlignValue == 0)
        	{
        		nLineStride = (nWidth+15) &~ 15;
        		nMemSizeY = nLineStride*nHeight16Align;
        	}
        	else
        	{
        		nLineStride = (nWidth+nAlignValue-1) &~ (nAlignValue-1);
                nHeight = (nHeight+nAlignValue-1) &~ (nAlignValue-1);
        		nMemSizeY = nLineStride*nHeight;
        	}
            if(ePixelFormat == PIXEL_FORMAT_YUV_PLANER_420 ||
               ePixelFormat == PIXEL_FORMAT_YV12 ||
               ePixelFormat == PIXEL_FORMAT_NV21)
                nMemSizeC = nMemSizeY>>2;
            else if(ePixelFormat == PIXEL_FORMAT_YUV_PLANER_422)
                nMemSizeC = nMemSizeY>>1;
            else
                nMemSizeC = nMemSizeY;  //* PIXEL_FORMAT_YUV_PLANER_444
            break;

        case PIXEL_FORMAT_YUV_MB32_420:
        case PIXEL_FORMAT_YUV_MB32_422:
        case PIXEL_FORMAT_YUV_MB32_444:
            //* for decoder,
            //* height of Y component is required to be 32 aligned.
            //* height of UV component are both required to be 32 aligned.
            //* nLineStride should be 32 aligned.
        	nLineStride = (nWidth+63) &~ 63;
            nMemSizeY = nLineStride*nHeight32Align;

            if(ePixelFormat == PIXEL_FORMAT_YUV_MB32_420)
                nMemSizeC = nLineStride*nHeight64Align/4;
            else if(ePixelFormat == PIXEL_FORMAT_YUV_MB32_422)
                nMemSizeC = nLineStride*nHeight64Align/2;
            else
                nMemSizeC = nLineStride*nHeight64Align;
            break;

        case PIXEL_FORMAT_RGBA:
        case PIXEL_FORMAT_ARGB:
        case PIXEL_FORMAT_ABGR:
        case PIXEL_FORMAT_BGRA:
//        	nLineStride = (nWidth+3) &~ 3;
        	nLineStride = nWidth *4;

            nMemSizeY = nLineStride*nHeight;
            nMemSizeC = 0;

        	break;

        default:
            loge("pixel format incorrect, ePixelFormat=%d", ePixelFormat);
            return -1;
    }
    if(nYBufferSize != NULL)
    {
    	*nYBufferSize = nMemSizeY;
    }
    if(nCBufferSize != NULL)
    {
    	*nCBufferSize = nMemSizeC;
    }
    if(nYLineStride != NULL)
    {
    	*nYLineStride = nLineStride;
    }
    if(nCLineStride != NULL)
    {
    	*nCLineStride = nLineStride>>1;
    }
    return 0;
}


/* /proc/[pid]/maps */

static int GetLocalPathFromProcessMaps(char *localPath, int len)
{
#define LOCAL_LIB "libvdecoder.so"
    char path[512] = {0};
    char line[1024] = {0};
    FILE *file = NULL;
    char *strLibPos = NULL;
    int ret = -1;

    memset(localPath, 0x00, len);

    sprintf(path, "/proc/%d/maps", getpid());
    file = fopen(path, "r");
    if (file == NULL)
    {
        loge("fopen failure, errno(%d)", errno);
        ret = -1;
        goto out;
    }

    while (fgets(line, 1023, file) != NULL)
    {
        if ((strLibPos = strstr(line, LOCAL_LIB)) != NULL)
        {
            char *rootPathPos = NULL;
            int localPathLen = 0;
            rootPathPos = strchr(line, '/');
            if (rootPathPos == NULL)
            {
                loge("some thing error, cur line '%s'", line);
                ret = -1;
                goto out;
            }

            localPathLen = strLibPos - rootPathPos - 1;
            if (localPathLen > len -1)
            {
                loge("localPath too long :%s ", localPath);
                ret = -1;
                goto out;
            }

            memcpy(localPath, rootPathPos, localPathLen);
            ret = 0;
            goto out;
        }
    }
    loge("Are you kidding? not found?");

out:
    if (file)
    {
        fclose(file);
    }
    return ret;
}

typedef void VDPluginEntry(void);

void AddVDPluginSingle(char *lib)
{
    void *libFd = NULL;
    if(lib == NULL)
    {
    	loge(" open lib == NULL ");
    	return;
    }

    libFd = dlopen(lib, RTLD_NOW);

    VDPluginEntry *PluginInit = NULL;

    if (libFd == NULL)
    {
        loge("dlopen '%s' fail: %s", lib, dlerror());
        return ;
    }

    PluginInit = dlsym(libFd, "CedarPluginVDInit");
    if (PluginInit == NULL)
    {
        logw("Invalid plugin, CedarPluginVDInit not found.");
        return;
    }
    logd("vdecoder open lib: %s", lib);
    PluginInit(); /* init plugin */
    return ;
}

/* executive when load */
static void AddVDPlugin(void) __attribute__((constructor));
void AddVDPlugin(void)
{
    char localPath[512];
    char slash[4] = "/";
    char loadLib[512];
    struct dirent **namelist = NULL;
    int num = 0, index = 0;
    int pathLen = 0;
    int ret;

    memset(localPath, 0, 512);
    memset(loadLib, 0, 512);
//scan_local_path:
    ret = GetLocalPathFromProcessMaps(localPath, 512);
    if (ret != 0)
    {
        logw("get local path failure, scan /system/lib ");
        goto scan_system_lib;
    }

    num = scandir(localPath, &namelist, NULL, NULL);
    if (num <= 0)
    {
        logw("scandir failure, errno(%d), scan /system/lib ", errno);
        goto scan_system_lib;
    }
    strcat(localPath, slash);
    pathLen = strlen(localPath);
    strcpy(loadLib, localPath);
    logv("1117 get local path: %s", localPath);
    for(index = 0; index < num; index++)
    {
        if(((strstr((namelist[index])->d_name, "libaw") != NULL) ||
        	(strstr((namelist[index])->d_name, "librv") != NULL))
        	&& (strstr((namelist[index])->d_name, ".so") != NULL))
        {
        	loadLib[pathLen] = '\0';
            strcat(loadLib, (namelist[index])->d_name);
            logv(" 1117 load so: %s ", loadLib);
            AddVDPluginSingle(loadLib);
        }
        free(namelist[index]);
        namelist[index] = NULL;
    }

scan_system_lib:
    // TODO: scan /system/lib

    return;
}

