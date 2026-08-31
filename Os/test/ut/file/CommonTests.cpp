// ======================================================================
// \title Os/test/ut/file/CommonTests.cpp
// \brief common test implementations
// ======================================================================
#include "Os/test/ut/file/CommonTests.hpp"
#include <gtest/gtest.h>
#include <cstring>
#include <vector>
#include "Os/File.hpp"

static const U32 RANDOM_BOUND = 1000;

Os::Test::FileTest::Tester::Tester() {
    // Wipe out the file system with a fresh copy
    SyntheticFile::setFileSystem(std::unique_ptr<SyntheticFileSystem>(new SyntheticFileSystem()));
}

Functionality::Functionality() : tester(Os::Test::FileTest::get_tester_implementation()) {}

void Functionality::SetUp() {
    Os::Test::FileTest::setUp(false);
}

void Functionality::TearDown() {
    Os::Test::FileTest::tearDown();
}

void FunctionalIO::SetUp() {
    // Check that the tester supports functional tests
    if (this->tester->functional()) {
        this->Functionality::SetUp();
    } else {
        GTEST_SKIP() << "Tester does not support functional i/o testing";
    }
}

// Ensure that open mode changes work reliably
TEST_F(Functionality, OpenWithCreation) {
    Os::Test::FileTest::Tester::OpenFileCreate rule(false);
    rule.apply(*tester);
}

// Ensure that close mode changes work reliably
TEST_F(Functionality, Close) {
    Os::Test::FileTest::Tester::OpenFileCreate create_rule(false);
    Os::Test::FileTest::Tester::CloseFile close_rule;
    create_rule.apply(*tester);
    close_rule.apply(*tester);
}

// Ensure that the assignment operator works correctly
TEST_F(Functionality, AssignmentOperator) {
    Os::Test::FileTest::Tester::OpenFileCreate open_rule(false);
    Os::Test::FileTest::Tester::CopyAssignment copy_rule;
    Os::Test::FileTest::Tester::CloseFile close_rule;
    open_rule.apply(*tester);
    copy_rule.apply(*tester);
    close_rule.apply(*tester);
}

// Ensure the copy constructor works correctly
TEST_F(Functionality, CopyConstructor) {
    Os::Test::FileTest::Tester::OpenFileCreate open_rule(false);
    Os::Test::FileTest::Tester::CopyConstruction copy_rule;
    Os::Test::FileTest::Tester::CloseFile close_rule;
    open_rule.apply(*tester);
    copy_rule.apply(*tester);
    close_rule.apply(*tester);
}

// Ensure that open works via the bounded char* overload
TEST_F(Functionality, OpenWithCreationBounded) {
    Os::Test::FileTest::Tester::OpenFileCreateBounded rule(false);
    rule.apply(*tester);
}

// Ensure that open works via the ConstStringBase overload
TEST_F(Functionality, OpenWithCreationString) {
    Os::Test::FileTest::Tester::OpenFileCreateString rule(false);
    rule.apply(*tester);
}

// Ensure that open asserts on unterminated path within bounds
TEST_F(InvalidArguments, OpenBoundedPathUnterminated) {
    Os::Test::FileTest::Tester::OpenIllegalBoundedPath rule;
    rule.apply(*tester);
}

// Ensure that open on existence works
TEST_F(FunctionalIO, OpenWithCreationExists) {
    Os::Test::FileTest::Tester::OpenFileCreate open_rule(false);
    Os::Test::FileTest::Tester::CloseFile close_rule;
    open_rule.apply(*tester);
    close_rule.apply(*tester);
    open_rule.apply(*tester);
}

// Ensure that open on existence with overwrite works
TEST_F(Functionality, OpenWithCreationOverwrite) {
    Os::Test::FileTest::Tester::OpenFileCreate open_rule(false);
    Os::Test::FileTest::Tester::OpenFileCreateOverwrite open_overwrite(false);
    Os::Test::FileTest::Tester::CloseFile close_rule;
    open_rule.apply(*tester);
    close_rule.apply(*tester);
    open_overwrite.apply(*tester);
}

// Ensure that open mode changes work reliably
TEST_F(Functionality, OpenInvalidModes) {
    Os::Test::FileTest::Tester::OpenFileCreate original_open(false);
    Os::Test::FileTest::Tester::OpenInvalidModes invalid_open;
    original_open.apply(*tester);
    invalid_open.apply(*tester);
}

