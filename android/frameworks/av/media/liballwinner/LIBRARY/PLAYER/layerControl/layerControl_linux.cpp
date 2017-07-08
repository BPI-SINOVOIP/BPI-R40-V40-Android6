
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/ioctl.h>

#include "config.h"
#include "layerControl.h"
#include "log.h"
#include "memoryAdapter.h"

//#if CONFIG_CHIP == OPTION_CHIP_1639
// only 1639 available now
#if (1)
#include "1639/drv_display.h"
#else
#error "this module only support chip 1639."
#endif

#if CONFIG_PRODUCT == OPTION_PRODUCT_PAD
static const int DISPLAY_CHANNEL = 0;   //* channel 0 for pad screen.
#else
//* products which use tv(hdmi) for display.
static const int DISPLAY_CHANNEL = 1;   //* channel 1 for hdmi screen.
#endif
static const int DISPLAY_LAYER = 1;     //* use layer 1 to show video, totally 4 layers, gui generally use layer 0.

static VideoPicture* gLastPicture = NULL;

#if 0
/* NUM_OF_PICTURES_KEEP_IN_LIST has defined in config.h */
#define NUM_OF_PICTURES_KEEP_IN_LIST    2      //* it may be set to 3 if new version of display system used.
#endif // #if 0

typedef struct VPictureNode_t VPictureNode;
struct VPictureNode_t
{
    VideoPicture* pPicture;
    VideoPicture* pSecondPictureOf3D;
    VPictureNode* pNext;
    int           bUsed;
};

typedef struct LayerCtrlContext
{
    int                  fdDisplay;
    enum EPIXELFORMAT    eReceivePixelFormat;
    int                  nWidth;
    int                  nHeight;
    int                  nLeftOff;
    int                  nTopOff;
    int                  nDisplayWidth;
    int                  nDisplayHeight;
    PlayerCallback       callback;
    void*                pUserData;
    int                  bRenderToHardwareLayer;
    enum EPICTURE3DMODE  ePicture3DMode;
    enum EDISPLAY3DMODE  eDisplay3DMode;
    int                  bLayerInitialized;
    int                  bLayerShowed;
    
    //* use when render to gpu.
    VideoPicture         bufferWrappers[32];
    int                  bBufferWrapperUsed[32];
    
    //* use when render derect to hardware layer.
    VPictureNode*        pPictureListHead;
    VPictureNode         picNodes[32];
    
    int                  nScreenWidth;
    int                  nScreenHeight;
    
}LayerCtrlContext;

static int SetLayerParam(LayerCtrlContext* lc);
static void setHwcLayerPictureInfo(LayerCtrlContext* lc,
                                   disp_layer_info*  pLayerInfo, 
                                   VideoPicture*     pPicture,
                                   VideoPicture*     pSecondPictureOf3D);


LayerCtrl* LayerInit(void* pNativeWindow)
{
    unsigned long     args[4];
    LayerCtrlContext* lc;
    
    logv("LayerInit.");
    
    lc = (LayerCtrlContext*)malloc(sizeof(LayerCtrlContext));
    if(lc == NULL)
    {
        loge("malloc memory fail.");
        return NULL;
    }
    memset(lc, 0, sizeof(LayerCtrlContext));
    
    lc->fdDisplay = open("/dev/disp", O_RDWR);
    lc->eReceivePixelFormat = PIXEL_FORMAT_YUV_MB32_420;
    lc->bRenderToHardwareLayer = 1;
    
    logv("lc->bRenderToHardwareLayer=%d\n", lc->bRenderToHardwareLayer);
    
    //* get screen size.
    args[0] = DISPLAY_CHANNEL;
    args[1] = DISPLAY_LAYER;
    args[2] = 0;
    args[3] = 0;
    lc->nScreenWidth  = ioctl(lc->fdDisplay, DISP_CMD_GET_SCN_WIDTH, args);
    lc->nScreenHeight = ioctl(lc->fdDisplay, DISP_CMD_GET_SCN_HEIGHT, args);

    //* open the memory module, we need get physical address of a picture buffer 
    //* by MemAdapterGetPhysicAddress().
    if(gLastPicture == NULL)
        MemAdapterOpen();
    
    return (LayerCtrl*)lc;
}

int LayerCtrlHideVideo(LayerCtrl* l);

