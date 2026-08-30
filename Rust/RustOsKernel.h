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
