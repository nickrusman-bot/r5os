/*
 * Copyright 2026, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */
#ifndef WIFI_WINDOW_H
#define WIFI_WINDOW_H


#include <Window.h>


class WiFiView;


//!	The WiFi program's window around the shared WiFiView (libr5wifi.so).
class WiFiWindow : public BWindow {
public:
								WiFiWindow();

	virtual	void				MessageReceived(BMessage* message);

private:
			WiFiView*			fView;
};


#endif	// WIFI_WINDOW_H
