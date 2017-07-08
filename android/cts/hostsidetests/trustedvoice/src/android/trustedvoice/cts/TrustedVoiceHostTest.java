/*
 * Copyright (C) 2016 The Android Open Source Project
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

package android.trustedvoice.cts;

import com.android.cts.tradefed.build.CtsBuildHelper;
import com.android.tradefed.build.IBuildInfo;
import com.android.tradefed.device.ITestDevice;
import com.android.tradefed.testtype.DeviceTestCase;
import com.android.tradefed.testtype.IBuildReceiver;


import java.io.File;
import java.lang.String;
import java.util.Scanner;



/**
 * Test to check the APK logs to Logcat.
 * This test first locks the device screen and then runs the associated app to run the test.
 *
 * When this test builds, it also builds {@see android.trustedvoice.app.TrustedVoiceActivity}
 * into an APK which it then installs at runtime. TrustedVoiceActivity sets the
 * FLAG_DISMISS_KEYGUARD, prints a message to Logcat and then gets uninstalled.
 */
public class TrustedVoiceHostTest extends DeviceTestCase implements IBuildReceiver {

    /**
     * The package name of the APK.
     */
    private static final String PACKAGE = "android.trustedvoice.app";

    /**
     * Lock screen key event code.
     */
    private static final int LOCK_KEYEVENT = 26;

    /**
     * The file name of the APK.
     */
    private static final String APK = "CtsTrustedVoiceApp.apk";

    /**
     * The class name of the main activity in the APK.
     */
    private static final String CLASS = "TrustedVoiceActivity";

    /**
     * The command to launch the main activity.
     */
    private static final String START_COMMAND = String.format(
            "am start -W -a android.intent.action.MAIN -n %s/%s.%s", PACKAGE, PACKAGE, CLASS);

    /**
     * The command to lock the device.
     */
    private static final String LOCKSCREEN_COMMAND = String.format(
            "input keyevent %d", LOCK_KEYEVENT);

    /**
     * The test string to look for.
     */
    private static final String TEST_STRING = "TrustedVoiceTestString";

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
     * Tests the string was successfully logged to Logcat from the activity.
     *
     * @throws Exception
     */
    public void testLogcat() throws Exception {
        // Clear logcat.
        mDevice.executeAdbCommand("logcat", "-c");
        // Lock the device
        mDevice.executeShellCommand(LOCKSCREEN_COMMAND);
        // Start the APK and wait for it to complete.
        mDevice.executeShellCommand(START_COMMAND);
        // Dump logcat.
        String logs = mDevice.executeAdbCommand("logcat", "-v", "brief", "-d", CLASS + ":I", "*S");
        // Search for string.
        Scanner in = new Scanner(logs);
        String testString = "";
        try {
            while (in.hasNextLine()) {
                String line = in.nextLine();
                if(line.contains(TEST_STRING)) {
                    // Retrieve the test string.
                    testString = line.split(":")[1].trim();
                    break;
                }
            }
            // Assert the logged string matches the test string.
            assertNotNull("Test string must not be null", testString);
            assertEquals("Test string does not match", TEST_STRING, testString);
        } finally {
            in.close();
        }
    }
}
