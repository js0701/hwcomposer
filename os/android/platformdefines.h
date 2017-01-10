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

#ifndef PLATFORM_DEFINES_
#define PLATFORM_DEFINES_

#include <cutils/log.h>
#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>

#ifdef _cplusplus
extern "C" {
#endif

typedef buffer_handle_t HWCNativeHandle;

#define ILOG ALOGI
#define DLOG ALOGD
#define VLOG VLOG
#define WLOG ALOGW
#define ELOG ALOGE

// _cplusplus
#ifdef _cplusplus
}
#endif

#endif  // PLATFORM_DEFINES_
