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
import com.android.ddmlib.Log;
import com.android.tradefed.build.IBuildInfo;
import com.android.tradefed.config.Option;
import com.android.tradefed.config.OptionClass;
import com.android.tradefed.device.DeviceNotAvailableException;
import com.android.tradefed.device.ITestDevice;
import com.android.tradefed.log.LogUtil;
import com.android.tradefed.targetprep.BuildError;
import com.android.tradefed.targetprep.ITargetPreparer;
import com.android.tradefed.targetprep.TargetSetupError;
import com.android.tradefed.util.FileUtil;
import com.android.tradefed.util.ZipUtil;

import java.awt.Dimension;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.IOException;
import java.net.MalformedURLException;
import java.net.URL;
import java.net.URLConnection;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.zip.ZipFile;

/**
 * A {@link ITargetPreparer} that performs steps on the host-side to meet the preconditions of CTS.
 * <p/>
 * This class is intended for runs of CTS against a device running a user build.
 * <p/>
 * This class performs checks to verify that the location services are on, WiFi is connected,
 * the device locale is set to 'en-US', and that the device runs a user build. The class also
 * performs automation to ensure that 3rd party app installs are enabled, the 'Stay Awake' setting
 * is turned on, and that the appropriate media files are pushed to the device for the media tests.
 * Additionally, options are provided for automatically connecting to a specific WiFi network.
 */
@OptionClass(alias="host-precondition-preparer")
public class HostPreconditionPreparer implements ITargetPreparer {

    /* This option also exists in the DevicePreconditionPreparer */
    @Option(name = "skip-preconditions",
            description = "Whether to skip precondition checks and automation")
    protected boolean mSkipPreconditions = false;

    @Option(name = "wifi-ssid", description = "Name of the WiFi network with which to connect")
    protected String mWifiSsid = null;

    @Option(name = "wifi-psk", description = "The WPA-PSK associated with option 'wifi-ssid'")
    protected String mWifiPsk = null;

    @Option(name = "skip-media-download",
            description = "Whether to skip verifying/downloading media files")
    protected boolean mSkipMediaDownload = false;

    @Option(name = "skip-wifi-check",
            description = "Whether to skip verification of network connection")
    protected boolean mSkipWifiCheck = false;

    @Option(name = "local-media-path",
            description = "Absolute path of the media files directory on the host, containing" +
            "'bbb_short' and 'bbb_full' directories")
    protected String mLocalMediaPath = null;

    private static final String LOG_TAG = HostPreconditionPreparer.class.getSimpleName();

    private static final String WIFI_FEATURE = "android.hardware.wifi";
    private static final String LOCATION_GPS_FEATURE = "android.hardware.location.gps";
    private static final String LOCATION_NETWORK_FEATURE = "android.hardware.location.network";

    /* Constants found in android.provider.Settings */
    protected static final String STAY_ON_WHILE_PLUGGED_IN = "stay_on_while_plugged_in";
    protected static final String PACKAGE_VERIFIER_INCLUDE_ADB = "verifier_verify_adb_installs";
    protected static final String LOCATION_PROVIDERS_ALLOWED = "location_providers_allowed";
    /* Constant from android.os.BatteryManager */
    private static final int BATTERY_PLUGGED_ANY = 7;

    /* Name and expected value of the device's locale property */
    private static final String LOCALE_PROPERTY_STRING = "ro.product.locale";
    private static final String US_EN_LOCALE_STRING = "en-US";
    /* Name and expected value of the device's build type property */
    private static final String BUILD_TYPE_PROPERTY_STRING = "ro.build.type";
    private static final String USER_BUILD_STRING = "user";

    /* Logged if the preparer fails to identify the device's maximum video playback resolution */
    private static final String MAX_PLAYBACK_RES_FAILURE_MSG =
            "Unable to parse maximum video playback resolution, pushing all media files";

    /*
     * The URL from which to download the compressed media files
     * TODO: Find a way to retrieve this programmatically
     */
    private static final String MEDIA_URL_STRING =
            "https://dl.google.com/dl/android/cts/android-cts-media-1.1.zip";

