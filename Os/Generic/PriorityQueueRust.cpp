// ======================================================================
// PriorityQueueRust.cpp
//
// Rust-backed implementation of the OSAL priority queue's message-slot
// bookkeeping (fprime-rs Phase 16). The class, its header and its semantics
// are the original's; the four PriorityQueueHandle methods -- which slot a
// message lands in, and where in the slab that slot lives -- run in Rust
// (Rust/fprime-utils/src/queue_slots.rs), as does the heap that orders them
// (Phase 9, Os/Generic/Types/MaxHeapRust.cpp).
//
// Storage, allocation, the mutex, the two condition variables and every
// FW_ASSERT stay here: guards do not cross the boundary, only decisions do.
//
// Selected instead of PriorityQueue.cpp via
// FPRIME_RUST_IMPL_OS_PRIORITYQUEUE; the oracle is the existing
// Os_Generic_PriorityQueue suite plus every active component in the tree,
// each of which queues its messages through this file.
//
// GENERATED from Os/Generic/PriorityQueue.cpp by
// tools/fprime-rs/gen_os_priorityqueue.py -- do not edit. Change the original
// (or the generator) and regenerate.
// ======================================================================

#include <algorithm>
#include <cstring>
#include "Fw/LanguageHelpers.hpp"
#include "Fw/Types/Assert.hpp"
#include "Fw/Types/MemAllocator.hpp"
#include "Os/Generic/PriorityQueue.hpp"
#include "Rust/RustOsKernel.h"
#include "config/MemoryAllocatorTypeEnumAc.hpp"

// clang-format off
// Fingerprints of the upstream bodies this generator replaced or
// removed. The generated file does not contain them, so nothing else
// would notice if upstream changed one -- a mismatch here means the
// C++ that the Rust implementation mirrors has moved and needs
// re-reviewing. The `removed:` entries are the helper functions the
// Rust reimplements, and are the reason this table exists at all.
//
//   FwSizeType PriorityQueueHandle ::find_index()                             7deeaa7b7ab1
//   void PriorityQueueHandle ::return_index(FwSizeType)                       48478621246c
//   void PriorityQueueHandle ::store_data(FwSizeType, const U8*, FwSizeType)  32d6cfa66d97
//   void PriorityQueueHandle ::load_data(FwSizeType, U8*, FwSizeType)         4afa6ed1fc58
// clang-format on

