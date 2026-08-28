//! The CFDP 32-bit checksum from `CFDP/Checksum/Checksum.cpp`: file bytes are
//! summed as big-endian 32-bit words aligned to the file (not buffer) offset,
//! with plain wrapping addition.
//!
//! The arithmetic mirrors the C++ exactly, including its `U8` truncation of
//! `offset + index` when computing word position (harmless: only the value
//! mod 4 is used, which u8 truncation preserves).

/// Mirrors `Checksum::update(data, offset, length)`. `data` must hold at
/// least `length` bytes; `offset` is the file offset of `data[0]`.
pub fn update(value: &mut u32, data: &[u8], offset: u32) {
    let length = data.len() as u32;
    let mut index: u32 = 0;

    // Add the first word unaligned if necessary
    let offset_mod4 = offset % 4;
    if offset_mod4 != 0 {
        let word_length = length.min(4 - offset_mod4);
        add_word_unaligned(
            value,
            &data[index as usize..(index + word_length) as usize],
            offset.wrapping_add(index) as u8,
        );
        index += word_length;
    }

    // Add the middle words aligned
    let aligned_end = index + ((length - index) / 4) * 4;
    while index < aligned_end {
        add_word_aligned(value, &data[index as usize..(index + 4) as usize]);
        index += 4;
    }

    // Add the last word unaligned if necessary
    if index < length {
        add_word_unaligned(
            value,
            &data[index as usize..length as usize],
            offset.wrapping_add(index) as u8,
        );
    }
}

fn add_word_aligned(value: &mut u32, word: &[u8]) {
    for (i, &byte) in word.iter().enumerate() {
        add_byte_at_offset(value, byte, i as u8);
    }
}

fn add_word_unaligned(value: &mut u32, word: &[u8], position: u8) {
    let mut offset = position % 4;
    for &byte in word {
        add_byte_at_offset(value, byte, offset);
        offset += 1;
        if offset == 4 {
            offset = 0;
        }
    }
}

fn add_byte_at_offset(value: &mut u32, byte: u8, offset: u8) {
    debug_assert!(offset < 4);
    let addend = (byte as u32) << (8 * (3 - offset as u32));
    *value = value.wrapping_add(addend);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn aligned_words_sum_big_endian() {
        let mut value = 0;
        update(
            &mut value,
            &[0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02],
            0,
        );
        assert_eq!(value, 3);
    }

    #[test]
    fn split_updates_equal_oneshot() {
        // The defining CFDP property: checksum is a function of (byte, file
        // offset) pairs, so any partition of the file sums identically.
        let data: [u8; 11] = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11];
        let mut oneshot = 0;
        update(&mut oneshot, &data, 0);

        let mut split = 0;
        update(&mut split, &data[..5], 0);
        update(&mut split, &data[5..], 5);
        assert_eq!(oneshot, split);

        // And an unaligned starting offset contributes at its true position.
        let mut offset_sum = 0;
        update(&mut offset_sum, &[0xFF], 3);
        assert_eq!(offset_sum, 0xFF);
    }

    #[test]
    fn wrapping_addition() {
        let mut value = 0xFFFF_FFFF;
        update(&mut value, &[0x00, 0x00, 0x00, 0x01], 0);
        assert_eq!(value, 0);
    }
}
