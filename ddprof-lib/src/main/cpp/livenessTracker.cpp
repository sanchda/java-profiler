/*
* Copyright 2021, 2023 Datadog, Inc
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

#include <set>

#include <jni.h>
#include <string.h>
#include "arch.h"
#include "context.h"
#include "incbin.h"
#include "livenessTracker.h"
#include "os.h"
#include "profiler.h"
#include "log.h"
#include "thread.h"
#include "tsc.h"
#include "vmStructs.h"
#include "jniHelper.h"

LivenessTracker* const LivenessTracker::_instance = new LivenessTracker();

void LivenessTracker::cleanup_table() {
    u64 current = loadAcquire(_last_gc_epoch);
    u64 target_gc_epoch = loadAcquire(_gc_epoch);

    if (target_gc_epoch == _last_gc_epoch || !__sync_bool_compare_and_swap(&_last_gc_epoch, current, target_gc_epoch)) {
        // if the last processed GC epoch hasn't changed, or if we failed to update it, there's nothing to do
        return;
    }

    JNIEnv* env = VM::jni();
    jvmtiEnv* jvmti = VM::jvmti();

    u64 start = OS::nanotime(), end;

    _table_lock.lock();
    int epoch_diff = (int) (target_gc_epoch - current);
    u32 sz, newsz = 0;
    std::set<jclass> kept_classes;
    for (u32 i = 0; i < (sz = _table_size); i++) {
        if (!env->IsSameObject(_table[i].ref, NULL)) {
            // it survived one more GarbageCollectionFinish event
            _table[i].age += epoch_diff;
            _table[newsz++] = _table[i];
            _table[i].ref = NULL;
        } else {
            env->DeleteWeakGlobalRef(_table[i].ref);
            _table[i].ref = NULL;
            delete[] _table[i].frames;
        }
    }

    _table_size = newsz;

    _table_lock.unlock();
    end = OS::nanotime();
    Log::debug("Liveness tracker cleanup took %.2fms (%.2fus/element)",
                1.0f * (end - start) / 1000 / 1000, 1.0f * (end - start) / 1000 / sz);
}

void LivenessTracker::flush(std::set<int> &tracked_thread_ids) {
    flush_table(&tracked_thread_ids);
}

void LivenessTracker::flush_table(std::set<int> *tracked_thread_ids) {
    JNIEnv *env = VM::jni();
    u64 start = OS::nanotime(), end;

    // make sure that the tracking table is cleaned up before we start flushing it
    // this is to make sure we are including as few false 'live' objects as possible
    cleanup_table();

    _table_lock.lockShared();

    u32 sz;
    for (int i = 0; i < (sz = _table_size); i++) {
        jobject ref = env->NewLocalRef(_table[i].ref);
        if (ref != NULL) {
            if (tracked_thread_ids != NULL) {
                tracked_thread_ids->insert(_table[i].tid);
            }
            ObjectLivenessEvent event;
            event._start_time = _table[i].time;
            event._age = _table[i].age;
            event._alloc = _table[i].alloc;
            event._ctx = _table[i].ctx;

            jstring name_str = (jstring)env->CallObjectMethod(env->GetObjectClass(ref), _Class_getName);
            jniExceptionCheck(env);
            const char *name = env->GetStringUTFChars(name_str, NULL);
            event._id = name != NULL ? Profiler::instance()->lookupClass(name, strlen(name)) : 0;
            env->ReleaseStringUTFChars(name_str, name);

            Profiler::instance()->recordExternalSample(1, _table[i].tid, _table[i].frames, _table[i].frames_size, /*truncated=*/false, BCI_LIVENESS, &event);
        }

        env->DeleteLocalRef(ref);
    }

    _table_lock.unlockShared();

    if (_record_heap_usage) {
        bool isLastGc = HeapUsage::isLastGCUsageSupported();
        size_t used = isLastGc ? HeapUsage::get()._used_at_last_gc : loadAcquire(_used_after_last_gc);
        if (used == 0) {
            used = HeapUsage::get()._used;
            isLastGc = false;
        }
        Profiler::instance()->writeHeapUsage(used, isLastGc);
    }

    end = OS::nanotime();
    Log::debug("Liveness tracker flush took %.2fms (%.2fus/element)",
                1.0f * (end - start) / 1000 / 1000, 1.0f * (end - start) / 1000 / sz);
}

Error LivenessTracker::initialize_table(int sampling_interval) {
    _table_max_cap = 0;
    jlong max_heap = HeapUsage::get()._maxSize;;
    if (max_heap == -1) {
        JNIEnv *env = VM::jni();
        static jclass _rt;
        static jmethodID _get_rt;
        static jmethodID _max_memory;

        if (!(_rt = env->FindClass("java/lang/Runtime"))) {
            env->ExceptionDescribe();
        } else if (!(_get_rt = env->GetStaticMethodID(_rt, "getRuntime", "()Ljava/lang/Runtime;"))) {
            env->ExceptionDescribe();
        } else if (!(_max_memory = env->GetMethodID(_rt, "maxMemory", "()J"))) {
            env->ExceptionDescribe();
        } else {
            jobject rt = (jobject)env->CallStaticObjectMethod(_rt, _get_rt);
            jniExceptionCheck(env);
            max_heap = (jlong)env->CallLongMethod(rt, _max_memory);
        }
    }
    if (max_heap == -1) {
        return Error("Can not track liveness for allocation samples without heap size information.");
    }

    int required_table_capacity = sampling_interval > 0 ? max_heap / sampling_interval : max_heap;
    
    if (required_table_capacity > MAX_TRACKING_TABLE_SIZE) {
        Log::warn("Tracking liveness for allocation samples with interval %d can not cover full heap.", sampling_interval);
    }
    _table_max_cap = __min(MAX_TRACKING_TABLE_SIZE, required_table_capacity);

    _table_cap = std::max(2048, _table_max_cap / 8); // the table will grow at most 3 times before fully covering heap
    return Error::OK;
}

