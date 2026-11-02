#!/usr/bin/env python3
"""About R5OS: build number, What's new tab. Idempotent."""
from __future__ import annotations

import pathlib
import shutil
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")
ROOT = pathlib.Path(__file__).resolve().parents[1]
ABOUT = HAIKU / "src" / "apps" / "aboutsystem"


def replace_once(
    path: pathlib.Path, old: str, new: str, tag: str, optional: bool = False
) -> None:
    text = path.read_text(encoding="utf-8")
    if new in text:
        print(f"already {path.name} ({tag})")
        return
    if old not in text:
        if optional:
            print(f"skip {path.name} ({tag})")
            return
        raise SystemExit(f"pattern not found in {path} ({tag})")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"patched {path.name} ({tag})")


def copy_whatsnew() -> None:
    src = ROOT / "distro" / "WHATSNEW"
    dst = ABOUT / "r5_whatsnew.txt"
    shutil.copy2(src, dst)
    licenses = ROOT / "distro" / "about" / "licenses.txt"
    license_dst = ABOUT / "r5_licenses.txt"
    shutil.copy2(licenses, license_dst)
    print(f"copied licenses -> {license_dst}")
    rdef = ABOUT / "AboutSystem.rdef"
    if rdef.is_file():
        rdef.touch()
    print(f"copied WHATSNEW -> {dst}")


def patch_rdef() -> None:
    replace_once(
        ABOUT / "AboutSystem.rdef",
        'resource(1, "walter_logo.png") #\'PNG \' import "../../../data/artwork/R5OS-logo.png";\n#endif\n',
        'resource(1, "walter_logo.png") #\'PNG \' import "../../../data/artwork/R5OS-logo.png";\n'
        'resource(3, "r5_whatsnew") #\'CSTR\' import "r5_whatsnew.txt";\n'
        "#endif\n",
        "whatsnew resource",
        optional=True,
    )
    replace_once(
        ABOUT / "AboutSystem.rdef",
        'resource(3, "r5_whatsnew") \'CSTR\' import "r5_whatsnew.txt";\n',
        'resource(3, "r5_whatsnew") #\'CSTR\' import "r5_whatsnew.txt";\n',
        "whatsnew cstr type",
        optional=True,
    )
    replace_once(
        ABOUT / "AboutSystem.rdef",
        'resource(3, "r5_whatsnew") #\'CSTR\' import "r5_whatsnew.txt";\n',
        'resource(3, "r5_whatsnew") #\'CSTR\' import "r5_whatsnew.txt";\n'
        'resource(4, "r5_licenses") #\'CSTR\' import "r5_licenses.txt";\n',
        "licenses resource",
        optional=True,
    )


