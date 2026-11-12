// ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~
//
//	Copyright (c) 2003, Haiku
//
//  This software is part of the Haiku distribution and is covered
//  by the MIT License.
//
//
//  File:        MediaViews.h
//  Author:      Sikosis, Jérôme Duval
//  Description: Media Preferences
//  Created :    June 25, 2003
// 
// ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~
#ifndef __MEDIAVIEWS_H__
#define __MEDIAVIEWS_H__
#include <GroupView.h>
#include <MediaAddOn.h>
#include <MenuItem.h>

#include <ObjectList.h>


class BBox;
class BButton;
class BCheckBox;
class BMenu;
class BMenuField;
class BMessageRunner;
class BString;
class BStringView;

class MediaWindow;


const uint32 ML_RESTART_MEDIA_SERVER = 'resr';
const uint32 ML_DEFAULT_CHANNEL_CHANGED = 'chch';
const uint32 ML_UPDATE_DEFAULTS = 'updf';
const uint32 ML_USB_CHANNEL_LAYOUT = 'ulyt';
const uint32 ML_VIRTUAL_SURROUND = 'vsur';


class NodeMenuItem : public BMenuItem
{
public:
								NodeMenuItem(const dormant_node_info* info,
									BMessage* message, char shortcut = 0,
									uint32 modifiers = 0);
	virtual	status_t			Invoke(BMessage* message = NULL);

			const dormant_node_info* NodeInfo() const {return fInfo;}
private:

			const dormant_node_info* fInfo;
};


class ChannelMenuItem : public BMenuItem
{
public:
								ChannelMenuItem(media_input* input,
									BMessage* message, char shortcut = 0,
									uint32 modifiers = 0);
	virtual						~ChannelMenuItem();

			int32				DestinationID();
			media_input*		Input();

	virtual	status_t			Invoke(BMessage* message = NULL);

private:
			media_input*		fInput;
};


class SettingsView : public BGroupView
{
public:
	typedef BObjectList<dormant_node_info, true> NodeList;

								SettingsView();
			void				AddInputNodes(NodeList& nodes);
			void				AddOutputNodes(NodeList& nodes);

	// Show info as the default. This does not change the server's default.
	virtual	void				SetDefaultInput(const dormant_node_info* info);
	virtual	void				SetDefaultOutput(const dormant_node_info* info);

	virtual	void				MessageReceived(BMessage* message);
	virtual	void				AttachedToWindow();


protected:

			BMenu*				InputMenu() {return fInputMenu;}
			BMenu*				OutputMenu() {return fOutputMenu;}

			BButton*			MakeRestartButton();

	// Make info the server's default, when the user chose it in the menu.
	virtual	status_t			_ChangeDefaultInput(
									const dormant_node_info* info);
	virtual	status_t			_ChangeDefaultOutput(
									const dormant_node_info* info);

			// release is true when the caller must ReleaseNode() the node.
			status_t			_GetNode(const dormant_node_info* info,
									media_node* node, bool* release);
			void				_EmptyMenu(BMenu* menu);
			MediaWindow*		_MediaWindow() const;

private:
			void				_ChangeFailed(const dormant_node_info* info,
									status_t status);
			void				_PopulateMenu(BMenu* menu, NodeList& nodes,
									const BMessage& message);
			NodeMenuItem*		_FindNodeItem(BMenu* menu,
									const dormant_node_info* nodeInfo);
			void				_ClearMenuSelection(BMenu* menu);

			BMenu* 				fInputMenu;
			BMenu* 				fOutputMenu;
};	


class AudioSettingsView : public SettingsView
{
public:
								AudioSettingsView();

			void				SetDefaultChannel(int32 channelID);

	virtual	void				SetDefaultInput(const dormant_node_info* info);
	virtual	void				SetDefaultOutput(const dormant_node_info* info);

	virtual	void				MessageReceived(BMessage* message);
	virtual	void				AttachedToWindow();
	virtual	void				DetachedFromWindow();

protected:
	virtual	status_t			_ChangeDefaultInput(
									const dormant_node_info* info);
	virtual	status_t			_ChangeDefaultOutput(
									const dormant_node_info* info);

private:
			// The administrator lock for the three driver settings menus.
			void				_RefreshLock();
			void				_UpdateLock();
			void				_SaveFailed(const char* what,
									const BString& error, status_t status);

			BMenuField*			_MakeChannelMenu();
			BMenuField*			_MakeLayoutMenu();
			BMenuField*			_MakeLatencyMenu();
			BMenuField*			_MakeSurroundMenu();
			BCheckBox*			_MakeVolumeCheckBox();
			void				_FillChannelMenu(const dormant_node_info* info);

			void				_ShowDeskbarVolumeControl();
			void				_HideDeskbarVolumeControl();

			ChannelMenuItem*	_ChannelMenuItemAt(int32 index);

			BMenu*				fChannelMenu;
			BMenu*				fLayoutMenu;
			BMenu*				fLatencyMenu;
			BMenu*				fSurroundMenu;
			BStringView*		fLatencyHint;
			BCheckBox* 			fVolumeCheckBox;
			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: GPL-3.0-or-later
			BMenuField*			fLayoutField;
			BMenuField*			fLatencyField;
			BMenuField*			fSurroundField;
			BButton*			fLockButton;
			BMessageRunner*		fLockRunner;
			// True for an account that cannot write the driver settings.
			bool				fNeedsLock;
			bool				fUnlocked;
			bool				fHasUsbAudio;
			// SPDX-SnippetEnd
};


class VideoSettingsView : public SettingsView
{
public:
								VideoSettingsView();

	virtual	void				SetDefaultInput(const dormant_node_info* info);
	virtual	void				SetDefaultOutput(const dormant_node_info* info);

protected:
	virtual	status_t			_ChangeDefaultInput(
									const dormant_node_info* info);
	virtual	status_t			_ChangeDefaultOutput(
									const dormant_node_info* info);
};

#endif
