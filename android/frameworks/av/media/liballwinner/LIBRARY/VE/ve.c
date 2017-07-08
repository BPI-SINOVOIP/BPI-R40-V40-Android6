#include "log.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <sys/mman.h>
#include <pthread.h>

#include "ve.h"
#include "vdecoder.h"

#define V3_CHIP_ID      (0)
#define V3S_CHIP_ID_A   (3)
#define V3S_CHIP_ID_B   (7)


#if(CONFIG_CHIP == OPTION_CHIP_1623 || \
    CONFIG_CHIP == OPTION_CHIP_1625 || \
    CONFIG_CHIP==  OPTION_CHIP_1651)
    #include "include/driver/cedardev_api.h"
#else
    #include "include/driver2/cedardev_api.h"
#endif

//for ipc
#if CONFIG_VE_IPC == OPTION_VE_IPC_ENABLE
#if CONFIG_OS == OPTION_OS_ANDROID
#include <linux/sem.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <sys/ipc.h>
#elif CONFIG_OS == OPTION_OS_LINUX
#include <linux/sem.h>
#endif
#define 		VE_IPC
#endif

#ifdef	VE_IPC
#define 		SEM_NUM 2
static int 		sem_id = 0;
static key_t 	id = 0;
static int		process_ref_count = 0;

static int aw_set_semvalue();
static void aw_del_semvalue();
static int semaphore_p(unsigned short sem_index);
static int semaphore_v(unsigned short sem_index);
#endif


static pthread_mutex_t  gVeMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t  gVeRegisterMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t  gVeDecoderMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t  gVeEncoderMutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t  gVeJpegDecoderMutex = PTHREAD_MUTEX_INITIALIZER;


static int              gVeRefCount = 0;
int                     gVeDriverFd = -1;
cedarv_env_info_t       gVeEnvironmentInfo;

#define VE_MODE_SELECT 0x00
#define VE_RESET	   0x04
#define JPG_VE_RESET   0x2c

static int              gNomalEncRefCount = 0;
static int              gPerfEncRefCount = 0;

#define REG_VALUE(addr) RegisterOperation(0, addr, 0)
#define REG_READ(addr)  RegisterOperation(0, addr, 0)
#define REG_WRITE(addr, value) RegisterOperation(1, addr, value)


int RegisterOperation(int rw, int addr, int value)
{
	struct cedarv_regop cedarv_reg;

	cedarv_reg.addr = addr;

	if(rw == 0) { //read
		return ioctl(gVeDriverFd, IOCTL_READ_REG, (unsigned long)&cedarv_reg);
	}
	else { //write
		cedarv_reg.value = value;
		ioctl(gVeDriverFd, IOCTL_WRITE_REG, (unsigned long)&cedarv_reg);
	}

	return 0;
}

int VeChipId(void)
{
    pthread_mutex_lock(&gVeMutex);
	int i = 0;
	int ret = 0;
	int dev = 0;
	char buf[16]={0};

	dev =open("/dev/sunxi_soc_info", O_RDONLY);
	if (dev < 0) 
	{
		logv("cannot open /dev/sunxi_soc_info\n");
		pthread_mutex_unlock(&gVeMutex); 
		return  CHIP_UNKNOWN;
	}

	ret = ioctl(dev, 3, buf);
	if(ret < 0)
	{
		loge("ioctl err!\n");
		pthread_mutex_unlock(&gVeMutex); 
    	return  CHIP_UNKNOWN;
	}

	logd("%s\n", buf);

	close(dev);

	if(!strncmp(buf, "00000000", 8) || 
	   !strncmp(buf, "00000081", 8))
	{
		ret = CHIP_H3;
	}
	else if(!strncmp(buf, "00000042", 8) || 
			!strncmp(buf, "00000083", 8))
	{
		ret = CHIP_H2PLUS;
	}
	else if(!strcmp(buf, "H2"))	// deprecated
	{
		ret = CHIP_H2;
	}
	else if(!strcmp(buf, "H3s"))// deprecated
	{
		ret = CHIP_H3s;
	}
	else
	{
		ret = CHIP_UNKNOWN;
	}
	pthread_mutex_unlock(&gVeMutex);
	return ret;
}


