#!/usr/bin/env python3
"""R5OS Login on the boot path. Idempotent copy + launch hook."""
from __future__ import annotations

import pathlib
import shutil
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")
ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "distro" / "login"


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


def copy_sources() -> None:
    dest = HAIKU / "src" / "apps" / "login"
    dest.mkdir(parents=True, exist_ok=True)
    for name in (
        "DesktopWindow.cpp",
        "DesktopWindow.h",
        "Jamfile",
        "Login.rdef",
        "LoginApp.cpp",
        "LoginApp.h",
        "LoginView.cpp",
        "LoginView.h",
        "LoginWindow.cpp",
        "LoginWindow.h",
        "R5Desktop.cpp",
        "SessionLog.h",
        "main.cpp",
    ):
        shutil.copy2(SRC / name, dest / name)
        (dest / name).touch()
        print(f"copied {name} -> {dest}")

    users = ROOT / "distro" / "users"
    for name in (
        "AccountAdmin.cpp",
        "AccountAdmin.h",
        "HomeLock.cpp",
        "HomeLock.h",
    ):
        shutil.copy2(users / name, dest / name)
        (dest / name).touch()
        print(f"copied {name} -> {dest}")


def patch_launch() -> None:
    replace_once(
        HAIKU / "data" / "launch" / "user",
        "target first_boot {\n"
        "	job x-vnd.Haiku-FirstBootPrompt\n"
        "}\n",
        "target login {\n"
        "	job x-vnd.Haiku-Login {\n"
        "		launch /system/apps/Login\n"
        "		on initial_volumes_mounted\n"
        "		legacy\n"
        "	}\n"
        "}\n"
        "\n"
        "target first_boot {\n"
        "	job x-vnd.Haiku-FirstBootPrompt\n"
        "}\n",
        "login target",
    )
    replace_once(
        HAIKU / "data" / "launch" / "user",
        "	else {\n"
        "		desktop\n"
        "	}\n"
        "}\n",
        "	else {\n"
        "		login\n"
        "	}\n"
        "}\n",
        "run login not desktop",
    )
    launch = HAIKU / "data" / "launch" / "user"
    text = launch.read_text(encoding="utf-8")
    unwrapped = (
        text.replace("/system/bin/r5-desktop ", "")
        .replace("/system/apps/r5-desktop ", "")
    )
    if unwrapped != text:
        launch.write_text(unwrapped, encoding="utf-8")
        print("unwrapped user (r5-desktop prefixes)")
    else:
        print("already user (no r5-desktop wrap)")


def patch_first_boot() -> None:
    replace_once(
        HAIKU / "src" / "apps" / "firstbootprompt" / "BootPrompt.cpp",
        '\t\t\tBLaunchRoster().Target("desktop");\n',
        '\t\t\tBLaunchRoster().Target("login");\n',
        "Try R5OS starts login",
    )


def patch_deskbar() -> None:
    replace_once(
        HAIKU / "src" / "apps" / "deskbar" / "BarSettings.h",
        "const uint32 kForceQuitTeam = 'FrQt';\n",
        "const uint32 kForceQuitTeam = 'FrQt';\n"
        "const uint32 kLogoutSession = 'lout';\n",
        "Deskbar logout message",
    )
    replace_once(
        HAIKU / "src" / "apps" / "deskbar" / "DeskbarMenu.cpp",
        "	AddSeparatorItem();\n"
        "\n"
        "	BMenu* shutdownMenu = new BMenu(B_TRANSLATE(\"Shutdown\" B_UTF8_ELLIPSIS));\n",
        "	AddSeparatorItem();\n"
        "\n"
        "	item = new BMenuItem(B_TRANSLATE(\"Log out\"),\n"
        "		new BMessage(kLogoutSession));\n"
        "	item->SetEnabled(!dragging);\n"
        "	item->SetTarget(be_app);\n"
        "	AddItem(item);\n"
        "\n"
        "	BMenu* shutdownMenu = new BMenu(B_TRANSLATE(\"Shutdown\" B_UTF8_ELLIPSIS));\n",
        "Deskbar Log out item",
    )
    replace_once(
        HAIKU / "src" / "apps" / "deskbar" / "DeskbarMenu.cpp",
        "				case kRebootSystem:\n"
        "				case kSuspendSystem:\n"
        "				case kShutdownSystem:\n",
        "				case kRebootSystem:\n"
        "				case kSuspendSystem:\n"
        "				case kShutdownSystem:\n"
        "				case kLogoutSession:\n",
        "Deskbar Log out target",
    )
    replace_once(
        HAIKU / "src" / "apps" / "deskbar" / "BarApp.cpp",
        "#include <Roster.h>\n",
        "#include <LaunchRoster.h>\n"
        "#include <Roster.h>\n",
        "Deskbar LaunchRoster include",
    )
    replace_once(
        HAIKU / "src" / "apps" / "deskbar" / "BarApp.cpp",
        "		case kShowSplash:\n"
        "			run_be_about();\n"
        "			break;\n",
        "		case kLogoutSession:\n"
        "		{\n"
        "			BAlert* alert = new BAlert(\"Log out\",\n"
        "				\"Log out of this session?\",\n"
        "				\"Cancel\", \"Log out\");\n"
        "			alert->SetShortcut(0, B_ESCAPE);\n"
        "			if (alert->Go() != 1)\n"
        "				break;\n"
        "\n"
        "			BMessenger login(\"application/x-vnd.Haiku-Login\");\n"
        "			if (login.IsValid())\n"
        "				login.SendMessage(kLogoutSession);\n"
        "			else {\n"
        "				BLaunchRoster roster;\n"
        "				roster.StopTarget(\"desktop\");\n"
        "				roster.Target(\"login\");\n"
        "			}\n"
        "			break;\n"
        "		}\n"
        "\n"
        "		case kShowSplash:\n"
        "			run_be_about();\n"
        "			break;\n",
        "Deskbar Log out handler",
    )


