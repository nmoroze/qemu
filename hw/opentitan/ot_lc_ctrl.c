/*
 * QEMU OpenTitan Life Cycle controller device
 *
 * Copyright (c) 2023-2024 Rivos, Inc.
 *
 * Author(s):
 *  Emmanuel Blot <eblot@rivosinc.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Note: for now, only a minimalist subset of Life Cycle controller device is
 *       implemented in order to enable OpenTitan's ROM boot to progress
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/typedefs.h"
#include "qapi/error.h"
#include "hw/opentitan/ot_alert.h"
#include "hw/opentitan/ot_common.h"
#include "hw/opentitan/ot_kmac.h"
#include "hw/opentitan/ot_lc_ctrl.h"
#include "hw/opentitan/ot_otp.h"
#include "hw/opentitan/ot_pwrmgr.h"
#include "hw/qdev-properties.h"
#include "hw/registerfields.h"
#include "hw/riscv/ibex_common.h"
#include "hw/riscv/ibex_irq.h"
#include "hw/sysbus.h"
#include "ot_lc_defs.h"
#include "trace.h"
#include "trace/trace-hw_opentitan.h"

#undef OT_LC_CTRL_DEBUG

#define NUM_ALERTS               3u
#define PRODUCT_ID_WIDTH         16u
#define SILICON_CREATOR_ID_WIDTH 16u
#define REVISION_ID_WIDTH        8u

/* clang-format off */
REG32(ALERT_TEST, 0x0u)
    SHARED_FIELD(ALERT_FATAL_PROG_ERROR, 0u, 1u)
    SHARED_FIELD(ALERT_FATAL_STATE_ERROR, 1u, 1u)
    SHARED_FIELD(ALERT_FATAL_BUS_INTEG_ERROR, 2u, 1u)
REG32(STATUS, 0x4u)
    FIELD(STATUS, INITIALIZED, 0u, 1u)
    FIELD(STATUS, READY, 1u, 1u)
    FIELD(STATUS, EXT_CLOCK_SWITCHED, 2u, 1u)
    FIELD(STATUS, TRANSITION_SUCCESSFUL, 3u, 1u)
    FIELD(STATUS, TRANSITION_COUNT_ERROR, 4u, 1u)
    FIELD(STATUS, TRANSITION_ERROR, 5u, 1u)
    FIELD(STATUS, TOKEN_ERROR, 6u, 1u)
    FIELD(STATUS, FLASH_RMA_ERROR, 7u, 1u)
    FIELD(STATUS, OTP_ERROR, 8u, 1u)
    FIELD(STATUS, STATE_ERROR, 9u, 1u)
    FIELD(STATUS, BUS_INTEG_ERROR, 10u, 1u)
    FIELD(STATUS, OTP_PARTITION_ERROR, 11u, 1u)
REG32(CLAIM_TRANSITION_IF_REGWEN, 0x8u)
    FIELD(CLAIM_TRANSITION_IF_REGWEN, EN, 0u, 1u)
REG32(CLAIM_TRANSITION_IF, 0xcu)
    FIELD(CLAIM_TRANSITION, IF_MUTEX, 0u, 8u)
REG32(TRANSITION_REGWEN, 0x10u)
    FIELD(TRANSITION_REGWEN, EN, 0u, 1u)
REG32(TRANSITION_CMD, 0x14u)
    FIELD(TRANSITION_CMD, START, 0u, 1u)
REG32(TRANSITION_CTRL, 0x18u)
    FIELD(TRANSITION_CTRL, EXT_CLOCK_EN, 0u, 1u)
    FIELD(TRANSITION_CTRL, VOLATILE_RAW_UNLOCK, 1u, 1u)
REG32(TRANSITION_TOKEN_0, 0x1cu)
REG32(TRANSITION_TOKEN_1, 0x20u)
REG32(TRANSITION_TOKEN_2, 0x24u)
REG32(TRANSITION_TOKEN_3, 0x28u)
REG32(TRANSITION_TARGET, 0x2cu)
    FIELD(TRANSITION_TARGET, STATE, 0u, 30u)
REG32(OTP_VENDOR_TEST_CTRL, 0x30u)
REG32(OTP_VENDOR_TEST_STATUS, 0x34u)
REG32(LC_STATE, 0x38u)
    FIELD(LC_STATE, STATE, 0u, 30u)
REG32(LC_TRANSITION_CNT, 0x3cu)
    FIELD(LC_TRANSITION_CNT, CNT, 0u, 5u)
REG32(LC_ID_STATE, 0x40u)
REG32(HW_REVISION0, 0x44u)
    FIELD(HW_REVISION0, PRODUCT_ID, 0u, PRODUCT_ID_WIDTH)
    FIELD(HW_REVISION0, SILICON_CREATOR_ID, PRODUCT_ID_WIDTH,
          SILICON_CREATOR_ID_WIDTH)
REG32(HW_REVISION1, 0x48u)
    FIELD(HW_REVISION1, REVISION_ID, 0u, REVISION_ID_WIDTH)
    FIELD(HW_REVISION1, RESERVED,
          REVISION_ID_WIDTH, (32u - REVISION_ID_WIDTH))
REG32(DEVICE_ID_0, 0x4cu)
REG32(DEVICE_ID_1, 0x50u)
REG32(DEVICE_ID_2, 0x54u)
REG32(DEVICE_ID_3, 0x58u)
REG32(DEVICE_ID_4, 0x5cu)
REG32(DEVICE_ID_5, 0x60u)
REG32(DEVICE_ID_6, 0x64u)
REG32(DEVICE_ID_7, 0x68u)
REG32(MANUF_STATE_0, 0x6cu)
REG32(MANUF_STATE_1, 0x70u)
REG32(MANUF_STATE_2, 0x74u)
REG32(MANUF_STATE_3, 0x78u)
REG32(MANUF_STATE_4, 0x7cu)
REG32(MANUF_STATE_5, 0x80u)
REG32(MANUF_STATE_6, 0x84u)
REG32(MANUF_STATE_7, 0x88u)
/* clang-format on */

#define R32_OFF(_r_) ((_r_) / sizeof(uint32_t))

#define R_LAST_REG (R_MANUF_STATE_7)
#define REGS_COUNT (R_LAST_REG + 1u)
#define REGS_SIZE  (REGS_COUNT * sizeof(uint32_t))
#define REG_NAME(_reg_) \
    ((((_reg_) <= REGS_COUNT) && REG_NAMES[_reg_]) ? REG_NAMES[_reg_] : "?")

#define R_FIRST_EXCLUSIVE_REG (R_TRANSITION_TOKEN_0)
#define R_LAST_EXCLUSIVE_REG  (R_TRANSITION_TARGET)
#define EXCLUSIVE_REGS_COUNT  (R_LAST_EXCLUSIVE_REG - R_FIRST_EXCLUSIVE_REG + 1u)
#define XREGS_OFFSET(_r_)     ((_r_) - R_FIRST_EXCLUSIVE_REG)

#define ALERT_TEST_MASK \
    (ALERT_FATAL_PROG_ERROR_MASK | ALERT_FATAL_STATE_ERROR_MASK | \
     ALERT_FATAL_BUS_INTEG_ERROR_MASK)

#define LC_TRANSITION_COUNT_MAX 24u
#define LC_TOKEN_WIDTH          16u /* 128 bits */
#define LC_TOKEN_DWORDS         (LC_TOKEN_WIDTH / sizeof(uint64_t))

#define REG_NAME_ENTRY(_reg_) [R_##_reg_] = stringify(_reg_)
static const char *REG_NAMES[REGS_COUNT] = {
    REG_NAME_ENTRY(ALERT_TEST),
    REG_NAME_ENTRY(STATUS),
    REG_NAME_ENTRY(CLAIM_TRANSITION_IF_REGWEN),
    REG_NAME_ENTRY(CLAIM_TRANSITION_IF),
    REG_NAME_ENTRY(TRANSITION_REGWEN),
    REG_NAME_ENTRY(TRANSITION_CMD),
    REG_NAME_ENTRY(TRANSITION_CTRL),
    REG_NAME_ENTRY(TRANSITION_TOKEN_0),
    REG_NAME_ENTRY(TRANSITION_TOKEN_1),
    REG_NAME_ENTRY(TRANSITION_TOKEN_2),
    REG_NAME_ENTRY(TRANSITION_TOKEN_3),
    REG_NAME_ENTRY(TRANSITION_TARGET),
    REG_NAME_ENTRY(OTP_VENDOR_TEST_CTRL),
    REG_NAME_ENTRY(OTP_VENDOR_TEST_STATUS),
    REG_NAME_ENTRY(LC_STATE),
    REG_NAME_ENTRY(LC_TRANSITION_CNT),
    REG_NAME_ENTRY(LC_ID_STATE),
    REG_NAME_ENTRY(HW_REVISION0),
    REG_NAME_ENTRY(HW_REVISION1),
    REG_NAME_ENTRY(DEVICE_ID_0),
    REG_NAME_ENTRY(DEVICE_ID_1),
    REG_NAME_ENTRY(DEVICE_ID_2),
    REG_NAME_ENTRY(DEVICE_ID_3),
    REG_NAME_ENTRY(DEVICE_ID_4),
    REG_NAME_ENTRY(DEVICE_ID_5),
    REG_NAME_ENTRY(DEVICE_ID_6),
    REG_NAME_ENTRY(DEVICE_ID_7),
    REG_NAME_ENTRY(MANUF_STATE_0),
    REG_NAME_ENTRY(MANUF_STATE_1),
    REG_NAME_ENTRY(MANUF_STATE_2),
    REG_NAME_ENTRY(MANUF_STATE_3),
    REG_NAME_ENTRY(MANUF_STATE_4),
    REG_NAME_ENTRY(MANUF_STATE_5),
    REG_NAME_ENTRY(MANUF_STATE_6),
    REG_NAME_ENTRY(MANUF_STATE_7),
};
#undef REG_NAME_ENTRY

