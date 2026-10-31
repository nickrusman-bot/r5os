/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef AIRPRINT_CAP_H
#define AIRPRINT_CAP_H

#include "PrinterCap.h"

class AirPrintCap : public PrinterCap {
public:
					AirPrintCap(const PrinterData* printerData);
	virtual	int		CountCap(CapID capid) const;
	virtual	bool	Supports(CapID capid) const;
	virtual	const BaseCap**	GetCaps(CapID capid) const;
};

#endif
