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
import android.media.NotProvisionedException;
import android.util.Log;
import android.util.Pair;

import java.io.UnsupportedEncodingException;
import java.util.Arrays;
import java.util.concurrent.CancellationException;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

public class KeyRequester {
    private final String TAG = "KeyRequester";
    private final int MAX_RETRY_COUNT = 3;
    private final int POOL_SIZE = 1;
    private final int POOL_TERMINATION_MS_TIMEOUT = 3000;  // in milliseconds
    private final int KEYREQUEST_MS_TIMEOUT = 5000;  // in milliseconds

    private byte[] mPssh;
    private ExecutorService mExecutorService;
    private Future<byte[]> mFuture;
    private String mDefaultHeartbeatUrl;
    private String mServerUrl;

    public KeyRequester(byte[] pssh, String url) {
        mPssh = pssh;
        mServerUrl = url;
    }

    public final String getDefaultHeartbeatUrl() {
        return mDefaultHeartbeatUrl;
    }

    public byte[] doTransact(final MediaDrm drm, final byte[] sessionId, final int keyType) {
        boolean retryRequest;
        boolean retryTransaction;
        byte[] keySetIdResult;
        int getKeyRequestRetryCount;
        int provisioningRetryCount = 0;
        MediaDrm.KeyRequest drmRequest;

        mExecutorService = Executors.newFixedThreadPool(POOL_SIZE);

        do {
            drmRequest = null;
            getKeyRequestRetryCount = 0;
            keySetIdResult = null;
            retryTransaction = false;

            do {
                retryRequest = false;

                try {
                    drmRequest = drm.getKeyRequest(sessionId, mPssh,
                        "video/avc", keyType, null);
                } catch (NotProvisionedException e) {
                    Log.i(TAG, "Invalid certificate, reprovisioning");
                    ProvisionRequester provisionRequester = new ProvisionRequester();
                    provisionRequester.doTransact(drm);
                    retryRequest = true;
                }
            } while (retryRequest && ++getKeyRequestRetryCount < MAX_RETRY_COUNT);

            if (drmRequest == null) {
                Log.e(TAG, "Failed to get key request");
                return null;
            }

            try {
                mFuture = mExecutorService.submit(new KeyRequesterTask(mServerUrl, drmRequest));
            } catch (RejectedExecutionException e) {
                Log.e(TAG, "Failed to submit KeyRequesterTask for execution", e);
                if (++provisioningRetryCount < MAX_RETRY_COUNT) {
                    continue;
                } else {
                    break;
                }
            }

            try {
                byte[] responseBody = mFuture.get(KEYREQUEST_MS_TIMEOUT, TimeUnit.MILLISECONDS);
                if (responseBody == null) {
                    Log.e(TAG, "No response from license server!");
                    retryTransaction = true;
                } else {
                    byte[] drmResponse = parseResponseBody(responseBody);
                    try {
                        keySetIdResult = drm.provideKeyResponse(sessionId, drmResponse);
                    } catch (NotProvisionedException e) {
                        Log.i(TAG, "Response invalidated the certificate, reprovisioning");
                        ProvisionRequester provisionRequester = new ProvisionRequester();
                        provisionRequester.doTransact(drm);
                        retryTransaction = true;
                    } catch (DeniedByServerException e) {
                        // informational, the event handler will take care of provisioning
                        Log.i(TAG, "Server rejected the key request");
                    }  catch (IllegalStateException e) {
                        Log.e(TAG, "provideKeyResponse failed", e);
                    }

                    try {
                        // first call to getKeyRequest does not return heartbeat url
                        drmRequest = drm.getKeyRequest(sessionId, mPssh, "video/avc",
                                keyType, null);
                        try {
                            mDefaultHeartbeatUrl = drmRequest.getDefaultUrl();
                        } catch (Exception e) {
                            // ignore
                        }
                    } catch (NotProvisionedException e) {
                        Log.e(TAG, "Fails to get heartbeat url");
                    }
                    break;
                }
            } catch (ExecutionException | InterruptedException ex) {
                Log.e(TAG, "Failed to execute KeyRequesterTask", ex);
                shutdownAndAwaitTermination(mExecutorService);
                return null;
            } catch (TimeoutException te) {
                // The request timed out. The network is possibly too slow.
                // Cancel the running task.
                Log.d(TAG, "Request timed out, retry...");
                mFuture.cancel(true);
                retryTransaction = true;
            }
        } while (retryTransaction && ++provisioningRetryCount < MAX_RETRY_COUNT);

        shutdownAndAwaitTermination(mExecutorService);
        return keySetIdResult;
    }

    private void shutdownAndAwaitTermination(ExecutorService pool) {
        pool.shutdown();  // disable new tasks from being submitted
        try {
            // wait for existing tasks to terminate
            if (!pool.awaitTermination(POOL_TERMINATION_MS_TIMEOUT, TimeUnit.MILLISECONDS)) {
                pool.shutdownNow();
                // wait for tasks to respond to being cancelled
                if (!pool.awaitTermination(POOL_TERMINATION_MS_TIMEOUT, TimeUnit.MILLISECONDS))
                    Log.e(TAG, "Pool did not terminate");
            }
        } catch (InterruptedException ie) {
            // (Re-)Cancel if current thread also interrupted
            pool.shutdownNow();
            // Preserve interrupt status
            Thread.currentThread().interrupt();
        }
    }

    // Validate the response body and return the drmResponse blob.
    private byte[] parseResponseBody(byte[] responseBody) {
        String bodyString = null;
        try {
            bodyString = new String(responseBody, "UTF-8");
        } catch (UnsupportedEncodingException e) {
            e.printStackTrace();
        }

        if (bodyString == null) {
            return null;
        }

        if (bodyString.startsWith("GLS/")) {
            if (!bodyString.startsWith("GLS/1.")) {
                Log.e(TAG, "Invalid server version, expected 1.x");
                return null;
            }
            int drmMessageOffset = bodyString.indexOf("\r\n\r\n");
            if (drmMessageOffset == -1) {
                Log.e(TAG, "Invalid server response, could not locate drm message");
                return null;
            }
            responseBody = Arrays.copyOfRange(responseBody, drmMessageOffset + 4,
                    responseBody.length);
        }
        return responseBody;
    }
}
