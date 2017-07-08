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
package com.android.compatibility.common.deviceinfo;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.util.Log;

import java.io.File;
import java.util.concurrent.CountDownLatch;

import com.android.compatibility.common.util.InfoStore;

/**
 * Collect device information on target device and write to a JSON file.
 */
public abstract class DeviceInfoActivity extends Activity {

    /** Device info result code: collector failed to complete. */
    private static final int DEVICE_INFO_RESULT_FAILED = -2;
    /** Device info result code: collector completed with error. */
    private static final int DEVICE_INFO_RESULT_ERROR = -1;
    /** Device info result code: collector has started but not completed. */
    private static final int DEVICE_INFO_RESULT_STARTED = 0;
    /** Device info result code: collector completed success. */
    private static final int DEVICE_INFO_RESULT_OK = 1;

    private static final int MAX_STRING_VALUE_LENGTH = 1000;
    private static final int MAX_ARRAY_LENGTH = 1000;

    private static final String LOG_TAG = "DeviceInfoActivity";

    private CountDownLatch mDone = new CountDownLatch(1);
    private String mResultFilePath = null;
    private String mErrorMessage = "Collector has started.";
    private int mResultCode = DEVICE_INFO_RESULT_STARTED;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        final File dir = new File(Environment.getExternalStorageDirectory(), "device-info-files");
        if (!Environment.getExternalStorageState().equals(Environment.MEDIA_MOUNTED)) {
            failed("External storage is not mounted");
        } else if (!dir.mkdirs() && !dir.isDirectory()) {
            failed("Cannot create directory for device info files");
        } else {
            try {
                File jsonFile = new File(dir, getClass().getSimpleName() + ".deviceinfo.json");
                jsonFile.createNewFile();
                mResultFilePath = jsonFile.getAbsolutePath();
                InfoStore store = new InfoStore(jsonFile);
                store.open();
                collectDeviceInfo(store);
                store.close();
                if (mResultCode == DEVICE_INFO_RESULT_STARTED) {
                    mResultCode = DEVICE_INFO_RESULT_OK;
                }
            } catch (Exception e) {
                failed("Could not collect device info: " + e.getMessage());
            }
        }
        Intent data = new Intent();
        if (mResultCode == DEVICE_INFO_RESULT_OK) {
            data.setData(Uri.parse(mResultFilePath));
            setResult(RESULT_OK, data);
        } else {
            data.setData(Uri.parse(mErrorMessage));
            setResult(RESULT_CANCELED, data);
        }

        mDone.countDown();
        finish();
    }

    /**
     * Method to collect device information.
     */
    protected abstract void collectDeviceInfo(InfoStore store) throws Exception;

    void waitForActivityToFinish() {
        try {
            mDone.await();
        } catch (Exception e) {
            failed("Exception while waiting for activity to finish: " + e.getMessage());
        }
    }

    /**
     * Returns the error message if collector did not complete successfully.
     */
    String getErrorMessage() {
        if (mResultCode == DEVICE_INFO_RESULT_OK) {
            return null;
        }
        return mErrorMessage;
    }

    /**
     * Returns the path to the json file if collector completed successfully.
     */
    String getResultFilePath() {
        if (mResultCode == DEVICE_INFO_RESULT_OK) {
            return mResultFilePath;
        }
        return null;
    }

    private void error(String message) {
        mResultCode = DEVICE_INFO_RESULT_ERROR;
        mErrorMessage = message;
        Log.e(LOG_TAG, message);
    }

    private void failed(String message) {
        mResultCode = DEVICE_INFO_RESULT_FAILED;
        mErrorMessage = message;
        Log.e(LOG_TAG, message);
    }

}

