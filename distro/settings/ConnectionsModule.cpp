/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Connections, under Network: the network cards, their IPv4 and IPv6
 * setup, and DNS. Like Display, a change is applied at once through the
 * system calls, after AdminChangeAllowed(): the interface settings through
 * BNetworkSettings (net_server applies them), on and off through
 * BNetworkInterface, the way the Network program does it. A manual address
 * waits for Apply. Wi-Fi opens the Wi-Fi page.
 */


#include "ConnectionsModule.h"

#include "NetworkPage.h"
#include "SettingsFont.h"
#include "SettingsManager.h"
#include "SettingsMessages.h"
#include "SettingsModule.h"

#include <Box.h>
#include <Button.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <Deskbar.h>
#include <Roster.h>
#include <File.h>
#include <FindDirectory.h>
#include <GridLayout.h>
#include <GridView.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <LayoutItem.h>
#include <ListView.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <NetworkAddress.h>
#include <NetworkDevice.h>
#include <NetworkInterface.h>
#include <NetworkRoster.h>
#include <NetworkSettings.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <String.h>
#include <StringList.h>
#include <StringView.h>
#include <TextControl.h>
#include <Window.h>

#include <net/if.h>
#include <net/if_media.h>
#include <string.h>

#include <vector>

#include "IPAddressControl.h"
#include "MediaTypes.h"


using BNetworkKit::BNetworkInterfaceAddressSettings;
using BNetworkKit::BNetworkInterfaceSettings;
using BNetworkKit::BNetworkSettings;


static const char* kModuleId = "connections";

static const uint32 kMsgSelect = 'cnsl';
static const uint32 kMsgUp = 'cnup';
static const uint32 kMsgMode = 'cnmd';
static const uint32 kMsgApply = 'cnap';
static const uint32 kMsgEditDns = 'cned';
static const uint32 kMsgDnsChosen = 'cndc';
static const uint32 kMsgTick = 'cntk';
static const uint32 kMsgDeskbar = 'cndb';

static const char* kNetworkStatusSignature
	= "application/x-vnd.Haiku-NetworkStatus";

enum {
	kModeAuto = 0,
	kModeManual,
	kModeOff
};


struct AddressSetup {
	int32		mode;
	BString		address;
	BString		mask;
	BString		router;

	AddressSetup()
		:
		mode(kModeAuto)
	{
	}

	bool operator==(const AddressSetup& other) const
	{
		if (mode != other.mode)
			return false;
		return mode != kModeManual || (address == other.address
			&& mask == other.mask && router == other.router);
	}

	bool operator!=(const AddressSetup& other) const
	{
		return !(*this == other);
	}
};


struct InterfaceSetup {
	BString		name;
	bool		up;
	AddressSetup ipv4;
	AddressSetup ipv6;

	InterfaceSetup()
		:
		up(true)
	{
	}

	bool operator==(const InterfaceSetup& other) const
	{
		return up == other.up && ipv4 == other.ipv4 && ipv6 == other.ipv6;
	}

	bool operator!=(const InterfaceSetup& other) const
	{
		return !(*this == other);
	}
};


struct DnsSetup {
	bool		manual;
	BString		servers;	// comma separated
	BString		domain;

	DnsSetup()
		:
		manual(false)
	{
	}

	bool operator==(const DnsSetup& other) const
	{
		if (manual != other.manual)
			return false;
		return !manual || (servers == other.servers && domain == other.domain);
	}

	bool operator!=(const DnsSetup& other) const
	{
		return !(*this == other);
	}
};


// #pragma mark - settings


// What the interface settings say for one family. No entry is automatic,
// as in the Network program.
static AddressSetup
read_address(const char* name, int family)
{
	AddressSetup setup;
	BNetworkSettings settings;
	BMessage message;
	if (settings.GetInterface(name, message) != B_OK)
		return setup;

	BNetworkInterfaceSettings interface(message);
	int32 index = interface.FindFirstAddress(family);
	if (index < 0)
		return setup;

	BNetworkInterfaceAddressSettings& address = interface.AddressAt(index);
	if (address.IsAutoConfigure())
		return setup;
	if (address.Address().IsEmpty()) {
		setup.mode = kModeOff;
		return setup;
	}
	setup.mode = kModeManual;
	setup.address = address.Address().ToString();
	setup.mask = address.Mask().ToString();
	if (!address.Gateway().IsEmpty())
		setup.router = address.Gateway().ToString();
	return setup;
}


// InterfaceAddressView::_UpdateSettings() of the Network program.
static status_t
apply_address(const char* name, int family, const AddressSetup& setup)
{
	BNetworkSettings settings;
	BMessage message;
	settings.GetInterface(name, message);
	BNetworkInterfaceSettings interface(message);
	interface.SetName(name);

	int32 index = interface.FindFirstAddress(family);
	if (index < 0)
		index = interface.FindFirstAddress(AF_UNSPEC);
	BNetworkInterfaceAddressSettings address;
	if (index >= 0)
		address = interface.AddressAt(index);

	address.SetFamily(family);
	address.SetAutoConfigure(setup.mode == kModeAuto);
	address.Address().Unset();
	address.Mask().Unset();
	address.Peer().Unset();
	address.Broadcast().Unset();
	address.Gateway().Unset();
	if (setup.mode == kModeManual) {
		address.Address().SetTo(family, setup.address, (char*)NULL,
			B_NO_ADDRESS_RESOLUTION);
		address.Mask().SetTo(family, setup.mask, (char*)NULL,
			B_NO_ADDRESS_RESOLUTION);
		if (!setup.router.IsEmpty()) {
			address.Gateway().SetTo(family, setup.router, (char*)NULL,
				B_NO_ADDRESS_RESOLUTION);
		}
	}

	if (index >= 0)
		interface.AddressAt(index) = address;
	else
		interface.AddAddress(address);

	message.MakeEmpty();
	status_t status = interface.GetMessage(message);
	if (status != B_OK)
		return status;
	return settings.AddInterface(message);
}


