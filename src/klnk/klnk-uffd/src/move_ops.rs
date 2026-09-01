// virthub/src/klnk/klnk-uffd/src/move_ops.rs

use libc::ioctl;
use std::io::Error as IoError;
use std::os::unix::io::RawFd;
use std::sync::OnceLock;
use thiserror::Error;

const UFFDIO: u8 = 0xAA;
const UFFDIO_MOVE_NUM: u8 = 0x05;
const UFFDIO_COPY_NUM: u8 = 0x03;
const UFFDIO_ZEROPAGE_NUM: u8 = 0x04;
const UFFDIO_WRITEPROTECT_NUM: u8 = 0x06;

/// Linux Kernel C-struct for `UFFDIO_MOVE`
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct uffdio_move {
    pub dst: u64,
    pub src: u64,
    pub len: u64,
    pub mode: u64,
    /// Renamed from C `move` to avoid Rust keyword collision
    pub move_bytes: i64,
}

/// Linux Kernel C-struct for `UFFDIO_COPY`
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct uffdio_copy {
    pub dst: u64,
    pub src: u64,
    pub len: u64,
    pub mode: u64,
    pub copy: i64,
}

/// Linux Kernel C-struct for `UFFDIO_ZEROPAGE`
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct uffdio_zeropage {
    pub range: uffdio_range,
    pub mode: u64,
    pub zeropage: i64,
}