// Ensure that Os::File properly refuses preallocate calls when not open
TEST_F(Functionality, PreallocateWithoutOpen) {
    Os::Test::FileTest::Tester::PreallocateWithoutOpen rule;
    rule.apply(*tester);
}

// Ensure that Os::File properly refuses seek calls when not open
TEST_F(Functionality, SeekWithoutOpen) {
    Os::Test::FileTest::Tester::SeekWithoutOpen rule;
    rule.apply(*tester);
}

// Ensure that Os::File properly refuses seek calls when not open
TEST_F(FunctionalIO, SeekInvalidSize) {
    Os::Test::FileTest::Tester::OpenFileCreate original_open(false);
    Os::Test::FileTest::Tester::SeekInvalidSize rule;
    original_open.apply(*tester);
    rule.apply(*tester);
}

// Ensure that Os::File properly refuses flush calls when not open and when reading
TEST_F(Functionality, FlushInvalidModes) {
    Os::Test::FileTest::Tester::FlushInvalidModes flush_rule;
    Os::Test::FileTest::Tester::OpenFileCreate open_rule(false);
    Os::Test::FileTest::Tester::CloseFile close_rule;
    Os::Test::FileTest::Tester::OpenForRead open_read;

    // Test flush in closed state
    flush_rule.apply(*tester);

    // Used to create the test file an open in read-mode correctly
    open_rule.apply(*tester);
    close_rule.apply(*tester);
    tester->assert_file_closed();
    open_read.apply(*tester);
    tester->assert_file_opened(tester->m_current_path);

    // Check that a read-mode file cannot flush
    flush_rule.apply(*tester);
}

// Ensure that Os::File properly refuses read calls when not open and when reading
TEST_F(Functionality, ReadInvalidModes) {
    Os::Test::FileTest::Tester::OpenFileCreate open_rule(false);
    Os::Test::FileTest::Tester::CloseFile close_rule;
    Os::Test::FileTest::Tester::OpenForWrite open_write;
    Os::Test::FileTest::Tester::ReadInvalidModes read_rule;

    // Test read in closed state
    read_rule.apply(*tester);
    tester->assert_file_closed();

    // Used to create the test file and ensure reads cannot happen in read-mode
    open_rule.apply(*tester);
    read_rule.apply(*tester);
    close_rule.apply(*tester);
    tester->assert_file_closed();

    // Used to open (now existent) file in write-mode
    open_write.apply(*tester);
    tester->assert_file_opened(tester->m_current_path);

    // Check that a read won't work on write-mode data
    read_rule.apply(*tester);
}

// Ensure that Os::File properly refuses write calls when not open and when reading
TEST_F(Functionality, WriteInvalidModes) {
    Os::Test::FileTest::Tester::OpenFileCreate open_rule(false);
    Os::Test::FileTest::Tester::CloseFile close_rule;
    Os::Test::FileTest::Tester::OpenForRead open_read;
    Os::Test::FileTest::Tester::WriteInvalidModes write_rule;

    // Test write in closed state
    write_rule.apply(*tester);
    tester->assert_file_closed();

    // Used to create the test file in read-mode correctly
    open_rule.apply(*tester);
    close_rule.apply(*tester);
    tester->assert_file_closed();
    open_read.apply(*tester);
    tester->assert_file_opened(tester->m_current_path);

    // Check that a write won't work on write-mode data
    write_rule.apply(*tester);
}

// Ensure a write followed by a read produces valid data
TEST_F(FunctionalIO, WriteSeekAppendReadBack) {
    Os::Test::FileTest::Tester::OpenFileCreate open_rule(false);
    Os::Test::FileTest::Tester::OpenForAppend open_append_rule(false);
    Os::Test::FileTest::Tester::Write write_rule;
    Os::Test::FileTest::Tester::Seek seek_rule;
    Os::Test::FileTest::Tester::CloseFile close_rule;
    Os::Test::FileTest::Tester::OpenForRead open_read;
    Os::Test::FileTest::Tester::Read read_rule;

    open_rule.apply(*tester);
    write_rule.apply(*tester);
    close_rule.apply(*tester);
    open_read.apply(*tester);
    close_rule.apply(*tester);
    open_append_rule.apply(*tester);
    write_rule.apply(*tester);
    seek_rule.apply(*tester);
    write_rule.apply(*tester);
    close_rule.apply(*tester);
    open_read.apply(*tester);
    read_rule.apply(*tester);
    close_rule.apply(*tester);
}