// The Disable button of the Network program.
static status_t
apply_up(const char* name, bool up)
{
	BNetworkInterface interface(name);
	uint32 flags = interface.Flags();
	if (((flags & IFF_UP) != 0) == up)
		return B_OK;
	return interface.SetFlags(up ? flags | IFF_UP : flags & ~IFF_UP);
}


// NetworkWindow::_ShowReplicant() of the Network program.
static status_t
apply_deskbar(bool show)
{
	if (!show) {
		BDeskbar deskbar;
		return deskbar.RemoveItem("NetworkStatus");
	}
	const char* argv[] = { "--deskbar", NULL };
	status_t status = be_roster->Launch(kNetworkStatusSignature, 1, argv);
	return status == B_ALREADY_RUNNING ? B_OK : status;
}


static status_t
resolv_path(BPath& path)
{
	status_t status = find_directory(B_SYSTEM_SETTINGS_DIRECTORY, &path);
	if (status == B_OK)
		status = path.Append("network/resolv.conf");
	return status;
}


static DnsSetup
read_dns()
{
	DnsSetup dns;
	BPath path;
	if (resolv_path(path) != B_OK)
		return dns;
	BFile file(path.Path(), B_READ_ONLY);
	off_t size = 0;
	if (file.InitCheck() != B_OK || file.GetSize(&size) != B_OK
		|| size <= 0 || size > 65536)
		return dns;

	BString text;
	char* buffer = text.LockBuffer(size);
	ssize_t bytes = file.Read(buffer, size);
	text.UnlockBuffer(bytes > 0 ? bytes : 0);

	BStringList lines;
	text.Split("\n", true, lines);
	for (int32 i = 0; i < lines.CountStrings(); i++) {
		BString line = lines.StringAt(i);
		if (line.StartsWith("# Static DNS Only"))
			dns.manual = true;
		line.ReplaceAll('\t', ' ');
		line.Trim();
		BString value;
		if (line.StartsWith("nameserver ")) {
			line.CopyInto(value, 11, line.Length() - 11);
			value.Trim();
			if (!dns.servers.IsEmpty())
				dns.servers << ", ";
			dns.servers << value;
		} else if (line.StartsWith("domain ")) {
			line.CopyInto(value, 7, line.Length() - 7);
			dns.domain = value.Trim();
		}
	}
	return dns;
}


// "# Static DNS Only" keeps net_server from changing the servers (Haiku's
// convention). Without it, DHCP and router advertisements fill the file.
static status_t
apply_dns(const DnsSetup& dns)
{
	BString content;
	if (dns.manual) {
		content << "# Static DNS Only\n";
		BStringList servers;
		dns.servers.Split(",", true, servers);
		for (int32 i = 0; i < servers.CountStrings(); i++) {
			BString server = servers.StringAt(i);
			if (server.Trim().Length() > 0)
				content << "nameserver\t" << server << "\n";
		}
		if (dns.domain.Length() > 0)
			content << "domain\t" << dns.domain << "\n";
	} else
		content << "# Dynamic DNS entries\n# may be altered by DHCP\n";

	BPath path;
	status_t status = resolv_path(path);
	if (status != B_OK)
		return status;
	BFile file(path.Path(), B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);
	status = file.InitCheck();
	if (status != B_OK)
		return status;
	ssize_t bytes = file.Write(content.String(), content.Length());
	if (bytes < 0)
		return bytes;
	return bytes == content.Length() ? B_OK : B_ERROR;
}


static bool
is_wireless(const char* name)
{
	BNetworkDevice device(name);
	return device.IsWireless();
}


static BString
kind_of(const char* name)
{
	if (is_wireless(name))
		return "Wi-Fi";
	if (strstr(name, "/usb") != NULL)
		return "USB Ethernet";
	return "Ethernet";
}


static bool
is_connected(const char* name)
{
	BNetworkInterface interface(name);
	return (interface.Flags() & IFF_UP) != 0 && interface.HasLink();
}


static BString
status_of(const char* name, bool full)
{
	BNetworkInterface interface(name);
	if ((interface.Flags() & IFF_UP) == 0)
		return "Off";

	BNetworkDevice device(name);
	if (device.IsWireless()) {
		uint32 cookie = 0;
		wireless_network network;
		if (device.GetNextAssociatedNetwork(cookie, network) != B_OK)
			return "Not connected";
		BString text;
		text.SetToFormat("Connected to “%s”", network.name);
		return text;
	}
	if (!interface.HasLink())
		return "Cable unplugged";

	BString text("Connected");
	int media = interface.Media();
	const char* speed = (media & IFM_ACTIVE) != 0
		? media_type_to_string(media) : NULL;
	if (speed != NULL && full)
		text << " · " << speed;
	return text;
}


static BString
hardware_of(const char* name)
{
	// The driver's name, as in /dev/net/<driver>/0.
	BString driver(name);
	driver.RemoveFirst("/dev/net/");
	int32 slash = driver.FindFirst('/');
	if (slash > 0)
		driver.Truncate(slash);

	BNetworkInterface interface(name);
	BNetworkAddress link;
	if (interface.GetHardwareAddress(link) == B_OK && !link.IsEmpty())
		driver << " · " << link.ToString();
	return driver;
}


// #pragma mark - views


// A connection in the list: a dot, the kind in bold, the status under it.
class ConnectionItem : public BListItem {
public:
								ConnectionItem(const char* name,
									const char* label);

			const char*			Name() const { return fName.String(); }
			void				SetStatus(const char* status, bool connected);
	virtual	void				DrawItem(BView* owner, BRect frame,
									bool complete);
	virtual	void				Update(BView* owner, const BFont* font);

private:
			BString				fName;
			BString				fLabel;
			BString				fStatus;
			bool				fConnected;
};


ConnectionItem::ConnectionItem(const char* name, const char* label)
	:
	fName(name),
	fLabel(label),
	fConnected(false)
{
}


void
ConnectionItem::SetStatus(const char* status, bool connected)
{
	fStatus = status;
	fConnected = connected;
}


void
ConnectionItem::Update(BView* owner, const BFont* font)
{
	BListItem::Update(owner, font);
	font_height height;
	font->GetHeight(&height);
	float line = ceilf(height.ascent + height.descent + height.leading);
	SetHeight(line * 2 + network_unit() * 12);
}