/// Linux Kernel C-struct for `UFFDIO_WRITEPROTECT`
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct uffdio_writeprotect {
    pub range: uffdio_range,
    pub mode: u64,
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct uffdio_range {
    pub start: u64,
    pub len: u64,
}

/// Flags for `UFFDIO_MOVE`
pub const UFFDIO_MOVE_MODE_ALLOW_SRC_HOLE: u64 = 1 << 0;

/// Flags for `UFFDIO_WRITEPROTECT`
pub const UFFDIO_WRITEPROTECT_MODE_WP: u64 = 1 << 0;
pub const UFFDIO_WRITEPROTECT_MODE_DONTWAKE: u64 = 1 << 1;

/// Error types for UFFD move and fault resolution ioctl operations.
#[derive(Debug, Error, PartialEq, Eq)]
pub enum UffdOpError {
    #[error("UFFDIO_MOVE ioctl failed: errno {0}")]
    MoveFailed(i32),

    #[error("UFFDIO_COPY ioctl failed: errno {0}")]
    CopyFailed(i32),

    #[error("UFFDIO_ZEROPAGE ioctl failed: errno {0}")]
    ZeroPageFailed(i32),

    #[error("UFFDIO_WRITEPROTECT ioctl failed: errno {0}")]
    WriteProtectFailed(i32),

    #[error("Partial page resolution: requested {expected} bytes, moved {actual} bytes")]
    PartialMove { expected: u64, actual: i64 },

    #[error("Invalid memory alignment: dst (0x{dst:x}), src (0x{src:x}), len ({len})")]
    InvalidAlignment { dst: u64, src: u64, len: u64 },
}

/// Helper macro to construct Linux `_IOWR(type, nr, size)` ioctl request numbers
macro_rules! iowr {
    ($ty:expr, $nr:expr, $size:ty) => {
        ((3u32) << 30)
            | (($ty as u32) << 8)
            | ($nr as u32)
            | ((std::mem::size_of::<$size>() as u32) << 16)
    };
}

/// Cache the system page size to avoid repeated `sysconf` calls.
static PAGE_SIZE: OnceLock<u64> = OnceLock::new();

/// Returns the system page size in bytes.
#[inline]
fn get_page_size() -> u64 {
    *PAGE_SIZE.get_or_init(|| {
        let page_size = unsafe { libc::sysconf(libc::_SC_PAGESIZE) };
        if page_size <= 0 {
            4096
        } else {
            page_size as u64
        }
    })
}

/// Safely issues Linux 6.8+ `UFFDIO_MOVE` ioctl to atomically swap physical page backing
/// from `src_vaddr` to `dst_vaddr` in O(1) time.
///
/// If the kernel does not support `UFFDIO_MOVE`, this function automatically falls back
/// to `UFFDIO_COPY` (which copies data instead of moving pages).
pub fn uffd_move(
    uffd_fd: RawFd,
    dst_vaddr: u64,
    src_vaddr: u64,
    len: u64,
) -> Result<usize, UffdOpError> {
    let page_size = get_page_size();

    // Validate page alignment against host system page boundary
    if dst_vaddr % page_size != 0 || src_vaddr % page_size != 0 || len % page_size != 0 {
        return Err(UffdOpError::InvalidAlignment {
            dst: dst_vaddr,
            src: src_vaddr,
            len,
        });
    }

    // Attempt UFFDIO_MOVE
    let mut move_data = uffdio_move {
        dst: dst_vaddr,
        src: src_vaddr,
        len,
        mode: UFFDIO_MOVE_MODE_ALLOW_SRC_HOLE,
        move_bytes: 0,
    };

    let request = iowr!(UFFDIO, UFFDIO_MOVE_NUM, uffdio_move);
    let ret = unsafe { ioctl(uffd_fd, request as _, &mut move_data as *mut uffdio_move) };

    if ret < 0 {
        let errno = IoError::last_os_error().raw_os_error().unwrap_or(0);

        // Fallback to UFFDIO_COPY if kernel does not support UFFDIO_MOVE
        if errno == libc::ENOSYS || errno == libc::EINVAL || errno == libc::ENOTTY {
            return uffd_copy_fallback(uffd_fd, dst_vaddr, src_vaddr, len);
        }

        return Err(UffdOpError::MoveFailed(errno));
    }

    if move_data.move_bytes < 0 || (move_data.move_bytes as u64) != len {
        return Err(UffdOpError::PartialMove {
            expected: len,
            actual: move_data.move_bytes,
        });
    }

    Ok(move_data.move_bytes as usize)
}

/// Fallback helper executing `UFFDIO_COPY` for older kernels (< 6.8).
fn uffd_copy_fallback(
    uffd_fd: RawFd,
    dst_vaddr: u64,
    src_vaddr: u64,
    len: u64,
) -> Result<usize, UffdOpError> {
    let mut copy_data = uffdio_copy {
        dst: dst_vaddr,
        src: src_vaddr,
        len,
        mode: 0,
        copy: 0,
    };

    let request = iowr!(UFFDIO, UFFDIO_COPY_NUM, uffdio_copy);
    let ret = unsafe { ioctl(uffd_fd, request as _, &mut copy_data as *mut uffdio_copy) };

    if ret < 0 {
        let errno = IoError::last_os_error().raw_os_error().unwrap_or(0);
        return Err(UffdOpError::CopyFailed(errno));
    }

    if copy_data.copy < 0 || (copy_data.copy as u64) != len {
        return Err(UffdOpError::PartialMove {
            expected: len,
            actual: copy_data.copy,
        });
    }

    Ok(copy_data.copy as usize)
}

/// Issues `UFFDIO_ZEROPAGE` to map zero-filled physical pages into a missing range.
pub fn uffd_zeropage(uffd_fd: RawFd, dst_vaddr: u64, len: u64) -> Result<(), UffdOpError> {
    let mut zeropage_data = uffdio_zeropage {
        range: uffdio_range {
            start: dst_vaddr,
            len,
        },
        mode: 0,
        zeropage: 0,
    };

    let request = iowr!(UFFDIO, UFFDIO_ZEROPAGE_NUM, uffdio_zeropage);
    let ret = unsafe { ioctl(uffd_fd, request as _, &mut zeropage_data as *mut uffdio_zeropage) };

    if ret < 0 {
        let errno = IoError::last_os_error().raw_os_error().unwrap_or(0);
        return Err(UffdOpError::ZeroPageFailed(errno));
    }

    Ok(())
}

/// Configures write-protection mode on a virtual address range via `UFFDIO_WRITEPROTECT`.
///
/// * `mode`: bitwise OR of `UFFDIO_WRITEPROTECT_MODE_WP` and optionally
///   `UFFDIO_WRITEPROTECT_MODE_DONTWAKE`.
pub fn uffd_set_write_protect(
    uffd_fd: RawFd,
    dst_vaddr: u64,
    len: u64,
    mode: u64,
) -> Result<(), UffdOpError> {
    let mut wp_data = uffdio_writeprotect {
        range: uffdio_range {
            start: dst_vaddr,
            len,
        },
        mode,
    };

    let request = iowr!(UFFDIO, UFFDIO_WRITEPROTECT_NUM, uffdio_writeprotect);
    let ret = unsafe { ioctl(uffd_fd, request as _, &mut wp_data as *mut uffdio_writeprotect) };

    if ret < 0 {
        let errno = IoError::last_os_error().raw_os_error().unwrap_or(0);
        return Err(UffdOpError::WriteProtectFailed(errno));
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_alignment_validation() {
        let page_size = get_page_size();
        let result = uffd_move(3, 0x1001, 0x2000, page_size);
        assert_eq!(
            result,
            Err(UffdOpError::InvalidAlignment {
                dst: 0x1001,
                src: 0x2000,
                len: page_size,
            })
        );
    }

    #[test]
    fn test_page_size_cached() {
        let sz1 = get_page_size();
        let sz2 = get_page_size();
        assert_eq!(sz1, sz2);
        assert!(sz1 > 0);
    }
}
