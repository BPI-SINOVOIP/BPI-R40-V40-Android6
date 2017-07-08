
#include "soundControl.h"
#include "log.h"
#include <pthread.h>

typedef struct SoundCtrlContext
{
}SoundCtrlContext;

static int SoundDeviceStop_l(SoundCtrlContext* sc);

SoundCtrl* SoundDeviceInit(void* pAudioSink)
{
    return NULL;
}


void SoundDeviceRelease(SoundCtrl* s)
{
    return;
}


void SoundDeviceSetFormat(SoundCtrl* s, unsigned int nSampleRate, unsigned int nChannelNum)
{
    return;
}


int SoundDeviceStart(SoundCtrl* s)
{
    return 0;
}


int SoundDeviceStop(SoundCtrl* s)
{
    return 0;
}


static int SoundDeviceStop_l(SoundCtrlContext* sc)
{
    return 0;
}


int SoundDevicePause(SoundCtrl* s)
{
    return 0;
}


int SoundDeviceWrite(SoundCtrl* s, void* pData, int nDataSize)
{
    return 0;
}


//* called at player seek operation.
int SoundDeviceReset(SoundCtrl* s)
{
    return SoundDeviceStop(s);
}


int SoundDeviceGetCachedTime(SoundCtrl* s)
{
    return 0;
}
int SoundDeviceSetVolume(SoundCtrl* s, float volume)
{
	return 0;
}
int SoundDeviceGetVolume(SoundCtrl* s, float *volume)
{
	return 0;
}