#define LC_ENCODE_STATE(_x_) \
    (((_x_) << 0u) | ((_x_) << 5u) | ((_x_) << 10u) | ((_x_) << 15u) | \
     ((_x_) << 20u) | ((_x_) << 25u))

#define LC_ID_STATE_BLANK        0
#define LC_ID_STATE_PERSONALIZED 0x55555555u
#define LC_ID_STATE_INVALID      0xaaaaaaaau

typedef enum {
    LC_ENC_STATE_RAW = LC_ENCODE_STATE(LC_STATE_RAW),
    LC_ENC_STATE_TESTUNLOCKED0 = LC_ENCODE_STATE(LC_STATE_TESTUNLOCKED0),
    LC_ENC_STATE_TESTLOCKED0 = LC_ENCODE_STATE(LC_STATE_TESTLOCKED0),
    LC_ENC_STATE_TESTUNLOCKED1 = LC_ENCODE_STATE(LC_STATE_TESTUNLOCKED1),
    LC_ENC_STATE_TESTLOCKED1 = LC_ENCODE_STATE(LC_STATE_TESTLOCKED1),
    LC_ENC_STATE_TESTUNLOCKED2 = LC_ENCODE_STATE(LC_STATE_TESTUNLOCKED2),
    LC_ENC_STATE_TESTLOCKED2 = LC_ENCODE_STATE(LC_STATE_TESTLOCKED2),
    LC_ENC_STATE_TESTUNLOCKED3 = LC_ENCODE_STATE(LC_STATE_TESTUNLOCKED3),
    LC_ENC_STATE_TESTLOCKED3 = LC_ENCODE_STATE(LC_STATE_TESTLOCKED3),
    LC_ENC_STATE_TESTUNLOCKED4 = LC_ENCODE_STATE(LC_STATE_TESTUNLOCKED4),
    LC_ENC_STATE_TESTLOCKED4 = LC_ENCODE_STATE(LC_STATE_TESTLOCKED4),
    LC_ENC_STATE_TESTUNLOCKED5 = LC_ENCODE_STATE(LC_STATE_TESTUNLOCKED5),
    LC_ENC_STATE_TESTLOCKED5 = LC_ENCODE_STATE(LC_STATE_TESTLOCKED5),
    LC_ENC_STATE_TESTUNLOCKED6 = LC_ENCODE_STATE(LC_STATE_TESTUNLOCKED6),
    LC_ENC_STATE_TESTLOCKED6 = LC_ENCODE_STATE(LC_STATE_TESTLOCKED6),
    LC_ENC_STATE_TESTUNLOCKED7 = LC_ENCODE_STATE(LC_STATE_TESTUNLOCKED7),
    LC_ENC_STATE_DEV = LC_ENCODE_STATE(LC_STATE_DEV),
    LC_ENC_STATE_PROD = LC_ENCODE_STATE(LC_STATE_PROD),
    LC_ENC_STATE_PRODEND = LC_ENCODE_STATE(LC_STATE_PRODEND),
    LC_ENC_STATE_RMA = LC_ENCODE_STATE(LC_STATE_RMA),
    LC_ENC_STATE_SCRAP = LC_ENCODE_STATE(LC_STATE_SCRAP),
    LC_ENC_STATE_POST_TRANSITION = LC_ENCODE_STATE(LC_STATE_POST_TRANSITION),
    LC_ENC_STATE_ESCALATE = LC_ENCODE_STATE(LC_STATE_ESCALATE),
    LC_ENC_STATE_INVALID = LC_ENCODE_STATE(LC_STATE_INVALID),
} OtLcEncodedState;

typedef enum {
    LC_IF_NONE,
    LC_IF_SW, /* CPU requester */
    LC_IF_DMI, /* DMI requester */
} OtLcCtrlIf;

#define EXCLUSIVE_SLOTS_COUNT 2u
#define LC_XSLOT(_ifreq_)     (((unsigned)(_ifreq_)) - 1u)

typedef enum {
    ST_RESET,
    ST_IDLE,
    ST_CLK_MUX,
    ST_CNT_INCR,
    ST_CNT_PROG,
    ST_TRANS_CHECK,
    ST_TOKEN_HASH,
    ST_FLASH_RMA,
    ST_TOKEN_CHECK0,
    ST_TOKEN_CHECK1,
    ST_TRANS_PROG,
    ST_POST_TRANS,
    ST_SCRAP,
    ST_ESCALATE,
    ST_INVALID,
} OtLcCtrlFsmState;

typedef enum {
    ST_KMAC_IDLE,
    ST_KMAC_FIRST,
    ST_KMAC_SECOND,
    ST_KMAC_WAIT,
} OtLcCtrlFsmKmacState;

typedef enum {
    LC_TK_INVALID, /* SBZ */
    LC_TK_ZERO,
    LC_TK_RAW_UNLOCK,
    LC_TK_TEST_UNLOCK,
    LC_TK_TEST_EXIT,
    LC_TK_RMA,
    LC_TK_COUNT,
} OtLcCtrlToken;

/* ife cycle state group diversification value for keymgr */
typedef enum {
    LC_DIV_INVALID,
    LC_DIV_TEST_DEV_RMA,
    LC_DIV_PROD,
} OtLcCtrlKeyMgrDiv;

struct OtLcCtrlState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    MemoryRegion dmi_mmio;
    QEMUBH *pwc_lc_bh;
    QEMUBH *escalate_bh;
    IbexIRQ alerts[NUM_ALERTS];
    IbexIRQ broadcasts[OT_LC_BROADCAST_COUNT];
    IbexIRQ pwc_lc_rsp;

    uint32_t *regs; /* slots in xregs are not used in regs */
    uint32_t xregs[EXCLUSIVE_SLOTS_COUNT][EXCLUSIVE_REGS_COUNT];
    OtLcState lc_state;
    OtLcCtrlKeyMgrDiv km_div;
    uint32_t lc_tcount;
    OtOTPTokenValue hash_token;
    OtLcCtrlIf owner;
    OtLcCtrlFsmState state;
    OtLcCtrlFsmKmacState kmac_state;
    OtOTPTokenValue hashed_tokens[LC_TK_COUNT];
    uint32_t hashed_token_bm;
    struct {
        uint32_t value;
        unsigned count;
    } status_cache; /* special debug cache for status register */
    bool ext_clock_en; /* request for external clock */
    bool volatile_unlocked; /* set on successful volatile unlock */
    uint8_t volatile_raw_unlock_bm; /* xslot-indexed bitmap */
    uint8_t state_invalid_error_bm; /* error bitmap */

    /* properties */
    OtOTPState *otp_ctrl;
    OtKMACState *kmac;
    uint16_t silicon_creator_id;
    uint16_t product_id;
    uint8_t revision_id;
    uint8_t kmac_app;
    bool volatile_raw_unlock;
};

static_assert(sizeof(OtOTPTokenValue) == LC_TOKEN_WIDTH,
              "Unexpected LC TOLEN WIDTH");

static const OtKMACAppCfg ot_lc_ctrl_kmac_config =
    OT_KMAC_CONFIG(CSHAKE, 128u, "", "LC_CTRL");

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-const-variable"

static const OtOTPTokenValue LC_ALL_ZERO_TOKEN = {
    .hi = 0,
    .lo = 0,
};

static const OtOTPTokenValue LC_ALL_ZERO_TOKEN_HASHED = {
    .hi = 0x3852305baecf5ff1u, .lo = 0xd5c1d25f6db9058du
};

static const OtOTPTokenValue LC_RND_CNST_RAW_UNLOCK_TOKEN = {
    .hi = 0xea2b3f32cbe77554u, .lo = 0xe43c8ea7ebf197c2u
};

static const OtOTPTokenValue LC_RND_CNST_RAW_UNLOCK_TOKEN_HASHED = {
    .hi = 0xf8fe11b88c36c814u, .lo = 0x0252f036d23804db
};

#pragma GCC diagnostic pop

/* transition matrix */
static const OtLcCtrlToken
    LC_TRANS_TOKEN_MATRIX[LC_STATE_VALID_COUNT][LC_STATE_VALID_COUNT];

/* NOLINTNEXTLINE*/
#include "ot_lc_ctrl_matrix.c"

#define LC_NAME_ENTRY(_st_) [_st_] = stringify(_st_)

/* clang-format off */
static const char *LC_FSM_STATE_NAMES[] = {
    LC_NAME_ENTRY(ST_RESET),
    LC_NAME_ENTRY(ST_IDLE),
    LC_NAME_ENTRY(ST_CLK_MUX),
    LC_NAME_ENTRY(ST_CNT_INCR),
    LC_NAME_ENTRY(ST_CNT_PROG),
    LC_NAME_ENTRY(ST_TRANS_CHECK),
    LC_NAME_ENTRY(ST_TOKEN_HASH),
    LC_NAME_ENTRY(ST_FLASH_RMA),
    LC_NAME_ENTRY(ST_TOKEN_CHECK0),
    LC_NAME_ENTRY(ST_TOKEN_CHECK1),
    LC_NAME_ENTRY(ST_TRANS_PROG),
    LC_NAME_ENTRY(ST_POST_TRANS),
    LC_NAME_ENTRY(ST_SCRAP),
    LC_NAME_ENTRY(ST_ESCALATE),
    LC_NAME_ENTRY(ST_INVALID),
};

