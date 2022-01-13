/*
 * Copyright 2021 Andrei Pangin
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package one.profiler;

import jdk.jfr.Configuration;
import jdk.jfr.FlightRecorder;
import jdk.jfr.FlightRecorderListener;
import jdk.jfr.Recording;
import jdk.jfr.RecordingState;

import java.io.IOException;
import java.nio.file.NoSuchFileException;
import java.nio.file.Paths;
import java.text.ParseException;

/**
 * Synchronize async-profiler recording with an existing JFR recording.
 */
class JfrSync implements FlightRecorderListener {
    private static volatile Recording masterRecording;

    private JfrSync() {
    }

    static {
        FlightRecorder.addListener(new JfrSync());
    }

    @Override
    public void recordingStateChanged(Recording recording) {
        if (recording == masterRecording && recording.getState() == RecordingState.STOPPED) {
            masterRecording = null;
            stopProfiler();
        }
    }

    public static void start(String fileName, String settings, int eventMask) throws IOException, ParseException {
        Configuration config;
        try {
            config = Configuration.getConfiguration(settings);
        } catch (NoSuchFileException e) {
            config = Configuration.create(Paths.get(settings));
        }

        Recording recording = new Recording(config);
        masterRecording = recording;

        disableBuiltinEvents(recording, eventMask);

        recording.setDestination(Paths.get(fileName));
        recording.setToDisk(true);
        recording.setDumpOnExit(true);
        recording.start();
    }

    public static void stop() {
        Recording recording = masterRecording;
        if (recording != null) {
            // Disable state change notification before stopping
            masterRecording = null;
            recording.stop();
        }
    }

    final static int EM_CPU   = 1;
    final static int EM_ALLOC = 2;
    final static int EM_LOCK  = 4;
    final static int EM_MEMLEAK = 8;

    private static void disableBuiltinEvents(Recording recording, int eventMask) {
        if ((eventMask & EM_CPU) != 0) {
            recording.disable("jdk.ExecutionSample");
            recording.disable("jdk.NativeMethodSample");
        }
        if ((eventMask & EM_ALLOC) != 0) {
            recording.disable("jdk.ObjectAllocationInNewTLAB");
            recording.disable("jdk.ObjectAllocationOutsideTLAB");
            recording.disable("jdk.ObjectAllocationSample");
        }
        if ((eventMask & EM_LOCK) != 0) {
            recording.disable("jdk.JavaMonitorEnter");
            recording.disable("jdk.ThreadPark");
        }
        if ((eventMask & EM_MEMLEAK) != 0) {
            recording.disable("jdk.OldObjectSample");
        }
    }

    private static native void stopProfiler();

    // JNI helper
    static Integer box(int n) {
        return n;
    }
}
