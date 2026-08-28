//! IEEE 802.3 CRC32 update, behaviorally identical to
//! `Utils::crc32_ieee802_3_update` in `Utils/Hash/Crc32/Crc32.cpp`.
//!
//! Like the C++ function, this updates a raw reflected CRC register (poly
//! 0xEDB88320) and performs NO initial or final one's complement — the `Hash`
//! class owns those conventions. The C++ uses a slice-by-4 table optimization;
//! byte-at-a-time Sarwate over the same tables computes the identical
//! function, so outputs are bit-identical.

const fn make_table() -> [u32; 256] {
    let mut table = [0u32; 256];
    let mut i = 0;
    while i < 256 {
        let mut crc = i as u32;
        let mut bit = 0;
        while bit < 8 {
            crc = if crc & 1 != 0 {
                (crc >> 1) ^ 0xEDB8_8320
            } else {
                crc >> 1
            };
            bit += 1;
        }
        table[i] = crc;
        i += 1;
    }
    table
}

static TABLE: [u32; 256] = make_table();

/// Update `crc` over `data`. Raw register semantics: seed with `!0` and
/// complement the result yourself to get a conventional CRC-32.
pub fn update(mut crc: u32, data: &[u8]) -> u32 {
    for &byte in data {
        crc = (crc >> 8) ^ TABLE[((crc ^ byte as u32) & 0xFF) as usize];
    }
    crc
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn standard_check_value() {
        // CRC-32/ISO-HDLC check: crc32("123456789") == 0xCBF43926 with the
        // conventional init/final complement the Hash class applies.
        let raw = update(0xFFFF_FFFF, b"123456789");
        assert_eq!(!raw, 0xCBF4_3926);
    }

    #[test]
    fn incremental_equals_oneshot() {
        let data = b"The quick brown fox jumps over the lazy dog";
        let oneshot = update(0xFFFF_FFFF, data);
        let split = update(update(0xFFFF_FFFF, &data[..17]), &data[17..]);
        assert_eq!(oneshot, split);
    }

    #[test]
    fn empty_input_is_identity() {
        assert_eq!(update(0x1234_5678, &[]), 0x1234_5678);
    }
}