static const char *LC_TOKEN_NAMES[] = {
    LC_NAME_ENTRY(LC_TK_INVALID),
    LC_NAME_ENTRY(LC_TK_ZERO),
    LC_NAME_ENTRY(LC_TK_RAW_UNLOCK),
    LC_NAME_ENTRY(LC_TK_TEST_UNLOCK),
    LC_NAME_ENTRY(LC_TK_TEST_EXIT),
    LC_NAME_ENTRY(LC_TK_RMA),
};

static const char *LC_STATE_NAMES[] = {
    LC_NAME_ENTRY(LC_STATE_RAW),
    LC_NAME_ENTRY(LC_STATE_TESTUNLOCKED0),
    LC_NAME_ENTRY(LC_STATE_TESTLOCKED0),
    LC_NAME_ENTRY(LC_STATE_TESTUNLOCKED1),
    LC_NAME_ENTRY(LC_STATE_TESTLOCKED1),
    LC_NAME_ENTRY(LC_STATE_TESTUNLOCKED2),
    LC_NAME_ENTRY(LC_STATE_TESTLOCKED2),
    LC_NAME_ENTRY(LC_STATE_TESTUNLOCKED3),
    LC_NAME_ENTRY(LC_STATE_TESTLOCKED3),
    LC_NAME_ENTRY(LC_STATE_TESTUNLOCKED4),
    LC_NAME_ENTRY(LC_STATE_TESTLOCKED4),
    LC_NAME_ENTRY(LC_STATE_TESTUNLOCKED5),
    LC_NAME_ENTRY(LC_STATE_TESTLOCKED5),
    LC_NAME_ENTRY(LC_STATE_TESTUNLOCKED6),
    LC_NAME_ENTRY(LC_STATE_TESTLOCKED6),
    LC_NAME_ENTRY(LC_STATE_TESTUNLOCKED7),
    LC_NAME_ENTRY(LC_STATE_DEV),
    LC_NAME_ENTRY(LC_STATE_PROD),
    LC_NAME_ENTRY(LC_STATE_PRODEND),
    LC_NAME_ENTRY(LC_STATE_RMA),
    LC_NAME_ENTRY(LC_STATE_SCRAP),
    LC_NAME_ENTRY(LC_STATE_POST_TRANSITION),
    LC_NAME_ENTRY(LC_STATE_ESCALATE),
    LC_NAME_ENTRY(LC_STATE_INVALID),
};

static const char *LC_BROADCAST_NAMES[] = {
    LC_NAME_ENTRY(OT_LC_RAW_TEST_RMA),
    LC_NAME_ENTRY(OT_LC_DFT_EN),
    LC_NAME_ENTRY(OT_LC_NVM_DEBUG_EN),
    LC_NAME_ENTRY(OT_LC_HW_DEBUG_EN),
    LC_NAME_ENTRY(OT_LC_CPU_EN),
    LC_NAME_ENTRY(OT_LC_KEYMGR_EN),
    LC_NAME_ENTRY(OT_LC_ESCALATE_EN),
    LC_NAME_ENTRY(OT_LC_CHECK_BYP_EN),
    LC_NAME_ENTRY(OT_LC_CREATOR_SEED_SW_RW_EN),
    LC_NAME_ENTRY(OT_LC_OWNER_SEED_SW_RW_EN),
    LC_NAME_ENTRY(OT_LC_ISO_PART_SW_RD_EN),
    LC_NAME_ENTRY(OT_LC_ISO_PART_SW_WR_EN),
    LC_NAME_ENTRY(OT_LC_SEED_HW_RD_EN),
};
/* clang-format on */

#undef LC_NAME_ENTRY

#define LC_FSM_STATE_NAME(_st_) \
    (((unsigned)(_st_)) < ARRAY_SIZE(LC_FSM_STATE_NAMES) ? \
         LC_FSM_STATE_NAMES[(_st_)] : \
         "?")

#define LC_FSM_CHANGE_STATE(_s_, _st_) \
    ot_lc_ctrl_change_state_line(_s_, _st_, __LINE__)

#define LC_TOKEN_NAME(_tk_) \
    (((unsigned)(_tk_)) < ARRAY_SIZE(LC_TOKEN_NAMES) ? \
         LC_TOKEN_NAMES[(_tk_)] : \
         "?")

#define LC_STATE_NAME(_st_) \
    (((unsigned)(_st_)) < ARRAY_SIZE(LC_STATE_NAMES) ? \
         LC_STATE_NAMES[(_st_)] : \
         "?")

#define LC_BCAST_BIT(_sig_) (1u << (OT_LC_##_sig_))

#define LC_BCAST_NAME(_bit_) \
    (((unsigned)(_bit_)) < ARRAY_SIZE(LC_BROADCAST_NAMES) ? \
         LC_BROADCAST_NAMES[(_bit_)] : \
         "?")

