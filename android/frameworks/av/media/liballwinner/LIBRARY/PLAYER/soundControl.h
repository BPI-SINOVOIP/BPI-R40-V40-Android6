
#ifndef SOUND_CONTROL_H
#define SOUND_CONTROL_H

#include "player_i.h"

typedef void* SoundCtrl;

typedef void (*RawCallback)(void *self, void *param);

SoundCtrl* SoundDeviceInit(void* pAudioSink);

void SoundDeviceRelease(SoundCtrl* s);

void SoundDeviceSetFormat(SoundCtrl* s, unsigned int nSampleRate, unsigned int nChannelNum);

int SoundDeviceStart(SoundCtrl* s);

int SoundDeviceStop(SoundCtrl* s);

int SoundDevicePause(SoundCtrl* s);

int SoundDeviceWrite(SoundCtrl* s, void* pData, int nDataSize);

int SoundDeviceReset(SoundCtrl* s);

int SoundDeviceGetCachedTime(SoundCtrl* s);

SoundCtrl* SoundDeviceInit_raw(void* raw_data,void* hdeccomp,RawCallback callback);

void SoundDeviceRelease_raw(SoundCtrl* s);

void SoundDeviceSetFormat_raw(SoundCtrl* s, unsigned int nSampleRate, unsigned int nChannelNum);

int SoundDeviceStart_raw(SoundCtrl* s);

int SoundDeviceStop_raw(SoundCtrl* s);

int SoundDevicePause_raw(SoundCtrl* s);

int SoundDeviceWrite_raw(SoundCtrl* s, void* pData, int nDataSize);

int SoundDeviceReset_raw(SoundCtrl* s);

int SoundDeviceGetCachedTime_raw(SoundCtrl* s);

int SoundDeviceSetVolume(SoundCtrl* s, float volume);

int SoundDeviceGetVolume(SoundCtrl* s, float *volume);


#endif

