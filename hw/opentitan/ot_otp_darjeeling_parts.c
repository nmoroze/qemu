/* Generated from otp_ctrl_mmap.hjson with otptool.py */

/* this prevents linters from checking this file without its parent file */
#ifdef OT_OTP_DARJEELING_PARTS

/* clang-format off */
/* NOLINTBEGIN */
static const OtOTPPartDesc OtOTPPartDescs[] = {
    [OTP_PART_VENDOR_TEST] = {
        .size = 64u,
        .offset = 0u,
        .digest_offset = 56u,
        .hw_digest = false,
        .sw_digest = true,
        .secret = false,
        .buffered = false,
        .write_lock = false,
        .read_lock_csr = true,
        .read_lock = true,
        .integrity = false,
    },
    [OTP_PART_CREATOR_SW_CFG] = {
        .size = 320u,
        .offset = 64u,
        .digest_offset = 376u,
        .hw_digest = false,
        .sw_digest = true,
        .secret = false,
        .buffered = false,
        .write_lock = false,
        .read_lock_csr = true,
        .read_lock = true,
        .integrity = true,
    },
    [OTP_PART_OWNER_SW_CFG] = {
        .size = 632u,
        .offset = 384u,
        .digest_offset = 1008u,
        .hw_digest = false,
        .sw_digest = true,
        .secret = false,
        .buffered = false,
        .write_lock = false,
        .read_lock_csr = true,
        .read_lock = true,
        .integrity = true,
    },
    [OTP_PART_OWNERSHIP_SLOT_STATE] = {
        .size = 48u,
        .offset = 1016u,
        .digest_offset = UINT16_MAX,
        .hw_digest = false,
        .sw_digest = false,
        .secret = false,
        .buffered = false,
        .write_lock = false,
        .read_lock_csr = true,
        .read_lock = true,
        .integrity = true,
    },
    [OTP_PART_ROT_CREATOR_AUTH] = {
        .size = 1424u,
        .offset = 1064u,
        .digest_offset = 2480u,
        .hw_digest = false,
        .sw_digest = true,
        .secret = false,
        .buffered = false,
        .write_lock = false,
        .read_lock_csr = true,
        .read_lock = true,
        .integrity = true,
    },
    [OTP_PART_ROT_OWNER_AUTH_SLOT0] = {
        .size = 328u,
        .offset = 2488u,
        .digest_offset = 2808u,
        .hw_digest = false,
        .sw_digest = true,
        .secret = false,
        .buffered = false,
        .write_lock = false,
        .read_lock_csr = true,
        .read_lock = true,
        .integrity = true,
    },
    [OTP_PART_ROT_OWNER_AUTH_SLOT1] = {
        .size = 328u,
        .offset = 2816u,
        .digest_offset = 3136u,
        .hw_digest = false,
        .sw_digest = true,
        .secret = false,
        .buffered = false,
        .write_lock = false,
        .read_lock_csr = true,
        .read_lock = true,
        .integrity = true,
    },
    [OTP_PART_PLAT_INTEG_AUTH_SLOT0] = {
        .size = 328u,
        .offset = 3144u,
        .digest_offset = 3464u,
        .hw_digest = false,
        .sw_digest = true,
        .secret = false,
        .buffered = false,
        .write_lock = false,
        .read_lock_csr = true,
        .read_lock = true,
        .integrity = true,
    },
    [OTP_PART_PLAT_INTEG_AUTH_SLOT1] = {
        .size = 328u,
        .offset = 3472u,
        .digest_offset = 3792u,
        .hw_digest = false,
        .sw_digest = true,
        .secret = false,
        .buffered = false,
        .write_lock = false,
        .read_lock_csr = true,
        .read_lock = true,
        .integrity = true,
    },
    [OTP_PART_PLAT_OWNER_AUTH_SLOT0] = {
        .size = 328u,
        .offset = 3800u,
        .digest_offset = 4120u,
        .hw_digest = false,
        .sw_digest = true,
        .secret = false,
        .buffered = false,
        .write_lock = false,
        .read_lock_csr = true,
        .read_lock = true,
        .integrity = true,
    },
    [OTP_PART_PLAT_OWNER_AUTH_SLOT1] = {
        .size = 328u,
        .offset = 4128u,
        .digest_offset = 4448u,
        .hw_digest = false,
        .sw_digest = true,
        .secret = false,
        .buffered = false,
        .write_lock = false,
        .read_lock_csr = true,
        .read_lock = true,
        .integrity = true,
    },
    [OTP_PART_PLAT_OWNER_AUTH_SLOT2] = {
        .size = 328u,
        .offset = 4456u,
        .digest_offset = 4776u,
        .hw_digest = false,
        .sw_digest = true,
        .secret = false,
        .buffered = false,
        .write_lock = false,
        .read_lock_csr = true,
        .read_lock = true,
        .integrity = true,
    },
    [OTP_PART_PLAT_OWNER_AUTH_SLOT3] = {
        .size = 328u,
        .offset = 4784u,
        .digest_offset = 5104u,
        .hw_digest = false,
        .sw_digest = true,
        .secret = false,
        .buffered = false,
        .write_lock = false,
        .read_lock_csr = true,
        .read_lock = true,
        .integrity = true,
    },
    [OTP_PART_EXT_NVM] = {
        .size = 1024u,
        .offset = 5112u,
        .digest_offset = UINT16_MAX,
        .hw_digest = false,
        .sw_digest = false,
        .secret = false,
        .buffered = false,
        .write_lock = false,
        .read_lock_csr = true,
        .read_lock = true,
        .integrity = false,
    },
    [OTP_PART_ROM_PATCH] = {
        .size = 9784u,
        .offset = 6136u,
        .digest_offset = 15912u,
        .hw_digest = false,
        .sw_digest = true,
        .secret = false,
        .buffered = false,
        .write_lock = false,
        .read_lock_csr = true,
        .read_lock = true,
        .integrity = true,
    },
    [OTP_PART_HW_CFG0] = {
        .size = 72u,
        .offset = 15920u,
        .digest_offset = 15984u,
        .hw_digest = true,
        .sw_digest = false,
        .secret = false,
        .buffered = true,
        .write_lock = false,
        .read_lock = false,
        .integrity = true,
    },
    [OTP_PART_HW_CFG1] = {
        .size = 16u,
        .offset = 15992u,
        .digest_offset = 16000u,
        .hw_digest = true,
        .sw_digest = false,
        .secret = false,
        .buffered = true,
        .write_lock = false,
        .read_lock = false,
        .integrity = true,
    },
    [OTP_PART_SECRET0] = {
        .size = 40u,
        .offset = 16008u,
        .digest_offset = 16040u,
        .hw_digest = true,
        .sw_digest = false,
        .secret = true,
        .buffered = true,
        .write_lock = false,
        .read_lock = true,
        .integrity = true,
    },
    [OTP_PART_SECRET1] = {
        .size = 88u,
        .offset = 16048u,
        .digest_offset = 16128u,
        .hw_digest = true,
        .sw_digest = false,
        .secret = true,
        .buffered = true,
        .write_lock = false,
        .read_lock = true,
        .integrity = true,
    },
    [OTP_PART_SECRET2] = {
        .size = 120u,
        .offset = 16136u,
        .digest_offset = 16248u,
        .hw_digest = true,
        .sw_digest = false,
        .secret = true,
        .buffered = true,
        .write_lock = false,
        .read_lock = true,
        .integrity = true,
        .iskeymgr_creator = true,
    },
    [OTP_PART_SECRET3] = {
        .size = 40u,
        .offset = 16256u,
        .digest_offset = 16288u,
        .hw_digest = true,
        .sw_digest = false,
        .secret = true,
        .buffered = true,
        .write_lock = false,
        .read_lock = true,
        .integrity = true,
        .iskeymgr_owner = true,
    },
    [OTP_PART_LIFE_CYCLE] = {
        .size = 88u,
        .offset = 16296u,
        .digest_offset = UINT16_MAX,
        .hw_digest = false,
        .sw_digest = false,
        .secret = false,
        .buffered = true,
        .write_lock = false,
        .read_lock = false,
        .integrity = true,
    },
};

#define OTP_PART_COUNT ARRAY_SIZE(OtOTPPartDescs)

/* NOLINTEND */
/* clang-format on */

#endif /* OT_OTP_DARJEELING_PARTS */