/*
 * Copyright 2017 Andrei Pangin
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

#ifdef __APPLE__

#include "perfEvents.h"

volatile bool PerfEvents::_enabled = false;
int PerfEvents::_max_events;
PerfEvent* PerfEvents::_events;
PerfEventType* PerfEvents::_event_type;
long PerfEvents::_interval;
Ring PerfEvents::_ring;
CStack PerfEvents::_cstack;
bool PerfEvents::_use_mmap_page;

u64 PerfEvents::readCounter(siginfo_t* siginfo, void* ucontext) {
    return 0;
}

void PerfEvents::signalHandler(int signo, siginfo_t* siginfo, void* ucontext) {
}


Error PerfEvents::check(Arguments& args) {
    return Error("PerfEvents are unsupported on macOS");
}

Error PerfEvents::start(Arguments& args) {
    return Error("PerfEvents are unsupported on macOS");
}

void PerfEvents::stop() {
}

int PerfEvents::walkKernel(int tid, const void** callchain, int max_depth, StackContext *java_ctx) {
    return 0;
}

void PerfEvents::resetBuffer(int tid) {
}

const char* PerfEvents::getEventName(int event_id) {
    return NULL;
}

int PerfEvents::registerThread(int tid) {
    return -1;
}

void PerfEvents::unregisterThread(int tid) {
}

#endif // __APPLE__
