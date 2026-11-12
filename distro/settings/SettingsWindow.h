/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef R5_SETTINGS_WINDOW_H
#define R5_SETTINGS_WINDOW_H


#include <List.h>
#include <String.h>
#include <Window.h>

#include <Sidebar.h>


class BBitmap;
class BButton;
class BCardLayout;
class BMessageRunner;
class BTextControl;
class BView;
class HomeView;
namespace BAppearanceKit { class BPageHeader; }
class SettingsContext;
class SettingsManager;
class SettingsModule;
class SettingsStorage;


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
			void				_ApplyCurrent();
			void				_SetHeader(const char* eyebrow,
									const char* title,
									const char* description,
									const BBitmap* icon);
			void				_UpdateTip();
			void				_NoteRecent(const char* moduleId);
			void				_ShowRecent();
			void				_UpdateActions();
			void				_ReloadLock(bool pageChanged);
			void				_ApplyPageLock();
			void				_RestoreLockedControls();
			void				_DisableLocked(BView* view);
			BView*				_PageContent() const;
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
			BAppearanceKit::BSidebar* fList;
			BAppearanceKit::BPageHeader* fHeader;
			HomeView*			fHomeView;
			BView*				fTip;
			BCardLayout*		fCards;
			BView*				fDeck;
			BButton*			fRevert;
			BButton*			fDefaults;
			SettingsModule*		fCurrent;
			bool				fAdjusting;
			BString				fQuery;
			status_t			fLastApplyError;
			BButton*			fLock;
			BMessageRunner*		fLockRunner;
			BList				fLockedControls;
			bool				fLockAdmin;
			bool				fLockOpen;
			float				fLockButtonHeight;
};


#endif
