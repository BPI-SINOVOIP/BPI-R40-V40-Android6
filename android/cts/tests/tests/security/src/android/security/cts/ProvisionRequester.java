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

import android.media.DeniedByServerException;
import android.media.MediaDrm;
import android.util.Log;
import android.util.Pair;

import java.io.IOException;
import java.util.HashMap;

public class ProvisionRequester {
    private final String TAG = "ProvisionRequester";

    public ProvisionRequester() {
    }

    public void doTransact(final MediaDrm drm) {
        Thread t = new Thread() {
            @Override
            public void run() {
                MediaDrm.ProvisionRequest drmRequest;
                drmRequest = drm.getProvisionRequest();
                byte[] responseBody = postRequest(drmRequest.getDefaultUrl(),
                        drmRequest.getData());

                if (responseBody == null) {
                    Log.e(TAG, "No response from provisioning server!");
                } else {
                    try {
                        drm.provideProvisionResponse(responseBody);
                    } catch (DeniedByServerException e) {
                        Log.e(TAG, "Server denied provisioning request");
                    }
                }
            }
        };
        t.start();

        try {
            t.join();
        } catch (InterruptedException e) {
        }
    }

    // TODO May want to throw exceptions without having try/catch in body.
    private byte[] postRequest(String url, byte[] drmRequest) {
        String signedUrl = url + "&signedRequest=" + new String(drmRequest);
        Log.d(TAG, "PostRequest:" + signedUrl);

        HashMap<String, String> headers = new HashMap<>();
        headers.put("Accept", "*/*");
        headers.put("User-Agent", "Widevine CDM v1.0");
        headers.put("Content-Type", "application/json");
        headers.put("Connection", "close");

        try {
            Pair<Integer, byte[]> response = HttpPost.execute(signedUrl, null, headers);
            int responseCode = response.first;
            if (responseCode != 200) {
                Log.e(TAG, "Server returned HTTP error code " + responseCode);
                return null;
            }
            return response.second;
        } catch (IOException e) {
            e.printStackTrace();
            return null;
        }
    }

    private void sleep(int msec) {
        try {
            Thread.sleep(msec);
        } catch (InterruptedException e) {
        }
    }
}