    /*
     * A default name for the local directory into which media files will be downloaded, if option
     * "local-media-path" is not provided. This name is intentionally predetermined and final, so
     * that when running CTS repeatedly, media files downloaded to the host in a previous run of
     * CTS can be found in this directory, which will live inside the local temp directory.
     */
    private static final String MEDIA_FOLDER_NAME = "android-cts-media";

    /* Constants identifying video playback resolutions of the media files to be copied */
    protected static final int RES_176_144 = 0; // 176x144 resolution
    protected static final int RES_DEFAULT = 1; // 480x360, the default max playback resolution
    protected static final int RES_720_480 = 2; // 720x480 resolution
    protected static final int RES_1280_720 = 3; // 1280x720 resolution
    protected static final int RES_1920_1080 = 4; // 1920x1080 resolution

    /* Array of Dimensions aligning with and corresponding to the resolution constants above */
    protected static final Dimension[] resolutions = {
            new Dimension(176, 144),
            new Dimension(480, 360),
            new Dimension(720, 480),
            new Dimension(1280, 720),
            new Dimension(1920, 1080)
    };

    /*********************************************************************************************
     * HELPER METHODS
     *********************************************************************************************/

    /* Helper that logs a message with LogLevel.INFO */
    private static void printInfo(String msg) {
        LogUtil.printLog(Log.LogLevel.INFO, LOG_TAG, msg);
    }

    /* Helper that logs a message with LogLevel.WARN */
    private static void printWarning(String msg) {
        LogUtil.printLog(Log.LogLevel.WARN, LOG_TAG, msg);
    }

    /* Helper that determines whether the device supports a feature */
    private boolean hasFeature(ITestDevice device, String feature)
            throws DeviceNotAvailableException {
        String pmFeatures = device.executeShellCommand("pm list features");
        return pmFeatures.contains(feature);
    }

    /*
     * Returns a string representation of the dimension
     * For dimension of width = 480 and height = 360, the resolution string is "480x360"
     */
    protected static String resolutionString(Dimension resolution) {
        return String.format("%dx%d", resolution.width, resolution.height);
    }

    /*
     * Returns the device's absolute path to the directory containing 'short' media files, given
     * a resolution. The instance of ITestDevice is used to identify the mount point for
     * external storage.
     */
    protected String getDeviceShortDir(ITestDevice device, Dimension resolution) {
        String mountPoint = device.getMountPoint(IDevice.MNT_EXTERNAL_STORAGE);
        return String.format("%s/test/bbb_short/%s", mountPoint, resolutionString(resolution));
    }

    /*
     * Returns the device's absolute path to the directory containing 'full' media files, given
     * a resolution. The instance of ITestDevice is used to identify the mount point for
     * external storage.
     */
    protected String getDeviceFullDir(ITestDevice device, Dimension resolution) {
        String mountPoint = device.getMountPoint(IDevice.MNT_EXTERNAL_STORAGE);
        return String.format("%s/test/bbb_full/%s", mountPoint, resolutionString(resolution));
    }

    /*
     * Loops through the predefined maximum video playback resolutions from largest to smallest,
     * And returns the greatest resolution that is strictly smaller than the width and height
     * provided in the arguments.
     */
    private Dimension getMaxVideoPlaybackResolution(int width, int height) {
        for (int resIndex = resolutions.length - 1; resIndex >= RES_DEFAULT; resIndex--) {
            Dimension resolution = resolutions[resIndex];
            if (width >= resolution.width && height >= resolution.height) {
                return resolution;
            }
        }
        return resolutions[RES_DEFAULT];
    }

