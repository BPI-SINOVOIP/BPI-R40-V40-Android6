
/*
********************************************************************************
*                                   AV Engine
*
*              Copyright(C), 2012-2016, All Winner Technology Co., Ltd.
*						        All Rights Reserved
*
* File Name   : avtimer.c
*
* Author      : XC
*
* Version     : 0.1
*
* Date        : 2012.03.20
*
* Description : This file implements a avtimer using the OS system clock.
*
* Others      : None at present.
*
* History     :
*
*  <Author>        <time>       <version>      <description>
*
*     XC         2012.03.20        0.1        build this file
*
********************************************************************************
*/

#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <malloc.h>
#include <memory.h>
#include <sys/time.h>
#include "avtimer.h"




//************************************************************//
//********************** implementation **********************//
//************************************************************//

AvTimer::AvTimer()
{
	mSpeed = 1000;
	mStartTime = -1;
	mStatus = 0;

	mStartOsTime.tv_sec  = 0;
	mStartOsTime.tv_usec = 0;
	mLastOsTime.tv_sec   = 0;
	mLastOsTime.tv_usec  = 0;

	pthread_mutex_init(&mMutex, NULL);
}


AvTimer::~AvTimer()
{
	pthread_mutex_destroy(&mMutex);
}


void AvTimer::SetTime(int64_t startTime)
{
    pthread_mutex_lock(&mMutex);

	mStartTime = startTime;

    gettimeofday(&mLastOsTime, NULL);

    mStartOsTime.tv_sec  = mLastOsTime.tv_sec;
    mStartOsTime.tv_usec = mLastOsTime.tv_usec;

    pthread_mutex_unlock(&mMutex);

    return;
}


void AvTimer::SetSpeed(int speed)
{
    int64_t passedTime;

    pthread_mutex_lock(&mMutex);

    //* record the system's time.
    gettimeofday(&mLastOsTime, NULL);

    //* change the start counting point.
    passedTime  = (int64_t)(mLastOsTime.tv_sec  - mStartOsTime.tv_sec) * 1000000;
    passedTime += (mLastOsTime.tv_usec - mStartOsTime.tv_usec);

    mStartTime += mSpeed * passedTime / 1000;

    mStartOsTime.tv_sec  = mLastOsTime.tv_sec;
    mStartOsTime.tv_usec = mLastOsTime.tv_usec;

    //* change the counting speed.
    mSpeed = speed;

    pthread_mutex_unlock(&mMutex);

    return;
}


void AvTimer::Start()
{
	pthread_mutex_lock(&mMutex);

    mStatus    = 1;

    //* record the system's time when timer starts.
    gettimeofday(&mStartOsTime, NULL);

    mLastOsTime.tv_sec  = mStartOsTime.tv_sec;
    mLastOsTime.tv_usec = mStartOsTime.tv_usec;

    pthread_mutex_unlock(&mMutex);

    return;
}


void AvTimer::Stop()
{
    int64_t passedTime;

    pthread_mutex_lock(&mMutex);

    //* record the system's time when timer stops.
    gettimeofday(&mLastOsTime, NULL);

    //* change the start counting point.
    passedTime  = (int64_t)(mLastOsTime.tv_sec  - mStartOsTime.tv_sec) * 1000000;
    passedTime += (mLastOsTime.tv_usec - mStartOsTime.tv_usec);

    mStartTime += mSpeed * passedTime / 1000;

    mStartOsTime.tv_sec  = mLastOsTime.tv_sec;
    mStartOsTime.tv_usec = mLastOsTime.tv_usec;

    mStatus = 0;

    pthread_mutex_unlock(&mMutex);

    return;
}


void AvTimer::Pause()
{
    int64_t passedTime;

    pthread_mutex_lock(&mMutex);

    if(mStatus == 1)
    {
        //* record the system's time when timer stops.
        gettimeofday(&mLastOsTime, NULL);

        //* change the start counting point.
        passedTime  = (int64_t)(mLastOsTime.tv_sec  - mStartOsTime.tv_sec) * 1000000;
        passedTime += (mLastOsTime.tv_usec - mStartOsTime.tv_usec);

        mStartTime += mSpeed * passedTime / 1000;

        mStartOsTime.tv_sec  = mLastOsTime.tv_sec;
        mStartOsTime.tv_usec = mLastOsTime.tv_usec;

        mStatus = 2;
    }

    pthread_mutex_unlock(&mMutex);

    return;
}



int AvTimer::GetSpeed()
{
    return mSpeed;
}


int64_t AvTimer::GetTime()
{
	int64_t curTime;
    int64_t passedTime;

    pthread_mutex_lock(&mMutex);

    if(mStatus == 1)
    {
        //* record the system's time.
        gettimeofday(&mLastOsTime, NULL);

        //* change the start counting point.
        passedTime  = (int64_t)(mLastOsTime.tv_sec  - mStartOsTime.tv_sec) * 1000000;
        passedTime += (mLastOsTime.tv_usec - mStartOsTime.tv_usec);

        curTime = mStartTime + (mSpeed * passedTime / 1000);
    }
    else    //* in stop or pause status.
    {
        //* return the last record time.
    	curTime = mStartTime;
    }

    pthread_mutex_unlock(&mMutex);

    return curTime;
}


int AvTimer::GetStatus()
{
	return mStatus;
}



