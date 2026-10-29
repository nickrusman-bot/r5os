#!/usr/bin/env python3
"""Users & Accounts preference. Idempotent build-tool copy."""
from __future__ import annotations

import pathlib
import shutil
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")
ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "distro" / "users"


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
    dest = HAIKU / "src" / "preferences" / "users"
    dest.mkdir(parents=True, exist_ok=True)
    for name in (
        "AccountAdmin.cpp",
        "AccountAdmin.h",
        "AdminLock.cpp",
        "AdminLock.h",
        "HomeLock.cpp",
        "HomeLock.h",
        "Users.cpp",
        "Users.rdef",
        "Jamfile",
    ):
        shutil.copy2(SRC / name, dest / name)
        (dest / name).touch()
        print(f"copied {name} -> {dest}")


def patch_jams() -> None:
    replace_once(
        HAIKU / "src" / "preferences" / "Jamfile",
        "SubInclude HAIKU_TOP src preferences virtualmemory ;\n",
        "SubInclude HAIKU_TOP src preferences virtualmemory ;\n"
        "SubInclude HAIKU_TOP src preferences users ;\n",
        "users jam",
    )
    regular = HAIKU / "build" / "jam" / "images" / "definitions" / "regular"
    text = regular.read_text(encoding="utf-8")
    if "SYSTEM_PREFERENCES += Users ;" not in text:
        replace_once(
            regular,
            "SYSTEM_PREFERENCES += [ FFilterByBuildFeatures\n"
            "	Bluetooth\n",
            "SYSTEM_PREFERENCES += Users ;\n"
            "SYSTEM_PREFERENCES += [ FFilterByBuildFeatures\n"
            "	Bluetooth\n",
            "users SYSTEM_PREFERENCES",
        )
        text = regular.read_text(encoding="utf-8")
    else:
        print("already regular (Users SYSTEM_PREFERENCES)")
    if "SYSTEM_PREFERENCES += Joysticks ;" in text:
        print("already regular (Joysticks SYSTEM_PREFERENCES)")
        return
    replace_once(
        regular,
        "SYSTEM_PREFERENCES += Users ;\n",
        "SYSTEM_PREFERENCES += Users ;\n"
        "SYSTEM_PREFERENCES += Joysticks ;\n",
        "joysticks SYSTEM_PREFERENCES",
    )


def install_lock(folder: str) -> None:
    dest = HAIKU / "src" / "preferences" / folder
    dest.mkdir(parents=True, exist_ok=True)
    for name in ("AdminLock.cpp", "AdminLock.h", "AccountAdmin.h"):
        shutil.copy2(SRC / name, dest / name)
        (dest / name).touch()
        print(f"copied {name} -> {dest}")


def retarget_lock(path: pathlib.Path, current: str, desired: str, original: str, tag: str) -> None:
    text = path.read_text(encoding="utf-8")
    if desired in text:
        print(f"already {path.name} ({tag})")
        return
    if current in text:
        path.write_text(text.replace(current, desired, 1), encoding="utf-8")
        print(f"patched {path.name} ({tag})")
        return
    if original in text:
        path.write_text(text.replace(original, desired, 1), encoding="utf-8")
        print(f"patched {path.name} ({tag})")
        return
    raise SystemExit(f"pattern not found in {path} ({tag})")


