
#ifndef AW_LOG_RECORDER_H
#define AW_LOG_RECORDER_H

namespace android
{

typedef void* AwLogRecorder;

AwLogRecorder* AwLogRecorderCreate(void);

void AwLogRecorderDestroy(AwLogRecorder* l);

//int AwLogRecorderRecord(AwLogRecorder* l, char* url, unsigned int nPlayTimeMs, int status);
int AwLogRecorderRecord(AwLogRecorder* l, char* cmccLog);
    
}

#endif