namespace Os {
namespace Generic {

FwSizeType PriorityQueueHandle ::find_index() {
    FW_ASSERT(this->m_depth > 0);
    return fprime_rs_queue_slots_take(this->m_indices, this->m_depth, &this->m_startIndex, &this->m_stopIndex);
}

void PriorityQueueHandle ::return_index(FwSizeType index) {
    FW_ASSERT(this->m_depth > 0);
    fprime_rs_queue_slots_give(this->m_indices, this->m_depth, &this->m_startIndex, &this->m_stopIndex, index);
}

void PriorityQueueHandle ::store_data(FwSizeType index, const U8* data, FwSizeType size) {
    FW_ASSERT(size <= this->m_maxSize);
    FW_ASSERT(index < this->m_depth);
    fprime_rs_queue_slots_store(this->m_data, this->m_sizes, this->m_depth, this->m_maxSize, index, data, size);
}

void PriorityQueueHandle ::load_data(FwSizeType index, U8* destination, FwSizeType size) {
    FW_ASSERT(size <= this->m_maxSize);
    FW_ASSERT(index < this->m_depth);
    fprime_rs_queue_slots_load(this->m_data, this->m_sizes, this->m_depth, this->m_maxSize, index, destination, size);
}

PriorityQueue::~PriorityQueue() {}

QueueInterface::Status PriorityQueue::create(FwEnumStoreType id,
                                             const Fw::ConstStringBase& name,
                                             FwSizeType depth,
                                             FwSizeType messageSize) {
    const FwEnumStoreType identifier = id;
    QueueInterface::Status status = Os::QueueInterface::Status::OP_OK;
    // Ensure we are created exactly once
    FW_ASSERT(this->m_handle.m_indices == nullptr);
    FW_ASSERT(this->m_handle.m_sizes == nullptr);
    FW_ASSERT(this->m_handle.m_data == nullptr);

    // Get the memory allocator configured for priority queues
    Fw::MemAllocator& allocator = Fw::MemAllocatorRegistry::getInstance().getAnAllocator(
        Fw::MemoryAllocation::MemoryAllocatorType::OS_GENERIC_PRIORITY_QUEUE);

    // Allocate indices list
    void* allocation = nullptr;
    FwSizeType size = 0;
    FwSizeType* indices = nullptr;
    FwSizeType* sizes = nullptr;
    U8* data = nullptr;
    U8* heap_pointer = nullptr;

    // Prevent integer overflow when computing allocation size (depth * sizeof(FwSizeType))
    FW_ASSERT(depth < std::numeric_limits<FwSizeType>::max() / sizeof(FwSizeType));

    // Allocate indices list and construct it when valid
    size = depth * sizeof(FwSizeType);
    allocation = allocator.allocate(identifier, size, alignof(FwSizeType));
    if (allocation == nullptr) {
        status = QueueInterface::Status::ALLOCATION_FAILED;
    } else if (size < (depth * sizeof(FwSizeType))) {
        allocator.deallocate(identifier, allocation);
        status = QueueInterface::Status::ALLOCATION_FAILED;
    } else {
        indices = Fw::arrayPlacementNew<FwSizeType>(Fw::ByteArray(static_cast<U8*>(allocation), size), depth);
    }

    // Allocate sizes list and construct it when valid
    if (status == QueueInterface::Status::OP_OK) {
        size = depth * sizeof(FwSizeType);
        allocation = allocator.allocate(identifier, size, alignof(FwSizeType));
        if (allocation == nullptr) {
            allocator.deallocate(identifier, indices);
            status = QueueInterface::Status::ALLOCATION_FAILED;
        } else if (size < (depth * sizeof(FwSizeType))) {
            allocator.deallocate(identifier, indices);
            allocator.deallocate(identifier, allocation);
            status = QueueInterface::Status::ALLOCATION_FAILED;
        } else {
            sizes = Fw::arrayPlacementNew<FwSizeType>(Fw::ByteArray(static_cast<U8*>(allocation), size), depth);
        }
    }
    // Allocate data
    if (status == QueueInterface::Status::OP_OK) {
        // Prevent integer overflow when computing allocation size (depth * messageSize)
        FW_ASSERT((depth == 0) || (messageSize <= std::numeric_limits<FwSizeType>::max() / depth));
        size = depth * messageSize;
        allocation = allocator.allocate(identifier, size, alignof(U8));
        if (allocation == nullptr) {
            allocator.deallocate(identifier, indices);
            allocator.deallocate(identifier, sizes);
            status = QueueInterface::Status::ALLOCATION_FAILED;
        } else if (size < (depth * messageSize)) {
            allocator.deallocate(identifier, indices);
            allocator.deallocate(identifier, sizes);
            allocator.deallocate(identifier, allocation);
            status = QueueInterface::Status::ALLOCATION_FAILED;
        } else {
            data = static_cast<U8*>(allocation);
        }
    }
    // Allocate data for max heap
    if (status == QueueInterface::Status::OP_OK) {
        size = Types::MaxHeap::ELEMENT_SIZE * depth;
        allocation = allocator.allocate(identifier, size, Types::MaxHeap::ALIGNMENT);
        if (allocation == nullptr) {
            allocator.deallocate(identifier, indices);
            allocator.deallocate(identifier, sizes);
            allocator.deallocate(identifier, data);
            status = QueueInterface::Status::ALLOCATION_FAILED;
        } else if (size < (Types::MaxHeap::ELEMENT_SIZE * depth)) {
            allocator.deallocate(identifier, indices);
            allocator.deallocate(identifier, sizes);
            allocator.deallocate(identifier, data);
            allocator.deallocate(identifier, allocation);
            status = QueueInterface::Status::ALLOCATION_FAILED;
        } else {
            heap_pointer = static_cast<U8*>(allocation);
            this->m_handle.m_heap.create(depth, Fw::ByteArray(static_cast<U8*>(allocation), size));
        }
    }
    // Set up structures when all allocations succeeded
    if (status == QueueInterface::Status::OP_OK) {
        // Assign initial indices and sizes. The ring fill is the Rust
        // reset(), so "a fresh ring" has one definition rather than two that
        // have to agree; the size list is plain zeroing and stays here.
        fprime_rs_queue_slots_reset(indices, depth, &this->m_handle.m_startIndex, &this->m_handle.m_stopIndex);
        for (FwSizeType i = 0; i < depth; i++) {
            sizes[i] = 0;
        }
        // Set local tracking variables
        this->m_handle.m_id = id;
        this->m_handle.m_maxSize = messageSize;
        this->m_handle.m_indices = indices;
        this->m_handle.m_data = data;
        this->m_handle.m_sizes = sizes;
        this->m_handle.m_heap_pointer = heap_pointer;
        // m_startIndex and m_stopIndex were rewound by
        // fprime_rs_queue_slots_reset above.
        this->m_handle.m_depth = depth;
        this->m_handle.m_highMark = 0;
    }
    return status;
}

void PriorityQueue::teardown() {
    this->teardownInternal();
}

void PriorityQueue::teardownInternal() {
    if (this->m_handle.m_data != nullptr) {
        // All backing arrays are allocated together in create()
        FW_ASSERT(this->m_handle.m_indices != nullptr);
        FW_ASSERT(this->m_handle.m_sizes != nullptr);
        FW_ASSERT(this->m_handle.m_heap_pointer != nullptr);
        const FwEnumStoreType identifier = this->m_handle.m_id;
        Fw::MemAllocator& allocator = Fw::MemAllocatorRegistry::getInstance().getAnAllocator(
            Fw::MemoryAllocation::MemoryAllocatorType::OS_GENERIC_PRIORITY_QUEUE);
        allocator.deallocate(identifier, this->m_handle.m_data);
        allocator.deallocate(identifier, this->m_handle.m_indices);
        allocator.deallocate(identifier, this->m_handle.m_sizes);
        this->m_handle.m_heap.teardown();
        allocator.deallocate(identifier, this->m_handle.m_heap_pointer);

        // Set these pointers to nullptr
        this->m_handle.m_data = nullptr;
        this->m_handle.m_indices = nullptr;
        this->m_handle.m_sizes = nullptr;
    }
}

QueueInterface::Status PriorityQueue::send(const U8* buffer,
                                           FwSizeType size,
                                           FwQueuePriorityType priority,
                                           QueueInterface::BlockingType blockType) {
    // Check for sizing problem before locking
    if (size > this->m_handle.m_maxSize) {
        return QueueInterface::Status::SIZE_MISMATCH;
    }
    // Artificial block scope for scope lock ensuring an unlock in all cases and ensuring an unlock before notify
    {
        Os::ScopeLock lock(this->m_handle.m_data_lock);
        if (this->m_handle.m_heap.isFull() and blockType == BlockingType::NONBLOCKING) {
            return QueueInterface::Status::FULL;
        }
        // Will loop and block until full is false
        // @non-terminating@: condition-variable wait loop
        while (this->m_handle.m_heap.isFull()) {
            this->m_handle.m_full.wait(this->m_handle.m_data_lock);
        }
        FwSizeType index = this->m_handle.find_index();

        // Space must exist, push must work
        const bool pushed = this->m_handle.m_heap.push(priority, index);
        FW_ASSERT(pushed);
        // store_data records the length; writing m_sizes again here was a
        // second place that had to agree with it.
        this->m_handle.store_data(index, buffer, size);
        this->m_handle.m_highMark = std::max(this->m_handle.m_highMark, this->getMessagesAvailable());
    }
    this->m_handle.m_empty.notify();
    return QueueInterface::Status::OP_OK;
}

QueueInterface::Status PriorityQueue::receive(U8* destination,
                                              FwSizeType capacity,
                                              QueueInterface::BlockingType blockType,
                                              FwSizeType& actualSize,
                                              FwQueuePriorityType& priority) {
    {
        Os::ScopeLock lock(this->m_handle.m_data_lock);
        if (this->m_handle.m_heap.isEmpty() and blockType == BlockingType::NONBLOCKING) {
            return QueueInterface::Status::EMPTY;
        }
        // Loop and lock while empty
        // @non-terminating@: condition-variable wait loop
        while (this->m_handle.m_heap.isEmpty()) {
            this->m_handle.m_empty.wait(this->m_handle.m_data_lock);
        }

        FwSizeType index;
        // Message must exist, so pop must pass and size must be valid
        const bool popped = this->m_handle.m_heap.pop(priority, index);
        FW_ASSERT(popped);
        actualSize = this->m_handle.m_sizes[index];
        FW_ASSERT(actualSize <= capacity);
        this->m_handle.load_data(index, destination, actualSize);
        this->m_handle.return_index(index);
    }
    this->m_handle.m_full.notify();
    return QueueInterface::Status::OP_OK;
}

FwSizeType PriorityQueue::getMessagesAvailable() const {
    return this->m_handle.m_heap.getSize();
}

FwSizeType PriorityQueue::getMessageHighWaterMark() const {
    // Safe to cast away const in this context because scope lock will restore unlocked state on return
    Os::ScopeLock lock(const_cast<Mutex&>(this->m_handle.m_data_lock));
    return this->m_handle.m_highMark;
}

QueueHandle* PriorityQueue::getHandle() {
    return &this->m_handle;
}

}  // namespace Generic
}  // namespace Os
