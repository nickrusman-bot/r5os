/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Whether an ACPI child stops the I2C walk, and whether a STOP_DET
 * wait is tried again. pch_i2c.cpp stays MIT and calls these.
 */

#ifndef R5_I2C_SCAN_H
#define R5_I2C_SCAN_H

/* A missing address or a failed step continues the walk. Zero is B_OK. */
static inline int
r5_i2c_child_continue(int stepStatus, int addressMissing)
{
	if (addressMissing || stepStatus != 0)
		return 0;
	return stepStatus;
}


/* waitOk is 1 when STOP_DET arrived. A timeout returns 0 and the
 * caller leaves the loop. */
static inline int
r5_i2c_stop_wait_again(int waitOk)
{
	return waitOk ? 1 : 0;
}

#endif
