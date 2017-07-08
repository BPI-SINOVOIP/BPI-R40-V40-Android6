#include "adapter.h"
#include "log.h"
#include <string.h>

static int nMaxValidMemoryThresh = 0;
static int nCurrentMemoryThresh = 0;

//* ve control methods.
int AdapterInitialize(int bIsSecureVideoFlag)
{
    if(VeInitialize() < 0)
        return -1;
	//If mediaserver died when playing protected media,
    //some hardware is in protected mode, shutdown.
    SecureMemAdapterOpen();
    SecureMemAdapterClose();
    if(MemAdapterOpen() < 0)
        return -1;

    if(bIsSecureVideoFlag == 1)
    	SecureMemAdapterOpen();
    
    nCurrentMemoryThresh = 0;
    return 0;    
}


void AdpaterRelease(int bIsSecureVideoFlag)
{
    VeRelease();
    MemAdapterClose();

    if(bIsSecureVideoFlag == 1)
        SecureMemAdapterClose();
    return;
}


int AdapterLockVideoEngine(int flag)
{
    if(flag==1)
    	return VeJpegDeLock();
    else
    	return VeLock();
}


void AdapterUnLockVideoEngine(int flag)
{
	if(flag==1)
		return VeJpegDeUnLock();
	else
		return VeUnLock();
}


void AdapterVeReset(void)
{
    VeReset();
}


int AdapterVeWaitInterrupt(void)
{
    return VeWaitInterrupt();
}


void* AdapterVeGetBaseAddress(void)
{
    return VeGetRegisterBaseAddress();
}


int AdapterMemGetDramType(void)
{
    return VeGetDramType();
}


//* memory methods.

void* AdapterMemPalloc(int nSize)
{
	void* pPallocPtr = NULL;

	if((nMaxValidMemoryThresh!=0) && ((nCurrentMemoryThresh+nSize)>=nMaxValidMemoryThresh))
	{
		return NULL;
	}
	pPallocPtr = MemAdapterPalloc(nSize);
	if(pPallocPtr != NULL)
	{
		nCurrentMemoryThresh += nSize;
		logv("palloc nPallocSize=%d, current memory size is %d, nMaxValidMemoryThresh=%d\n", nSize, nCurrentMemoryThresh, nMaxValidMemoryThresh);
	}
	return pPallocPtr;
}


void AdapterMemPfree(void* pMem)
{
	int nFreeSize = 0;

    nFreeSize = MemAdapterPfree(pMem);
    if(nFreeSize != 0)
    {
    	nCurrentMemoryThresh -= nFreeSize;
    }
    logv("free nFreeSize=%d, current memory size is %d\n", nFreeSize, nCurrentMemoryThresh);
}


void AdapterMemFlushCache(void* pMem, int nSize)
{
    MemAdapterFlushCache(pMem, nSize);
}


void* AdapterMemGetPhysicAddress(void* pVirtualAddress)
{
    return MemAdapterGetPhysicAddress(pVirtualAddress);
}


void* AdapterMemGetVirtualAddress(void* pPhysicAddress)
{
    return MemAdapterGetVirtualAddress(pPhysicAddress);
}

void AdapterMemThresh(int nMemoryThreshold)
{
	nMaxValidMemoryThresh = nMemoryThreshold;
	logv("nMaxValidMemoryThresh=%d\n", nMaxValidMemoryThresh);
    return;
}

void AdapterMemSet(void* pMem, int nValue, int nSize)
{
    memset(pMem, nValue, nSize);
}


void AdapterMemCopy(void* pMemDst, void* pMemSrc, int nSize)
{
    memcpy(pMemDst, pMemSrc, nSize);
}

int AdapterMemRead(void* pMemSrc, void* pMemDst, int nSize)
{
    memcpy(pMemDst, pMemSrc, nSize);
    return 0;
}

int AdapterMemWrite(void* pMemSrc, void* pMemDst, int nSize)
{
    memcpy(pMemDst, pMemSrc, nSize);
    return 0;
}



void* AdapterSecureMemAdapterAlloc(int size)
{
	return SecureMemAdapterAlloc(size);
}


void AdapterSecureMemAdapterFree(void* ptr)
{
	SecureMemAdapterFree(ptr);
}

void AdapterSecureMemAdapterCopy(void *dest, void *src, int n)
{
	SecureMemAdapterCopy(dest, src, n);
}
void AdapterSecureMemAdapterFlushCache(void *ptr, int size)
{
	SecureMemAdapterFlushCache(ptr, size);
}

int AdapterSecureMemAdapterRead(void *src, void *dest, int n)
{
	return SecureMemAdapterRead(src, dest, n);
}

int AdapterSecureMemAdapterWrite(void *src, void *dest, int n)
{
	return SecureMemAdapterWrite(src, dest, n);
}

void AdapterSecureMemAdapterSet(void *s, int c, int n)
{
	SecureMemAdapterSet(s, c,  n);
}

void * AdapterSecureMemAdapterGetPhysicAddress(void *virt)
{
	return SecureMemAdapterGetPhysicAddress(virt);
}

void * AdapterSecureMemAdapterGetVirtualAddress(void *phy)
{
	return SecureMemAdapterGetVirtualAddress(phy);
}

void* AdapterSecureMemAdapterGetPhysicAddressCpu(void *virt)
{
	return AdapterSecureMemAdapterGetPhysicAddressCpu(virt);
}

void* AdapterSecureMemAdapterGetVirtualAddressCpu(void *phy)
{
	return SecureMemAdapterGetVirtualAddressCpu(phy);
}

int AdapterSetupSecureHardware()
{
	return SecureAdapterSetupHW();
}

int AdapterShutdownSecureHardware()
{
	return SecureAdapterShutdownHW();
}

