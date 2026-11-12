/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * Device Manager. Distributed under the terms of the MIT License.
 */
#ifndef DM_PROPERTIES_WINDOW_H
#define DM_PROPERTIES_WINDOW_H


#include <Message.h>
#include <Messenger.h>
#include <Window.h>


class BBitmap;
class BButton;
class BListView;
class BMenuField;
class BPopUpMenu;
class BStringView;
class BTextView;

namespace BAppearanceKit {
class BIconView;
}


// One device, in tabs: General, Driver, Details, Events. The main window
// sends a fresh description after every scan (kMsgDeviceUpdate) or
// kMsgDeviceGone when the device was unplugged.
class PropertiesWindow : public BWindow {
public:
								PropertiesWindow(BMessenger mainWindow,
									const BMessage& device);
	virtual						~PropertiesWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			BView*				_Header(BAppearanceKit::BIconView*& icon,
									BStringView*& name);
			BView*				_GeneralTab();
			BView*				_DriverTab();
			BView*				_DetailsTab();
			BView*				_EventsTab();
			void				_SetDevice(const BMessage& device);
			void				_SetGone();
			void				_FillDriver();
			void				_FillProperties();
			void				_ShowProperty(int32 index);
			void				_FillEvents();
			void				_CopySelection();

			BMessenger			fMainWindow;
			BMessage			fDevice;
			uint32				fCookie;
			bool				fGone;
			BBitmap*			fIcon;

			BAppearanceKit::BIconView* fGeneralIcon;
			BStringView*		fGeneralName;
			BStringView*		fType;
			BStringView*		fManufacturer;
			BStringView*		fLocation;
			BTextView*			fStatus;

			BAppearanceKit::BIconView* fDriverIcon;
			BStringView*		fDriverDeviceName;
			BStringView*		fDriverName;
			BStringView*		fDriverFile;
			BStringView*		fDriverSize;
			BStringView*		fDriverDate;
			BStringView*		fDriverPackage;
			BStringView*		fDriverVersion;
			BStringView*		fDriverLicense;
			BStringView*		fDriverNote;
			BButton*			fDetailsButton;
			BButton*			fToggleButton;

			BPopUpMenu*			fPropertyMenu;
			BMenuField*			fPropertyField;
			BListView*			fValues;
			int32				fShownProperty;

			BTextView*			fEvents;
};


#endif
