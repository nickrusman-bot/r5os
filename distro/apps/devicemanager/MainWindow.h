/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * Device Manager. Distributed under the terms of the MIT License.
 */
#ifndef DM_MAIN_WINDOW_H
#define DM_MAIN_WINDOW_H


#include <Messenger.h>
#include <String.h>
#include <Window.h>

#include <map>

#include "DeviceModel.h"


class BBitmap;
class BButton;
class BFilePanel;
class BMenuItem;
class BMessageRunner;
class BOutlineListView;
class DeviceItem;


class MainWindow : public BWindow {
public:
								MainWindow();
	virtual						~MainWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

	// The data a Properties window shows, as a message.
	static	void				DescribeDevice(const DeviceRecord* record,
									BMessage& out);

private:
			void				_Reload();
			void				_Rebuild();
			void				_AddConnection(DeviceRecord* record,
									DeviceItem* under);
			bool				_IsVisible(const DeviceRecord* record) const;
			DeviceItem*			_NewItem(const DeviceRecord* record);
			DeviceRecord*		_Selected() const;
			void				_UpdateActions();
			void				_OpenProperties(uint32 cookie);
			void				_UpdatePropertiesWindows();
			void				_RequestToggle(const BString& driverPath,
									bool enable);
			void				_ApplyToggle(const BString& driverPath,
									bool enable);
			void				_ShowTurnedOff();
			void				_Export(int32 format);
			void				_SaveReport(BMessage* message);
			const BBitmap*		_Icon(int32 group);
			void				_LoadSettings(BRect& frame);
			void				_SaveSettings();

			DeviceModel			fModel;
			BOutlineListView*	fTree;
			BMenuItem*			fByTypeItem;
			BMenuItem*			fByConnectionItem;
			BMenuItem*			fShowHiddenItem;
			BMenuItem*			fPropertiesItem;
			BMenuItem*			fToggleItem;
			BButton*			fPropertiesButton;
			BButton*			fScanButton;
			BButton*			fToggleButton;
			bool				fByConnection;
			bool				fShowHidden;

			std::map<int32, BBitmap*> fIcons;
			std::map<uint32, BMessenger> fPropertiesWindows;
			std::map<BString, bool> fExpanded;
			BMessenger			fTurnedOffWindow;
			BFilePanel*			fSavePanel;
			BMessageRunner*		fReloadRunner;

			bool				fHasPending;
			BString				fPendingPath;
			bool				fPendingEnable;
};


#endif