def patch_cpp() -> None:
    path = ABOUT / "AboutSystem.cpp"
    replace_once(
        path,
        "#include <ScrollView.h>\n",
        "#include <ScrollView.h>\n#include <TabView.h>\n",
        "TabView include",
        optional=True,
    )
    replace_once(
        path,
        "	virtual void			Draw(BRect updateRect);\n\n"
        "private:\n"
        "			BBitmap*		fLogo;\n",
        "	virtual void			Draw(BRect updateRect);\n\n"
        "			BSize			_DisplaySize() const;\n\n"
        "private:\n"
        "			BBitmap*		fLogo;\n",
        "LogoView display size decl",
        optional=True,
    )
    replace_once(
        path,
        """BSize
LogoView::MinSize()
{
	if (fLogo == NULL)
		return BSize(0, 0);

	return BSize(fLogo->Bounds().Width(), fLogo->Bounds().Height());
}


BSize
LogoView::MaxSize()
{
	if (fLogo == NULL)
		return BSize(0, 0);

	return BSize(B_SIZE_UNLIMITED, fLogo->Bounds().Height());
}


void
LogoView::Draw(BRect updateRect)
{
	if (fLogo == NULL)
		return;

	BRect bounds(Bounds());
	SetLowColor(ui_color(B_DOCUMENT_BACKGROUND_COLOR));
	FillRect(bounds, B_SOLID_LOW);

	DrawBitmap(fLogo, BPoint((bounds.Width() - fLogo->Bounds().Width()) / 2, 0));
}
""",
        """BSize
LogoView::_DisplaySize() const
{
	if (fLogo == NULL)
		return BSize(0, 0);

	// Unofficial Haiku used Walter logo.png (170x110). Keep the R5OS
	// wordmark in that box so About does not grow with a 420px asset.
	const float kMaxWidth = 170.0f;
	const float kMaxHeight = 110.0f;
	float width = fLogo->Bounds().Width();
	float height = fLogo->Bounds().Height();
	float scale = 1.0f;
	if (width > kMaxWidth)
		scale = kMaxWidth / width;
	if (height * scale > kMaxHeight)
		scale = kMaxHeight / height;
	return BSize(width * scale, height * scale);
}


BSize
LogoView::MinSize()
{
	return _DisplaySize();
}


BSize
LogoView::MaxSize()
{
	BSize size = _DisplaySize();
	if (size.width > 0)
		size.width = B_SIZE_UNLIMITED;
	return size;
}


void
LogoView::Draw(BRect updateRect)
{
	if (fLogo == NULL)
		return;

	BRect bounds(Bounds());
	SetLowColor(ui_color(B_DOCUMENT_BACKGROUND_COLOR));
	FillRect(bounds, B_SOLID_LOW);

	BSize size = _DisplaySize();
	float left = (bounds.Width() - size.width) / 2;
	BRect dest(left, 0, left + size.width, size.height);
	DrawBitmap(fLogo, fLogo->Bounds(), dest, B_FILTER_BITMAP_BILINEAR);
}
""",
        "LogoView Walter size",
        optional=True,
    )
    replace_once(
        path,
        """	revision.SetToFormat(B_TRANSLATE_COMMENT("R5OS %s",
		"Marketing version, e.g. R5OS 2026.1-dev"), R5_OS_VERSION);
	return revision;
""",
        """	revision.SetToFormat(
		B_TRANSLATE_COMMENT("R5OS %s Build %" B_PRId32,
			"Marketing version and build, e.g. R5OS 2026.1-dev Build 10001"),
		R5_OS_VERSION, (int32)R5_OS_BUILD);
	return revision;
""",
        "About version build",
        optional=True,
    )
    replace_once(
        path,
        '\tabiVersion << " (" << B_HAIKU_ABI_NAME << ")";\n',
        """\t// Package ABI stays x86_64. About shows the name used for the target.
	const char* archName = B_HAIKU_ABI_NAME;
	if (strcmp(archName, "x86_64") == 0)
		archName = "AMD64";
	else if (strcmp(archName, "aarch64") == 0)
		archName = "ARM64";
	else if (strcmp(archName, "riscv64") == 0)
		archName = "RISC-V 64";
	abiVersion << " (" << archName << ")";
""",
        "About architecture name",
        optional=True,
    )
    replace_once(
        path,
        "			CropView*		_CreateCreditsView();\n",
        "			CropView*		_CreateCreditsView();\n"
        "			BView*			_CreateInfoTabs();\n"
        "			void			_FillWhatsNew();\n",
        "AboutView tab methods",
        optional=True,
    )
    replace_once(
        path,
        "			HyperTextView*	fCreditsView;\n"
        "\n"
        "			bigtime_t		fLastActionTime;\n",
        "			HyperTextView*	fCreditsView;\n"
        "			HyperTextView*	fWhatsNewView;\n"
        "			BTabView*		fTabView;\n"
        "\n"
        "			bigtime_t		fLastActionTime;\n",
        "AboutView tab members",
        optional=True,
    )
    replace_once(
        path,
        "	fCreditsView(NULL),\n"
        "	fLastActionTime(system_time()),\n",
        "	fCreditsView(NULL),\n"
        "	fWhatsNewView(NULL),\n"
        "	fTabView(NULL),\n"
        "	fLastActionTime(system_time()),\n",
        "AboutView tab init",
        optional=True,
    )
    replace_once(
        path,
        "		.Add(_CreateCreditsView())\n",
        "		.Add(_CreateInfoTabs())\n",
        "AboutView tab layout",
        optional=True,
    )
    replace_once(
        path,
        """		case kMsgScrollCreditsView:
		{
			BScrollBar* scrollBar = fCreditsView->ScrollBar(B_VERTICAL);
""",
        """		case kMsgScrollCreditsView:
		{
			if (fTabView != NULL && fTabView->Selection() != 0)
				break;
			BScrollBar* scrollBar = fCreditsView->ScrollBar(B_VERTICAL);
""",
        "pause credits scroll",
        optional=True,
    )
    cpp_now = path.read_text(encoding="utf-8")
    if "AboutView::_CreateInfoTabs()" in cpp_now:
        print("already AboutSystem.cpp (CreateInfoTabs)")
    else:
        replace_once(
        path,
        """CropView*
AboutView::_CreateCreditsView()
{
""",
        """BView*
AboutView::_CreateInfoTabs()
{
	CropView* credits = _CreateCreditsView();

	fWhatsNewView = new HyperTextView("whatsnew");
	fWhatsNewView->SetFlags(fWhatsNewView->Flags() | B_FRAME_EVENTS);
	fWhatsNewView->SetStylable(true);
	fWhatsNewView->MakeEditable(false);
	fWhatsNewView->SetWordWrap(true);
	fWhatsNewView->SetInsets(5, 5, 5, 5);
	fWhatsNewView->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	_FillWhatsNew();

	BScrollView* newsScroller = new BScrollView("whatsNewScroller",
		fWhatsNewView, B_WILL_DRAW | B_FRAME_EVENTS, false, true,
		B_PLAIN_BORDER);

	fTabView = new BTabView("aboutTabs", B_WIDTH_FROM_LABEL);
	fTabView->AddTab(credits);
	fTabView->TabAt(0)->SetLabel(B_TRANSLATE("Credits"));
	fTabView->AddTab(newsScroller);
	fTabView->TabAt(1)->SetLabel(B_TRANSLATE("What's new"));
	return fTabView;
}


void
AboutView::_FillWhatsNew()
{
	BString text;
	BResources* resources = BApplication::AppResources();
	if (resources != NULL) {
		size_t size = 0;
		const void* data = resources->LoadResource(B_STRING_TYPE,
			"r5_whatsnew", &size);
		if (data != NULL && size > 0)
			text.SetTo(static_cast<const char*>(data), (int32)size);
	}
	if (text.Length() > 0 && text[text.Length() - 1] == '\\0')
		text.Truncate(text.Length() - 1);
	if (text.IsEmpty())
		text = B_TRANSLATE("No notes in this build.");

	BFont headingFont(be_bold_font);
	headingFont.SetSize(headingFont.Size() + 2);

	int32 start = 0;
	while (start < text.Length()) {
		int32 end = text.FindFirst('\\n', start);
		if (end < 0)
			end = text.Length();
		BString line;
		text.CopyInto(line, start, end - start);
		if (line.FindFirst("R5OS ") == 0) {
			fWhatsNewView->SetFontAndColor(&headingFont, B_FONT_ALL,
				&fHaikuGreenColor);
		} else {
			fWhatsNewView->SetFontAndColor(be_plain_font, B_FONT_ALL,
				&fTextColor);
		}
		fWhatsNewView->Insert(line);
		fWhatsNewView->Insert("\\n");
		if (end >= text.Length())
			break;
		start = end + 1;
	}
}


CropView*
AboutView::_CreateCreditsView()
{
""",
        "CreateInfoTabs",
        optional=True,
    )
    replace_once(
        path,
        "			void			_FillWhatsNew();\n",
        "			void			_FillWhatsNew();\n"
        "			void			_FillLicenses();\n",
        "AboutView license method",
        optional=True,
    )
    replace_once(
        path,
        "			HyperTextView*	fWhatsNewView;\n"
        "			BTabView*		fTabView;\n",
        "			HyperTextView*	fWhatsNewView;\n"
        "			HyperTextView*	fLicensesView;\n"
        "			BTabView*		fTabView;\n",
        "AboutView license member",
        optional=True,
    )
    replace_once(
        path,
        "	fWhatsNewView(NULL),\n"
        "	fTabView(NULL),\n",
        "	fWhatsNewView(NULL),\n"
        "	fLicensesView(NULL),\n"
        "	fTabView(NULL),\n",
        "AboutView license init",
        optional=True,
    )
    replace_once(
        path,
        "	fTabView->AddTab(newsScroller);\n"
        "	fTabView->TabAt(1)->SetLabel(B_TRANSLATE(\"What's new\"));\n"
        "	return fTabView;\n",
        "	fTabView->AddTab(newsScroller);\n"
        "	fTabView->TabAt(1)->SetLabel(B_TRANSLATE(\"What's new\"));\n"
        "\n"
        "	fLicensesView = new HyperTextView(\"licenses\");\n"
        "	fLicensesView->SetFlags(fLicensesView->Flags() | B_FRAME_EVENTS);\n"
        "	fLicensesView->SetStylable(true);\n"
        "	fLicensesView->MakeEditable(false);\n"
        "	fLicensesView->SetWordWrap(true);\n"
        "	fLicensesView->SetInsets(5, 5, 5, 5);\n"
        "	fLicensesView->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);\n"
        "	_FillLicenses();\n"
        "\n"
        "	BScrollView* licenseScroller = new BScrollView(\"licenseScroller\",\n"
        "		fLicensesView, B_WILL_DRAW | B_FRAME_EVENTS, false, true,\n"
        "		B_PLAIN_BORDER);\n"
        "	fTabView->AddTab(licenseScroller);\n"
        "	fTabView->TabAt(2)->SetLabel(B_TRANSLATE(\"Licenses\"));\n"
        "	return fTabView;\n",
        "AboutView licenses tab",
        optional=True,
    )
    replace_once(
        path,
        "		start = end + 1;\n"
        "	}\n"
        "}\n"
        "\n"
        "\n"
        "CropView*\n"
        "AboutView::_CreateCreditsView()\n",
        "		start = end + 1;\n"
        "	}\n"
        "}\n"
        "\n"
        "\n"
        "void\n"
        "AboutView::_FillLicenses()\n"
        "{\n"
        "	BString text;\n"
        "	BResources* resources = BApplication::AppResources();\n"
        "	if (resources != NULL) {\n"
        "		size_t size = 0;\n"
        "		const void* data = resources->LoadResource(B_STRING_TYPE,\n"
        "			\"r5_licenses\", &size);\n"
        "		if (data != NULL && size > 0)\n"
        "			text.SetTo(static_cast<const char*>(data), (int32)size);\n"
        "	}\n"
        "	if (text.Length() > 0 && text[text.Length() - 1] == '\\0')\n"
        "		text.Truncate(text.Length() - 1);\n"
        "	if (text.IsEmpty())\n"
        "		text = B_TRANSLATE(\"No license notes in this build.\");\n"
        "\n"
        "	fLicensesView->SetFontAndColor(be_plain_font, B_FONT_ALL, &fTextColor);\n"
        "	fLicensesView->Insert(text);\n"
        "	if (text.Length() == 0 || text[text.Length() - 1] != '\\n')\n"
        "		fLicensesView->Insert(\"\\n\");\n"
        "}\n"
        "\n"
        "\n"
        "CropView*\n"
        "AboutView::_CreateCreditsView()\n",
        "AboutView fill licenses",
        optional=True,
    )


