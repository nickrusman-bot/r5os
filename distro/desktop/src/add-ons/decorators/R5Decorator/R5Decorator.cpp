/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


/*!	The R5 window frame. It starts from Haiku's DefaultDecorator and changes
	only the frame: a single line of one pixel instead of the wide bevelled
	border, no resize knob, and an input margin outside the line where the
	window is resized by its edge. The tab, the buttons and the colours are
	Haiku's. See docs/APP-SERVER-COMPOSITOR.md, "The resize grab area".
*/


#include "R5Decorator.h"

#include <new>
#include <syslog.h>

#include <WindowPrivate.h>

#include "DrawingEngine.h"


static const int32 kFrameWidth = 1;


static bool
has_thin_frame(window_look look)
{
	switch ((int)look) {
		case B_TITLED_WINDOW_LOOK:
		case B_DOCUMENT_WINDOW_LOOK:
		case B_FLOATING_WINDOW_LOOK:
		case B_MODAL_WINDOW_LOOK:
		case kLeftTitledWindowLook:
			return true;
		default:
			return false;
	}
}


static bool
has_tab(window_look look)
{
	return look == B_TITLED_WINDOW_LOOK || look == B_DOCUMENT_WINDOW_LOOK
		|| look == B_FLOATING_WINDOW_LOOK || look == kLeftTitledWindowLook;
}


//	#pragma mark - R5DecorAddOn


R5DecorAddOn::R5DecorAddOn(image_id id, const char* name)
	:
	DecorAddOn(id, name)
{
}


Decorator*
R5DecorAddOn::_AllocateDecorator(DesktopSettings& settings, BRect rect,
	Desktop* desktop)
{
	return new (std::nothrow) R5Decorator(settings, rect, desktop);
}


//	#pragma mark - R5Decorator


R5Decorator::R5Decorator(DesktopSettings& settings, BRect frame,
	Desktop* desktop)
	:
	SATDecorator(settings, frame, desktop),
	fThinFrame(false),
	fHaikuBorderWidth(0)
{
}


R5Decorator::~R5Decorator()
{
}


Decorator::Region
R5Decorator::RegionAt(BPoint where, int32& tab) const
{
	Region region = SATDecorator::RegionAt(where, tab);
	if (!fThinFrame || (fTopTab->flags & B_NOT_RESIZABLE) != 0)
		return region;

	switch (region) {
		case REGION_NONE:
		case REGION_LEFT_BORDER:
		case REGION_RIGHT_BORDER:
		case REGION_TOP_BORDER:
		case REGION_BOTTOM_BORDER:
		case REGION_RIGHT_BOTTOM_CORNER:
			break;
		default:
			// the tab and its buttons
			return region;
	}

	// The grab area is the line plus the margin outside it.
	BRect margin;
	const_cast<R5Decorator*>(this)->GetInputMargin(&margin);
	BRect outer(fBorderRect.left - margin.left, fBorderRect.top - margin.top,
		fBorderRect.right + margin.right, fBorderRect.bottom + margin.bottom);
	if (!outer.Contains(where) || fFrame.Contains(where))
		return region;

	const float length = fBorderResizeLength;
	bool nearLeft = where.x < outer.left + length;
	bool nearRight = where.x > outer.right - length;
	bool nearTop = where.y < outer.top + length;
	bool nearBottom = where.y > outer.bottom - length;

	if (where.x < fFrame.left) {
		if (nearTop)
			return REGION_LEFT_TOP_CORNER;
		if (nearBottom)
			return REGION_LEFT_BOTTOM_CORNER;
		return REGION_LEFT_BORDER;
	}
	if (where.x > fFrame.right) {
		if (nearTop)
			return REGION_RIGHT_TOP_CORNER;
		if (nearBottom)
			return REGION_RIGHT_BOTTOM_CORNER;
		return REGION_RIGHT_BORDER;
	}
	if (where.y < fFrame.top) {
		if (nearLeft)
			return REGION_LEFT_TOP_CORNER;
		if (nearRight)
			return REGION_RIGHT_TOP_CORNER;
		return REGION_TOP_BORDER;
	}
	if (nearLeft)
		return REGION_LEFT_BOTTOM_CORNER;
	if (nearRight)
		return REGION_RIGHT_BOTTOM_CORNER;
	return REGION_BOTTOM_BORDER;
}


