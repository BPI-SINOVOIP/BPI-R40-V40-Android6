#include "SkMultiThreadCanvas.h"

bool bExitComputeThread    = false;
bool bComputeThreadCreated = false;

////////////////////////////////////////////////////////////////////////////////
//thread code
void *compute_thread_code(void *args){
    SkCanvasComputeThreadWrapper* pHandler = (SkCanvasComputeThreadWrapper*) args;
    MY_LOGD("Thread: pid:%d, thread id:%d",getpid(),gettid());
    while(1){
        SkTask_Base *pTask = pHandler->threadGetTask();
        if(pTask != NULL){
            pTask->go();
			pTask->signal();
        }else{
            pHandler->threadWaitTask();//
        }

        if(bExitComputeThread == true){
            break;
        }
    }
    return NULL;
}

#ifdef AW_TEST_USE_DOUBLE_COMPUTE_THREAD
void *compute_thread2_code(void *args){
	SkCanvasComputeThreadWrapper* pHandler = (SkCanvasComputeThreadWrapper*) args;
	MY_LOGD("Thread: pid:%d, thread id:%d",getpid(),gettid());
	while(1){
		SkTask_Base *pTask = pHandler->thread2GetTask();
        if(pTask != NULL){
            pTask->go();
			pTask->signal();
        }else{
            pHandler->thread2WaitTask();//
        }

        if(bExitComputeThread == true){
            break;
        }
	}
	return NULL;
}
#endif /*AW_TEST_USE_DOUBLE_COMPUTE_THREAD*/


/////////////////////////////////////////////////////////////////////////////////
//
SkTask_Base::SkTask_Base(){
    sem_init(&sem,0,0);
}
SkTask_Base::~SkTask_Base(){
    sem_destroy(&sem);
}
bool SkTask_Base::wait(){
    int ret = sem_wait(&sem);
    if(ret != 0){
        return false;
    }
    return true;
}
bool SkTask_Base::signal(){
    int ret = sem_post(&sem);
    if(ret != 0){
        return false;
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////////
//
SkCanvasComputeThreadWrapper::SkCanvasComputeThreadWrapper(){
    currentProcessName = NULL;
	nFirstRef = 0;
}
SkCanvasComputeThreadWrapper::~SkCanvasComputeThreadWrapper(){
	int ret = 0;
	if(bComputeThreadCreated == true){
		bExitComputeThread = true;
        ret = pthread_join(threadID, NULL);
		if(ret != 0){
			MY_LOGD("Destruct global object failed. ret:%d",ret);
		}
        pthread_mutex_destroy(&mutex);
        pthread_cond_destroy(&cond);
        delete pTaskQueue;
		pTaskQueue = NULL;
	}
}
bool SkCanvasComputeThreadWrapper::canUseThread(){
	if(nFirstRef == 0){/*We expect that each process only do this work just once.*/
		bCanUseThread = processCanEnableThread();
        if(bCanUseThread == true && !bComputeThreadCreated){
			bComputeThreadCreated = real_init();
        }
		nFirstRef = -1;
	}
	return bComputeThreadCreated;
}
bool SkCanvasComputeThreadWrapper::real_init(){
	bool bSuccess = false;
    int ret = pthread_create(&threadID, NULL, compute_thread_code, (void *)this);
    if(ret == 0){
        pthread_mutex_init(&mutex,NULL);
        pthread_cond_init(&cond,NULL);
        pTaskQueue = new SkTasksQueue();
		bSuccess = true;
    }else{
        MY_LOGD("create thread 0 for process %s failed",currentProcessName);
		bSuccess = false;
    }

#ifdef AW_TEST_USE_DOUBLE_COMPUTE_THREAD
    pthread_create(&threadID2, NULL, compute_thread2_code, (void *)this);
	pthread_mutex_init(&mutex2,NULL);
    pthread_cond_init(&cond2,NULL);
    pTaskQueue2 = new SkTasksQueue();
#endif /*AW_TEST_USE_DOUBLE_COMPUTE_THREAD*/

out:
	return bSuccess;
}

const char* SkCanvasComputeThreadWrapper::PROCESS_NAME[] = {"org.zeroxlab.zeroxbenchmark","cn.wps"};
bool SkCanvasComputeThreadWrapper::processCanEnableThread(){
	char procPath[1024];
	int ret = false;
    //sprintf(procPath, "/proc/%d/status", getpid());
    sprintf(procPath, "/proc/%d/cmdline", getpid());
	FILE *pFile = fopen(procPath,"r");
	if(pFile != NULL){
		char buf[128];
		int readCount = fread(buf,1,128,pFile);
        if(readCount == 0){
			MY_LOGD("read %s 0 chars, errno:%d \n",procPath,errno);
			goto out;
		}
		buf[readCount] = '\0';
		for(unsigned i = 0; i < sizeof(PROCESS_NAME)/4; i++){
			if(strstr(buf,PROCESS_NAME[i]) != NULL){
				currentProcessName = PROCESS_NAME[i];
				MY_LOGD("find process %s in suitable apps list",currentProcessName);
				ret = true;
				goto out;
			}
		}
	}else{
	    MY_LOGD("open %s failed, errno:%d \n",procPath,errno);
	}
out:
	if(pFile != NULL){
		fclose(pFile);
	}
	return ret;
}

bool SkCanvasComputeThreadWrapper::addTask(SkTask_Base *pTask){
    if(true == pTaskQueue->enqueue(pTask)){
        startThread();
    }else{
        return false;
    }
    return true;
}
SkTask_Base *SkCanvasComputeThreadWrapper::threadGetTask(){
    return pTaskQueue->dequeue();
}
void SkCanvasComputeThreadWrapper::startThread(){
    pthread_mutex_lock(&mutex);
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);
}
void SkCanvasComputeThreadWrapper::threadWaitTask(){
    pthread_mutex_lock(&mutex);
    pthread_cond_wait(&cond,&mutex);
    pthread_mutex_unlock(&mutex);
}

#ifdef AW_TEST_USE_DOUBLE_COMPUTE_THREAD
bool SkCanvasComputeThreadWrapper::addTask2(SkTask_Base *pTask){
    if(true == pTaskQueue2->enqueue(pTask)){
        startThread2();
    }else{
        return false;
    }
    return true;
}
SkTask_Base *SkCanvasComputeThreadWrapper::thread2GetTask(){
    return pTaskQueue2->dequeue();
}
void SkCanvasComputeThreadWrapper::startThread2(){
    pthread_mutex_lock(&mutex2);
    pthread_cond_signal(&cond2);
    pthread_mutex_unlock(&mutex2);
}
void SkCanvasComputeThreadWrapper::thread2WaitTask(){
    pthread_mutex_lock(&mutex2);
    pthread_cond_wait(&cond2,&mutex2);
    pthread_mutex_unlock(&mutex2);
}
#endif /*AW_TEST_USE_DOUBLE_COMPUTE_THREAD*/

#ifdef AW_MULTIPLE_THREAD_SUPPORT
SkCanvasComputeThreadWrapper gComptuteThread;
#endif /*AW_MULTIPLE_THREAD_SUPPORT*/