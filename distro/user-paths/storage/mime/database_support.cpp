/*
 * Copyright 2002-2013, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Tyler Dauwalder
 *		Ingo Weinhold <ingo_weinhold@gmx.de>
 */


#include <mime/database_support.h>

#if defined(__HAIKU__) && !defined(HAIKU_HOST_PLATFORM_HAIKU)
#	include <pthread.h>
#endif

#include <new>

#include <Bitmap.h>
#include <FindDirectory.h>
#include <Path.h>
#include <String.h>

#include <mime/DatabaseLocation.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>


namespace BPrivate {
namespace Storage {
namespace Mime {


#define ATTR_PREFIX "META:"
#define MINI_ICON_ATTR_PREFIX ATTR_PREFIX "M:"
#define LARGE_ICON_ATTR_PREFIX ATTR_PREFIX "L:"

const char *kMiniIconAttrPrefix		= MINI_ICON_ATTR_PREFIX;
const char *kLargeIconAttrPrefix	= LARGE_ICON_ATTR_PREFIX;
const char *kIconAttrPrefix			= ATTR_PREFIX;

// attribute names
const char *kFileTypeAttr			= "BEOS:TYPE";
const char *kTypeAttr				= ATTR_PREFIX "TYPE";
const char *kAppHintAttr			= ATTR_PREFIX "PPATH";
const char *kAttrInfoAttr			= ATTR_PREFIX "ATTR_INFO";
const char *kShortDescriptionAttr	= ATTR_PREFIX "S:DESC";
const char *kLongDescriptionAttr	= ATTR_PREFIX "L:DESC";
const char *kFileExtensionsAttr		= ATTR_PREFIX "EXTENS";
const char *kMiniIconAttr			= MINI_ICON_ATTR_PREFIX "STD_ICON";
const char *kLargeIconAttr			= LARGE_ICON_ATTR_PREFIX "STD_ICON";
const char *kIconAttr				= ATTR_PREFIX "ICON";
const char *kPreferredAppAttr		= ATTR_PREFIX "PREF_APP";
const char *kSnifferRuleAttr		= ATTR_PREFIX "SNIFF_RULE";
const char *kSupportedTypesAttr		= ATTR_PREFIX "FILE_TYPES";

// attribute data types (as used in the R5 database)
const int32 kFileTypeType			= 'MIMS';	// B_MIME_STRING_TYPE
const int32 kTypeType				= B_STRING_TYPE;
const int32 kAppHintType			= 'MPTH';
const int32 kAttrInfoType			= B_MESSAGE_TYPE;
const int32 kShortDescriptionType	= 'MSDC';
const int32 kLongDescriptionType	= 'MLDC';
const int32 kFileExtensionsType		= B_MESSAGE_TYPE;
const int32 kMiniIconType			= B_MINI_ICON_TYPE;
const int32 kLargeIconType			= B_LARGE_ICON_TYPE;
const int32 kIconType				= B_VECTOR_ICON_TYPE;
const int32 kPreferredAppType		= 'MSIG';
const int32 kSnifferRuleType		= B_STRING_TYPE;
const int32 kSupportedTypesType		= B_MESSAGE_TYPE;

// Message fields
const char *kApplicationsField				= "applications";
const char *kExtensionsField				= "extensions";
const char *kSupertypesField				= "super_types";
const char *kSupportingAppsSubCountField	= "be:sub";
const char *kSupportingAppsSuperCountField	= "be:super";
const char *kTypesField						= "types";

// Mime types
const char *kGenericFileType	= "application/octet-stream";
const char *kDirectoryType		= "application/x-vnd.Be-directory";
const char *kSymlinkType		= "application/x-vnd.Be-symlink";
const char *kMetaMimeType		= "application/x-vnd.Be-meta-mime";

// Error codes
const status_t kMimeGuessFailureError	= B_ERRORS_END+1;


#if defined(__HAIKU__) && !defined(HAIKU_HOST_PLATFORM_HAIKU)


static const directory_which kBaseDirectoryConstants[] = {
	B_USER_SETTINGS_DIRECTORY,
	B_USER_NONPACKAGED_DATA_DIRECTORY,
	B_USER_DATA_DIRECTORY,
	B_SYSTEM_NONPACKAGED_DATA_DIRECTORY,
	B_SYSTEM_DATA_DIRECTORY
};

static pthread_once_t sDefaultDatabaseLocationInitOnce = PTHREAD_ONCE_INIT;
static pthread_mutex_t sDefaultDatabaseLocationLock = PTHREAD_MUTEX_INITIALIZER;
static DatabaseLocation* sDefaultDatabaseLocation = NULL;
static BString sDefaultDatabaseLocationKey;


static bool
home_directory_for_name(const char* name, BString& home)
{
	// Read the file directly. getpwnam() asks the registrar, and the
	// registrar calls this while it is still inside ReadyToRun().
	FILE* file = fopen("/etc/passwd", "r");
	if (file == NULL)
		return false;

	char line[512];
	bool found = false;
	while (fgets(line, sizeof(line), file) != NULL) {
		char* fields[7];
		char* cursor = line;
		int count = 0;
		while (count < 7) {
			fields[count++] = cursor;
			char* colon = strchr(cursor, ':');
			if (colon == NULL)
				break;
			*colon = '\0';
			cursor = colon + 1;
		}
		if (count < 6 || strcmp(fields[0], name) != 0)
			continue;
		if (strcmp(fields[2], "0") == 0)
			break;

		char* directory = fields[5];
		size_t length = strlen(directory);
		while (length > 0 && (directory[length - 1] == '\n'
			|| directory[length - 1] == '\r'
			|| directory[length - 1] == ' ')) {
			directory[--length] = '\0';
		}
		if (directory[0] == '\0')
			break;
		home = directory;
		found = true;
		break;
	}

	fclose(file);
	return found;
}


static BString
mime_user_settings_path()
{
	// Registrar is uid 0, so find_directory would always pick /boot/home.
	// After Sign in, use that account's settings instead.
	if (geteuid() == 0) {
		FILE* file = fopen("/boot/system/settings/r5-desktop-session", "r");
		if (file != NULL) {
			char name[64];
			name[0] = '\0';
			if (fgets(name, sizeof(name), file) != NULL) {
				size_t length = strlen(name);
				while (length > 0 && (name[length - 1] == '\n'
					|| name[length - 1] == '\r'
					|| name[length - 1] == ' ')) {
					name[--length] = '\0';
				}
				BString home;
				if (length > 0 && home_directory_for_name(name, home)) {
					BString path;
					path.SetToFormat("%s/config/settings", home.String());
					fclose(file);
					return path;
				}
			}
			fclose(file);
		}
	}

	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK)
		return BString(path.Path());
	return BString("/boot/home/config/settings");
}


static void
fill_default_database_location(DatabaseLocation* location)
{
	BString userSettings = mime_user_settings_path();
	BString userMime;
	userMime.SetToFormat("%s/mime_db", userSettings.String());
	location->AddDirectory(userMime);

	for (size_t i = 1;
		i < sizeof(kBaseDirectoryConstants)
			/ sizeof(kBaseDirectoryConstants[0]); i++) {
		BPath path;
		if (find_directory(kBaseDirectoryConstants[i], &path) != B_OK)
			continue;
		BString directoryPath(path.Path());
		directoryPath += "/mime_db";
		location->AddDirectory(directoryPath);
	}
}


static void
init_default_database_location()
{
	static DatabaseLocation databaseLocation;
	sDefaultDatabaseLocation = &databaseLocation;
}


DatabaseLocation*
default_database_location()
{
	pthread_once(&sDefaultDatabaseLocationInitOnce,
		&init_default_database_location);

	BString key = mime_user_settings_path();
	pthread_mutex_lock(&sDefaultDatabaseLocationLock);
	if (sDefaultDatabaseLocationKey != key
		|| sDefaultDatabaseLocation->Directories().IsEmpty()) {
		sDefaultDatabaseLocation->MakeEmpty();
		fill_default_database_location(sDefaultDatabaseLocation);
		sDefaultDatabaseLocationKey = key;
	}
	pthread_mutex_unlock(&sDefaultDatabaseLocationLock);
	return sDefaultDatabaseLocation;
}


#else	// building for the host platform


DatabaseLocation*
default_database_location()
{
	// Should never actually be used, but make it valid, anyway.
	static DatabaseLocation location;
	if (location.Directories().IsEmpty())
		location.AddDirectory("/tmp");
	return &location;
}


#endif


/*! \brief Returns properly formatted raw bitmap data, ready to be shipped off
	to the hacked up 4-parameter version of Database::SetIcon()

	BBitmap implemented.  This function takes the given bitmap, converts it to the
	B_CMAP8 color space if necessary and able, and returns said bitmap data in
	a newly allocated array pointed to by the pointer that's pointed to by
	\c data. The length of the array is stored in the integer pointed to by
	\c dataSize. The array is allocated with \c new[], and it's your
	responsibility to \c delete[] it when you're finished.
*/
status_t
get_icon_data(const BBitmap *icon, icon_size which, void **data,
	int32 *dataSize)
{
	if (icon == NULL || data == NULL || dataSize == 0
		|| icon->InitCheck() != B_OK)
		return B_BAD_VALUE;

	BRect bounds;
	BBitmap *icon8 = NULL;
	void *srcData = NULL;
	bool otherColorSpace = false;

	// Figure out what kind of data we *should* have
	switch (which) {
		case B_MINI_ICON:
			bounds.Set(0, 0, 15, 15);
			break;
		case B_LARGE_ICON:
			bounds.Set(0, 0, 31, 31);
			break;
		default:
			return B_BAD_VALUE;
	}

	// Check the icon
	status_t err = icon->Bounds() == bounds ? B_OK : B_BAD_VALUE;

	// Convert to B_CMAP8 if necessary
	if (!err) {
		otherColorSpace = (icon->ColorSpace() != B_CMAP8);
		if (otherColorSpace) {
			icon8 = new(std::nothrow) BBitmap(bounds, B_BITMAP_NO_SERVER_LINK,
				B_CMAP8);
			if (!icon8)
				err = B_NO_MEMORY;
			if (!err)
				err = icon8->ImportBits(icon);
			if (!err) {
				srcData = icon8->Bits();
				*dataSize = icon8->BitsLength();
			}
		} else {
			srcData = icon->Bits();
			*dataSize = icon->BitsLength();
		}
	}

	// Alloc a new data buffer
	if (!err) {
		*data = new(std::nothrow) char[*dataSize];
		if (!*data)
			err = B_NO_MEMORY;
	}

	// Copy the data into it.
	if (!err)
		memcpy(*data, srcData, *dataSize);
	if (otherColorSpace)
		delete icon8;
	return err;
}


} // namespace Mime
} // namespace Storage
} // namespace BPrivate
