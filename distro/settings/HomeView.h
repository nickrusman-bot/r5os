/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef R5_SETTINGS_HOME_VIEW_H
#define R5_SETTINGS_HOME_VIEW_H


#include <String.h>
#include <View.h>


class BGridLayout;
class BView;


class HomeView : public BView {
public:
								HomeView(bool recent);
	virtual						~HomeView();

			void				AddCard(BView* card);
			void				SetRecent(const BString* names,
									const BString* ids, int32 count);

private:
			BGridLayout*		fGrid;
			BView*				fRecent;
			int32				fCount;
			bool				fWantsRecent;
};


#endif
