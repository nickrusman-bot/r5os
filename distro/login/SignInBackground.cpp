/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


#include "SignInBackground.h"

#include <Bitmap.h>
#include <Gradient.h>
#include <GradientLinear.h>
#include <Shape.h>
#include <TranslationUtils.h>

#include <math.h>
#include <new>


// #pragma mark - the look, change it here


// A picture that fills the screen instead of the drawn waves, for example
// "/boot/system/data/artwork/sign-in.jpg". Any format the Translation Kit
// reads. It is scaled to cover the screen and cropped at the edges. When
// it is NULL, or the file cannot be read, the waves below are drawn.
static const char* kBackgroundPicture = NULL;


struct gradient_stop {
	float		offset;		// 0 is the top (or start), 1 the bottom (or end)
	rgb_color	color;
};


// The sky, from the top of the screen to the bottom.
static const gradient_stop kSky[] = {
	{ 0.00f, { 0xdb, 0xe8, 0xfa, 255 } },
	{ 0.45f, { 0xc6, 0xdc, 0xfa, 255 } },
	{ 1.00f, { 0xb1, 0xce, 0xf6, 255 } },
};


// A wave is a filled band from a curved top edge down to the bottom of
// the screen. The points are on a 1920 x 1080 canvas that is stretched to
// the screen. `start` is the left end of the edge; each curve is two
// control points and an end point, as in SVG "C". The color runs from
// the top left to the bottom right of the band.
struct wave {
	BPoint		start;
	BPoint		curves[2][3];
	rgb_color	from;
	rgb_color	to;
	uint8		alpha;		// 255 is solid
};


static const wave kWaves[] = {
	{
		BPoint(0, 420),
		{
			{ BPoint(380, 280), BPoint(740, 390), BPoint(1100, 540) },
			{ BPoint(1460, 690), BPoint(1720, 680), BPoint(1920, 600) },
		},
		{ 0x9f, 0xc4, 0xf5, 255 }, { 0x81, 0xae, 0xe9, 255 }, 217
	},
	{
		BPoint(0, 700),
		{
			{ BPoint(430, 590), BPoint(800, 700), BPoint(1180, 840) },
			{ BPoint(1510, 960), BPoint(1760, 950), BPoint(1920, 910) },
		},
		{ 0x7e, 0xa6, 0xe3, 255 }, { 0x64, 0x8e, 0xd6, 255 }, 242
	},
};


static const float kCanvasWidth = 1920.0f;
static const float kCanvasHeight = 1080.0f;


// #pragma mark - drawing


static BPoint
to_screen(BPoint point, BRect bounds)
{
	return BPoint(bounds.left + point.x * (bounds.Width() + 1) / kCanvasWidth,
		bounds.top + point.y * (bounds.Height() + 1) / kCanvasHeight);
}


static void
draw_waves(BView* view, BRect bounds)
{
	BGradientLinear sky(bounds.LeftTop(), bounds.LeftBottom());
	for (size_t i = 0; i < sizeof(kSky) / sizeof(kSky[0]); i++)
		sky.AddColor(kSky[i].color, kSky[i].offset * 255);
	view->FillRect(bounds, sky);

	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	for (size_t i = 0; i < sizeof(kWaves) / sizeof(kWaves[0]); i++) {
		const wave& w = kWaves[i];
		BShape shape;
		shape.MoveTo(to_screen(w.start, bounds));
		for (int c = 0; c < 2; c++) {
			BPoint points[3];
			for (int p = 0; p < 3; p++)
				points[p] = to_screen(w.curves[c][p], bounds);
			shape.BezierTo(points);
		}
		shape.LineTo(BPoint(bounds.right + 1, bounds.bottom + 1));
		shape.LineTo(BPoint(bounds.left, bounds.bottom + 1));
		shape.Close();

		rgb_color from = w.from;
		rgb_color to = w.to;
		from.alpha = to.alpha = w.alpha;
		BGradientLinear fill(bounds.LeftTop(), bounds.RightBottom());
		fill.AddColor(from, 0);
		fill.AddColor(to, 255);
		view->FillShape(&shape, fill);
	}
	view->SetDrawingMode(B_OP_COPY);
}


// #pragma mark - SignInBackground


SignInBackground::SignInBackground(BRect frame)
	:
	BView(frame, "desktop", B_FOLLOW_ALL, B_WILL_DRAW | B_FRAME_EVENTS),
	fPicture(NULL),
	fRendered(NULL)
{
	SetViewColor(B_TRANSPARENT_COLOR);
	if (kBackgroundPicture != NULL)
		fPicture = BTranslationUtils::GetBitmap(kBackgroundPicture);
	_Render();
}


SignInBackground::~SignInBackground()
{
	delete fPicture;
	delete fRendered;
}


void
SignInBackground::Draw(BRect updateRect)
{
	if (fRendered != NULL)
		DrawBitmap(fRendered, updateRect, updateRect);
	else {
		SetHighColor(kSky[0].color);
		FillRect(updateRect);
	}
}


void
SignInBackground::FrameResized(float width, float height)
{
	BView::FrameResized(width, height);
	_Render();
	Invalidate();
}


// The background is drawn once into a bitmap the size of the screen, so
// moving the Sign in window only copies pixels.
void
SignInBackground::_Render()
{
	delete fRendered;
	fRendered = NULL;

	BRect bounds = Bounds();
	BBitmap* bitmap = new(std::nothrow) BBitmap(bounds.OffsetToCopy(0, 0),
		B_BITMAP_ACCEPTS_VIEWS, B_RGB32);
	if (bitmap == NULL || bitmap->InitCheck() != B_OK) {
		delete bitmap;
		return;
	}

	BView* view = new BView(bitmap->Bounds(), "render", B_FOLLOW_NONE, 0);
	bitmap->AddChild(view);
	if (!bitmap->Lock()) {
		delete bitmap;
		return;
	}

	BRect target = view->Bounds();
	if (fPicture != NULL && fPicture->IsValid()) {
		// Cover the screen: scale until both sides fill it, then center.
		BRect source = fPicture->Bounds();
		float scale = fmaxf((target.Width() + 1) / (source.Width() + 1),
			(target.Height() + 1) / (source.Height() + 1));
		float cropWidth = (target.Width() + 1) / scale;
		float cropHeight = (target.Height() + 1) / scale;
		BRect crop(0, 0, cropWidth - 1, cropHeight - 1);
		crop.OffsetTo(floorf((source.Width() + 1 - cropWidth) / 2),
			floorf((source.Height() + 1 - cropHeight) / 2));
		view->DrawBitmap(fPicture, crop, target, B_FILTER_BITMAP_BILINEAR);
	} else
		draw_waves(view, target);

	view->Sync();
	bitmap->Unlock();
	fRendered = bitmap;
}