// Ensure a write followed by a read produces valid data
TEST_F(FunctionalIO, WriteReadBack) {
    Os::Test::FileTest::Tester::OpenFileCreate open_rule(false);
    Os::Test::FileTest::Tester::Write write_rule;
    Os::Test::FileTest::Tester::CloseFile close_rule;
    Os::Test::FileTest::Tester::OpenForRead open_read;
    Os::Test::FileTest::Tester::Read read_rule;

    open_rule.apply(*tester);
    write_rule.apply(*tester);
    close_rule.apply(*tester);
    open_read.apply(*tester);
    read_rule.apply(*tester);
}

// Ensure a write followed by a read produces valid data
TEST_F(FunctionalIO, WriteReadSeek) {
    Os::Test::FileTest::Tester::OpenFileCreate open_rule(false);
    Os::Test::FileTest::Tester::Write write_rule;
    Os::Test::FileTest::Tester::CloseFile close_rule;
    Os::Test::FileTest::Tester::OpenForRead open_read;
    Os::Test::FileTest::Tester::Read read_rule;
    Os::Test::FileTest::Tester::Seek seek_rule;

    open_rule.apply(*tester);
    write_rule.apply(*tester);
    close_rule.apply(*tester);
    open_read.apply(*tester);
    read_rule.apply(*tester);
    seek_rule.apply(*tester);
}

// Ensure a write followed by a full crc produces valid results
TEST_F(FunctionalIO, WriteFullCrc) {
    Os::Test::FileTest::Tester::OpenFileCreate open_rule(false);
    Os::Test::FileTest::Tester::Write write_rule;
    Os::Test::FileTest::Tester::CloseFile close_rule;
    Os::Test::FileTest::Tester::OpenForRead open_read;
    Os::Test::FileTest::Tester::FullCrc crc_rule;

    open_rule.apply(*tester);
    write_rule.apply(*tester);
    close_rule.apply(*tester);
    open_read.apply(*tester);
    crc_rule.apply(*tester);
}

// Ensure a write followed by a partial crc produces valid results
TEST_F(FunctionalIO, WritePartialCrc) {
    Os::Test::FileTest::Tester::OpenFileCreate open_rule(false);
    Os::Test::FileTest::Tester::Write write_rule;
    Os::Test::FileTest::Tester::CloseFile close_rule;
    Os::Test::FileTest::Tester::OpenForRead open_read;
    Os::Test::FileTest::Tester::IncrementalCrc crc_rule;
    Os::Test::FileTest::Tester::FinalizeCrc finalize_rule;

    open_rule.apply(*tester);
    write_rule.apply(*tester);
    close_rule.apply(*tester);
    open_read.apply(*tester);
    crc_rule.apply(*tester);
    finalize_rule.apply(*tester);
}

// Ensure a preallocate produces valid sizes
TEST_F(FunctionalIO, Flush) {
    Os::Test::FileTest::Tester::OpenFileCreate open_rule(false);
    Os::Test::FileTest::Tester::Write write_rule;
    Os::Test::FileTest::Tester::Flush flush_rule;

    open_rule.apply(*tester);
    write_rule.apply(*tester);
    flush_rule.apply(*tester);
}

// Ensure a preallocate produces valid sizes
TEST_F(FunctionalIO, Preallocate) {
    Os::Test::FileTest::Tester::OpenFileCreate open_rule(false);
    Os::Test::FileTest::Tester::Preallocate preallocate_rule;

    open_rule.apply(*tester);
    preallocate_rule.apply(*tester);
}

