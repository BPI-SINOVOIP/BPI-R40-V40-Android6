#define LOG_TAG "CustomerStream"
#include <CdxLog.h>
#include <CdxStream.h>

CdxStreamT *CustomerStreamOpen(CdxDataSourceT *dataSource)
{
    void* handle;
    int ret;
    ret = sscanf(dataSource->uri, "customer://%p", &handle);
    if (ret != 1)
    {
        CDX_LOGE("sscanf failure...(%s)", dataSource->uri);
        return NULL;
    }
    return (CdxStreamT *)handle;
}

CdxStreamCreatorT customerStreamCtor =
{
    .create = CustomerStreamOpen
};