    /**
     * Returns the maximum video playback resolution of the device, in the form of a Dimension
     * object. This method parses dumpsys output to find resolutions listed under the
     * 'mBaseDisplayInfo' field. The value of the 'smallest app' field is used as an estimate for
     * maximum video playback resolution, and is rounded down to the nearest dimension in the
     * resolutions array.
     */
    protected Dimension getMaxVideoPlaybackResolution(ITestDevice device)
            throws DeviceNotAvailableException{
        String dumpsysOutput =
                device.executeShellCommand("dumpsys display | grep mBaseDisplayInfo");
        Pattern pattern = Pattern.compile("smallest app (\\d+) x (\\d+)");
        Matcher matcher = pattern.matcher(dumpsysOutput);
        if(!matcher.find()) {
            // could not find resolution in dumpsysOutput, return largest max playback resolution
            // so that preparer copies all media files
            printInfo(MAX_PLAYBACK_RES_FAILURE_MSG);
            return resolutions[RES_1920_1080];
        }

        int first;
        int second;
        try {
            first = Integer.parseInt(matcher.group(1));
            second = Integer.parseInt(matcher.group(2));
        } catch (NumberFormatException e) {
            // match was found, but not an identifiable resolution
            printInfo(MAX_PLAYBACK_RES_FAILURE_MSG);
            return resolutions[RES_1920_1080];
        }
        // ensure that the larger of the two values found is assigned to 'width'
        int height = Math.min(first, second);
        int width = Math.max(first, second);
        return getMaxVideoPlaybackResolution(width, height);
    }

    /*
     * After downloading and unzipping the media files, mLocalMediaPath must be the path to the
     * directory containing 'bbb_short' and 'bbb_full' directories, as it is defined in its
     * description as an option.
     * After extraction, this directory exists one level below the the directory 'mediaFolder'.
     * If the 'mediaFolder' contains anything other than exactly one subdirectory, a
     * TargetSetupError is thrown. Otherwise, the mLocalMediaPath variable is set to the path of
     * this subdirectory.
     */
    private void updateLocalMediaPath(File mediaFolder) throws TargetSetupError {
        String[] subDirs = mediaFolder.list();
        if (subDirs.length != 1) {
            throw new TargetSetupError(String.format(
                    "Unexpected contents in directory %s", mLocalMediaPath));
        }
        File newMediaFolder = new File(mediaFolder, subDirs[0]);
        mLocalMediaPath = newMediaFolder.toString();
    }

    /*
     * Copies the media files to the host from predefined url MEDIA_URL_STRING.
     * The compressed file is downloaded and unzipped into mLocalMediaPath.
     */
    private void downloadMediaToHost() throws TargetSetupError {

        URL url;
        try {
            url = new URL(MEDIA_URL_STRING);
        } catch (MalformedURLException e) {
            throw new TargetSetupError(
                    String.format("Trouble finding android media files at %s", MEDIA_URL_STRING));
        }

        File mediaFolder = new File(mLocalMediaPath);
        File mediaFolderZip = new File(mediaFolder.getAbsolutePath() + ".zip");
        try {

            mediaFolder.mkdirs();
            mediaFolderZip.createNewFile();

            URLConnection conn = url.openConnection();
            InputStream in = conn.getInputStream();
            BufferedOutputStream out =
                    new BufferedOutputStream(new FileOutputStream(mediaFolderZip));
            byte[] buffer = new byte[1024];
            int count;
            printInfo("Downloading media files to host");
            while ((count = in.read(buffer)) >= 0) {
                out.write(buffer, 0, count);
            }
            out.flush();
            out.close();
            in.close();

            printInfo("Unzipping media files");
            ZipUtil.extractZip(new ZipFile(mediaFolderZip), mediaFolder);

        } catch (IOException e) {
            FileUtil.recursiveDelete(mediaFolder);
            FileUtil.recursiveDelete(mediaFolderZip);
            throw new TargetSetupError("Failed to open media files on host");
        }
    }

    /**
     * Pushes directories containing media files to the device for all directories that:
     * - are not already present on the device
     * - contain video files of a resolution less than or equal to the device's
     *       max video playback resolution
     */
    protected void copyMediaFiles(ITestDevice device, Dimension mvpr)
            throws DeviceNotAvailableException {

        int resIndex = RES_176_144;
        while (resIndex <= RES_1920_1080) {
            Dimension copiedResolution = resolutions[resIndex];
            String resString = resolutionString(copiedResolution);
            if (copiedResolution.width > mvpr.width || copiedResolution.height > mvpr.height) {
                printInfo(String.format(
                        "Device cannot support resolutions %s and larger, media copying complete",
                        resString));
                return;
            }
            String deviceShortFilePath = getDeviceShortDir(device, copiedResolution);
            String deviceFullFilePath = getDeviceFullDir(device, copiedResolution);
            if (!device.doesFileExist(deviceShortFilePath) ||
                    !device.doesFileExist(deviceFullFilePath)) {
                printInfo(String.format("Copying files of resolution %s to device", resString));
                String localShortDirName = "bbb_short/" + resString;
                String localFullDirName = "bbb_full/" + resString;
                File localShortDir = new File(mLocalMediaPath, localShortDirName);
                File localFullDir = new File(mLocalMediaPath, localFullDirName);
                // push short directory of given resolution, if not present on device
                if(!device.doesFileExist(deviceShortFilePath)) {
                    device.pushDir(localShortDir, deviceShortFilePath);
                }
                // push full directory of given resolution, if not present on device
                if(!device.doesFileExist(deviceFullFilePath)) {
                    device.pushDir(localFullDir, deviceFullFilePath);
                }
            }
            resIndex++;
        }
    }

