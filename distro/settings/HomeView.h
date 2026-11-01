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
	const char* iconSignature;
	uint32		command;
	const char* field;
};


class HomeView : public BGroupView {
public:
								HomeView(const SettingsHomeItem* items,
									int32 count, const char* intro);
	virtual						~HomeView();

private:
			CategoryCard*		fCards[16];
			int32				fCount;
};


#endif