// Randomized testing on the interfaces
TEST_F(Functionality, RandomizedInterfaceTesting) {
    // Enumerate all rules and construct an instance of each
    Os::Test::FileTest::Tester::OpenFileCreateOverwrite open_file_create_overwrite_rule(true);
    Os::Test::FileTest::Tester::CloseFile close_file_rule;
    Os::Test::FileTest::Tester::CopyConstruction copy_construction;
    Os::Test::FileTest::Tester::CopyAssignment copy_assignment;
    Os::Test::FileTest::Tester::OpenInvalidModes open_invalid_modes_rule;
    Os::Test::FileTest::Tester::PreallocateWithoutOpen preallocate_without_open_rule;
    Os::Test::FileTest::Tester::SeekWithoutOpen seek_without_open_rule;
    Os::Test::FileTest::Tester::FlushInvalidModes flush_invalid_modes_rule;
    Os::Test::FileTest::Tester::ReadInvalidModes read_invalid_modes_rule;
    Os::Test::FileTest::Tester::WriteInvalidModes write_invalid_modes_rule;
    Os::Test::FileTest::Tester::OpenIllegalPath open_illegal_path;
    Os::Test::FileTest::Tester::OpenIllegalMode open_illegal_mode;
    Os::Test::FileTest::Tester::SeekIllegal seek_illegal;
    Os::Test::FileTest::Tester::ReadIllegalBuffer read_illegal_buffer;
    Os::Test::FileTest::Tester::WriteIllegalBuffer write_illegal_buffer;
    Os::Test::FileTest::Tester::IncrementalCrcInvalidModes incremental_invalid_mode_rule;
    Os::Test::FileTest::Tester::FullCrcInvalidModes full_invalid_mode_rule;
    Os::Test::FileTest::Tester::OpenFileCreateBounded open_file_create_bounded_rule(true);
    Os::Test::FileTest::Tester::OpenFileCreateString open_file_create_string_rule(true);

    // Place these rules into a list of rules
    STest::Rule<Os::Test::FileTest::Tester>* rules[] = {&open_file_create_overwrite_rule,
                                                        &close_file_rule,
                                                        &copy_assignment,
                                                        &copy_construction,
                                                        &open_invalid_modes_rule,
                                                        &preallocate_without_open_rule,
                                                        &seek_without_open_rule,
                                                        &flush_invalid_modes_rule,
                                                        &read_invalid_modes_rule,
                                                        &write_invalid_modes_rule,
                                                        &open_illegal_path,
                                                        &open_illegal_mode,
                                                        &seek_illegal,
                                                        &read_illegal_buffer,
                                                        &write_illegal_buffer,
                                                        &incremental_invalid_mode_rule,
                                                        &full_invalid_mode_rule,
                                                        &open_file_create_bounded_rule,
                                                        &open_file_create_string_rule};

    // Take the rules and place them into a random scenario
    STest::RandomScenario<Os::Test::FileTest::Tester> random("Random Rules", rules, FW_NUM_ARRAY_ELEMENTS(rules));

    // Create a bounded scenario wrapping the random scenario
    STest::BoundedScenario<Os::Test::FileTest::Tester> bounded("Bounded Random Rules Scenario", random,
                                                               RANDOM_BOUND / 10);
    // Run!
    const U32 numSteps = bounded.run(*tester);
    printf("Ran %u steps.\n", numSteps);
}

// Ensure a write followed by a read produces valid data
TEST_F(FunctionalIO, RandomizedTesting) {
    // Enumerate all rules and construct an instance of each
    Os::Test::FileTest::Tester::OpenFileCreate open_file_create_rule(true);
    Os::Test::FileTest::Tester::OpenFileCreateOverwrite open_file_create_overwrite_rule(true);
    Os::Test::FileTest::Tester::OpenForWrite open_for_write_rule(true);
    Os::Test::FileTest::Tester::OpenForRead open_for_read_rule(true);
    Os::Test::FileTest::Tester::CloseFile close_file_rule;
    Os::Test::FileTest::Tester::Read read_rule;
    Os::Test::FileTest::Tester::Write write_rule;
    Os::Test::FileTest::Tester::Seek seek_rule;
    Os::Test::FileTest::Tester::Preallocate preallocate_rule;
    Os::Test::FileTest::Tester::Flush flush_rule;
    Os::Test::FileTest::Tester::CopyConstruction copy_construction;
    Os::Test::FileTest::Tester::CopyAssignment copy_assignment;
    Os::Test::FileTest::Tester::IncrementalCrc incremental_crc_rule;
    Os::Test::FileTest::Tester::FinalizeCrc finalize_crc_rule;
    Os::Test::FileTest::Tester::FullCrc full_crc_rule;
    Os::Test::FileTest::Tester::OpenInvalidModes open_invalid_modes_rule;
    Os::Test::FileTest::Tester::PreallocateWithoutOpen preallocate_without_open_rule;
    Os::Test::FileTest::Tester::SeekWithoutOpen seek_without_open_rule;
    Os::Test::FileTest::Tester::SeekInvalidSize seek_invalid_size;
    Os::Test::FileTest::Tester::FlushInvalidModes flush_invalid_modes_rule;
    Os::Test::FileTest::Tester::ReadInvalidModes read_invalid_modes_rule;
    Os::Test::FileTest::Tester::WriteInvalidModes write_invalid_modes_rule;
    Os::Test::FileTest::Tester::IncrementalCrcInvalidModes incremental_invalid_mode_rule;
    Os::Test::FileTest::Tester::FullCrcInvalidModes full_invalid_mode_rule;
    Os::Test::FileTest::Tester::OpenFileCreateBounded open_file_create_bounded_rule(true);
    Os::Test::FileTest::Tester::OpenFileCreateString open_file_create_string_rule(true);

    // Place these rules into a list of rules
    STest::Rule<Os::Test::FileTest::Tester>* rules[] = {&open_file_create_rule,
                                                        &open_file_create_overwrite_rule,
                                                        &open_for_write_rule,
                                                        &open_for_read_rule,
                                                        &close_file_rule,
                                                        &copy_assignment,
                                                        &copy_construction,
                                                        &read_rule,
                                                        &write_rule,
                                                        &seek_rule,
                                                        &preallocate_rule,
                                                        &flush_rule,
                                                        &incremental_crc_rule,
                                                        &finalize_crc_rule,
                                                        &full_crc_rule,
                                                        &open_invalid_modes_rule,
                                                        &preallocate_without_open_rule,
                                                        &seek_without_open_rule,
                                                        &seek_invalid_size,
                                                        &flush_invalid_modes_rule,
                                                        &read_invalid_modes_rule,
                                                        &write_invalid_modes_rule,
                                                        &incremental_invalid_mode_rule,
                                                        &full_invalid_mode_rule,
                                                        &open_file_create_bounded_rule,
                                                        &open_file_create_string_rule};

    // Take the rules and place them into a random scenario
    STest::RandomScenario<Os::Test::FileTest::Tester> random("Random Rules", rules, FW_NUM_ARRAY_ELEMENTS(rules));

    // Create a bounded scenario wrapping the random scenario
    STest::BoundedScenario<Os::Test::FileTest::Tester> bounded("Bounded Random Rules Scenario", random, RANDOM_BOUND);
    // Run!
    const U32 numSteps = bounded.run(*tester);
    printf("Ran %u steps.\n", numSteps);
}

