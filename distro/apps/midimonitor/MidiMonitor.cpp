/*
 * Copyright 2026, R5OS Contributors.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * MIDI Monitor: shows every MIDI message that a device or a program sends,
 * one line per message, with the time, the source, the channel, what the
 * message means and its bytes. Each producer gets its own local consumer,
 * so the source of every line is known.
 */


#include <Application.h>
#include <Button.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Menu.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <MidiConsumer.h>
#include <MidiProducer.h>
#include <MidiRoster.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <String.h>
#include <StringView.h>
#include <Window.h>

#include <Midi2Defs.h>

#include <map>
#include <stdio.h>
#include <string.h>


static const uint32 kMsgData = 'mdat';
static const uint32 kMsgClear = 'clea';
static const uint32 kMsgPause = 'paus';
static const uint32 kMsgHideClock = 'hclk';
static const uint32 kMsgSource = 'srce';

static const int32 kMaxLines = 2000;
static const size_t kMaxShownBytes = 24;


class MonitorConsumer : public BMidiLocalConsumer {
public:
	MonitorConsumer(int32 producerId, const char* name,
		const BMessenger& target)
		:
		BMidiLocalConsumer(name),
		fProducerId(producerId),
		fTarget(target)
	{
	}

	virtual void Data(uchar* data, size_t length, bool atomic,
		bigtime_t time)
	{
		// The raw buffer, also a MIDI 2.0 packet. The base class would turn
		// it into NoteOn() and friends, which this program does not need.
		if (data == NULL || length == 0)
			return;
		BMessage message(kMsgData);
		message.AddInt32("producer", fProducerId);
		message.AddData("bytes", B_RAW_TYPE, data, length);
		message.AddInt64("time", time);
		fTarget.SendMessage(&message);
	}

private:
	int32		fProducerId;
	BMessenger	fTarget;
};


struct Source {
	BMidiProducer*		producer;
	MonitorConsumer*	consumer;
	BString				name;
};


static const char*
note_name(uint8 note, char* out, size_t size)
{
	static const char* kNames[] = { "C", "C#", "D", "D#", "E", "F", "F#",
		"G", "G#", "A", "A#", "B" };
	// Note 60 is C4, as in most programs.
	snprintf(out, size, "%s%d", kNames[note % 12], note / 12 - 1);
	return out;
}


static const char*
controller_name(uint8 controller)
{
	switch (controller) {
		case 0: return "Bank select";
		case 1: return "Modulation";
		case 6: return "Data entry";
		case 7: return "Volume";
		case 10: return "Pan";
		case 11: return "Expression";
		case 32: return "Bank select LSB";
		case 64: return "Sustain";
		case 98: return "NRPN LSB";
		case 99: return "NRPN MSB";
		case 100: return "RPN LSB";
		case 101: return "RPN MSB";
		case 120: return "All sound off";
		case 121: return "Reset controllers";
		case 123: return "All notes off";
	}
	return NULL;
}