void
ConnectionItem::DrawItem(BView* owner, BRect frame, bool complete)
{
	(void)complete;
	rgb_color background = ui_color(B_LIST_BACKGROUND_COLOR);
	if (IsSelected()) {
		background = mix_color(background,
			ui_color(B_CONTROL_HIGHLIGHT_COLOR), 60);
	}
	owner->SetLowColor(background);
	owner->FillRect(frame, B_SOLID_LOW);

	float u = network_unit();
	font_height height;
	be_plain_font->GetHeight(&height);
	float line = ceilf(height.ascent + height.descent + height.leading);
	float x = frame.left + u * 8;
	float top = frame.top + u * 6;

	owner->SetHighColor(fConnected ? ui_color(B_SUCCESS_COLOR)
		: tint_color(background, B_DARKEN_2_TINT));
	owner->FillEllipse(BPoint(x + u * 4, top + line / 2), u * 4, u * 4);
	x += u * 16;

	owner->SetFont(be_bold_font);
	owner->SetHighColor(ui_color(B_LIST_ITEM_TEXT_COLOR));
	owner->DrawString(fLabel.String(), BPoint(x, top + height.ascent));

	BFont small(be_plain_font);
	small.SetSize(be_plain_font->Size() * 0.9f);
	owner->SetFont(&small);
	owner->SetHighColor(network_quiet_color(140));
	BString status(fStatus);
	owner->TruncateString(&status, B_TRUNCATE_END, frame.right - x - u * 4);
	owner->DrawString(status.String(),
		BPoint(x, top + line + height.ascent));
	owner->SetFont(be_plain_font);
}


// Edit… of DNS: the servers and the search domain.
class DnsWindow : public BWindow {
public:
								DnsWindow(const DnsSetup& dns,
									const BMessenger& target);
	virtual	void				MessageReceived(BMessage* message);

private:
			BTextControl*		fServers;
			BTextControl*		fDomain;
			BMessenger			fTarget;
};


static const uint32 kMsgDnsSave = 'cnds';
static const uint32 kMsgDnsNetwork = 'cndn';


