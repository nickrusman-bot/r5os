/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Discovery, under Network: this computer's name and its .local name,
 * whether other devices may find it, what it shares (SSH, FTP), and the
 * devices r5-mdns can find. Like Display, a change is applied at once
 * after AdminChangeAllowed(). Rename… is the same sethostname() as on
 * Information. The services are written the way the Network program's
 * SSH and FTP add-ons do it.
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
#include <File.h>
#include <FindDirectory.h>
#include <GridView.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <NetworkSettings.h>
#include <Path.h>
#include <ScrollView.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>
#include <Window.h>

#include <r5_mdns.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>


using BNetworkKit::BNetworkServiceSettings;
using BNetworkKit::BNetworkSettings;


static const char* kModuleId = "discovery";
static const char* kDiscoveryFile = "network/discovery";

static const uint32 kMsgRename = 'dsrn';
static const uint32 kMsgApplyName = 'dsan';
static const uint32 kMsgDiscoverable = 'dsdv';
static const uint32 kMsgService = 'dssv';
static const uint32 kMsgRefresh = 'dsrf';
static const uint32 kMsgFound = 'dsfd';


struct DiscoverySetup {
	BString		name;
	bool		discoverable;
	bool		ssh;
	bool		ftp;

	DiscoverySetup()
		:
		discoverable(true),
		ssh(false),
		ftp(false)
	{
	}

