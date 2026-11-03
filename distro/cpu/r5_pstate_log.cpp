/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Read /dev/misc/r5_pstate twice and leave the text where a USB stick's
 * FAT partition can be opened from another computer. Also copy the tail
 * of the kernel syslog, a device list, and the early display lines onto
 * that partition. No MSR is written.
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

	// The anyboot EFI partition is FAT12 and does carry "FAT12   ".
	// A sector that lost that string still has a BIOS parameter block.
	uint16 bytesPerSector = sector[11] | (uint16(sector[12]) << 8);
	if (bytesPerSector != 512 || sector[13] == 0 || sector[16] == 0
		|| sector[16] > 2)
		return false;
	return true;
}


struct fat_choice {
	char	path[256];
	off_t	size;
	int		nodes;
	int		opened;
	int		signatures;
	bool	fat32;
	bool	found;
};


static void
consider(const char* path, fat_choice* best)
{
	// The internal NVMe EFI partition is FAT32. Preferring FAT32 wrote
	// the log onto the Windows boot partition on the 5625U. The anyboot
	// EFI partition is the small FAT12 volume on the USB disk.
	if (strstr(path, "/nvme/") != NULL)
		return;

	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return;

	best->opened++;
	uint8 sector[512];
	ssize_t got = read(fd, sector, sizeof(sector));
	partition_info info;
	memset(&info, 0, sizeof(info));
	status_t status = ioctl(fd, B_GET_PARTITION_INFO, &info, sizeof(info));
	close(fd);
	if (got != ssize_t(sizeof(sector)))
		return;
	if (sector[510] == 0x55 && sector[511] == 0xaa)
		best->signatures++;

	bool fat32 = false;
	if (!looks_like_fat(sector, &fat32))
		return;

	off_t size = status == B_OK && info.size > 0 ? info.size
		: (off_t(1) << 62);
	bool better = !best->found || size < best->size;
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
	if (depth > 8)
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
		else {
			// devfs nodes are character devices. A later publish can
			// also show up as a plain file, so both are read.
			best->nodes++;
			consider(child, best);
		}
	}
	closedir(dir);
}


static const char* kSyslogSources[] = {
	"/var/log/syslog",
	"/boot/system/var/log/syslog",
	"/boot/common/var/log/syslog",
	NULL
};