def patch_jam() -> None:
    jam = ABOUT / "Jamfile"
    text = jam.read_text(encoding="utf-8")
    search = (
        "SEARCH on r5_whatsnew.txt = [ FDirName $(HAIKU_TOP) "
        "src apps aboutsystem ] ;\n"
    )
    depend = "Depends AboutSystem : r5_whatsnew.txt ;\n"
    license_search = (
        "SEARCH on r5_licenses.txt = [ FDirName $(HAIKU_TOP) "
        "src apps aboutsystem ] ;\n"
    )
    license_depend = "Depends AboutSystem : r5_licenses.txt ;\n"
    if license_search not in text and "r5_whatsnew.txt" in text:
        jam.write_text(
            text.replace(
                depend,
                depend + license_search + license_depend,
                1,
            ),
            encoding="utf-8",
        )
        print("patched Jamfile (licenses jam depend)")
        text = jam.read_text(encoding="utf-8")
    elif license_search in text:
        print("already Jamfile (licenses jam depend)")
    if search in text:
        print("already Jamfile (whatsnew jam depend)")
        return
    if depend in text:
        jam.write_text(text.replace(depend, search + depend, 1), encoding="utf-8")
        print("patched Jamfile (whatsnew jam search)")
        return
    replace_once(
        jam,
        """Application AboutSystem :
	AboutSystem.cpp
	HyperTextActions.cpp
	HyperTextView.cpp
	Utilities.cpp
	: [ TargetLibstdc++ ] be translation localestub
	: AboutSystem.rdef
;
""",
        """Application AboutSystem :
	AboutSystem.cpp
	HyperTextActions.cpp
	HyperTextView.cpp
	Utilities.cpp
	: [ TargetLibstdc++ ] be translation localestub
	: AboutSystem.rdef
;

SEARCH on r5_whatsnew.txt = [ FDirName $(HAIKU_TOP) src apps aboutsystem ] ;
Depends AboutSystem : r5_whatsnew.txt ;
""",
        "whatsnew jam depend",
        optional=True,
    )


