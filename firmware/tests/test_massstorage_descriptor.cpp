#include "microtest.h"
#include "service/MassStorage/MassStorageDescriptor.h"

#include <cstring>
#include <string>

// Security invariant (previously guarded by the deleted gtest
// tests/test_invariant_FatFSUSB.cpp against the removed lib/FatFSUSB):
// the USB MSC SCSI INQUIRY descriptor strings the firmware sets must never
// overflow their fixed-size fields (vendor 8, product 16, revision 4).
//
// The rev-8 firmware (service/MassStorage/esp32/MassStorageESP32.cpp) holds
// this on two counts: (1) the descriptor values are compile-time constants
// that fit their fields, and (2) the copy into the fixed buffers is length-
// clamped. These tests pin both so a future edit cannot silently break it.

using namespace msc_descriptor;

// ---- (1) the actual firmware constants fit their SCSI fields ----

TEST(descriptor_constants_fit_fields) {
    CHECK(strlen(VENDOR_ID) <= VENDOR_ID_LEN);
    CHECK(strlen(PRODUCT_ID) <= PRODUCT_ID_LEN);
    CHECK(strlen(PRODUCT_REVISION) <= PRODUCT_REVISION_LEN);
}

// ---- (2) the bounded copy cannot overflow, for any input length ----

// Mirrors the deleted test's canary approach: a fixed field flanked by guard
// bytes; after copying an adversarial string the guards must be untouched.
static void check_no_overflow(const std::string &payload, size_t field_len) {
    struct {
        unsigned char canary_before[8];
        unsigned char field[16];  // largest SCSI field
        unsigned char canary_after[8];
    } mem;
    memset(&mem, 0xAA, sizeof(mem));

    size_t written = copy_field(mem.field, payload.c_str(), field_len);

    CHECK(written <= field_len);
    for (int i = 0; i < 8; i++) {
        CHECK_EQ_INT(mem.canary_before[i], 0xAA);
        CHECK_EQ_INT(mem.canary_after[i], 0xAA);
    }
}

TEST(vendor_field_no_overflow) {
    check_no_overflow(std::string(256, 'A'), VENDOR_ID_LEN);   // far over
    check_no_overflow(std::string(9, 'B'), VENDOR_ID_LEN);     // one over
    check_no_overflow(std::string(8, 'C'), VENDOR_ID_LEN);     // exact
    check_no_overflow(VENDOR_ID, VENDOR_ID_LEN);               // real value
}

TEST(product_field_no_overflow) {
    check_no_overflow(std::string(256, 'A'), PRODUCT_ID_LEN);
    check_no_overflow(std::string(17, 'B'), PRODUCT_ID_LEN);
    check_no_overflow(PRODUCT_ID, PRODUCT_ID_LEN);
}

TEST(revision_field_no_overflow) {
    check_no_overflow(std::string(256, 'A'), PRODUCT_REVISION_LEN);
    check_no_overflow(std::string(5, 'B'), PRODUCT_REVISION_LEN);
    check_no_overflow(PRODUCT_REVISION, PRODUCT_REVISION_LEN);
}

// ---- copy_field edge cases ----

TEST(copy_field_handles_null_and_zero) {
    unsigned char buf[4] = {1, 2, 3, 4};
    CHECK_EQ_INT(copy_field(buf, nullptr, 4), 0);
    CHECK_EQ_INT(copy_field(nullptr, "x", 4), 0);
    CHECK_EQ_INT(copy_field(buf, "x", 0), 0);
}
