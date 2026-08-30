/*
 * ComPacket.cpp
 *
 *  Created on: May 24, 2014
 *      Author: Timothy Canham
 */

#include <Fw/Com/ComPacket.hpp>

namespace Fw {

ComPacket::ComPacket() : m_type(ComPacketType::FW_PACKET_UNKNOWN) {}

ComPacket::~ComPacket() {}

SerializeStatus ComPacket::serializeBase(SerialBufferBase& buffer) const {
    return buffer.serializeFrom(static_cast<FwPacketDescriptorType>(this->m_type));
}

SerializeStatus ComPacket::deserializeBase(SerialBufferBase& buffer) {
    FwPacketDescriptorType serVal;
    SerializeStatus stat = buffer.deserializeTo(serVal);
    if (FW_SERIALIZE_OK == stat) {
        // The descriptor arrives from the wire, so it is attacker-controlled on
        // an uplink. ComPacketType is ComCfg::Apid::T, a plain enum whose
        // largest enumerator is 0x800, giving it a value range of [0, 0xFFF];
        // FwPacketDescriptorType is wider, so casting an out-of-range value is
        // undefined behaviour rather than merely a wrong answer. The generated
        // enum class supplies exactly the check needed, and its own
        // constructors already assert on it -- storing the raw enum here is
        // what bypassed it.
        if (!ComCfg::Apid::isValid(serVal)) {
            // TYPE_MISMATCH rather than FORMAT_ERROR, deliberately. Every
            // derived packet that checks its own descriptor already answers
            // TYPE_MISMATCH for one it does not recognise, and the command
            // dispatcher telemeters that status verbatim as MalformedCommand's
            // only argument. Reporting the same code keeps this a pure removal
            // of undefined behaviour rather than a change ground software can
            // see. The one packet whose behaviour does move is LogPacket,
            // which had no descriptor check of its own and so used to accept
            // an unrecognised one outright.
            return FW_DESERIALIZE_TYPE_MISMATCH;
        }
        this->m_type = static_cast<ComPacketType>(serVal);
    }
    return stat;
}

} /* namespace Fw */