#ifdef OT_LC_CTRL_DEBUG
#define TRACE_LC_CTRL(msg, ...) \
    qemu_log("%s: " msg "\n", __func__, ##__VA_ARGS__);
#else
#define TRACE_LC_CTRL(msg, ...)
#endif

#ifdef OT_LC_CTRL_DEBUG
static char hexbuf[256u];
static const char *ot_lc_ctrl_hexdump(const void *data, size_t size)
{
    static const char _hex[] = "0123456789abcdef";
    const uint8_t *buf = (const uint8_t *)data;

    if (size > ((sizeof(hexbuf) / 2u) - 2u)) {
        size = sizeof(hexbuf) / 2u - 2u;
    }

    char *hexstr = hexbuf;
    for (size_t ix = 0; ix < size; ix++) {
        hexstr[(ix * 2u)] = _hex[(buf[ix] >> 4u) & 0xfu];
        hexstr[(ix * 2u) + 1u] = _hex[buf[ix] & 0xfu];
    }
    hexstr[size * 2u] = '\0';
    return hexbuf;
}
#endif

static void ot_lc_ctrl_resume_transition(OtLcCtrlState *s);

static void
ot_lc_ctrl_change_state_line(OtLcCtrlState *s, OtLcCtrlFsmState state, int line)
{
    trace_ot_lc_ctrl_change_state(line, LC_FSM_STATE_NAME(s->state), s->state,
                                  LC_FSM_STATE_NAME(state), state);

    s->state = state;
}

static void ot_lc_ctrl_update_alerts(OtLcCtrlState *s)
{
    uint32_t level = s->regs[R_ALERT_TEST];

    for (unsigned ix = 0; ix < NUM_ALERTS; ix++) {
        ibex_irq_set(&s->alerts[ix], (int)((level >> ix) & 0x1u));
    }
}

static void ot_lc_ctrl_update_broadcast(OtLcCtrlState *s)
{
    uint32_t sigbm = 0;
    OtLcCtrlKeyMgrDiv div = LC_DIV_INVALID;

    switch (s->state) {
    case ST_RESET:
        break;
    case ST_IDLE:
    case ST_CLK_MUX:
    case ST_CNT_INCR:
    case ST_CNT_PROG:
    case ST_TRANS_CHECK:
    case ST_TOKEN_HASH:
    case ST_FLASH_RMA:
    case ST_TOKEN_CHECK0:
    case ST_TOKEN_CHECK1:
    case ST_TRANS_PROG:
        switch (s->lc_state) {
        case LC_STATE_RAW:
        case LC_STATE_TESTLOCKED0:
        case LC_STATE_TESTLOCKED1:
        case LC_STATE_TESTLOCKED2:
        case LC_STATE_TESTLOCKED3:
        case LC_STATE_TESTLOCKED4:
        case LC_STATE_TESTLOCKED5:
        case LC_STATE_TESTLOCKED6:
            sigbm = LC_BCAST_BIT(RAW_TEST_RMA);
            break;
        case LC_STATE_TESTUNLOCKED0:
        case LC_STATE_TESTUNLOCKED1:
        case LC_STATE_TESTUNLOCKED2:
        case LC_STATE_TESTUNLOCKED3:
        case LC_STATE_TESTUNLOCKED4:
        case LC_STATE_TESTUNLOCKED5:
        case LC_STATE_TESTUNLOCKED6:
            sigbm = LC_BCAST_BIT(RAW_TEST_RMA) | LC_BCAST_BIT(DFT_EN) |
                    LC_BCAST_BIT(NVM_DEBUG_EN) | LC_BCAST_BIT(HW_DEBUG_EN) |
                    LC_BCAST_BIT(CPU_EN) | LC_BCAST_BIT(ISO_PART_SW_WR_EN);
            div = LC_DIV_TEST_DEV_RMA;
            break;
        case LC_STATE_TESTUNLOCKED7:
            sigbm = LC_BCAST_BIT(RAW_TEST_RMA) | LC_BCAST_BIT(DFT_EN) |
                    LC_BCAST_BIT(HW_DEBUG_EN) | LC_BCAST_BIT(CPU_EN) |
                    LC_BCAST_BIT(ISO_PART_SW_WR_EN);
            div = LC_DIV_TEST_DEV_RMA;
            break;
        case LC_STATE_PROD:
        case LC_STATE_PRODEND:
            sigbm = LC_BCAST_BIT(CPU_EN) | LC_BCAST_BIT(KEYMGR_EN) |
                    LC_BCAST_BIT(OWNER_SEED_SW_RW_EN) |
                    LC_BCAST_BIT(ISO_PART_SW_WR_EN) |
                    LC_BCAST_BIT(ISO_PART_SW_RD_EN);
            /*
             * "Only allow provisioning if the device has not yet been
             * personalized."
             */
            if (s->regs[R_LC_ID_STATE] == LC_ID_STATE_BLANK) {
                sigbm |= LC_BCAST_BIT(CREATOR_SEED_SW_RW_EN);
            }
            /*
             * "Only allow hardware to consume the seeds once personalized."
             */
            if (s->regs[R_LC_ID_STATE] == LC_ID_STATE_PERSONALIZED) {
                sigbm |= LC_BCAST_BIT(SEED_HW_RD_EN);
            }
            div = LC_DIV_PROD;
            break;
        case LC_STATE_DEV:
            sigbm =
                LC_BCAST_BIT(HW_DEBUG_EN) | LC_BCAST_BIT(CPU_EN) |
                LC_BCAST_BIT(KEYMGR_EN) | LC_BCAST_BIT(OWNER_SEED_SW_RW_EN) |
                LC_BCAST_BIT(ISO_PART_SW_WR_EN);
            if (s->regs[R_LC_ID_STATE] == LC_ID_STATE_BLANK) {
                sigbm |= LC_BCAST_BIT(CREATOR_SEED_SW_RW_EN);
            }
            if (s->regs[R_LC_ID_STATE] == LC_ID_STATE_PERSONALIZED) {
                sigbm |= LC_BCAST_BIT(SEED_HW_RD_EN);
            }
            div = LC_DIV_TEST_DEV_RMA;
            break;
        case LC_STATE_RMA:
            sigbm =
                LC_BCAST_BIT(RAW_TEST_RMA) | LC_BCAST_BIT(DFT_EN) |
                LC_BCAST_BIT(NVM_DEBUG_EN) | LC_BCAST_BIT(HW_DEBUG_EN) |
                LC_BCAST_BIT(CPU_EN) | LC_BCAST_BIT(KEYMGR_EN) |
                LC_BCAST_BIT(CHECK_BYP_EN) |
                LC_BCAST_BIT(CREATOR_SEED_SW_RW_EN) |
                LC_BCAST_BIT(OWNER_SEED_SW_RW_EN) |
                LC_BCAST_BIT(ISO_PART_SW_RD_EN) |
                LC_BCAST_BIT(ISO_PART_SW_WR_EN) | LC_BCAST_BIT(SEED_HW_RD_EN);
            div = LC_DIV_TEST_DEV_RMA;
            break;
        case LC_STATE_SCRAP:
        default:
            trace_ot_lc_ctrl_escalate(LC_FSM_STATE_NAME(s->state),
                                      LC_STATE_NAME(s->lc_state));
            sigbm = LC_BCAST_BIT(ESCALATE_EN);
            break;
        }
        break;
    case ST_POST_TRANS:
        break;
    case ST_SCRAP:
    case ST_ESCALATE:
    case ST_INVALID:
    default:
        trace_ot_lc_ctrl_escalate(LC_FSM_STATE_NAME(s->state),
                                  LC_STATE_NAME(s->lc_state));
        sigbm = LC_BCAST_BIT(ESCALATE_EN);
        break;
    }

    s->km_div = div;

    for (unsigned ix = 0; ix < ARRAY_SIZE(s->broadcasts); ix++) {
        bool level = (bool)(sigbm & (1u << ix));
        bool curlvl = (bool)ibex_irq_get_level(&s->broadcasts[ix]);
        if (level != curlvl) {
            trace_ot_lc_ctrl_update_broadcast(LC_FSM_STATE_NAME(s->state),
                                              LC_BCAST_NAME(ix), curlvl, level);
        }
        ibex_irq_set(&s->broadcasts[ix], (int)level);
    }
}

static bool ot_lc_ctrl_match_token(const OtLcCtrlState *s, OtLcCtrlToken tok)
{
    g_assert(((unsigned)tok) < LC_TK_COUNT);

    bool match = ((bool)(s->hashed_token_bm & (1u << tok))) &&
                 (s->hash_token.lo == s->hashed_tokens[tok].lo) &&
                 (s->hash_token.hi == s->hashed_tokens[tok].hi);

    if (!match) {
        trace_ot_lc_ctrl_mismatch_token(s->hashed_token_bm & (1u << tok) ?
                                            "hashed" :
                                            "zero",
                                        LC_TOKEN_NAME(tok), tok,
                                        s->hash_token.hi, s->hash_token.lo,
                                        s->hashed_tokens[tok].hi,
                                        s->hashed_tokens[tok].lo);
    }

    return match;
}

static bool
ot_lc_ctrl_is_hw_mutex_owner(const OtLcCtrlState *s, OtLcCtrlIf owner)
{
    return s->owner == owner;
}

static bool ot_lc_ctrl_lock_hw_mutex(OtLcCtrlState *s, OtLcCtrlIf owner)
{
    if (s->owner != LC_IF_NONE) {
        return ot_lc_ctrl_is_hw_mutex_owner(s, owner);
    }

    s->owner = owner;

    return true;
}

static void ot_lc_ctrl_release_hw_mutex(OtLcCtrlState *s)
{
    s->owner = LC_IF_NONE;
}

static bool
ot_lc_ctrl_is_transition_en(const OtLcCtrlState *s, OtLcCtrlIf owner)
{
    return ot_lc_ctrl_is_hw_mutex_owner(s, owner) && s->state == ST_IDLE;
}

static bool ot_lc_ctrl_is_known_state(uint32_t state)
{
    switch (state) {
    case LC_ENC_STATE_RAW:
    case LC_ENC_STATE_TESTUNLOCKED0:
    case LC_ENC_STATE_TESTLOCKED0:
    case LC_ENC_STATE_TESTUNLOCKED1:
    case LC_ENC_STATE_TESTLOCKED1:
    case LC_ENC_STATE_TESTUNLOCKED2:
    case LC_ENC_STATE_TESTLOCKED2:
    case LC_ENC_STATE_TESTUNLOCKED3:
    case LC_ENC_STATE_TESTLOCKED3:
    case LC_ENC_STATE_TESTUNLOCKED4:
    case LC_ENC_STATE_TESTLOCKED4:
    case LC_ENC_STATE_TESTUNLOCKED5:
    case LC_ENC_STATE_TESTLOCKED5:
    case LC_ENC_STATE_TESTUNLOCKED6:
    case LC_ENC_STATE_TESTLOCKED6:
    case LC_ENC_STATE_TESTUNLOCKED7:
    case LC_ENC_STATE_DEV:
    case LC_ENC_STATE_PROD:
    case LC_ENC_STATE_PRODEND:
    case LC_ENC_STATE_RMA:
    case LC_ENC_STATE_SCRAP:
        return true;
    default:
        return false;
    }
}

static bool ot_lc_ctrl_is_vendor_test_state(uint32_t state)
{
    switch (state) {
    case LC_ENC_STATE_RAW:
    case LC_ENC_STATE_TESTUNLOCKED0:
    case LC_ENC_STATE_TESTLOCKED0:
    case LC_ENC_STATE_TESTUNLOCKED1:
    case LC_ENC_STATE_TESTLOCKED1:
    case LC_ENC_STATE_TESTUNLOCKED2:
    case LC_ENC_STATE_TESTLOCKED2:
    case LC_ENC_STATE_TESTUNLOCKED3:
    case LC_ENC_STATE_TESTLOCKED3:
    case LC_ENC_STATE_TESTUNLOCKED4:
    case LC_ENC_STATE_TESTLOCKED4:
    case LC_ENC_STATE_TESTUNLOCKED5:
    case LC_ENC_STATE_TESTLOCKED5:
    case LC_ENC_STATE_TESTUNLOCKED6:
    case LC_ENC_STATE_TESTLOCKED6:
    case LC_ENC_STATE_TESTUNLOCKED7:
    case LC_ENC_STATE_RMA:
        return true;
    default:
        return false;
    }
}

static OtLcState ot_lc_ctrl_convert_code_to_state(uint32_t enc_state)
{
    OtLcState state;

    switch (enc_state) {
    case LC_ENC_STATE_RAW:
        state = LC_STATE_RAW;
        break;
    case LC_ENC_STATE_TESTUNLOCKED0:
        state = LC_STATE_TESTUNLOCKED0;
        break;
    case LC_ENC_STATE_TESTLOCKED0:
        state = LC_STATE_TESTLOCKED0;
        break;
    case LC_ENC_STATE_TESTUNLOCKED1:
        state = LC_STATE_TESTUNLOCKED1;
        break;
    case LC_ENC_STATE_TESTLOCKED1:
        state = LC_STATE_TESTLOCKED1;
        break;
    case LC_ENC_STATE_TESTUNLOCKED2:
        state = LC_STATE_TESTUNLOCKED2;
        break;
    case LC_ENC_STATE_TESTLOCKED2:
        state = LC_STATE_TESTLOCKED2;
        break;
    case LC_ENC_STATE_TESTUNLOCKED3:
        state = LC_STATE_TESTUNLOCKED3;
        break;
    case LC_ENC_STATE_TESTLOCKED3:
        state = LC_STATE_TESTLOCKED3;
        break;
    case LC_ENC_STATE_TESTUNLOCKED4:
        state = LC_STATE_TESTUNLOCKED4;
        break;
    case LC_ENC_STATE_TESTLOCKED4:
        state = LC_STATE_TESTLOCKED4;
        break;
    case LC_ENC_STATE_TESTUNLOCKED5:
        state = LC_STATE_TESTUNLOCKED5;
        break;
    case LC_ENC_STATE_TESTLOCKED5:
        state = LC_STATE_TESTLOCKED5;
        break;
    case LC_ENC_STATE_TESTUNLOCKED6:
        state = LC_STATE_TESTUNLOCKED6;
        break;
    case LC_ENC_STATE_TESTLOCKED6:
        state = LC_STATE_TESTLOCKED6;
        break;
    case LC_ENC_STATE_TESTUNLOCKED7:
        state = LC_STATE_TESTUNLOCKED7;
        break;
    case LC_ENC_STATE_DEV:
        state = LC_STATE_DEV;
        break;
    case LC_ENC_STATE_PROD:
        state = LC_STATE_PROD;
        break;
    case LC_ENC_STATE_PRODEND:
        state = LC_STATE_PRODEND;
        break;
    case LC_ENC_STATE_RMA:
        state = LC_STATE_RMA;
        break;
    case LC_ENC_STATE_SCRAP:
        state = LC_STATE_SCRAP;
        break;
    default:
        /* code validity should have been verified first */
        g_assert_not_reached();
    }

    return state;
}

static OtLcState ot_lc_ctrl_safe_convert_code_to_state(uint32_t enc_state)
{
    if (!ot_lc_ctrl_is_known_state(enc_state)) {
        return LC_STATE_INVALID;
    }

    return ot_lc_ctrl_convert_code_to_state(enc_state);
}

static uint32_t ot_lc_ctrl_get_target_state(const OtLcCtrlState *s)
{
    unsigned sreq = LC_XSLOT(s->owner);

    return s->xregs[sreq][R_TRANSITION_TARGET - R_FIRST_EXCLUSIVE_REG];
}

static void ot_lc_ctrl_load_hashed_token(OtLcCtrlState *s)
{
    g_assert(LC_XSLOT(s->owner) < EXCLUSIVE_SLOTS_COUNT);

    const uint32_t *xregs = s->xregs[LC_XSLOT(s->owner)];

    s->hash_token.lo =
        (uint64_t)xregs[XREGS_OFFSET(R_TRANSITION_TOKEN_0)] |
        (((uint64_t)xregs[XREGS_OFFSET(R_TRANSITION_TOKEN_1)]) << 32u);
    s->hash_token.hi =
        (uint64_t)xregs[XREGS_OFFSET(R_TRANSITION_TOKEN_2)] |
        (((uint64_t)xregs[XREGS_OFFSET(R_TRANSITION_TOKEN_3)]) << 32u);
}

static void ot_lc_ctrl_kmac_request(OtLcCtrlState *s)
{
    g_assert(LC_XSLOT(s->owner) < EXCLUSIVE_SLOTS_COUNT);

    const uint32_t *xregs = s->xregs[LC_XSLOT(s->owner)];
    const uint32_t *token =
        &xregs[(s->kmac_state == ST_KMAC_SECOND ?
                    XREGS_OFFSET(R_TRANSITION_TOKEN_2) :
                    XREGS_OFFSET(R_TRANSITION_TOKEN_0))];

    OtKMACAppReq req = {
        .msg_len = 8u,
        .last = s->kmac_state == ST_KMAC_SECOND,
    };
    stl_le_p(&req.msg_data[0], token[0]);
    stl_le_p(&req.msg_data[sizeof(uint32_t)], token[1]);

    TRACE_LC_CTRL("KMAC input: %s", ot_lc_ctrl_hexdump(&req.msg_data[0], 8u));

    ot_kmac_app_request(s->kmac, s->kmac_app, &req);
}

static void ot_lc_ctrl_kmac_handle_resp(void *opaque, const OtKMACAppRsp *rsp)
{
    OtLcCtrlState *s = opaque;

    if (s->kmac_state == ST_KMAC_FIRST) {
        g_assert(!rsp->done);
        s->kmac_state = ST_KMAC_SECOND;
        ot_lc_ctrl_kmac_request(s);
        s->kmac_state = ST_KMAC_WAIT;
        return;
    }

    g_assert(rsp->done);
    g_assert(s->kmac_state == ST_KMAC_WAIT);

    uint64_t dig0;
    uint64_t dig1;
    dig0 = ldq_le_p(&rsp->digest_share0[0]);
    dig1 = ldq_le_p(&rsp->digest_share1[0]);
    s->hash_token.lo = dig0 ^ dig1;
    dig0 = ldq_le_p(&rsp->digest_share0[sizeof(uint64_t)]);
    dig1 = ldq_le_p(&rsp->digest_share1[sizeof(uint64_t)]);
    s->hash_token.hi = dig0 ^ dig1;

    TRACE_LC_CTRL("MKAC output: %s",
                  ot_lc_ctrl_hexdump(&s->hash_token.lo, 16u));

    ot_lc_ctrl_resume_transition(s);
}

static uint32_t ot_lc_ctrl_load_lc_info(OtLcCtrlState *s)
{
    OtOTPStateClass *oc =
        OBJECT_GET_CLASS(OtOTPStateClass, s->otp_ctrl, TYPE_OT_OTP);
    uint32_t enc_state;
    uint8_t lc_valid;
    uint8_t secret_valid;
    const OtOTPTokens *tokens = NULL;
    oc->get_lc_info(s->otp_ctrl, &enc_state, &s->lc_tcount, &lc_valid,
                    &secret_valid, &tokens);

    switch (secret_valid) {
    case OT_MULTIBITBOOL_LC4_FALSE:
        /* blank */
        s->regs[R_LC_ID_STATE] = LC_ID_STATE_BLANK;
        break;
    case OT_MULTIBITBOOL_LC4_TRUE:
        /* personalized */
        s->regs[R_LC_ID_STATE] = LC_ID_STATE_PERSONALIZED;
        break;
    default:
        /* invalid */
        s->regs[R_LC_ID_STATE] = LC_ID_STATE_INVALID;
        break;
    }

    g_assert(tokens);

    uint32_t valid_bm = tokens->valid_bm;
    for (unsigned otix = 0; otix < OTP_TOKEN_COUNT; otix++) {
        /* beware: LC controller and OTP controller do not use same indices */
        unsigned ltix = otix + LC_TK_TEST_UNLOCK - OTP_TOKEN_TEST_UNLOCK;
        /* 'valid' is OT terminology, should be considered as 'defined' */
        bool valid = (bool)(valid_bm & (1u << otix));
        if (valid) {
            s->hashed_tokens[ltix] = tokens->values[otix];
            s->hashed_token_bm |= 1u << ltix;
        } else {
            s->hashed_tokens[ltix] = (OtOTPTokenValue){ 0, 0 };
            s->hashed_token_bm &= ~(1u << ltix);
        }
        trace_ot_lc_ctrl_load_otp_token(LC_TOKEN_NAME(ltix), ltix,
                                        valid ? "" : "in",
                                        s->hashed_tokens[ltix].hi,
                                        s->hashed_tokens[ltix].lo);
    }

    return lc_valid == OT_MULTIBITBOOL_LC4_TRUE ? enc_state : UINT32_MAX;
}

static void ot_lc_ctrl_load_otp_hw_cfg(OtLcCtrlState *s)
{
    OtOTPStateClass *oc =
        OBJECT_GET_CLASS(OtOTPStateClass, s->otp_ctrl, TYPE_OT_OTP);
    const OtOTPHWCfg *hw_cfg = oc->get_hw_cfg(s->otp_ctrl);

    memcpy(&s->regs[R_DEVICE_ID_0], &hw_cfg->device_id[0],
           sizeof(*hw_cfg->device_id));
    memcpy(&s->regs[R_MANUF_STATE_0], &hw_cfg->manuf_state[0],
           sizeof(*hw_cfg->manuf_state));
}

static void ot_lc_ctrl_handle_otp_ack(void *opaque, bool ack)
{
    OtLcCtrlState *s = opaque;

    switch (s->state) {
    case ST_IDLE:
        trace_ot_lc_ctrl_info("Ignore OTP completion in IDLE");
        break;
    case ST_CNT_PROG:
        LC_FSM_CHANGE_STATE(s, ST_TRANS_CHECK);
        /*
         * Notes:
         *  - FLASH RMA is not implemented (not available on Darjeeling)
         *  - Perform a unique Token Check (vs. 3 successive ones on real HW)
         */
        trace_ot_lc_ctrl_info("Request KMAC hashing");
        g_assert(s->kmac_state == ST_KMAC_IDLE);
        s->kmac_state = ST_KMAC_FIRST;
        LC_FSM_CHANGE_STATE(s, ST_TOKEN_HASH);
        ot_lc_ctrl_kmac_request(s);
        break;
    case ST_TRANS_PROG:
        if (ack) {
            trace_ot_lc_ctrl_info("Succesful transition programmation");
            s->regs[R_STATUS] |= R_STATUS_TRANSITION_SUCCESSFUL_MASK;
        } else {
            trace_ot_lc_ctrl_info("Failed to program transition");
            s->regs[R_STATUS] |= R_STATUS_OTP_ERROR_MASK;
        }
        LC_FSM_CHANGE_STATE(s, ST_POST_TRANS);
        break;
    default:
        g_assert_not_reached();
        break;
    }
}

static void ot_lc_ctrl_program_otp(OtLcCtrlState *s, OtLcState lc_state)
{
    OtOTPStateClass *oc =
        OBJECT_GET_CLASS(OtOTPStateClass, s->otp_ctrl, TYPE_OT_OTP);

    if (!oc->program_req) {
        qemu_log_mask(LOG_UNIMP,
                      "%s: OTP implementation does not support programming",
                      __func__);
        s->regs[R_STATUS] |= R_STATUS_OTP_ERROR_MASK;
        LC_FSM_CHANGE_STATE(s, ST_POST_TRANS);
        return;
    }

    uint32_t enc_state = LC_ENCODE_STATE(lc_state);

    if (!oc->program_req(s->otp_ctrl, enc_state, s->lc_tcount,
                         &ot_lc_ctrl_handle_otp_ack, s)) {
        trace_ot_lc_ctrl_error("OTP program request rejected");
        s->regs[R_STATUS] |= R_STATUS_STATE_ERROR_MASK;
        LC_FSM_CHANGE_STATE(s, ST_POST_TRANS);
        return;
    }
}

static void ot_lc_ctrl_start_transition(OtLcCtrlState *s)
{
    g_assert(s->state == ST_IDLE);

    s->regs[R_STATUS] &= ~R_STATUS_READY_MASK;

    bool tvolatile =
        (bool)(s->volatile_raw_unlock_bm & LC_XSLOT(1u << s->owner));

    uint32_t target_code = ot_lc_ctrl_get_target_state(s);
    OtLcState target = ot_lc_ctrl_safe_convert_code_to_state(target_code);

    trace_ot_lc_ctrl_start_transition(s->owner == LC_IF_SW ? "SW" : "DMI",
                                      !tvolatile ? "OTP" :
                                      s->volatile_raw_unlock ?
                                                   "unlocked volatile" :
                                                   "locked volatile",
                                      LC_STATE_NAME(s->lc_state), s->lc_state,
                                      LC_STATE_NAME(target), target,
                                      s->lc_tcount);

    if (s->volatile_raw_unlock && tvolatile) {
        if (s->lc_state == LC_STATE_RAW || target == LC_STATE_TESTUNLOCKED0) {
            ot_lc_ctrl_load_hashed_token(s);
            if (ot_lc_ctrl_match_token(s, LC_TK_RAW_UNLOCK)) {
                s->lc_state = LC_STATE_TESTUNLOCKED0;
                if (s->lc_tcount == 0) {
                    s->lc_tcount = 1u;
                }
                // TODO DFT start override (see RTL)
                // TODO change FSM behavior once this is selected
                s->volatile_unlocked = true;
                s->regs[R_STATUS] |= R_STATUS_TRANSITION_SUCCESSFUL_MASK;
                trace_ot_lc_ctrl_info("Successful volatile unlock");
                s->regs[R_STATUS] |= R_STATUS_READY_MASK;
                /* FSM state is kept in IDLE */
            } else {
                trace_ot_lc_ctrl_error("Invalid volatile unlock token");
                s->regs[R_STATUS] |= R_STATUS_TOKEN_ERROR_MASK;
                LC_FSM_CHANGE_STATE(s, ST_POST_TRANS);
            }
        } else {
            trace_ot_lc_ctrl_error("Invalid state(s) for volatile unlock");
            s->regs[R_STATUS] |= R_STATUS_TRANSITION_ERROR_MASK;
            LC_FSM_CHANGE_STATE(s, ST_POST_TRANS);
        }
        return;
    }

    LC_FSM_CHANGE_STATE(s, ST_CLK_MUX);
    switch (s->lc_state) {
    case LC_STATE_RAW:
    case LC_STATE_TESTUNLOCKED0:
    case LC_STATE_TESTLOCKED0:
    case LC_STATE_TESTUNLOCKED1:
    case LC_STATE_TESTLOCKED1:
    case LC_STATE_TESTUNLOCKED2:
    case LC_STATE_TESTLOCKED2:
    case LC_STATE_TESTUNLOCKED3:
    case LC_STATE_TESTLOCKED3:
    case LC_STATE_TESTUNLOCKED4:
    case LC_STATE_TESTLOCKED4:
    case LC_STATE_TESTUNLOCKED5:
    case LC_STATE_TESTLOCKED5:
    case LC_STATE_TESTUNLOCKED6:
    case LC_STATE_TESTLOCKED6:
    case LC_STATE_TESTUNLOCKED7:
    case LC_STATE_RMA:
        trace_ot_lc_ctrl_info("External clock enabled");
        s->regs[R_STATUS] |= R_STATUS_EXT_CLOCK_SWITCHED_MASK;
        break;
    default:
        break;
    }

    LC_FSM_CHANGE_STATE(s, ST_CNT_INCR);
    if (s->lc_tcount >= LC_TRANSITION_COUNT_MAX) {
        trace_ot_lc_ctrl_error("Max transition count reached");
        s->regs[R_STATUS] |= R_STATUS_TRANSITION_COUNT_ERROR_MASK;
        LC_FSM_CHANGE_STATE(s, ST_POST_TRANS);
        return;
    }

    if (target != LC_STATE_SCRAP) {
        s->lc_tcount += 1;
    } else {
        s->lc_tcount = LC_TRANSITION_COUNT_MAX;
    }

    LC_FSM_CHANGE_STATE(s, ST_CNT_PROG);

    ot_lc_ctrl_program_otp(s, s->lc_state);
}

static void ot_lc_ctrl_resume_transition(OtLcCtrlState *s)
{
    g_assert(s->state == ST_TOKEN_HASH);

    /*
     * Notes:
     *  - FLASH RMA is not implemented (not available on Darjeeling)
     *  - Perform a unique Token Chaeck (vs. 3 successive ones on real HW)
     */
    LC_FSM_CHANGE_STATE(s, ST_TOKEN_CHECK0);

    uint32_t target_code = ot_lc_ctrl_get_target_state(s);
    OtLcState target_state = ot_lc_ctrl_safe_convert_code_to_state(target_code);

    OtLcCtrlToken token;

    if ((s->lc_state < LC_STATE_VALID_COUNT) &&
        (target_state < LC_STATE_VALID_COUNT)) {
        token = LC_TRANS_TOKEN_MATRIX[s->lc_state][target_state];
    } else {
        token = LC_TK_INVALID;
    }

    trace_ot_lc_ctrl_transit_request(s->owner == LC_IF_SW ? "SW" : "DMI",
                                     LC_STATE_NAME(s->lc_state), s->lc_state,
                                     LC_STATE_NAME(target_state), target_state,
                                     LC_TOKEN_NAME(token), token);

    if (token == LC_TK_INVALID) {
        trace_ot_lc_ctrl_error("Invalid transition");
        s->regs[R_STATUS] |= R_STATUS_TRANSITION_ERROR_MASK;
        LC_FSM_CHANGE_STATE(s, ST_POST_TRANS);
    } else if (!ot_lc_ctrl_match_token(s, token)) {
        trace_ot_lc_ctrl_error("Invalid OTP token");
        s->regs[R_STATUS] |= R_STATUS_TOKEN_ERROR_MASK;
        LC_FSM_CHANGE_STATE(s, ST_POST_TRANS);
    } else {
        trace_ot_lc_ctrl_info("Valid token");

        LC_FSM_CHANGE_STATE(s, ST_TRANS_PROG);

        ot_lc_ctrl_program_otp(s, target_state);
    }
}

static void ot_lc_ctrl_initialize(OtLcCtrlState *s)
{
    s->regs[R_HW_REVISION0] =
        (((uint32_t)s->silicon_creator_id) << 16u) | ((uint32_t)s->product_id);
    s->regs[R_HW_REVISION1] = (uint32_t)s->revision_id;

    ot_kmac_connect_app(s->kmac, s->kmac_app, &ot_lc_ctrl_kmac_config,
                        &ot_lc_ctrl_kmac_handle_resp, s);

    s->hashed_tokens[LC_TK_ZERO] = LC_ALL_ZERO_TOKEN_HASHED;
    s->hashed_tokens[LC_TK_RAW_UNLOCK] = LC_RND_CNST_RAW_UNLOCK_TOKEN_HASHED;
    s->hashed_token_bm = (1u << LC_TK_ZERO) | (1u << LC_TK_RAW_UNLOCK);

    uint32_t enc_state = ot_lc_ctrl_load_lc_info(s);
    if (enc_state == UINT32_MAX) {
        trace_ot_lc_ctrl_error("LC invalid state");
        s->state_invalid_error_bm |= 1u << 0u;
    } else {
        s->regs[R_STATUS] |= R_STATUS_INITIALIZED_MASK;
    }

    if (!ot_lc_ctrl_is_known_state(enc_state)) {
        trace_ot_lc_ctrl_error("LC unknown state");
        s->state_invalid_error_bm |= 1u << 1u;
    } else {
        s->lc_state = ot_lc_ctrl_convert_code_to_state(enc_state);
    }

    if (s->lc_tcount > LC_TRANSITION_COUNT_MAX) {
        trace_ot_lc_ctrl_error("LC max transition count reached");
        s->state_invalid_error_bm |= 1u << 2u;
    }

    if (s->regs[R_LC_ID_STATE] == LC_ID_STATE_INVALID) {
        trace_ot_lc_ctrl_error("LC corrupted secret valid info");
        s->state_invalid_error_bm |= 1u << 3u;
    }

    if (s->lc_state != LC_STATE_RAW && s->lc_tcount == 0) {
        trace_ot_lc_ctrl_error("LC state non-RAW with zero transition count");
        s->state_invalid_error_bm |= 1u << 4u;
    }

    if (s->regs[R_LC_ID_STATE] == LC_ID_STATE_PERSONALIZED) {
        switch (s->lc_state) {
        case LC_STATE_DEV:
        case LC_STATE_PROD:
        case LC_STATE_PRODEND:
        case LC_STATE_RMA:
        case LC_STATE_SCRAP:
            break;
        default:
            trace_ot_lc_ctrl_error("Personalized ID state w/ no secrets");
            s->state_invalid_error_bm |= 1u << 5u;
        }
    }

    if (!s->state_invalid_error_bm) {
        ot_lc_ctrl_load_otp_hw_cfg(s);

        s->regs[R_STATUS] |= R_STATUS_READY_MASK;

        LC_FSM_CHANGE_STATE(s, ST_IDLE);

        if (s->lc_state == LC_STATE_SCRAP) {
            LC_FSM_CHANGE_STATE(s, ST_SCRAP);
        }
    } else {
        LC_FSM_CHANGE_STATE(s, ST_INVALID);
    }

    trace_ot_lc_ctrl_initialize(LC_STATE_NAME(s->lc_state), s->lc_state,
                                s->lc_tcount, LC_FSM_STATE_NAME(s->state),
                                s->state);
}

static void ot_lc_ctrl_pwr_lc_req(void *opaque, int n, int level)
{
    OtLcCtrlState *s = opaque;

    g_assert(n == 0);

    if (level) {
        trace_ot_lc_ctrl_pwr_lc_req("signaled");
        qemu_bh_schedule(s->pwc_lc_bh);
    }
}

static void ot_lc_ctrl_escalate_rx(void *opaque, int n, int level)
{
    OtLcCtrlState *s = opaque;

    g_assert((unsigned)n < 2u);

    trace_ot_lc_ctrl_escalate_rx((unsigned)n, (bool)level);

    if (level) {
        qemu_bh_schedule(s->escalate_bh);
    }
}

static void ot_lc_ctrl_escalate_bh(void *opaque)
{
    OtLcCtrlState *s = opaque;

    LC_FSM_CHANGE_STATE(s, ST_ESCALATE);

    ot_lc_ctrl_update_broadcast(s);
}

static void ot_lc_ctrl_pwr_lc_bh(void *opaque)
{
    OtLcCtrlState *s = opaque;

    trace_ot_lc_ctrl_pwr_lc_req("initialize");

    ot_lc_ctrl_initialize(s);

    ot_lc_ctrl_update_broadcast(s);

    trace_ot_lc_ctrl_pwr_lc_req("done");

    ibex_irq_set(&s->pwc_lc_rsp, 1);
    ibex_irq_set(&s->pwc_lc_rsp, 0);
}

/* NOLINTBEGIN */
static_assert(R_FIRST_EXCLUSIVE_REG == R_TRANSITION_TOKEN_0,
              "Incoherent exclusive reg definition");
static_assert(R_LAST_EXCLUSIVE_REG == R_TRANSITION_TARGET,
              "Incoherent exclusive reg definition");
/* NOLINTEND */

static uint32_t ot_lc_ctrl_regs_read(OtLcCtrlState *s, hwaddr addr,
                                     OtLcCtrlIf ifreq)
{
    uint32_t val32;

    hwaddr reg = R32_OFF(addr);

    switch (reg) {
    case R_LC_TRANSITION_CNT:
        // TODO: >= 24 -> state == SCRAP
        // Error: should be 31
        val32 = s->lc_tcount;
        break;
    case R_LC_STATE:
        val32 = LC_ENCODE_STATE(s->lc_state);
        break;
    case R_OTP_VENDOR_TEST_STATUS:
        val32 = ot_lc_ctrl_is_hw_mutex_owner(s, ifreq) &&
                        ot_lc_ctrl_is_vendor_test_state(s->lc_state) ?
                    s->regs[reg] :
                    0u;
        break;
    case R_OTP_VENDOR_TEST_CTRL:
        val32 = ot_lc_ctrl_is_hw_mutex_owner(s, ifreq) ? s->regs[reg] : 0u;
        break;
    case R_CLAIM_TRANSITION_IF:
        val32 = ot_lc_ctrl_is_hw_mutex_owner(s, ifreq) ?
                    OT_MULTIBITBOOL8_TRUE :
                    OT_MULTIBITBOOL8_FALSE;
        break;
    case R_TRANSITION_CTRL:
        val32 = 0;
        if (ot_lc_ctrl_is_transition_en(s, ifreq)) {
            if (s->ext_clock_en) {
                val32 |= R_TRANSITION_CTRL_EXT_CLOCK_EN_MASK;
            }
            if (s->volatile_raw_unlock_bm & (1u << LC_XSLOT(ifreq))) {
                val32 |= R_TRANSITION_CTRL_VOLATILE_RAW_UNLOCK_MASK;
            }
        }
        break;
    case R_TRANSITION_REGWEN:
        val32 = ot_lc_ctrl_is_transition_en(s, ifreq) ?
                    R_TRANSITION_REGWEN_EN_MASK :
                    0;
        break;
    case R_TRANSITION_TOKEN_0:
    case R_TRANSITION_TOKEN_1:
    case R_TRANSITION_TOKEN_2:
    case R_TRANSITION_TOKEN_3:
    case R_TRANSITION_TARGET:
        g_assert(LC_XSLOT(ifreq) < EXCLUSIVE_SLOTS_COUNT);
        val32 = s->xregs[LC_XSLOT(ifreq)][reg - R_FIRST_EXCLUSIVE_REG];
        break;
    case R_STATUS:
    case R_TRANSITION_CMD:
    case R_CLAIM_TRANSITION_IF_REGWEN:
    case R_LC_ID_STATE:
    case R_HW_REVISION0:
    case R_HW_REVISION1:
    case R_DEVICE_ID_0:
    case R_DEVICE_ID_1:
    case R_DEVICE_ID_2:
    case R_DEVICE_ID_3:
    case R_DEVICE_ID_4:
    case R_DEVICE_ID_5:
    case R_DEVICE_ID_6:
    case R_DEVICE_ID_7:
    case R_MANUF_STATE_0:
    case R_MANUF_STATE_1:
    case R_MANUF_STATE_2:
    case R_MANUF_STATE_3:
    case R_MANUF_STATE_4:
    case R_MANUF_STATE_5:
    case R_MANUF_STATE_6:
    case R_MANUF_STATE_7:
        val32 = s->regs[reg];
        break;
    case R_ALERT_TEST:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: W/O register 0x%02" HWADDR_PRIx " (%s)\n", __func__,
                      addr, REG_NAME(reg));
        val32 = 0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        val32 = 0;
        break;
    }

    uint32_t pc = ibex_get_current_pc();
    if (reg != R_STATUS) {
        trace_ot_lc_ctrl_io_read_out((uint32_t)addr, REG_NAME(reg), val32, pc);
        s->status_cache.count = 0;
    } else {
        /*
         * special trace for STATUS register: as LC_CTRL does not support an
         * INTR channel, the SW needs to poll -a lot- the status register to
         * check once an update operation is completed. To avoid flooding the
         * trace log with many subsequent call traces to STATUS read out, track
         * how many times the last STATUS read out has been repeated
         */
        if (s->status_cache.value == val32 && s->status_cache.count) {
            s->status_cache.count += 1;
        } else {
            if (s->status_cache.count) {
                trace_ot_lc_ctrl_io_read_out_repeat((uint32_t)addr,
                                                    REG_NAME(reg),
                                                    s->status_cache.count,
                                                    s->status_cache.value);
            }
            s->status_cache.value = val32;
            s->status_cache.count = 1;
            trace_ot_lc_ctrl_io_read_out((uint32_t)addr, REG_NAME(reg), val32,
                                         pc);
        }
    }

    return val32;
};

