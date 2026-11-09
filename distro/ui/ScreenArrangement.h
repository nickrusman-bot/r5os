/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef _SCREEN_ARRANGEMENT_H
#define _SCREEN_ARRANGEMENT_H


#include <Invoker.h>
#include <Rect.h>
#include <View.h>


namespace BAppearanceKit {


struct screen_tile {
	int32			id;			// the caller's id for this screen
	BRect			frame;		// in desktop coordinates
	const char*		name;		// copied by SetScreens()
	bool			main;		// the Deskbar is on this screen
	bool			enabled;	// false draws it dimmed
};


class BScreenArrangement : public BView, public BInvoker {
public:
								BScreenArrangement(const char* name,
									BMessage* selectionMessage);
								BScreenArrangement(BMessage* archive);
	virtual						~BScreenArrangement();

	static	BArchivable*		Instantiate(BMessage* archive);
	virtual	status_t			Archive(BMessage* archive,
									bool deep = true) const;

	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseUp(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);
	virtual	void				KeyDown(const char* bytes, int32 numBytes);
	virtual	void				MakeFocus(bool focus = true);
	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				GetPreferredSize(float* width, float* height);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();

			status_t			SetScreens(const screen_tile* tiles,
									int32 count);
			int32				CountScreens() const;
			bool				ScreenAt(int32 index,
									screen_tile* tile) const;
									// tile->name stays valid until the
									// next SetScreens()
			void				SetSelected(int32 id);
			int32				Selected() const;
			void				SetDraggable(bool draggable);
			bool				IsDraggable() const;
			void				SetModificationMessage(BMessage* message);
									// adopted
			BMessage*			ModificationMessage() const;
			void				SetCaption(const char* caption);
			const char*			Caption() const;
									// NULL when there is none

private:
			struct Data;
			void				_Init();
			void				_Place();
			int32				_IndexAt(BPoint where) const;
			int32				_Neighbor(int32 index, int32 dx, int32 dy) const;
			void				_SelectIndex(int32 index, bool invoke);
			void				_Snap(int32 index, BRect proposed);
			float				_Unit() const;

	virtual	void				_ReservedScreenArrangement1();
	virtual	void				_ReservedScreenArrangement2();
	virtual	void				_ReservedScreenArrangement3();
	virtual	void				_ReservedScreenArrangement4();

								BScreenArrangement(const BScreenArrangement&);
			BScreenArrangement&	operator=(const BScreenArrangement&);

			Data*				fData;
			uint32				_reserved[4];
};


}	// namespace BAppearanceKit


#endif	// _SCREEN_ARRANGEMENT_H
