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

package com.android.cts.cpptools;

import com.android.cts.tradefed.build.CtsBuildHelper;
import com.android.tradefed.build.IBuildInfo;
import com.android.tradefed.device.ITestDevice;
import com.android.tradefed.testtype.DeviceTestCase;
import com.android.tradefed.testtype.IBuildReceiver;

import java.io.File;
import java.lang.String;
import java.util.Scanner;
import java.util.regex.Pattern;
import java.util.regex.Matcher;

/**
 * Test to check the host can execute commands via "adb shell run-as".
 */
public class RunAsHostTest extends DeviceTestCase implements IBuildReceiver {

    /**
     * The package name of the APK.
     */
    private static final String PACKAGE = "android.sample.app";

    /**
     * The file name of the APK.
     */
    private static final String APK = "CtsCppToolsApp.apk";

    
    /**
     * A reference to the build.
     */
    private CtsBuildHelper mBuild;

    /**
     * A reference to the device under test.
     */
    private ITestDevice mDevice;

    @Override
    public void setBuild(IBuildInfo buildInfo) {
        // Get the build, this is used to access the APK.
        mBuild = CtsBuildHelper.createBuildHelper(buildInfo);
    }

    @Override
    protected void setUp() throws Exception {
        super.setUp();
        // Get the device, this gives a handle to run commands and install APKs.
        mDevice = getDevice();
        // Remove any previously installed versions of this APK.
        mDevice.uninstallPackage(PACKAGE);
        // Get the APK from the build.
        File app = mBuild.getTestApp(APK);
        // Install the APK on the device.
        mDevice.installPackage(app, false);
    }

    @Override
    protected void tearDown() throws Exception {
        // Remove the package once complete.
        mDevice.uninstallPackage(PACKAGE);
        super.tearDown();
    }

    /**
     * Tests that host can execute shell commands as a debuggable app via adb.
     *
     * @throws Exception
     */
    public void testRunAs() throws Exception {
        String runAsResult = mDevice.executeShellCommand("run-as android.sample.app id -u");
        assertNotNull("adb shell command failed", runAsResult);
        runAsResult = runAsResult.trim();
        Matcher appIdMatcher = Pattern.compile("^uid=([0-9]+).*$").matcher(runAsResult);
        assertTrue("unexpected result returned by adb shell command: \"" + runAsResult + "\"",
                   appIdMatcher.matches());
        String appIdString = appIdMatcher.group(1);
        assertTrue("invalid app id " + appIdString, Integer.parseInt(appIdString) > 10000);
    }
}