static void ot_lc_ctrl_regs_write(OtLcCtrlState *s, hwaddr addr, uint32_t val32,
                                  OtLcCtrlIf ifreq)
{
    hwaddr reg = R32_OFF(addr);

    uint32_t pc = ibex_get_current_pc();
    trace_ot_lc_ctrl_io_write((uint32_t)addr, REG_NAME(reg), val32, pc);

    switch (reg) {
    case R_ALERT_TEST:
        val32 &= ALERT_TEST_MASK;
        s->regs[R_ALERT_TEST] = val32;
        ot_lc_ctrl_update_alerts(s);
        break;
    case R_CLAIM_TRANSITION_IF_REGWEN:
        val32 &= R_CLAIM_TRANSITION_IF_REGWEN_EN_MASK;
        s->regs[reg] &= val32; /* rw0c */
        break;
    case R_CLAIM_TRANSITION_IF:
        if (!(s->regs[R_CLAIM_TRANSITION_IF_REGWEN] &
              R_CLAIM_TRANSITION_IF_REGWEN_EN_MASK)) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: CLAIM_TRANSITION_IF disabled\n",
                          __func__);
            break;
        }
        val32 &= R_CLAIM_TRANSITION_IF_MUTEX_MASK;
        if (val32 == OT_MULTIBITBOOL8_TRUE) {
            ot_lc_ctrl_lock_hw_mutex(s, ifreq);
        } else {
            ot_lc_ctrl_release_hw_mutex(s);
        }
        break;
    case R_TRANSITION_CMD:
        val32 &= R_TRANSITION_CMD_START_MASK;
        if (val32) {
            if (!ot_lc_ctrl_is_transition_en(s, ifreq)) {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: LC IF not available\n",
                              __func__);
                break;
            }
            ot_lc_ctrl_start_transition(s);
        }
        break;
    case R_TRANSITION_CTRL:
        if (!ot_lc_ctrl_is_transition_en(s, ifreq)) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: LC IF not available\n",
                          __func__);
            break;
        }
        if (val32 & R_TRANSITION_CTRL_EXT_CLOCK_EN_MASK) {
            s->ext_clock_en = true; /* rw1s */
        }
        if (s->volatile_raw_unlock) {
            if (val32 & R_TRANSITION_CTRL_VOLATILE_RAW_UNLOCK_MASK) {
                s->volatile_raw_unlock_bm |= 1u << LC_XSLOT(ifreq);
            } else {
                s->volatile_raw_unlock_bm &= ~(1u << LC_XSLOT(ifreq));
            }
        }
        break;
    case R_TRANSITION_TOKEN_0:
    case R_TRANSITION_TOKEN_1:
    case R_TRANSITION_TOKEN_2:
    case R_TRANSITION_TOKEN_3:
        if (!ot_lc_ctrl_is_transition_en(s, ifreq)) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: LC IF not available\n",
                          __func__);
            break;
        }
        g_assert(LC_XSLOT(ifreq) < EXCLUSIVE_SLOTS_COUNT);
        s->xregs[LC_XSLOT(ifreq)][reg - R_FIRST_EXCLUSIVE_REG] = val32;
        break;
    case R_TRANSITION_TARGET:
        if (!ot_lc_ctrl_is_transition_en(s, ifreq)) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: LC IF not available\n",
                          __func__);
            break;
        }
        val32 &= R_TRANSITION_TARGET_STATE_MASK;
        if (ot_lc_ctrl_is_known_state(val32)) {
            g_assert(LC_XSLOT(ifreq) < EXCLUSIVE_SLOTS_COUNT);
            s->xregs[LC_XSLOT(ifreq)][reg - R_FIRST_EXCLUSIVE_REG] = val32;
        }
        break;
    case R_OTP_VENDOR_TEST_CTRL:
        if (!ot_lc_ctrl_is_transition_en(s, ifreq)) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: LC IF not available\n",
                          __func__);
            break;
        }
        s->regs[reg] = val32;
        break;
    case R_STATUS:
    case R_TRANSITION_REGWEN:
    case R_OTP_VENDOR_TEST_STATUS:
    case R_LC_STATE:
    case R_LC_TRANSITION_CNT:
    case R_LC_ID_STATE:
    case R_HW_REVISION0:
    case R_HW_REVISION1:
    case R_DEVICE_ID_0:
    case R_DEVICE_ID_1:
    case R_DEVICE_ID_2:
    case R_DEVICE_ID_3:
    case R_DEVICE_ID_4:
    case R_DEVICE_ID_5:
    case R_DEVICE_ID_6:
    case R_DEVICE_ID_7:
    case R_MANUF_STATE_0:
    case R_MANUF_STATE_1:
    case R_MANUF_STATE_2:
    case R_MANUF_STATE_3:
    case R_MANUF_STATE_4:
    case R_MANUF_STATE_5:
    case R_MANUF_STATE_6:
    case R_MANUF_STATE_7:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: R/O register 0x%02" HWADDR_PRIx " (%s)\n", __func__,
                      addr, REG_NAME(reg));
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }
};