void
R5Decorator::GetShadowExtent(BRect* extent)
{
	if (!fThinFrame) {
		SATDecorator::GetShadowExtent(extent);
		return;
	}

	// The same small shadow as a Haiku frame, from Haiku's border width.
	int32 width = fHaikuBorderWidth;
	extent->Set(width, (width + 1) / 2, width, width * 2);
}


void
R5Decorator::GetGlassRegion(BRegion* region)
{
	region->MakeEmpty();
	if (!fThinFrame)
		return;

	region->Include(fTopBorder);
	region->Include(fLeftBorder);
	region->Include(fRightBorder);
	region->Include(fBottomBorder);
	region->Include(&fTabsRegion);
}


void
R5Decorator::GetInputMargin(BRect* margin)
{
	margin->Set(0, 0, 0, 0);
	if (!fThinFrame || fTopTab == NULL
		|| (fTopTab->flags & B_NOT_RESIZABLE) != 0) {
		return;
	}

	// As wide as Haiku's border; the line is the inner pixel of it.
	float width = fHaikuBorderWidth - kFrameWidth;
	if ((fTopTab->flags & B_NOT_H_RESIZABLE) == 0) {
		margin->left = width;
		margin->right = width;
	}
	if ((fTopTab->flags & B_NOT_V_RESIZABLE) == 0) {
		margin->top = width;
		margin->bottom = width;
	}
}


void
R5Decorator::GetComponentColors(Component component, uint8 highlight,
	ComponentColors _colors, Decorator::Tab* tab)
{
	SATDecorator::GetComponentColors(component, highlight, _colors, tab);

	if (component != COMPONENT_TAB)
		return;
	if (highlight == HIGHLIGHT_STACK_AND_TILE
		&& (tab == NULL || tab->isHighlighted)) {
		// the stack and tile colours stay
		return;
	}

	// Haiku's yellow, without the R5 tint per window of DefaultDecorator.
	if (tab != NULL && tab->buttonFocus) {
		_colors[COLOR_TAB] = fFocusTabColor;
		_colors[COLOR_TAB_LIGHT] = fFocusTabColorLight;
		_colors[COLOR_TAB_BEVEL] = fFocusTabColorBevel;
		_colors[COLOR_TAB_SHADOW] = fFocusTabColorShadow;
	} else {
		_colors[COLOR_TAB] = fNonFocusTabColor;
		_colors[COLOR_TAB_LIGHT] = fNonFocusTabColorLight;
		_colors[COLOR_TAB_BEVEL] = fNonFocusTabColorBevel;
		_colors[COLOR_TAB_SHADOW] = fNonFocusTabColorShadow;
	}
}


void
R5Decorator::_DoLayout()
{
	// First Haiku's layout, with Haiku's border width.
	fThinFrame = false;
	SATDecorator::_DoLayout();

	if (!has_thin_frame(fTopTab->look) || fBorderWidth <= kFrameWidth)
		return;

	fHaikuBorderWidth = fBorderWidth;
	fBorderWidth = kFrameWidth;
	fThinFrame = true;

	fLeftBorder.Set(fFrame.left - fBorderWidth, fFrame.top,
		fFrame.left - 1, fFrame.bottom);
	fRightBorder.Set(fFrame.right + 1, fFrame.top,
		fFrame.right + fBorderWidth, fFrame.bottom);
	fTopBorder.Set(fFrame.left - fBorderWidth, fFrame.top - fBorderWidth,
		fFrame.right + fBorderWidth, fFrame.top - 1);
	fBottomBorder.Set(fFrame.left - fBorderWidth, fFrame.bottom + 1,
		fFrame.right + fBorderWidth, fFrame.bottom + fBorderWidth);
	fBorderRect = BRect(fTopBorder.LeftTop(), fBottomBorder.RightBottom());

	// no resize knob
	fResizeRect.Set(0, 0, -1, -1);

	if (has_tab(fTopTab->look))
		_DoTabLayout();
}