def patch_gpl_credit() -> None:
    path = ABOUT / "AboutSystem.cpp"
    # License text lives on the Licenses tab. Credits keeps a short name.
    replace_once(
        path,
        """		"Project.\\n\\n"
		"This build includes GNU GPL version 3 code: VirtualBox Guest "
		"Additions 7.2.22. The license is GPL version 3 only. The license "
		"text and the matching source are at "
		"/boot/system/non-packaged/data/licenses/virtualbox.\\n\\n"));
	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &fHaikuGreenColor);
#endif
	fCreditsView->Insert("Haiku\\n");
""",
        """		"Project.\\n\\n"));
	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &fHaikuGreenColor);
#endif
	fCreditsView->Insert("Haiku\\n");
""",
        "About move GPL paragraph off credits",
        optional=True,
    )
    replace_once(
        path,
        """	AddCopyrightEntry("VirtualBox Guest Additions",
		B_TRANSLATE("Guest Additions 7.2.22, included in this build. "
		"The license is the GNU General Public License, version 3, only. "
		"Some runtime files also offer the CDDL; this build uses the GPL "
		"version 3 terms. The license text and the matching source, "
		"including the R5OS build fixes, are at "
		"/boot/system/non-packaged/data/licenses/virtualbox.\\n"),
""",
        """	AddCopyrightEntry("VirtualBox Guest Additions",
		B_TRANSLATE("Guest software included with this build.\\n"),
""",
        "About shorten VirtualBox credit",
        optional=True,
    )


