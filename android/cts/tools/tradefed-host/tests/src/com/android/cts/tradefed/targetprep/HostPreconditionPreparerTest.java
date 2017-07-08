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

import com.android.ddmlib.IDevice;
import com.android.tradefed.config.OptionSetter;
import com.android.tradefed.device.ITestDevice;
import com.android.tradefed.targetprep.TargetSetupError;

import java.awt.Dimension;

import junit.framework.TestCase;

import org.easymock.EasyMock;

/**
 * Unit tests for {@link HostPreconditionPreparer}.
 */
public class HostPreconditionPreparerTest extends TestCase {

    private HostPreconditionPreparer mHostPreconditionPreparer;
    private ITestDevice mMockDevice;
    private OptionSetter mOptionSetter;

    private final Dimension DEFAULT_DIMENSION =
            HostPreconditionPreparer.resolutions[HostPreconditionPreparer.RES_DEFAULT];

    /**
     * {@inheritDoc}
     */
    @Override
    protected void setUp() throws Exception {
        super.setUp();
        mHostPreconditionPreparer = new HostPreconditionPreparer();
        mMockDevice = EasyMock.createMock(ITestDevice.class);
        mOptionSetter = new OptionSetter(mHostPreconditionPreparer);

        EasyMock.expect(mMockDevice.getMountPoint(IDevice.MNT_EXTERNAL_STORAGE)).andReturn(
                "/sdcard").anyTimes();
    }

    public void testLocationServicesOnGpsNetwork() throws Exception {
        String shellCmd = String.format(
                "settings get secure %s", HostPreconditionPreparer.LOCATION_PROVIDERS_ALLOWED);

        // expect location to be enabled by both gps and network
        EasyMock.expect(
                mMockDevice.executeShellCommand(shellCmd)).andReturn("\ngps,network\n").once();
        EasyMock.replay(mMockDevice);
        mHostPreconditionPreparer.checkLocationServices(mMockDevice);
    }

    public void testLocationServicesOnGps() throws Exception {
        String shellCmd = String.format(
                "settings get secure %s", HostPreconditionPreparer.LOCATION_PROVIDERS_ALLOWED);

        // expect location to be enabled by gps only
        EasyMock.expect(
                mMockDevice.executeShellCommand(shellCmd)).andReturn("\ngps\n").once();
        EasyMock.replay(mMockDevice);
        mHostPreconditionPreparer.checkLocationServices(mMockDevice);
    }

    public void testLocationServicesOnNetwork() throws Exception {
        String shellCmd = String.format(
                "settings get secure %s", HostPreconditionPreparer.LOCATION_PROVIDERS_ALLOWED);

        // expect location to be enabled by network only
        EasyMock.expect(
                mMockDevice.executeShellCommand(shellCmd)).andReturn("\nnetwork\n").once();
        EasyMock.replay(mMockDevice);
        mHostPreconditionPreparer.checkLocationServices(mMockDevice);
    }

    public void testLocationServicesOff() throws Exception {
        String shellCmd = String.format(
                "settings get secure %s", HostPreconditionPreparer.LOCATION_PROVIDERS_ALLOWED);

        // expect location to be disabled
        EasyMock.expect(
                mMockDevice.executeShellCommand(shellCmd)).andReturn("\n\n").once();
        EasyMock.replay(mMockDevice);
        try {
            mHostPreconditionPreparer.checkLocationServices(mMockDevice);
            fail("TargetSetupError expected");
        } catch (TargetSetupError e) {
            // Expected
        }
    }

    public void testWifiConnected() throws Exception {
        EasyMock.expect(mMockDevice.checkConnectivity()).andReturn(true).once();
        EasyMock.replay(mMockDevice);
        mHostPreconditionPreparer.runWifiPrecondition(mMockDevice);
    }

    public void testWifiDisconnected() throws Exception {
        EasyMock.expect(mMockDevice.checkConnectivity()).andReturn(false).once();
        EasyMock.replay(mMockDevice);
        try {
            mHostPreconditionPreparer.runWifiPrecondition(mMockDevice);
            fail("TargetSetupError expected");
        } catch (TargetSetupError e) {
            // Expected
        }
    }

