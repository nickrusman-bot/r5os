#!/usr/bin/env python3
"""Use find_directory for leftover hardcoded home settings paths. Idempotent."""
from __future__ import annotations

import pathlib
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")


def replace_once(
    path: pathlib.Path, old: str, new: str, tag: str, optional: bool = False
) -> None:
    text = path.read_text(encoding="utf-8")
    if old not in text:
        if optional:
            print(f"skip {path.name} ({tag})")
            return
        if not new or new in text:
            print(f"already {path.name} ({tag})")
            return
        raise SystemExit(f"pattern not found in {path} ({tag})")
    if new and new in text:
        print(f"already {path.name} ({tag})")
        return
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"patched {path.name} ({tag})")


def replace_all(path: pathlib.Path, old: str, new: str, tag: str) -> None:
    text = path.read_text(encoding="utf-8")
    if old not in text:
        if new in text:
            print(f"already {path.name} ({tag})")
            return
        raise SystemExit(f"pattern not found in {path} ({tag})")
    path.write_text(text.replace(old, new), encoding="utf-8")
    print(f"patched {path.name} ({tag})")


def patch_midiplayer() -> None:
    header = HAIKU / "src/apps/midiplayer/MidiPlayerWindow.h"
    cpp = HAIKU / "src/apps/midiplayer/MidiPlayerWindow.cpp"
    replace_once(
        header,
        '#define SETTINGS_FILE "/boot/home/config/settings/MidiPlayerSettings"\n\n',
        "",
        "drop hardcoded settings path",
    )
    replace_once(
        cpp,
        '#include "SynthBridge.h"\n'
        "\n"
        "\n"
        "#define _W(a) (a->Frame().Width())\n"
        "#define _H(a) (a->Frame().Height())\n",
        '#include "SynthBridge.h"\n'
        "\n"
        "#include <string.h>\n"
        "\n"
        "\n"
        "#define _W(a) (a->Frame().Width())\n"
        "#define _H(a) (a->Frame().Height())\n"
        "\n"
        "\n"
        "static const char*\n"
        "settings_file_path()\n"
        "{\n"
        "\tstatic char sPath[B_PATH_NAME_LENGTH];\n"
        "\tBPath path;\n"
        "\tif (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) == B_OK\n"
        "\t\t&& path.Append(\"MidiPlayerSettings\") == B_OK\n"
        "\t\t&& path.Path() != NULL) {\n"
        "\t\tstrlcpy(sPath, path.Path(), sizeof(sPath));\n"
        "\t\treturn sPath;\n"
        "\t}\n"
        "\treturn NULL;\n"
        "}\n",
        "settings helper",
    )
    replace_once(
        cpp,
        "\tBFile file(SETTINGS_FILE, B_READ_ONLY);\n"
        "\tif (file.InitCheck() != B_OK || file.Lock() != B_OK)\n"
        "\t\treturn;\n",
        "\tconst char* settingsPath = settings_file_path();\n"
        "\tif (settingsPath == NULL)\n"
        "\t\treturn;\n"
        "\tBFile file(settingsPath, B_READ_ONLY);\n"
        "\tif (file.InitCheck() != B_OK || file.Lock() != B_OK)\n"
        "\t\treturn;\n",
        "load settings",
    )
    replace_once(
        cpp,
        "\tBFile file(SETTINGS_FILE, B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);\n"
        "\tif (file.InitCheck() != B_OK || file.Lock() != B_OK)\n"
        "\t\treturn;\n",
        "\tconst char* settingsPath = settings_file_path();\n"
        "\tif (settingsPath == NULL)\n"
        "\t\treturn;\n"
        "\tBFile file(settingsPath, B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);\n"
        "\tif (file.InitCheck() != B_OK || file.Lock() != B_OK)\n"
        "\t\treturn;\n",
        "save settings",
    )