void LayerRelease(LayerCtrl* l, int bKeepPictureOnScreen)
{
    LayerCtrlContext* lc;
    VPictureNode*     nodePtr;
    
    lc = (LayerCtrlContext*)l;
    
    logv("Layer release");
    
    //* disable layer.
    if(gLastPicture == NULL)
        LayerCtrlHideVideo(l);
    
    if(lc->fdDisplay >= 0)
        close(lc->fdDisplay);
    
    //* return pictures.
    while(lc->pPictureListHead != NULL)
    {
        nodePtr = lc->pPictureListHead;
        lc->pPictureListHead = lc->pPictureListHead->pNext;
        lc->callback(lc->pUserData, MESSAGE_ID_LAYER_RETURN_BUFFER, (void*)nodePtr->pPicture);
    }
    
    //* free the memory module.
    if(gLastPicture == NULL)
        MemAdapterClose();
    
    free(lc);   
}


int LayerSetExpectPixelFormat(LayerCtrl* l, enum EPIXELFORMAT ePixelFormat)
{
    LayerCtrlContext* lc;
    
    lc = (LayerCtrlContext*)l;
    
    logv("Layer set expected pixel format, format = %d", (int)ePixelFormat);
    
    //* reder directly to hardware layer.
    if(ePixelFormat == PIXEL_FORMAT_YUV_MB32_420   ||
       ePixelFormat == PIXEL_FORMAT_YUV_MB32_422   ||
       ePixelFormat == PIXEL_FORMAT_YUV_MB32_444   ||
       ePixelFormat == PIXEL_FORMAT_YUV_PLANER_420 ||
       ePixelFormat == PIXEL_FORMAT_YV12           ||
       ePixelFormat == PIXEL_FORMAT_NV12           ||
       ePixelFormat == PIXEL_FORMAT_NV21)   //* add new pixel formats supported by hardware layer here.
    {
        lc->eReceivePixelFormat = ePixelFormat;
    }
    else
    {
        logv("receive pixel format is %d, not match.", lc->eReceivePixelFormat);
        return -1;
    }
    
    return 0;
}


enum EPIXELFORMAT LayerGetPixelFormat(LayerCtrl* l)
{
    LayerCtrlContext* lc;
    
    lc = (LayerCtrlContext*)l;
    
    logv("Layer get pixel format, return %d", lc->eReceivePixelFormat);
    
    return lc->eReceivePixelFormat;
}


int LayerSetPictureSize(LayerCtrl* l, int nWidth, int nHeight)
{
    LayerCtrlContext* lc;
    
    lc = (LayerCtrlContext*)l;
    
    logv("Layer set picture size, width = %d, height = %d", nWidth, nHeight);
    
    lc->nWidth         = nWidth;
    lc->nHeight        = nHeight;
    lc->nDisplayWidth  = nWidth;
    lc->nDisplayHeight = nHeight;
    lc->nLeftOff       = 0;
    lc->nTopOff        = 0;
    
    return 0;
}


int LayerSetDisplayRegion(LayerCtrl* l, int nLeftOff, int nTopOff, int nDisplayWidth, int nDisplayHeight)
{
    LayerCtrlContext* lc;
    
    lc = (LayerCtrlContext*)l;
    
    logv("Layer set display region, leftOffset = %d, topOffset = %d, displayWidth = %d, displayHeight = %d",
        nLeftOff, nTopOff, nDisplayWidth, nDisplayHeight);
    
    if(nDisplayWidth != 0 && nDisplayHeight != 0)
    {
        lc->nDisplayWidth  = nDisplayWidth;
        lc->nDisplayHeight = nDisplayHeight;
        lc->nLeftOff       = nLeftOff;
        lc->nTopOff        = nTopOff;
        return 0;
    }
    else
        return -1;
}


int LayerSetPicture3DMode(LayerCtrl* l, enum EPICTURE3DMODE ePicture3DMode)
{
    LayerCtrlContext* lc;
    
    lc = (LayerCtrlContext*)l;
    
    logv("Layer set picture 3d mode, mode = %d", (int)ePicture3DMode);
    
    lc->ePicture3DMode = ePicture3DMode;
    
    return 0;
}


enum EPICTURE3DMODE LayerGetPicture3DMode(LayerCtrl* l)
{
    LayerCtrlContext* lc;
    
    lc = (LayerCtrlContext*)l;
    