    public void testWifiConnectionSuccessful() throws Exception {
        EasyMock.expect(
                mMockDevice.connectToWifiNetworkIfNeeded("wifi-ssid", "wifi-psk")).andReturn(true).once();
        mOptionSetter.setOptionValue("wifi-ssid", "wifi-ssid");
        mOptionSetter.setOptionValue("wifi-psk", "wifi-psk");
        EasyMock.replay(mMockDevice);
        mHostPreconditionPreparer.runWifiPrecondition(mMockDevice);
    }

    public void testWifiConnectionUnuccessful() throws Exception {
        EasyMock.expect(
                mMockDevice.connectToWifiNetworkIfNeeded("wifi-ssid", "wifi-psk")).andReturn(false).once();
        mOptionSetter.setOptionValue("wifi-ssid", "wifi-ssid");
        mOptionSetter.setOptionValue("wifi-psk", "wifi-psk");
        EasyMock.replay(mMockDevice);
        try {
            mHostPreconditionPreparer.runWifiPrecondition(mMockDevice);
            fail("TargetSetupError expected");
        } catch (TargetSetupError e) {
            // Expected
        }
    }

    public void testResolutionString() throws Exception {
        assertEquals("480x360", mHostPreconditionPreparer.resolutionString(DEFAULT_DIMENSION));
    }

    public void testGetDeviceDirs() throws Exception {
        EasyMock.replay(mMockDevice);
        String shortDir =
                mHostPreconditionPreparer.getDeviceShortDir(mMockDevice, DEFAULT_DIMENSION);
        String fullDir =
                mHostPreconditionPreparer.getDeviceFullDir(mMockDevice, DEFAULT_DIMENSION);
        assertEquals(shortDir, "/sdcard/test/bbb_short/480x360");
        assertEquals(fullDir, "/sdcard/test/bbb_full/480x360");
    }

    public void testGetMaxVideoPlaybackResolutionFound() throws Exception {
        // set "smallest app" field to DEFAULT_DIMENSION
        String mockDumpsysOutput = "mBaseDisplayInfo=DisplayInfo{\"Built-in Screen\", uniqueId " +
                "\"local:0\", app 1440 x 2560, real 1440 x 2560, largest app 1440 x 2560, " +
                "smallest app 360 x 480, mode 1, defaultMode 1, modes [{id=1, width=1440, " +
                "height=2560, fps=60.0}], rotation 0, density 560 (494.27 x 492.606) dpi, " +
                "layerStack 0, appVsyncOff 2500000, presDeadline 17666667, type BUILT_IN, state " +
                "ON, FLAG_SECURE, FLAG_SUPPORTS_PROTECTED_BUFFERS}\n";
        EasyMock.expect(mMockDevice.executeShellCommand(
                "dumpsys display | grep mBaseDisplayInfo")).andReturn(mockDumpsysOutput).once();
        EasyMock.replay(mMockDevice);
        Dimension result = mHostPreconditionPreparer.getMaxVideoPlaybackResolution(mMockDevice);
        assertEquals(result, DEFAULT_DIMENSION);
    }

    public void testGetMaxVideoPlaybackResolutionNotFound() throws Exception {
        String mockDumpsysOutput = "incorrect output";
        EasyMock.expect(mMockDevice.executeShellCommand(
                "dumpsys display | grep mBaseDisplayInfo")).andReturn(mockDumpsysOutput).once();
        EasyMock.replay(mMockDevice);
        Dimension result = mHostPreconditionPreparer.getMaxVideoPlaybackResolution(mMockDevice);
        Dimension maxRes =
                HostPreconditionPreparer.resolutions[HostPreconditionPreparer.RES_1920_1080];
        assertEquals(result,maxRes);
    }

    public void testSkipMediaDownload() throws Exception {
        mOptionSetter.setOptionValue("skip-media-download", "true");
        EasyMock.replay();
        mHostPreconditionPreparer.runMediaPrecondition(mMockDevice);
    }

}