// Describes one MIDI 1.0 message. Sets channel to 0 for a system message.
static void
describe_midi1(const uint8* bytes, size_t length, int* channel,
	BString& what)
{
	char note[8];
	uint8 status = bytes[0];
	uint8 data1 = length > 1 ? bytes[1] : 0;
	uint8 data2 = length > 2 ? bytes[2] : 0;
	*channel = 0;

	if (status < 0x80) {
		what = "Data bytes (running status)";
		return;
	}
	if (status < 0xf0) {
		*channel = (status & 0x0f) + 1;
		switch (status & 0xf0) {
			case 0x80:
				what.SetToFormat("Note off        %-4s (%3u)  velocity %u",
					note_name(data1, note, sizeof(note)), data1, data2);
				break;
			case 0x90:
				what.SetToFormat("%s %-4s (%3u)  velocity %u",
					data2 == 0 ? "Note off       " : "Note on        ",
					note_name(data1, note, sizeof(note)), data1, data2);
				break;
			case 0xa0:
				what.SetToFormat("Key pressure    %-4s (%3u)  value %u",
					note_name(data1, note, sizeof(note)), data1, data2);
				break;
			case 0xb0:
			{
				const char* name = controller_name(data1);
				what.SetToFormat("Control change  %3u  value %3u%s%s", data1,
					data2, name != NULL ? "  " : "", name != NULL ? name : "");
				break;
			}
			case 0xc0:
				what.SetToFormat("Program change  %u", data1);
				break;
			case 0xd0:
				what.SetToFormat("Channel pressure  %u", data1);
				break;
			case 0xe0:
				what.SetToFormat("Pitch bend      %d",
					(int)(((uint32)data2 << 7) | data1) - 8192);
				break;
		}
		return;
	}

	switch (status) {
		case 0xf0:
			what.SetToFormat("System exclusive  %" B_PRIuSIZE " bytes", length);
			break;
		case 0xf1:
			what.SetToFormat("Time code quarter frame  %u", data1);
			break;
		case 0xf2:
			what.SetToFormat("Song position  %u",
				((uint32)data2 << 7) | data1);
			break;
		case 0xf3:
			what.SetToFormat("Song select  %u", data1);
			break;
		case 0xf6: what = "Tune request"; break;
		case 0xf7: what = "End of exclusive"; break;
		case 0xf8: what = "Clock"; break;
		case 0xfa: what = "Start"; break;
		case 0xfb: what = "Continue"; break;
		case 0xfc: what = "Stop"; break;
		case 0xfe: what = "Active sensing"; break;
		case 0xff: what = "System reset"; break;
		default: what.SetToFormat("System message %02X", status); break;
	}
}


// Describes one Universal MIDI Packet.
static void
describe_ump(const uint32* words, size_t count, int* channel, BString& what)
{
	uint32 type = words[0] >> 28;
	*channel = 0;
	if ((type == 0x1 || type == 0x2) && count == 1) {
		uint8 bytes[3] = { (uint8)(words[0] >> 16), (uint8)(words[0] >> 8),
			(uint8)words[0] };
		describe_midi1(bytes, 3, channel, what);
		what.Prepend("UMP ");
		return;
	}
	if (type == 0x4 && count == 2) {
		uint8 status = (words[0] >> 20) & 0x0f;
		uint8 index = (words[0] >> 8) & 0x7f;
		*channel = ((words[0] >> 16) & 0x0f) + 1;
		char note[8];
		switch (status) {
			case 0x8:
			case 0x9:
				what.SetToFormat("MIDI 2.0 note %s  %-4s (%3u)  velocity %u",
					status == 0x9 ? "on " : "off",
					note_name(index, note, sizeof(note)), index,
					(unsigned)(words[1] >> 16));
				return;
			case 0xb:
				what.SetToFormat("MIDI 2.0 control change  %3u  value %"
					B_PRIu32, index, words[1]);
				return;
			case 0xe:
				what.SetToFormat("MIDI 2.0 pitch bend  %" B_PRIu32, words[1]);
				return;
		}
		what.SetToFormat("MIDI 2.0 channel message %X", status);
		return;
	}
	if (type == 0x3)
		what = "UMP system exclusive";
	else if (type == 0xf)
		what.SetToFormat("UMP stream message %03X", (words[0] >> 16) & 0x3ff);
	else
		what.SetToFormat("UMP type %X", (unsigned)type);
}


static bool
is_clock_or_sensing(const uint8* bytes, size_t length)
{
	if (length == 1)
		return bytes[0] == 0xf8 || bytes[0] == 0xfe;
	uint32 words[4];
	size_t count = 0;
	if (midi2_ump_from_payload(bytes, length, words, 4, &count) != B_OK)
		return false;
	uint8 status = (words[0] >> 16) & 0xff;
	return (words[0] >> 28) == 0x1 && (status == 0xf8 || status == 0xfe);
}


class MonitorWindow : public BWindow {
public:
								MonitorWindow();
	virtual						~MonitorWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			void				_AddProducer(int32 id);
			void				_RemoveProducer(int32 id);
			void				_HandleRoster(BMessage* message);
			void				_ShowData(BMessage* message);
			void				_UpdateStatus();

			BListView*			fList;
			BCheckBox*			fPause;
			BCheckBox*			fHideClock;
			BPopUpMenu*			fSourceMenu;
			BStringView*		fStatus;
			std::map<int32, Source> fSources;
			int32				fShownSource;
			int32				fCount;
			bigtime_t			fStart;
};