    /*
     * Returns true if all media files of a resolution less than or equal to 'mvpr' exist on the
     * device, and otherwise returns false.
     */
    private boolean mediaFilesExistOnDevice(ITestDevice device, Dimension mvpr)
            throws DeviceNotAvailableException{
        int resIndex = RES_176_144;
        while (resIndex <= RES_1920_1080) {
            Dimension copiedResolution = resolutions[resIndex];
            if (copiedResolution.width > mvpr.width || copiedResolution.height > mvpr.height) {
                break; // we don't need to check for resolutions greater than or equal to this
            }
            String deviceShortFilePath = getDeviceShortDir(device, copiedResolution);
            String deviceFullFilePath = getDeviceFullDir(device, copiedResolution);
            if (!device.doesFileExist(deviceShortFilePath) ||
                    !device.doesFileExist(deviceFullFilePath)) {
                // media files of valid resolution not found on the device, and must be pushed
                return false;
            }
            resIndex++;
        }
        return true;
    }

    /* Static method that returns a directory called 'dirName' in the system's temp directory */
    private static File createSimpleTempDir(String dirName) throws IOException {
        // find system's temp directory
        File throwaway = File.createTempFile(dirName, null);
        String systemTempDir = throwaway.getParent();
        // create directory with simple name within temp directory
        File simpleTempDir = new File(systemTempDir, dirName);
        // delete file used to find temp directory
        throwaway.delete();
        return simpleTempDir;
    }

    /* Method that creates a local media path, and ensures that the necessary media files live
     * within that path */
    private void createLocalMediaPath() throws TargetSetupError {
        File mediaFolder;
        try {
            mediaFolder = createSimpleTempDir(MEDIA_FOLDER_NAME);
        } catch (IOException e) {
            throw new TargetSetupError("Unable to create host temp directory for media files");
        }
        mLocalMediaPath = mediaFolder.getAbsolutePath();
        if (!mediaFolder.exists()) {
            // directory has not been created or filled by previous runs of MediaPreparer
            downloadMediaToHost(); //download media into mLocalMediaPath
        }
        updateLocalMediaPath(mediaFolder);
    }

    /*********************************************************************************************
     * PRECONDITION METHODS
     *********************************************************************************************/

    /**
     * Prevents the screen from sleeping while charging via USB
     */
    protected void enableStayAwakeSetting(ITestDevice device) throws DeviceNotAvailableException {
        String shellCmd = String.format("settings put global %s %d",
                STAY_ON_WHILE_PLUGGED_IN, BATTERY_PLUGGED_ANY);
        device.executeShellCommand(shellCmd);
    }

    /**
     * Prevents package verification on apps installed through ADB/ADT/USB
     */
    protected void disableAdbAppVerification(ITestDevice device)
            throws DeviceNotAvailableException {
        String shellCmd = String.format("settings put global %s 0", PACKAGE_VERIFIER_INCLUDE_ADB);
        device.executeShellCommand(shellCmd);
    }

    /**
     * Prevents the keyguard from re-emerging during the CTS test, which can cause some failures
     * Note: the shell command run here is not supported on L
     */
    protected void disableKeyguard(ITestDevice device) throws DeviceNotAvailableException {
        device.executeShellCommand("wm disable-keyguard");
    }

