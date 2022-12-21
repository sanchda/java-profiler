package com.datadoghq.profiler.wallclock;

import one.profiler.JavaProfiler;

import java.util.concurrent.*;

class ContextExecutor extends ThreadPoolExecutor {

    private final JavaProfiler profiler;
    public ContextExecutor(int corePoolSize, JavaProfiler profiler) {
        super(corePoolSize, corePoolSize, 30, TimeUnit.SECONDS,
                new ArrayBlockingQueue<>(128), new RegisteringThreadFactory(profiler));
        this.profiler = profiler;
    }

    @Override
    protected <T> RunnableFuture<T> newTaskFor(Runnable runnable, T value) {
        return ContextTask.wrap(runnable, value);
    }

    @Override
    protected void beforeExecute(Thread thread, Runnable runnable) {
        profiler.setPoolParallelism(profiler.getNativeThreadId(), getPoolSize());
        super.beforeExecute(thread, runnable);
    }
}