    logv("Layer get picture 3d mode, mode = %d", (int)lc->ePicture3DMode);
    
    return lc->ePicture3DMode;
}


int LayerSetDisplay3DMode(LayerCtrl* l, enum EDISPLAY3DMODE eDisplay3DMode)
{
    LayerCtrlContext* lc;
    disp_layer_info   layerInfo;
    unsigned long     args[4];
    int               err;
    
    lc = (LayerCtrlContext*)l;
    
    logv("Layer set display 3d mode, mode = %d", (int)eDisplay3DMode);
    
    lc->eDisplay3DMode = eDisplay3DMode;
    
    if(lc->bLayerInitialized == 0)
        return 0;
    
    args[0] = DISPLAY_CHANNEL;
    args[1] = DISPLAY_LAYER;
    args[2] = (unsigned long)&layerInfo;
    args[3] = 0;
    ioctl(lc->fdDisplay, DISP_CMD_LAYER_GET_INFO, args);
    
    switch(lc->ePicture3DMode)
    {
        case PICTURE_3D_MODE_TWO_SEPERATED_PICTURE:
            layerInfo.fb.b_trd_src = 1;
            layerInfo.fb.trd_mode  = DISP_3D_SRC_MODE_FP;
            break;
        case PICTURE_3D_MODE_SIDE_BY_SIDE:
            layerInfo.fb.b_trd_src = 1;
            layerInfo.fb.trd_mode  = DISP_3D_SRC_MODE_SSH;
            break;
        case PICTURE_3D_MODE_TOP_TO_BOTTOM:
            layerInfo.fb.b_trd_src = 1;
            layerInfo.fb.trd_mode  = DISP_3D_SRC_MODE_TB;
            break;
        case PICTURE_3D_MODE_LINE_INTERLEAVE:
            layerInfo.fb.b_trd_src = 1;
            layerInfo.fb.trd_mode  = DISP_3D_SRC_MODE_LI;
            break;
        default:
            layerInfo.fb.b_trd_src = 0;
            break;
    }
    
    switch(eDisplay3DMode)
    {
        case DISPLAY_3D_MODE_3D:
            if(lc->ePicture3DMode == PICTURE_3D_MODE_TWO_SEPERATED_PICTURE)
                layerInfo.out_trd_mode = DISP_3D_OUT_MODE_FP;
            else if(lc->ePicture3DMode == PICTURE_3D_MODE_SIDE_BY_SIDE)
                layerInfo.out_trd_mode = DISP_3D_OUT_MODE_SSH;
            else if(lc->ePicture3DMode == PICTURE_3D_MODE_TOP_TO_BOTTOM)
                layerInfo.out_trd_mode = DISP_3D_OUT_MODE_TB;
            else if(lc->ePicture3DMode == PICTURE_3D_MODE_LINE_INTERLEAVE)
                layerInfo.out_trd_mode = DISP_3D_OUT_MODE_LI;
            else
                layerInfo.out_trd_mode = DISP_3D_OUT_MODE_SSH;
            
            layerInfo.b_trd_out = 1;
            break;
        
        case DISPLAY_3D_MODE_HALF_PICTURE:
            if(lc->ePicture3DMode == PICTURE_3D_MODE_SIDE_BY_SIDE)
            {
                //* set source window to the top half.
                layerInfo.fb.src_win.x	    = lc->nLeftOff;
                layerInfo.fb.src_win.y	    = lc->nTopOff;
                layerInfo.fb.src_win.width  = lc->nDisplayWidth/2;
                layerInfo.fb.src_win.height = lc->nDisplayHeight;
            }
            else if(lc->ePicture3DMode == PICTURE_3D_MODE_TOP_TO_BOTTOM)
            {
                //* set source window to the left half.
                layerInfo.fb.src_win.x	    = lc->nLeftOff;
                layerInfo.fb.src_win.y	    = lc->nTopOff;
                layerInfo.fb.src_win.width  = lc->nDisplayWidth;
                layerInfo.fb.src_win.height = lc->nDisplayHeight/2;
            }
            else
            {
                //* set source window to the full picture.
                layerInfo.fb.src_win.x	    = lc->nLeftOff;
                layerInfo.fb.src_win.y	    = lc->nTopOff;
                layerInfo.fb.src_win.width  = lc->nDisplayWidth;
                layerInfo.fb.src_win.height = lc->nDisplayHeight;
            }
            
            layerInfo.b_trd_out = 0;
            break;
        
        case DISPLAY_3D_MODE_2D:
        default:
            //* set source window to the full picture.
            layerInfo.fb.src_win.x	    = lc->nLeftOff;
            layerInfo.fb.src_win.y	    = lc->nTopOff;
            layerInfo.fb.src_win.width  = lc->nDisplayWidth;
            layerInfo.fb.src_win.height = lc->nDisplayHeight;
            layerInfo.b_trd_out = 0;
            break;
    }
    
    err = ioctl(lc->fdDisplay, DISP_CMD_LAYER_SET_INFO, args);
    if(err == 0)
    {
        lc->eDisplay3DMode = eDisplay3DMode;
        return 0;
    }
    else
    {
        logw("set 3d mode fail to hardware layer.");
        return -1;
    }
}


