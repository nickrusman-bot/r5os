#include "AirPrint.h"

#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

#include <Bitmap.h>

#include "JobData.h"
#include "PrinterData.h"


static const uint32 kPwgHeaderSize = 1796;


static void
put_be32(uint8* p, uint32 value)
{
	uint32 be = htonl(value);
	memcpy(p, &be, 4);
}


AirPrintDriver::AirPrintDriver(BMessage* message, PrinterData* printerData,
	const PrinterCap* printerCap)
	:
	GraphicsDriver(message, printerData, printerCap),
	fWroteFileHeader(false),
	fWrotePageHeader(false)
{
}


bool
AirPrintDriver::StartDocument()
{
	fWroteFileHeader = false;
	return true;
}


bool
AirPrintDriver::StartPage(int)
{
	fWrotePageHeader = false;
	return true;
}


bool
AirPrintDriver::EndPage(int)
{
	return true;
}


bool
AirPrintDriver::EndDocument(bool)
{
	return true;
}


void
AirPrintDriver::_WritePwgHeader(int width, int height)
{
	uint8 header[kPwgHeaderSize];
	int32 xres = GetJobData()->GetXres();
	int32 yres = GetJobData()->GetYres();
	BRect paper = GetJobData()->GetPaperRect();
	uint32 bytesPerLine = (uint32)((width * 3 + 3) & ~3);

	if (xres <= 0)
		xres = 300;
	if (yres <= 0)
		yres = 300;

	memset(header, 0, sizeof(header));
	strlcpy((char*)header + 0, "PwgRaster", 64);
	put_be32(header + 276, (uint32)xres);
	put_be32(header + 280, (uint32)yres);
	put_be32(header + 340, 1);
	put_be32(header + 352, (uint32)(paper.Width() + 1));
	put_be32(header + 356, (uint32)(paper.Height() + 1));
	put_be32(header + 372, (uint32)width);
	put_be32(header + 376, (uint32)height);
	put_be32(header + 384, 8);
	put_be32(header + 388, 24);
	put_be32(header + 392, bytesPerLine);
	put_be32(header + 396, 0);
	put_be32(header + 400, 19);
	put_be32(header + 420, 3);
	strlcpy((char*)header + 1732, "iso_a4_210x297mm", 64);
	WriteSpoolData(header, sizeof(header));
}


bool
AirPrintDriver::NextBand(BBitmap* bitmap, BPoint* offset)
{
	if (bitmap == NULL || offset == NULL)
		return false;

	int width = bitmap->Bounds().IntegerWidth() + 1;
	int bandHeight = bitmap->Bounds().IntegerHeight() + 1;
	int y = (int)offset->y;
	uint8* src = (uint8*)bitmap->Bits();
	int32 srcBpr = bitmap->BytesPerRow();
	uint32 dstBpr = (uint32)((width * 3 + 3) & ~3);
	uint8* row = (uint8*)malloc(dstBpr);
	int i;

	if (row == NULL)
		return false;

	try {
		if (!fWroteFileHeader) {
			WriteSpoolData("RaS2", 4);
			fWroteFileHeader = true;
		}
		if (!fWrotePageHeader) {
			_WritePwgHeader(width, GetPageHeight());
			fWrotePageHeader = true;
		}

		memset(row, 0xff, dstBpr);
		for (i = 0; i < bandHeight; i++) {
			ConvertToRGB24(src + i * srcBpr, row, width, bitmap->ColorSpace());
			WriteSpoolData(row, dstBpr);
		}
	} catch (...) {
		free(row);
		return false;
	}
	free(row);

	y += bandHeight;
	if (y >= GetPageHeight()) {
		offset->x = -1;
		offset->y = -1;
	} else
		offset->y = y;
	return true;
}
