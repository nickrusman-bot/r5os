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
        "LockWindow.cpp",
        "LockWindow.h",
        "LoginApp.cpp",
        "LoginApp.h",
        "LoginView.cpp",
        "LoginView.h",
        "LoginWindow.cpp",
        "LoginWindow.h",
        "R5Desktop.cpp",
        "SessionLog.h",
        "TrayApp.cpp",
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
    disarm_desktop_target()
    patch_autologin()
    copy_first_login_script()


def patch_autologin() -> None:
    """Keep autologin. It starts the user launch_daemon, which starts
    app_server and Login. Without it the boot splash never leaves."""
    path = HAIKU / "data" / "launch" / "system"
    text = path.read_text(encoding="utf-8")
    job = (
        "# target login\n"
        "job x-vnd.Haiku-autologin {\n"
        "	launch /system/bin/autologin\n"
        "}\n"
    )
    removed = (
        "# R5OS Login owns Sign in. autologin StartSession spawned\n"
        "# launch_daemon as uid 0 (user, then root) and raced Tracker.\n"
    )
    if job in text:
        print("already system (autologin job)")
        return
    if removed in text:
        path.write_text(text.replace(removed, job, 1), encoding="utf-8")
        print("patched system (restore autologin job)")
        return
    raise SystemExit(f"autologin job not found in {path}")


def copy_first_login_script() -> None:
    dest = HAIKU / "data" / "system" / "boot" / "first_login" / "default_deskbar_items.sh"
    dest.parent.mkdir(parents=True, exist_ok=True)
    text = (SRC / "default_deskbar_items.sh").read_text(encoding="utf-8")
    dest.write_text(text.replace("\r\n", "\n").replace("\r", "\n"),
        encoding="utf-8", newline="\n")
    dest.touch()
    print(f"copied default_deskbar_items.sh -> {dest}")


def disarm_desktop_target() -> None:
    """Desktop jobs must not start as the autologin uid (still 0)."""
    launch = HAIKU / "data" / "launch" / "user"
    text = launch.read_text(encoding="utf-8")
    start = text.find("target desktop {")
    if start < 0:
        raise SystemExit(f"target desktop not found in {launch}")
    end = text.find("\ntarget ", start + 1)
    if end < 0:
        raise SystemExit(f"target after desktop not found in {launch}")
    block = text[start:end]
    needle = "\n\t\ton initial_volumes_mounted\n"
    count = block.count(needle)
    if count == 0:
        print("already desktop (no initial_volumes_mounted)")
        return
    block = block.replace(needle, "\n")
    launch.write_text(text[:start] + block + text[end:], encoding="utf-8")
    print(f"desktop jobs: removed {count} initial_volumes_mounted")