DnsWindow::DnsWindow(const DnsSetup& dns, const BMessenger& target)
	:
	BWindow(BRect(0, 0, 420, 140), "DNS", B_TITLED_WINDOW,
		B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fTarget(target)
{
	fServers = new BTextControl("servers", "Servers:", dns.servers, NULL);
	fDomain = new BTextControl("domain", "Search domain:", dns.domain, NULL);
	BStringView* hint = new BStringView("hint",
		"Separate the servers with a comma.");
	BButton* network = new BButton("network", "Use the network's servers",
		new BMessage(kMsgDnsNetwork));
	BButton* cancel = new BButton("cancel", "Cancel",
		new BMessage(B_QUIT_REQUESTED));
	BButton* save = new BButton("save", "Save", new BMessage(kMsgDnsSave));

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.AddGrid()
			.AddTextControl(fServers, 0, 0)
			.AddTextControl(fDomain, 0, 1)
			.Add(hint, 1, 2)
			.End()
		.AddGroup(B_HORIZONTAL)
			.Add(network)
			.AddGlue()
			.Add(cancel)
			.Add(save)
			.End();

	fServers->SetAlignment(B_ALIGN_RIGHT, B_ALIGN_LEFT);
	fDomain->SetAlignment(B_ALIGN_RIGHT, B_ALIGN_LEFT);
	SetDefaultButton(save);
	fServers->MakeFocus(true);
	CenterOnScreen();
}


void
DnsWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgDnsSave:
		case kMsgDnsNetwork:
		{
			BMessage chosen(kMsgDnsChosen);
			chosen.AddBool("manual", message->what == kMsgDnsSave);
			chosen.AddString("servers", fServers->Text());
			chosen.AddString("domain", fDomain->Text());
			fTarget.SendMessage(&chosen);
			PostMessage(B_QUIT_REQUESTED);
			break;
		}
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


// #pragma mark - ConnectionsModule


class ConnectionsView;


class ConnectionsModule : public SettingsModule {
public:
								ConnectionsModule();

	virtual	const char*			Identifier() const { return kModuleId; }
	virtual	const char*			Name() const { return "Connections"; }
	virtual	const char*			Category() const { return "network"; }
	virtual	const char*			Description() const
									{ return "Wired connections, addresses, "
										"and DNS"; }
	virtual	const char*			Keywords() const
									{ return "network ethernet ip ipv4 ipv6 "
										"dhcp dns address router"; }
	virtual	bool				RequiresAdmin() const { return true; }
	virtual	BView*				CreateView(SettingsContext* context,
									BRect frame);
	virtual	status_t			LoadSettings(SettingsContext* context);
	virtual	status_t			SaveSettings(SettingsContext* context);
	virtual	status_t			RevertSettings(SettingsContext* context);
	virtual	bool				IsModified() const;
	virtual	bool				CanApplyDefaults() const;
	virtual	status_t			ApplyDefaults(SettingsContext* context);

			void				ClearView() { fView = NULL; }
			SettingsContext*	Context() const { return fContext; }
			int32				CountInterfaces() const
									{ return fCurrent.size(); }
			const InterfaceSetup& InterfaceAt(int32 index) const
									{ return fCurrent[index]; }
			const InterfaceSetup* Find(const char* name) const;
			const DnsSetup&		Dns() const { return fDns; }
			bool				Deskbar() const { return fDeskbar; }

			// apply false: only shown, as a manual address before Apply
			void				UserEdited(const InterfaceSetup& setup,
									bool apply);
			void				UserEditedDns(const DnsSetup& dns);
			void				UserEditedDeskbar(bool show);

private:
			status_t			_Apply(const InterfaceSetup& from,
									const InterfaceSetup& to);
			void				_Changed();

			SettingsContext*	fContext;
			ConnectionsView*	fView;
			std::vector<InterfaceSetup> fCurrent;	// on the page
			std::vector<InterfaceSetup> fActive;	// on the cards
			std::vector<InterfaceSetup> fOpened;
			DnsSetup			fDns;
			DnsSetup			fDnsOpened;
			bool				fDeskbar;
			bool				fDeskbarOpened;
};


class ConnectionsView : public BGroupView {
public:
								ConnectionsView(ConnectionsModule* module);
	virtual						~ConnectionsView();

	virtual	void				AttachedToWindow();
	virtual	void				AllAttached();
	virtual	void				DetachedFromWindow();
	virtual	void				MessageReceived(BMessage* message);

			void				Reload();

private:
			BBox*				_Box(const char* name, const char* title,
									BGridView*& grid);
			BBox*				_AddressBox(const char* name,
									const char* title, BGroupView*& group);
			BMenuField*			_ModeField(const char* label,
									const char* automatic, int family,
									BPopUpMenu*& menu);
			int32				_Mode(BPopUpMenu* menu) const;
			void				_SetMode(BPopUpMenu* menu, int32 mode);
			void				_FillList();
			void				_ShowSelected();
			void				_ShowLive();
			void				_UpdateEnabled();
			void				_ShowManualRows(const InterfaceSetup& setup);
			void				_ReadFields(int family, AddressSetup& setup);
			void				_FillManual6(const char* name);
			const InterfaceSetup* _Selected() const;

			ConnectionsModule*	fModule;
			BString				fSelected;
			BMessageRunner*		fTicker;

			BListView*			fList;
			BCheckBox*			fDeskbar;
			BBox*				fDeviceBox;
			BCheckBox*			fUp;
			ValueField*			fStatus;
			ValueField*			fHardware;

			BMenuField*			fMode4;
			BPopUpMenu*			fMode4Menu;
			IPAddressControl*	fAddress4;
			IPAddressControl*	fMask4;
			IPAddressControl*	fRouter4;
			BButton*			fApply4;

			BMenuField*			fMode6;
			BPopUpMenu*			fMode6Menu;
			BStringView*		fAddressesLabel;
			BGroupView*			fAddresses6;
			IPAddressControl*	fAddress6;
			IPAddressControl*	fMask6;
			IPAddressControl*	fRouter6;
			BButton*			fApply6;

			ValueField*			fServers;
			ValueField*			fDomain;
			BButton*			fEditDns;
};


ConnectionsView::ConnectionsView(ConnectionsModule* module)
	:
	BGroupView("connections", B_HORIZONTAL),
	fModule(module),
	fTicker(NULL)
{
	float u = network_unit();
	BGridView* grid;

	// the list
	BBox* listBox = _Box("connections box", "Connections", grid);
	fList = new BListView("connections list");
	fList->SetSelectionMessage(new BMessage(kMsgSelect));
	BScrollView* scroll = new BScrollView("connections scroll", fList, 0,
		false, false);
	grid->GridLayout()->AddView(scroll, 0, 0);
	// the Network program's switch, in the room the list leaves free
	fDeskbar = new BCheckBox("deskbar", "Show in Deskbar",
		new BMessage(kMsgDeskbar));
	grid->GridLayout()->AddView(fDeskbar, 0, 1);
	listBox->SetExplicitMinSize(BSize(u * 196, B_SIZE_UNSET));
	listBox->SetExplicitMaxSize(BSize(u * 196, B_SIZE_UNLIMITED));

	// the card: the title is the switch, as Enable virtual memory
	fDeviceBox = new FilledBox("device box");
	fUp = new BoxCheckBox("up", "Ethernet", new BMessage(kMsgUp));
	fDeviceBox->SetLabel(fUp);
	BGridView* deviceGrid = new BGridView(u * 12, u * 6);
	deviceGrid->SetViewColor(network_box_fill());
	fDeviceBox->AddChild(deviceGrid);
	fStatus = new ValueField("status");
	fHardware = new ValueField("hardware");
	BStringView* statusLabel = new BStringView("status label", "Status:");
	BStringView* hardwareLabel = new BStringView("hardware label",
		"Hardware:");
	statusLabel->SetAlignment(B_ALIGN_RIGHT);
	hardwareLabel->SetAlignment(B_ALIGN_RIGHT);
	BLayoutBuilder::Grid<>(deviceGrid)
		.SetInsets(u * 12, u * 8, u * 12, u * 12)
		.Add(statusLabel, 0, 0)
		.Add(fStatus, 1, 0)
		.Add(hardwareLabel, 0, 1)
		.Add(fHardware, 1, 1);

	// IPv4 and IPv6 are built as InterfaceAddressView of the Network
	// program builds them: the labels right-aligned through the layout
	// builder, Apply in its own row under the grid.
	float minimumWidth = be_control_look->DefaultItemSpacing() * 15;

	// IPv4
	BGroupView* group4;
	BBox* box4 = _AddressBox("ipv4 box", "IPv4", group4);
	fMode4 = _ModeField("Configure:", "Automatic (DHCP)", AF_INET,
		fMode4Menu);
	fAddress4 = new IPAddressControl(AF_INET, "Address:", "address4");
	fMask4 = new IPAddressControl(AF_INET, "Subnet mask:", "mask4");
	fRouter4 = new IPAddressControl(AF_INET, "Router:", "router4");
	BMessage* apply4 = new BMessage(kMsgApply);
	apply4->AddInt32("family", AF_INET);
	fApply4 = new BButton("apply4", "Apply", apply4);
	BLayoutBuilder::Group<>(group4)
		.AddGrid()
			.AddMenuField(fMode4, 0, 0, B_ALIGN_RIGHT)
			.AddTextControl(fAddress4, 0, 1, B_ALIGN_RIGHT)
			.AddTextControl(fMask4, 0, 2, B_ALIGN_RIGHT)
			.AddTextControl(fRouter4, 0, 3, B_ALIGN_RIGHT)
			.End()
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fApply4)
			.End()
		.AddGlue();

	// IPv6
	BGroupView* group6;
	BBox* box6 = _AddressBox("ipv6 box", "IPv6", group6);
	fMode6 = _ModeField("Configure:", "Automatic", AF_INET6, fMode6Menu);
	fMode6->SetToolTip("Automatic uses router advertisements (SLAAC) and "
		"DHCPv6.");
	fAddressesLabel = new BStringView("addresses label", "Addresses:");
	fAddressesLabel->SetAlignment(B_ALIGN_RIGHT);
	fAddresses6 = new BGroupView(B_VERTICAL, u * 3);
	fAddresses6->SetViewColor(network_box_fill());
	fAddress6 = new IPAddressControl(AF_INET6, "Address:", "address6");
	fMask6 = new IPAddressControl(AF_INET6, "Netmask:", "mask6");
	fRouter6 = new IPAddressControl(AF_INET6, "Router:", "router6");
	BMessage* apply6 = new BMessage(kMsgApply);
	apply6->AddInt32("family", AF_INET6);
	fApply6 = new BButton("apply6", "Apply", apply6);
	BLayoutBuilder::Group<>(group6)
		.AddGrid()
			.AddMenuField(fMode6, 0, 0, B_ALIGN_RIGHT)
			.Add(fAddressesLabel, 0, 1)
			.Add(fAddresses6, 1, 1)
			.AddTextControl(fAddress6, 0, 2, B_ALIGN_RIGHT)
			.AddTextControl(fMask6, 0, 3, B_ALIGN_RIGHT)
			.AddTextControl(fRouter6, 0, 4, B_ALIGN_RIGHT)
			.End()
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fApply6)
			.End()
		.AddGlue();

	// DNS
	BBox* boxDns = _Box("dns box", "DNS", grid);
	fServers = new ValueField("servers");
	fDomain = new ValueField("domain");
	fEditDns = new BButton("edit dns", "Edit…", new BMessage(kMsgEditDns));
	BStringView* serversLabel = new BStringView("servers label", "Servers:");
	BStringView* domainLabel = new BStringView("domain label",
		"Search domain:");
	serversLabel->SetAlignment(B_ALIGN_RIGHT);
	domainLabel->SetAlignment(B_ALIGN_RIGHT);
	BLayoutBuilder::Grid<>(grid)
		.Add(serversLabel, 0, 0)
		.Add(fServers, 1, 0)
		.Add(fEditDns, 2, 0)
		.Add(domainLabel, 0, 1)
		.Add(fDomain, 1, 1);

	IPAddressControl* fields[] = { fAddress4, fMask4, fRouter4, fAddress6,
		fMask6, fRouter6 };
	for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
		fields[i]->TextView()->SetExplicitMinSize(
			BSize(minimumWidth, B_SIZE_UNSET));
		fields[i]->TextView()->SetFontAndColor(be_fixed_font);
	}

	// IPv4 and IPv6 are as wide as each other
	BSize half(u * 220, B_SIZE_UNSET);
	box4->SetExplicitMinSize(half);
	box4->SetExplicitPreferredSize(half);
	box6->SetExplicitMinSize(half);
	box6->SetExplicitPreferredSize(half);
	BSize wide(B_SIZE_UNLIMITED, B_SIZE_UNSET);
	box4->SetExplicitMaxSize(wide);
	box6->SetExplicitMaxSize(wide);

	BLayoutBuilder::Group<>(this, B_HORIZONTAL, u * 12)
		.Add(listBox)
		.AddGroup(B_VERTICAL, u * 12)
			.Add(fDeviceBox)
			.AddGroup(B_HORIZONTAL, u * 12)
				.Add(box4, 1)
				.Add(box6, 1)
				.End()
			.Add(boxDns)
			.AddGlue()
			.End();
}


