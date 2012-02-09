//===-- tsan_test_util_linux.cc ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
// Test utils, linux implementation.
//===----------------------------------------------------------------------===//

#include "tsan_interface.h"
#include "tsan_test_util.h"
#include "tsan_atomic.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

using __tsan::memory_order_relaxed;
using __tsan::memory_order_consume;
using __tsan::memory_order_acquire;
using __tsan::memory_order_release;
using __tsan::memory_order_acq_rel;
using __tsan::memory_order_seq_cst;
using __tsan::atomic_uintptr_t;
using __tsan::atomic_load;
using __tsan::atomic_store;
using __tsan::atomic_fetch_add;

// A lock which is not observed by the race detector.
class HiddenLock {
 public:
  HiddenLock() : lock_(0) { }
  ~HiddenLock() { CHECK_EQ(lock_, 0); }
  void Lock() {
    while (__sync_val_compare_and_swap(&lock_, 0, 1) != 0)
      usleep(0);
    CHECK_EQ(lock_, 1);
  }
  void Unlock() {
    CHECK_EQ(lock_, 1);
    int res =__sync_val_compare_and_swap(&lock_, 1, 0);
    CHECK_EQ(res, 1);
    (void)res;
  }
 private:
  int lock_;
};

class ScopedHiddenLock {
 public:
  explicit ScopedHiddenLock(HiddenLock *lock) : lock_(lock) {
    lock_->Lock();
  }
  ~ScopedHiddenLock() { lock_->Unlock(); }
 private:
  HiddenLock *lock_;
};

static void* allocate_addr(int offset_from_aligned = 0) {
  static uintptr_t foo;
  static atomic_uintptr_t uniq = {(uintptr_t)&foo};  // Some real address.
  return (void*)(atomic_fetch_add(&uniq, 32, memory_order_relaxed)
      + offset_from_aligned);
}

MemLoc::MemLoc(int offset_from_aligned)
  : loc_(allocate_addr(offset_from_aligned)) {
}

MemLoc::~MemLoc() { }

Mutex::Mutex()
  : addr(NULL) {
}

Mutex::~Mutex() {
  CHECK_EQ(addr, NULL);
}

struct Event {
  enum Type {
    SHUTDOWN,
    READ,
    WRITE,
    CALL,
    RETURN,
    MUTEX_CREATE,
    MUTEX_DESTROY,
    MUTEX_LOCK,
    MUTEX_UNLOCK,
  };
  Type type;
  void *ptr;
  int arg;

  Event(Type type, void *ptr = NULL, int arg = 0)
    : type(type)
    , ptr(ptr)
    , arg(arg) {
  }
};

struct ScopedThread::Impl {
  pthread_t thread;
  atomic_uintptr_t event;  // Event*
  int tid;

  static void *ScopedThreadCallback(void *arg);
  void send(Event *ev);
};

void *ScopedThread::Impl::ScopedThreadCallback(void *arg) {
  Impl *impl = (Impl*)arg;
  __tsan_thread_start(impl->tid);
  for (;;) {
    Event* ev = (Event*)atomic_load(&impl->event, memory_order_acquire);
    if (ev == NULL) {
      pthread_yield();
      continue;
    }
    if (ev->type == Event::SHUTDOWN) {
      atomic_store(&impl->event, 0, memory_order_release);
      break;
    }
    switch (ev->type) {
    case Event::READ:
      switch (ev->arg /*size*/) {
        case 1: __tsan_read1(ev->ptr); break;
        case 2: __tsan_read2(ev->ptr); break;
        case 4: __tsan_read4(ev->ptr); break;
        case 8: __tsan_read8(ev->ptr); break;
        case 16: __tsan_read16(ev->ptr); break;
      }
      break;
    case Event::WRITE:
      switch (ev->arg /*size*/) {
        case 1: __tsan_write1(ev->ptr); break;
        case 2: __tsan_write2(ev->ptr); break;
        case 4: __tsan_write4(ev->ptr); break;
        case 8: __tsan_write8(ev->ptr); break;
        case 16: __tsan_write16(ev->ptr); break;
      }
      break;
    case Event::CALL:
      __tsan_func_entry(ev->ptr);
      break;
    case Event::RETURN:
      __tsan_func_exit();
      break;
    case Event::MUTEX_CREATE:
      __tsan_mutex_create(ev->ptr, 0);
      break;
    case Event::MUTEX_DESTROY:
      __tsan_mutex_destroy(ev->ptr);
      break;
    case Event::MUTEX_LOCK:
      __tsan_mutex_lock(ev->ptr);
      break;
    case Event::MUTEX_UNLOCK:
      __tsan_mutex_unlock(ev->ptr);
      break;
    default: CHECK(0);
    }
    atomic_store(&impl->event, 0, memory_order_release);
  }
  return NULL;
}

void ScopedThread::Impl::send(Event *e) {
  CHECK_EQ(atomic_load(&event, memory_order_relaxed), 0);
  atomic_store(&event, (uintptr_t)e, memory_order_release);
  while (atomic_load(&event, memory_order_acquire) != 0)
    pthread_yield();
}

ScopedThread::ScopedThread() {
  impl_ = new Impl;
  impl_->tid = __tsan_thread_create();
  atomic_store(&impl_->event, 0, memory_order_relaxed);
  pthread_create(&impl_->thread, NULL,
      ScopedThread::Impl::ScopedThreadCallback, impl_);
}

ScopedThread::~ScopedThread() {
  Event event(Event::SHUTDOWN);
  impl_->send(&event);
  pthread_join(impl_->thread, NULL);
  delete impl_;
}

void ScopedThread::Access(const MemLoc &ml, bool is_write,
                          int size, bool expect_race) {
  (void)expect_race;
  Event event(is_write ? Event::WRITE : Event::READ, ml.loc(), size);
  impl_->send(&event);
}

void ScopedThread::Call(void(*pc)()) {
  Event event(Event::CALL, (void*)pc);
  impl_->send(&event);
}

void ScopedThread::Return() {
  Event event(Event::RETURN);
  impl_->send(&event);
}

void ScopedThread::Create(const Mutex &m) {
  CHECK_EQ(m.addr, NULL);
  m.addr = allocate_addr();
  Event event(Event::MUTEX_CREATE, m.addr);
  impl_->send(&event);
}

void ScopedThread::Destroy(const Mutex &m) {
  CHECK_NE(m.addr, NULL);
  Event event(Event::MUTEX_DESTROY, m.addr);
  impl_->send(&event);
  m.addr = NULL;
}

void ScopedThread::Lock(const Mutex &m) {
  CHECK_NE(m.addr, NULL);
  Event event(Event::MUTEX_LOCK, m.addr);
  impl_->send(&event);
}

void ScopedThread::Unlock(const Mutex &m) {
  CHECK_NE(m.addr, NULL);
  Event event(Event::MUTEX_UNLOCK, m.addr);
  impl_->send(&event);
}