// Ensure that Os::File properly refuses fullCrc when not in write mode
TEST_F(Functionality, FullCrcInvalidMode) {
    Os::Test::FileTest::Tester::OpenFileCreate open_rule(false);
    Os::Test::FileTest::Tester::FullCrcInvalidModes rule;
    open_rule.apply(*tester);
    rule.apply(*tester);
}

// Ensure that Os::File properly refuses incrementalCrc when not in write mode
TEST_F(Functionality, IncrementalCrcInvalidMode) {
    Os::Test::FileTest::Tester::OpenFileCreate open_rule(false);
    Os::Test::FileTest::Tester::IncrementalCrcInvalidModes rule;
    open_rule.apply(*tester);
    rule.apply(*tester);
}

// Ensure open prevents nullptr as path
TEST_F(InvalidArguments, OpenBadPath) {
    Os::Test::FileTest::Tester::OpenIllegalPath rule;
    rule.apply(*tester);
}

// Ensure open prevents bad modes
TEST_F(InvalidArguments, OpenBadMode) {
    Os::Test::FileTest::Tester::OpenIllegalMode rule;
    rule.apply(*tester);
}

// Ensure preallocate prevents bad length
TEST_F(InvalidArguments, SeekAbsoluteWithNegativeLength) {
    Os::Test::FileTest::Tester::SeekIllegal rule;
    rule.apply(*tester);
}

// Ensure read prevents bad buffer pointers
TEST_F(InvalidArguments, ReadInvalidBuffer) {
    Os::Test::FileTest::Tester::ReadIllegalBuffer rule;
    rule.apply(*tester);
}

// Ensure write prevents bad buffer pointers
TEST_F(InvalidArguments, WriteInvalidBuffer) {
    Os::Test::FileTest::Tester::WriteIllegalBuffer rule;
    rule.apply(*tester);
}

// ======================================================================
// Hardcoded CRC32 value tests
//
// These tests verify that Os::File CRC calculations produce known expected
// values. Os::File uses raw CRC32 accumulation (init 0xFFFFFFFF, no final
// ones' complement), so the "finalized" CRC is the raw accumulator value.
// ======================================================================