ConnectionsView::~ConnectionsView()
{
	delete fTicker;
	if (fModule != NULL)
		fModule->ClearView();
}


BBox*
ConnectionsView::_Box(const char* name, const char* title, BGridView*& grid)
{
	float u = network_unit();
	BBox* box = new FilledBox(name);
	box->SetLabel(new BoxTitle(title));
	grid = new BGridView(u * 12, u * 6);
	grid->SetViewColor(network_box_fill());
	grid->GridLayout()->SetInsets(u * 12, u * 8, u * 12, u * 12);
	grid->GridLayout()->SetColumnWeight(1, 1);
	box->AddChild(grid);
	return box;
}


BBox*
ConnectionsView::_AddressBox(const char* name, const char* title,
	BGroupView*& group)
{
	float u = network_unit();
	BBox* box = new FilledBox(name);
	box->SetLabel(new BoxTitle(title));
	group = new BGroupView(B_VERTICAL, u * 6);
	group->SetViewColor(network_box_fill());
	group->GroupLayout()->SetInsets(u * 12, u * 8, u * 12, u * 12);
	box->AddChild(group);
	return box;
}


BMenuField*
ConnectionsView::_ModeField(const char* label, const char* automatic,
	int family, BPopUpMenu*& menu)
{
	menu = new BPopUpMenu("mode");
	const char* labels[] = { automatic, "Manual", "Off" };
	for (int32 mode = kModeAuto; mode <= kModeOff; mode++) {
		BMessage* message = new BMessage(kMsgMode);
		message->AddInt32("family", family);
		message->AddInt32("mode", mode);
		menu->AddItem(new BMenuItem(labels[mode], message));
	}
	BMenuField* field = new BMenuField("mode", label, menu);
	field->SetAlignment(B_ALIGN_RIGHT);
	return field;
}


int32
ConnectionsView::_Mode(BPopUpMenu* menu) const
{
	BMenuItem* item = menu->FindMarked();
	int32 mode = kModeAuto;
	if (item != NULL && item->Message() != NULL)
		item->Message()->FindInt32("mode", &mode);
	return mode;
}


void
ConnectionsView::_SetMode(BPopUpMenu* menu, int32 mode)
{
	BMenuItem* item = menu->ItemAt(mode);
	if (item != NULL)
		item->SetMarked(true);
}


void
ConnectionsView::AttachedToWindow()
{
	BGroupView::AttachedToWindow();
	fList->SetTarget(this);
	fUp->SetTarget(this);
	fDeskbar->SetTarget(this);
	fMode4Menu->SetTargetForItems(this);
	fMode6Menu->SetTargetForItems(this);
	fApply4->SetTarget(this);
	fApply6->SetTarget(this);
	fEditDns->SetTarget(this);
	Reload();

	BMessage tick(kMsgTick);
	fTicker = new BMessageRunner(BMessenger(this), &tick, 2000000);
}