def patch_license_format() -> None:
    """Headings are '# Name'. A source line links the URL after 'Source: '."""
    path = ABOUT / "AboutSystem.cpp"
    text = path.read_text(encoding="utf-8")
    if 'line.StartsWith("# ")' in text:
        print("already AboutSystem.cpp (license format)")
        return
    old = (
        "\t\tif (line.StartsWith(\"http://\") || line.StartsWith(\"https://\")) {\n"
        "\t\t\tfLicensesView->SetFontAndColor(be_plain_font, B_FONT_ALL,\n"
        "\t\t\t\t&fLinkColor);\n"
        "\t\t\tfLicensesView->InsertHyperText(line, new URLAction(line.String()));\n"
        "\t\t} else if (line.Length() > 0 && line.FindFirst(\".\") < 0) {\n"
        "\t\t\tfLicensesView->SetFontAndColor(&headingFont, B_FONT_ALL,\n"
        "\t\t\t\t&fHaikuGreenColor);\n"
        "\t\t\tfLicensesView->Insert(line);\n"
        "\t\t} else {\n"
        "\t\t\tfLicensesView->SetFontAndColor(be_plain_font, B_FONT_ALL,\n"
        "\t\t\t\t&fTextColor);\n"
        "\t\t\tfLicensesView->Insert(line);\n"
        "\t\t}\n"
    )
    new = (
        "\t\tif (line.StartsWith(\"# \")) {\n"
        "\t\t\tline.Remove(0, 2);\n"
        "\t\t\tfLicensesView->SetFontAndColor(&headingFont, B_FONT_ALL,\n"
        "\t\t\t\t&fHaikuGreenColor);\n"
        "\t\t\tfLicensesView->Insert(line);\n"
        "\t\t} else if (line.StartsWith(\"Source: http://\")\n"
        "\t\t\t|| line.StartsWith(\"Source: https://\")) {\n"
        "\t\t\tBString url(line);\n"
        "\t\t\turl.Remove(0, 8);\n"
        "\t\t\tfLicensesView->SetFontAndColor(be_plain_font, B_FONT_ALL,\n"
        "\t\t\t\t&fTextColor);\n"
        "\t\t\tfLicensesView->Insert(\"Source: \");\n"
        "\t\t\tfLicensesView->SetFontAndColor(be_plain_font, B_FONT_ALL,\n"
        "\t\t\t\t&fLinkColor);\n"
        "\t\t\tfLicensesView->InsertHyperText(url.String(),\n"
        "\t\t\t\tnew URLAction(url.String()));\n"
        "\t\t} else if (line.StartsWith(\"http://\") || line.StartsWith(\"https://\")) {\n"
        "\t\t\tfLicensesView->SetFontAndColor(be_plain_font, B_FONT_ALL,\n"
        "\t\t\t\t&fLinkColor);\n"
        "\t\t\tfLicensesView->InsertHyperText(line, new URLAction(line.String()));\n"
        "\t\t} else {\n"
        "\t\t\tfLicensesView->SetFontAndColor(be_plain_font, B_FONT_ALL,\n"
        "\t\t\t\t&fTextColor);\n"
        "\t\t\tfLicensesView->Insert(line);\n"
        "\t\t}\n"
    )
    if old not in text:
        raise SystemExit("license line format not found")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print("patched AboutSystem.cpp (license format)")


