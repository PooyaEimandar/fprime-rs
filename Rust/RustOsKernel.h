// ======================================================================
// \title  RustOsKernel.h
// \brief  C declarations for the Rust-backed OSAL pieces
//
// fprime-rs Phase 9. As in Phase 8's Fw kernel, C++ keeps the class, its
// header and its storage; Rust owns the algorithm.
//
// MaxHeap is the ordering core of Os::Generic::PriorityQueue and
// PriorityMemQueue: every message a deployment queues to an active component
// is ordered by it, and it must be *stable* -- equal priorities pop in FIFO
// order, which the node's push-order field is what buys.
//
// The node array belongs to the C++ object (its create() places the nodes in
// memory the queue supplies) and both languages index it, so the two Node
// layouts must agree exactly. fprime_rs_maxheap_node_layout reports the Rust
// side's so the shim can pin size, alignment and every field offset against
// its own at heap creation. `nodes` is void* here because the C++ Node is a
// private nested type that this header cannot name.
// ======================================================================
#ifndef FPRIME_RUST_OS_KERNEL_H
#define FPRIME_RUST_OS_KERNEL_H

#include <stdbool.h>
#include <stdint.h>

#include <Fw/FPrimeBasicTypes.h>

#ifdef __cplusplus
extern "C" {
#endif

//! Push one entry; false when the heap is full.
bool fprime_rs_maxheap_push(void* nodes,
                            FwSizeType capacity,
                            FwSizeType* size,
                            FwSizeType* order,
                            FwQueuePriorityType value,
                            FwSizeType id);

//! Pop the highest-priority entry, oldest first among equals; false when the
//! heap is empty, in which case the out-parameters are left untouched.
bool fprime_rs_maxheap_pop(void* nodes,
                           FwSizeType capacity,
                           FwSizeType* size,
                           FwSizeType* order,
                           FwQueuePriorityType* value,
                           FwSizeType* id);

//! Reports the Rust node layout, so the shim can prove the two agree.
void fprime_rs_maxheap_node_layout(FwSizeType* size,
                                   FwSizeType* align,
                                   FwSizeType* valueOffset,
                                   FwSizeType* orderOffset,
                                   FwSizeType* idOffset);

// Os::Generic::PriorityQueue message slots (Phase 16). The queue's other
// half: MaxHeap decides which message comes out next, these decide which slot
// it lives in and where in the slab that slot is. The C++ keeps the
// allocation, the mutex, the two condition variables and every FW_ASSERT, and
// calls these only while holding the handle's m_data_lock.
//
// The ring cannot tell "all slots free" from "all slots taken" -- both are
// start == stop -- so the caller must check the heap's isFull()/isEmpty()
// before taking or returning an index, which is what PriorityQueue::send and
// ::receive already do. Rust preserves that rather than adding a second count
// of the same fact.

//! Fill the ring with every slot, free, and rewind both cursors.
void fprime_rs_queue_slots_reset(FwSizeType* indices, FwSizeType depth, FwSizeType* start, FwSizeType* stop);

//! Take the next free slot index.
FwSizeType fprime_rs_queue_slots_take(FwSizeType* indices,
                                      FwSizeType depth,
                                      FwSizeType* start,
                                      FwSizeType* stop);

//! Return a slot index to the free list.
void fprime_rs_queue_slots_give(FwSizeType* indices,
                                FwSizeType depth,
                                FwSizeType* start,
                                FwSizeType* stop,
                                FwSizeType index);

//! Copy a message into a slot and record its length.
void fprime_rs_queue_slots_store(U8* data,
                                 FwSizeType* sizes,
                                 FwSizeType depth,
                                 FwSizeType maxSize,
                                 FwSizeType index,
                                 const U8* source,
                                 FwSizeType size);

//! Copy a stored message out. `size` is the STORED length, which the caller
//! reads from `sizes` -- not the receiver's capacity. Passing the capacity
//! would copy whatever the slot's previous message left behind.
void fprime_rs_queue_slots_load(U8* data,
                                FwSizeType* sizes,
                                FwSizeType depth,
                                FwSizeType maxSize,
                                FwSizeType index,
                                U8* destination,
                                FwSizeType size);

// Os::FilePathUtils: textual path resolution and sandbox containment. Both
// return an Os::FilePathUtils::Status integer value. `baseDir` may be null,
// which is an error only for a relative path. Strings must be terminated
// within maxPath; an unterminated one is reported as a malformed path rather
// than scanned past, matching Fw::StringUtils::string_length's bound.
int32_t fprime_rs_path_resolve(const char* path,
                               const char* baseDir,
                               char* out,
                               FwSizeType outSize,
                               FwSizeType maxPath);
int32_t fprime_rs_path_check_containment(const char* resolved, const char* allowed, FwSizeType maxPath);

#ifdef __cplusplus
}
#endif

#endif  // FPRIME_RUST_OS_KERNEL_H