// The rows are hidden through their layout items after the window is
// shown (docs/STYLE.md).
void
ConnectionsView::AllAttached()
{
	BGroupView::AllAttached();

	// Controls in a filled box paint the window grey by default. The switch
	// sits on the border line, so the box shows through it.
	rgb_color fill = network_box_fill();
	BView* controls[] = { fDeskbar, fMode4, fMode6, fAddress4, fMask4,
		fRouter4, fAddress6, fMask6, fRouter6 };
	for (size_t i = 0; i < sizeof(controls) / sizeof(controls[0]); i++) {
		controls[i]->SetViewColor(fill);
		controls[i]->SetLowColor(fill);
	}

	_ShowSelected();
}


void
ConnectionsView::DetachedFromWindow()
{
	delete fTicker;
	fTicker = NULL;
	BGroupView::DetachedFromWindow();
}


void
ConnectionsView::Reload()
{
	fDeskbar->SetValue(fModule->Deskbar() ? B_CONTROL_ON : B_CONTROL_OFF);
	_FillList();
	_ShowSelected();
}


const InterfaceSetup*
ConnectionsView::_Selected() const
{
	return fModule->Find(fSelected);
}


void
ConnectionsView::_FillList()
{
	while (fList->CountItems() > 0)
		delete fList->RemoveItem((int32)0);

	int32 select = -1;
	for (int32 i = 0; i < fModule->CountInterfaces(); i++) {
		const InterfaceSetup& setup = fModule->InterfaceAt(i);
		ConnectionItem* item = new ConnectionItem(setup.name,
			kind_of(setup.name));
		item->SetStatus(status_of(setup.name, true),
			is_connected(setup.name));
		fList->AddItem(item);
		if (setup.name == fSelected)
			select = i;
		else if (select < 0 && !is_wireless(setup.name))
			select = i;
	}
	if (select >= 0)
		fList->Select(select);
}


static void
show_view(BView* view, bool show)
{
	if (show && view->IsHidden(view))
		view->Show();
	else if (!show && !view->IsHidden(view))
		view->Hide();
}


// Manual shows the fields to type in and Apply; automatic and off show
// what the card has. Hide() and Show() once the window is shown
// (docs/STYLE.md); a BTextControl's layout items follow its view.
void
ConnectionsView::_ShowManualRows(const InterfaceSetup& setup)
{
	if (Window() == NULL)
		return;
	bool manual4 = setup.ipv4.mode == kModeManual;
	bool manual6 = setup.ipv6.mode == kModeManual;
	show_view(fApply4, manual4);
	show_view(fAddressesLabel, !manual6);
	show_view(fAddresses6, !manual6);
	show_view(fAddress6, manual6);
	show_view(fMask6, manual6);
	show_view(fApply6, manual6);

	// the fields move a little; draw the page again, or the old label
	// pieces stay where the fields were
	Invalidate();
}


void
ConnectionsView::_ShowSelected()
{
	ConnectionItem* item = dynamic_cast<ConnectionItem*>(
		fList->ItemAt(fList->CurrentSelection()));
	if (item == NULL)
		return;
	fSelected = item->Name();
	const InterfaceSetup* setup = _Selected();
	if (setup == NULL)
		return;

	fUp->SetLabel(kind_of(setup->name));
	fUp->SetValue(setup->up ? B_CONTROL_ON : B_CONTROL_OFF);
	_SetMode(fMode4Menu, setup->ipv4.mode);
	_SetMode(fMode6Menu, setup->ipv6.mode);
	if (setup->ipv4.mode == kModeManual) {
		fAddress4->SetText(setup->ipv4.address);
		fMask4->SetText(setup->ipv4.mask);
		fRouter4->SetText(setup->ipv4.router);
	}
	if (setup->ipv6.mode == kModeManual) {
		fAddress6->SetText(setup->ipv6.address);
		fMask6->SetText(setup->ipv6.mask);
		fRouter6->SetText(setup->ipv6.router);
	}

	_ShowManualRows(*setup);
	_ShowLive();
	_UpdateEnabled();
}


// Only text: this runs every two seconds and never enables a control.
void
ConnectionsView::_ShowLive()
{
	for (int32 i = 0; i < fList->CountItems(); i++) {
		ConnectionItem* row = dynamic_cast<ConnectionItem*>(fList->ItemAt(i));
		if (row == NULL)
			continue;
		row->SetStatus(status_of(row->Name(), true),
			is_connected(row->Name()));
		fList->InvalidateItem(i);
	}

	const InterfaceSetup* setup = _Selected();
	if (setup == NULL)
		return;
	fStatus->SetText(status_of(setup->name, true));
	fHardware->SetText(hardware_of(setup->name));

	BNetworkInterface interface(setup->name);
	BNetworkInterfaceAddress address;
	if (setup->ipv4.mode != kModeManual) {
		BString text4, mask4, router4;
		int32 index = interface.FindFirstAddress(AF_INET);
		if (setup->ipv4.mode == kModeAuto && index >= 0
			&& interface.GetAddressAt(index, address) == B_OK) {
			text4 = address.Address().ToString();
			mask4 = address.Mask().ToString();
			BNetworkAddress gateway;
			if (interface.GetDefaultGateway(AF_INET, gateway) == B_OK)
				router4 = gateway.ToString();
		}
		fAddress4->SetText(text4);
		fMask4->SetText(mask4);
		fRouter4->SetText(router4);
	}

	// every IPv6 address of the card, one per line
	while (fAddresses6->CountChildren() > 0) {
		BView* child = fAddresses6->ChildAt(0);
		child->RemoveSelf();
		delete child;
	}
	for (int32 i = 0; setup->ipv6.mode == kModeAuto
			&& i < interface.CountAddresses(); i++) {
		if (interface.GetAddressAt(i, address) != B_OK
			|| address.Address().Family() != AF_INET6)
			continue;
		BString text = address.Address().ToString();
		text << "/" << address.Mask().PrefixLength();
		ValueField* field = new ValueField("address");
		field->SetText(text);
		fAddresses6->GroupLayout()->AddView(field);
	}
	if (setup->ipv6.mode != kModeManual) {
		BString router6;
		BNetworkAddress gateway;
		if (setup->ipv6.mode == kModeAuto
			&& interface.GetDefaultGateway(AF_INET6, gateway) == B_OK)
			router6 = gateway.ToString();
		fRouter6->SetText(router6);
	}

	DnsSetup dns = read_dns();
	fServers->SetText(dns.servers);
	fDomain->SetText(dns.domain);
}


