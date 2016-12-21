/*
// Copyright (c) 2016 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

#include "hwcthread.h"

#include <signal.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <time.h>

#include "hwctrace.h"

namespace hwcomposer {

HWCThread::HWCThread(int priority) : priority_(priority), initialized_(false) {
}

HWCThread::~HWCThread() {
  if (!initialized_)
    return;

  pthread_kill(thread_, SIGTERM);
  pthread_cond_destroy(&cond_);
  pthread_mutex_destroy(&lock_);
}

bool HWCThread::InitWorker(const char *name) {
  pthread_condattr_t cond_attr;
  pthread_condattr_init(&cond_attr);
  pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC);
  int ret = pthread_cond_init(&cond_, &cond_attr);
  if (ret) {
    ETRACE("Failed to int thread %s condition %d", name, ret);
    return false;
  }

  ret = pthread_mutex_init(&lock_, NULL);
  if (ret) {
    ETRACE("Failed to init thread %s lock %d", name, ret);
    pthread_cond_destroy(&cond_);
    return false;
  }

  ret = pthread_create(&thread_, NULL, InternalRoutine, this);
  if (ret) {
    ETRACE("Could not create thread %s %d", name, ret);
    pthread_mutex_destroy(&lock_);
    pthread_cond_destroy(&cond_);
    return false;
  }
  initialized_ = true;
  return true;
}

int HWCThread::Lock() {
  return pthread_mutex_lock(&lock_);
}

int HWCThread::Unlock() {
  return pthread_mutex_unlock(&lock_);
}

// static
void *HWCThread::InternalRoutine(void *arg) {
  HWCThread *thread = (HWCThread *)arg;

  setpriority(PRIO_PROCESS, 0, thread->priority_);

  while (true) {
    thread->Routine();
  }
  return NULL;
}
}