def patch_installer() -> None:
    replace_once(
        HAIKU / "src" / "apps" / "installer" / "InstallerApp.cpp",
        "		} else {\n"
        "			// Return to FirstBootPrompt if the user hasn't\n"
        "			// installed Haiku yet\n"
        "			be_roster->Launch(\"application/x-vnd.Haiku-FirstBootPrompt\");\n"
        "		}\n",
        "		} else if (!be_roster->IsRunning(\n"
        "				\"application/x-vnd.Haiku-Login\")) {\n"
        "			// Live Try/Install: Installer cancelled and Deskbar is\n"
        "			// not running. Log out also stops Deskbar; if Sign in is\n"
        "			// up, do not reopen FirstBootPrompt.\n"
        "			be_roster->Launch(\"application/x-vnd.Haiku-FirstBootPrompt\");\n"
        "		}\n",
        "Installer skip FirstBootPrompt when Login runs",
    )


def patch_image() -> None:
    regular = HAIKU / "build" / "jam" / "images" / "definitions" / "regular"
    text = regular.read_text(encoding="utf-8")
    if "SYSTEM_BIN += r5-desktop ;" in text:
        text = text.replace("SYSTEM_BIN += r5-desktop ;\n", "")
        regular.write_text(text, encoding="utf-8")
        print("patched regular (drop r5-desktop SYSTEM_BIN)")
    text = regular.read_text(encoding="utf-8")
    if "SYSTEM_APPS += r5-desktop ;\nSYSTEM_APPS += Login ;\n" in text:
        text = text.replace(
            "SYSTEM_APPS += r5-desktop ;\nSYSTEM_APPS += Login ;\n",
            "SYSTEM_APPS += r5-desktop ;\n",
            1,
        )
        regular.write_text(text, encoding="utf-8")
        print("patched regular (drop duplicate Login SYSTEM_APPS)")
    text = regular.read_text(encoding="utf-8")
    if "SYSTEM_APPS += Login ;" in text:
        print("already regular (Login SYSTEM_APPS)")
    else:
        replace_once(
            regular,
            "SYSTEM_APPS += [ FFilterByBuildFeatures\n"
            "	ActivityMonitor\n",
            "SYSTEM_APPS += Login ;\n"
            "SYSTEM_APPS += [ FFilterByBuildFeatures\n"
            "	ActivityMonitor\n",
            "Login SYSTEM_APPS",
        )
    replace_once(
        regular,
        "SYSTEM_APPS += Login ;\n",
        "SYSTEM_APPS += Login ;\n"
        "SYSTEM_APPS += r5-desktop ;\n",
        "r5-desktop SYSTEM_APPS",
    )


def main() -> int:
    copy_sources()
    patch_launch()
    patch_first_boot()
    patch_deskbar()
    patch_installer()
    patch_image()
    return 0


if __name__ == "__main__":
    sys.exit(main())