static void
copy_tail(const char* source, const char* destination, off_t maxBytes)
{
	int input = open(source, O_RDONLY);
	if (input < 0)
		return;

	off_t size = lseek(input, 0, SEEK_END);
	if (size < 0) {
		close(input);
		return;
	}

	off_t start = 0;
	if (maxBytes > 0 && size > maxBytes)
		start = size - maxBytes;
	if (lseek(input, start, SEEK_SET) < 0) {
		close(input);
		return;
	}

	int output = open(destination, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (output < 0) {
		fprintf(stderr, "r5_pstate_log: open %s: %s\n", destination,
			strerror(errno));
		close(input);
		return;
	}

	char buffer[8192];
	ssize_t amount;
	while ((amount = read(input, buffer, sizeof(buffer))) > 0) {
		ssize_t written = 0;
		while (written < amount) {
			ssize_t step = write(output, buffer + written, amount - written);
			if (step < 0) {
				fprintf(stderr, "r5_pstate_log: write %s: %s\n", destination,
					strerror(errno));
				close(output);
				close(input);
				return;
			}
			written += step;
		}
	}

	fsync(output);
	close(output);
	close(input);
	fprintf(stderr, "r5_pstate_log: copied tail of %s (%lld bytes) to %s\n",
		source, (long long)size, destination);
}


static const char*
syslog_source()
{
	for (int i = 0; kSyslogSources[i] != NULL; i++) {
		if (access(kSyslogSources[i], R_OK) == 0)
			return kSyslogSources[i];
	}
	return NULL;
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
	fflush(file);
	fsync(fileno(file));
	fclose(file);
	fprintf(stderr, "r5_pstate_log: wrote %s\n", path);
}


static void
append_command(FILE* file, const char* title, const char* command)
{
	fprintf(file, "\n## %s\n", title);
	FILE* pipe = popen(command, "r");
	if (pipe == NULL) {
		fprintf(file, "failed: %s\n", strerror(errno));
		return;
	}

	char buffer[512];
	while (fgets(buffer, sizeof(buffer), pipe) != NULL)
		fputs(buffer, file);
	fprintf(file, "\n# exit %d\n", pclose(pipe));
}


static bool
line_matters(const char* line)
{
	static const char* keys[] = {
		"radeon", "Radeon", "vesa", "VESA", "framebuffer", "frame buffer",
		"VGA", "vga", "PCI", "display", "Display", "app_server", "panic",
		"intel_extreme", "KERN:", "vmware", "virtio_gpu", NULL
	};
	for (int i = 0; keys[i] != NULL; i++) {
		if (strstr(line, keys[i]) != NULL)
			return true;
	}
	return false;
}


static void
append_display_lines(FILE* file, const char* source)
{
	fprintf(file, "\n## syslog lines about display and PCI\n");
	if (source == NULL) {
		fputs("syslog not readable yet\n", file);
		return;
	}

	FILE* input = fopen(source, "r");
	if (input == NULL) {
		fprintf(file, "open %s: %s\n", source, strerror(errno));
		return;
	}

	char line[1024];
	int kept = 0;
	while (fgets(line, sizeof(line), input) != NULL) {
		if (!line_matters(line))
			continue;
		fputs(line, file);
		kept++;
		if (kept >= 4000)
			break;
	}
	fclose(input);
	fprintf(file, "\n# kept %d lines from %s\n", kept, source);
}


static void
write_hardware(const char* path, const char* source)
{
	FILE* file = fopen(path, "w");
	if (file == NULL) {
		fprintf(stderr, "r5_pstate_log: open %s: %s\n", path, strerror(errno));
		return;
	}

	fputs("R5 hardware log\n", file);
	append_command(file, "listdev", "/bin/listdev");
	append_command(file, "kernel images", "/bin/listimage 1");
	append_display_lines(file, source);
	fflush(file);
	fsync(fileno(file));
	fclose(file);
	fprintf(stderr, "r5_pstate_log: wrote %s\n", path);
}


static bool
write_fat(const fat_choice* best, const char* report)
{
	mkdir("/boot/system/cache", 0755);
	mkdir(kMountPoint, 0755);
	dev_t volume = fs_mount_volume(kMountPoint, best->path, "fat", 0, NULL);
	if (volume < 0) {
		fprintf(stderr, "r5_pstate_log: mount %s: %s\n", best->path,
			strerror(volume));
		return false;
	}

	char fatPath[320];
	snprintf(fatPath, sizeof(fatPath), "%s/r5-cpu.txt", kMountPoint);
	write_text(fatPath, report);

	const char* source = syslog_source();
	snprintf(fatPath, sizeof(fatPath), "%s/r5-hardware.txt", kMountPoint);
	write_hardware(fatPath, source);
	if (source != NULL) {
		snprintf(fatPath, sizeof(fatPath), "%s/r5-syslog.txt", kMountPoint);
		// The EFI system partition is about 2.8 MB. The boot files stay,
		// and the newest part of the log is what a later boot needs.
		copy_tail(source, fatPath, 1200 * 1024);
	}

	status_t unmount = fs_unmount_volume(kMountPoint, 0);
	fprintf(stderr, "r5_pstate_log: unmount %" B_PRId32 "\n", unmount);
	return true;
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
	for (int attempt = 0; attempt < 40 && !best.found; attempt++) {
		walk("/dev/disk", 0, &best);
		if (!best.found)
			snooze(500000);
	}

	char note[512];
	if (!best.found) {
		snprintf(note, sizeof(note),
			"\nFAT: no FAT partition found under /dev/disk"
			" (nodes %d opened %d signatures %d)\n",
			best.nodes, best.opened, best.signatures);
	} else if (!write_fat(&best, report)) {
		snprintf(note, sizeof(note),
			"\nFAT: %s mount failed\n", best.path);
	} else {
		snprintf(note, sizeof(note),
			"\nFAT: wrote r5-cpu.txt, r5-hardware.txt, and r5-syslog.txt"
			" on %s\n",
			best.path);
	}

	FILE* home = fopen(kHomeReport, "a");
	if (home != NULL) {
		fputs(note, home);
		fclose(home);
	}
	fprintf(stderr, "%s", note);

	// The EFI partition can appear after the boot volume. Each pass looks
	// again. A hang in between still leaves the previous FAT files.
	const bigtime_t waits[] = { 15000000, 45000000, 120000000 };
	for (int pass = 0; pass < 3; pass++) {
		snooze(waits[pass]);
		if (!best.found) {
			for (int attempt = 0; attempt < 10 && !best.found; attempt++) {
				walk("/dev/disk", 0, &best);
				if (!best.found)
					snooze(500000);
			}
		}
		if (best.found)
			write_fat(&best, report);
	}
	return 0;
}
