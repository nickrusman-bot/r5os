/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Drop to the account named in the session file and start the job as a
 * new team. Exec would keep the team name r5-desktop in Deskbar.
 * The helper then exits. Do not wrap launch_daemon jobs with this
 * binary: a service whose team exits is restarted forever.
 * launch_daemon and Login stay uid 0.
 */


#include "SessionLog.h"

#include <errno.h>
#include <grp.h>
#include <image.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

#include <OS.h>


extern char** environ;


static const char* kSessionPaths[] = {
	"/boot/system/settings/r5-desktop-session",
	"/tmp/r5-desktop-session",
	NULL
};


static bool
read_session_name(char* name, size_t nameSize)
{
	for (int i = 0; kSessionPaths[i] != NULL; i++) {
		FILE* file = fopen(kSessionPaths[i], "r");
		if (file == NULL)
			continue;
		if (fgets(name, nameSize, file) == NULL) {
			fclose(file);
			continue;
		}
		fclose(file);

		size_t length = strlen(name);
		while (length > 0
			&& (name[length - 1] == '\n' || name[length - 1] == '\r'
				|| name[length - 1] == ' ')) {
			name[--length] = '\0';
		}
		if (length > 0)
			return true;
	}
	return false;
}


static void
ensure_user_cache(const struct passwd* pwd)
{
	if (pwd->pw_dir == NULL || pwd->pw_dir[0] == '\0')
		return;

	char path[B_PATH_NAME_LENGTH];
	int written = snprintf(path, sizeof(path), "%s/config/cache", pwd->pw_dir);
	if (written <= 0 || (size_t)written >= sizeof(path))
		return;

	// The image creates this directory as root, mode 0755. Mesa then
	// cannot create mesa_shader_cache for the session user.
	if (mkdir(path, 0700) != 0 && errno != EEXIST) {
		r5_session_log("cache mkdir %s: %s", path, strerror(errno));
		return;
	}
	if (chown(path, pwd->pw_uid, pwd->pw_gid) != 0)
		r5_session_log("cache chown %s: %s", path, strerror(errno));
	if (chmod(path, 0700) != 0)
		r5_session_log("cache chmod %s: %s", path, strerror(errno));
}


int
main(int argc, char** argv)
{
	if (argc < 2 || argv[1] == NULL || argv[1][0] == '\0') {
		r5_session_log("missing command");
		syslog(LOG_ERR, "r5-desktop: missing command");
		return 1;
	}

	char name[64];
	if (!read_session_name(name, sizeof(name))) {
		r5_session_log("no session file for %s", argv[1]);
		syslog(LOG_ERR, "r5-desktop: no session file");
		return 1;
	}

	struct passwd* pwd = getpwnam(name);
	if (pwd == NULL || strcmp(pwd->pw_name, name) != 0 || pwd->pw_uid == 0) {
		r5_session_log("session user %s uid=%d not allowed", name,
			pwd != NULL ? (int)pwd->pw_uid : -1);
		syslog(LOG_ERR, "r5-desktop: session user %s is not allowed", name);
		return 1;
	}

	r5_session_log("session %s uid=%d gid=%d cmd=%s", name, (int)pwd->pw_uid,
		(int)pwd->pw_gid, argv[1]);

	if (pwd->pw_dir != NULL && pwd->pw_dir[0] != '\0')
		setenv("HOME", pwd->pw_dir, 1);
	setenv("USER", pwd->pw_name, 1);
	setenv("LOGNAME", pwd->pw_name, 1);
	if (pwd->pw_shell != NULL && pwd->pw_shell[0] != '\0')
		setenv("SHELL", pwd->pw_shell, 1);

	ensure_user_cache(pwd);

	if (setresgid(pwd->pw_gid, pwd->pw_gid, pwd->pw_gid) != 0) {
		r5_session_log("setresgid: %s", strerror(errno));
		syslog(LOG_ERR, "r5-desktop: setresgid: %s", strerror(errno));
		return 1;
	}
	if (initgroups(pwd->pw_name, pwd->pw_gid) != 0)
		r5_session_log("initgroups: %s", strerror(errno));
	if (setresuid(pwd->pw_uid, pwd->pw_uid, pwd->pw_uid) != 0
		|| geteuid() == 0) {
		r5_session_log("setresuid: %s euid=%d", strerror(errno),
			(int)geteuid());
		syslog(LOG_ERR, "r5-desktop: setresuid: %s", strerror(errno));
		return 1;
	}

	r5_session_log("dropped uid=%d euid=%d, load_image %s", (int)getuid(),
		(int)geteuid(), argv[1]);

	thread_id thread = load_image(argc - 1, (const char**)(argv + 1),
		(const char**)environ);
	if (thread < B_OK) {
		r5_session_log("load_image %s: %s", argv[1], strerror(thread));
		syslog(LOG_ERR, "r5-desktop: load_image %s: %s", argv[1],
			strerror(thread));
		return 1;
	}
	resume_thread(thread);
	r5_session_log("started team thread %d for %s", (int)thread, argv[1]);
	return 0;
}
