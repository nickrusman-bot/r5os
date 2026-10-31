/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "AirPrint.h"
#include "AirPrintCap.h"
#include "PrinterDriver.h"


class AirPrintPrinterDriver : public PrinterDriver {
public:
	AirPrintPrinterDriver(BNode* printerFolder)
		:
		PrinterDriver(printerFolder)
	{
	}

	const char* GetSignature() const
	{
		return "application/x-vnd.r5os-airprint";
	}

	const char* GetDriverName() const
	{
		return "AirPrint";
	}

	const char* GetVersion() const
	{
		return "0.1";
	}

	const char* GetCopyright() const
	{
		return "AirPrint / IPP Everywhere driver for R5OS.\n";
	}

	PrinterCap* InstantiatePrinterCap(PrinterData* printerData)
	{
		return new AirPrintCap(printerData);
	}

	GraphicsDriver* InstantiateGraphicsDriver(BMessage* settings,
		PrinterData* printerData, PrinterCap* printerCap)
	{
		return new AirPrintDriver(settings, printerData, printerCap);
	}
};


PrinterDriver*
instantiate_printer_driver(BNode* printerFolder)
{
	return new AirPrintPrinterDriver(printerFolder);
}
