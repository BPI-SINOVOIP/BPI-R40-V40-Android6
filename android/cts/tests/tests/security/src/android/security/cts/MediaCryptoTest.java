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

package android.security.cts;

import android.media.MediaCrypto;
import android.media.MediaCryptoException;
import android.media.MediaDrm;
import android.media.MediaDrmException;
import android.media.NotProvisionedException;
import android.media.ResourceBusyException;
import android.test.AndroidTestCase;
import android.util.Log;
import java.util.UUID;

public class MediaCryptoTest extends AndroidTestCase {
    private static final String TAG = "MediaCryptoTest";

    private static final UUID CLEARKEY_SCHEME_UUID =
        new UUID(0x1077efecc0b24d02L, 0xace33c1e52e2fb4bL);
    private static final UUID WIDEVINE_SCHEME_UUID =
        new UUID(0xedef8ba979d64aceL, 0xa3c827dcd51d21edL);

    static {
        System.loadLibrary("ctssecurity_jni");
    }

    private native boolean validateCryptoNative(MediaCrypto crypto);

    public void testMediaCryptoClearKey() throws Exception {
        MediaCrypto crypto = null;
        if (!MediaDrm.isCryptoSchemeSupported(CLEARKEY_SCHEME_UUID)) {
            Log.i(TAG, "No ClearKey plugin, skipping test");
            return;
        }
        try {
            byte[] initData = new byte[0];
            crypto = new MediaCrypto(CLEARKEY_SCHEME_UUID, initData);
        } catch (MediaCryptoException e) {
            throw new Error("Failed to create MediaCrypto using ClearKey plugin");
        }

        assertTrue("MediaCrypto validation failed", validateCryptoNative(crypto));
    }

    public void testMediaCryptoWidevine() throws Exception {
        if (!MediaDrm.isCryptoSchemeSupported(WIDEVINE_SCHEME_UUID)) {
            Log.i(TAG, "No Widevine plugin, skipping test");
            return;
        }

        MediaDrm drm = null;
        byte[] sessionId = null;

        try {
            drm = new MediaDrm(WIDEVINE_SCHEME_UUID);
            sessionId = openSession(drm);
            getWidevineKeys(drm, sessionId);
            MediaCrypto crypto = new MediaCrypto(WIDEVINE_SCHEME_UUID, sessionId);
            assertTrue("MediaCrypto validation failed", validateCryptoNative(crypto));
        } catch (MediaCryptoException | MediaDrmException e) {
            if (drm != null && sessionId != null) {
                drm.closeSession(sessionId);
            }
            throw e;
        }
    }

    private byte[] openSession(MediaDrm drm) throws Exception {
        byte[] sessionId = null;
        int retryCount = 3;
        while (retryCount-- > 0) {
            try {
                return drm.openSession();
            } catch (NotProvisionedException e) {
                Log.i(TAG, "Missing certificate, provisioning");
                ProvisionRequester provisionRequester = new ProvisionRequester();
                provisionRequester.doTransact(drm);
            } catch (ResourceBusyException e) {
                Log.w(TAG, "Resource busy in openSession, retrying...");
                try {
                    Thread.sleep(1000);
                } catch (InterruptedException ie) {
                    // ignore
                }
            }
        }
        throw new Error("Failed to open session");
    }

    private void getWidevineKeys(MediaDrm drm, byte[] sessionId) throws Exception {
        final String kKeyServerUrl = "https://jmt17.google.com/video/license/GetCencLicense";
        final byte[] kPssh = hex2ba("08011210e02562e04cd55351b14b3d748d36ed8e");
        final String kClientAuth = "?source=YOUTUBE&video_id=EGHC6OHNbOo&oauth=ya.gtsqawidevine";
        final String kPort = "80";
        KeyRequester keyRequester = new KeyRequester(kPssh, kKeyServerUrl + ":" + kPort + kClientAuth);
        if (keyRequester.doTransact(drm, sessionId, MediaDrm.KEY_TYPE_STREAMING) == null) {
            throw new Error("Failed to get keys from license server!");
        }
    }

    private static byte[] hex2ba(String s) {
        int len = s.length();
        byte[] data = new byte[len / 2];
        for (int i = 0; i < len; i += 2) {
            data[i / 2] = (byte) ((Character.digit(s.charAt(i), 16) << 4)
                                  + Character.digit(s.charAt(i+1), 16));
        }
        return data;
    }
}
