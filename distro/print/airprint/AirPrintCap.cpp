/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "AirPrintCap.h"

#define TO72DPI(a)	((a) * 72.0f / 300.0f)

static const PaperCap letter(
	"Letter", false, JobData::kLetter,
	BRect(0.0f, 0.0f, TO72DPI(2550.0f), TO72DPI(3300.0f)),
	BRect(TO72DPI(50.0f), TO72DPI(50.0f), TO72DPI(2500.0f), TO72DPI(3250.0f)));

static const PaperCap legal(
	"Legal", false, JobData::kLegal,
	BRect(0.0f, 0.0f, TO72DPI(2550.0f), TO72DPI(4200.0f)),
	BRect(TO72DPI(50.0f), TO72DPI(50.0f), TO72DPI(2500.0f), TO72DPI(4150.0f)));

static const PaperCap a4(
	"A4", true, JobData::kA4,
	BRect(0.0f, 0.0f, TO72DPI(2480.0f), TO72DPI(3508.0f)),
	BRect(TO72DPI(50.0f), TO72DPI(50.0f), TO72DPI(2430.0f), TO72DPI(3458.0f)));

static const PaperCap a5(
	"A5", false, JobData::kA5,
	BRect(0.0f, 0.0f, TO72DPI(1748.0f), TO72DPI(2480.0f)),
	BRect(TO72DPI(50.0f), TO72DPI(50.0f), TO72DPI(1698.0f), TO72DPI(2430.0f)));

static const PaperCap* papers[] = { &a4, &letter, &legal, &a5 };

static const PaperSourceCap autobin("Auto", true, JobData::kAuto);
static const PaperSourceCap* paperSources[] = { &autobin };

static const ResolutionCap dpi300("300dpi", true, 1, 300, 300);
static const ResolutionCap dpi600("600dpi", false, 2, 600, 600);
static const ResolutionCap* resolutions[] = { &dpi300, &dpi600 };

static const ColorCap color("Color", true, JobData::kColor);
static const ColorCap monochrome("Shades of Gray", false, JobData::kMonochrome);
static const ColorCap* colors[] = { &color, &monochrome };

static const PrintStyleCap simplex("Simplex", true, JobData::kSimplex);
static const PrintStyleCap* printStyles[] = { &simplex };


AirPrintCap::AirPrintCap(const PrinterData* printerData)
	:
	PrinterCap(printerData)
{
}


int
AirPrintCap::CountCap(CapID capid) const
{
	switch (capid) {
		case kPaper:
			return (int)(sizeof(papers) / sizeof(papers[0]));
		case kPaperSource:
			return (int)(sizeof(paperSources) / sizeof(paperSources[0]));
		case kResolution:
			return (int)(sizeof(resolutions) / sizeof(resolutions[0]));
		case kColor:
			return (int)(sizeof(colors) / sizeof(colors[0]));
		case kPrintStyle:
			return (int)(sizeof(printStyles) / sizeof(printStyles[0]));
		default:
			return 0;
	}
}


const BaseCap**
AirPrintCap::GetCaps(CapID capid) const
{
	switch (capid) {
		case kPaper:
			return (const BaseCap**)papers;
		case kPaperSource:
			return (const BaseCap**)paperSources;
		case kResolution:
			return (const BaseCap**)resolutions;
		case kColor:
			return (const BaseCap**)colors;
		case kPrintStyle:
			return (const BaseCap**)printStyles;
		default:
			return NULL;
	}
}


bool
AirPrintCap::Supports(CapID capid) const
{
	switch (capid) {
		case kPaper:
		case kPaperSource:
		case kResolution:
		case kColor:
		case kPrintStyle:
			return true;
		default:
			return false;
	}
}
