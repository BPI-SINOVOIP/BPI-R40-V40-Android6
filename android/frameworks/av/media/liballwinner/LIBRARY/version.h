#ifndef CDX_VERSION_H
#define CDX_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

#define REPO_TAG "pad-a83/a64-android6.0-2.5.0"
#define REPO_BRANCH "master"
#define REPO_COMMIT "e0c7598a079c86a6bae3bed1421183f221163b98"
#define REPO_DATE "Mon Jan 25 11:01:01 2016 +0800"
#define RELEASE_AUTHOR "guoshaomin"

static inline void LogVersionInfo(void)
{
    logd("\n"
         ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> CedarX <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n" 
         "tag   : %s\n"
         "branch: %s\n"
         "commit: %s\n"
         "date  : %s\n"
         "author: %s\n"
         "----------------------------------------------------------------------\n",
         REPO_TAG, REPO_BRANCH, REPO_COMMIT, REPO_DATE, RELEASE_AUTHOR);
}

/* usage: TagVersionInfo(myLibTag) */
#define TagVersionInfo(tag) \
    static void VersionInfo_##tag(void) __attribute__((constructor));\
    void VersionInfo_##tag(void) \
    { \
        logd("-------library tag: %s-------", #tag);\
        LogVersionInfo(); \
    }


#ifdef __cplusplus
}
#endif

#endif


