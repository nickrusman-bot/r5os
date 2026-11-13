/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Discovery, under Network: this computer's name and its .local name,
 * whether other devices may find it, what it shares, and the devices
 * r5-mdns can find. Like Display, a change is applied at once after
 * AdminChangeAllowed(). Rename… is the same sethostname() as on
 * Information. The services are the Network program's own service add-ons
 * (Network Settings), with their list items and views, so every service
 * add-on shows up here.
 */


#include "DiscoveryModule.h"

#include "NetworkPage.h"
#include "SettingsFont.h"
#include "SettingsManager.h"
#include "SettingsMessages.h"
#include "SettingsModule.h"

#include <Alert.h>
#include <Button.h>
#include <CheckBox.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <GridView.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <NetworkSettings.h>
#include <NetworkSettingsAddOn.h>
#include <ObjectList.h>
#include <Path.h>
#include <PathFinder.h>
#include <ScrollView.h>
#include <String.h>
#include <StringList.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>
#include <Window.h>

#include <r5_mdns.h>

#include <map>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <vector>

// kMsgSettingsItemUpdated and gNetworkWindow, which the add-on classes use
#include "NetworkWindow.h"
#include "ServiceView.h"


using BNetworkKit::BNetworkSettings;
using BNetworkKit::BNetworkSettingsAddOn;
using BNetworkKit::BNetworkSettingsItem;
using BNetworkKit::BNetworkSettingsListener;


// The add-ons tell the window of the Network program that an item
// changed. In Settings that is the Discovery page.
BMessenger gNetworkWindow;


static const char* kModuleId = "discovery";
static const char* kDiscoveryFile = "network/discovery";

static const uint32 kMsgRename = 'dsrn';
static const uint32 kMsgApplyName = 'dsan';
static const uint32 kMsgDiscoverable = 'dsdv';
static const uint32 kMsgServiceSelected = 'dssl';
static const uint32 kMsgRefresh = 'dsrf';
static const uint32 kMsgFound = 'dsfd';


struct DiscoverySetup {
	BString		name;
	bool		discoverable;

	DiscoverySetup()
		:
		discoverable(true)
	{
	}

	bool operator==(const DiscoverySetup& other) const
	{
		return name == other.name && discoverable == other.discoverable;
	}

	bool operator!=(const DiscoverySetup& other) const
	{
		return !(*this == other);
	}
};


// #pragma mark - settings


static BString
host_name()
{
	char host[64];
	memset(host, 0, sizeof(host));
	if (gethostname(host, sizeof(host) - 1) != 0 || host[0] == '\0')
		return BString("This computer");
	return BString(host);
}


// The same check as Rename… on Information.
static bool
name_ok(const char* name)
{
	if (name == NULL || name[0] == '\0')
		return false;
	size_t length = strlen(name);
	if (length > 63 || name[0] == '-' || name[length - 1] == '-')
		return false;
	for (size_t i = 0; i < length; i++) {
		char c = name[i];
		bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
		bool digit = c >= '0' && c <= '9';
		if (!letter && !digit && c != '-')
			return false;
	}
	return true;
}


static status_t
discovery_path(BPath& path)
{
	status_t status = find_directory(B_SYSTEM_SETTINGS_DIRECTORY, &path);
	if (status == B_OK)
		status = path.Append(kDiscoveryFile);
	return status;
}


// r5-mdns reads "discoverable true" or "discoverable false"; no file is on.
static bool
read_discoverable()
{
	BPath path;
	if (discovery_path(path) != B_OK)
		return true;
	FILE* file = fopen(path.Path(), "r");
	if (file == NULL)
		return true;
	bool discoverable = true;
	char line[128];
	while (fgets(line, sizeof(line), file) != NULL) {
		char key[64];
		char value[32];
		if (line[0] != '#' && sscanf(line, "%63s %31s", key, value) == 2
			&& strcmp(key, "discoverable") == 0)
			discoverable = strcmp(value, "false") != 0;
	}
	fclose(file);
	return discoverable;
}


