// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <lib/user_copy/user_ptr.h>
#include <zircon/types.h>
#include <fbl/mutex.h>
#include <kernel/lockdep.h>
#include <object/futex_node.h>

// FutexContext is a class that encapsulates support for futex operations.
// FutexContext uses a hash table keyed on the futex address (a pointer to integer in userspace)
// to contain all active futexes.
// A futex is considered active if there is one or more threads blocked on the futex.
// After no threads are left blocked on a futex it is removed from the hash table.
// The value in the futex hash table is the FutexNode object associated with the head
// of the list of threads blocked on the futex.
// To avoid memory allocation at futex operation time, a FutexNode is embedded in each
// ThreadDispatcher object.
// When the thread at the head of the futex's blocked thread list is resumed,
// The FutexNode for the new head of the blocked thread list is set as the hash table value
// for the futex.
class FutexContext {
public:
    FutexContext();
    ~FutexContext();

    enum class OwnerAction {
        RELEASE,
        ASSIGN_WOKEN,
    };

    // FutexWait first verifies that the integer pointed to by |value_ptr|
    // still equals |current_value|. If the test fails, FutexWait returns ZX_ERR_BAD_STATE.
    // Otherwise it will block the current thread until the |deadline| passes, or until the thread
    // is woken by a FutexWake or FutexRequeue operation on the same |value_ptr| futex.
    zx_status_t FutexWait(user_in_ptr<const zx_futex_t> value_ptr, zx_futex_t current_value,
                          zx_handle_t new_futex_owner, const Deadline& deadline);

    // FutexWake will wake up to |wake_count| number of threads blocked on the |value_ptr| futex.
    //
    // If owner_action is set to RELEASE, then the futex's owner will be set to nullptr in the
    // process.  If the owner_action is set to ASSIGN_WOKEN, then the wake_count *must* be 1, and
    // the futex's owner will be set to the thread which was woken during the operation, or nullptr
    // if no thread was woken.
    zx_status_t FutexWake(user_in_ptr<const zx_futex_t> value_ptr, uint32_t wake_count,
                          OwnerAction owner_action);

    // FutexWait first verifies that the integer pointed to by |wake_ptr|
    // still equals |current_value|. If the test fails, FutexWait returns ZX_ERR_BAD_STATE.
    // Otherwise it will wake up to |wake_count| number of threads blocked on the |wake_ptr| futex.
    // If any other threads remain blocked on on the |wake_ptr| futex, up to |requeue_count|
    // of them will then be requeued to the tail of the list of threads
    // blocked on the |requeue_ptr| futex.
    //
    // If owner_action is set to RELEASE, then the futex's owner will be set to nullptr in the
    // process.  If the owner_action is set to ASSIGN_WOKEN, then the wake_count *must* be 1, and
    // the futex's owner will be set to the thread which was woken during the operation, or nullptr
    // if no thread was woken.
    zx_status_t FutexRequeue(user_in_ptr<const zx_futex_t> wake_ptr,
                             uint32_t wake_count,
                             zx_futex_t current_value,
                             OwnerAction owner_action,
                             user_in_ptr<const zx_futex_t> requeue_ptr,
                             uint32_t requeue_count,
                             zx_handle_t new_requeue_owner);

    // Get the KOID of the current owner of the specified futex, if any, or ZX_KOID_INVALID if there
    // is no known owner.
    zx_status_t FutexGetOwner(user_in_ptr<const zx_futex_t> wake_ptr,
                              user_out_ptr<zx_koid_t> koid);

private:
    FutexContext(const FutexContext&) = delete;
    FutexContext& operator=(const FutexContext&) = delete;

    void QueueNodesLocked(FutexNode* head) TA_REQ(lock_);

    bool UnqueueNodeLocked(FutexNode* node) TA_REQ(lock_);

    // protects futex_table_
    DECLARE_MUTEX(FutexContext) lock_;

    // Hash table for futexes in this context.
    // Key is futex address, value is the FutexNode for the head of futex's blocked thread list.
    FutexNode::HashTable futex_table_ TA_GUARDED(lock_);
};