// Verify CRC32 of known data "123456789" against hardcoded expected value
TEST_F(FunctionalIO, CrcHardcodedValue_123456789) {
    // Write known data to file
    const char data[] = "123456789";
    const FwSizeType dataLen = 9;

    Os::Test::FileTest::Tester::OpenFileCreate open_rule(false);
    open_rule.apply(*tester);

    // Write data directly through the file handle
    FwSizeType writeSize = dataLen;
    Os::File::Status status = tester->m_file.write(reinterpret_cast<const U8*>(data), writeSize);
    ASSERT_EQ(status, Os::File::Status::OP_OK);
    ASSERT_EQ(writeSize, dataLen);

    // Close and reopen for read
    tester->m_file.close();
    status = tester->m_file.open(tester->m_current_path.c_str(), Os::File::OPEN_READ);
    ASSERT_EQ(status, Os::File::Status::OP_OK);

    // Calculate CRC
    U32 crc = 0;
    status = tester->m_file.calculateCrc(crc);
    ASSERT_EQ(status, Os::File::Status::OP_OK);

    // Os::File returns raw accumulator (no ones' complement)
    // Expected: CRC32 raw accumulator of "123456789" = 0x340BC6D9
    ASSERT_EQ(crc, static_cast<U32>(0x340BC6D9))
        << "Os::File CRC of \"123456789\" must equal 0x340BC6D9 (raw accumulator, no ones' complement)";

    tester->m_file.close();
}

// Verify CRC32 of {0xDE, 0xAD, 0xBE, 0xEF} against hardcoded expected value
TEST_F(FunctionalIO, CrcHardcodedValue_DEADBEEF) {
    const U8 data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    const FwSizeType dataLen = 4;

    Os::Test::FileTest::Tester::OpenFileCreate open_rule(false);
    open_rule.apply(*tester);

    FwSizeType writeSize = dataLen;
    Os::File::Status status = tester->m_file.write(data, writeSize);
    ASSERT_EQ(status, Os::File::Status::OP_OK);

    tester->m_file.close();
    status = tester->m_file.open(tester->m_current_path.c_str(), Os::File::OPEN_READ);
    ASSERT_EQ(status, Os::File::Status::OP_OK);

    U32 crc = 0;
    status = tester->m_file.calculateCrc(crc);
    ASSERT_EQ(status, Os::File::Status::OP_OK);

    // Expected: CRC32 raw accumulator of {0xDE,0xAD,0xBE,0xEF} = 0x83635CA5
    ASSERT_EQ(crc, static_cast<U32>(0x83635CA5)) << "Os::File CRC of {0xDE,0xAD,0xBE,0xEF} must equal 0x83635CA5";

    tester->m_file.close();
}

// Verify incremental CRC produces the same hardcoded value
TEST_F(FunctionalIO, CrcHardcodedValue_Incremental) {
    const char data[] = "123456789";
    const FwSizeType dataLen = 9;

    Os::Test::FileTest::Tester::OpenFileCreate open_rule(false);
    open_rule.apply(*tester);

    FwSizeType writeSize = dataLen;
    Os::File::Status status = tester->m_file.write(reinterpret_cast<const U8*>(data), writeSize);
    ASSERT_EQ(status, Os::File::Status::OP_OK);

    tester->m_file.close();
    status = tester->m_file.open(tester->m_current_path.c_str(), Os::File::OPEN_READ);
    ASSERT_EQ(status, Os::File::Status::OP_OK);

    // Use incremental CRC: read 4 bytes, then 5 bytes
    FwSizeType size = 4;
    status = tester->m_file.incrementalCrc(size);
    ASSERT_EQ(status, Os::File::Status::OP_OK);
    ASSERT_EQ(size, static_cast<FwSizeType>(4));

    size = 5;
    status = tester->m_file.incrementalCrc(size);
    ASSERT_EQ(status, Os::File::Status::OP_OK);
    ASSERT_EQ(size, static_cast<FwSizeType>(5));

    U32 crc = 0;
    status = tester->m_file.finalizeCrc(crc);
    ASSERT_EQ(status, Os::File::Status::OP_OK);

    // Same expected value as full CRC
    ASSERT_EQ(crc, static_cast<U32>(0x340BC6D9)) << "Incremental CRC of \"123456789\" must equal 0x340BC6D9";

    tester->m_file.close();
}

// ======================================================================
// readline and seek_absolute
//
// Both had no coverage at all. readline is 48 lines of parsing logic in
// Os/File.cpp -- a chunked read loop, a newline scan, and a contract about
// where the file is left on each exit path -- and its only caller in the tree
// is Os/Linux/Cpu.cpp. seek_absolute has no caller outside Os/File.cpp and is
// reachable from the suite only through readline.
//
// These are written against the interface, so they run for any implementation
// that supports functional i/o. They pin the behaviour that exists rather than
// the behaviour one might expect: where the two differ, the comment says so.
// ======================================================================

