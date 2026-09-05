#!/usr/bin/env python3
"""Generate Os/Generic/PriorityQueueRust.cpp from PriorityQueue.cpp.

fprime-rs Phase 16: the message-slot bookkeeping of the OSAL priority queue
runs in Rust. This is the queue `cmake/platform/unix/Platform/CMakeLists.txt`
selects, so it is the one every active component in a Linux or macOS
deployment sends through.

The queue has two data structures. Its *ordering* -- the stable max heap that
decides which message comes out next -- moved to Rust in Phase 9
(gen_os_maxheap.py). Its *allocation* moves here: the circular list of free
slot indices, and the flat slab those slots address. Between the two, every
non-OS decision in the queue is now Rust.

What stays in C++ is what should. `create` and `teardown` are memory
management against `Fw::MemAllocator`. `send` and `receive` are the mutex, the
two condition variables and the blocking policy. Every `FW_ASSERT` stays where
it is: guards do not cross the boundary, only decisions do.

  python3 tools/fprime-rs/gen_os_priorityqueue.py           # regenerate
  python3 tools/fprime-rs/gen_os_priorityqueue.py --check   # verify

Selected instead of PriorityQueue.cpp via FPRIME_RUST_IMPL_OS_PRIORITYQUEUE.
The oracle is the existing Os_Generic_PriorityQueue suite plus every active
component in the tree, all of which queue through this file.

See tools/fprime-rs/shimgen.py for the generation and drift-check mechanism.
"""

import sys

import shimgen
from shimgen import REPO

SOURCE = REPO / "Os" / "Generic" / "PriorityQueue.cpp"
TARGET = REPO / "Os" / "Generic" / "PriorityQueueRust.cpp"

BANNER = """\
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

"""

KERNEL_INCLUDE = '#include "Rust/RustOsKernel.h"\n'

# The C++ fills the ring inline at the end of create(). Rerouting it keeps one
# definition of "a fresh ring" rather than two that must agree.
RING_INIT_FROM = """        // Assign initial indices and sizes
        for (FwSizeType i = 0; i < depth; i++) {
            indices[i] = i;
            sizes[i] = 0;
        }
"""

RING_INIT_TO = """        // Assign initial indices and sizes. The ring fill is the Rust
        // reset(), so "a fresh ring" has one definition rather than two that
        // have to agree; the size list is plain zeroing and stays here.
        fprime_rs_queue_slots_reset(indices, depth, &this->m_handle.m_startIndex,
                                    &this->m_handle.m_stopIndex);
        for (FwSizeType i = 0; i < depth; i++) {
            sizes[i] = 0;
        }
"""

REPLACEMENTS = [
    (
        "FwSizeType PriorityQueueHandle ::find_index()",
        """    FW_ASSERT(this->m_depth > 0);
    return fprime_rs_queue_slots_take(this->m_indices, this->m_depth, &this->m_startIndex,
                                      &this->m_stopIndex);
""",
    ),
    (
        "void PriorityQueueHandle ::return_index(FwSizeType index)",
        """    FW_ASSERT(this->m_depth > 0);
    fprime_rs_queue_slots_give(this->m_indices, this->m_depth, &this->m_startIndex, &this->m_stopIndex, index);
""",
    ),
    (
        "void PriorityQueueHandle ::store_data(FwSizeType index, const U8* data, FwSizeType size)",
        """    FW_ASSERT(size <= this->m_maxSize);
    FW_ASSERT(index < this->m_depth);
    fprime_rs_queue_slots_store(this->m_data, this->m_sizes, this->m_depth, this->m_maxSize, index, data, size);
""",
    ),
    (
        "void PriorityQueueHandle ::load_data(FwSizeType index, U8* destination, FwSizeType size)",
        """    FW_ASSERT(size <= this->m_maxSize);
    FW_ASSERT(index < this->m_depth);
    fprime_rs_queue_slots_load(this->m_data, this->m_sizes, this->m_depth, this->m_maxSize, index, destination,
                               size);
""",
    ),
]

# create() zeroes both cursors again after filling the ring. reset() already
# set them, and leaving the duplicate would mean two places claim to define
# where a fresh ring starts -- the exact drift this file exists to prevent.
CURSOR_INIT_FROM = """        this->m_handle.m_startIndex = 0;
        this->m_handle.m_stopIndex = 0;
"""

CURSOR_INIT_TO = """        // m_startIndex and m_stopIndex were rewound by
        // fprime_rs_queue_slots_reset above.
"""

SHIM = shimgen.Shim(
    source=SOURCE,
    target=TARGET,
    banner=BANNER,
    replacements=REPLACEMENTS,
    edits=[
        (
            '#include "Fw/Types/Assert.hpp"\n',
            '#include "Fw/Types/Assert.hpp"\n' + KERNEL_INCLUDE,
            True,
        ),
        (RING_INIT_FROM, RING_INIT_TO, True),
        (CURSOR_INIT_FROM, CURSOR_INIT_TO, True),
    ],
    preamble=("namespace Os {\n", ""),
)


if __name__ == "__main__":
    sys.exit(shimgen.main(SHIM, __doc__))
