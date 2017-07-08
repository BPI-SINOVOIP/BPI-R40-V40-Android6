
//#define LOG_NDEBUG 0
#define LOG_TAG "AwLogRecorder"
#include <utils/Log.h>

#include "awLogRecorder.h"
#include "awMessageQueue.h"
#include <media/mediaplayer.h>

#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <malloc.h>
#include <semaphore.h>
#include <dirent.h>
#include <errno.h>
#include<sys/stat.h>
#include<fcntl.h>
#include "log.h"

namespace android
{

typedef struct MESSAGENODE MessageNode;
struct MESSAGENODE
{
    int64_t      nSysTime;
    char*        url;
    int          nPlayeTimeMs;
    int          status;
    MessageNode* pNext;
};


typedef struct AwLogRecorderContext
{
    int64_t         nThreadStartTime;
    int64_t         nFileStartTime;   // UTC

    MessageNode*    pMsgList;
    MessageNode*    pMsgListTail;

    char            strFilePath[5][64];
    struct tm       fileTime[5];
    int             nFileCount;
    int             nFilePathArrayStartPos;

    pthread_t       threadId;
    AwMessageQueue* mq;
	sem_t           semNewLog;
	sem_t           semQuit;
	
	int             nNewLogReply;

    FILE*           fp;
    int             nCreate;
    int             bufferStartFlag;
    char            url[4096];
    int             nPlayTime;

}AwLogRecorderContext;

static const int LOGRECORDER_MSG_QUIT   = 0x101;
static const int LOGRECORDER_MSG_NEWLOG = 0x102;
 

static void* AwLogRecorderThread(void* arg);
static void  AddOneMessage(AwLogRecorderContext* lc, char* url, int nPlayeTimeMs, int status);
static void TimeFormatTransform(int64_t nTimeUs, char strTime[64], const char* prefix, const char* subfix);
static void  SaveNewFile(AwLogRecorderContext* lc);
static void  DeleteOneFile(AwLogRecorderContext* lc);
static void  DeleteFiles(AwLogRecorderContext* lc);

typedef int sou(const dirent**, const dirent**);

// we should get the exist record file, and delete the no used file
static void CreateFile(AwLogRecorder l)
{
    AwLogRecorderContext* lc;     
    lc = (AwLogRecorderContext*)l;
    lc->nCreate = 1;

    struct timeval tv;
    gettimeofday(&tv, NULL);  //UTC
    lc->nFileStartTime = tv.tv_sec * 1000000ll + tv.tv_usec;

    struct tm ptime; // the time from the record file name

    time_t curTime;
    struct tm* pCurTime;

    
    time(&curTime);
    pCurTime = localtime(&curTime);
    //ALOGD("curTime: %d-%d-%d-%d-%d-%d \n", pCurTime->tm_year, pCurTime->tm_mon, pCurTime->tm_mday, pCurTime->tm_hour, pCurTime->tm_min, pCurTime->tm_sec);
    ptime = *pCurTime;  // we must do  this, because we need mktime

    char fileName[256];

    static char playLogPath[] = "/tmp/playInfoLog/";
    struct dirent **ptr=NULL;
    int fileNum;
    char tmp;
    int nPos; 
    int nCreateFileNum = -1;
    int nLastMin = -1;
    struct tm p;
	int removeFileMin = -1;
	char lastFileName[256];

	fileNum = scandir(playLogPath, &ptr, 0, (sou*)alphasort);
	if(fileNum<0)
	{
		return;
	}

	int i;
	for(i=0; i<fileNum; i++)
	{
		if((ptr[i]->d_name[0] == '.') || (!strstr(ptr[i]->d_name, ".txt")))
            continue;
		//ALOGD("++ filename = %s", ptr[i]->d_name);
		sprintf(fileName, "%s%s", playLogPath, ptr[i]->d_name);

        //ALOGD("++ filename = %s", fileName);

        tmp = ptr[i]->d_name[4];
		ptr[i]->d_name[4] = '\0';
		ptime.tm_year = atoi(ptr[i]->d_name)-1900;
        if(ptime.tm_year != pCurTime->tm_year)
        {
            remove(fileName);
            continue;
        }
		
		ptr[i]->d_name[4] = tmp;
		tmp = ptr[i]->d_name[6];
		ptr[i]->d_name[6] = '\0';
		ptime.tm_mon = atoi(ptr[i]->d_name+4)-1;
        if(ptime.tm_mon != pCurTime->tm_mon)
        {
            remove(fileName);
            continue;
        }
		
		ptr[i]->d_name[6] = tmp;
		tmp = ptr[i]->d_name[8];
		ptr[i]->d_name[8] = '\0';
		ptime.tm_mday = atoi(ptr[i]->d_name+6);
        if(ptime.tm_mday != pCurTime->tm_mday)
        {
            remove(fileName);
            continue;
        }
		
		ptr[i]->d_name[8] = tmp;
		tmp = ptr[i]->d_name[10];
		ptr[i]->d_name[10] = '\0';
		ptime.tm_hour = atoi(ptr[i]->d_name+8);		
		if(ptime.tm_hour > pCurTime->tm_hour)
        {
        	remove(fileName);
            continue;
        }
		
		ptr[i]->d_name[10] = tmp;
		tmp = ptr[i]->d_name[12];
		ptr[i]->d_name[12] = '\0';
		ptime.tm_min = atoi(ptr[i]->d_name+10);

		if((pCurTime->tm_hour == ptime.tm_hour) && (pCurTime->tm_min < ptime.tm_min ))
		{
			remove(fileName);
        	continue;
		}

		if(((pCurTime->tm_hour == ptime.tm_hour) && (pCurTime->tm_min - ptime.tm_min <= 15))
			|| ((pCurTime->tm_hour == (ptime.tm_hour+1)) && (60+ pCurTime->tm_min - ptime.tm_min <= 15)))
		{
			//ALOGD("open the exist logrecorder file <%s>", fileName);
			lc->fp = fopen(fileName, "at+");
			if(!lc->fp)
			{
				ALOGW("--- can not open this file, %s", fileName);
			}
			else
		    {
		    	int fd = fileno(lc->fp);
    			fchmod(fd, 0777);
		    }
		}
		else if(((pCurTime->tm_hour == ptime.tm_hour) && (pCurTime->tm_min - ptime.tm_min <= 60))
			|| ((pCurTime->tm_hour == (ptime.tm_hour+1)) && (pCurTime->tm_min <= ptime.tm_min )))
		{
			// do nothing
			//ALOGD("save this file %s", fileName);
		}
		else
		{
			//ALOGD("remove this file %s", fileName);
			remove(fileName);
			continue;
		}


        nCreateFileNum = (pCurTime->tm_min - ptime.tm_min)/15;
        nLastMin = ptime.tm_min;
		
		ptr[i]->d_name[12] = tmp;
		tmp = ptr[i]->d_name[14];
		ptr[i]->d_name[14] = '\0';
		ptime.tm_sec = atoi(ptr[i]->d_name+12);
		
		//ALOGD("get time from file name: %d-%d-%d-%d-%d-%d \n", ptime.tm_year, ptime.tm_mon, ptime.tm_mday, ptime.tm_hour, ptime.tm_min, ptime.tm_sec);

        lc->nFileStartTime = (int64_t)mktime(&ptime) * 1000000ll;
        //ALOGD("-- nFileStartTime = %lld, gettimeofday = %lld, timezone = %s", lc->nFileStartTime, tv.tv_sec * 1000000ll + tv.tv_usec, ptime.tm_zone);

        nPos = lc->nFilePathArrayStartPos + lc->nFileCount;
        lc->fileTime[nPos] = ptime;
        strcpy(lc->strFilePath[nPos], fileName);
        lc->nFileCount++;

        free(ptr[i]);
    }

	free(ptr);
	
    if(lc->fp)
        return;
 /*
 	int i;
 	for(i=0; i<lc->nFileCount; i++)
 	{
 		nPos = (lc->nFilePathArrayStartPos + lc->nFileCount)%5;
 		if((pCurTime->tm_hour == (lc->fileTime[nPos].tm_hour+1)) && (ptime.tm_min > removeFileMin))
        {     
        ALOGD("removeFileMin = %d, lc->strFilePath[nPos] = %s", removeFileMin, lc->strFilePath[nPos]);
        	if(removeFileMin != -1)
        	{
        		remove(lastFileName);
        		memcpy(lastFileName, lc->strFilePath[nPos], 256);
        		lc->nFilePathArrayStartPos++;
        		continue;
        	}
        	removeFileMin = ptime.tm_min;
        	memcpy(lastFileName, lc->strFilePath[nPos], 256);
        }
 	}*/
    char strTime[256];
    TimeFormatTransform(lc->nFileStartTime, strTime, playLogPath, ".txt");
    //ALOGD("fileName = %s", strTime);
    
    p.tm_year = pCurTime->tm_year;
    p.tm_mon  = pCurTime->tm_mon;
    p.tm_mday = pCurTime->tm_mday;
    p.tm_hour = pCurTime->tm_hour;
    p.tm_min  = pCurTime->tm_min;
    p.tm_sec  = pCurTime->tm_sec;
    
    lc->fp = fopen(strTime, "a+");
    if(!lc->fp)
    {
        ALOGW("++++ cannot create the playLog file <%s>", strTime);
    }
    else
    {
    	int fd = fileno(lc->fp);
    	fchmod(fd, 0777);
    }
    
    nPos = lc->nFilePathArrayStartPos + lc->nFileCount;
    strcpy(lc->strFilePath[nPos], strTime);
    lc->nFileCount++;
}


AwLogRecorder* AwLogRecorderCreate(void)
{
    AwLogRecorderContext* l;
    
    l = (AwLogRecorderContext*)malloc(sizeof(AwLogRecorderContext));
    if(l == NULL)
    {
        ALOGE("malloc memory fail.");
        return NULL;
    }
    memset(l, 0, sizeof(AwLogRecorderContext));
    
    sem_init(&l->semNewLog, 0, 0);
    sem_init(&l->semQuit, 0, 0);
    
    l->mq = AwMessageQueueCreate(64);
    if(l->mq == NULL)
    {
        ALOGE("AwMessageQueueCreate() return fail.");
        sem_destroy(&l->semNewLog);
        sem_destroy(&l->semQuit);
        free(l);
        return NULL;
    }

    CreateFile((void*)l);
    
    if(pthread_create(&l->threadId, NULL, AwLogRecorderThread, (void*)l) != 0)
    {
        ALOGE("can not create thread for log record.");
        AwMessageQueueDestroy(l->mq);
        sem_destroy(&l->semNewLog);
        sem_destroy(&l->semQuit);
        free(l);
        return NULL;
    }
    
    return (AwLogRecorder*)l;
}

void AwLogRecorderDestroy(AwLogRecorder* l)
{
    void* status;
    
    AwMessage msg;
    AwLogRecorderContext* lc;
    
    lc = (AwLogRecorderContext*)l;
    
    if(lc->bufferStartFlag)
    {
    	//AwLogRecorderRecord(l, lc->url, lc->nPlayTime, MEDIA_INFO_BUFFERING_END);
    }
    
    //* send a quit message.
    setMessage(&msg, LOGRECORDER_MSG_QUIT, (uintptr_t)&lc->semQuit);
    AwMessageQueuePostMessage(lc->mq, &msg);
    SemTimedWait(&lc->semQuit, -1);
    
    pthread_join(lc->threadId, &status);
    
    if(lc->mq != NULL)
        AwMessageQueueDestroy(lc->mq);

    if(lc->fp)
    {
        fclose(lc->fp);
    }
    sem_destroy(&lc->semNewLog);
    sem_destroy(&lc->semQuit);
    free(lc);
    
    return;
}

int AwLogRecorderRecord(AwLogRecorder* l, char* cmccLog)
{
    AwMessage msg;
    AwLogRecorderContext* lc;
    
    lc = (AwLogRecorderContext*)l;

    //* send a cmcc log message.
    logv("++++++++++ new cmcc log message, log = %p", cmccLog);
    setMessage(&msg, 
               LOGRECORDER_MSG_NEWLOG,          //* message id.
               (uintptr_t)&lc->semQuit,      //* params[0] = &semQuit.
               (uintptr_t)&lc->nNewLogReply, //* params[1] = &nNewLogReply.
               (uintptr_t)cmccLog);          //* params[2] = cmcc log string.
    logv("+++++++++>> param2 = %p.", msg.params[2]);
    AwMessageQueuePostMessage(lc->mq, &msg);
    SemTimedWait(&lc->semQuit, -1);
    
    return lc->nNewLogReply;
}

static void* AwLogRecorderThread(void* arg)
{
    AwMessage             msg;
    int                   ret;
    sem_t*                pReplySem;
    int*                  pReplyValue;
    AwLogRecorderContext* lc;
    struct timeval        tv;
    char*                 cmccLog;
    int                   nPlayTimeMs;
    int                   status;
    int64_t               nCurTime;
    int64_t               nTimeDiff;

    lc = (AwLogRecorderContext*)arg;

    gettimeofday(&tv, NULL);
    lc->nThreadStartTime = lc->nFileStartTime;
    //ALOGD("+++ nFileStartTime = %lld", lc->nFileStartTime);
    //lc->nFileStartTime   = lc->nThreadStartTime;

    while(1)
    {
        if(AwMessageQueueTryGetMessage(lc->mq, &msg, 5000) == 0)
        {
            pReplySem   = (sem_t*)msg.params[0];
            pReplyValue = (int*)msg.params[1];

            if(msg.messageId == LOGRECORDER_MSG_NEWLOG)
            {
                //* record the message to list.
                cmccLog         = (char*)msg.params[2];
                logv("+++++++++<< param2 = %p.", msg.params[2]);

                gettimeofday(&tv, NULL);
                int64_t nSysTime     = tv.tv_sec * 1000000ll + tv.tv_usec;

                //ALOGD("--- lc->fp == NULL %p", lc->fp);
				
                if(lc->fp != NULL)
                {
                    char buf[4096];
                    TimeFormatTransform(nSysTime, buf, "[", "]");
                    ret = fprintf(lc->fp, "%s%s\n", buf, cmccLog);
                    if(ret < 0)
                    {
                        loge("return of fprintf %d, errno = %d", ret, errno);
                    }
                    
                    fprintf(lc->fp, "\n");
                }

                if(pReplyValue != NULL)
                    *pReplyValue = 0;
                if(pReplySem != NULL)
                    sem_post(pReplySem);
                continue;
            } //* end LOGRECORDER_MSG_NEWLOG.
            else if(msg.messageId == LOGRECORDER_MSG_QUIT)
            {
                ALOGD("+++++++++ quit message");
                //* save message list to file.
                SaveNewFile(lc);

                if(pReplyValue != NULL)
                    *pReplyValue = 0;
                if(pReplySem != NULL)
                    sem_post(pReplySem);
                break;
            } //* end LOGRECORDER_MSG_QUIT.
        }

        gettimeofday(&tv, NULL);
        nCurTime = tv.tv_sec * 1000000ll + tv.tv_usec; 

        //* check whether it is time to save new log file, according to nFileStartTime.
        nTimeDiff = nCurTime - lc->nFileStartTime;
        //ALOGD("+++++ nTimeDiff = %lld, nCurTime = %lld, startTime = %lld", nTimeDiff, nCurTime,lc->nFileStartTime);
        if(nTimeDiff >= 15*60*1000000ll)  //* more than 15 minutes.
        {
            //ALOGD("+++++++ time to save a new file.");
            lc->nFileStartTime += 15*60*1000000ll;
            SaveNewFile(lc);
        }

		/*
        // check whether it is time to clear old log files, according to nThreadStartTime.
        nTimeDiff = nCurTime - lc->nThreadStartTime;
        ALOGD("+++++ nTimeDiff = %lld, nCurTime = %lld, threadStartTime = %lld", nTimeDiff, nCurTime,lc->nThreadStartTime);
        if(nTimeDiff >= 60*60*1000000ll)
        {
            ALOGD("+++++++ time to clear files.");
            DeleteFiles(lc);
            lc->nThreadStartTime += 60*60*1000000ll;

            time_t t = (time_t)lc->nThreadStartTime/1000000ll;
            t += 8*3600;
        }*/
    }

    return NULL;
}


static void  AddOneMessage(AwLogRecorderContext* lc, char* url, int nPlayTimeMs, int status)
{
    MessageNode*   pMsg;
    struct timeval tv;

    pMsg = (MessageNode*)malloc(sizeof(MessageNode));
    if(pMsg == NULL)
    {
        ALOGW("malloc memory for log message fail.");
        return;
    }
    memset(pMsg, 0, sizeof(MessageNode));

    gettimeofday(&tv, NULL);
    pMsg->nSysTime     = tv.tv_sec * 1000000ll + tv.tv_usec;
    pMsg->nPlayeTimeMs = nPlayTimeMs;
    pMsg->status       = status;
    pMsg->pNext        = NULL;
    pMsg->url          = strdup(url);
    if(pMsg->url == NULL)
    {
        ALOGW("malloc memory for log message fail.");
        free(pMsg);
        return;
    }

    //ALOGD("+++++++++++ add one message, status = %d, nPlayTimeMs = %d", status, nPlayTimeMs);

    if(lc->pMsgListTail != NULL)
    {
        lc->pMsgListTail->pNext = pMsg;
        lc->pMsgListTail = pMsg;
    }
    else
        lc->pMsgList = lc->pMsgListTail = pMsg;

    return;
}


static void TimeFormatTransform(int64_t nTimeUs, char* strTime, const char* prefix, const char* subfix)
{
    struct tm* p;
    time_t     nTimeSec;
    nTimeSec = (time_t)(nTimeUs/1000000);
    //nTimeSec += 8*3600;
    p = localtime(&nTimeSec);
    sprintf(strTime, "%s%d%02d%02d%02d%02d%02d%s",
            prefix, 1900+p->tm_year, 1+p->tm_mon, p->tm_mday, p->tm_hour, p->tm_min, p->tm_sec, subfix);
    return;
}


static void  SaveNewFile(AwLogRecorderContext* lc)
{
    char           strFilePath[64];
    MessageNode*   pMsg;
    struct timeval tv;
    int            nPos;

    //* set file name.
    TimeFormatTransform(lc->nFileStartTime, strFilePath, "/tmp/playInfoLog/", ".txt");
    
    //* save file if message list is not empty.
    //ALOGD("++++++++++++++ create a new file, filename is %s", strFilePath);
    if(lc->fp)
    {
        fclose(lc->fp);
        lc->fp = NULL;
    }
    lc->fp = fopen(strFilePath, "a+");
    if(!lc->fp)
    {
        ALOGW("++++ cannot create the playLog file <%s>, errno(%d)", strFilePath, errno);
    }
    else
    {
    	int fd = fileno(lc->fp);
    	fchmod(fd, 0777);
    }

    if(lc->fp != NULL)
    {
        // delete one file if file array is full.
        if(lc->nFileCount == 5)
            DeleteOneFile(lc);

        // add file to list.
        nPos = lc->nFilePathArrayStartPos + lc->nFileCount;
        if(nPos >= 5)
            nPos -= 5;
        strcpy(lc->strFilePath[nPos], strFilePath);
        lc->nFileCount++;
    }

    //gettimeofday(&tv, NULL);
    //lc->nFileStartTime = tv.tv_sec * 1000000ll + tv.tv_usec;  //UTC

    return;
}

static void  DeleteOneFile(AwLogRecorderContext* lc)
{
    if(lc->nFileCount > 0)
    {
        //* delete the file lc->strFilePath[lc->nFilePathArrayStartPos].
        remove(lc->strFilePath[lc->nFilePathArrayStartPos]);

        lc->nFilePathArrayStartPos++;
        if(lc->nFilePathArrayStartPos >= 5)
            lc->nFilePathArrayStartPos -= 5;
        lc->nFileCount--;
    }
}

static void DeleteFiles(AwLogRecorderContext* lc)
{
    while(lc->nFileCount > 2)
        DeleteOneFile(lc);
    return;
}


}