static status_t
apply_discoverable(bool discoverable)
{
	BPath path;
	status_t status = discovery_path(path);
	if (status != B_OK)
		return status;
	BFile file(path.Path(), B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);
	status = file.InitCheck();
	if (status != B_OK)
		return status;
	BString content("# Let other devices find this computer (Settings)\n");
	content << "discoverable " << (discoverable ? "true" : "false") << "\n";
	ssize_t bytes = file.Write(content.String(), content.Length());
	if (bytes < 0)
		return bytes;
	return bytes == content.Length() ? B_OK : B_ERROR;
}


// #pragma mark - views


// Information's Rename window.
class DiscoveryRenameWindow : public BWindow {
public:
								DiscoveryRenameWindow(const char* name,
									const BMessenger& target);
	virtual	void				MessageReceived(BMessage* message);

private:
			BTextControl*		fName;
			BStringView*		fError;
			BMessenger			fTarget;
};


DiscoveryRenameWindow::DiscoveryRenameWindow(const char* name,
	const BMessenger& target)
	:
	BWindow(BRect(0, 0, 360, 120), "Rename", B_TITLED_WINDOW,
		B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fTarget(target)
{
	fName = new BTextControl("name", "Name:", name, NULL);
	fError = new BStringView("error", "");
	BButton* cancel = new BButton("cancel", "Cancel",
		new BMessage(B_QUIT_REQUESTED));
	BButton* rename = new BButton("rename", "Rename",
		new BMessage(kMsgApplyName));
	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(fName)
		.Add(fError)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.AddGlue()
			.Add(cancel)
			.Add(rename)
			.End();
	fName->SetTarget(this);
	rename->SetTarget(this);
	cancel->SetTarget(this);
	SetDefaultButton(rename);
	fName->MakeFocus(true);
	CenterOnScreen();
}


void
DiscoveryRenameWindow::MessageReceived(BMessage* message)
{
	if (message->what != kMsgApplyName) {
		BWindow::MessageReceived(message);
		return;
	}
	const char* name = fName->Text();
	if (!name_ok(name)) {
		fError->SetText("Use letters, digits and -, up to 63, "
			"not starting or ending with -.");
		return;
	}
	BMessage apply(kMsgApplyName);
	apply.AddString("name", name);
	fTarget.SendMessage(&apply);
	PostMessage(B_QUIT_REQUESTED);
}


// A found device: the name in bold, what it is under it.
class DeviceItem : public BListItem {
public:
								DeviceItem(const char* name,
									const char* detail);

	virtual	void				DrawItem(BView* owner, BRect frame,
									bool complete);
	virtual	void				Update(BView* owner, const BFont* font);

private:
			BString				fName;
			BString				fDetail;
};


DeviceItem::DeviceItem(const char* name, const char* detail)
	:
	fName(name),
	fDetail(detail)
{
}


void
DeviceItem::Update(BView* owner, const BFont* font)
{
	BListItem::Update(owner, font);
	font_height height;
	font->GetHeight(&height);
	float line = ceilf(height.ascent + height.descent + height.leading);
	SetHeight(line * 2 + network_unit() * 12);
}


void
DeviceItem::DrawItem(BView* owner, BRect frame, bool complete)
{
	(void)complete;
	owner->SetLowColor(ui_color(B_LIST_BACKGROUND_COLOR));
	owner->FillRect(frame, B_SOLID_LOW);

	float u = network_unit();
	font_height height;
	be_plain_font->GetHeight(&height);
	float line = ceilf(height.ascent + height.descent + height.leading);
	float x = frame.left + u * 8;
	float top = frame.top + u * 6;

	owner->SetFont(be_bold_font);
	owner->SetHighColor(ui_color(B_LIST_ITEM_TEXT_COLOR));
	owner->DrawString(fName.String(), BPoint(x, top + height.ascent));

	BFont small(be_plain_font);
	small.SetSize(be_plain_font->Size() * 0.9f);
	owner->SetFont(&small);
	owner->SetHighColor(network_quiet_color(140));
	BString detail(fDetail);
	owner->TruncateString(&detail, B_TRUNCATE_END, frame.right - x - u * 4);
	owner->DrawString(detail.String(), BPoint(x, top + line + height.ascent));
	owner->SetFont(be_plain_font);
}


class DiscoveryModule;


class DiscoveryView : public BGroupView {
public:
								DiscoveryView(DiscoveryModule* module);
	virtual						~DiscoveryView();

	virtual	void				AttachedToWindow();
	virtual	void				AllAttached();
	virtual	void				DetachedFromWindow();
	virtual	void				MessageReceived(BMessage* message);

			void				Reload();

private:
			BBox*				_Box(const char* title, BGroupView*& group);
			void				_ShowService();
			void				_Fill(BView* view);
			void				_SettingsUpdated(uint32 type);
			void				_Search();
	static	int32				_SearchThread(void* data);

			DiscoveryModule*	fModule;
			BStringView*		fName;
			BStringView*		fLocalName;
			LinkControl*		fRename;
			BCheckBox*			fDiscoverable;
			BListView*			fServices;
			BGroupView*			fServiceShell;
			BListView*			fDevices;
			BButton*			fRefresh;
			thread_id			fSearcher;
};


class DiscoveryModule : public SettingsModule {
public:
								DiscoveryModule();
	virtual						~DiscoveryModule();

	virtual	const char*			Identifier() const { return kModuleId; }
	virtual	const char*			Name() const { return "Discovery"; }
	virtual	const char*			Category() const { return "network"; }
	virtual	const char*			Description() const
									{ return "Find devices and services on "
										"this network"; }
	virtual	const char*			Keywords() const
									{ return "network bonjour mdns local name "
										"hostname ssh ftp telnet share "
										"printer airplay"; }
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
			const DiscoverySetup& Current() const { return fCurrent; }
			void				UserEdited(const DiscoverySetup& setup);
			void				ServicesChanged();

			BNetworkSettings&	Settings() { return fSettings; }
			int32				CountServices() const
									{ return fServices.size(); }
			BNetworkSettingsItem* ServiceAt(int32 index) const
									{ return fServices[index]; }
			BNetworkSettingsItem* ServiceFor(BListItem* item) const;

private:
			void				_LoadAddOns();
			status_t			_Apply(const DiscoverySetup& to);
			bool				_ServiceOn(const char* name);

			SettingsContext*	fContext;
			DiscoveryView*		fView;
			DiscoverySetup		fCurrent;
			DiscoverySetup		fOpened;

			BNetworkSettings	fSettings;
			BObjectList<BNetworkSettingsAddOn> fAddOns;
			std::vector<BNetworkSettingsItem*> fServices;
			bool				fLoaded;
};


// #pragma mark - DiscoveryView


DiscoveryView::DiscoveryView(DiscoveryModule* module)
	:
	BGroupView("discovery", B_HORIZONTAL),
	fModule(module),
	fSearcher(-1)
{
	float u = network_unit();
	BGroupView* group;

	// This computer
	BBox* computerBox = _Box("This computer", group);
	BStringView* nameLabel = new BStringView("name label", "Name:");
	BStringView* localLabel = new BStringView("local label",
		"On this network:");
	nameLabel->SetAlignment(B_ALIGN_RIGHT);
	localLabel->SetAlignment(B_ALIGN_RIGHT);
	fName = new BStringView("name", "");
	fName->SetFont(be_bold_font);
	fRename = new LinkControl("rename", "Rename…", new BMessage(kMsgRename));
	fLocalName = new BStringView("local name", "");
	fLocalName->SetFont(be_fixed_font);
	fDiscoverable = new BCheckBox("discoverable",
		"Let other devices find this computer",
		new BMessage(kMsgDiscoverable));
	BGridView* grid = new BGridView(u * 12, u * 6);
	grid->SetViewColor(network_box_fill());
	BLayoutBuilder::Grid<>(grid)
		.Add(nameLabel, 0, 0)
		.AddGroup(B_HORIZONTAL, u * 10, 1, 0)
			.Add(fName)
			.Add(fRename)
			.AddGlue()
			.End()
		.Add(localLabel, 0, 1)
		.Add(fLocalName, 1, 1);
	// extra space is glue outside the form (docs/STYLE.md)
	BLayoutBuilder::Group<>(group)
		.AddGroup(B_HORIZONTAL, 0)
			.Add(grid, 0)
			.AddGlue(1)
			.End()
		.Add(fDiscoverable);

	// Shared from this computer: the Network program's service add-ons
	BBox* sharedBox = _Box("Shared from this computer", group);
	fServices = new BListView("services");
	fServices->SetSelectionMessage(new BMessage(kMsgServiceSelected));
	BScrollView* servicesScroll = new BScrollView("services scroll",
		fServices, 0, false, false);
	// the list on the left, the chosen service's text and button beside it,
	// as in the Network program
	servicesScroll->SetExplicitMinSize(BSize(u * 150, u * 170));
	servicesScroll->SetExplicitMaxSize(BSize(u * 150, B_SIZE_UNLIMITED));
	fServiceShell = new BGroupView(B_VERTICAL, 0);
	BLayoutBuilder::Group<>(group)
		.AddGroup(B_HORIZONTAL, u * 10, 0)
			.Add(servicesScroll, 0)
			.Add(fServiceShell, 1)
			.End()
		.AddGlue(1)
		.Add(network_hint("An item that is on is also announced, so other "
			"devices list it."), 0);

	// Find devices
	BBox* findBox = _Box("Find devices (Bonjour)", group);
	fDevices = new BListView("devices");
	BScrollView* scroll = new BScrollView("devices scroll", fDevices, 0,
		false, true);
	fRefresh = new BButton("refresh", "Refresh", new BMessage(kMsgRefresh));
	BLayoutBuilder::Group<>(group)
		.Add(scroll)
		.AddGroup(B_HORIZONTAL, u * 10)
			.Add(network_hint("Programs can reach a .local name. Printers "
				"and speakers in this list are also offered by Printers and "
				"AirPlay."), 1)
			.Add(fRefresh, 0)
			.End();

	// two columns as wide as each other
	BGroupView* left = new BGroupView(B_VERTICAL, u * 12);
	BLayoutBuilder::Group<>(left)
		.Add(computerBox, 0)
		.Add(sharedBox, 1);
	// This computer gets no extra height (weight 0); Shared takes the rest.

	// Find devices takes about a third of the width.
	BSize wide(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED);
	left->SetExplicitMinSize(BSize(u * 300, B_SIZE_UNSET));
	left->SetExplicitPreferredSize(BSize(u * 300, B_SIZE_UNSET));
	left->SetExplicitMaxSize(wide);
	findBox->SetExplicitMinSize(BSize(u * 150, B_SIZE_UNSET));
	findBox->SetExplicitPreferredSize(BSize(u * 150, B_SIZE_UNSET));
	findBox->SetExplicitMaxSize(wide);

	BLayoutBuilder::Group<>(this, B_HORIZONTAL, u * 12)
		.Add(left, 2)
		.Add(findBox, 1);
}


DiscoveryView::~DiscoveryView()
{
	if (fSearcher >= 0) {
		status_t result;
		wait_for_thread(fSearcher, &result);
	}
	// The services' list items and views belong to the add-ons.
	while (fServiceShell->CountChildren() > 0)
		fServiceShell->ChildAt(0)->RemoveSelf();
	fServices->MakeEmpty();
	while (fDevices->CountItems() > 0)
		delete fDevices->RemoveItem((int32)0);
	if (fModule != NULL)
		fModule->ClearView();
}


BBox*
DiscoveryView::_Box(const char* title, BGroupView*& group)
{
	float u = network_unit();
	BBox* box = new FilledBox(title);
	box->SetLabel(new BoxTitle(title));
	group = new BGroupView(B_VERTICAL, u * 6);
	group->SetViewColor(network_box_fill());
	group->GroupLayout()->SetInsets(u * 12, u * 8, u * 12, u * 12);
	box->AddChild(group);
	return box;
}


void
DiscoveryView::AttachedToWindow()
{
	BGroupView::AttachedToWindow();
	fRename->SetTarget(this);
	fDiscoverable->SetTarget(this);
	fServices->SetTarget(this);
	fRefresh->SetTarget(this);

	gNetworkWindow = BMessenger(this);
	fModule->Settings().StartMonitoring(BMessenger(this));
	for (int32 i = 0; i < fModule->CountServices(); i++)
		fServices->AddItem(fModule->ServiceAt(i)->ListItem());
	if (fServices->CountItems() > 0)
		fServices->Select(0);

	Reload();
	_Search();
}


void
DiscoveryView::AllAttached()
{
	BGroupView::AllAttached();

	// Controls on a filled box paint the window grey by default.
	rgb_color fill = network_box_fill();
	fDiscoverable->SetViewColor(fill);
	fDiscoverable->SetLowColor(fill);
	fRename->SetViewColor(fill);
	fRename->SetLowColor(fill);
}


void
DiscoveryView::DetachedFromWindow()
{
	fModule->Settings().StopMonitoring(BMessenger(this));
	gNetworkWindow = BMessenger();
	BGroupView::DetachedFromWindow();
}


void
DiscoveryView::Reload()
{
	const DiscoverySetup& setup = fModule->Current();
	fName->SetText(setup.name);
	BString local(setup.name);
	local << ".local";
	fLocalName->SetText(local);
	fDiscoverable->SetValue(setup.discoverable);
	_SettingsUpdated(BNetworkSettings::kMsgServiceSettingsUpdated);
}


// NetworkWindow::_SelectItem() of the Network program.
void
DiscoveryView::_ShowService()
{
	while (fServiceShell->CountChildren() > 0)
		fServiceShell->ChildAt(0)->RemoveSelf();
	BNetworkSettingsItem* item = fModule->ServiceFor(
		fServices->ItemAt(fServices->CurrentSelection()));
	if (item != NULL && item->View() != NULL) {
		fServiceShell->GroupLayout()->AddView(item->View());
		_Fill(item->View());
	}
}


// The add-on's view uses the window grey; on this filled box it takes the
// fill, as the other controls do.
void
DiscoveryView::_Fill(BView* view)
{
	rgb_color fill = network_box_fill();
	view->SetViewColor(fill);
	view->SetLowColor(fill);
	view->Invalidate();
	for (int32 i = 0; i < view->CountChildren(); i++)
		_Fill(view->ChildAt(i));
}


// NetworkWindow::_BroadcastSettingsUpdate() of the Network program.
void
DiscoveryView::_SettingsUpdated(uint32 type)
{
	for (int32 i = 0; i < fServices->CountItems(); i++) {
		BNetworkSettingsListener* listener
			= dynamic_cast<BNetworkSettingsListener*>(fServices->ItemAt(i));
		if (listener != NULL)
			listener->SettingsUpdated(type);
	}
	for (int32 i = 0; i < fModule->CountServices(); i++)
		fModule->ServiceAt(i)->SettingsUpdated(type);
	fServices->Invalidate();
}


// Browsing waits for answers, so it runs in its own thread and sends what
// it finds back to the view.
void
DiscoveryView::_Search()
{
	if (fSearcher >= 0)
		return;
	fRefresh->SetEnabled(false);
	BMessenger* target = new BMessenger(this);
	fSearcher = spawn_thread(_SearchThread, "discovery search",
		B_NORMAL_PRIORITY, target);
	if (fSearcher < 0) {
		delete target;
		fRefresh->SetEnabled(true);
		return;
	}
	resume_thread(fSearcher);
}


int32
DiscoveryView::_SearchThread(void* data)
{
	BMessenger* target = static_cast<BMessenger*>(data);
	struct {
		const char*	type;
		const char*	kind;
	} types[] = {
		{ "_ipp._tcp.local", "Printer" },
		{ "_airplay._tcp.local", "AirPlay speaker" },
		{ "_ssh._tcp.local", "SSH" },
		{ "_smb._tcp.local", "File sharing" }
	};

	BMessage found(kMsgFound);
	r5_mdns_service services[R5_MDNS_MAX_SERVICES];
	for (size_t t = 0; t < sizeof(types) / sizeof(types[0]); t++) {
		int count = r5_mdns_browse(types[t].type, services,
			R5_MDNS_MAX_SERVICES, 800);
		for (int i = 0; i < count; i++) {
			BString detail(types[t].kind);
			if (services[i].host[0] != '\0')
				detail << " · " << services[i].host;
			found.AddString("name", services[i].name);
			found.AddString("detail", detail);
		}
	}
	target->SendMessage(&found);
	delete target;
	return B_OK;
}


void
DiscoveryView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgRename:
		{
			DiscoveryRenameWindow* window = new DiscoveryRenameWindow(
				fModule->Current().name, BMessenger(this));
			window->Show();
			break;
		}

		case kMsgApplyName:
		{
			const char* name;
			if (message->FindString("name", &name) != B_OK)
				break;
			DiscoverySetup setup = fModule->Current();
			setup.name = name;
			fModule->UserEdited(setup);
			Reload();
			break;
		}

		case kMsgDiscoverable:
		{
			DiscoverySetup setup = fModule->Current();
			setup.discoverable = fDiscoverable->Value() == B_CONTROL_ON;
			fModule->UserEdited(setup);
			Reload();
			break;
		}

		case kMsgServiceSelected:
			_ShowService();
			break;

		case BNetworkSettings::kMsgInterfaceSettingsUpdated:
		case BNetworkSettings::kMsgNetworkSettingsUpdated:
		case BNetworkSettings::kMsgServiceSettingsUpdated:
			_SettingsUpdated(message->what);
			fModule->ServicesChanged();
			break;

		case kMsgSettingsItemUpdated:
			fServices->Invalidate();
			fModule->ServicesChanged();
			break;

		case kMsgRefresh:
			_Search();
			break;

		case kMsgFound:
		{
			if (fSearcher >= 0) {
				status_t result;
				wait_for_thread(fSearcher, &result);
				fSearcher = -1;
			}
			fRefresh->SetEnabled(true);
			while (fDevices->CountItems() > 0)
				delete fDevices->RemoveItem((int32)0);
			const char* name;
			const char* detail;
			for (int32 i = 0; message->FindString("name", i, &name) == B_OK
					&& message->FindString("detail", i, &detail) == B_OK;
					i++) {
				fDevices->AddItem(new DeviceItem(name, detail));
			}
			if (fDevices->IsEmpty()) {
				fDevices->AddItem(new DeviceItem("Nothing found",
					"No device answered on this network."));
			}
			break;
		}

		case kMsgRefreshPage:
			Reload();
			break;

		default:
			BGroupView::MessageReceived(message);
			break;
	}
}


