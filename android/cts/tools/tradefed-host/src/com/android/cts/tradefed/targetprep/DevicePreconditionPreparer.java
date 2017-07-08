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

package com.android.cts.tradefed.targetprep;

import com.android.cts.tradefed.build.CtsBuildHelper;
import com.android.cts.tradefed.testtype.Abi;
import com.android.ddmlib.Log;
import com.android.ddmlib.testrunner.TestIdentifier;
import com.android.tradefed.build.IBuildInfo;
import com.android.tradefed.build.IFolderBuildInfo;
import com.android.tradefed.config.Option;
import com.android.tradefed.config.OptionClass;
import com.android.tradefed.device.DeviceNotAvailableException;
import com.android.tradefed.device.ITestDevice;
import com.android.tradefed.log.LogUtil;
import com.android.tradefed.result.InputStreamSource;
import com.android.tradefed.result.ITestInvocationListener;
import com.android.tradefed.result.LogDataType;
import com.android.tradefed.result.TestSummary;
import com.android.tradefed.testtype.IAbi;
import com.android.tradefed.testtype.InstrumentationTest;
import com.android.tradefed.targetprep.BuildError;
import com.android.tradefed.targetprep.ITargetPreparer;
import com.android.tradefed.targetprep.TargetSetupError;

import java.io.File;
import java.io.FileNotFoundException;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/**
 * A {@link ITargetPreparer} that performs precondition checks on the device-side for CTS.
 * <p/>
 * This class instruments an APK containing tests verifying that the device meets CTS
 * preconditions. At present, the APK contains tests to ensure that the device's screen is not
 * locked, and that the device's external storage is present and writable. The test lives under
 * //cts/tools/tradefed-host/preconditions, and can be modified to perform further checks and tasks
 * from the device-side.
 */
@OptionClass(alias="device-precondition-preparer")
public class DevicePreconditionPreparer implements ITargetPreparer {

    /* This option also exists in the HostPreconditionPreparer */
    @Option(name = "skip-preconditions",
            description = "Whether to skip precondition checks and automation")
    protected boolean mSkipPreconditions = false;

    /* Constants for the InstrumentationTest */
    private static final String APK_NAME = "CtsPreconditionsApp.apk";
    private static final String PACKAGE_NAME = "com.android.cts.preconditions";
    private static final String RUNNER_NAME = "android.support.test.runner.AndroidJUnitRunner";

    private static final String LOG_TAG = DevicePreconditionPreparer.class.getSimpleName();

    /* Map used to track test failures */
    private ConcurrentHashMap<TestIdentifier, String> testFailures = new ConcurrentHashMap<>();

    /* Helper that logs a message with LogLevel.WARN */
    private static void printWarning(String msg) {
        LogUtil.printLog(Log.LogLevel.WARN, LOG_TAG, msg);
    }

    /**
     * {@inheritDoc}
     */
    @Override
    public void setUp(ITestDevice device, IBuildInfo buildInfo) throws TargetSetupError,
            BuildError, DeviceNotAvailableException {
        if (mSkipPreconditions) {
            return; // skipping device-side preconditions
        }

        try {
            if (!instrument(device, buildInfo)) {
                printWarning("Not all device-side preconditions met, " +
                        "CTS tests may fail as a result.");
            }
        } catch (FileNotFoundException e) {
            throw new TargetSetupError(
                    String.format("Couldn't find %s to instrument", APK_NAME), e);
        }
    }

    /* Instruments the APK on the device, and logs precondition test failures, if any are found.
     * Returns true if all tests pass, and otherwise returns false */
    private boolean instrument(ITestDevice device, IBuildInfo buildInfo)
            throws DeviceNotAvailableException, FileNotFoundException {
        ITestInvocationListener listener = new PreconditionPreparerListener();
        CtsBuildHelper mCtsBuild = CtsBuildHelper.createBuildHelper(buildInfo);
        File apkFile = mCtsBuild.getTestApp(APK_NAME); // get the APK file with the CtsBuildHelper
        InstrumentationTest instrTest = new InstrumentationTest();
        instrTest.setDevice(device);
        instrTest.setInstallFile(apkFile);
        instrTest.setPackageName(PACKAGE_NAME);
        instrTest.setRunnerName(RUNNER_NAME);
        instrTest.run(listener);
        boolean success = true;
        if (!testFailures.isEmpty()) {
            success = false; // at least one precondition has failed
            for (TestIdentifier test : testFailures.keySet()) {
                String trace = testFailures.get(test);
                printWarning(String.format(
                        "Precondition test %s failed.\n%s", test.getTestName(), trace));
            }
        }
        return success;
    }

    /**
     * The PreconditionPreparerListener is an implementation of ITestInvocationListener
     * that adds entries to the ConcurrentHashMap 'testFailures' of the outer class whenever
     * a test fails. The listener also logs information if the test run fails, for debugging
     * purposes.
     */
    public class PreconditionPreparerListener implements ITestInvocationListener {

        /**
         * {@inheritDoc}
         */
        @Override
        public void testFailed(TestIdentifier test, String trace) {
            testFailures.put(test, trace);
        }

        /**
         * {@inheritDoc}
         */
        @Override
        public void testRunFailed(String errorMessage) {
            printWarning(String.format(
                    "Device-side preconditions test run failed: %s", errorMessage));
        }

        /**
         * {@inheritDoc}
         */
        @Override
        public void testEnded(TestIdentifier test, Map<String, String> metrics) {}

        /**
         * {@inheritDoc}
         */
        @Override
        public void invocationStarted(IBuildInfo buildInfo) {}

        /**
         * {@inheritDoc}
         */
        @Override
        public void testLog(String dataName, LogDataType dataType, InputStreamSource dataStream) {}

        /**
         * {@inheritDoc}
         */
        @Override
        public void invocationEnded(long elapsedTime) {}

        /**
         * {@inheritDoc}
         */
        @Override
        public void invocationFailed(Throwable cause) {}

        /**
         * {@inheritDoc}
         */
        @Override
        public TestSummary getSummary() {
            return null;
        }

        /**
         * {@inheritDoc}
         */
        @Override
        public void testRunStarted(String runName, int testCount) {}

        /**
         * {@inheritDoc}
         */
        @Override
        public void testStarted(TestIdentifier test) {}

        /**
         * {@inheritDoc}
         */
        @Override
        public void testAssumptionFailure(TestIdentifier test, String trace) {}

        /**
         * {@inheritDoc}
         */
        @Override
        public void testIgnored(TestIdentifier test) {}

        /**
         * {@inheritDoc}
         */
        @Override
        public void testRunStopped(long elapsedTime) {}

        /**
         * {@inheritDoc}
         */
        @Override
        public void testRunEnded(long elapsedTime, Map<String, String> runMetrics) {}
    }
}