int VeInitialize(void)
{
	unsigned short sem_index;
	int sem_result = 0;
	
    pthread_mutex_lock(&gVeMutex);
    
    if(gVeRefCount == 0)
    {
	 #ifdef VE_IPC
		id = ftok("/data/device.info", 'a');

		// create a sem
		sem_result = syscall(__NR_semget, id, SEM_NUM, 0666 | IPC_CREAT | IPC_EXCL);
		if(sem_result == -1)
		{
			sem_id = syscall(__NR_semget, id, SEM_NUM, 0666 | IPC_CREAT);
			if(sem_id == -1)
			{
				fprintf(stderr, "cedar-ve semphore has already been created ,but the process %d get sem_id error\n", getpid());
				exit(EXIT_FAILURE);
			}
			logd("get cedar-ve semphore ok: the process that call the cedar-ve is %d, sem id is %d\n", getpid(),sem_id);
		}
		else
		{
			// init sem
			if(!aw_set_semvalue())
			{
				fprintf(stderr, "Failed to initialize semaphore\n");
				exit(EXIT_FAILURE);
			}
			logd("init cedar-ve semphore ok: the process that first call the cedar-ve is %d, sem id is %d\n", getpid(),sem_id);
		}

		sem_index = 0;
		if(!semaphore_p(sem_index))
			exit(EXIT_FAILURE);
		sem_index = 1;
		if(!semaphore_p(sem_index))
			exit(EXIT_FAILURE);
	#endif
	
        //* open Ve driver.
        gVeDriverFd = open("/dev/cedar_dev", O_RDWR);
        if(gVeDriverFd < 0)
        {
            loge("open /dev/cedar_dev fail.");
            pthread_mutex_unlock(&gVeMutex);
            return -1;
        }
        
        //* set ve reference count to zero.
        //* we must reset it to zero to fix refcount error when process crash.
      #ifndef VE_IPC
        if(sem_result != -1)
       		ioctl(gVeDriverFd, IOCTL_SET_REFCOUNT, 0);
	  #endif
        //* request ve.
        ioctl(gVeDriverFd, IOCTL_ENGINE_REQ, 0);
        
#if (CONFIG_CHIP == OPTION_CHIP_1639)
		ioctl(gVeDriverFd, IOCTL_SET_VE_FREQ, 600);
#elif (CONFIG_CHIP == OPTION_CHIP_1689 && CONFIG_PRODUCT == OPTION_PRODUCT_PAD)	
		ioctl(gVeDriverFd, IOCTL_SET_VE_FREQ, 360);	
#else
	    //*the ve speed is set by the driver to default value
#endif

        //* map registers.
        ioctl(gVeDriverFd, IOCTL_GET_ENV_INFO, (unsigned long)&gVeEnvironmentInfo);
		gVeEnvironmentInfo.address_macc = (unsigned long)mmap(NULL, 
                                                             2048, 
                                                             PROT_READ | PROT_WRITE, MAP_SHARED,
                                                             gVeDriverFd, 
                                                             (unsigned long)gVeEnvironmentInfo.address_macc);

		//* reset ve.
	#ifdef VE_IPC
		process_ref_count = ioctl(gVeDriverFd, IOCTL_GET_REFCOUNT, 0);
		if(process_ref_count <= 1)
        	VeReset();

		sem_index = 0;
		if(!semaphore_v(sem_index))
			exit(EXIT_FAILURE);
		sem_index = 1;
		if(!semaphore_v(sem_index))
			exit(EXIT_FAILURE);
	#else
		VeReset();
	#endif
    }
	
    gVeRefCount++;
	
    pthread_mutex_unlock(&gVeMutex);
    
    return 0;    
}


void VeRelease(void)
{
	unsigned short sem_index;
	long ve_ref_count;
	
    pthread_mutex_lock(&gVeMutex);
    
    if(gVeRefCount <= 0)
    {
        loge("invalid status, gVeRefCount=%d at AdpaterRelease", gVeRefCount);
        pthread_mutex_unlock(&gVeMutex);
        return;
    }
    
    gVeRefCount--;
    
    if(gVeRefCount == 0)
    {
    #ifdef VE_IPC
    	sem_index = 0;
		if(!semaphore_p(sem_index))
			exit(EXIT_FAILURE);
		sem_index = 1;
		if(!semaphore_p(sem_index))
			exit(EXIT_FAILURE);

		process_ref_count = ioctl(gVeDriverFd, IOCTL_GET_REFCOUNT, 0);
	#endif
		{
	        if(gVeDriverFd != -1)
	        {
	            ioctl(gVeDriverFd, IOCTL_ENGINE_REL, 0);
	            munmap((void *)gVeEnvironmentInfo.address_macc, 2048);
	            close(gVeDriverFd);
	            gVeDriverFd = -1;
	        }
		}
	#ifdef VE_IPC
		sem_index = 0;
		if(!semaphore_v(sem_index))
			exit(EXIT_FAILURE);
		sem_index = 1;
		if(!semaphore_v(sem_index))
			exit(EXIT_FAILURE);

	#endif
    }
    
    pthread_mutex_unlock(&gVeMutex);
    
    return;
}