// #pragma mark - DiscoveryModule


DiscoveryModule::DiscoveryModule()
	:
	fContext(NULL),
	fView(NULL),
	fAddOns(10),
	fLoaded(false)
{
}


DiscoveryModule::~DiscoveryModule()
{
	for (size_t i = 0; i < fServices.size(); i++)
		delete fServices[i];
	for (int32 i = 0; i < fAddOns.CountItems(); i++) {
		BNetworkSettingsAddOn* addOn = fAddOns.ItemAt(i);
		image_id image = addOn->Image();
		delete addOn;
		unload_add_on(image);
	}
}


// NetworkWindow::_ScanAddOns() of the Network program, for the service
// items only.
void
DiscoveryModule::_LoadAddOns()
{
	if (fLoaded)
		return;
	fLoaded = true;

	BStringList paths;
	BPathFinder::FindPaths(B_FIND_PATH_ADD_ONS_DIRECTORY, "Network Settings",
		paths);

	// one add-on per name, the first one found
	std::map<BString, BPath> addOns;
	for (int32 i = 0; i < paths.CountStrings(); i++) {
		BDirectory directory(paths.StringAt(i));
		BEntry entry;
		while (directory.GetNextEntry(&entry) == B_OK) {
			BPath path;
			if (entry.GetPath(&path) == B_OK
				&& addOns.find(path.Leaf()) == addOns.end())
				addOns[path.Leaf()] = path;
		}
	}

	std::map<BString, BPath>::const_iterator iterator = addOns.begin();
	for (; iterator != addOns.end(); iterator++) {
		image_id image = load_add_on(iterator->second.Path());
		if (image < 0)
			continue;

		BNetworkSettingsAddOn* (*instantiate)(image_id image,
			BNetworkSettings& settings);
		if (get_image_symbol(image, "instantiate_network_settings_add_on",
				B_SYMBOL_TYPE_TEXT, (void**)&instantiate) != B_OK) {
			unload_add_on(image);
			continue;
		}
		BNetworkSettingsAddOn* addOn = instantiate(image, fSettings);
		if (addOn == NULL) {
			unload_add_on(image);
			continue;
		}
		fAddOns.AddItem(addOn);

		// DNS and the host name call themselves services too; a service
		// here is one with an Enable button (ServiceView): FTP, SSH, Telnet.
		uint32 cookie = 0;
		while (BNetworkSettingsItem* item = addOn->CreateNextItem(cookie)) {
			if (item->Type() == BNetworkKit::B_NETWORK_SETTINGS_TYPE_SERVICE
				&& dynamic_cast<ServiceView*>(item->View()) != NULL)
				fServices.push_back(item);
			else
				delete item;
		}
	}
}


