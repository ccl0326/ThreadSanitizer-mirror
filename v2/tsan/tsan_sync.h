//===-- tsan_sync.h ---------------------------------------------*- C++ -*-===//
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
//===----------------------------------------------------------------------===//
#ifndef TSAN_SYNC_H
#define TSAN_SYNC_H

#include "tsan_atomic.h"
#include "tsan_clock.h"
#include "tsan_defs.h"
#include "tsan_mutex.h"

namespace __tsan {

class SlabCache;

class StackTrace {
 public:
  StackTrace();
  ~StackTrace();

  void Init(ThreadState *thr, uptr *pcs, uptr cnt);
  void ObtainCurrent(ThreadState *thr, uptr toppc);
  void Free(ThreadState *thr);
  bool IsEmpty() const;
  uptr Size() const;
  uptr Get(uptr i) const;
  const uptr *Begin() const;

 private:
  uptr n_;
  uptr *s_;

  StackTrace(const StackTrace&);
  void operator = (const StackTrace&);
};

struct SyncVar {
  explicit SyncVar(uptr addr);

  static const unsigned kInvalidTid = -1;

  Mutex mtx;
  const uptr addr;
  SyncClock clock;
  StackTrace creation_stack;
  SyncClock read_clock;  // Used for rw mutexes only.
  unsigned owner_tid;  // Set only by exclusive owners.
  int recursion;
  bool is_rw;
  bool is_recursive;
  bool is_broken;
  SyncVar *next;  // In SyncTab hashtable.
};

class SyncTab {
 public:
  SyncTab();

  // If the SyncVar does not exist yet, it is created.
  SyncVar* GetAndLock(ThreadState *thr, uptr pc,
                      SlabCache *slab, uptr addr, bool write_lock);

  // If the SyncVar does not exist, returns 0.
  SyncVar* GetAndRemove(uptr addr);

 private:
  struct Part {
    Mutex mtx;
    SyncVar *val;
    char pad[kCacheLineSize - sizeof(Mutex) - sizeof(SyncVar*)];  // NOLINT
    Part();
  };

  // FIXME: Implement something more sane.
  static const int kPartCount = 1009;
  Part tab_[kPartCount];

  int PartIdx(uptr addr);

  SyncTab(const SyncTab&);  // Not implemented.
  void operator = (const SyncTab&);  // Not implemented.
};

}  // namespace __tsan

#endif  // TSAN_SYNC_H