int VeLock(void)
{
#if CONFIG_CHIP != OPTION_CHIP_1639
	#ifdef VE_IPC
		if(!semaphore_p(0))
			exit(EXIT_FAILURE);
	#endif
#else
	#ifdef VE_IPC
		if(!semaphore_p(1))
			exit(EXIT_FAILURE);
	#endif
#endif

    return pthread_mutex_lock(&gVeDecoderMutex);
}


void VeUnLock(void)
{
#if CONFIG_CHIP != OPTION_CHIP_1639
	#ifdef VE_IPC
			if(!semaphore_v(0))
				exit(EXIT_FAILURE);
	#endif
#else
	#ifdef VE_IPC
			if(!semaphore_v(1))
				exit(EXIT_FAILURE);
	#endif
#endif	

    pthread_mutex_unlock(&gVeDecoderMutex);
}


int VeEncoderLock(void)
{
#if CONFIG_CHIP != OPTION_CHIP_1639
	return VeLock();
#else
	#ifdef VE_IPC
		if(!semaphore_p(0))
			exit(EXIT_FAILURE);
	#endif
    return pthread_mutex_lock(&gVeEncoderMutex);
#endif
}


void VeEncoderUnLock(void)
{
#if CONFIG_CHIP != OPTION_CHIP_1639
	VeUnLock();
#else
#ifdef VE_IPC
		if(!semaphore_v(0))
			exit(EXIT_FAILURE);
#endif
    pthread_mutex_unlock(&gVeEncoderMutex);
#endif
}

void VeSetDramType()
{
	volatile vetop_reg_mode_sel_t* pVeModeSelect;
	pthread_mutex_lock(&gVeRegisterMutex);
	pVeModeSelect = (vetop_reg_mode_sel_t*)(gVeEnvironmentInfo.address_macc + VE_MODE_SELECT);
	switch (VeGetDramType())
	{
		case DDRTYPE_DDR1_16BITS:
			pVeModeSelect->ddr_mode = 0;
			break;
	
		case DDRTYPE_DDR1_32BITS:
		case DDRTYPE_DDR2_16BITS:
			pVeModeSelect->ddr_mode = 1;
			break;
	
		case DDRTYPE_DDR2_32BITS:
		case DDRTYPE_DDR3_16BITS:
			pVeModeSelect->ddr_mode = 2;
			break;
	
		case DDRTYPE_DDR3_32BITS:
		case DDRTYPE_DDR3_64BITS:
			pVeModeSelect->ddr_mode = 3;
			pVeModeSelect->rec_wr_mode = 1;
			break;
	
		default:
			break;
	}
	pthread_mutex_unlock(&gVeRegisterMutex);
}

void VeReset(void)
{
    ioctl(gVeDriverFd, IOCTL_RESET_VE, 0);

#if (CONFIG_CHIP != OPTION_CHIP_1663)
	VeSetDramType();
#endif
}

int VeWaitInterrupt(void)
{
    int ret;
	
    ret = ioctl(gVeDriverFd, IOCTL_WAIT_VE_DE, 1);
    if(ret <= 0)
    {
        logw("wait ve interrupt timeout.");
        return -1;  //* wait ve interrupt fail.
    }
    else
        return 0;
}

int VeWaitEncoderInterrupt(void)
{
    int ret;
	
    ret = ioctl(gVeDriverFd, IOCTL_WAIT_VE_EN, 1);
    if(ret <= 0)
        return -1;  //* wait ve interrupt fail.
    else
        return 0;
}


void* VeGetRegisterBaseAddress(void)
{
    return (void*)gVeEnvironmentInfo.address_macc;
}


unsigned int VeGetIcVersion()
{
    if(gVeRefCount >0)
    {
		volatile unsigned int value;
   		value = *((unsigned int*)((char *)gVeEnvironmentInfo.address_macc + 0xf0));
		return (value>>16);
    }
	else
	{
		loge("must call VeGetIcVersion(), affer VeInitialize");
		return 0;
	}
}