BView*
DiscoveryModule::CreateView(SettingsContext* context, BRect frame)
{
	(void)frame;
	fContext = context;
	_LoadAddOns();
	fView = new DiscoveryView(this);
	return fView;
}


BNetworkSettingsItem*
DiscoveryModule::ServiceFor(BListItem* item) const
{
	for (size_t i = 0; item != NULL && i < fServices.size(); i++) {
		if (fServices[i]->ListItem() == item)
			return fServices[i];
	}
	return NULL;
}


status_t
DiscoveryModule::LoadSettings(SettingsContext* context)
{
	fContext = context;
	fCurrent.name = host_name();
	fCurrent.discoverable = read_discoverable();
	fOpened = fCurrent;
	if (fView != NULL)
		fView->Reload();
	return B_OK;
}


// Applied in UserEdited(), as Display does. The services apply
// themselves through their Enable button.
status_t
DiscoveryModule::SaveSettings(SettingsContext* context)
{
	(void)context;
	return B_OK;
}


status_t
DiscoveryModule::_Apply(const DiscoverySetup& to)
{
	status_t result = B_OK;
	if (to.name != fCurrent.name) {
		if (sethostname(to.name, to.name.Length()) == 0)
			fCurrent.name = to.name;
		else {
			result = B_ERROR;
			BAlert* alert = new BAlert("Rename",
				"The name could not be saved.", "OK", NULL, NULL,
				B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go(NULL);
		}
	}
	if (to.discoverable != fCurrent.discoverable) {
		status_t status = apply_discoverable(to.discoverable);
		if (status == B_OK)
			fCurrent.discoverable = to.discoverable;
		else
			result = status;
	}
	if (result != B_OK && fContext != NULL) {
		BString line;
		line.SetToFormat("Discovery: %s", strerror(result));
		fContext->Log(line.String());
	}
	return result;
}


void
DiscoveryModule::UserEdited(const DiscoverySetup& setup)
{
	if (fContext != NULL && fContext->AdminChangeAllowed())
		_Apply(setup);
	ServicesChanged();
}


void
DiscoveryModule::ServicesChanged()
{
	if (fContext != NULL)
		fContext->NotifyChanged(Identifier());
}


status_t
DiscoveryModule::RevertSettings(SettingsContext* context)
{
	fContext = context;
	if (context == NULL || !context->AdminChangeAllowed())
		return B_PERMISSION_DENIED;
	status_t status = _Apply(fOpened);
	for (size_t i = 0; i < fServices.size(); i++) {
		if (fServices[i]->IsRevertable())
			fServices[i]->Revert();
	}
	if (fView != NULL)
		fView->Reload();
	context->NotifyChanged(Identifier());
	return status;
}


bool
DiscoveryModule::IsModified() const
{
	if (fCurrent != fOpened)
		return true;
	for (size_t i = 0; i < fServices.size(); i++) {
		if (fServices[i]->IsRevertable())
			return true;
	}
	return false;
}


bool
DiscoveryModule::_ServiceOn(const char* name)
{
	BMessage service;
	return fSettings.GetService(name, service) == B_OK;
}


bool
DiscoveryModule::CanApplyDefaults() const
{
	DiscoveryModule* self = const_cast<DiscoveryModule*>(this);
	return !fCurrent.discoverable || self->_ServiceOn("ssh")
		|| self->_ServiceOn("ftp");
}


// Found by others, SSH and FTP off. The name stays.
status_t
DiscoveryModule::ApplyDefaults(SettingsContext* context)
{
	fContext = context;
	if (context == NULL || !context->AdminChangeAllowed())
		return B_PERMISSION_DENIED;
	DiscoverySetup factory;
	factory.name = fCurrent.name;
	status_t status = _Apply(factory);
	// ServiceView::Disable() of the Network program
	if (_ServiceOn("ssh"))
		fSettings.RemoveService("ssh");
	if (_ServiceOn("ftp"))
		fSettings.RemoveService("ftp");
	if (fView != NULL)
		fView->Reload();
	context->NotifyChanged(Identifier());
	return status;
}


void
register_discovery_page(SettingsManager& manager)
{
	manager.Add(new DiscoveryModule());
}
