/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Text clipboard between Haiku and the VirtualBox host over COM1.
 * Not Guest Additions: no vboxsf, no auto-resize, no VBox drag-and-drop.
 */

#include <Application.h>
#include <Clipboard.h>
#include <Locker.h>
#include <Message.h>
#include <Mime.h>
#include <OS.h>
#include <SerialPort.h>
#include <String.h>

#include <string.h>


static const uint32 kMsgFrame = 'r5cf';
static const uint32 kMaxBytes = 256 * 1024;
static const char kMagic[4] = { 'R', '5', 'C', '1' };
static const char* kPortName = "pc_serial1";


class ClipApp : public BApplication {
public:
					ClipApp();
	virtual			~ClipApp();

	virtual void	ReadyToRun();
	virtual void	MessageReceived(BMessage* message);
	virtual bool	QuitRequested();

	void			ReadLoop();

private:
	static int32	_Reader(void* cookie);
	void			_StopReader();
	bool			_OpenPort();
	void			_WriteFrame(const void* data, uint32 length);
	BString			_ClipboardText();
	void			_SetClipboard(const char* text, int32 length);
	void			_SendIfChanged();

	BSerialPort		fPort;
	BLocker			fLock;
	thread_id		fReader;
	BString			fLastSent;
	BString			fLastRecv;
	bool			fOpen;
	volatile bool	fStopping;
};


ClipApp::ClipApp()
	:
	BApplication("application/x-vnd.R5OS-vbox-clip"),
	fReader(-1),
	fOpen(false),
	fStopping(false)
{
}


ClipApp::~ClipApp()
{
	_StopReader();
}


void
ClipApp::ReadyToRun()
{
	int32 tries = 0;
	while (!fStopping && !_OpenPort() && tries < 30) {
		snooze(200000);
		tries++;
	}
	if (fStopping || !fOpen) {
		Quit();
		return;
	}

	be_clipboard->StartWatching(BMessenger(this));
	fReader = spawn_thread(ClipApp::_Reader, "vbox clip", B_NORMAL_PRIORITY,
		this);
	if (fReader >= 0)
		resume_thread(fReader);
	_SendIfChanged();
}


bool
ClipApp::QuitRequested()
{
	_StopReader();
	return true;
}


void
ClipApp::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_CLIPBOARD_CHANGED:
			_SendIfChanged();
			break;
		case kMsgFrame:
		{
			const char* text = NULL;
			ssize_t size = 0;
			if (message->FindData("text", B_MIME_TYPE, (const void**)&text,
					&size) != B_OK || text == NULL || size < 0)
				break;
			BString incoming(text, size);
			if (incoming == fLastSent || incoming == fLastRecv)
				break;
			fLastRecv = incoming;
			_SetClipboard(incoming.String(), incoming.Length());
			break;
		}
		default:
			BApplication::MessageReceived(message);
			break;
	}
}


int32
ClipApp::_Reader(void* cookie)
{
	ClipApp* app = static_cast<ClipApp*>(cookie);
	app->ReadLoop();
	return 0;
}


void
ClipApp::_StopReader()
{
	fStopping = true;
	thread_id reader = fReader;
	fReader = -1;
	if (fOpen) {
		fPort.Close();
		fOpen = false;
	}
	if (reader < 0)
		return;
	kill_thread(reader);
	status_t ignored;
	wait_for_thread_etc(reader, B_RELATIVE_TIMEOUT, 250000, &ignored);
}


void
ClipApp::ReadLoop()
{
	uint8 buffer[4096];
	BString frame;
	int32 needed = 8;

	fPort.SetTimeout(250000);
	while (!fStopping) {
		ssize_t available = fPort.WaitForInput();
		if (fStopping)
			return;
		if (available <= 0)
			continue;

		fLock.Lock();
		ssize_t got = fPort.Read(buffer, sizeof(buffer));
		fLock.Unlock();
		if (got <= 0)
			continue;

		frame.Append(reinterpret_cast<char*>(buffer), got);
		while (frame.Length() >= needed) {
			if (needed == 8) {
				if (frame.ByteAt(0) != kMagic[0]
					|| frame.ByteAt(1) != kMagic[1]
					|| frame.ByteAt(2) != kMagic[2]
					|| frame.ByteAt(3) != kMagic[3]) {
					frame.Remove(0, 1);
					continue;
				}
				uint32 length = 0;
				memcpy(&length, frame.String() + 4, 4);
				if (length > kMaxBytes) {
					frame.Remove(0, 1);
					continue;
				}
				if (length == 0) {
					frame.Remove(0, 8);
					continue;
				}
				needed = 8 + static_cast<int32>(length);
				continue;
			}

			const char* payload = frame.String() + 8;
			int32 payloadLength = needed - 8;
			BMessage msg(kMsgFrame);
			msg.AddData("text", B_MIME_TYPE, payload, payloadLength);
			PostMessage(&msg);
			frame.Remove(0, needed);
			needed = 8;
		}
	}
}


bool
ClipApp::_OpenPort()
{
	if (fPort.Open(kPortName) < 0)
		return false;

	fPort.SetFlowControl(B_NOFLOW_CONTROL);
	fPort.SetDataRate(B_115200_BPS);
	fPort.SetDataBits(B_DATA_BITS_8);
	fPort.SetStopBits(B_STOP_BIT_1);
	fPort.SetParityMode(B_NO_PARITY);
	fPort.SetBlocking(true);
	fOpen = true;
	return true;
}


void
ClipApp::_WriteFrame(const void* data, uint32 length)
{
	uint8 header[8];
	memcpy(header, kMagic, 4);
	memcpy(header + 4, &length, 4);

	fLock.Lock();
	fPort.Write(header, 8);
	if (length > 0)
		fPort.Write(data, length);
	fLock.Unlock();
}


BString
ClipApp::_ClipboardText()
{
	BString text;
	if (!be_clipboard->Lock())
		return text;

	BMessage* clip = be_clipboard->Data();
	const void* data = NULL;
	ssize_t size = 0;
	if (clip != NULL
		&& clip->FindData("text/plain", B_MIME_TYPE, &data, &size) == B_OK
		&& data != NULL && size > 0) {
		text.SetTo(static_cast<const char*>(data), size);
		while (text.Length() > 0
			&& (text.ByteAt(text.Length() - 1) == '\0'))
			text.Truncate(text.Length() - 1);
	}
	be_clipboard->Unlock();
	return text;
}


void
ClipApp::_SetClipboard(const char* text, int32 length)
{
	if (!be_clipboard->Lock())
		return;
	be_clipboard->Clear();
	if (text != NULL && length > 0)
		be_clipboard->Data()->AddData("text/plain", B_MIME_TYPE, text, length);
	be_clipboard->Commit();
	be_clipboard->Unlock();
}


void
ClipApp::_SendIfChanged()
{
	if (!fOpen)
		return;
	BString text = _ClipboardText();
	if (text.Length() == 0 || text == fLastSent || text == fLastRecv)
		return;
	if (static_cast<uint32>(text.Length()) > kMaxBytes)
		text.Truncate(kMaxBytes);
	fLastSent = text;
	_WriteFrame(text.String(), static_cast<uint32>(text.Length()));
}


int
main()
{
	ClipApp app;
	app.Run();
	return 0;
}