MonitorWindow::MonitorWindow()
	:
	BWindow(BRect(80, 80, 900, 560), "MIDI Monitor", B_TITLED_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE),
	fShownSource(-1),
	fCount(0),
	fStart(system_time())
{
	fList = new BListView("messages", B_SINGLE_SELECTION_LIST);
	fList->SetFont(be_fixed_font);
	BScrollView* scroll = new BScrollView("scroll", fList, 0, true, true);

	fSourceMenu = new BPopUpMenu("source");
	BMessage* all = new BMessage(kMsgSource);
	all->AddInt32("producer", -1);
	BMenuItem* allItem = new BMenuItem("All devices", all);
	allItem->SetMarked(true);
	fSourceMenu->AddItem(allItem);
	fSourceMenu->AddSeparatorItem();
	BMenuField* source = new BMenuField("source", "Show:", fSourceMenu);

	fPause = new BCheckBox("pause", "Pause", new BMessage(kMsgPause));
	fHideClock = new BCheckBox("clock", "Hide clock and active sensing",
		new BMessage(kMsgHideClock));
	fHideClock->SetValue(B_CONTROL_ON);
	BButton* clear = new BButton("clear", "Clear", new BMessage(kMsgClear));
	fStatus = new BStringView("status", "");

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.AddGroup(B_HORIZONTAL)
			.Add(source)
			.Add(fHideClock)
			.Add(fPause)
			.AddGlue()
			.Add(clear)
		.End()
		.Add(scroll, 1.0f)
		.Add(fStatus);

	BMidiRoster* roster = BMidiRoster::MidiRoster();
	if (roster != NULL) {
		BMessenger self(this);
		roster->StartWatching(&self);
	}

	int32 id = 0;
	BMidiProducer* producer;
	while ((producer = BMidiRoster::NextProducer(&id)) != NULL) {
		int32 producerId = producer->ID();
		producer->Release();
		_AddProducer(producerId);
	}
	_UpdateStatus();
}


MonitorWindow::~MonitorWindow()
{
	BMidiRoster* roster = BMidiRoster::MidiRoster();
	if (roster != NULL)
		roster->StopWatching();

	while (!fSources.empty())
		_RemoveProducer(fSources.begin()->first);

	while (fList->CountItems() > 0)
		delete fList->RemoveItem(fList->CountItems() - 1);
}


bool
MonitorWindow::QuitRequested()
{
	return true;
}


void
MonitorWindow::_AddProducer(int32 id)
{
	if (fSources.find(id) != fSources.end())
		return;
	BMidiProducer* producer = BMidiRoster::FindProducer(id);
	if (producer == NULL)
		return;

	Source source;
	source.producer = producer;
	source.name = producer->Name();
	if (source.name.IsEmpty())
		source.name.SetToFormat("Producer %" B_PRId32, id);
	BString consumerName("MIDI Monitor: ");
	consumerName << source.name;
	source.consumer = new MonitorConsumer(id, consumerName.String(),
		BMessenger(this));
	if (producer->Connect(source.consumer) != B_OK) {
		source.consumer->Release();
		producer->Release();
		return;
	}
	fSources[id] = source;

	BMessage* message = new BMessage(kMsgSource);
	message->AddInt32("producer", id);
	fSourceMenu->AddItem(new BMenuItem(source.name.String(), message));
	_UpdateStatus();
}


void
MonitorWindow::_RemoveProducer(int32 id)
{
	std::map<int32, Source>::iterator found = fSources.find(id);
	if (found == fSources.end())
		return;

	Source& source = found->second;
	source.producer->Disconnect(source.consumer);
	source.consumer->Release();
	source.producer->Release();

	for (int32 i = fSourceMenu->CountItems() - 1; i >= 0; i--) {
		BMenuItem* item = fSourceMenu->ItemAt(i);
		int32 itemId;
		if (item->Message() != NULL
			&& item->Message()->FindInt32("producer", &itemId) == B_OK
			&& itemId == id) {
			if (item->IsMarked())
				fSourceMenu->ItemAt(0)->SetMarked(true);
			delete fSourceMenu->RemoveItem(i);
		}
	}
	if (fShownSource == id)
		fShownSource = -1;

	fSources.erase(found);
	_UpdateStatus();
}


