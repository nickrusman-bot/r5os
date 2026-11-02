/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef R5_SETTINGS_WINDOW_H
#define R5_SETTINGS_WINDOW_H


#include <List.h>
#include <String.h>
#include <Window.h>


class BButton;
class BCardLayout;
class BListView;
class BStringView;
class BTextControl;
class BView;
class HomeView;
class SettingsContext;
class SettingsManager;
class SettingsModule;
class SettingsStorage;


struct SettingsPlace {
	BString id;
	bool category;
};


class SettingsWindow : public BWindow {
public:
								SettingsWindow(SettingsManager* manager,
									SettingsStorage* storage);
	virtual						~SettingsWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

			SettingsContext*	Context() const { return fContext; }
			void				ModulesLoaded();

private:
			void				_BuildSidebar();
			void				_ShowHome();
			void				_ShowCategory(const char* categoryId);
			void				_ShowModule(const char* moduleId);
			bool				_ConfirmLeave();
			void				_Remember(const char* id, bool category);
			void				_SetHeader(const char* title,
									const char* description);
			void				_UpdateActions();
			BView*				_ModuleView(SettingsModule* module);
			BView*				_CategoryView(const char* categoryId,
									const char* name);
			bool				_Matches(SettingsModule* module,
									const char* query) const;
			const char*			_CategoryName(const char* categoryId) const;
			const char*			_CategorySummary(const char* categoryId) const;
			void				_SelectSidebar(const char* moduleId);

			SettingsManager*	fManager;
			SettingsStorage*	fStorage;
			SettingsContext*	fContext;
			BTextControl*		fSearch;
			BListView*			fList;
			BStringView*		fTitle;
			BStringView*		fDescription;
			BCardLayout*		fCards;
			BView*				fDeck;
			BButton*			fHome;
			BButton*			fBack;
			BButton*			fRevert;
			BButton*			fSave;
			SettingsModule*		fCurrent;
			BList				fHistory;
			bool				fAdjusting;
			BString				fQuery;
};


#endif