def patch_joysticks() -> None:
    path = HAIKU / "src/preferences/joysticks/JoyWin.cpp"
    replace_once(
        path,
        '#define JOYSTICKFILESETTINGS "/boot/home/config/settings/joysticks/"\n'
        '#define SELECTGAMEPORTFIRST "Select a game port first"\n',
        '#define SELECTGAMEPORTFIRST "Select a game port first"\n'
        "\n"
        "\n"
        "static BString\n"
        "joystick_settings_directory()\n"
        "{\n"
        "\tBPath path;\n"
        "\tif (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) != B_OK)\n"
        "\t\treturn BString();\n"
        "\tpath.Append(\"joysticks\");\n"
        "\tcreate_directory(path.Path(), 0755);\n"
        "\tBString dir(path.Path());\n"
        "\tdir << '/';\n"
        "\treturn dir;\n"
        "}\n"
        "\n"
        "\n",
        "settings dir helper",
    )
    replace_all(
        path,
        "JOYSTICKFILESETTINGS",
        "joystick_settings_directory()",
        "use helper",
    )
    replace_once(
        path,
        "_FindFilePathForSymLink(joystick_settings_directory(),",
        "_FindFilePathForSymLink(joystick_settings_directory().String(),",
        "symlink helper wants const char*",
        optional=True,
    )


def patch_poorman() -> None:
    header = HAIKU / "src/apps/poorman/constants.h"
    cpp = HAIKU / "src/apps/poorman/constants.cpp"
    app = HAIKU / "src/apps/poorman/PoorManApplication.cpp"
    window = HAIKU / "src/apps/poorman/PoorManWindow.cpp"
    prefs = HAIKU / "src/apps/poorman/PoorManPreferencesWindow.cpp"

    replace_once(
        header,
        "extern const char* STR_DEFAULT_WEB_DIRECTORY;\n",
        "const char* DefaultWebDirectory();\n",
        "declare helper",
    )
    replace_once(
        cpp,
        '#include "constants.h"\n'
        "\n"
        "#include <Catalog.h>\n"
        "#include <InterfaceDefs.h>\n"
        "#include <Locale.h>\n",
        '#include "constants.h"\n'
        "\n"
        "#include <Catalog.h>\n"
        "#include <FindDirectory.h>\n"
        "#include <InterfaceDefs.h>\n"
        "#include <Locale.h>\n"
        "#include <Path.h>\n"
        "\n"
        "#include <string.h>\n",
        "includes",
    )
    replace_once(
        cpp,
        "\t  \"A default web folder has been created \"\n"
        "\t  \"at \\\"/boot/home/public_html.\\\"\\nMake sure there is a HTML \"\n"
        "\t  \"file named \\\"index.html\\\" in that folder.\");\n",
        "\t  \"A default web folder has been created \"\n"
        "\t  \"in your home folder (public_html).\\nMake sure there is a HTML \"\n"
        "\t  \"file named \\\"index.html\\\" in that folder.\");\n",
        "created-folder copy",
    )
    replace_once(
        cpp,
        "const char* STR_SETTINGS_FILE_NAME\n"
        "\t= \"PoorMan settings\";\n"
        "const char* STR_DEFAULT_WEB_DIRECTORY\n"
        "\t= \"/boot/home/public_html\";\n",
        "const char* STR_SETTINGS_FILE_NAME\n"
        "\t= \"PoorMan settings\";\n"
        "\n"
        "\n"
        "const char*\n"
        "DefaultWebDirectory()\n"
        "{\n"
        "\tstatic char sPath[B_PATH_NAME_LENGTH];\n"
        "\tBPath path;\n"
        "\tif (find_directory(B_USER_DIRECTORY, &path) == B_OK\n"
        "\t\t&& path.Append(\"public_html\") == B_OK\n"
        "\t\t&& path.Path() != NULL) {\n"
        "\t\tstrlcpy(sPath, path.Path(), sizeof(sPath));\n"
        "\t\treturn sPath;\n"
        "\t}\n"
        "\tstrlcpy(sPath, \"/boot/home/public_html\", sizeof(sPath));\n"
        "\treturn sPath;\n"
        "}\n",
        "web dir helper",
    )
    replace_all(app, "STR_DEFAULT_WEB_DIRECTORY", "DefaultWebDirectory()", "app")
    replace_all(
        window, "STR_DEFAULT_WEB_DIRECTORY", "DefaultWebDirectory()", "window"
    )
    replace_once(
        prefs,
        "#include <Directory.h>\n"
        "#include <LayoutBuilder.h>\n",
        "#include <Directory.h>\n"
        "#include <FindDirectory.h>\n"
        "#include <LayoutBuilder.h>\n"
        "#include <Path.h>\n",
        "pref includes",
    )
    replace_once(
        prefs,
        "\tfWebDirFilePanel->SetPanelDirectory(\n"
        "\t\tnew BDirectory(\"/boot/home/public_html\"));\n",
        "\tBPath homePath;\n"
        "\tBDirectory webDir;\n"
        "\tif (find_directory(B_USER_DIRECTORY, &homePath) == B_OK) {\n"
        "\t\tBPath webPath(homePath.Path(), \"public_html\");\n"
        "\t\tif (webDir.SetTo(webPath.Path()) != B_OK)\n"
        "\t\t\twebDir.SetTo(homePath.Path());\n"
        "\t\tfWebDirFilePanel->SetPanelDirectory(&webDir);\n"
        "\t}\n",
        "file panel start dir",
    )


