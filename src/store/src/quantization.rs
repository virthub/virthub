// virthub/src/store/src/quantization.rs

//! Quantization and dequantization helpers for PSP‑KV.
//!
//! This module provides functions to convert between 32‑bit floating‑point
//! values (`f32`) and the reduced‑precision formats used by PSP‑KV:
//! FP8 (E4M3 and E5M2), FP4 (E2M1, packed 2 values per byte), and E8M0 scale
//! exponents. The conversions are symmetric power‑of‑two where applicable.

/// Quantizes an `f32` value to FP8 E4M3 format.
pub fn quantize_fp8_e4m3(value: f32) -> u8 {
    if value.is_nan() {
        return 0x7F;
    }
    if value == 0.0 {
        return 0x00;
    }

    let bits = value.to_bits();
    let sign = ((bits >> 31) & 0x1) as u8;
    let abs_value = f32::from_bits(bits & 0x7FFF_FFFF);

    if abs_value == f32::INFINITY {
        return (sign << 7) | 0x78;
    }

    let f32_exp = ((bits >> 23) & 0xFF) as i32 - 127;
    let f32_mant = bits & 0x7F_FFFF;

    let mut fp8_exp = f32_exp + 7;
    let mut mant = f32_mant >> (23 - 3);
    let round_bit = (f32_mant >> (23 - 3 - 1)) & 1;
    let sticky = (f32_mant & ((1 << (23 - 3 - 1)) - 1)) != 0;

    if round_bit == 1 && (sticky || (mant & 1) == 1) {
        mant += 1;
        if mant == 0x8 {
            mant = 0;
            fp8_exp += 1;
        }
    }

    if fp8_exp >= 15 {
        return (sign << 7) | 0x78;
    } else if fp8_exp <= 0 {
        if fp8_exp < -7 {
            return sign << 7;
        }
        mant = ((0x8 | mant) >> (1 - fp8_exp)) & 0x7;
        fp8_exp = 0;
    }

    (sign << 7) | ((fp8_exp as u8) << 3) | (mant as u8)
}

/// Dequantizes an FP8 E4M3 value to `f32`.
pub fn dequantize_fp8_e4m3(q: u8) -> f32 {
    let sign = (q >> 7) & 0x1;
    let exp = (q >> 3) & 0xF;
    let mant = q & 0x7;

    let value = if exp == 0 {
        (mant as f32) * 2f32.powi(-6)
    } else {
        (1.0 + (mant as f32) / 8.0) * 2f32.powi(exp as i32 - 7)
    };

    if sign == 1 { -value } else { value }
}

/// Quantizes an `f32` value to FP8 E5M2 format.
pub fn quantize_fp8_e5m2(value: f32) -> u8 {
    if value.is_nan() {
        return 0x7F;
    }
    if value == 0.0 {
        return 0x00;
    }

    let bits = value.to_bits();
    let sign = ((bits >> 31) & 0x1) as u8;
    let abs_value = f32::from_bits(bits & 0x7FFF_FFFF);

    if abs_value == f32::INFINITY {
        return (sign << 7) | 0x7C;
    }

    let f32_exp = ((bits >> 23) & 0xFF) as i32 - 127;
    let f32_mant = bits & 0x7F_FFFF;

    let mut fp8_exp = f32_exp + 15;
    let mut mant = f32_mant >> (23 - 2);
    let round_bit = (f32_mant >> (23 - 2 - 1)) & 1;
    let sticky = (f32_mant & ((1 << (23 - 2 - 1)) - 1)) != 0;

    if round_bit == 1 && (sticky || (mant & 1) == 1) {
        mant += 1;
        if mant == 0x4 {
            mant = 0;
            fp8_exp += 1;
        }
    }

    if fp8_exp >= 31 {
        return (sign << 7) | 0x7C;
    } else if fp8_exp <= 0 {
        if fp8_exp < -1 {
            return sign << 7;
        }
        mant = ((0x4 | mant) >> (1 - fp8_exp)) & 0x3;
        fp8_exp = 0;
    }

    (sign << 7) | ((fp8_exp as u8) << 2) | (mant as u8)
}

/// Dequantizes an FP8 E5M2 value to `f32`.
pub fn dequantize_fp8_e5m2(q: u8) -> f32 {
    let sign = (q >> 7) & 0x1;
    let exp = (q >> 2) & 0x1F;
    let mant = q & 0x3;

    let value = if exp == 0 {
        (mant as f32) * 2f32.powi(-14)
    } else {
        (1.0 + (mant as f32) / 4.0) * 2f32.powi(exp as i32 - 15)
    };

    if sign == 1 { -value } else { value }
}

