/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* Original code copied from NDK Native-media sample code */

//#define LOG_NDEBUG 0
#define TAG "NativeMediaCrypto"
#include <log/log.h>

#include <android_media_MediaCrypto.h>
#include <assert.h>
#include <binder/MemoryDealer.h>
#include <jni.h>
#include <media/ICrypto.h>
#include <media/stagefright/foundation/AString.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <utils/StrongPointer.h>
#include <semaphore.h>


static const size_t kBufferSize = 1024;

using namespace android;

static jboolean testCrypto(sp<ICrypto> icrypto,
        const CryptoPlugin::SubSample *subSample, CryptoPlugin::Mode mode)
{
    // Allocate source buffer
    sp<MemoryDealer> memDealer = new MemoryDealer(kBufferSize, "MediaCryptoTest");
    sp<IMemory> srcBuffer = memDealer->allocate(kBufferSize);
    if (!srcBuffer->pointer()) {
        ALOGE("Failed to allocate source buffer");
        return false;
    }
    memset(srcBuffer->pointer(), 's', kBufferSize);

    // Invalid dest pointer should fault if mediaserver attempts
    // to write to it.  Don't use NULL because that's probably
    // checked for.
    void *dstPtr = reinterpret_cast<void *>(1);

    // Spoof the device as being secure
    bool secure = true;

    uint8_t key[16] = {0};
    uint8_t iv[16] = {0};
    uint32_t offset = 0;
    AString errorDetailMsg;

    ssize_t result = icrypto->decrypt(secure, key, iv, mode, srcBuffer, offset,
            subSample, 1, dstPtr, &errorDetailMsg);

    // call should return an error and shouldn't kill media server
    return (result != OK && result != DEAD_OBJECT);
}

//
// This native code is currently disabled, as it depends on a non-NDK
// shared library, which may not be available on all systems.
// 
#if 0
//Test for icrypto interface vulnerabilities
extern "C" jboolean Java_android_security_cts_MediaCryptoTest_validateCryptoNative(JNIEnv *env,
        jclass /*clazz*/, jobject crypto)
{
    bool result = false;
    sp<ICrypto> icrypto = JCrypto::GetCrypto(env, crypto);
    if (icrypto != NULL) {
        if (icrypto->requiresSecureDecoderComponent("video/avc")) {
            ALOGI("device is secure, bypassing test");
            return true;
        }

        CryptoPlugin::Mode unencryptedMode = CryptoPlugin::kMode_Unencrypted;
        CryptoPlugin::Mode aesCtrMode = CryptoPlugin::kMode_AES_CTR;

        CryptoPlugin::SubSample clrSubSample = {kBufferSize, 0};
        CryptoPlugin::SubSample encSubSample = {0, kBufferSize};

        result =
            testCrypto(icrypto, &clrSubSample, unencryptedMode) &&
            testCrypto(icrypto, &clrSubSample, aesCtrMode) &&
            testCrypto(icrypto, &encSubSample, unencryptedMode) &&
            testCrypto(icrypto, &encSubSample, aesCtrMode);
    } else {
        ALOGE("Failed to get icrypto interface");
    }
    return result;
}
#endif


