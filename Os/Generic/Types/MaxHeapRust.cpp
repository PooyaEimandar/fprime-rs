// ======================================================================
// MaxHeapRust.cpp
//
// Rust-backed implementation of the stable max heap behind the OSAL priority
// queue (fprime-rs Phase 9). The class, its header and its semantics are the
// original's; `push` and `pop` -- the heap algorithm, including the age
// tie-break that keeps equal priorities FIFO -- run in Rust
// (Rust/fprime-utils/src/max_heap.rs). Storage stays where it was: the caller
// supplies it and this file still places the node array in it.
//
// Selected instead of MaxHeap.cpp via FPRIME_RUST_IMPL_OS_MAXHEAP; the oracle
// is the existing Types_Max_Heap_test rules-based suite plus the
// PriorityQueue, PriorityMemQueue and LocklessPriorityQueue suites that run on
// top of it.
//
// GENERATED from Os/Generic/Types/MaxHeap.cpp by
// tools/fprime-rs/gen_os_maxheap.py -- do not edit. Change the original (or
// the generator) and regenerate.
// ======================================================================

#include <Fw/FPrimeBasicTypes.hpp>
#include <Fw/Logger/Logger.hpp>
#include <cstddef>
#include <cstdio>
#include "Fw/LanguageHelpers.hpp"
#include "Fw/Types/Assert.hpp"
#include "Os/Generic/Types/MaxHeap.hpp"
#include "Rust/RustOsKernel.h"

// Macros for traversing the heap:
#define LCHILD(x) (2 * x + 1)
#define RCHILD(x) (2 * x + 2)
#define PARENT(x) ((x - 1) / 2)

// clang-format off
// Fingerprints of the upstream bodies this generator replaced or
// removed. The generated file does not contain them, so nothing else
// would notice if upstream changed one -- a mismatch here means the
// C++ that the Rust implementation mirrors has moved and needs
// re-reviewing. The `removed:` entries are the helper functions the
// Rust reimplements, and are the reason this table exists at all.
//
//   push(FwQueuePriorityType, FwSizeType)   6500c7b260b6
//   pop(FwQueuePriorityType&, FwSizeType&)  fd03aad7214f
//   removed: heapify()                      b6fc0cfd73fa
//   removed: max(FwSizeType, FwSizeType)    67fcea298112
//   removed: swap(FwSizeType, FwSizeType)   ef75283e5b96
// clang-format on

namespace Types {

MaxHeap::MaxHeap() {
    this->m_capacity = 0;
    this->m_heap = nullptr;
    this->m_size = 0;
    this->m_order = 0;
}

MaxHeap::~MaxHeap() {
    this->m_heap = nullptr;
}

void MaxHeap::create(FwSizeType capacity, Fw::ByteArray heap_allocation) {
    // The Rust heap indexes this same array. Both sides take their field
    // types from the same generated config, but different toolchains compile
    // them, and a silent layout disagreement would corrupt the queue rather
    // than fail -- so prove they agree, once, when the heap is created.
    FwSizeType rsSize = 0;
    FwSizeType rsAlign = 0;
    FwSizeType rsValueOffset = 0;
    FwSizeType rsOrderOffset = 0;
    FwSizeType rsIdOffset = 0;
    fprime_rs_maxheap_node_layout(&rsSize, &rsAlign, &rsValueOffset, &rsOrderOffset, &rsIdOffset);
    FW_ASSERT(rsSize == sizeof(Node), static_cast<FwAssertArgType>(rsSize), static_cast<FwAssertArgType>(sizeof(Node)));
    FW_ASSERT(rsAlign == alignof(Node), static_cast<FwAssertArgType>(rsAlign),
              static_cast<FwAssertArgType>(alignof(Node)));
    FW_ASSERT(rsValueOffset == offsetof(Node, value), static_cast<FwAssertArgType>(rsValueOffset));
    FW_ASSERT(rsOrderOffset == offsetof(Node, order), static_cast<FwAssertArgType>(rsOrderOffset));
    FW_ASSERT(rsIdOffset == offsetof(Node, id), static_cast<FwAssertArgType>(rsIdOffset));

    FW_ASSERT(this->m_heap == nullptr);
    FW_ASSERT((heap_allocation.size / sizeof(Node)) >= capacity, static_cast<FwAssertArgType>(capacity),
              static_cast<FwAssertArgType>(heap_allocation.size));
    FW_ASSERT(heap_allocation.bytes != nullptr);
    // Loop bounds will overflow if capacity set to the max allowable value
    FW_ASSERT(capacity < std::numeric_limits<FwSizeType>::max());
    this->m_heap = Fw::arrayPlacementNew<Node>(heap_allocation, capacity);
    this->m_capacity = capacity;
}

void MaxHeap::teardown() {
    // Only destroy the heap if it is still allocated
    if (this->m_heap != nullptr) {
        Fw::arrayPlacementDestruct<Node>(this->m_heap, this->m_capacity);
    }
    // Reset the capacity and heap so that the provider of memory
    this->m_capacity = 0;
    this->m_heap = nullptr;
    this->m_size = 0;
    this->m_order = 0;
}

bool MaxHeap::push(FwQueuePriorityType value, FwSizeType id) {
    return fprime_rs_maxheap_push(this->m_heap, this->m_capacity, &this->m_size, &this->m_order, value, id);
}

bool MaxHeap::pop(FwQueuePriorityType& value, FwSizeType& id) {
    return fprime_rs_maxheap_pop(this->m_heap, this->m_capacity, &this->m_size, &this->m_order, &value, &id);
}

// Is the heap full:
bool MaxHeap::isFull() {
    return (this->m_size == this->m_capacity);
}

// Is the heap empty:
bool MaxHeap::isEmpty() {
    return (this->m_size == 0);
}

// Get the current size of the heap:
FwSizeType MaxHeap::getSize() const {
    return this->m_size;
}

}  // namespace Types
