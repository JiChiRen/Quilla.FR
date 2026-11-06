/*
 * Copyright (c) 2026, JiChiRen Info. Tech.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the conditions of the BSD-3
 * license referenced in the project LICENSE file are met.
 *
 * This file is part of the Quilla.FR appliance.
 */
/*
 * app_qboot_secver.c - firmware security-version region for the QBOOT gate.
 *
 * Despite the filename, this file is entirely appliance-owned: it includes
 * only appliance/Inc/qfr_secver.h and no Quilla source or header.
 * "qboot" in the name describes the protocol the data serves, not a code
 * dependency. The same applies to app_qboot_meta.c, which writes the META
 * flag using constants defined locally in app_qboot_meta.h.
 *
 * The 8-byte region sits at image offset 0x400 (0x08007400) and is covered
 * by the QBOOT image digest / signature. SEALED and SEC read the version
 * from it for the anti-rollback floor; CHECK ignores it. Increment
 * QFR_SECVER_VERSION for each release that must not be downgraded.
 */
#include "qfr_secver.h"

/* Exactly one definition, at file scope. */
QFR_SECVER_PLACE();
