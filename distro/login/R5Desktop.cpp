/*
 * Launch a desktop job as the administrator from the session file.
 * launch_daemon stays uid 0; this helper drops before exec.
 */


#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


static const char* kSessionPath = "/boot/system/var/r5-desktop-session";


int
main(int argc, char** argv)
{
	if (argc < 2 || argv[1] == NULL || argv[1][0] == '\0') {
		fprintf(stderr, "r5-desktop: missing command\n");
		return 1;
	}

	if (geteuid() != 0) {
		execv(argv[1], argv + 1);
		fprintf(stderr, "r5-desktop: exec %s: %s\n", argv[1], strerror(errno));
		return 1;
	}

	FILE* file = fopen(kSessionPath, "r");
	if (file == NULL) {
		fprintf(stderr, "r5-desktop: no session file\n");
		return 1;
	}

	char name[64];
	if (fgets(name, sizeof(name), file) == NULL) {
		fclose(file);
		fprintf(stderr, "r5-desktop: empty session file\n");
		return 1;
	}
	fclose(file);

	size_t length = strlen(name);
	while (length > 0
		&& (name[length - 1] == '\n' || name[length - 1] == '\r'
			|| name[length - 1] == ' ')) {
		name[--length] = '\0';
	}
	if (length == 0) {
		fprintf(stderr, "r5-desktop: blank session user\n");
		return 1;
	}

	struct passwd* pwd = getpwnam(name);
	if (pwd == NULL || strcmp(pwd->pw_name, name) != 0 || pwd->pw_uid == 0) {
		fprintf(stderr, "r5-desktop: session user %s is not allowed\n", name);
		return 1;
	}

	if (pwd->pw_dir != NULL && pwd->pw_dir[0] != '\0')
		setenv("HOME", pwd->pw_dir, 1);
	setenv("USER", pwd->pw_name, 1);
	setenv("LOGNAME", pwd->pw_name, 1);
	if (pwd->pw_shell != NULL && pwd->pw_shell[0] != '\0')
		setenv("SHELL", pwd->pw_shell, 1);

	if (setgid(pwd->pw_gid) != 0
		|| initgroups(pwd->pw_name, pwd->pw_gid) != 0
		|| setuid(pwd->pw_uid) != 0 || geteuid() == 0) {
		fprintf(stderr, "r5-desktop: could not drop privileges\n");
		return 1;
	}

	execv(argv[1], argv + 1);
	fprintf(stderr, "r5-desktop: exec %s: %s\n", argv[1], strerror(errno));
	return 1;
}
