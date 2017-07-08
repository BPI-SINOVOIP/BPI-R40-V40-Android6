/*
 * WVMDataSource.h
 *
 *  Created on: 2015-1-8
 *      Author: wei
 */

#ifndef WVMDATASOURCE_H_
#define WVMDATASOURCE_H_
#include <stdio.h>

#include <media/stagefright/DataSource.h>
#include <media/stagefright/MediaErrors.h>
#include <utils/threads.h>
#include <drm/DrmManagerClient.h>

#include <CdxTypes.h>
#include <CdxParser.h>
#include <CdxStream.h>
namespace android {

class WVMDataSource : public DataSource {
public:
	WVMDataSource(CdxStreamT *stream);

    virtual status_t initCheck() const;

    virtual ssize_t readAt(off64_t offset, void *data, size_t size);

    virtual status_t getSize(off64_t *size);

    virtual sp<DecryptHandle> DrmInitialization(const char *mime);

    virtual void getDrmInfo(sp<DecryptHandle> &handle, DrmManagerClient **client);

    virtual String8 getUri();

    virtual String8 getMIMEType() const;

    virtual status_t reconnectAtOffset(off64_t offset);

protected:
    virtual ~WVMDataSource();

private:
    Mutex mLock;

    CdxStreamT *mStream;
    AString mURI;
    /*for DRM*/
    sp<DecryptHandle> mDecryptHandle;
    DrmManagerClient *mDrmManagerClient;

    WVMDataSource(const WVMDataSource &);
    WVMDataSource &operator=(const WVMDataSource &);
};

}

#endif /* WVMDATASOURCE_H_ */
