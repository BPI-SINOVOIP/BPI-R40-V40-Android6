/*
********************************************************************************
*                                   AV Engine
*
*              Copyright(C), 2012-2016, All Winner Technology Co., Ltd.
*						        All Rights Reserved
*
* File Name   : avtimer.h
*
* Author      : XC
*
* Version     : 0.1
*
* Date        : 2012.03.13
*
* Description : This file defines the prototype of AVTimer used in avengine.
*               The AVTimer is used for video synchronization.
*
* Others      : None at present.
*
* History     :
*
*  <Author>        <time>       <version>      <description>
*
*     XC         2012.03.13        0.1        build this file
*
********************************************************************************
*/

#ifndef AVTIMER_H
#define AVTIMER_H

#include <pthread.h>
#include <sys/time.h>

class AvTimer
{
public:
	AvTimer();
	virtual ~AvTimer();

	virtual void    SetTime(int64_t startTime);
	virtual void    SetSpeed(int speed);
	virtual void    Start();
	virtual void    Stop();
	virtual void    Pause();
	virtual int     GetSpeed();
	virtual int64_t GetTime();
	virtual int     GetStatus();

public:
	int             mSpeed;
	int64_t         mStartTime;
	int             mStatus;
	struct timeval  mStartOsTime;
	struct timeval  mLastOsTime;
	pthread_mutex_t mMutex;
};


#endif