Error LivenessTracker::start(Arguments& args) {
    Error err = initialize(args);
    if (err) { return err; }
    // Enable Java Object Sample events
    jvmtiEnv* jvmti = VM::jvmti();
    jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_GARBAGE_COLLECTION_FINISH, NULL);

    return Error::OK;
}

void LivenessTracker::stop() {
    cleanup_table();
    flush_table(NULL);

    // do not disable GC notifications here - the tracker is supposed to survive multiple recordings
}

static int _min(int a, int b) { return a < b ? a : b; }

Error LivenessTracker::initialize(Arguments& args) {
    if (_initialized) {
        // if the tracker was previously initialized return the stored result for consistency
        // this hack also means that if the profiler is started with different arguments for liveness tracking those will be ignored
        // it is required in order to be able to track the object liveness across many recordings
        return _stored_error;
    }
    _initialized = true;

    if (VM::java_version() < 11) {
        Log::warn("Liveness tracking requires Java 11+");
        // disable liveness tracking
        _table_max_cap = 0;
        return _stored_error = Error::OK;
    }

    JNIEnv* env = VM::jni();

    Error err = initialize_table(args._memory);
    if (err) {
        Log::warn("Liveness tracking requires heap size information");
        // disable liveness tracking
        _table_max_cap = 0;
        return _stored_error = Error::OK;
    }
    if (!(_Class = env->FindClass("java/lang/Class"))) {
        env->ExceptionDescribe();
        err = Error("Unable to find java/lang/Class");
    } else if (!(_Class_getName = env->GetMethodID(_Class, "getName", "()Ljava/lang/String;"))) {
        env->ExceptionDescribe();
        err = Error("Unable to find java/lang/Class.getName");
    }
    if (err) {
        Log::warn("Liveness tracking requires access to java.lang.Class#getName()");
        // disable liveness tracking
        _table_max_cap = 0;
        return _stored_error = Error::OK;
    }

    _table_size = 0;
    _table_cap = __min(2048, _table_max_cap); // with default 512k sampling interval, it's enough for 1G of heap
    _table = (TrackingEntry*)malloc(sizeof(TrackingEntry) * _table_cap);

    _record_heap_usage = args._record_heap_usage;

    _gc_epoch = 0;
    _last_gc_epoch = 0;

    env->ExceptionClear();

    return _stored_error = Error::OK;
}

void LivenessTracker::track(JNIEnv* env, AllocEvent &event, jint tid, jobject object, int num_frames, jvmtiFrameInfo* frames) {
    if (_table_max_cap == 0) {
        // we are not to store any objects
        return;
    }

    jweak ref = env->NewWeakGlobalRef(object);
    if (ref == NULL) {
        return;
    }

    bool retried = false;

retry:
    if (!_table_lock.tryLockShared()) {
        return;
    }

    // Increment _table_size in a thread-safe manner (CAS) and store the new value in idx
    // It bails out if _table_size would overflow _table_cap
    int idx;
    do {
        idx = _table_size;
    } while (idx < _table_cap &&
                !__sync_bool_compare_and_swap(&_table_size, idx, idx + 1));

    if (idx < _table_cap) {
        _table[idx].tid = tid;
        _table[idx].time = TSC::ticks();
        _table[idx].ref = ref;
        _table[idx].alloc = event;
        _table[idx].age = 0;
        _table[idx].frames_size = num_frames;
        _table[idx].frames = new jvmtiFrameInfo[_table[idx].frames_size];
        memcpy(_table[idx].frames, frames, sizeof(jvmtiFrameInfo) * _table[idx].frames_size);
        _table[idx].ctx = Contexts::get(tid);
    }

    _table_lock.unlockShared();

    if (idx == _table_cap) {
        if (!retried) {
            // guarantees we don't busy loop until memory exhaustion
            retried = true;

            // try cleanup before resizing - there is a good chance it will free some space
            cleanup_table();

            if (_table_cap < _table_max_cap) {

                // Let's increase the size of the table
                // This should only ever happen when sampling interval * size of table
                // is smaller than maximum heap size. So we only support increasing
                // the size of the table, not decreasing it.
                _table_lock.lock();

                // Only increase the size of the table to _table_max_cap elements
                int newcap = __min(_table_cap * 2, _table_max_cap);
                if (_table_cap != newcap) {
                    TrackingEntry* tmp = (TrackingEntry*)realloc(_table, sizeof(TrackingEntry) * (_table_cap = newcap));
                    if (tmp != NULL) {
                        _table = tmp;
                        Log::debug("Increased size of Liveness tracking table to %d entries", _table_cap);
                    } else {
                        Log::debug("Cannot add sampled object to Liveness tracking table, resize attempt failed, the table is overflowing");
                    }
                }

                _table_lock.unlock();

                goto retry;
            } else {
                Log::debug("Cannot add sampled object to Liveness tracking table, it's overflowing");
            }
        }
    }

    delete[] frames;
}

void JNICALL LivenessTracker::GarbageCollectionFinish(jvmtiEnv *jvmti_env) {
    LivenessTracker::instance()->onGC();
}

void LivenessTracker::onGC() {
    if (!_initialized) {
        return;
    }

    // just increment the epoch
   atomicInc(_gc_epoch, 1);

   if (!HeapUsage::isLastGCUsageSupported()) {
       storeRelease(_used_after_last_gc, HeapUsage::get(false)._used);
   }
}