int VeGetDramType(void)
{
    //* can we know memory type by some system api?
#if CONFIG_DRAM_INTERFACE == OPTION_DRAM_INTERFACE_DDR1_16BITS
    return DDRTYPE_DDR1_16BITS;
#elif CONFIG_DRAM_INTERFACE == OPTION_DRAM_INTERFACE_DDR1_32BITS
    return DDRTYPE_DDR1_32BITS;
#elif CONFIG_DRAM_INTERFACE == OPTION_DRAM_INTERFACE_DDR2_16BITS
    return DDRTYPE_DDR2_16BITS;
#elif CONFIG_DRAM_INTERFACE == OPTION_DRAM_INTERFACE_DDR2_32BITS
    return DDRTYPE_DDR2_32BITS;
#elif CONFIG_DRAM_INTERFACE == OPTION_DRAM_INTERFACE_DDR3_16BITS
    return DDRTYPE_DDR3_16BITS;
#elif CONFIG_DRAM_INTERFACE == OPTION_DRAM_INTERFACE_DDR3_32BITS
    return DDRTYPE_DDR3_32BITS;
#elif CONFIG_DRAM_INTERFACE == OPTION_DRAM_INTERFACE_DDR3_64BITS
    return DDRTYPE_DDR3_64BITS;
#else
    #error "invalid ddr type configuration."
#endif
}


int VeSetSpeed(int nSpeedMHz)
{
    return ioctl(gVeDriverFd, IOCTL_SET_VE_FREQ, nSpeedMHz);
}


void VeEnableEncoder()
{
	volatile vetop_reg_mode_sel_t* pVeModeSelect;
	pthread_mutex_lock(&gVeRegisterMutex);
	pVeModeSelect = (vetop_reg_mode_sel_t*)(gVeEnvironmentInfo.address_macc + VE_MODE_SELECT);

#if (CONFIG_CHIP==OPTION_CHIP_1623 || CONFIG_CHIP==OPTION_CHIP_1625 || CONFIG_CHIP==OPTION_CHIP_1651 || CONFIG_CHIP==OPTION_CHIP_1663)
	pVeModeSelect->mode = 11;
#else
	pVeModeSelect->enc_enable = 1;
	pVeModeSelect->enc_isp_enable = 1;
#endif
	pthread_mutex_unlock(&gVeRegisterMutex);
}

void VeDisableEncoder()
{
	volatile vetop_reg_mode_sel_t* pVeModeSelect;
	pthread_mutex_lock(&gVeRegisterMutex);
	pVeModeSelect = (vetop_reg_mode_sel_t*)(gVeEnvironmentInfo.address_macc + VE_MODE_SELECT);

#if (CONFIG_CHIP==OPTION_CHIP_1623 || CONFIG_CHIP==OPTION_CHIP_1625 || CONFIG_CHIP==OPTION_CHIP_1651 || CONFIG_CHIP==OPTION_CHIP_1663)
	pVeModeSelect->mode = 0x7;
#else
	pVeModeSelect->enc_enable = 0;
	pVeModeSelect->enc_isp_enable = 0;
#endif
	pthread_mutex_unlock(&gVeRegisterMutex);	
}

void VeEnableDecoder(int nDecoderMode)
{
	volatile vetop_reg_mode_sel_t* pVeModeSelect;
	pthread_mutex_lock(&gVeRegisterMutex);
	pVeModeSelect = (vetop_reg_mode_sel_t*)(gVeEnvironmentInfo.address_macc + VE_MODE_SELECT);
	
	switch(nDecoderMode)
	{
		case VIDEO_CODEC_FORMAT_H264:
			pVeModeSelect->mode = 1;
			break;
		case VIDEO_CODEC_FORMAT_VP8:
			pVeModeSelect->mode = 1;
			break;
		case VIDEO_CODEC_FORMAT_AVS:
			pVeModeSelect->mode = 1;
			break;			
		case VIDEO_CODEC_FORMAT_WMV3:
			pVeModeSelect->mode = 2;
			break;
		case VIDEO_CODEC_FORMAT_RX:
			pVeModeSelect->mode = 3;
			break;
		case VIDEO_CODEC_FORMAT_H265:
			pVeModeSelect->mode = 4;
			break;
		case VIDEO_CODEC_FORMAT_MJPEG:
#if (CONFIG_CHIP == OPTION_CHIP_1681 || CONFIG_CHIP == OPTION_CHIP_1689)
			pVeModeSelect->jpg_dec_en = 1;
#else
			pVeModeSelect->mode = 0;
#endif
			break;
		default:
			pVeModeSelect->mode = 0; //* MPEG1/2/4 or JPEG decoder.
			break;
	}
	pthread_mutex_unlock(&gVeRegisterMutex);		
}