def patch_license_entries() -> None:
    """Style license headings, and list Haiku's own components with links."""
    path = ABOUT / "AboutSystem.cpp"
    text = path.read_text(encoding="utf-8")
    marker = "fLicensesView != NULL ? 2 : 1"
    if marker in text:
        print("already AboutSystem.cpp (license links)")
        return

    start = text.find(
        "void\nAboutView::AddCopyrightEntry(const char* name, const char* text,\n"
        "\tconst StringVector& licenses, const StringVector& sources,\n"
        "\tconst char* url)\n{\n"
    )
    end = text.find("\n\nvoid\nAboutView::PickRandomHaiku()", start)
    if start < 0 or end < 0:
        raise SystemExit("AddCopyrightEntry body not found")
    body = text[start:end]
    body = body.replace("fCreditsView->", "view->")
    wrapped = (
        "void\nAboutView::AddCopyrightEntry(const char* name, const char* text,\n"
        "\tconst StringVector& licenses, const StringVector& sources,\n"
        "\tconst char* url)\n{\n"
        "\tHyperTextView* views[] = { fCreditsView, fLicensesView };\n"
        "\tint32 viewCount = fLicensesView != NULL ? 2 : 1;\n"
        "\tfor (int32 viewIndex = 0; viewIndex < viewCount; viewIndex++) {\n"
        "\t\tHyperTextView* view = views[viewIndex];\n"
        "\t\tif (view == NULL)\n"
        "\t\t\tcontinue;\n"
    )
    # Drop the original signature; keep the statements, one extra indent.
    brace = body.find("{\n")
    statements = body[brace + 2:]
    indented = "".join(
        ("\t" + line if line else line)
        for line in statements.splitlines(keepends=True)
    )
    if not indented.endswith("\t}\n"):
        # The function closes with a single tabbed brace.
        pass
    # The original closing brace is the function's. Pull it inside the loop.
    if not indented.rstrip().endswith("}"):
        raise SystemExit("AddCopyrightEntry has no closing brace")
    close_at = indented.rfind("}")
    inside = indented[:close_at]
    wrapped += inside + "\t}\n}\n"
    text = text[:start] + wrapped + text[end:]

    old_tabs = (
        "AboutView::_CreateInfoTabs()\n"
        "{\n"
        "\tCropView* credits = _CreateCreditsView();\n"
    )
    new_tabs = (
        "AboutView::_CreateInfoTabs()\n"
        "{\n"
        "\tfLicensesView = new HyperTextView(\"licenses\");\n"
        "\tfLicensesView->SetFlags(fLicensesView->Flags() | B_FRAME_EVENTS);\n"
        "\tfLicensesView->SetStylable(true);\n"
        "\tfLicensesView->MakeEditable(false);\n"
        "\tfLicensesView->SetWordWrap(true);\n"
        "\tfLicensesView->SetInsets(5, 5, 5, 5);\n"
        "\tfLicensesView->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);\n"
        "\t_FillLicenses();\n"
        "\n"
        "\tCropView* credits = _CreateCreditsView();\n"
    )
    if old_tabs not in text:
        raise SystemExit("info tabs order not found")
    text = text.replace(old_tabs, new_tabs, 1)
    # The later block still created the view. Drop that second construction.
    duplicate = (
        "\tfLicensesView = new HyperTextView(\"licenses\");\n"
        "\tfLicensesView->SetFlags(fLicensesView->Flags() | B_FRAME_EVENTS);\n"
        "\tfLicensesView->SetStylable(true);\n"
        "\tfLicensesView->MakeEditable(false);\n"
        "\tfLicensesView->SetWordWrap(true);\n"
        "\tfLicensesView->SetInsets(5, 5, 5, 5);\n"
        "\tfLicensesView->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);\n"
        "\t_FillLicenses();\n"
        "\n"
        "\tBScrollView* licenseScroller = new BScrollView(\"licenseScroller\",\n"
    )
    if text.count(duplicate) != 1:
        raise SystemExit("license view construction count is wrong")
    text = text.replace(
        duplicate,
        "\tBScrollView* licenseScroller = new BScrollView(\"licenseScroller\",\n",
        1,
    )

    old_fill = (
        "void\n"
        "AboutView::_FillLicenses()\n"
        "{\n"
        "\tBString text;\n"
    )
    new_fill = '''void
AboutView::_FillLicenses()
{
	BString text;
	BResources* resources = BApplication::AppResources();
	if (resources != NULL) {
		size_t size = 0;
		const void* data = resources->LoadResource(B_STRING_TYPE,
			"r5_licenses", &size);
		if (data != NULL && size > 0)
			text.SetTo(static_cast<const char*>(data), (int32)size);
	}
	if (text.Length() > 0 && text[text.Length() - 1] == '\\0')
		text.Truncate(text.Length() - 1);

	BFont headingFont(be_bold_font);
	headingFont.SetSize(headingFont.Size() + 2);

	int32 start = 0;
	while (start < text.Length()) {
		int32 end = text.FindFirst('\\n', start);
		if (end < 0)
			end = text.Length();
		BString line;
		text.CopyInto(line, start, end - start);
		if (line.StartsWith("# ")) {
			line.Remove(0, 2);
			fLicensesView->SetFontAndColor(&headingFont, B_FONT_ALL,
				&fHaikuGreenColor);
			fLicensesView->Insert(line);
		} else if (line.StartsWith("Source: http://")
			|| line.StartsWith("Source: https://")) {
			BString url(line);
			url.Remove(0, 8);
			fLicensesView->SetFontAndColor(be_plain_font, B_FONT_ALL,
				&fTextColor);
			fLicensesView->Insert("Source: ");
			fLicensesView->SetFontAndColor(be_plain_font, B_FONT_ALL,
				&fLinkColor);
			fLicensesView->InsertHyperText(url.String(),
				new URLAction(url.String()));
		} else if (line.StartsWith("http://") || line.StartsWith("https://")) {
			fLicensesView->SetFontAndColor(be_plain_font, B_FONT_ALL,
				&fLinkColor);
			fLicensesView->InsertHyperText(line, new URLAction(line.String()));
		} else {
			fLicensesView->SetFontAndColor(be_plain_font, B_FONT_ALL,
				&fTextColor);
			fLicensesView->Insert(line);
		}
		fLicensesView->Insert("\\n");
		if (end >= text.Length())
			break;
		start = end + 1;
	}
}


void
AboutView::_FillLicensesUnusedAnchor()
{
	BString text;
'''
    if old_fill not in text:
        raise SystemExit("FillLicenses not found")
    text = text.replace(old_fill, new_fill, 1)
    # Remove the unused anchor function left so the old resource-load stays
    # deleted. The anchor plus the original loader is now duplicate junk.
    anchor = text.find("void\nAboutView::_FillLicensesUnusedAnchor()")
    if anchor < 0:
        raise SystemExit("anchor missing")
    # Original function continued until CropView* _CreateCreditsView.
    crop = text.find("\n\nCropView*\nAboutView::_CreateCreditsView()", anchor)
    if crop < 0:
        raise SystemExit("credits view after fill not found")
    text = text[:anchor] + text[crop + 2:]
    path.write_text(text, encoding="utf-8")
    print("patched AboutSystem.cpp (license links)")


def main() -> int:
    copy_whatsnew()
    patch_license_format()
    patch_rdef()
    patch_jam()
    patch_cpp()
    patch_gpl_credit()
    patch_license_entries()
    return 0


if __name__ == "__main__":
    sys.exit(main())