//! Create the test file, fill it with `content`, and reopen it for reading.
static void write_then_reopen_for_read(Os::Test::FileTest::Tester& tester,
                                       const U8* content,
                                       FwSizeType length) {
    Os::Test::FileTest::Tester::OpenFileCreate open_rule(false);
    open_rule.apply(tester);

    FwSizeType written = length;
    ASSERT_EQ(Os::File::Status::OP_OK, tester.m_file.write(content, written));
    ASSERT_EQ(written, length);

    tester.m_file.close();
    ASSERT_EQ(Os::File::Status::OP_OK,
              tester.m_file.open(tester.m_current_path.c_str(), Os::File::OPEN_READ));
}

static void write_then_reopen_for_read(Os::Test::FileTest::Tester& tester, const char* content) {
    write_then_reopen_for_read(tester, reinterpret_cast<const U8*>(content),
                               static_cast<FwSizeType>(::strlen(content)));
}

// A line is returned up to and including its newline, and the file is left
// positioned just after it -- so consecutive calls walk the file.
TEST_F(FunctionalIO, ReadlineReturnsLinesInOrder) {
    write_then_reopen_for_read(*tester, "ab\ncde\n");

    U8 buffer[32];
    FwSizeType size = sizeof(buffer);
    ASSERT_EQ(Os::File::Status::OP_OK, tester->m_file.readline(buffer, size, Os::File::WaitType::NO_WAIT));
    ASSERT_EQ(size, static_cast<FwSizeType>(3));
    ASSERT_EQ(0, ::memcmp(buffer, "ab\n", 3));

    FwSizeType position = 0;
    ASSERT_EQ(Os::File::Status::OP_OK, tester->m_file.position(position));
    ASSERT_EQ(position, static_cast<FwSizeType>(3)) << "left just past the newline";

    size = sizeof(buffer);
    ASSERT_EQ(Os::File::Status::OP_OK, tester->m_file.readline(buffer, size, Os::File::WaitType::NO_WAIT));
    ASSERT_EQ(size, static_cast<FwSizeType>(4));
    ASSERT_EQ(0, ::memcmp(buffer, "cde\n", 4));
    tester->m_file.close();
}

// A newline in the first byte yields a one-byte line, not an empty one.
TEST_F(FunctionalIO, ReadlineOnALeadingNewlineReturnsOneByte) {
    write_then_reopen_for_read(*tester, "\nrest\n");

    U8 buffer[32];
    FwSizeType size = sizeof(buffer);
    ASSERT_EQ(Os::File::Status::OP_OK, tester->m_file.readline(buffer, size, Os::File::WaitType::NO_WAIT));
    ASSERT_EQ(size, static_cast<FwSizeType>(1));
    ASSERT_EQ(buffer[0], static_cast<U8>('\n'));
    tester->m_file.close();
}

// Running out of file before finding a newline is OP_OK with however much was
// read -- NOT an error, and NOT a restored position. This is the one exit path
// in readline that does not seek back, which is worth pinning precisely
// because it is the asymmetric one.
TEST_F(FunctionalIO, ReadlineAtEndOfFileReturnsWhatItRead) {
    write_then_reopen_for_read(*tester, "tail");

    U8 buffer[32];
    FwSizeType size = sizeof(buffer);
    ASSERT_EQ(Os::File::Status::OP_OK, tester->m_file.readline(buffer, size, Os::File::WaitType::NO_WAIT));
    ASSERT_EQ(size, static_cast<FwSizeType>(4)) << "an unterminated final line is still returned";
    ASSERT_EQ(0, ::memcmp(buffer, "tail", 4));

    FwSizeType position = 0;
    ASSERT_EQ(Os::File::Status::OP_OK, tester->m_file.position(position));
    ASSERT_EQ(position, static_cast<FwSizeType>(4))
        << "the end-of-file path does not restore the original position";
    tester->m_file.close();
}

// No newline within the bytes the caller allowed: OTHER_ERROR, size zeroed,
// and the position put back where it started so the caller can retry larger.
TEST_F(FunctionalIO, ReadlineWithNoNewlineInRangeRestoresThePosition) {
    write_then_reopen_for_read(*tester, "abcdefgh\n");

    U8 buffer[4];
    FwSizeType size = 3;  // "abc" -- no newline inside the allowance
    ASSERT_EQ(Os::File::Status::OTHER_ERROR, tester->m_file.readline(buffer, size, Os::File::WaitType::NO_WAIT));
    ASSERT_EQ(size, static_cast<FwSizeType>(0)) << "size is zeroed on the error path";

    FwSizeType position = 0;
    ASSERT_EQ(Os::File::Status::OP_OK, tester->m_file.position(position));
    ASSERT_EQ(position, static_cast<FwSizeType>(0)) << "the contract is to seek back";
    tester->m_file.close();
}