void VeDisableDecoder()
{
	volatile vetop_reg_mode_sel_t* pVeModeSelect;
	pthread_mutex_lock(&gVeRegisterMutex);
	pVeModeSelect = (vetop_reg_mode_sel_t*)(gVeEnvironmentInfo.address_macc + VE_MODE_SELECT);
	pVeModeSelect->mode = 7;
	pthread_mutex_unlock(&gVeRegisterMutex);	
}

void VeDecoderWidthMode(int nWidth)
{
	volatile vetop_reg_mode_sel_t* pVeModeSelect;
	pthread_mutex_lock(&gVeRegisterMutex);
	pVeModeSelect = (vetop_reg_mode_sel_t*)(gVeEnvironmentInfo.address_macc + VE_MODE_SELECT);
	
	if(nWidth >= 4096)
	{
		pVeModeSelect->pic_width_more_2048 = 1;
		pVeModeSelect->pic_width_is_4096 = 1;
	}
	else if(nWidth >= 2048)
	{
		pVeModeSelect->pic_width_more_2048 = 1;
		pVeModeSelect->pic_width_is_4096 = 0;
	}
	else
	{
		pVeModeSelect->pic_width_more_2048 = 0;
		pVeModeSelect->pic_width_is_4096 = 0;
	}
	pthread_mutex_unlock(&gVeRegisterMutex);
}

void VeResetDecoder()
{
#if (CONFIG_CHIP == OPTION_CHIP_1639 || CONFIG_CHIP == OPTION_CHIP_1681 || CONFIG_CHIP == OPTION_CHIP_1689)
	volatile vetop_reg_reset_t* pVeReset;
	pthread_mutex_lock(&gVeRegisterMutex);
	pVeReset = (vetop_reg_reset_t*)(gVeEnvironmentInfo.address_macc + VE_RESET);
	pVeReset->decoder_reset = 1;
	pVeReset->decoder_reset = 0;
	pthread_mutex_unlock(&gVeRegisterMutex);
#else
	VeReset();	
#endif
}


void VeResetEncoder()
{
#if (CONFIG_CHIP == OPTION_CHIP_1639 || CONFIG_CHIP == OPTION_CHIP_1681 || CONFIG_CHIP == OPTION_CHIP_1689)
	volatile vetop_reg_reset_t* pVeReset;
	pthread_mutex_lock(&gVeRegisterMutex);
	pVeReset = (vetop_reg_reset_t*)(gVeEnvironmentInfo.address_macc + VE_RESET);
	pVeReset->encoder_reset = 1;
	pVeReset->encoder_reset = 0;
	pthread_mutex_unlock(&gVeRegisterMutex);
#else
	VeReset();	
#endif
}

void VeInitEncoderPerformance(int nMode) //* 0: normal performance; 1. high performance 
{
#if CONFIG_CHIP == OPTION_CHIP_1639
	VeLock();
	VeEncoderLock();
	if(nMode == 0) 
	{
		if(gNomalEncRefCount == 0 && gPerfEncRefCount == 0)
		{
			ioctl(gVeDriverFd, IOCTL_SET_VE_FREQ, 450); //MHZ
		}
		gNomalEncRefCount++;
	}
	else
	{
		if(gPerfEncRefCount == 0)
		{
			ioctl(gVeDriverFd, IOCTL_SET_VOL, 960); // mv
			ioctl(gVeDriverFd, IOCTL_SET_VE_FREQ, 480); //MHZ
		}
		gPerfEncRefCount++;
	}
	VeEncoderUnLock();
	VeUnLock();
#else
	CEDARX_UNUSE(nMode);
#endif
}

