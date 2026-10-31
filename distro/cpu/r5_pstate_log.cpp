/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Read /dev/misc/r5_pstate twice and leave the text where a USB stick's
 * FAT partition can be opened from another computer. No MSR is written.
 */


#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <Drivers.h>
#include <OS.h>
#include <fs_volume.h>


static const char* kHomeReport = "/boot/home/r5-cpu.txt";
static const char* kMountPoint = "/boot/system/cache/r5-esp";


static bool
read_report(char* buffer, size_t capacity)
{
	if (capacity == 0)
		return false;

	int fd = open("/dev/misc/r5_pstate", O_RDONLY);
	if (fd < 0)
		return false;

	ssize_t got = read(fd, buffer, capacity - 1);
	close(fd);
	if (got < 0)
		return false;
	buffer[got] = '\0';
	return true;
}


static void
burn_cpu()
{
	volatile uint64 sink = 0;
	bigtime_t start = system_time();
	while (system_time() - start < 1000000)
		sink += 1;
	(void)sink;
}


static bool
looks_like_fat(const uint8* sector, bool* fat32)
{
	if (sector[510] != 0x55 || sector[511] != 0xaa)
		return false;

	*fat32 = memcmp(sector + 0x52, "FAT32   ", 8) == 0;
	if (*fat32)
		return true;
	if (memcmp(sector + 0x36, "FAT16   ", 8) == 0)
		return true;
	if (memcmp(sector + 0x36, "FAT12   ", 8) == 0)
		return true;
	if (memcmp(sector + 0x36, "FAT     ", 8) == 0)
		return true;
	return false;
}


struct fat_choice {
	char	path[256];
	off_t	size;
	bool	fat32;
	bool	found;
};


static void
consider(const char* path, fat_choice* best)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return;

	uint8 sector[512];
	ssize_t got = read(fd, sector, sizeof(sector));
	partition_info info;
	memset(&info, 0, sizeof(info));
	status_t status = ioctl(fd, B_GET_PARTITION_INFO, &info, sizeof(info));
	close(fd);
	if (got != ssize_t(sizeof(sector)))
		return;

	bool fat32 = false;
	if (!looks_like_fat(sector, &fat32))
		return;

	off_t size = status == B_OK && info.size > 0 ? info.size
		: (off_t(1) << 62);
	bool better = !best->found || (fat32 && !best->fat32)
		|| (fat32 == best->fat32 && size < best->size);
	if (!better)
		return;

	strlcpy(best->path, path, sizeof(best->path));
	best->size = size;
	best->fat32 = fat32;
	best->found = true;
}


static void
walk(const char* path, int depth, fat_choice* best)
{
	if (depth > 6)
		return;

	DIR* dir = opendir(path);
	if (dir == NULL)
		return;

	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0
			|| strcmp(entry->d_name, "..") == 0)
			continue;

		char child[512];
		int wrote = snprintf(child, sizeof(child), "%s/%s", path,
			entry->d_name);
		if (wrote < 0 || wrote >= int(sizeof(child)))
			continue;

		struct stat st;
		if (stat(child, &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode))
			walk(child, depth + 1, best);
		else if (S_ISCHR(st.st_mode))
			consider(child, best);
	}
	closedir(dir);
}


static void
write_text(const char* path, const char* text)
{
	FILE* file = fopen(path, "w");
	if (file == NULL) {
		fprintf(stderr, "r5_pstate_log: open %s: %s\n", path,
			strerror(errno));
		return;
	}
	fputs(text, file);
	fclose(file);
	fprintf(stderr, "r5_pstate_log: wrote %s\n", path);
}


int
main()
{
	char idle[4096];
	char busy[4096];
	strcpy(idle, "R5 CPU: device missing\n");
	strcpy(busy, "R5 CPU: device missing\n");

	for (int attempt = 0; attempt < 20; attempt++) {
		if (read_report(idle, sizeof(idle)))
			break;
		snooze(500000);
	}

	snooze(500000);
	read_report(idle, sizeof(idle));
	burn_cpu();
	if (!read_report(busy, sizeof(busy)))
		strcpy(busy, "R5 CPU: second read failed\n");

	char report[9000];
	snprintf(report, sizeof(report),
		"R5 CPU log\n"
		"sample: after a short wait\n"
		"%s"
		"sample: after one second busy on this thread\n"
		"%s",
		idle, busy);
	write_text(kHomeReport, report);

	fat_choice best;
	memset(&best, 0, sizeof(best));
	best.size = off_t(1) << 62;
	for (int attempt = 0; attempt < 20 && !best.found; attempt++) {
		walk("/dev/disk", 0, &best);
		if (!best.found)
			snooze(500000);
	}

	char note[512];
	if (!best.found) {
		snprintf(note, sizeof(note),
			"\nFAT: no FAT partition found under /dev/disk\n");
	} else {
		mkdir("/boot/system/cache", 0755);
		mkdir(kMountPoint, 0755);
		dev_t volume = fs_mount_volume(kMountPoint, best.path, "fat", 0,
			NULL);
		if (volume < 0) {
			snprintf(note, sizeof(note),
				"\nFAT: %s mount failed: %s\n", best.path,
				strerror(volume));
		} else {
			char fatPath[320];
			snprintf(fatPath, sizeof(fatPath), "%s/r5-cpu.txt", kMountPoint);
			write_text(fatPath, report);
			status_t unmount = fs_unmount_volume(kMountPoint, 0);
			snprintf(note, sizeof(note),
				"\nFAT: wrote %s on %s (unmount %" B_PRId32 ")\n",
				fatPath, best.path, unmount);
		}
	}

	FILE* home = fopen(kHomeReport, "a");
	if (home != NULL) {
		fputs(note, home);
		fclose(home);
	}
	fprintf(stderr, "%s", note);
	return 0;
}