void
MonitorWindow::_HandleRoster(BMessage* message)
{
	int32 op;
	int32 id;
	const char* type;
	if (message->FindInt32("be:op", &op) != B_OK
		|| message->FindInt32("be:id", &id) != B_OK
		|| message->FindString("be:type", &type) != B_OK
		|| strcmp(type, "producer") != 0) {
		return;
	}

	if (op == B_MIDI_REGISTERED)
		_AddProducer(id);
	else if (op == B_MIDI_UNREGISTERED)
		_RemoveProducer(id);
	else if (op == B_MIDI_CHANGED_NAME) {
		std::map<int32, Source>::iterator found = fSources.find(id);
		const char* name;
		if (found != fSources.end()
			&& message->FindString("be:name", &name) == B_OK) {
			found->second.name = name;
		}
	}
}


void
MonitorWindow::_ShowData(BMessage* message)
{
	if (fPause->Value() == B_CONTROL_ON)
		return;

	int32 producer;
	const void* data;
	ssize_t length;
	bigtime_t time;
	if (message->FindInt32("producer", &producer) != B_OK
		|| message->FindData("bytes", B_RAW_TYPE, &data, &length) != B_OK
		|| length <= 0 || message->FindInt64("time", &time) != B_OK) {
		return;
	}
	if (fShownSource != -1 && producer != fShownSource)
		return;

	const uint8* bytes = (const uint8*)data;
	if (fHideClock->Value() == B_CONTROL_ON
		&& is_clock_or_sensing(bytes, (size_t)length)) {
		return;
	}

	BString what;
	BString raw;
	int channel = 0;
	uint32 words[4];
	size_t count = 0;
	if (midi2_ump_from_payload(bytes, (size_t)length, words, 4, &count)
			== B_OK) {
		describe_ump(words, count, &channel, what);
		for (size_t i = 0; i < count; i++)
			raw << BString().SetToFormat("%08" B_PRIX32 " ", words[i]);
	} else {
		describe_midi1(bytes, (size_t)length, &channel, what);
		for (ssize_t i = 0; i < length && (size_t)i < kMaxShownBytes; i++)
			raw << BString().SetToFormat("%02X ", bytes[i]);
		if ((size_t)length > kMaxShownBytes)
			raw << "...";
	}

	BString name;
	std::map<int32, Source>::iterator found = fSources.find(producer);
	if (found != fSources.end())
		name = found->second.name;
	if (name.CountChars() > 20)
		name.TruncateChars(20);

	BString channelText = channel > 0
		? BString().SetToFormat("%2d", channel) : BString("--");
	BString line;
	line.SetToFormat("%10.3f  %-20s  %s  %-48s  %s",
		(time > fStart ? time - fStart : 0) / 1000000.0, name.String(),
		channelText.String(), what.String(), raw.String());

	fList->AddItem(new BStringItem(line.String()));
	while (fList->CountItems() > kMaxLines)
		delete fList->RemoveItem((int32)0);
	// Keep the newest line in view.
	float top = fList->ItemFrame(fList->CountItems() - 1).bottom
		- fList->Bounds().Height();
	fList->ScrollTo(0, top > 0 ? top : 0);
	fCount++;
	_UpdateStatus();
}


void
MonitorWindow::_UpdateStatus()
{
	BString text;
	text.SetToFormat("%d MIDI sources  -  %" B_PRId32 " messages",
		(int)fSources.size(), fCount);
	fStatus->SetText(text.String());
}


void
MonitorWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgData:
			_ShowData(message);
			break;
		case B_MIDI_EVENT:
			_HandleRoster(message);
			break;
		case kMsgClear:
			while (fList->CountItems() > 0)
				delete fList->RemoveItem(fList->CountItems() - 1);
			fCount = 0;
			_UpdateStatus();
			break;
		case kMsgSource:
			message->FindInt32("producer", &fShownSource);
			break;
		case kMsgPause:
		case kMsgHideClock:
			break;
		default:
			BWindow::MessageReceived(message);
	}
}


int
main()
{
	BApplication app("application/x-vnd.R5-MidiMonitor");
	(new MonitorWindow())->Show();
	app.Run();
	return 0;
}
