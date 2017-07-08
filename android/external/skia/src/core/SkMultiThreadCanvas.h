#ifndef _SK_MULTI_THREAD_CANVAS_
#define _SK_MULTI_THREAD_CANVAS_

#include <pthread.h>
#include <semaphore.h>

#include <string.h>
#include <unistd.h>
#include <errno.h>

//#define AW_TEST_USE_DOUBLE_COMPUTE_THREAD

#ifndef  MY_LOGD
#include <cutils/log.h>
#define MY_LOGD(fmt, args...) //\
	//__android_log_print(ANDROID_LOG_ERROR, "_SKIA", "%s:%d " fmt,__FILE__,__LINE__,args)
#endif  /*MY_LOGD*/

class SkTask_Base{
public:
    SkTask_Base();
    virtual ~SkTask_Base();

    virtual void go() = 0;
    bool wait();
    bool signal();
private:
    sem_t sem;
};

typedef struct _SkTaskItem{
    SkTask_Base *pTask;
    struct _SkTaskItem  *next;
}SkTaskItem;
class SkCanvasComputeThreadWrapper{
public:
    SkCanvasComputeThreadWrapper();

	/*
	Note: Destruction function will never be called for its global objects, Eg: gComptuteThread,
	      duing to Android's special runtime.
	      When Application quit, or be killed by system, process's resource (including threads and
	      threads's resource) will be re-collected by system directly, not need to call each resource's
	      destruction functions.
	*/
    ~SkCanvasComputeThreadWrapper();

    /*
     @when use this pthread framework, first call must be call this one.
     @return :return false which means we can not use this pthread framework,
              because thread has not been created.
	*/
	bool canUseThread();
	/*
	 @when canUseThread return true, which means the compute thread has been inited properly,
	  then app main thread can use this interface to push a task to compute thread.
	  if app want to sync with compute thread, it must call pTask->wait();
	*/
    bool addTask(SkTask_Base *pTask);

    /*
      @interface used by compute thread.
      @@when compute thread is active, it will use this interface to get a unclosed task,
        then complete it. After complete this task, compute thread will call threadWaitTask() to
        step in sleep status for free cpu.
	*/
    SkTask_Base* threadGetTask();
    void         threadWaitTask();


#ifdef AW_TEST_USE_DOUBLE_COMPUTE_THREAD
    bool         addTask2(SkTask_Base *pTask);
    SkTask_Base *thread2GetTask();
    void         thread2WaitTask();
protected:
    void startThread2();
#endif /*AW_TEST_USE_DOUBLE_COMPUTE_THREAD*/

protected:
	bool real_init();
    void startThread();
private:
    class SkTasksQueue{
        public:
            SkTasksQueue(){
                pHead = pTail = NULL;
                pthread_mutex_init(&lock, NULL);
            }
            ~SkTasksQueue(){
                pthread_mutex_destroy(&lock);
                SkTaskItem *pNext;
                while(pHead != NULL){
                    pNext = pHead->next;
                    delete  pHead;
                    pHead = pNext;
                }
                pTail = NULL;
            }
            bool enqueue(SkTask_Base *pTask){
                SkTaskItem* pNewItem = new SkTaskItem;
                pNewItem->pTask = pTask;
                pNewItem->next = NULL;

                pthread_mutex_lock(&lock);
                if(pHead == NULL){//insert first item
                   pHead = pNewItem;
                   pTail = pNewItem;
                }else{
                    pTail->next = pNewItem;
                    pTail = pNewItem;
                }
                pthread_mutex_unlock(&lock);

                return true;
            }
            SkTask_Base *dequeue(){
                SkTask_Base *pTask = NULL;
                SkTaskItem  *pNext = NULL;

                pthread_mutex_lock(&lock);
                if(pHead != NULL){
                    pTask = pHead->pTask;
                    pNext = pHead->next;
                    delete pHead;
                    pHead = pNext;
                }
                if(pHead == NULL){//delete last item
                    pTail = NULL;
                }
                pthread_mutex_unlock(&lock);

                return pTask;
            }
        private:
            SkTaskItem     *pHead;
            SkTaskItem     *pTail;
            pthread_mutex_t lock;
    };

private:
    pthread_t threadID;
    SkTasksQueue *pTaskQueue;

    //mutex/signal used to start thread.
    pthread_cond_t  cond;
    pthread_mutex_t mutex;

	//process name.
    static const char* PROCESS_NAME[2];
	const char *currentProcessName;
	bool bCanUseThread;

	bool processCanEnableThread();

	int nFirstRef;

#ifdef AW_TEST_USE_DOUBLE_COMPUTE_THREAD
    pthread_t threadID2;
    SkTasksQueue *pTaskQueue2;
	pthread_cond_t  cond2;
	pthread_mutex_t mutex2;
#endif /*AW_TEST_USE_DOUBLE_COMPUTE_THREAD*/
};

extern bool bComputeThreadCreated;
extern bool bExitComputeThread;

#ifdef AW_MULTIPLE_THREAD_SUPPORT
extern SkCanvasComputeThreadWrapper gComptuteThread;
#endif /**/
#endif /*_SK_MULTI_THREAD_CANVAS_*/