// The Settings window grays a locked page. While the lock is closed the
// page leaves the controls alone; the window sends kMsgRefreshPage when
// it opens.
void
ConnectionsView::_UpdateEnabled()
{
	SettingsContext* context = fModule->Context();
	if (context == NULL || !context->AdminChangeAllowed())
		return;
	const InterfaceSetup* setup = _Selected();
	if (setup == NULL)
		return;

	bool manual4 = setup->ipv4.mode == kModeManual;
	bool manual6 = setup->ipv6.mode == kModeManual;
	fAddress4->SetEnabled(manual4);
	fMask4->SetEnabled(manual4);
	fRouter4->SetEnabled(manual4);
	fApply4->SetEnabled(manual4);
	fAddress6->SetEnabled(manual6);
	fMask6->SetEnabled(manual6);
	fRouter6->SetEnabled(manual6);
	fApply6->SetEnabled(manual6);
}


// The first address that is not link-local, as the IPv4 fields show the
// DHCP address.
void
ConnectionsView::_FillManual6(const char* name)
{
	BNetworkInterface interface(name);
	BNetworkInterfaceAddress address;
	for (int32 i = 0; i < interface.CountAddresses(); i++) {
		if (interface.GetAddressAt(i, address) != B_OK
			|| address.Address().Family() != AF_INET6
			|| address.Address().IsEmpty()
			|| address.Address().IsLinkLocal())
			continue;
		fAddress6->SetText(address.Address().ToString());
		fMask6->SetText(address.Mask().ToString());
		break;
	}
	BNetworkAddress gateway;
	if (interface.GetDefaultGateway(AF_INET6, gateway) == B_OK
		&& !gateway.IsEmpty())
		fRouter6->SetText(gateway.ToString());
}


void
ConnectionsView::_ReadFields(int family, AddressSetup& setup)
{
	if (family == AF_INET) {
		setup.address = fAddress4->Text();
		setup.mask = fMask4->Text();
		setup.router = fRouter4->Text();
	} else {
		setup.address = fAddress6->Text();
		setup.mask = fMask6->Text();
		setup.router = fRouter6->Text();
	}
	setup.address.Trim();
	setup.mask.Trim();
	setup.router.Trim();
}


void
ConnectionsView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSelect:
		{
			ConnectionItem* item = dynamic_cast<ConnectionItem*>(
				fList->ItemAt(fList->CurrentSelection()));
			if (item != NULL && is_wireless(item->Name())) {
				// Wi-Fi has its own page.
				if (fModule->Context() != NULL)
					fModule->Context()->OpenPage("wifi");
				return;
			}
			_ShowSelected();
			break;
		}

		case kMsgTick:
			_ShowLive();
			break;

		case kMsgDeskbar:
			fModule->UserEditedDeskbar(fDeskbar->Value() == B_CONTROL_ON);
			fDeskbar->SetValue(fModule->Deskbar() ? B_CONTROL_ON
				: B_CONTROL_OFF);
			break;

		case kMsgRefreshPage:
			Reload();
			break;

		case kMsgUp:
		{
			const InterfaceSetup* current = _Selected();
			if (current == NULL)
				break;
			InterfaceSetup setup = *current;
			setup.up = fUp->Value() == B_CONTROL_ON;
			fModule->UserEdited(setup, true);
			_ShowSelected();
			break;
		}

		case kMsgMode:
		{
			const InterfaceSetup* current = _Selected();
			int32 family;
			int32 mode;
			if (current == NULL || message->FindInt32("family", &family) != B_OK
				|| message->FindInt32("mode", &mode) != B_OK)
				break;
			InterfaceSetup setup = *current;
			AddressSetup& address = family == AF_INET ? setup.ipv4 : setup.ipv6;
			if (mode == kModeManual) {
				// start from what the card has now, nothing is applied yet
				if (family == AF_INET6)
					_FillManual6(setup.name);
				_ReadFields(family, address);
				address.mode = kModeManual;
				fModule->UserEdited(setup, false);
				_SetMode(family == AF_INET ? fMode4Menu : fMode6Menu, mode);
				_ShowManualRows(setup);
				_UpdateEnabled();
				break;
			}
			address = AddressSetup();
			address.mode = mode;
			fModule->UserEdited(setup, true);
			_ShowSelected();
			break;
		}

		case kMsgApply:
		{
			const InterfaceSetup* current = _Selected();
			int32 family;
			if (current == NULL || message->FindInt32("family", &family) != B_OK)
				break;
			InterfaceSetup setup = *current;
			AddressSetup& address = family == AF_INET ? setup.ipv4 : setup.ipv6;
			_ReadFields(family, address);
			address.mode = kModeManual;
			fModule->UserEdited(setup, true);
			_ShowSelected();
			break;
		}

		case kMsgEditDns:
		{
			DnsWindow* window = new DnsWindow(read_dns(), BMessenger(this));
			window->Show();
			break;
		}

		case kMsgDnsChosen:
		{
			DnsSetup dns;
			const char* text;
			message->FindBool("manual", &dns.manual);
			if (message->FindString("servers", &text) == B_OK)
				dns.servers = text;
			if (message->FindString("domain", &text) == B_OK)
				dns.domain = text;
			dns.servers.Trim();
			dns.domain.Trim();
			fModule->UserEditedDns(dns);
			_ShowLive();
			break;
		}

		default:
			BGroupView::MessageReceived(message);
			break;
	}
}


// #pragma mark - ConnectionsModule


ConnectionsModule::ConnectionsModule()
	:
	fContext(NULL),
	fView(NULL),
	fDeskbar(true),
	fDeskbarOpened(true)
{
}


