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

import android.annotation.SuppressLint;
import android.content.Context;
import android.content.pm.PackageManager;
import android.os.Build;
import android.util.Log;
import android.util.Pair;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.Locale;
import java.util.Map;

/**
 * HttpPost utility functions.
 */
public final class HttpPost {
    private static final String TAG = "HttpPost";

    private HttpPost() {}

    /**
     * Executes a post request using {@link HttpURLConnection}.
     *
     * @param url The request URL.
     * @param data The request body, or null.
     * @param requestProperties Request properties, or null.
     * @return The response code and body.
     * @throws IOException If an error occurred making the request.
     */
    public static Pair<Integer, byte[]> execute(String url, byte[] data,
            Map<String, String> requestProperties) throws IOException {
        HttpURLConnection urlConnection = null;
        try {
            urlConnection = (HttpURLConnection) new URL(url).openConnection();
            urlConnection.setRequestMethod("POST");
            urlConnection.setDoOutput(data != null);
            urlConnection.setDoInput(true);
            urlConnection.setConnectTimeout(5000);
            urlConnection.setReadTimeout(5000);
            if (requestProperties != null) {
                for (Map.Entry<String, String> requestProperty : requestProperties.entrySet()) {
                    urlConnection.setRequestProperty(requestProperty.getKey(),
                            requestProperty.getValue());
                }
            }
            // Write the request body, if there is one.
            if (data != null) {
                OutputStream out = urlConnection.getOutputStream();
                try {
                    out.write(data);
                } finally {
                    out.close();
                }
            }
            // Read the response code.
            int responseCode = urlConnection.getResponseCode();
            // Read the response body.
            InputStream inputStream = urlConnection.getInputStream();
            try {
                ByteArrayOutputStream byteArrayOutputStream = new ByteArrayOutputStream();
                byte scratch[] = new byte[1024];
                int bytesRead;
                while ((bytesRead = inputStream.read(scratch)) != -1) {
                    byteArrayOutputStream.write(scratch, 0, bytesRead);
                }
                byte[] responseBody = byteArrayOutputStream.toByteArray();
                Log.d(TAG, "responseCode=" + responseCode + ", length=" + responseBody.length);
                return Pair.create(responseCode, responseBody);
            } finally {
                inputStream.close();
            }
        } finally {
            if (urlConnection != null) {
                urlConnection.disconnect();
            }
        }
    }

}
