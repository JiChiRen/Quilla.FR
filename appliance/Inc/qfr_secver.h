/*
 * qfr_secver.h - firmware security-version region, owned by the appliance.
 *
 * This header belongs to the FreeRTOS appliance. It includes NO Quilla header
 * and is not shared with QBOOT. The two images agree only on a binary
 * contract: 8 bytes at image offset 0x400 (absolute 0x08007400),
 * magic "QAV1" (0x31564151) + u32 LE version, inside the signed image.
 * QBOOT reads those bytes from Flash; this image emits them here.
 *
 * Placement is toolchain-specific but resolves to the same address:
 *   GNU ld     -> .quilla_secver, pinned by appliance/gcc/py32f040xb.ld
 *   armlink    -> .ARM.__at_0x08007400, placed by absolute section name
 * Both survive regenerated scatter/linker scripts.
 */
#ifndef QFR_SECVER_H_
#define QFR_SECVER_H_

#include <stdint.h>

#define QFR_SECVER_ADDR        0x08007400U
#define QFR_SECVER_MAGIC_U32   0x31564151U   /* "QAV1" */

/* Increment monotonically for releases that must not be rolled back.
 * Overridable from the build command line. */
#ifndef QFR_SECVER_VERSION
#define QFR_SECVER_VERSION     1U
#endif

#if defined(__ARMCC_VERSION)
#define QFR_SECVER_SECTION    ".ARM.__at_0x08007400"
#else
#define QFR_SECVER_SECTION    ".quilla_secver"
#endif

typedef struct {
  uint32_t magic;
  uint32_t version;
} qfr_secver_t;

#define QFR_SECVER_PLACE()                                              \
  const qfr_secver_t qfr_secver                                      \
    __attribute__((section(QFR_SECVER_SECTION), used)) = {              \
      QFR_SECVER_MAGIC_U32, QFR_SECVER_VERSION                       \
    }

#endif /* QFR_SECVER_H_ */