def patch_locks() -> None:
    install_lock("time")
    install_lock("screen")
    install_lock("network")

    replace_once(
        HAIKU / "src/preferences/time/Jamfile",
        "	ZoneView.cpp\n	;",
        "	ZoneView.cpp\n	AdminLock.cpp\n	;",
        "time AdminLock",
    )
    replace_once(
        HAIKU / "src/preferences/screen/Jamfile",
        "	Utility.cpp\n	: be",
        "	Utility.cpp\n	AdminLock.cpp\n	: be",
        "screen AdminLock",
    )
    replace_once(
        HAIKU / "src/preferences/network/Jamfile",
        "	WirelessNetworkMenuItem.cpp\n\n	: <src!bin!network!ifconfig>MediaTypes.o",
        "	WirelessNetworkMenuItem.cpp\n	AdminLock.cpp\n\n	: <src!bin!network!ifconfig>MediaTypes.o",
        "network AdminLock",
    )

    for name in ("BaseView.cpp", "DateTimeView.cpp", "ntp.cpp", "ZoneView.cpp", "TimeWindow.cpp"):
        path = HAIKU / "src/preferences/time" / name
        text = path.read_text(encoding="utf-8")
        if '#include "AdminLock.h"' not in text:
            path.write_text('#include "AdminLock.h"\n' + text, encoding="utf-8")
            print(f"patched {name} (include AdminLock)")
        else:
            print(f"already {name} (include AdminLock)")

    replace_once(
        HAIKU / "src/preferences/time/BaseView.cpp",
        "	set_real_time_clock(dateTime.Time_t());",
        "	r5_admin_set_clock(dateTime.Time_t());",
        "time base clock",
    )
    replace_once(
        HAIKU / "src/preferences/time/DateTimeView.cpp",
        "	set_real_time_clock(dateTime.Time_t());",
        "	r5_admin_set_clock(dateTime.Time_t());",
        "time revert clock",
    )
    replace_once(
        HAIKU / "src/preferences/time/ntp.cpp",
        "	set_real_time_clock(now);",
        "	r5_admin_set_clock(now);",
        "time ntp clock",
    )
    replace_once(
        HAIKU / "src/preferences/time/ZoneView.cpp",
        "	_kern_set_timezone(timeZone.OffsetFromGMT(), timeZone.ID().String(),\n"
        "		timeZone.ID().Length());",
        "	r5_admin_set_timezone(timeZone.OffsetFromGMT(), timeZone.ID().String());",
        "time zone",
    )
    replace_once(
        HAIKU / "src/preferences/time/ZoneView.cpp",
        "	_kern_set_real_time_clock_is_gmt(fUseGmtTime);",
        "	r5_admin_set_clock_gmt(fUseGmtTime);",
        "time gmt",
    )
    time_current = (
        "	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)\n"
        "		.SetInsets(0, B_USE_DEFAULT_SPACING, 0, 0)\n"
        "		.Add(new AdminLockBar())\n"
        "		.Add(fBaseView)\n"
        "		.Add(new BSeparatorView(B_HORIZONTAL))\n"
        "		.AddGroup(B_HORIZONTAL)\n"
        "			.Add(fRevertButton)\n"
        "			.SetInsets(B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING,\n"
        "				B_USE_DEFAULT_SPACING, B_USE_WINDOW_SPACING);\n"
    )
    time_desired = (
        "	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)\n"
        "		.SetInsets(0, B_USE_DEFAULT_SPACING, 0, 0)\n"
        "		.Add(fBaseView)\n"
        "		.Add(new BSeparatorView(B_HORIZONTAL))\n"
        "		.AddGroup(B_HORIZONTAL)\n"
        "			.Add(fRevertButton)\n"
        "			.AddGlue()\n"
        "			.Add(new AdminLockBar())\n"
        "			.SetInsets(B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING,\n"
        "				B_USE_DEFAULT_SPACING, B_USE_WINDOW_SPACING);\n"
    )
    time_original = time_current.replace("\t\t.Add(new AdminLockBar())\n", "", 1)
    retarget_lock(
        HAIKU / "src/preferences/time/TimeWindow.cpp",
        time_current,
        time_desired,
        time_original,
        "time lock bar",
    )

    screen = HAIKU / "src/preferences/screen/ScreenWindow.cpp"
    text = screen.read_text(encoding="utf-8")
    if '#include "AdminLock.h"' not in text:
        screen.write_text(text.replace(
            '#include "ScreenWindow.h"\n',
            '#include "ScreenWindow.h"\n\n#include "AdminLock.h"\n',
            1,
        ), encoding="utf-8")
        print("patched ScreenWindow.cpp (include AdminLock)")
    else:
        print("already ScreenWindow.cpp (include AdminLock)")
    screen_current = (
        "	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)\n"
        "		.Add(new AdminLockBar())\n"
        "		.AddGroup(B_HORIZONTAL)\n"
        "			.AddGroup(B_VERTICAL, 0, 1)\n"
        "				.AddStrut(floorf(controlsBox->TopBorderOffset()\n"
        "					- fScreenBox->TopBorderOffset()))\n"
        "				.Add(fScreenBox)\n"
        "				.Add(workspacesView)\n"
        "				.End()\n"
        "			.AddGroup(B_VERTICAL, 0, 1)\n"
        "				.Add(controlsBox, 2)\n"
        "				.End()\n"
        "			.End()\n"
        "		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)\n"
        "			.Add(fRevertButton)\n"
        "			.AddGlue()\n"
        "			.End()\n"
        "		.SetInsets(B_USE_WINDOW_SPACING);\n"
    )
    screen_desired = screen_current.replace(
        "\t\t.Add(new AdminLockBar())\n", "", 1
    ).replace(
        "\t\t\t.Add(fRevertButton)\n\t\t\t.AddGlue()\n\t\t\t.End()\n",
        "\t\t\t.Add(fRevertButton)\n\t\t\t.AddGlue()\n"
        "\t\t\t.Add(new AdminLockBar())\n\t\t\t.End()\n",
        1,
    )
    screen_original = screen_current.replace("\t\t.Add(new AdminLockBar())\n", "", 1)
    retarget_lock(screen, screen_current, screen_desired, screen_original, "screen lock bar")
    replace_once(
        screen,
        "void\nScreenWindow::_Apply()\n{\n"
        "	// make checkpoint, so we can undo these changes\n",
        "void\nScreenWindow::_Apply()\n{\n"
        "	if (!r5_admin_change_allowed()) {\n"
        "		BAlert* alert = new BAlert(\"Screen\",\n"
        "			\"Unlock this with an administrator password.\", \"OK\");\n"
        "		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);\n"
        "		alert->Go();\n"
        "		return;\n"
        "	}\n\n"
        "	// make checkpoint, so we can undo these changes\n",
        "screen apply lock",
    )

    network = HAIKU / "src/preferences/network/NetworkWindow.cpp"
    text = network.read_text(encoding="utf-8")
    if '#include "AdminLock.h"' not in text:
        network.write_text(text.replace(
            '#include "NetworkWindow.h"\n',
            '#include "NetworkWindow.h"\n\n#include "AdminLock.h"\n',
            1,
        ), encoding="utf-8")
        print("patched NetworkWindow.cpp (include AdminLock)")
    else:
        print("already NetworkWindow.cpp (include AdminLock)")
    network_current = (
        "	BLayoutBuilder::Group<>(this, B_VERTICAL)\n"
        "		.SetInsets(B_USE_WINDOW_SPACING)\n"
        "		.Add(new AdminLockBar())\n"
        "\n"
        "#if ENABLE_PROFILES\n"
        "		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)\n"
        "			.Add(profilesMenuField)\n"
        "			.AddGlue()\n"
        "		.End()\n"
        "#endif\n"
        "		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)\n"
        "			.Add(scrollView)\n"
        "			.Add(fAddOnShellView)\n"
        "		.End()\n"
        "\n"
        "		.Add(showReplicantCheckBox)\n"
        "		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)\n"
        "			.Add(fRevertButton)\n"
        "			.AddGlue()\n"
        "		.End();\n"
    )
    network_desired = network_current.replace(
        "\t\t.Add(new AdminLockBar())\n", "", 1
    ).replace(
        "\t\t\t.Add(fRevertButton)\n\t\t\t.AddGlue()\n\t\t.End();\n",
        "\t\t\t.Add(fRevertButton)\n\t\t\t.AddGlue()\n"
        "\t\t\t.Add(new AdminLockBar())\n\t\t.End();\n",
        1,
    )
    network_original = network_current.replace("\t\t.Add(new AdminLockBar())\n", "", 1)
    retarget_lock(
        network, network_current, network_desired, network_original, "network lock bar"
    )

    settings = HAIKU / "src/kits/network/libnetapi/NetworkSettings.cpp"
    replace_once(
        settings,
        "#include <Directory.h>\n",
        "#include <Directory.h>\n#include <Messenger.h>\n",
        "network settings messenger",
    )
    replace_once(
        settings,
        "status_t\nBNetworkSettings::_Save(const char* name)\n{\n\tBPath path;\n",
        "static bool\nr5_network_save_allowed()\n{\n"
        "\tif (geteuid() == 0)\n\t\treturn true;\n"
        "\tBMessenger login(\"application/x-vnd.Haiku-Login\");\n"
        "\tif (!login.IsValid())\n\t\treturn false;\n"
        "\tBMessage request('r5aq');\n"
        "\tBMessage reply;\n"
        "\tif (login.SendMessage(&request, &reply) != B_OK)\n\t\treturn false;\n"
        "\tbool unlocked = false;\n"
        "\treply.FindBool(\"unlocked\", &unlocked);\n"
        "\treturn unlocked;\n"
        "}\n\n\n"
        "status_t\nBNetworkSettings::_Save(const char* name)\n{\n"
        "\tif (!r5_network_save_allowed())\n"
        "\t\treturn B_PERMISSION_DENIED;\n\n"
        "\tBPath path;\n",
        "network save lock",
    )
    text = settings.read_text(encoding="utf-8")
    if "#include <unistd.h>" not in text:
        settings.write_text(text.replace(
            "#include <string.h>\n",
            "#include <string.h>\n#include <unistd.h>\n",
            1,
        ), encoding="utf-8")
        print("patched NetworkSettings.cpp (unistd)")

    server = HAIKU / "src/servers/app/ServerApp.cpp"
    text = server.read_text(encoding="utf-8")
    if "r5_screen_change_allowed" not in text:
        helper = '''
#include <OS.h>
#include <grp.h>
#include <pwd.h>
#include <time.h>


static bool
r5_screen_change_allowed(team_id team)
{
	team_info info;
	if (get_team_info(team, &info) != B_OK)
		return false;
	if (info.uid == 0)
		return true;

	struct passwd* pwd = getpwuid(info.uid);
	if (pwd != NULL && pwd->pw_dir != NULL
		&& strcmp(pwd->pw_dir, "/boot/home") == 0) {
		return true;
	}
	if (pwd != NULL && pwd->pw_name != NULL) {
		const char* groups[] = { "admin", "wheel", NULL };
		for (int32 i = 0; groups[i] != NULL; i++) {
			struct group* group = getgrnam(groups[i]);
			if (group == NULL || group->gr_mem == NULL)
				continue;
			for (int32 member = 0; group->gr_mem[member] != NULL; member++) {
				if (strcmp(group->gr_mem[member], pwd->pw_name) == 0)
					return true;
			}
		}
	}

	FILE* file = fopen("/boot/system/settings/r5-machine-unlock", "r");
	if (file == NULL)
		return false;
	bool allowed = false;
	char line[128];
	while (fgets(line, (int)sizeof(line), file) != NULL) {
		long granted = 0;
		long long until = 0;
		if (sscanf(line, "team %ld until %lld", &granted, &until) == 2
			&& (team_id)granted == team && (time_t)until > time(NULL)) {
			allowed = true;
		}
	}
	fclose(file);
	return allowed;
}


'''
        server.write_text(text.replace(
            '#include "ServerApp.h"\n',
            '#include "ServerApp.h"\n' + helper,
            1,
        ), encoding="utf-8")
        print("patched ServerApp.cpp (screen lock helper)")
    else:
        print("already ServerApp.cpp (screen lock helper)")
    replace_once(
        server,
        "			if (status == B_OK) {\n"
        "				status = fDesktop->SetScreenMode(workspace, id, mode,\n"
        "					makeDefault);\n"
        "			}\n",
        "			if (status == B_OK && !r5_screen_change_allowed(ClientTeam()))\n"
        "				status = B_PERMISSION_DENIED;\n\n"
        "			if (status == B_OK) {\n"
        "				status = fDesktop->SetScreenMode(workspace, id, mode,\n"
        "					makeDefault);\n"
        "			}\n",
        "screen mode lock",
    )


def main() -> int:
    copy_sources()
    patch_jams()
    patch_locks()
    return 0


if __name__ == "__main__":
    sys.exit(main())
