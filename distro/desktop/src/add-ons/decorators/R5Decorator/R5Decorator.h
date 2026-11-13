/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef R5_DECORATOR_H
#define R5_DECORATOR_H


#include "DecorManager.h"
#include "SATDecorator.h"


class R5DecorAddOn : public DecorAddOn {
public:
								R5DecorAddOn(image_id id, const char* name);

protected:
	virtual	Decorator*			_AllocateDecorator(DesktopSettings& settings,
									BRect rect, Desktop* desktop);
};


/*!	The R5 window frame: Haiku's tab and buttons, a frame of one pixel, no
	resize knob, and an input margin outside the line that is as wide as
	Haiku's border.
*/
class R5Decorator : public SATDecorator {
public:
								R5Decorator(DesktopSettings& settings,
									BRect frame, Desktop* desktop);
	virtual						~R5Decorator();

	virtual	Region				RegionAt(BPoint where, int32& tab) const;

	virtual	void				GetShadowExtent(BRect* extent);
	virtual	void				GetGlassRegion(BRegion* region);
	virtual	void				GetInputMargin(BRect* margin);

protected:
	virtual	void				GetComponentColors(Component component,
									uint8 highlight, ComponentColors _colors,
									Decorator::Tab* tab = NULL);

	virtual	void				_DoLayout();
	virtual	void				_DoTabLayout();
	virtual	void				_DrawFrame(BRect rect);
	virtual	void				_GetFootprint(BRegion* region);

	virtual	void				_ResizeBy(BPoint offset, BRegion* dirty);
	virtual	void				_SetFocus(Decorator::Tab* tab);
	virtual	bool				_SetTabLocation(Decorator::Tab* tab,
									float location, bool isShifting,
									BRegion* updateRegion = NULL);
	virtual	bool				_MoveTab(int32 from, int32 to, bool isMoving,
									BRegion* updateRegion = NULL);

private:
			bool				fThinFrame;
			int32				fHaikuBorderWidth;
};


#endif	// R5_DECORATOR_H
