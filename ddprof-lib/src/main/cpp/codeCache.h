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

#ifndef _CODECACHE_H
#define _CODECACHE_H

#include <jvmti.h>
#include <string.h>
#include <vector>


#define NO_MIN_ADDRESS  ((const void*)-1)
#define NO_MAX_ADDRESS  ((const void*)0)

typedef bool (*NamePredicate)(const char* name);

const int INITIAL_CODE_CACHE_CAPACITY = 1000;
const int MAX_NATIVE_LIBS = 2048;


class NativeFunc {
  private:
    short _lib_index;
    char _mark;
    char _reserved;
    char _name[0];

    static NativeFunc* from(const char* name) {
        return (NativeFunc*)(name - sizeof(NativeFunc));
    }

  public:
    static char* create(const char* name, short lib_index);
    static void destroy(char* name);

    static short libIndex(const char* name) {
        return from(name)->_lib_index;
    }

    static bool isMarked(const char* name) {
        return from(name)->_mark != 0;
    }

    static void mark(const char* name) {
        from(name)->_mark = 1;
    }
};


class CodeBlob {
  public:
    const void* _start;
    const void* _end;
    char* _name;

    static int comparator(const void* c1, const void* c2) {
        CodeBlob* cb1 = (CodeBlob*)c1;
        CodeBlob* cb2 = (CodeBlob*)c2;
        if (cb1->_start < cb2->_start) {
            return -1;
        } else if (cb1->_start > cb2->_start) {
            return 1;
        } else if (cb1->_end == cb2->_end) {
            return 0;
        } else {
            return cb1->_end > cb2->_end ? -1 : 1;
        }
    }
};


class FrameDesc;

class CodeCache {
  protected:
    char* _name;
    short _lib_index;
    const void* _min_address;
    const void* _max_address;
    const char* _text_base;

    void** _got_start;
    void** _got_end;
    bool _got_patchable;

    FrameDesc* _dwarf_table;
    int _dwarf_table_length;

    int _capacity;
    int _count;
    CodeBlob* _blobs;

    void expand();
    void makeGotPatchable();

  public:
    explicit CodeCache(const char* name,
              short lib_index = -1,
              const void* min_address = NO_MIN_ADDRESS,
              const void* max_address = NO_MAX_ADDRESS);
    // Copy constructor
    CodeCache(const CodeCache& other);
    // Copy assignment operator
    CodeCache& operator=(const CodeCache& other);

    ~CodeCache();

    void dump();

    const char* name() const {
        return _name;
    }

    const void* minAddress() const {
        return _min_address;
    }

    const void* maxAddress() const {
        return _max_address;
    }

    bool contains(const void* address) const {
        return address >= _min_address && address < _max_address;
    }

    void setTextBase(const char* text_base) {
        _text_base = text_base;
    }

    void add(const void* start, int length, const char* name, bool update_bounds = false);
    void updateBounds(const void* start, const void* end);
    void sort();
    void mark(NamePredicate predicate);

    CodeBlob* find(const void* address);
    const char* binarySearch(const void* address);
    const void* findSymbol(const char* name);
    const void* findSymbolByPrefix(const char* prefix);
    const void* findSymbolByPrefix(const char* prefix, int prefix_len);
    void findSymbolsByPrefix(std::vector<const char*>& prefixes, std::vector<const void*>& symbols);

    void setGlobalOffsetTable(void** start, void** end, bool patchable);
    void** findGlobalOffsetEntry(void* address);

    void setDwarfTable(FrameDesc* table, int length);
    FrameDesc* findFrameDesc(const void* pc);

    long long memoryUsage() {
        return _capacity * sizeof(CodeBlob*) + _count * sizeof(NativeFunc);
    }
};


class CodeCacheArray {
  private:
    CodeCache* _libs[MAX_NATIVE_LIBS];
    int _count;

  public:
    CodeCacheArray() : _count(0) {
        memset(_libs, 0, MAX_NATIVE_LIBS * sizeof(CodeCache*));
    }

    CodeCache* operator[](int index) {
        return _libs[index];
    }

    int count() {
        return __atomic_load_n(&_count, __ATOMIC_ACQUIRE);
    }

    void add(CodeCache* lib) {
        int index = __atomic_load_n(&_count, __ATOMIC_ACQUIRE);
        _libs[index] = lib;
        __atomic_store_n(&_count, index + 1, __ATOMIC_RELEASE);
    }

    long long memoryUsage() {
        int count = __atomic_load_n(&_count, __ATOMIC_ACQUIRE);
        long long totalUsage = 0;
        for (int i = 0; i < count; i++) {
            totalUsage += _libs[i]->memoryUsage();
        }
        return totalUsage;
    }
};

#endif // _CODECACHE_H
