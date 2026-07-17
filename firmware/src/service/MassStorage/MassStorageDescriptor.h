#pragma once

// Single source of truth for the USB Mass Storage SCSI INQUIRY descriptor
// strings, plus the security invariant that guards them.
//
// The SCSI INQUIRY response carries three fixed-width fields that are space-
// padded, NOT null-terminated on the wire (see SPC): vendor id (8), product
// id (16), product revision (4). The USB stack copies these values into those
// fixed-size buffers, so the invariant that keeps a descriptor from
// overflowing adjacent memory is simply: each string must fit its field width.
//
// This header exists so the constants and their bounds are declared once and
// enforced at compile time (static_assert) and by a host unit test
// (firmware/tests/test_massstorage_descriptor.cpp) — even though the firmware
// build only compiles MassStorageESP32.cpp behind BOARD_ESP32_S3.

#include <cstddef>
#include <cstring>

namespace msc_descriptor
{
    // SCSI INQUIRY field widths (bytes).
    constexpr size_t VENDOR_ID_LEN        = 8;
    constexpr size_t PRODUCT_ID_LEN       = 16;
    constexpr size_t PRODUCT_REVISION_LEN = 4;

    constexpr char VENDOR_ID[]        = "ESP32S3";
    constexpr char PRODUCT_ID[]       = "FlashDisk";
    constexpr char PRODUCT_REVISION[] = "1.0";

    // sizeof - 1 drops the compiler-added NUL; the field is not null-terminated
    // on the wire, so the usable width equals the full field size.
    static_assert(sizeof(VENDOR_ID) - 1 <= VENDOR_ID_LEN,
                  "MSC vendor id exceeds the 8-byte SCSI INQUIRY field");
    static_assert(sizeof(PRODUCT_ID) - 1 <= PRODUCT_ID_LEN,
                  "MSC product id exceeds the 16-byte SCSI INQUIRY field");
    static_assert(sizeof(PRODUCT_REVISION) - 1 <= PRODUCT_REVISION_LEN,
                  "MSC product revision exceeds the 4-byte SCSI INQUIRY field");

    // Bounded copy contract the firmware relies on: copy at most field_len
    // bytes from src into a fixed field, never writing past it. This mirrors
    // the framework's cplstr() (USBMSC.cpp) and is what the invariant test
    // exercises against adversarial, over-long input.
    inline size_t copy_field(void *dst, const char *src, size_t field_len)
    {
        if (!dst || !src || field_len == 0)
        {
            return 0;
        }

        size_t l = strlen(src);
        if (l > field_len)
        {
            l = field_len;
        }

        memcpy(dst, src, l);
        return l;
    }
}
