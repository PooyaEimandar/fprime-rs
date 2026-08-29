// ======================================================================
// \title  RustComQueueSchedulerTest.cpp
// \brief  Direct tests of the ComQueue priority scheduler across the FFI
//
// Two gaps make this necessary:
//
//   * ComQueue's own unit-test suite never observes the round-robin rotation
//     (it does not configure multiple queues at the same priority with data
//     in each), so passing that suite does not exercise the only nontrivial
//     behavior this port moved to Rust.
//   * `cargo test` excludes the fprime-ffi crate, so the exported entry point
//     `fprime_rs_comqueue_select` has no coverage from the Rust side either.
//
// These tests call the shipped FFI with known inputs, which also pins the
// FprimeRsComQueueDecision struct layout across the language boundary.
// ======================================================================

#include <gtest/gtest.h>

#include <Fw/FPrimeBasicTypes.hpp>

#include "Rust/RustSvc.h"

namespace {

//! Run one selection over the given prioritized list and queue depths.
FprimeRsComQueueDecision select(const I16* indices,
                                const I16* priorities,
                                FwSizeType count,
                                const FwSizeType* sizes,
                                FwSizeType sizeCount) {
    FprimeRsComQueueDecision decision;
    fprime_rs_comqueue_select(indices, priorities, count, sizes, sizeCount, &decision);
    return decision;
}

//! Apply the rotation exactly as ComQueueRust.cpp does, over a parallel list.
void rotate(I16* indices, I16* priorities, const FprimeRsComQueueDecision& decision) {
    for (FwSizeType position = decision.position + 1; position < decision.rotateEnd; position++) {
        const I16 tempIndex = indices[position];
        const I16 tempPriority = priorities[position];
        indices[position] = indices[position - 1];
        priorities[position] = priorities[position - 1];
        indices[position - 1] = tempIndex;
        priorities[position - 1] = tempPriority;
    }
}

}  // namespace

TEST(RustComQueueScheduler, AllQueuesEmptySelectsNothing) {
    const I16 indices[] = {0, 1, 2};
    const I16 priorities[] = {0, 1, 2};
    const FwSizeType sizes[] = {0, 0, 0};
    const auto decision = select(indices, priorities, 3, sizes, 3);
    ASSERT_EQ(decision.selected, -1) << "empty queues must select nothing";
}

TEST(RustComQueueScheduler, HighestPriorityNonEmptyWins) {
    // List order is priority order; queue 2 is first but empty
    const I16 indices[] = {2, 0, 1};
    const I16 priorities[] = {0, 1, 1};
    const FwSizeType sizes[] = {5, 5, 0};  // queues 0 and 1 by queue index
    const auto decision = select(indices, priorities, 3, sizes, 3);
    ASSERT_EQ(decision.selected, 0);
    ASSERT_EQ(decision.position, 1u);
    ASSERT_EQ(decision.rotateEnd, 3u) << "the priority-1 run covers positions 1..2";
}

TEST(RustComQueueScheduler, SamePriorityQueuesRoundRobin) {
    // Three queues, all priority 0, all with data: each must get a turn.
    // This is the behavior ComQueue's own suite never observes.
    I16 indices[] = {0, 1, 2};
    I16 priorities[] = {0, 0, 0};
    const FwSizeType sizes[] = {1, 1, 1};

    I16 served[6] = {0};
    for (FwSizeType i = 0; i < 6; i++) {
        const auto decision = select(indices, priorities, 3, sizes, 3);
        ASSERT_GE(decision.selected, 0);
        served[i] = decision.selected;
        rotate(indices, priorities, decision);
    }
    const I16 expected[6] = {0, 1, 2, 0, 1, 2};
    for (FwSizeType i = 0; i < 6; i++) {
        ASSERT_EQ(served[i], expected[i]) << "round-robin broke at turn " << i;
    }
}

TEST(RustComQueueScheduler, RotationStopsAtPriorityBoundary) {
    // Two queues at priority 0, one at priority 1: the rotation must not
    // drag the lower-priority entry into the high-priority run.
    I16 indices[] = {0, 1, 9};
    I16 priorities[] = {0, 0, 1};
    const FwSizeType sizes[] = {1, 1, 0, 0, 0, 0, 0, 0, 0, 1};

    auto decision = select(indices, priorities, 3, sizes, 10);
    ASSERT_EQ(decision.selected, 0);
    ASSERT_EQ(decision.rotateEnd, 2u) << "priority-1 entry must stay put";
    rotate(indices, priorities, decision);
    ASSERT_EQ(indices[0], 1);
    ASSERT_EQ(indices[1], 0);
    ASSERT_EQ(indices[2], 9) << "lower-priority entry must remain last";

    // The lower-priority queue is served only once both priority-0 queues drain
    const FwSizeType drained[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    decision = select(indices, priorities, 3, drained, 10);
    ASSERT_EQ(decision.selected, 9);
}

TEST(RustComQueueScheduler, SelectedEntryLastInListDoesNotRotate) {
    const I16 indices[] = {0, 1};
    const I16 priorities[] = {0, 1};
    const FwSizeType sizes[] = {0, 4};
    const auto decision = select(indices, priorities, 2, sizes, 2);
    ASSERT_EQ(decision.selected, 1);
    ASSERT_EQ(decision.position, 1u);
    ASSERT_EQ(decision.rotateEnd, 2u) << "a trailing selection rotates nothing";
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