enum EDISPLAY3DMODE LayerGetDisplay3DMode(LayerCtrl* l)
{
    LayerCtrlContext* lc;
    
    lc = (LayerCtrlContext*)l;
    
    logv("Layer get display 3d mode, mode = %d", (int)lc->eDisplay3DMode);
    
    return lc->eDisplay3DMode;
}


int LayerSetCallback(LayerCtrl* l, PlayerCallback callback, void* pUserData)
{
    LayerCtrlContext* lc;
    
    lc = (LayerCtrlContext*)l;
    lc->callback  = callback;
    lc->pUserData = pUserData;
    return 0;
}


int LayerDequeueBuffer(LayerCtrl* l, VideoPicture** ppBuf)
{
    LayerCtrlContext* lc;
    
    lc = (LayerCtrlContext*)l;
    
    *ppBuf = NULL;
    
    return LAYER_RESULT_USE_OUTSIDE_BUFFER;
}



int LayerQueueBuffer(LayerCtrl* l, VideoPicture* pBuf, int bValid)
{
    LayerCtrlContext* lc;
    int               i;
    VPictureNode*     newNode;
    VPictureNode*     nodePtr;
    disp_layer_info   layerInfo;
    unsigned long     args[4];
    
    lc = (LayerCtrlContext*)l;
        
    if(bValid == 0)
    {
        if(pBuf != NULL)
            lc->callback(lc->pUserData, MESSAGE_ID_LAYER_RETURN_BUFFER, (void*)pBuf);
        return 0;
    }
    
    if(lc->bLayerInitialized == 0)
    {
        if(SetLayerParam(lc) != 0)
        {
            loge("can not initialize layer.");
            return -1;
        }
        
        lc->bLayerInitialized = 1;
        
        if(lc->eDisplay3DMode != DISPLAY_3D_MODE_2D || lc->ePicture3DMode != PICTURE_3D_MODE_NONE)
           LayerSetDisplay3DMode(l, lc->eDisplay3DMode);
    }
    
    if(pBuf->nWidth != lc->nWidth ||
       pBuf->nHeight != lc->nHeight ||
       pBuf->ePixelFormat != lc->eReceivePixelFormat)
    {
        //* config the display hardware again.
        //* TODO.
    }
    
    //* set picture to display hardware.
    setHwcLayerPictureInfo(lc, &layerInfo, pBuf, NULL);
    args[0] = DISPLAY_CHANNEL;
    args[1] = DISPLAY_LAYER;
    args[2] = (unsigned long)(&layerInfo);
    args[3] = 0;
    ioctl(lc->fdDisplay, DISP_CMD_LAYER_SET_INFO, args);
   
    //* wait for new frame showed.
    if(lc->bLayerShowed == 1)
    {
        int nCurFrameId;
        int nWaitTime;
        
        args[0] = DISPLAY_CHANNEL;
        args[1] = DISPLAY_LAYER;
        args[2] = 0;
        args[3] = 0;
        
        nWaitTime = 50000;  //* max frame interval is 1000/24fps = 41.67ms, here we wait 50ms for max.
        do
        {
            nCurFrameId = ioctl(lc->fdDisplay, DISP_CMD_LAYER_GET_FRAME_ID, args);
            if(nCurFrameId == pBuf->nID)
                break;
            else
            {
                if(nWaitTime <= 0)
                {
                    logv("check frame id fail, maybe something error with the HWC layer.");
                    break;
                }
                else
                {
                    usleep(5000);
                    nWaitTime -= 5000;
                }
            }
        }while(1);
    }
    
    //* free last picture of last video stream in case gConfigHoldLastPicture is set.
    if(gLastPicture != NULL)
    {
        logv("xxxx free gLastPicture, pData0 = %p", gLastPicture->pData0);
        FreePictureBuffer(gLastPicture);
        gLastPicture = NULL;
    }
    
    //* attach the new picture to list and return the old picture.
    newNode = NULL;
    for(i=0; i<32; i++)
    {
        if(lc->picNodes[i].bUsed == 0)
        {
            newNode = &lc->picNodes[i];
            newNode->pNext              = NULL;
            newNode->bUsed              = 1;
            newNode->pPicture           = pBuf;
            newNode->pSecondPictureOf3D = NULL;
            break;
        }
    }
    
    if(i == 32)
    {
        loge("not enough picture nodes, shouldn't run here.");
        abort();
    }
    
    if(lc->pPictureListHead != NULL)
    {
        nodePtr = lc->pPictureListHead;
        i = 1;
        while(nodePtr->pNext != NULL)
        {
            i++;
            nodePtr = nodePtr->pNext;
        }
        
        nodePtr->pNext = newNode;
        i++;
        
        //* return one picture.
        while(i > NUM_OF_PICTURES_KEEP_IN_LIST)
        {
            nodePtr = lc->pPictureListHead;
            lc->pPictureListHead = lc->pPictureListHead->pNext;
            lc->callback(lc->pUserData, MESSAGE_ID_LAYER_RETURN_BUFFER, (void*)nodePtr->pPicture);
            nodePtr->pPicture = NULL;
            if(nodePtr->pSecondPictureOf3D != NULL)
            {
                lc->callback(lc->pUserData, MESSAGE_ID_LAYER_RETURN_BUFFER, (void*)nodePtr->pSecondPictureOf3D);
                nodePtr->pSecondPictureOf3D = NULL;
            }
            nodePtr->bUsed = 0;
            i--;
        }
    }
    else
    {
        lc->pPictureListHead = newNode;
    }
    
    return 0;
}