/// Quantizes an `f32` value to FP4 E2M1 format.
///
/// The representable positive magnitudes are: 0, 0.5, 1.0, 1.5, 2.0, 3.0.
/// Rounding is to the nearest representable value.
pub fn quantize_fp4_e2m1(value: f32) -> u8 {
    if value.is_nan() {
        return 0x7;
    }
    if value == 0.0 {
        return 0x0;
    }

    let bits = value.to_bits();
    let sign = ((bits >> 31) & 0x1) as u8;
    let abs_value = f32::from_bits(bits & 0x7FFF_FFFF);

    if abs_value == f32::INFINITY {
        return (sign << 3) | 0x6;
    }

    let (exp, mant) = if abs_value < 0.25 {
        (0, 0)  // 0.0
    } else if abs_value < 0.75 {
        (0, 1)  // 0.5
    } else if abs_value < 1.25 {
        (1, 0)  // 1.0
    } else if abs_value < 1.75 {
        (1, 1)  // 1.5
    } else if abs_value < 2.5 {
        (2, 0)  // 2.0
    } else if abs_value < 3.5 {
        (2, 1)  // 3.0
    } else {
        return (sign << 3) | 0x6;  // infinity
    };

    (sign << 3) | ((exp as u8) << 1) | (mant as u8)
}

/// Dequantizes an FP4 E2M1 value to `f32`.
pub fn dequantize_fp4_e2m1(q: u8) -> f32 {
    let sign = (q >> 3) & 0x1;
    let exp = (q >> 1) & 0x3;
    let mant = q & 0x1;

    let value = if exp == 0 {
        // Subnormal: value = mantissa * 0.5
        (mant as f32) * 0.5
    } else if exp == 3 {
        f32::INFINITY
    } else {
        (1.0 + (mant as f32) / 2.0) * 2f32.powi(exp as i32 - 1)
    };

    if sign == 1 { -value } else { value }
}

/// Packs two FP4 values into a single byte.
pub fn pack_fp4_pair(a: u8, b: u8) -> u8 {
    (a << 4) | (b & 0x0F)
}

/// Unpacks a packed FP4 byte into two nibbles.
pub fn unpack_fp4_pair(packed: u8) -> (u8, u8) {
    (packed >> 4, packed & 0x0F)
}

/// Converts an E8M0 scale exponent to `f32` scale factor.
pub fn e8m0_to_f32(exponent: u8) -> f32 {
    2f32.powi(exponent as i32)
}

/// Converts an `f32` scale factor to an E8M0 exponent (rounded).
///
/// - Positive infinity maps to 255 (saturation).
/// - NaN, zero, and non‑positive values map to 0.
/// - Other values are rounded to the nearest exponent.
pub fn f32_to_e8m0(scale: f32) -> u8 {
    if scale.is_nan() {
        return 0;
    }
    if scale == f32::INFINITY {
        return 255;
    }
    if scale <= 0.0 || !scale.is_finite() {
        return 0;
    }
    let exp = scale.log2().round() as i32;
    if exp < 0 {
        0
    } else if exp > 255 {
        255
    } else {
        exp as u8
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn assert_close(a: f32, b: f32, eps: f32) {
        assert!(
            (a - b).abs() < eps,
            "expected {} and {} to be within {}",
            a,
            b,
            eps
        );
    }

    #[test]
    fn test_fp8_e4m3_roundtrip() {
        let values = [0.0, 1.0, -1.0, 0.5, 2.0, 0.125, 6.0, -0.25];
        for &v in &values {
            let q = quantize_fp8_e4m3(v);
            let dq = dequantize_fp8_e4m3(q);
            assert_close(v, dq, 0.1);
        }
    }

    #[test]
    fn test_fp8_e5m2_roundtrip() {
        let values = [0.0, 1.0, -1.0, 0.5, 2.0, 0.25, 8.0, -0.125];
        for &v in &values {
            let q = quantize_fp8_e5m2(v);
            let dq = dequantize_fp8_e5m2(q);
            assert_close(v, dq, 0.2);
        }
    }

    #[test]
    fn test_fp4_pair_packing() {
        let a = 0x5;
        let b = 0xA;
        let packed = pack_fp4_pair(a, b);
        assert_eq!(unpack_fp4_pair(packed), (a, b));
    }

    #[test]
    fn test_fp4_e2m1_roundtrip() {
        let values = [0.0, 0.5, 1.0, 1.5, 2.0, 3.0, -0.5, -2.0];
        for &v in &values {
            let q = quantize_fp4_e2m1(v);
            let dq = dequantize_fp4_e2m1(q);
            assert_close(v, dq, 0.4);
        }
    }

    #[test]
    fn test_e8m0_conversion() {
        assert_eq!(e8m0_to_f32(0), 1.0);
        assert_eq!(e8m0_to_f32(1), 2.0);
        assert_eq!(e8m0_to_f32(3), 8.0);
        assert_eq!(f32_to_e8m0(4.0), 2);
        assert_eq!(f32_to_e8m0(6.0), 3);
        assert_eq!(f32_to_e8m0(f32::INFINITY), 255);
        assert_eq!(f32_to_e8m0(f32::NAN), 0);
        assert_eq!(f32_to_e8m0(0.0), 0);
        assert_eq!(f32_to_e8m0(-1.0), 0);
    }
}
