#include <stdio.h>
#include <unistd.h>
#include <utils/Log.h>
#include <binder/IServiceManager.h>
#include <binder/IPCThreadState.h>
#include <gui/ISurfaceComposer.h>
#include <gui/Surface.h>
#include <gui/SurfaceComposerClient.h>
#include <ui/DisplayInfo.h>
#include <utils/RefBase.h>
#include <utils/Log.h>
#include <cutils/memory.h>
#include <android/native_window.h>
using namespace android;
#include "libSurface.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "libSurface"

#define PLANE_WIDTH 1280
#define PLANE_HEIGHT 720

static sp<Surface> videoSurface = NULL;
static sp<SurfaceControl> videoSfControl = NULL;

static sp<SurfaceComposerClient> client = NULL;
static DisplayInfo dinfo;

int sw_surface_init()
{
	sp<ProcessState> proc(ProcessState::self());
	ProcessState::self()->startThreadPool();

	client = new SurfaceComposerClient();
	if (client != NULL)
	{
	    sp<IBinder> display(SurfaceComposerClient::getBuiltInDisplay(
            ISurfaceComposer::eDisplayIdMain));
		status_t status = client->getDisplayInfo(display, &dinfo);
		ALOGD("getDisplayInfo,status(%d)", status);
		if (status != 0)
		{
			dinfo.w = PLANE_WIDTH;
			dinfo.h = PLANE_HEIGHT;
		}

	}
	ALOGD("dinfo.w(%d), dinfo.h(%d)", dinfo.w, dinfo.h);

	videoSfControl = client->createSurface(
                String8("Surface.iptv"), dinfo.w, dinfo.h, HAL_PIXEL_FORMAT_YCrCb_420_SP, 0);
	if(videoSfControl == NULL)
		return -1;

	SurfaceComposerClient::openGlobalTransaction();
	videoSfControl->setLayer(0x7fffffff);
	videoSfControl->show();

	videoSfControl->setSize(dinfo.w, dinfo.h);
	videoSfControl->setPosition(0, 0);
    Rect outCrop;
    outCrop.left = 0;
    outCrop.right = 1280;
    outCrop.top = 0;
    outCrop.bottom = 720;
    videoSfControl->setCrop(outCrop);
	//videoSfControl->setLayer(0x2000000 - 1);
	//videoSfControl->show();
	SurfaceComposerClient::closeGlobalTransaction();
	videoSurface = videoSfControl->getSurface();
    ALOGD("%s surface0 = %p", __func__, videoSurface.get());
    ANativeWindow_Buffer outBuffer;
    videoSurface->lock(&outBuffer, NULL);
    memset((char*)outBuffer.bits, 0x10,(outBuffer.height * outBuffer.stride));
    memset((char*)outBuffer.bits + outBuffer.height * outBuffer.stride,0x80,(outBuffer.height * outBuffer.stride)/2);
    videoSurface->unlockAndPost();

	return 0;
}

void sw_surface_exit()
{
	client = NULL;
    videoSurface = NULL;
	videoSfControl = NULL;

	return ;
}

void* swget_VideoSurface( )
{
    ALOGD("%s surface = %p", __func__, videoSurface.get());
	return videoSurface.get();
	//return (videoSurface->getSurfaceTexture()).get();
}