// A request for zero bytes never enters the loop and falls through to the
// no-newline-found path, so it is an error rather than an empty success.
TEST_F(FunctionalIO, ReadlineOfZeroBytesIsAnError) {
    write_then_reopen_for_read(*tester, "abc\n");

    U8 buffer[4];
    FwSizeType size = 0;
    ASSERT_EQ(Os::File::Status::OTHER_ERROR, tester->m_file.readline(buffer, size, Os::File::WaitType::NO_WAIT));
    ASSERT_EQ(size, static_cast<FwSizeType>(0));
    tester->m_file.close();
}

// The read loop works in FW_FILE_CHUNK_SIZE pieces, so a newline that lands
// past the first chunk exercises the second iteration and the `i` offset
// arithmetic that goes with it.
TEST_F(FunctionalIO, ReadlineFindsANewlineBeyondTheFirstChunk) {
    const FwSizeType filler = static_cast<FwSizeType>(FW_FILE_CHUNK_SIZE) + 10;
    std::vector<U8> content(filler, static_cast<U8>('x'));
    content.push_back(static_cast<U8>('\n'));
    content.push_back(static_cast<U8>('z'));
    write_then_reopen_for_read(*tester, content.data(), static_cast<FwSizeType>(content.size()));

    std::vector<U8> buffer(content.size() + 16, 0);
    FwSizeType size = static_cast<FwSizeType>(buffer.size());
    ASSERT_EQ(Os::File::Status::OP_OK, tester->m_file.readline(buffer.data(), size, Os::File::WaitType::NO_WAIT));
    ASSERT_EQ(size, filler + 1) << "the line runs to the newline in the second chunk";

    FwSizeType position = 0;
    ASSERT_EQ(Os::File::Status::OP_OK, tester->m_file.position(position));
    ASSERT_EQ(position, filler + 1);
    tester->m_file.close();
}

// Mode checks come before anything touches the file.
TEST_F(Functionality, ReadlineOnAClosedFileIsNotOpened) {
    U8 buffer[8];
    FwSizeType size = sizeof(buffer);
    ASSERT_EQ(Os::File::Status::NOT_OPENED, tester->m_file.readline(buffer, size, Os::File::WaitType::NO_WAIT));
    ASSERT_EQ(size, static_cast<FwSizeType>(0));
}

TEST_F(FunctionalIO, ReadlineOnAFileOpenedForWriteIsInvalidMode) {
    Os::Test::FileTest::Tester::OpenFileCreate open_rule(false);
    open_rule.apply(*tester);

    U8 buffer[8];
    FwSizeType size = sizeof(buffer);
    ASSERT_EQ(Os::File::Status::INVALID_MODE, tester->m_file.readline(buffer, size, Os::File::WaitType::NO_WAIT));
    ASSERT_EQ(size, static_cast<FwSizeType>(0));
    tester->m_file.close();
}

// seek_absolute is reachable from the suite only through readline; this is the
// only direct exercise of it.
TEST_F(FunctionalIO, SeekAbsoluteMovesToTheRequestedOffset) {
    write_then_reopen_for_read(*tester, "0123456789");

    ASSERT_EQ(Os::File::Status::OP_OK, tester->m_file.seek_absolute(6));
    FwSizeType position = 0;
    ASSERT_EQ(Os::File::Status::OP_OK, tester->m_file.position(position));
    ASSERT_EQ(position, static_cast<FwSizeType>(6));

    U8 byte = 0;
    FwSizeType size = 1;
    ASSERT_EQ(Os::File::Status::OP_OK, tester->m_file.read(&byte, size));
    ASSERT_EQ(byte, static_cast<U8>('6')) << "seeking absolute lands on the byte at that index";

    // Seeking backwards is equally absolute, not relative.
    ASSERT_EQ(Os::File::Status::OP_OK, tester->m_file.seek_absolute(1));
    ASSERT_EQ(Os::File::Status::OP_OK, tester->m_file.position(position));
    ASSERT_EQ(position, static_cast<FwSizeType>(1));
    tester->m_file.close();
}