int LayerDequeue3DBuffer(LayerCtrl* l, VideoPicture** ppBuf0, VideoPicture** ppBuf1)
{
    LayerCtrlContext* lc;
    
    lc = (LayerCtrlContext*)l;
    *ppBuf0 = NULL;
    *ppBuf1 = NULL;
    
    return LAYER_RESULT_USE_OUTSIDE_BUFFER;
}


int LayerQueue3DBuffer(LayerCtrl* l, VideoPicture* pBuf0, VideoPicture* pBuf1, int bValid)
{
    LayerCtrlContext* lc;
    int               i;
    VPictureNode*     newNode;
    VPictureNode*     nodePtr;
    disp_layer_info   layerInfo;
    unsigned long     args[4];
    
    lc = (LayerCtrlContext*)l;
        
    if(bValid == 0)
    {
        if(pBuf0 != NULL)
            lc->callback(lc->pUserData, MESSAGE_ID_LAYER_RETURN_BUFFER, (void*)pBuf0);
        if(pBuf1 != NULL)
            lc->callback(lc->pUserData, MESSAGE_ID_LAYER_RETURN_BUFFER, (void*)pBuf1);
        return 0;
    }
    
    if(lc->bLayerInitialized == 0)
    {
        if(SetLayerParam(lc) != 0)
        {
            loge("can not initialize layer.");
            return -1;
        }
        
        lc->bLayerInitialized = 1;
        
        if(lc->eDisplay3DMode != DISPLAY_3D_MODE_2D || lc->ePicture3DMode != PICTURE_3D_MODE_NONE)
           LayerSetDisplay3DMode(l, lc->eDisplay3DMode);
    }
    
    if(pBuf0->nWidth != lc->nWidth ||
       pBuf0->nHeight != lc->nHeight ||
       pBuf0->ePixelFormat != lc->eReceivePixelFormat)
    {
        //* config the display hardware again.
        //* TODO.
    }
    
    //* set picture to display hardware.
    setHwcLayerPictureInfo(lc, &layerInfo, pBuf0, pBuf1);
    args[0] = DISPLAY_CHANNEL;
    args[1] = DISPLAY_LAYER;
    args[2] = (unsigned long)(&layerInfo);
    args[3] = 0;
    ioctl(lc->fdDisplay, DISP_CMD_LAYER_SET_INFO, args);
    
    //* wait for new frame showed.
    if(lc->bLayerShowed == 1)
    {
        int nCurFrameId;
        int nWaitTime;
        
        args[0] = DISPLAY_CHANNEL;
        args[1] = DISPLAY_LAYER;
        args[2] = 0;
        args[3] = 0;
        
        nWaitTime = 50000;  //* max frame interval is 1000/24fps = 41.67ms, here we wait 50ms for max.
        do
        {
            nCurFrameId = ioctl(lc->fdDisplay, DISP_CMD_LAYER_GET_FRAME_ID, args);
            if(nCurFrameId == pBuf0->nID)
                break;
            else
            {
                if(nWaitTime <= 0)
                {
                    loge("check frame id fail, maybe something error with the HWC layer.");
                    break;
                }
                else
                {
                    usleep(5000);
                    nWaitTime -= 5000;
                }
            }
        }while(1);
    }
    
    //* attach the new picture to list and return the old picture.
    newNode = NULL;
    for(i=0; i<32; i++)
    {
        if(lc->picNodes[i].bUsed == 0)
        {
            newNode = &lc->picNodes[i];
            newNode->pNext              = NULL;
            newNode->bUsed              = 1;
            newNode->pPicture           = pBuf0;
            newNode->pSecondPictureOf3D = pBuf1;
            break;
        }
    }
    
    if(i == 32)
    {
        loge("not enough picture nodes, shouldn't run here.");
        abort();
    }
    
    if(lc->pPictureListHead != NULL)
    {
        nodePtr = lc->pPictureListHead;
        i = 1;
        while(nodePtr->pNext != NULL)
        {
            i++;
            nodePtr = nodePtr->pNext;
        }
        
        nodePtr->pNext = newNode;
        i++;
        
        //* return one picture.
        while(i > NUM_OF_PICTURES_KEEP_IN_LIST)
        {
            nodePtr = lc->pPictureListHead;
            lc->pPictureListHead = lc->pPictureListHead->pNext;
            lc->callback(lc->pUserData, MESSAGE_ID_LAYER_RETURN_BUFFER, (void*)nodePtr->pPicture);
            nodePtr->pPicture = NULL;
            if(nodePtr->pSecondPictureOf3D != NULL)
            {
                lc->callback(lc->pUserData, MESSAGE_ID_LAYER_RETURN_BUFFER, (void*)nodePtr->pSecondPictureOf3D);
                nodePtr->pSecondPictureOf3D = NULL;
            }
            nodePtr->bUsed = 0;
            i--;
        }
    }
    else
    {
        lc->pPictureListHead = newNode;
    }
    
    return 0;
}