/*!	The tab keeps Haiku's size: it is laid out with Haiku's border width,
	against a frame that is that much smaller, so it ends on the line.
*/
void
R5Decorator::_DoTabLayout()
{
	if (!fThinFrame) {
		SATDecorator::_DoTabLayout();
		return;
	}

	int32 borderWidth = fBorderWidth;
	BRect frame = fFrame;
	float inset = fHaikuBorderWidth - kFrameWidth;

	fBorderWidth = fHaikuBorderWidth;
	fFrame.InsetBy(inset, inset);
	SATDecorator::_DoTabLayout();
	fFrame = frame;
	fBorderWidth = borderWidth;
}


void
R5Decorator::_DrawFrame(BRect rect)
{
	if (!fThinFrame) {
		SATDecorator::_DrawFrame(rect);
		return;
	}

	if (!rect.Intersects(fBorderRect))
		return;

	// The line Haiku draws for a bordered window.
	ComponentColors colors;
	GetComponentColors(COMPONENT_LEFT_BORDER,
		RegionHighlight(REGION_LEFT_BORDER), colors, fTopTab);
	fDrawingEngine->StrokeRect(fBorderRect, colors[5]);
}


void
R5Decorator::_GetFootprint(BRegion* region)
{
	if (!fThinFrame) {
		SATDecorator::_GetFootprint(region);
		return;
	}

	// The line and the tab. The input margin is not part of it.
	region->Include(fTopBorder);
	region->Include(fLeftBorder);
	region->Include(fRightBorder);
	region->Include(fBottomBorder);
	region->Include(&fTabsRegion);
}


// These lay out the tab items again. They use Haiku's border width for the
// text offset, so the title stays where Haiku puts it.


void
R5Decorator::_ResizeBy(BPoint offset, BRegion* dirty)
{
	int32 borderWidth = fBorderWidth;
	if (fThinFrame)
		fBorderWidth = fHaikuBorderWidth;
	SATDecorator::_ResizeBy(offset, dirty);
	fBorderWidth = borderWidth;
}


void
R5Decorator::_SetFocus(Decorator::Tab* tab)
{
	int32 borderWidth = fBorderWidth;
	if (fThinFrame)
		fBorderWidth = fHaikuBorderWidth;
	SATDecorator::_SetFocus(tab);
	fBorderWidth = borderWidth;
}


bool
R5Decorator::_SetTabLocation(Decorator::Tab* tab, float location,
	bool isShifting, BRegion* updateRegion)
{
	int32 borderWidth = fBorderWidth;
	if (fThinFrame)
		fBorderWidth = fHaikuBorderWidth;
	bool result = SATDecorator::_SetTabLocation(tab, location, isShifting,
		updateRegion);
	fBorderWidth = borderWidth;
	return result;
}


bool
R5Decorator::_MoveTab(int32 from, int32 to, bool isMoving,
	BRegion* updateRegion)
{
	int32 borderWidth = fBorderWidth;
	if (fThinFrame)
		fBorderWidth = fHaikuBorderWidth;
	bool result = SATDecorator::_MoveTab(from, to, isMoving, updateRegion);
	fBorderWidth = borderWidth;
	return result;
}


//	#pragma mark - add-on entry


R5_DECORATOR_ABI_EXPORT


extern "C" DecorAddOn*
(instantiate_decor_addon)(image_id id, const char* name)
{
	syslog(LOG_INFO, "decorator: r5 loaded\n");
	return new (std::nothrow) R5DecorAddOn(id, name);
}