	bool operator==(const DiscoverySetup& other) const
	{
		return name == other.name && discoverable == other.discoverable
			&& ssh == other.ssh && ftp == other.ftp;
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


// ServiceView::IsEnabled() of the Network program.
static bool
service_running(const char* name)
{
	BNetworkSettings settings;
	BMessage service;
	if (settings.GetService(name, service) != B_OK)
		return false;
	return BNetworkServiceSettings(service).IsRunning();
}


// SSHServiceView::Enable() and ServiceView::Enable() / Disable() of the
// Network program. The image has the sshd login already.
static status_t
apply_service(const char* name, bool enabled)
{
	BNetworkSettings settings;
	if (!enabled)
		return settings.RemoveService(name);

	BNetworkServiceSettings service;
	service.SetName(name);
	if (strcmp(name, "ssh") == 0) {
		service.SetStandAlone(true);
		service.AddArgument("/bin/sshd");
		service.AddArgument("-D");
	} else
		service.AddArgument("ftpd");

	BMessage message;
	status_t status = service.GetMessage(message);
	if (status != B_OK)
		return status;
	return settings.AddService(message);
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


DiscoveryRenameWindow::DiscoveryRenameWindow(const char* name, const BMessenger& target)
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
	virtual	void				MessageReceived(BMessage* message);

			void				Reload();

private:
			BBox*				_Box(const char* title, BGroupView*& group);
			BStringView*		_Hint(const char* text);
			void				_Search();
	static	int32				_SearchThread(void* data);

			DiscoveryModule*	fModule;
			BStringView*		fName;
			BStringView*		fLocalName;
			LinkControl*		fRename;
			BCheckBox*			fDiscoverable;
			BCheckBox*			fSsh;
			BCheckBox*			fFtp;
			BListView*			fDevices;
			BButton*			fRefresh;
			thread_id			fSearcher;
};


class DiscoveryModule : public SettingsModule {
public:
								DiscoveryModule();

	virtual	const char*			Identifier() const { return kModuleId; }
	virtual	const char*			Name() const { return "Discovery"; }
	virtual	const char*			Category() const { return "network"; }
	virtual	const char*			Description() const
									{ return "Find devices and services on "
										"this network"; }
	virtual	const char*			Keywords() const
									{ return "network bonjour mdns local name "
										"hostname ssh ftp share printer "
										"airplay"; }
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

private:
			status_t			_Apply(const DiscoverySetup& to);

			SettingsContext*	fContext;
			DiscoveryView*		fView;
			DiscoverySetup		fCurrent;
			DiscoverySetup		fOpened;
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
	BLayoutBuilder::Group<>(group)
		.Add(grid)
		.Add(fDiscoverable)
		.Add(_Hint("Rename… is the same name as on Information."));

	// Shared from this computer
	BBox* sharedBox = _Box("Shared from this computer", group);
	fSsh = new BCheckBox("ssh", "Remote login (SSH)",
		new BMessage(kMsgService));
	fFtp = new BCheckBox("ftp", "File transfer (FTP)",
		new BMessage(kMsgService));
	BLayoutBuilder::Group<>(group)
		.Add(fSsh)
		.Add(_Hint("Port 22 · password or key"))
		.Add(fFtp)
		.Add(_Hint("Not encrypted"))
		.Add(_Hint("An item that is on is also announced, so other devices "
			"list it."))
		.AddGlue();

	// Find devices
	BBox* findBox = _Box("Find devices (Bonjour)", group);
	fDevices = new BListView("devices");
	BScrollView* scroll = new BScrollView("devices scroll", fDevices, 0,
		false, true);
	fRefresh = new BButton("refresh", "Refresh", new BMessage(kMsgRefresh));
	BLayoutBuilder::Group<>(group)
		.Add(scroll)
		.AddGroup(B_HORIZONTAL)
			.Add(_Hint("Programs can reach a .local name. Printers and "
				"speakers in this list are also offered by Printers and "
				"AirPlay."))
			.Add(fRefresh)
			.End();

	BLayoutBuilder::Group<>(this, B_HORIZONTAL, u * 12)
		.AddGroup(B_VERTICAL, u * 12, 1)
			.Add(computerBox)
			.Add(sharedBox, 1)
			.End()
		.Add(findBox, 1);
}


DiscoveryView::~DiscoveryView()
{
	if (fSearcher >= 0) {
		status_t result;
		wait_for_thread(fSearcher, &result);
	}
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


BStringView*
DiscoveryView::_Hint(const char* text)
{
	BStringView* hint = new BStringView("hint", text);
	BFont font(be_plain_font);
	font.SetSize(be_plain_font->Size() * 0.9f);
	hint->SetFont(&font);
	hint->SetHighColor(network_quiet_color(140));
	hint->SetExplicitMinSize(BSize(network_unit() * 40, B_SIZE_UNSET));
	return hint;
}


void
DiscoveryView::AttachedToWindow()
{
	BGroupView::AttachedToWindow();
	fRename->SetTarget(this);
	fDiscoverable->SetTarget(this);
	fSsh->SetTarget(this);
	fFtp->SetTarget(this);
	fRefresh->SetTarget(this);
	Reload();
	_Search();
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
	fSsh->SetValue(setup.ssh);
	fFtp->SetValue(setup.ftp);
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
		case kMsgService:
		{
			DiscoverySetup setup = fModule->Current();
			setup.discoverable = fDiscoverable->Value() == B_CONTROL_ON;
			setup.ssh = fSsh->Value() == B_CONTROL_ON;
			setup.ftp = fFtp->Value() == B_CONTROL_ON;
			fModule->UserEdited(setup);
			Reload();
			break;
		}

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
			if (fDevices->IsEmpty())
				fDevices->AddItem(new DeviceItem("Nothing found",
					"No device answered on this network."));
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
	fView(NULL)
{
}


BView*
DiscoveryModule::CreateView(SettingsContext* context, BRect frame)
{
	(void)frame;
	fContext = context;
	fView = new DiscoveryView(this);
	return fView;
}


status_t
DiscoveryModule::LoadSettings(SettingsContext* context)
{
	fContext = context;
	fCurrent.name = host_name();
	fCurrent.discoverable = read_discoverable();
	fCurrent.ssh = service_running("ssh");
	fCurrent.ftp = service_running("ftp");
	fOpened = fCurrent;
	if (fView != NULL)
		fView->Reload();
	return B_OK;
}


// Applied in UserEdited(), as Display does.
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
	status_t status = B_OK;
	if (to.name != fCurrent.name) {
		if (sethostname(to.name, to.name.Length()) != 0)
			status = B_ERROR;
		if (status != B_OK) {
			result = status;
			BAlert* alert = new BAlert("Rename",
				"The name could not be saved.", "OK", NULL, NULL,
				B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go(NULL);
		} else
			fCurrent.name = to.name;
	}
	if (to.discoverable != fCurrent.discoverable) {
		status = apply_discoverable(to.discoverable);
		if (status == B_OK)
			fCurrent.discoverable = to.discoverable;
		else
			result = status;
	}
	if (to.ssh != fCurrent.ssh) {
		status = apply_service("ssh", to.ssh);
		if (status == B_OK)
			fCurrent.ssh = to.ssh;
		else
			result = status;
	}
	if (to.ftp != fCurrent.ftp) {
		status = apply_service("ftp", to.ftp);
		if (status == B_OK)
			fCurrent.ftp = to.ftp;
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
	if (fView != NULL)
		fView->Reload();
	context->NotifyChanged(Identifier());
	return status;
}


bool
DiscoveryModule::IsModified() const
{
	return fCurrent != fOpened;
}


bool
DiscoveryModule::CanApplyDefaults() const
{
	return !fCurrent.discoverable || fCurrent.ssh || fCurrent.ftp;
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