def patch_webpositive() -> None:
    header = HAIKU / "src/apps/webpositive/SettingsKeys.h"
    cpp = HAIKU / "src/apps/webpositive/SettingsKeys.cpp"
    replace_once(
        header,
        "extern const char* kDefaultDownloadPath;\n"
        "extern const char* kDefaultStartPageURL;\n",
        "const char* DefaultDownloadPath();\n"
        "const char* DefaultStartPageURL();\n",
        "declare defaults",
    )
    replace_once(
        cpp,
        '#include "SettingsKeys.h"\n',
        '#include "SettingsKeys.h"\n'
        "\n"
        "#include <FindDirectory.h>\n"
        "#include <Path.h>\n"
        "\n"
        "#include <stdio.h>\n",
        "includes",
    )
    replace_once(
        cpp,
        'const char* kDefaultDownloadPath = "/boot/home/Desktop/";\n'
        "const char* kDefaultStartPageURL\n"
        '\t= "file:///boot/home/config/settings/WebPositive/LoaderPages/Welcome";\n',
        "const char*\n"
        "DefaultDownloadPath()\n"
        "{\n"
        "\tstatic char sPath[B_PATH_NAME_LENGTH];\n"
        "\tBPath path;\n"
        "\tif (find_directory(B_DESKTOP_DIRECTORY, &path) == B_OK\n"
        "\t\t&& path.Path() != NULL) {\n"
        "\t\tsnprintf(sPath, sizeof(sPath), \"%s/\", path.Path());\n"
        "\t\treturn sPath;\n"
        "\t}\n"
        "\treturn \"/boot/home/Desktop/\";\n"
        "}\n"
        "\n"
        "\n"
        "const char*\n"
        "DefaultStartPageURL()\n"
        "{\n"
        "\tstatic char sUrl[B_PATH_NAME_LENGTH + 64];\n"
        "\tBPath path;\n"
        "\tif (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK\n"
        "\t\t&& path.Path() != NULL) {\n"
        "\t\tsnprintf(sUrl, sizeof(sUrl),\n"
        "\t\t\t\"file://%s/WebPositive/LoaderPages/Welcome\", path.Path());\n"
        "\t\treturn sUrl;\n"
        "\t}\n"
        "\treturn \"file:///boot/home/config/settings/WebPositive/LoaderPages/Welcome\";\n"
        "}\n",
        "default helpers",
    )
    for rel, old, new, tag in (
        (
            "src/apps/webpositive/BrowserWindow.cpp",
            "kDefaultStartPageURL",
            "DefaultStartPageURL()",
            "browser window start page",
        ),
        (
            "src/apps/webpositive/SettingsWindow.cpp",
            "kDefaultStartPageURL",
            "DefaultStartPageURL()",
            "settings start page",
        ),
        (
            "src/apps/webpositive/SettingsWindow.cpp",
            "kDefaultDownloadPath",
            "DefaultDownloadPath()",
            "settings download path",
        ),
    ):
        replace_all(HAIKU / rel, old, new, tag)


def main() -> int:
    if not (HAIKU / "src").is_dir():
        raise SystemExit(f"haiku tree missing: {HAIKU}")
    patch_midiplayer()
    patch_joysticks()
    patch_poorman()
    patch_webpositive()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