void VeUninitEncoderPerformance(int nMode) //* 0: normal performance; 1. high performance 
{
#if CONFIG_CHIP == OPTION_CHIP_1639
	VeLock();
	VeEncoderLock();
	if(nMode == 0)
	{
		gNomalEncRefCount--;
		if(gNomalEncRefCount == 0)
		{
			if(gPerfEncRefCount == 0)
			{
				ioctl(gVeDriverFd, IOCTL_SET_VE_FREQ, 600); //MHZ
			}
			else
			{
                //* do nothing 
			}
		}
	}
	else
	{
		gPerfEncRefCount--;
		if(gPerfEncRefCount == 0)
		{
			ioctl(gVeDriverFd, IOCTL_SET_VOL, 900);
			if(gNomalEncRefCount == 0)
			{
				ioctl(gVeDriverFd, IOCTL_SET_VE_FREQ, 600); //MHZ
			}
			else
			{
				ioctl(gVeDriverFd, IOCTL_SET_VE_FREQ, 450); //MHZ
			}
		}
	}
	VeEncoderUnLock();
	VeUnLock();
#else
	CEDARX_UNUSE(nMode);
#endif
}

int VeWaitJpegDecodeInterrupt(void)
{
    int ret;
    
    ret = ioctl(gVeDriverFd, IOCTL_WAIT_JPEG_DEC, 1);
    if(ret <= 0)
    {
        logw("wait jepg decoder interrupt timeout.");
        return -1;  //* wait jepg decoder interrupt fail.
    }
    else
        return 0;
}

void VeEnableJpegDecoder()
{
	volatile vetop_reg_mode_sel_t* pVeModeSelect;
	pthread_mutex_lock(&gVeRegisterMutex);
	pVeModeSelect = (vetop_reg_mode_sel_t*)(gVeEnvironmentInfo.address_macc + VE_MODE_SELECT);

	//pVeModeSelect->ddr_mode = 2; //MEMTYPE_DDR3_16BITS
	pVeModeSelect->jpg_dec_en = 1;
	
	pthread_mutex_unlock(&gVeRegisterMutex);
}


void VeDisableJpegDecoder()
{
	volatile vetop_reg_mode_sel_t* pVeModeSelect;
	pthread_mutex_lock(&gVeRegisterMutex);
	pVeModeSelect = (vetop_reg_mode_sel_t*)(gVeEnvironmentInfo.address_macc + VE_MODE_SELECT);
	pVeModeSelect->jpg_dec_en = 0;
	pthread_mutex_unlock(&gVeRegisterMutex);	
}

void VeResetJpegDecoder()
{
	volatile vetop_reg_jpg_reset_t *ve_reset;
	
	ve_reset = (vetop_reg_jpg_reset_t *)(gVeEnvironmentInfo.address_macc + JPG_VE_RESET);
	ve_reset->jpg_dec_reset = 1;
	ve_reset->jpg_dec_reset = 0;
}

int VeJpegDeLock(void)
{
#ifdef VE_IPC
	if(!semaphore_p(1))
		exit(EXIT_FAILURE);
#endif

    return pthread_mutex_lock(&gVeJpegDecoderMutex);
}


void VeJpegDeUnLock(void)
{
#ifdef VE_IPC
	if(!semaphore_v(1))
		exit(EXIT_FAILURE);
#endif

    pthread_mutex_unlock(&gVeJpegDecoderMutex);
}

#ifdef	VE_IPC
static int aw_set_semvalue()
{
	union semun sem_union;
	unsigned short sem_init_value[2] = {1,1};

	sem_union.array = sem_init_value;
	if(syscall(__NR_semctl, sem_id, 0, SETALL, sem_union)== -1)
	//if(semctl(sem_id, 0, SETALL, sem_union) == -1)
		return 0;
	return 1;
}

static void aw_del_semvalue()
{
	union semun sem_union;

	if(syscall(__NR_semctl, sem_id, 0, IPC_RMID, sem_union) == -1)
		fprintf(stderr, "Failed to delete semaphore\n");
}

static int semaphore_p(unsigned short sem_index)
{
	struct sembuf sem_b;
	sem_b.sem_num = sem_index;
	sem_b.sem_op = -1;//P()
	sem_b.sem_flg = SEM_UNDO;
	if(syscall(__NR_semop, sem_id, &sem_b, 1) == -1)
	{
		fprintf(stderr, "semaphore_p failed\n");
		return 0;
	}
	return 1;
}

static int semaphore_v(unsigned short sem_index)
{
	struct sembuf sem_b;
	sem_b.sem_num = sem_index;
	sem_b.sem_op = 1;//V()
	sem_b.sem_flg = SEM_UNDO;
	if(syscall(__NR_semop, sem_id, &sem_b, 1) == -1)
	{
		fprintf(stderr, "semaphore_v failed\n");
		return 0;
	}
	return 1;
}
#endif