static uint64_t
ot_lc_ctrl_sw_regs_read(void *opaque, hwaddr addr, unsigned size)
{
    OtLcCtrlState *s = opaque;
    (void)size;

    return (uint32_t)ot_lc_ctrl_regs_read(s, addr, LC_IF_SW);
}

static void ot_lc_ctrl_sw_regs_write(void *opaque, hwaddr addr, uint64_t val64,
                                     unsigned size)
{
    OtLcCtrlState *s = opaque;
    (void)size;

    ot_lc_ctrl_regs_write(s, addr, (uint32_t)val64, LC_IF_SW);
}

static uint64_t
ot_lc_ctrl_dmi_regs_read(void *opaque, hwaddr addr, unsigned size)
{
    OtLcCtrlState *s = opaque;
    (void)size;

    return (uint32_t)ot_lc_ctrl_regs_read(s, addr, LC_IF_DMI);
}

static void ot_lc_ctrl_dmi_regs_write(void *opaque, hwaddr addr, uint64_t val64,
                                      unsigned size)
{
    OtLcCtrlState *s = opaque;
    (void)size;

    ot_lc_ctrl_regs_write(s, addr, (uint32_t)val64, LC_IF_DMI);
}

static Property ot_lc_ctrl_properties[] = {
    DEFINE_PROP_LINK("otp_ctrl", OtLcCtrlState, otp_ctrl, TYPE_OT_OTP,
                     OtOTPState *),
    DEFINE_PROP_LINK("kmac", OtLcCtrlState, kmac, TYPE_OT_KMAC, OtKMACState *),
    DEFINE_PROP_UINT16("silicon_creator_id", OtLcCtrlState, silicon_creator_id,
                       0),
    DEFINE_PROP_UINT16("product_id", OtLcCtrlState, product_id, 0),
    DEFINE_PROP_UINT8("revision_id", OtLcCtrlState, revision_id, 0),
    DEFINE_PROP_BOOL("volatile_raw_unlock", OtLcCtrlState, volatile_raw_unlock,
                     true),
    DEFINE_PROP_UINT8("kmac-app", OtLcCtrlState, kmac_app, UINT8_MAX),
    DEFINE_PROP_END_OF_LIST(),
};

