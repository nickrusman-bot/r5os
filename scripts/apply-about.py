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
        "			CropView*		_CreateCreditsView();\n",
        "			CropView*		_CreateCreditsView();\n"
        "			BView*			_CreateInfoTabs();\n"
        "			void			_FillWhatsNew();\n",
        "AboutView tab methods",
        optional=True,
    )
    replace_once(
        path,
        "			HyperTextView*	fCreditsView;\n",
        "			HyperTextView*	fCreditsView;\n"
        "			HyperTextView*	fWhatsNewView;\n"
        "			BTabView*		fTabView;\n",
        "AboutView tab members",
        optional=True,
    )
    replace_once(
        path,
        "	fCreditsView(NULL),\n",
        "	fCreditsView(NULL),\n"
        "	fWhatsNewView(NULL),\n"
        "	fTabView(NULL),\n",
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


def main() -> int:
    copy_whatsnew()
    patch_rdef()
    patch_cpp()
    return 0


if __name__ == "__main__":
    sys.exit(main())
