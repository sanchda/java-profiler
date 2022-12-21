package com.datadoghq.loader;

import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.concurrent.ThreadLocalRandom;
import one.profiler.JavaProfiler;

public class DynamicLibraryLoader {

    static {
        System.loadLibrary("loader");
    }

    public static void main(String... args) throws Exception {
        DynamicLibraryLoader loader = new DynamicLibraryLoader();
        String jfrDump = args[0];
        for (int i = 1; i < args.length; i++) {
            String[] split = args[i].split(":");
            if (!loader.loadLibrary(split[0], split[1])) {
                System.err.println("Could not load " + split[0] + " and invoke " + split[1]);
                System.exit(1);
            }
        }
        startProfilerAndDoWork(jfrDump);
    }

    private static void startProfilerAndDoWork(String jfrFile) throws Exception {
        JavaProfiler ap = JavaProfiler.getInstance(getJavaProfilerLib());
        Path jfrDump = Paths.get(jfrFile);
        ap.execute("start,loglevel=TRACE,cpu=1m,wall=1ms,filter=0,jfr,file=" + jfrDump.toAbsolutePath());
        ap.addThread(ap.getNativeThreadId());
        work();
        ap.stop();
    }

    private static void work() throws Exception {
        Thread.sleep(10);
        long blackhole = System.nanoTime();
        for (int i = 0; i < 10_000_000; i++) {
            blackhole ^= ThreadLocalRandom.current().nextLong();
        }
        Thread.sleep(10);
        System.err.println(blackhole);
    }


    private native boolean loadLibrary(String libraryFile, String functionName);


    private static String getJavaProfilerLib() {
        try {
            File root = new File(DynamicLibraryLoader.class
                .getResource("DynamicLibraryLoader.class").toURI()).getParentFile();
            // TODO make the exit condition not to depend on the project root folder name
            while (!root.getName().startsWith("java-profiler")) {
                root = root.getParentFile();
            }
            return root.toPath().resolve("build/libjavaProfiler.so").toAbsolutePath().toString();
        } catch (Throwable t) {
             throw new RuntimeException("Could not find javaProfiler lib", t);
        }
    }

}