static int SetLayerParam(LayerCtrlContext* lc)
{
    disp_pixel_format pixelFormat;
    disp_layer_info   layerInfo;
    unsigned long     args[4];
    
    //* close the layer first, otherwise, in case when last frame is kept showing,
    //* the picture showed will not valid because parameters changed.
    logv("Set layer param.");
    logv("temporally close the video layer when parameter changed.");
    lc->bLayerShowed = 0;
    args[0] = DISPLAY_CHANNEL;
    args[1] = DISPLAY_LAYER;
    args[2] = 0;
    args[3] = 0;
    ioctl(lc->fdDisplay, DISP_CMD_LAYER_DISABLE, args);
    
    //* transform pixel format.
    switch(lc->eReceivePixelFormat)
    {
        case PIXEL_FORMAT_YUV_PLANER_420:
            pixelFormat = DISP_FORMAT_YUV420_P;
            break;
        case PIXEL_FORMAT_YV12:
            pixelFormat = DISP_FORMAT_YUV420_P; //* can not find a pixel format for YV12 in drv_display.h, use YUV planer.
            break;
        case PIXEL_FORMAT_YUV_MB32_420:
            pixelFormat = DISP_FORMAT_YUV420_SP_TILED_UVUV;
            break;
        case PIXEL_FORMAT_YUV_MB32_422:
            pixelFormat = DISP_FORMAT_YUV422_SP_TILED_UVUV;
            break;
        case PIXEL_FORMAT_YUV_PLANER_422:
            pixelFormat = DISP_FORMAT_YUV422_P;
            break;
        default:
        {
            loge("unsupported pixel format.");
            return -1;
            break;
        }
    }

    //* initialize the layerInfo.
    args[0] = DISPLAY_CHANNEL;
    args[1] = DISPLAY_LAYER;
    args[2] = (unsigned long)(&layerInfo);
    args[3] = 0;
    ioctl(lc->fdDisplay, DISP_CMD_LAYER_GET_INFO, args);
    
    layerInfo.mode        = DISP_LAYER_WORK_MODE_SCALER;
    layerInfo.alpha_mode  = 1;
    layerInfo.alpha_value = 0xff;
    layerInfo.pipe        = 0;

    //* set color space
    layerInfo.fb.cs_mode = (lc->nHeight < 720) ? DISP_BT601 : DISP_BT709;
    layerInfo.fb.format  = pixelFormat;

    //* image size.
    layerInfo.fb.size.width     = lc->nWidth;
    layerInfo.fb.size.height    = lc->nHeight;

    //* source window.
    layerInfo.fb.src_win.x	    = lc->nLeftOff;
    layerInfo.fb.src_win.y	    = lc->nTopOff;
    layerInfo.fb.src_win.width  = lc->nDisplayWidth;
    layerInfo.fb.src_win.height = lc->nDisplayHeight;

    //* screen window.
    layerInfo.screen_win.x      = 0;
    layerInfo.screen_win.y      = 0;
    layerInfo.screen_win.width  = lc->nScreenWidth;
    layerInfo.screen_win.height = lc->nScreenHeight;

    //* set layerInfo to the driver.
    args[0] = DISPLAY_CHANNEL;
    args[1] = DISPLAY_LAYER;
    args[2] = (unsigned long)(&layerInfo);
    args[3] = 0;
    ioctl(lc->fdDisplay, DISP_CMD_LAYER_SET_INFO, args);
	
    //* set the video layer to the top.
    args[0] = DISPLAY_CHANNEL;
    args[1] = DISPLAY_LAYER;
    args[2] = 0;
    args[3] = 0;
    ioctl(lc->fdDisplay, DISP_CMD_LAYER_TOP, args);
    
    return 0;
}


