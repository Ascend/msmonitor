/*
 * -------------------------------------------------------------------------
 * This file is part of the MindStudio project.
 * Copyright (c) 2026 Huawei Technologies Co.,Ltd.
 *
 * MindStudio is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *
 *          http://license.coscl.org.cn/MulanPSL2
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * -------------------------------------------------------------------------
 */

#include "mspti.h"

#include <atomic>

namespace
{
// UT-only tracking of the stop-path flush order.
std::atomic<bool> g_unsubscribed{false};
std::atomic<int> g_unsubscribeCount{0};
std::atomic<int> g_flushAfterUnsubscribeCount{0};
}  // namespace

namespace dynolog_npu
{
namespace ipc_monitor
{
namespace stub
{
void ResetMsptiCallOrder()
{
    g_unsubscribed.store(false);
    g_unsubscribeCount.store(0);
    g_flushAfterUnsubscribeCount.store(0);
}

int GetUnsubscribeCount() { return g_unsubscribeCount.load(); }

int GetFlushAfterUnsubscribeCount() { return g_flushAfterUnsubscribeCount.load(); }
}  // namespace stub
}  // namespace ipc_monitor
}  // namespace dynolog_npu

msptiResult msptiSubscribe(msptiSubscriberHandle *subscriber, msptiCallbackFunc callback, void *userdata)
{
    return MSPTI_SUCCESS;
}

msptiResult msptiUnsubscribe(msptiSubscriberHandle subscriber)
{
    g_unsubscribed.store(true);
    g_unsubscribeCount.fetch_add(1);
    return MSPTI_SUCCESS;
}

msptiResult msptiActivityRegisterCallbacks(msptiBuffersCallbackRequestFunc funcBufferRequested,
                                           msptiBuffersCallbackCompleteFunc funcBufferCompleted)
{
    return MSPTI_SUCCESS;
}

msptiResult msptiActivityEnable(msptiActivityKind kind) { return MSPTI_SUCCESS; }

msptiResult msptiActivityDisable(msptiActivityKind kind) { return MSPTI_SUCCESS; }

msptiResult msptiActivityGetNextRecord(uint8_t *buffer, size_t validBufferSizeBytes, msptiActivity **record)
{
    return MSPTI_SUCCESS;
}

msptiResult msptiActivityFlushAll(uint32_t flag)
{
    if (g_unsubscribed.load())
    {
        g_flushAfterUnsubscribeCount.fetch_add(1);
    }
    return MSPTI_SUCCESS;
}

msptiResult msptiActivityEnableMarkerDomain(const char *name) { return MSPTI_SUCCESS; }

msptiResult msptiActivityDisableMarkerDomain(const char *name) { return MSPTI_SUCCESS; }
