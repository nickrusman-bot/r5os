/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef AIRPRINT_H
#define AIRPRINT_H

#include "GraphicsDriver.h"

class AirPrintDriver : public GraphicsDriver {
public:
					AirPrintDriver(BMessage* message, PrinterData* printerData,
						const PrinterCap* printerCap);

protected:
	virtual	bool	StartDocument();
	virtual	bool	StartPage(int page);
	virtual	bool	NextBand(BBitmap* bitmap, BPoint* offset);
	virtual	bool	EndPage(int page);
	virtual	bool	EndDocument(bool success);

private:
			void	_WritePwgHeader(int width, int height);
			bool	fWroteFileHeader;
			bool	fWrotePageHeader;
};

#endif
