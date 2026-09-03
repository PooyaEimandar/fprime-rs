#!/usr/bin/env python3
"""Generate Os/Generic/Types/MaxHeapRust.cpp from MaxHeap.cpp.

fprime-rs Phase 9: the stable max heap that orders every message queued to an
active component runs in Rust. `push` and `pop` call into
`fprime-utils::max_heap`; everything else in the file -- the constructor,
`create`/`teardown` with their placement-new, and the three trivial accessors
-- is the original's, because it is memory management and bookkeeping rather
than algorithm.

  python3 tools/fprime-rs/gen_os_maxheap.py           # regenerate
  python3 tools/fprime-rs/gen_os_maxheap.py --check   # verify, exit 1 on drift

`heapify`, `swap` and `max` are declared in the unchanged header and were only
ever called from `push`/`pop`; with those two rerouted, the three are
deliberately left undefined here rather than kept as dead code that no longer
runs. Nothing outside the class can call them -- they are private.

See tools/fprime-rs/shimgen.py for the generation and drift-check mechanism.
"""

import sys

import shimgen
from shimgen import REPO

SOURCE = REPO / "Os" / "Generic" / "Types" / "MaxHeap.cpp"
TARGET = REPO / "Os" / "Generic" / "Types" / "MaxHeapRust.cpp"

BANNER = """\
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
// PriorityQueue suite that runs on
// top of it.
//
// GENERATED from Os/Generic/Types/MaxHeap.cpp by
// tools/fprime-rs/gen_os_maxheap.py -- do not edit. Change the original (or
// the generator) and regenerate.
// ======================================================================

"""

KERNEL_INCLUDE = '#include "Rust/RustOsKernel.h"\n'

# The Rust `Node` and the C++ `MaxHeap::Node` index the same array, so their
# layouts must agree exactly. The C++ Node is private, so these live inside a
# member function where the name is accessible.
LAYOUT_GUARD = """\
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

"""

REPLACEMENTS = [
    (
        "bool MaxHeap::push(FwQueuePriorityType value, FwSizeType id)",
        """    return fprime_rs_maxheap_push(this->m_heap, this->m_capacity, &this->m_size, &this->m_order, value, id);
""",
    ),
    (
        "bool MaxHeap::pop(FwQueuePriorityType& value, FwSizeType& id)",
        """    return fprime_rs_maxheap_pop(this->m_heap, this->m_capacity, &this->m_size, &this->m_order, &value, &id);
""",
    ),
]

SHIM = shimgen.Shim(
    source=SOURCE,
    target=TARGET,
    banner=BANNER,
    replacements=REPLACEMENTS,
    edits=[
        # The kernel declarations, and the layout guard inside create() where
        # the private nested Node type is nameable.
        (
            '#include "Fw/Types/Assert.hpp"\n',
            '#include "Fw/Types/Assert.hpp"\n'
            + KERNEL_INCLUDE
            + "#include <cstddef>\n",
            True,
        ),
        (
            "void MaxHeap::create(FwSizeType capacity, Fw::ByteArray heap_allocation) {\n",
            "void MaxHeap::create(FwSizeType capacity, Fw::ByteArray heap_allocation) {\n"
            + LAYOUT_GUARD,
            True,
        ),
    ],
    # heapify/swap/max were only ever called from push/pop, both of which now
    # run in Rust. Remove them rather than leave unreachable code in a flight
    # file; they are private, so nothing outside the class could call them.
    removals=[
        "void MaxHeap::heapify()",
        "FwSizeType MaxHeap::max(FwSizeType a, FwSizeType b)",
        "void MaxHeap::swap(FwSizeType a, FwSizeType b)",
    ],
    preamble=("namespace Types {\n", ""),
)


if __name__ == "__main__":
    sys.exit(shimgen.main(SHIM, __doc__))
