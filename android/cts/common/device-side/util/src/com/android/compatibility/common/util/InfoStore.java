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
package com.android.compatibility.common.util;

import android.util.JsonWriter;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.nio.charset.StandardCharsets;
import java.io.Serializable;
import java.util.Arrays;
import java.util.List;

public class InfoStore {

    private static final int MAX_STRING_LENGTH = 1000;
    private static final int MAX_ARRAY_LENGTH = 1000;
    private static final int MAX_LIST_LENGTH = 1000;

    private final File mJsonFile;
    private JsonWriter mJsonWriter = null;

    public InfoStore(File file) throws Exception {
        mJsonFile = file;
    }

    /**
     * Opens the file for storage and creates the writer.
     */
    public void open() throws IOException {
        FileOutputStream out = new FileOutputStream(mJsonFile);
        mJsonWriter = new JsonWriter(new OutputStreamWriter(out, StandardCharsets.UTF_8));
        // TODO(agathaman): remove to make json output less pretty
        mJsonWriter.setIndent("  ");
        mJsonWriter.beginObject();
    }

    /**
     * Closes the writer.
     */
    public void close() throws IOException {
        mJsonWriter.endObject();
        mJsonWriter.close();
    }

    /**
     * Start a new group of result.
     */
    public void startGroup() throws IOException {
        mJsonWriter.beginObject();
    }

    /**
     * Start a new group of result with specified name.
     */
    public void startGroup(String name) throws IOException {
        mJsonWriter.name(name);
        mJsonWriter.beginObject();
    }

    /**
     * Complete adding result to the last started group.
     */
    public void endGroup() throws IOException {
        mJsonWriter.endObject();
    }

    /**
     * Start a new array of result.
     */
    public void startArray() throws IOException {
        mJsonWriter.beginArray();
    }

    /**
     * Start a new array of result with specified name.
     */
    public void startArray(String name) throws IOException {
        checkName(name);
        mJsonWriter.name(name);
        mJsonWriter.beginArray();
    }

    /**
     * Complete adding result to the last started array.
     */
    public void endArray() throws IOException {
        mJsonWriter.endArray();
    }

    /**
     * Adds a int value to the InfoStore
     */
    public void addResult(String name, int value) throws IOException {
        checkName(name);
        mJsonWriter.name(name);
        mJsonWriter.value(value);
    }

    /**
     * Adds a long value to the InfoStore
     */
    public void addResult(String name, long value) throws IOException {
        checkName(name);
        mJsonWriter.name(name);
        mJsonWriter.value(value);
    }

    /**
     * Adds a float value to the InfoStore
     */
    public void addResult(String name, float value) throws IOException {
        checkName(name);
        mJsonWriter.name(name);
        mJsonWriter.value(value);
    }

    /**
     * Adds a double value to the InfoStore
     */
    public void addResult(String name, double value) throws IOException {
        checkName(name);
        mJsonWriter.name(name);
        mJsonWriter.value(value);
    }

    /**
     * Adds a boolean value to the InfoStore
     */
    public void addResult(String name, boolean value) throws IOException {
        checkName(name);
        mJsonWriter.name(name);
        mJsonWriter.value(value);
    }

    /**
     * Adds a String value to the InfoStore
     */
    public void addResult(String name, String value) throws IOException {
        checkName(name);
        mJsonWriter.name(name);
        mJsonWriter.value(checkString(value));
    }

    /**
     * Adds a int array to the InfoStore
     */
    public void addArrayResult(String name, int[] array) throws IOException {
        checkName(name);
        mJsonWriter.name(name);
        mJsonWriter.beginArray();
        for (int value : checkArray(array)) {
            mJsonWriter.value(value);
        }
        mJsonWriter.endArray();
    }

    /**
     * Adds a long array to the InfoStore
     */
    public void addArrayResult(String name, long[] array) throws IOException {
        checkName(name);
        mJsonWriter.name(name);
        mJsonWriter.beginArray();
        for (long value : checkArray(array)) {
            mJsonWriter.value(value);
        }
        mJsonWriter.endArray();
    }

    /**
     * Adds a float array to the InfoStore
     */
    public void addArrayResult(String name, float[] array) throws IOException {
        checkName(name);
        mJsonWriter.name(name);
        mJsonWriter.beginArray();
        for (float value : checkArray(array)) {
            mJsonWriter.value(value);
        }
        mJsonWriter.endArray();
    }

    /**
     * Adds a double array to the InfoStore
     */
    public void addArrayResult(String name, double[] array) throws IOException {
        checkName(name);
        mJsonWriter.name(name);
        mJsonWriter.beginArray();
        for (double value : checkArray(array)) {
            mJsonWriter.value(value);
        }
        mJsonWriter.endArray();
    }

    /**
     * Adds a boolean array to the InfoStore
     */
    public void addArrayResult(String name, boolean[] array) throws IOException {
        checkName(name);
        mJsonWriter.name(name);
        mJsonWriter.beginArray();
        for (boolean value : checkArray(array)) {
            mJsonWriter.value(value);
        }
        mJsonWriter.endArray();
    }

    /**
     * Adds a List of String to the InfoStore
     */
    public void addListResult(String name, List<String> list) throws IOException {
        checkName(name);
        mJsonWriter.name(name);
        mJsonWriter.beginArray();
        for (String value : checkStringList(list)) {
            mJsonWriter.value(checkString(value));
        }
        mJsonWriter.endArray();
    }

    private static int[] checkArray(int[] values) {
        if (values.length > MAX_ARRAY_LENGTH) {
            return Arrays.copyOf(values, MAX_ARRAY_LENGTH);
        } else {
            return values;
        }
    }

    private static long[] checkArray(long[] values) {
        if (values.length > MAX_ARRAY_LENGTH) {
            return Arrays.copyOf(values, MAX_ARRAY_LENGTH);
        } else {
            return values;
        }
    }

    private static float[] checkArray(float[] values) {
        if (values.length > MAX_ARRAY_LENGTH) {
            return Arrays.copyOf(values, MAX_ARRAY_LENGTH);
        } else {
            return values;
        }
    }

    private static double[] checkArray(double[] values) {
        if (values.length > MAX_ARRAY_LENGTH) {
            return Arrays.copyOf(values, MAX_ARRAY_LENGTH);
        } else {
            return values;
        }
    }

    private static boolean[] checkArray(boolean[] values) {
        if (values.length > MAX_ARRAY_LENGTH) {
            return Arrays.copyOf(values, MAX_ARRAY_LENGTH);
        } else {
            return values;
        }
    }

    private static List<String> checkStringList(List<String> list) {
        if (list.size() > MAX_LIST_LENGTH) {
            return list.subList(0, MAX_LIST_LENGTH);
        }
        return list;
    }

    private static String checkString(String value) {
        if (value == null || value.isEmpty()) {
            return "null";
        }
        if (value.length() > MAX_STRING_LENGTH) {
            return value.substring(0, MAX_STRING_LENGTH);
        }
        return value;
    }

    private static String checkName(String value) {
        if (value == null || value.isEmpty()) {
            throw new NullPointerException();
        }
        return value;
    }
}