    /**
     * Prints a warning if the device's locale is something other than US English, as some tests
     * may pass or fail depending on the 'en-US' locale.
     */
    protected void verifyLocale(ITestDevice device) throws DeviceNotAvailableException {
        String locale = device.getProperty(LOCALE_PROPERTY_STRING);
        if (locale == null) {
            printWarning(String.format("Property %s not found on device", LOCALE_PROPERTY_STRING));
            return;
        }
        if (!locale.equalsIgnoreCase(US_EN_LOCALE_STRING)) {
            printWarning(String.format("Expected locale en-US, detected locale \"%s\"", locale));
        }
    }

    /**
     * Prints a warning if the device is not running a user build. This is not allowed for
     * testing production devices, but should not block testers from running CTS on a userdebug
     * build.
     */
    protected void verifyUserBuild(ITestDevice device) throws DeviceNotAvailableException {
        String buildType = device.getProperty(BUILD_TYPE_PROPERTY_STRING);
        if (buildType == null) {
            printWarning(
                    String.format("Property %s not found on device", BUILD_TYPE_PROPERTY_STRING));
            return;
        }
        if (!buildType.equalsIgnoreCase(USER_BUILD_STRING)) {
            printWarning(String.format("Expected user build, detected type \"%s\"", buildType));
        }
    }

    /**
     * Throws a TargetSetupError if location services are not enabled by gps or a network
     */
    protected void checkLocationServices(ITestDevice device)
            throws DeviceNotAvailableException, TargetSetupError {

        String shellCmd = String.format("settings get secure %s", LOCATION_PROVIDERS_ALLOWED);
        String locationServices = device.executeShellCommand(shellCmd);
        if (!locationServices.contains("gps") && !locationServices.contains("network")) {
            // location services are not enabled by gps nor by the network
            throw new TargetSetupError(
                    "Location services must be enabled for several CTS test packages");
        }
    }

    /**
     * Throws a TargetSetupError if the device is not connected to a WiFi network. Testers can
     * optionally supply a 'wifi-ssid' and 'wifi-psk' (in the options above) to attempt connection
     * to a specific network.
     */
    protected void runWifiPrecondition(ITestDevice device)
            throws TargetSetupError, DeviceNotAvailableException {
        if (mSkipWifiCheck) {
            return; // skip this precondition
        }
        if (mWifiSsid == null) {
            // no connection to create, check for existing connectivity
            if (!device.checkConnectivity()) {
                printWarning("Device has no network connection, option --wifi-ssid was not " +
                        "provided. Some CTS tests require an active network connection to pass");
            }
        } else {
            // network provided in options, attempt to create new connection if needed
            if (!device.connectToWifiNetworkIfNeeded(mWifiSsid, mWifiPsk)) {
                printWarning("Unable to establish network connection, some CTS tests require " +
                        "an active network connection to pass");
            }
        }
    }

    /**
     * Checks that media files for the mediastress tests are present on the device, and if not,
     * pushes them onto the device.
     */
    protected void runMediaPrecondition(ITestDevice device)
            throws TargetSetupError, DeviceNotAvailableException {
        if (mSkipMediaDownload) {
            return; // skip this precondition
        }
        Dimension mvpr = getMaxVideoPlaybackResolution(device);
        if (mediaFilesExistOnDevice(device, mvpr)) {
            return; // media files already found on the device
        }
        if (mLocalMediaPath == null) {
            createLocalMediaPath(); // make new path on host containing media files
        }
        printInfo(String.format("Media files located on host at: %s", mLocalMediaPath));
        copyMediaFiles(device, mvpr);
    }

    public void setUp(ITestDevice device, IBuildInfo buildInfo)
            throws TargetSetupError, BuildError, DeviceNotAvailableException {
        if (mSkipPreconditions) {
            return; // skipping host-side preconditions
        }

        /* run each host-side precondition */
        enableStayAwakeSetting(device);
        disableAdbAppVerification(device);
        disableKeyguard(device);
        verifyLocale(device);
        verifyUserBuild(device);

        if (hasFeature(device, LOCATION_NETWORK_FEATURE) ||
                hasFeature(device, LOCATION_GPS_FEATURE)) {
            checkLocationServices(device); // only check location services if supported
        }

        if (hasFeature(device, WIFI_FEATURE)) {
            runWifiPrecondition(device); // check wifi precondition only if device supports wifi
        }

        runMediaPrecondition(device);
    }

}