static const MemoryRegionOps ot_lc_ctrl_sw_regs_ops = {
    .read = &ot_lc_ctrl_sw_regs_read,
    .write = &ot_lc_ctrl_sw_regs_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 4u,
    .impl.max_access_size = 4u,
};

static const MemoryRegionOps ot_lc_ctrl_dmi_regs_ops = {
    .read = &ot_lc_ctrl_dmi_regs_read,
    .write = &ot_lc_ctrl_dmi_regs_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 4u,
    .impl.max_access_size = 4u,
};

static void ot_lc_ctrl_reset(DeviceState *dev)
{
    OtLcCtrlState *s = OT_LC_CTRL(dev);

    trace_ot_lc_ctrl_reset();

    g_assert(s->otp_ctrl);
    g_assert(s->kmac);
    g_assert(s->kmac_app != UINT8_MAX);

    /*
     * "ID of the silicon creator. Assigned by the OpenTitan project.
     * 0x0000: invalid value
     * 0x0001 - 0x3FFF: reserved for use in the open-source OpenTitan project
     * 0x4000 - 0x7FFF: reserved for real integrations of OpenTitan
     * 0x8000 - 0xFFFF: reserved for future use"
     */
    if (s->silicon_creator_id == 0 || s->silicon_creator_id > 0x8000) {
        error_setg(&error_fatal, "Invalid silicon_creator_id: 0x%04x",
                   s->silicon_creator_id);
    }

    /*
     * "Used to identify a class of devices. Assigned by the Silicon Creator
     * 0x0000: invalid value
     * 0x0001 - 0x3FFF: reserved for discrete chip products
     * 0x4000 - 0x7FFF: reserved for integrated IP products
     * 0x8000 - 0xFFFF: reserved for future use"
     */
    if (s->product_id == 0 || s->product_id > 0x8000) {
        error_setg(&error_fatal, "Invalid product_id: 0x%04x", s->product_id);
    }

    /*
     * "Product revision ID. Assigned by the Silicon Creator
     * Zero is an invalid value."
     */
    if (s->revision_id == 0) {
        error_setg(&error_fatal, "Invalid revision_id: 0x%02x", s->revision_id);
    }

    memset(s->regs, 0, REGS_SIZE);
    memset(s->xregs, 0, sizeof(s->xregs));

    s->owner = LC_IF_NONE;
    LC_FSM_CHANGE_STATE(s, ST_RESET);
    s->kmac_state = ST_KMAC_IDLE;
    s->regs[R_CLAIM_TRANSITION_IF] = OT_MULTIBITBOOL8_FALSE;
    s->regs[R_CLAIM_TRANSITION_IF_REGWEN] = 1u;
    s->ext_clock_en = false;
    s->volatile_unlocked = false;
    s->volatile_raw_unlock_bm = 0;
    s->state_invalid_error_bm = 0;

    memset(&s->status_cache, 0, sizeof(s->status_cache));

    ot_lc_ctrl_update_alerts(s);

    for (unsigned ix = 0; ix < ARRAY_SIZE(s->broadcasts); ix++) {
        ibex_irq_set(&s->broadcasts[ix], 0);
    }

    ibex_irq_set(&s->pwc_lc_rsp, 0);

    s->lc_state = LC_STATE_INVALID;
    s->lc_tcount = LC_TRANSITION_COUNT_MAX + 1u;
    s->km_div = LC_DIV_INVALID;

    /*
     * do not broadcast the current status, wait for initialization to happen,
     * triggered by the Power Manager
     */
}