static void setHwcLayerPictureInfo(LayerCtrlContext* lc,
                                   disp_layer_info*  pLayerInfo,
                                   VideoPicture*     pPicture,
                                   VideoPicture*     pSecondPictureOf3D)
{
    unsigned long   args[4];

    args[0] = DISPLAY_CHANNEL;
    args[1] = DISPLAY_LAYER;
    args[2] = (unsigned long)pLayerInfo;
    args[3] = 0;
    ioctl(lc->fdDisplay, DISP_CMD_LAYER_GET_INFO, args);
    
    if(pSecondPictureOf3D == NULL)
    {
        pLayerInfo->id         = pPicture->nID;
        pLayerInfo->fb.addr[0] = (unsigned long)MemAdapterGetPhysicAddressCpu(pPicture->pData0);
        pLayerInfo->fb.addr[1] = (unsigned long)MemAdapterGetPhysicAddressCpu(pPicture->pData1);
        //* should we set the address fb.addr[2] according to the pixel format?
    }
    else
    {
        pLayerInfo->id                   = pPicture->nID;
        pLayerInfo->fb.addr[0]           = (unsigned long)MemAdapterGetPhysicAddressCpu(pPicture->pData0);
        pLayerInfo->fb.addr[1]           = (unsigned long)MemAdapterGetPhysicAddressCpu(pPicture->pData1);
        pLayerInfo->fb.trd_right_addr[0] = (unsigned long)MemAdapterGetPhysicAddressCpu(pSecondPictureOf3D->pData0);
        pLayerInfo->fb.trd_right_addr[1] = (unsigned long)MemAdapterGetPhysicAddressCpu(pSecondPictureOf3D->pData1);
        //* should we set the address fb.addr[2] according to the pixel format?
    }
    
    //* set layer info
    ioctl(lc->fdDisplay, DISP_CMD_LAYER_SET_INFO, args);
    
    return;
}


