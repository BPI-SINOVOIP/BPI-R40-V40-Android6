
#ifndef LAYER_CONTROL
#define LAYER_CONTROL

#include "player_i.h"
#include "videoDecComponent.h"
#include "vdecoder.h"

typedef void* LayerCtrl;

const int MESSAGE_ID_LAYER_RETURN_BUFFER = 0x31;

const int LAYER_RESULT_USE_OUTSIDE_BUFFER = 0x2;

typedef struct LayerControlOpsS
{

	LayerCtrl* (*LayerInit)(void*, int);

	void (*LayerRelease)(LayerCtrl* , int );

	int (*LayerSetExpectPixelFormat)(LayerCtrl* , enum EPIXELFORMAT );

	enum EPIXELFORMAT (*LayerGetPixelFormat)(LayerCtrl* );

	int (*LayerSetPictureSize)(LayerCtrl* , int , int );

	int (*LayerSetDisplayRegion)(LayerCtrl* , int , int , int , int );

	int (*LayerSetBufferTimeStamp)(LayerCtrl* , int64_t );

	int (*LayerDequeueBuffer)(LayerCtrl* , VideoPicture** , int);

	int (*LayerQueueBuffer)(LayerCtrl* , VideoPicture* , int);

	int (*LayerCtrlHideVideo)(LayerCtrl*);

	int (*LayerCtrlShowVideo)(LayerCtrl* );

	int (*LayerCtrlIsVideoShow)(LayerCtrl* );

	int (*LayerCtrlHoldLastPicture)(LayerCtrl* , int );

	int (*LayerSetRenderToHardwareFlag)(LayerCtrl* ,int );

	int (*LayerSetDeinterlaceFlag)(LayerCtrl* ,int );

	//* for old display 
	int (*LayerSetPicture3DMode)(LayerCtrl* , enum EPICTURE3DMODE );

	enum EPICTURE3DMODE (*LayerGetPicture3DMode)(LayerCtrl* );

	int (*LayerSetDisplay3DMode)(LayerCtrl* , enum EDISPLAY3DMODE );

	enum EDISPLAY3DMODE (*LayerGetDisplay3DMode)(LayerCtrl* );

	int (*LayerDequeue3DBuffer)(LayerCtrl* , VideoPicture** , VideoPicture** );

	int (*LayerQueue3DBuffer)(LayerCtrl* , VideoPicture* , VideoPicture* , int);

	int (*LayerGetRotationAngle)(LayerCtrl* );

	int (*LayerSetCallback)(LayerCtrl* , PlayerCallback , void* );

	//* end 

	//* for new display
	int (*LayerSetBufferCount)(LayerCtrl* , int );

	int (*LayerSetVideoWithTwoStreamFlag)(LayerCtrl* , int );

	int (*LayerSetIsSoftDecoderFlag)(LayerCtrl* , int);

	void (*LayerResetNativeWindow)(LayerCtrl* ,void*);

	int (*LayerReleaseBuffer)(LayerCtrl* ,VideoPicture*);

	VideoPicture* (*LayerGetPicNode)(LayerCtrl* );

	int (*LayerGetAddedPicturesCount)(LayerCtrl* );

	int (*LayerGetDisplayFPS)(LayerCtrl* );
	
}LayerControlOpsT;

#endif

