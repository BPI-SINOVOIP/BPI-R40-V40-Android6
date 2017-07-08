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

import android.media.MediaDrm;
import android.util.Log;
import android.util.Pair;

import java.io.IOException;
import java.util.concurrent.Callable;
import java.util.HashMap;

public class KeyRequesterTask implements Callable<byte[]> {
    private static final String TAG = "KeyRequesterTask";
    private final MediaDrm.KeyRequest mDrmRequest;
    private final String mUrl;

    public KeyRequesterTask(String url, MediaDrm.KeyRequest drmRequest) {
        mDrmRequest = drmRequest;
        mUrl = url;
    }

    /**
     * @return a byte array containing the license response if successful,
     * {@code null} otherwise.
     */
    @Override
    public byte[] call() throws Exception {
        byte[] drmRequest = mDrmRequest.getData();
        Log.d(TAG, "PostRequest:" + mUrl);

        HashMap<String, String> headers = new HashMap<>();
        headers.put("User-Agent", "Widevine CDM v1.0");
        headers.put("Connection", "close");

        try {
            Pair<Integer, byte[]> response = HttpPost.execute(mUrl, drmRequest, headers);
            int responseCode = response.first;
            if (responseCode != 200) {
                Log.d(TAG, "Server returned HTTP error code " + responseCode);
                return null;
            }
            return response.second;
        } catch (IOException e) {
            e.printStackTrace();
            return null;
        }
    }
}