def patch_session_home() -> None:
    """autologin StartSession(getpwuid(0)). After migrate that name is root
    with home /boot/system (packagefs). The user launch_daemon then never
    paints Sign in and the boot splash stays."""
    daemon = HAIKU / "src" / "servers" / "launch" / "LaunchDaemon.cpp"
    replace_once(
        daemon,
        "#include <unistd.h>\n",
        "#include <unistd.h>\n"
        "#include <string.h>\n"
        "#include <sys/stat.h>\n",
        "launch_daemon root home includes",
    )
    replace_once(
        daemon,
        "	if (passwd->pw_dir != NULL && passwd->pw_dir[0] != '\\0') {\n"
        "		setenv(\"HOME\", passwd->pw_dir, true);\n"
        "\n"
        "		if (chdir(passwd->pw_dir) != 0) {\n"
        "			debug_printf(\"Could not switch to home dir %s: %s\\n\",\n"
        "				passwd->pw_dir, strerror(errno));\n",
        "	const char* home = passwd->pw_dir;\n"
        "	char rootHome[] = \"/boot/system/var/root\";\n"
        "	if (user == 0 && (home == NULL || home[0] == '\\0'\n"
        "			|| strcmp(home, \"/boot/system\") == 0\n"
        "			|| access(home, W_OK) != 0)) {\n"
        "		mkdir(\"/boot/system/var\", 0755);\n"
        "		mkdir(rootHome, 0700);\n"
        "		home = rootHome;\n"
        "	}\n"
        "	if (home != NULL && home[0] != '\\0') {\n"
        "		setenv(\"HOME\", home, true);\n"
        "\n"
        "		if (chdir(home) != 0) {\n"
        "			debug_printf(\"Could not switch to home dir %s: %s\\n\",\n"
        "				home, strerror(errno));\n",
        "launch_daemon root writable home",
    )


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
        HAIKU / "src" / "apps" / "deskbar" / "BarSettings.h",
        "const uint32 kLogoutSession = 'lout';\n",
        "const uint32 kLogoutSession = 'lout';\n"
        "const uint32 kLockSession = 'lokc';\n",
        "Deskbar lock message",
    )
    replace_once(
        HAIKU / "src" / "apps" / "deskbar" / "DeskbarMenu.cpp",
        "#include <Locale.h>\n",
        "#include <Locale.h>\n"
        "#include <pwd.h>\n"
        "#include <unistd.h>\n",
        "Deskbar logout pwd include",
    )
    logout_plain = (
        "	item = new BMenuItem(B_TRANSLATE(\"Log out\"),\n"
        "		new BMessage(kLogoutSession));\n"
    )
    logout_named = (
        "	BString logoutLabel(B_TRANSLATE(\"Log out\"));\n"
        "	struct passwd* sessionUser = getpwuid(getuid());\n"
        "	if (sessionUser != NULL && sessionUser->pw_name != NULL\n"
        "		&& sessionUser->pw_name[0] != '\\0') {\n"
        "		logoutLabel << ' ' << sessionUser->pw_name;\n"
        "	}\n"
        "	item = new BMenuItem(logoutLabel.String(),\n"
        "		new BMessage(kLogoutSession));\n"
    )
    menu = HAIKU / "src" / "apps" / "deskbar" / "DeskbarMenu.cpp"
    text = menu.read_text(encoding="utf-8")
    if logout_named in text:
        print("already DeskbarMenu.cpp (Deskbar Log out item)")
    elif logout_plain in text:
        menu.write_text(text.replace(logout_plain, logout_named, 1), encoding="utf-8")
        print("patched DeskbarMenu.cpp (Deskbar Log out item)")
    else:
        raise SystemExit("pattern not found in DeskbarMenu.cpp (Deskbar Log out item)")
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
        HAIKU / "src" / "apps" / "deskbar" / "DeskbarMenu.cpp",
        "				case kLogoutSession:\n",
        "				case kLogoutSession:\n"
        "				case kLockSession:\n",
        "Deskbar Lock screen target",
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
    replace_once(
        HAIKU / "src" / "apps" / "deskbar" / "BarApp.cpp",
        "		case kLogoutSession:\n"
        "		{\n"
        "			BAlert* alert = new BAlert(\"Log out\",\n",
        "		case kLockSession:\n"
        "		{\n"
        "			BMessenger login(\"application/x-vnd.Haiku-Login\");\n"
        "			if (login.IsValid())\n"
        "				login.SendMessage(kLockSession);\n"
        "			break;\n"
        "		}\n"
        "\n"
        "		case kLogoutSession:\n"
        "		{\n"
        "			BAlert* alert = new BAlert(\"Log out\",\n",
        "Deskbar Lock screen handler",
    )
    replace_once(
        HAIKU / "src" / "apps" / "deskbar" / "DeskbarMenu.cpp",
        "	BString logoutLabel(B_TRANSLATE(\"Log out\"));\n",
        "	item = new BMenuItem(B_TRANSLATE(\"Lock screen\"),\n"
        "		new BMessage(kLockSession));\n"
        "	item->SetEnabled(!dragging);\n"
        "	item->SetTarget(be_app);\n"
        "	AddItem(item);\n"
        "\n"
        "	BString logoutLabel(B_TRANSLATE(\"Log out\"));\n",
        "Deskbar Lock screen item",
    )
    jam = HAIKU / "src" / "add-ons" / "input_server" / "filters" / "Jamfile"
    text = jam.read_text(encoding="utf-8")
    line = "SubInclude HAIKU_TOP src add-ons input_server filters r5_lock ;\n"
    if line not in text:
        print("already Jamfile (no r5_lock)")
    else:
        jam.write_text(text.replace(line, ""), encoding="utf-8")
        print("patched Jamfile (drop r5_lock)")


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
    replace_once(
        regular,
        "SYSTEM_APPS += r5-desktop ;\n",
        "SYSTEM_APPS += r5-desktop ;\n"
        "SYSTEM_APPS += r5-tray ;\n",
        "r5-tray SYSTEM_APPS",
    )


def patch_session_paths() -> None:
    # Shells source this and would otherwise force every account onto
    # /boot/home, then fail to read that account's profile.
    replace_once(
        HAIKU / "data" / "system" / "boot" / "SetupEnvironment",
        "export HOME=/boot/home\n",
        "export HOME=`finddir B_USER_DIRECTORY`\n"
        "if [ -z \"$HOME\" ]; then\n"
        "\texport HOME=/boot/home\n"
        "fi\n",
        "HOME follows the signed-in account",
    )
    replace_once(
        HAIKU / "src" / "system" / "libroot" / "os" / "find_directory.cpp",
        '\t\t\t\ttemplatePath = "trash"; '
        "// TODO: add suffix for current user\n",
        '\t\t\t\ttemplatePath = "$h/trash";\n',
        "per-account trash",
    )


def main() -> int:
    copy_sources()
    patch_launch()
    patch_session_home()
    patch_first_boot()
    patch_deskbar()
    patch_installer()
    patch_image()
    patch_session_paths()
    return 0


if __name__ == "__main__":
    sys.exit(main())
