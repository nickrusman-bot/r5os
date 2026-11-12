/*
 * Copyright 2026, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */
#ifndef R5_WIFI_VIEW_H
#define R5_WIFI_VIEW_H


#include <String.h>
#include <GroupView.h>

#include <R5WiFi.h>


class BBox;
class BButton;
class BCheckBox;
class BMenuField;
class BPopUpMenu;
class BStringView;
class BTextControl;
class NetworkListView;
class SignalView;


/*!	The whole Wi-Fi panel: power, the current network, known and other
	networks, the password prompt. The WiFi program puts it in a window, and
	the Wi-Fi page of Settings shows it as its page (libr5wifi.so).
*/
class WiFiView : public BGroupView {
public:
								WiFiView(bool settingsPage = false);
	virtual						~WiFiView();

	virtual	void				AttachedToWindow();
	virtual	void				DetachedFromWindow();
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_StopController();
			void				_SetState(const WiFiState& state);
			void				_UpdateCurrent();
			void				_UpdateDevices();
			void				_UpdateButtons();
			void				_JoinResult(BMessage* message);

			const WiFiNetworkInfo* _SelectedNetwork() const;
			void				_JoinSelected();
			void				_ShowPrompt(const char* name,
									uint32 authentication);
			void				_HidePrompt();
			void				_JoinFromPrompt();
			void				_SetMessage(const char* text, bool error);
			void				_Forget();
			void				_ShowJoinOther();

private:
			bool				fSettingsPage;
			WiFiController*		fController;
			BMessenger			fControllerMessenger;
			WiFiState			fState;
			bool				fHasState;
			bool				fShowingNetServerError;

			BCheckBox*			fPowerCheck;
			BMenuField*			fDeviceField;
			BPopUpMenu*			fDeviceMenu;

			SignalView*			fCurrentSignal;
			BStringView*		fCurrentName;
			BStringView*		fCurrentStatus;
			BButton*			fDisconnectButton;

			BStringView*		fKnownLabel;
			NetworkListView*	fKnownList;
			BButton*			fForgetButton;
			BStringView*		fOtherLabel;
			BStringView*		fScanLabel;
			NetworkListView*	fOtherList;

			BBox*				fPromptBox;
			BStringView*		fPromptLabel;
			BTextControl*		fPasswordControl;
			BCheckBox*			fShowPassword;
			BCheckBox*			fRememberCheck;
			BStringView*		fPromptError;
			BButton*			fPromptJoinButton;
			BString				fPromptName;
			uint32				fPromptAuthentication;

			BStringView*		fMessageView;
			BButton*			fJoinButton;

			BString				fJoiningName;
			BString				fRequestedName;
			uint32				fRequestedAuthentication;
			bigtime_t			fRequestedTime;

			// room for later private state (libr5wifi.so)
			uint32				_reserved[8];
};


#endif	// R5_WIFI_VIEW_H