int LayerCtrlShowVideo(LayerCtrl* l)
{
    LayerCtrlContext* lc;
    int               i;
    unsigned long     args[4];

    lc = (LayerCtrlContext*)l;
    logv("xxxx show video, current show flag = %d", lc->bLayerShowed);
    if(lc->bLayerShowed == 0)
    {
    	lc->bLayerShowed = 1;
        args[0] = DISPLAY_CHANNEL;
        args[1] = DISPLAY_LAYER;
        args[2] = 0;
        args[3] = 0;
        ioctl(lc->fdDisplay, DISP_CMD_LAYER_ENABLE, args);
    }
    return 0;
}


int LayerCtrlHideVideo(LayerCtrl* l)
{
    LayerCtrlContext* lc;
    int               i;
    unsigned long     args[4];

    lc = (LayerCtrlContext*)l;
    logv("xxxx hide video, current show flag = %d", lc->bLayerShowed);
    if(lc->bLayerShowed == 1)
    {
    	lc->bLayerShowed = 0;
        args[0] = DISPLAY_CHANNEL;
        args[1] = DISPLAY_LAYER;
        args[2] = 0;
        args[3] = 0;
        ioctl(lc->fdDisplay, DISP_CMD_LAYER_DISABLE, args);
    }
    return 0;
}


int LayerCtrlIsVideoShow(LayerCtrl* l)
{
    LayerCtrlContext* lc;

    lc = (LayerCtrlContext*)l;
    return lc->bLayerShowed;
}


int LayerCtrlHoldLastPicture(LayerCtrl* l, int bHold)
{
    LayerCtrlContext* lc;
    VPictureNode*     nodePtr;
    unsigned long     args[4];
    
    lc = (LayerCtrlContext*)l;
    
    logv("LayerCtrlHoldLastPicture, bHold = %d", bHold);
    
    if(bHold == 0)
    {
        if(gLastPicture != NULL)
        {
            FreePictureBuffer(gLastPicture);
            gLastPicture = NULL;
        }
        return 0;
    }
    
    VideoPicture* lastPicture;
        
    lastPicture = NULL;
    nodePtr = lc->pPictureListHead;
    if(nodePtr != NULL)
    {
        while(nodePtr->pNext != NULL)
            nodePtr = nodePtr->pNext;
        lastPicture = nodePtr->pPicture;
    }
        
    if(lastPicture != NULL)
    {
        if(gLastPicture != NULL)
            FreePictureBuffer(gLastPicture);
        
        gLastPicture = AllocatePictureBuffer(lastPicture->nWidth,
                                             lastPicture->nHeight,
                                             lastPicture->nLineStride,
                                             lastPicture->ePixelFormat);
        logd("width = %d, height = %d, pdata0 = %p", 
            gLastPicture->nWidth, gLastPicture->nHeight, gLastPicture->pData0);
        if(gLastPicture != NULL)
        {
            disp_layer_info layerInfo;
            
            gLastPicture->nID = 0xa5a5a5a5;
            int nGpuYAlign = 16;
            int nGpuCAlign = 8;
            RotatePicture(lastPicture, gLastPicture, 0,nGpuYAlign,nGpuCAlign);
    
            //* set picture to display hardware.
            setHwcLayerPictureInfo(lc, &layerInfo, gLastPicture, NULL);
            args[0] = DISPLAY_CHANNEL;
            args[1] = DISPLAY_LAYER;
            args[2] = (unsigned long)(&layerInfo);
            args[3] = 0;
            ioctl(lc->fdDisplay, DISP_CMD_LAYER_SET_INFO, args);
                                       
            //* wait for new frame showed.
            if(lc->bLayerShowed == 1)
            {
                int nCurFrameId;
                int nWaitTime;
        
                args[0] = DISPLAY_CHANNEL;
                args[1] = DISPLAY_LAYER;
                args[2] = 0;
                args[3] = 0;
                
                nWaitTime = 50000;  //* max frame interval is 1000/24fps = 41.67ms, here we wait 50ms for max.
                do
                {
                    nCurFrameId = ioctl(lc->fdDisplay, DISP_CMD_LAYER_GET_FRAME_ID, args);
                    if(nCurFrameId == gLastPicture->nID)
                        break;
                    else
                    {
                        if(nWaitTime <= 0)
                        {
                            loge("check frame id fail, maybe something error with the HWC layer.");
                            break;
                        }
                        else
                        {
                            usleep(5000);
                            nWaitTime -= 5000;
                        }
                    }
                }while(1);
            }
        }
    }
    
    return 0;
}