BView*
ConnectionsModule::CreateView(SettingsContext* context, BRect frame)
{
	(void)frame;
	fContext = context;
	fView = new ConnectionsView(this);
	return fView;
}


const InterfaceSetup*
ConnectionsModule::Find(const char* name) const
{
	for (size_t i = 0; i < fCurrent.size(); i++) {
		if (fCurrent[i].name == name)
			return &fCurrent[i];
	}
	return NULL;
}


status_t
ConnectionsModule::LoadSettings(SettingsContext* context)
{
	fContext = context;
	fCurrent.clear();

	BNetworkRoster& roster = BNetworkRoster::Default();
	BNetworkInterface interface;
	uint32 cookie = 0;
	while (roster.GetNextInterface(&cookie, interface) == B_OK) {
		if ((interface.Flags() & IFF_LOOPBACK) != 0)
			continue;
		InterfaceSetup setup;
		setup.name = interface.Name();
		setup.up = (interface.Flags() & IFF_UP) != 0;
		setup.ipv4 = read_address(setup.name, AF_INET);
		setup.ipv6 = read_address(setup.name, AF_INET6);
		fCurrent.push_back(setup);
	}
	fActive = fCurrent;
	fOpened = fCurrent;
	fDns = read_dns();
	fDnsOpened = fDns;
	fDeskbar = BDeskbar().HasItem("NetworkStatus");
	fDeskbarOpened = fDeskbar;

	if (fView != NULL)
		fView->Reload();
	return B_OK;
}


// Applied in UserEdited(), as Display does.
status_t
ConnectionsModule::SaveSettings(SettingsContext* context)
{
	(void)context;
	return B_OK;
}


status_t
ConnectionsModule::_Apply(const InterfaceSetup& from, const InterfaceSetup& to)
{
	status_t status = B_OK;
	if (from.up != to.up)
		status = apply_up(to.name, to.up);
	if (status == B_OK && from.ipv4 != to.ipv4)
		status = apply_address(to.name, AF_INET, to.ipv4);
	if (status == B_OK && from.ipv6 != to.ipv6)
		status = apply_address(to.name, AF_INET6, to.ipv6);
	if (status != B_OK && fContext != NULL) {
		BString line;
		line.SetToFormat("Connections: %s: %s", to.name.String(),
			strerror(status));
		fContext->Log(line.String());
	}
	return status;
}


void
ConnectionsModule::_Changed()
{
	if (fContext != NULL)
		fContext->NotifyChanged(Identifier());
}


void
ConnectionsModule::UserEdited(const InterfaceSetup& setup, bool apply)
{
	for (size_t i = 0; i < fCurrent.size(); i++) {
		if (fCurrent[i].name != setup.name)
			continue;
		fCurrent[i] = setup;
		if (apply && fContext != NULL && fContext->AdminChangeAllowed()
			&& _Apply(fActive[i], setup) == B_OK)
			fActive[i] = setup;
		break;
	}
	_Changed();
}


void
ConnectionsModule::UserEditedDeskbar(bool show)
{
	if (fContext != NULL && fContext->AdminChangeAllowed()
		&& apply_deskbar(show) == B_OK)
		fDeskbar = show;
	_Changed();
}


void
ConnectionsModule::UserEditedDns(const DnsSetup& dns)
{
	fDns = dns;
	if (fContext != NULL && fContext->AdminChangeAllowed()) {
		status_t status = apply_dns(dns);
		if (status != B_OK && fContext != NULL) {
			BString line;
			line.SetToFormat("Connections: resolv.conf: %s", strerror(status));
			fContext->Log(line.String());
		}
	}
	_Changed();
}


status_t
ConnectionsModule::RevertSettings(SettingsContext* context)
{
	fContext = context;
	if (context == NULL || !context->AdminChangeAllowed())
		return B_PERMISSION_DENIED;

	status_t result = B_OK;
	for (size_t i = 0; i < fActive.size() && i < fOpened.size(); i++) {
		if (fActive[i] == fOpened[i])
			continue;
		status_t status = _Apply(fActive[i], fOpened[i]);
		if (status != B_OK)
			result = status;
	}
	if (fDns != fDnsOpened) {
		status_t status = apply_dns(fDnsOpened);
		if (status != B_OK)
			result = status;
	}
	if (fDeskbar != fDeskbarOpened && apply_deskbar(fDeskbarOpened) == B_OK)
		fDeskbar = fDeskbarOpened;
	fCurrent = fOpened;
	fActive = fOpened;
	fDns = fDnsOpened;
	if (fView != NULL)
		fView->Reload();
	_Changed();
	return result;
}


bool
ConnectionsModule::IsModified() const
{
	for (size_t i = 0; i < fCurrent.size() && i < fOpened.size(); i++) {
		if (fCurrent[i] != fOpened[i])
			return true;
	}
	return fDns != fDnsOpened || fDeskbar != fDeskbarOpened;
}


bool
ConnectionsModule::CanApplyDefaults() const
{
	InterfaceSetup factory;
	for (size_t i = 0; i < fCurrent.size(); i++) {
		if (fCurrent[i] != factory)
			return true;
	}
	return fDns.manual;
}


// Automatic IPv4 and IPv6, every card on, DNS from the network.
status_t
ConnectionsModule::ApplyDefaults(SettingsContext* context)
{
	fContext = context;
	if (context == NULL || !context->AdminChangeAllowed())
		return B_PERMISSION_DENIED;

	status_t result = B_OK;
	for (size_t i = 0; i < fCurrent.size(); i++) {
		InterfaceSetup factory;
		factory.name = fCurrent[i].name;
		fCurrent[i] = factory;
		if (fActive[i] == factory)
			continue;
		status_t status = _Apply(fActive[i], factory);
		if (status != B_OK)
			result = status;
		fActive[i] = factory;
	}
	if (fDns.manual) {
		DnsSetup network;
		status_t status = apply_dns(network);
		if (status != B_OK)
			result = status;
		fDns = network;
	}
	if (fView != NULL)
		fView->Reload();
	_Changed();
	return result;
}


void
register_connections_page(SettingsManager& manager)
{
	manager.Add(new ConnectionsModule());
}
