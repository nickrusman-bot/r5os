/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef R5_SETTINGS_HOME_VIEW_H
#define R5_SETTINGS_HOME_VIEW_H


#include <GroupView.h>


class CategoryCard;


struct SettingsHomeItem {
	const char* id;
	const char* name;
	const char* summary;
};


class HomeView : public BGroupView {
public:
								HomeView(const SettingsHomeItem* items,
									int32 count);
	virtual						~HomeView();

private:
			CategoryCard*		fCards[16];
			int32				fCount;
};


#endif