static void ot_lc_ctrl_init(Object *obj)
{
    OtLcCtrlState *s = OT_LC_CTRL(obj);

    memory_region_init_io(&s->mmio, obj, &ot_lc_ctrl_sw_regs_ops, s,
                          TYPE_OT_LC_CTRL, REGS_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);

    memory_region_init_io(&s->dmi_mmio, obj, &ot_lc_ctrl_dmi_regs_ops, s,
                          TYPE_OT_LC_CTRL, REGS_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->dmi_mmio);

    s->regs = g_new0(uint32_t, REGS_COUNT);

    for (unsigned ix = 0; ix < ARRAY_SIZE(s->alerts); ix++) {
        ibex_qdev_init_irq(obj, &s->alerts[ix], OT_DEVICE_ALERT);
    }

    for (unsigned ix = 0; ix < ARRAY_SIZE(s->broadcasts); ix++) {
        ibex_qdev_init_irq(obj, &s->broadcasts[ix], OT_LC_BROADCAST);
    }

    ibex_qdev_init_irq(obj, &s->pwc_lc_rsp, OT_PWRMGR_LC_RSP);

    qdev_init_gpio_in_named(DEVICE(obj), &ot_lc_ctrl_pwr_lc_req,
                            OT_PWRMGR_LC_REQ, 1);
    qdev_init_gpio_in_named(DEVICE(obj), &ot_lc_ctrl_escalate_rx,
                            OT_ALERT_ESCALATE, 2);

    s->pwc_lc_bh = qemu_bh_new(&ot_lc_ctrl_pwr_lc_bh, s);
    s->escalate_bh = qemu_bh_new(&ot_lc_ctrl_escalate_bh, s);
}

static void ot_lc_ctrl_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    (void)data;

    dc->reset = &ot_lc_ctrl_reset;
    device_class_set_props(dc, ot_lc_ctrl_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo ot_lc_ctrl_info = {
    .name = TYPE_OT_LC_CTRL,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(OtLcCtrlState),
    .instance_init = &ot_lc_ctrl_init,
    .class_init = &ot_lc_ctrl_class_init,
};

static void ot_lc_ctrl_register_types(void)
{
    type_register_static(&ot_lc_ctrl_info);
}

type_init(ot_lc_ctrl_register_types);
