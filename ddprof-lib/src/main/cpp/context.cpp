/*
 * Copyright 2021, 2022 Datadog, Inc
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

#include <assert.h>
#include <cstring>
#include "context.h"
#include "counters.h"
#include "os.h"

int Contexts::_max_pages = Contexts::getMaxPages();
Context** Contexts::_pages = new Context *[_max_pages]();

static Context DD_EMPTY_CONTEXT = {};

Context& Contexts::get(int tid) {
    int pageIndex = tid >> DD_CONTEXT_PAGE_SHIFT;
    assert(pageIndex < _max_pages);
    Context* page = _pages[pageIndex];
    if (page != NULL) {
        Context& context = page[tid & DD_CONTEXT_PAGE_MASK];
        if ((context.spanId ^ context.rootSpanId) == context.checksum) {
            return context;
        }
    }
    return empty();
}

Context& Contexts::empty() {
    return DD_EMPTY_CONTEXT;
}

void Contexts::initialize(int pageIndex) {
    if (__atomic_load_n(&_pages[pageIndex], __ATOMIC_ACQUIRE) == NULL) {
        u32 capacity = DD_CONTEXT_PAGE_SIZE * sizeof(Context);
        Context *page = (Context*) aligned_alloc(sizeof(Context), capacity);
        // need to zero the storage because there is no aligned_calloc
        memset(page, 0, capacity);
        if (!__sync_bool_compare_and_swap(&_pages[pageIndex], NULL, page)) {
            free(page);
        } else {
            Counters::increment(Counters::CONTEXT_STORAGE_BYTES, capacity);
            Counters::increment(Counters::CONTEXT_STORAGE_PAGES);
        }
    }
}

ContextPage Contexts::getPage(int tid) {
    int pageIndex = tid >> DD_CONTEXT_PAGE_SHIFT;
    initialize(pageIndex);
    return {.capacity = DD_CONTEXT_PAGE_SIZE * sizeof(Context), .storage = _pages[pageIndex]};
}

// The number of pages that can cover all allowed thread IDs
int Contexts::getMaxPages(int maxTid) {
    // Max thread id is 0-based but exclusive - eg. value of 1024 will mean that max 1024 will be ever
    // present. The following formula will 'round up' the number of pages necessary to hold the given
    // number of threads.

    //! the next sequence of computation and static cast to int needs to be split into two statements
    //  - otherwise the gtest will crash and burn while linking
    long ret = ((long)maxTid + DD_CONTEXT_PAGE_SIZE - 1) / DD_CONTEXT_PAGE_SIZE;
    return (int)ret;
}
