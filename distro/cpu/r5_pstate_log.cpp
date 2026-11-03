/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Read /dev/misc/r5_pstate twice and write the boot logs on /boot/home.
 * That directory is on the BeFS boot volume. Mounting another partition
 * of the same USB stick stalled the boot while the rocket was still on
 * screen. No FAT partition is mounted. No MSR is written.
 */


#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <OS.h>


static const char* kHomeReport = "/boot/home/r5-cpu.txt";
static const char* kHomeHardware = "/boot/home/r5-hardware.txt";
static const char* kHomeSyslog = "/boot/home/r5-syslog.txt";


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


static void
write_boot_logs(const char* report, int pass)
{
	char text[9200];
	snprintf(text, sizeof(text),
		"%s"
		"\nlogs: /boot/home pass %d, no FAT mount\n",
		report, pass);
	write_text(kHomeReport, text);

	const char* source = syslog_source();
	write_hardware(kHomeHardware, source);
	if (source != NULL)
		copy_tail(source, kHomeSyslog, 1200 * 1024);
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
	write_boot_logs(report, 0);

	// Later passes still run if the desktop has not appeared. They write
	// the BeFS files again and do not mount a disk.
	const bigtime_t waits[] = { 15000000, 45000000, 120000000 };
	for (int pass = 0; pass < 3; pass++) {
		snooze(waits[pass]);
		write_boot_logs(report, pass + 1);
	}
	return 0;
}
