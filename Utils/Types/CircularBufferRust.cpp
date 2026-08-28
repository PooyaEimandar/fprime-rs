/*
 * CircularBufferRust.cpp:
 *
 * Rust-backed implementation of Types::CircularBuffer (fprime-rs Phase 1).
 * The class layout, FW_ASSERT preconditions, and the Fw::Serializable
 * template paths (which call C++ virtuals) stay here; every raw byte
 * operation on the ring delegates to the fprime-rs engine
 * (Rust/fprime-utils/src/circular.rs). Selected instead of
 * CircularBuffer.cpp via FPRIME_RUST_IMPL_UTILS_TYPES; behavior is
 * identical, enforced by the existing unit-test suite.
 */
#include <Fw/FPrimeBasicTypes.hpp>
#include <Fw/Types/Assert.hpp>
#include <Utils/Types/CircularBuffer.hpp>

#include <Rust/RustUtils.h>

namespace Types {

CircularBuffer ::CircularBuffer()
    : m_store(nullptr), m_store_size(0), m_head_idx(0), m_allocated_size(0), m_high_water_mark(0) {}

CircularBuffer ::CircularBuffer(U8* const buffer, const FwSizeType size)
    : m_store(nullptr), m_store_size(0), m_head_idx(0), m_allocated_size(0), m_high_water_mark(0) {
    setup(buffer, size);
}

void CircularBuffer ::setup(U8* const buffer, const FwSizeType size) {
    FW_ASSERT(size > 0);
    FW_ASSERT(buffer != nullptr);
    FW_ASSERT(m_store == nullptr && m_store_size == 0);  // Not already setup

    // Initialize buffer data
    m_store = buffer;
    m_store_size = size;
    m_head_idx = 0;
    m_allocated_size = 0;
    m_high_water_mark = 0;
}

FwSizeType CircularBuffer ::get_allocated_size() const {
    return m_allocated_size;
}

FwSizeType CircularBuffer ::get_free_size() const {
    FW_ASSERT(m_store != nullptr && m_store_size != 0);  // setup method was called
    FW_ASSERT(m_allocated_size <= m_store_size, static_cast<FwAssertArgType>(m_allocated_size));
    return m_store_size - m_allocated_size;
}

FwSizeType CircularBuffer ::advance_idx(FwSizeType idx, FwSizeType amount) const {
    FW_ASSERT(idx < m_store_size, static_cast<FwAssertArgType>(idx));
    return (idx + amount) % m_store_size;
}

Fw::SerializeStatus CircularBuffer ::serialize(const U8* const buffer, const FwSizeType size) {
    FW_ASSERT(m_store != nullptr && m_store_size != 0);  // setup method was called
    FW_ASSERT(buffer != nullptr);
    // Rust slice aliasing rules forbid the source overlapping the ring store
    FW_ASSERT(buffer + size <= m_store || m_store + m_store_size <= buffer);
    return static_cast<Fw::SerializeStatus>(fprime_rs_cb_serialize(m_store, m_store_size, &m_head_idx,
                                                                   &m_allocated_size, &m_high_water_mark, buffer,
                                                                   size));
}

template <typename T>
Fw::SerializeStatus CircularBuffer ::serialize_impl(const T& serializable, const FwSizeType size) {
    FW_ASSERT(m_store != nullptr && m_store_size != 0);  // setup method was called
    // Check there is sufficient space
    if (size > get_free_size()) {
        return Fw::FW_SERIALIZE_NO_ROOM_LEFT;
    }
    const FwSizeType idx = advance_idx(m_head_idx, m_allocated_size);
    // Wrapping slot: serialize into a stack staging buffer, then byte-copy in with wrap-around
    if ((idx + size) > m_store_size) {
        FW_ASSERT(size <= STAGING_BUFFER_SIZE, static_cast<FwAssertArgType>(size),
                  static_cast<FwAssertArgType>(STAGING_BUFFER_SIZE));
        U8 staging[STAGING_BUFFER_SIZE > 0 ? STAGING_BUFFER_SIZE : 1] = {};
        Fw::ExternalSerializeBuffer stagingBuffer(staging, size);
        const Fw::SerializeStatus stagingStatus = stagingBuffer.serializeFrom(serializable);
        if (stagingStatus != Fw::FW_SERIALIZE_OK) {
            return stagingStatus;
        }
        return this->serialize(staging, size);
    }
    // Linear slot: serialize directly into the store; the Rust engine does the bookkeeping
    Fw::ExternalSerializeBuffer slot(&m_store[idx], size);
    const Fw::SerializeStatus status = slot.serializeFrom(serializable);
    if (status != Fw::FW_SERIALIZE_OK) {
        return status;
    }
    (void)fprime_rs_cb_commit(m_store, m_store_size, &m_head_idx, &m_allocated_size, &m_high_water_mark, size);
    FW_ASSERT(m_allocated_size <= this->get_capacity(), static_cast<FwAssertArgType>(m_allocated_size));
    return Fw::FW_SERIALIZE_OK;
}

Fw::SerializeStatus CircularBuffer ::serialize(const Fw::Serializable& serializable, const FwSizeType size) {
    return this->serialize_impl(serializable, size);
}

Fw::SerializeStatus CircularBuffer ::serialize(const Fw::LinearBufferBase& buffer, const FwSizeType size) {
    return this->serialize_impl(buffer, size);
}

Fw::SerializeStatus CircularBuffer ::peek(char& value, FwSizeType offset) const {
    FW_ASSERT(m_store != nullptr && m_store_size != 0);  // setup method was called
    return peek(reinterpret_cast<U8&>(value), offset);
}

Fw::SerializeStatus CircularBuffer ::peek(U8& value, FwSizeType offset) const {
    FW_ASSERT(m_store != nullptr && m_store_size != 0);  // setup method was called
    return static_cast<Fw::SerializeStatus>(
        fprime_rs_cb_peek_u8(m_store, m_store_size, m_head_idx, m_allocated_size, offset, &value));
}

Fw::SerializeStatus CircularBuffer ::peek(U32& value, FwSizeType offset) const {
    FW_ASSERT(m_store != nullptr && m_store_size != 0);  // setup method was called
    return static_cast<Fw::SerializeStatus>(
        fprime_rs_cb_peek_u32(m_store, m_store_size, m_head_idx, m_allocated_size, offset, &value));
}

Fw::SerializeStatus CircularBuffer ::peek(U8* buffer, FwSizeType size, FwSizeType offset) const {
    FW_ASSERT(m_store != nullptr && m_store_size != 0);  // setup method was called
    FW_ASSERT(buffer != nullptr);
    // Rust slice aliasing rules forbid the destination overlapping the ring store
    FW_ASSERT(buffer + size <= m_store || m_store + m_store_size <= buffer);
    return static_cast<Fw::SerializeStatus>(
        fprime_rs_cb_peek_bytes(m_store, m_store_size, m_head_idx, m_allocated_size, offset, buffer, size));
}

template <typename T>
Fw::SerializeStatus CircularBuffer ::peek_impl(T& serializable, FwSizeType size, FwSizeType offset) const {
    FW_ASSERT(m_store != nullptr && m_store_size != 0);  // setup method was called
    // Check there is sufficient data
    if ((size + offset) > m_allocated_size) {
        return Fw::FW_DESERIALIZE_BUFFER_EMPTY;
    }
    const FwSizeType idx = advance_idx(m_head_idx, offset);
    // Wrapping slot: byte-copy out with wrap-around into a stack staging buffer, then deserialize
    if ((idx + size) > m_store_size) {
        FW_ASSERT(size <= STAGING_BUFFER_SIZE, static_cast<FwAssertArgType>(size),
                  static_cast<FwAssertArgType>(STAGING_BUFFER_SIZE));
        U8 staging[STAGING_BUFFER_SIZE > 0 ? STAGING_BUFFER_SIZE : 1];
        const Fw::SerializeStatus peekStatus = this->peek(staging, size, offset);
        if (peekStatus != Fw::FW_SERIALIZE_OK) {
            return peekStatus;
        }
        Fw::ExternalSerializeBuffer stagingBuffer(staging, size);
        const Fw::SerializeStatus status = stagingBuffer.setBuffLen(size);
        FW_ASSERT(status == Fw::FW_SERIALIZE_OK, static_cast<FwAssertArgType>(status));
        return stagingBuffer.deserializeTo(serializable);
    }
    // Linear slot: deserialize directly from the store
    Fw::ExternalSerializeBuffer slot(&m_store[idx], size);
    Fw::SerializeStatus status = slot.setBuffLen(size);
    FW_ASSERT(status == Fw::FW_SERIALIZE_OK, static_cast<FwAssertArgType>(status));
    return slot.deserializeTo(serializable);
}

Fw::SerializeStatus CircularBuffer ::peek(Fw::Serializable& serializable, FwSizeType size, FwSizeType offset) const {
    return this->peek_impl(serializable, size, offset);
}

Fw::SerializeStatus CircularBuffer ::peek(Fw::LinearBufferBase& buffer, FwSizeType size, FwSizeType offset) const {
    return this->peek_impl(buffer, size, offset);
}

Fw::SerializeStatus CircularBuffer ::rotate(FwSizeType amount) {
    FW_ASSERT(m_store != nullptr && m_store_size != 0);  // setup method was called
    return static_cast<Fw::SerializeStatus>(
        fprime_rs_cb_rotate(m_store, m_store_size, &m_head_idx, &m_allocated_size, &m_high_water_mark, amount));
}

Fw::SerializeStatus CircularBuffer ::trim(FwSizeType amount) {
    FW_ASSERT(m_store != nullptr && m_store_size != 0);  // setup method was called
    return static_cast<Fw::SerializeStatus>(
        fprime_rs_cb_trim(m_store, m_store_size, &m_head_idx, &m_allocated_size, &m_high_water_mark, amount));
}

FwSizeType CircularBuffer ::get_capacity() const {
    FW_ASSERT(m_store != nullptr && m_store_size != 0);  // setup method was called
    return m_store_size;
}

FwSizeType CircularBuffer ::get_high_water_mark() const {
    return m_high_water_mark;
}

void CircularBuffer ::clear_high_water_mark() {
    m_high_water_mark = 0;
}

}  // End Namespace Types
