/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Part of libr5ui.so (docs/LIBR5.md, "Driver settings"). Media writes these
 * files directly when it may, and Login (uid 0) writes them for a standard
 * account that has an administrator grant. Both use this code.
 */


#include "AdminLock.h"

#include <String.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>


// load_driver_settings() in the kernel always reads kUserSettingsDirectory
// (headers/private/system/directories.h) plus "kernel/drivers", not the home
// of the person who changes the setting.
static const char* kKernelSettingsDirectory = "/boot/home/config/settings";
static const char* kDriverSettingsSubdirectory = "kernel/drivers";

// Larger than any of these files. A bigger file is left alone instead of
// being cut off.
static const off_t kMaxDriverSettingsSize = 1024 * 1024;


static status_t
driver_settings_path(const char* fileName, char* path, size_t pathSize)
{
	if (fileName == NULL || fileName[0] == '\0' || strchr(fileName, '/') != NULL
		|| strcmp(fileName, ".") == 0 || strcmp(fileName, "..") == 0)
		return B_BAD_VALUE;
	if (snprintf(path, pathSize, "%s/%s/%s", kKernelSettingsDirectory,
			kDriverSettingsSubdirectory, fileName) >= (int)pathSize)
		return B_NAME_TOO_LONG;
	return B_OK;
}


static status_t
make_driver_settings_directory()
{
	// BPath::SetTo and create_directory both walk the path with a BEntry,
	// which left kernel/drivers without the file. Each level uses mkdir().
	const char* levels[] = { "kernel", kDriverSettingsSubdirectory };
	char directory[B_PATH_NAME_LENGTH];
	for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
		if (snprintf(directory, sizeof(directory), "%s/%s",
				kKernelSettingsDirectory, levels[i]) >= (int)sizeof(directory))
			return B_NAME_TOO_LONG;
		if (mkdir(directory, 0755) < 0 && errno != EEXIST)
			return errno != 0 ? errno : B_ERROR;
	}
	return B_OK;
}


static status_t
read_whole_file(const char* path, BString& text)
{
	text.Truncate(0);

	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return errno != 0 ? errno : B_ERROR;

	struct stat st;
	status_t status = B_OK;
	if (fstat(fd, &st) != 0)
		status = errno != 0 ? errno : B_ERROR;
	else if (st.st_size > kMaxDriverSettingsSize)
		status = B_FILE_TOO_LARGE;
	else if (st.st_size > 0) {
		char* buffer = text.LockBuffer((int32)st.st_size);
		if (buffer == NULL)
			status = B_NO_MEMORY;
		else {
			ssize_t length = read(fd, buffer, (size_t)st.st_size);
			if (length < 0)
				status = errno != 0 ? errno : B_ERROR;
			text.UnlockBuffer(length > 0 ? (int32)length : 0);
		}
	}
	close(fd);
	return status;
}


static bool
is_settings_key(const BString& line, const char* key)
{
	int32 length = (int32)strlen(key);
	if (!line.StartsWith(key))
		return false;
	if (line.Length() == length)
		return true;
	char next = line.ByteAt(length);
	return next == ' ' || next == '\t' || next == '\r';
}


static status_t
write_atomically(const char* path, const BString& text)
{
	// A crash or a full disk never leaves an empty file. rename() also
	// replaces a file owned by someone else in a directory this team may
	// write.
	char tempPath[B_PATH_NAME_LENGTH];
	if (snprintf(tempPath, sizeof(tempPath), "%s.tmp", path)
			>= (int)sizeof(tempPath))
		return B_NAME_TOO_LONG;

	int fd = open(tempPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return errno != 0 ? errno : B_ERROR;

	status_t status = B_OK;
	ssize_t written = write(fd, text.String(), (size_t)text.Length());
	if (written != (ssize_t)text.Length())
		status = written < 0 && errno != 0 ? errno : B_ERROR;
	else if (fsync(fd) != 0)
		status = errno != 0 ? errno : B_ERROR;
	close(fd);

	if (status == B_OK && rename(tempPath, path) != 0)
		status = errno != 0 ? errno : B_ERROR;
	if (status != B_OK)
		unlink(tempPath);
	return status;
}


bool
r5_driver_settings_writable()
{
	// The directory may not exist yet. Then the nearest existing parent
	// decides whether it can be made.
	char path[B_PATH_NAME_LENGTH];
	if (snprintf(path, sizeof(path), "%s/%s", kKernelSettingsDirectory,
			kDriverSettingsSubdirectory) >= (int)sizeof(path))
		return false;

	while (true) {
		if (access(path, F_OK) == 0)
			return access(path, W_OK) == 0;
		char* slash = strrchr(path, '/');
		if (slash == NULL || slash == path)
			return false;
		*slash = '\0';
	}
}


status_t
r5_driver_settings_read(const char* fileName, const char* key, BString& value)
{
	if (key == NULL || key[0] == '\0')
		return B_BAD_VALUE;

	char path[B_PATH_NAME_LENGTH];
	status_t status = driver_settings_path(fileName, path, sizeof(path));
	if (status != B_OK)
		return status;

	BString text;
	status = read_whole_file(path, text);
	if (status != B_OK)
		return status;

	int32 start = 0;
	while (start < text.Length()) {
		int32 end = text.FindFirst('\n', start);
		if (end < 0)
			end = text.Length();
		BString line;
		text.CopyInto(line, start, end - start);
		line.RemoveAll("\r");
		if (is_settings_key(line, key)) {
			line.Remove(0, (int32)strlen(key));
			value = line.Trim();
			return B_OK;
		}
		start = end + 1;
	}
	return B_NAME_NOT_FOUND;
}


status_t
r5_driver_settings_write(const char* fileName, const char* const* keys,
	int32 keyCount, const char* lines, bool replaceFile)
{
	char path[B_PATH_NAME_LENGTH];
	status_t status = driver_settings_path(fileName, path, sizeof(path));
	if (status != B_OK)
		return status;
	if (keyCount < 0 || (keyCount > 0 && keys == NULL))
		return B_BAD_VALUE;
	status = make_driver_settings_directory();
	if (status != B_OK)
		return status;

	BString kept;
	if (!replaceFile) {
		BString text;
		status = read_whole_file(path, text);
		if (status != B_OK && status != B_ENTRY_NOT_FOUND)
			return status;

		int32 start = 0;
		while (start < text.Length()) {
			int32 end = text.FindFirst('\n', start);
			if (end < 0)
				end = text.Length();
			BString line;
			text.CopyInto(line, start, end - start);
			bool replaced = false;
			for (int32 i = 0; i < keyCount; i++) {
				if (keys[i] != NULL && is_settings_key(line, keys[i])) {
					replaced = true;
					break;
				}
			}
			if (!replaced)
				kept << line << "\n";
			start = end + 1;
		}
	}
	if (lines != NULL)
		kept << lines;

	return write_atomically(path, kept);
}
