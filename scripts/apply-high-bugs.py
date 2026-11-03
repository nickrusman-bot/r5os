#!/usr/bin/env python3
"""Apply HIGH boot/install/DriveSetup fixes: #16573 #13754 #10208.

The radeon_hd sources, including #17377, #17664 and the Barcelo probe,
are copied from distro/graphics by scripts/apply-radeon.py.
"""
from __future__ import annotations

import pathlib
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")


def replace_once(path: pathlib.Path, old: str, new: str, tag: str) -> None:
    text = path.read_text(encoding="utf-8")
    if "R5OS modifications in this file are GPL-3.0-or-later." in text:
        print(f"already {path.name} ({tag})")
        return
    if new in text:
        print(f"already {path.name} ({tag})")
        return
    if old not in text:
        raise SystemExit(f"pattern not found in {path} ({tag})")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"patched {path} ({tag})")


def replace_all(path: pathlib.Path, old: str, new: str, tag: str) -> None:
    text = path.read_text(encoding="utf-8")
    if "R5OS modifications in this file are GPL-3.0-or-later." in text:
        print(f"already {path.name} ({tag})")
        return
    if old not in text:
        if new in text:
            print(f"already {path.name} ({tag})")
            return
        raise SystemExit(f"pattern not found in {path} ({tag})")
    path.write_text(text.replace(old, new), encoding="utf-8")
    print(f"patched {path} ({tag})")


def patch_packages_first_boot() -> None:
    path = HAIKU / "src/servers/package/Volume.cpp"
    replace_once(
        path,
        """		bool anyPackages = false;
		for (PackageNodeRefHashTable::Iterator it =
				fActiveState->ByNodeRefIterator(); it.HasNext();) {
			Package* package = it.Next();
			if (package->IsActive()) {
				anyPackages = true;
				break;
			}
		}

		if (anyPackages) {
			// Create first boot processing special transaction for current
			// volume, which also creates an empty transaction directory.
			BPackageInstallationLocation location = Location();
			BDirectory transactionDirectory;
			BActivationTransaction transaction;
			error = CreateTransaction(location, transaction,
				transactionDirectory);
			if (error != B_OK)
				RETURN_ERROR(error);

			// Add all package files in currently active state to transaction.
			for (PackageNodeRefHashTable::Iterator it =
					fActiveState->ByNodeRefIterator(); it.HasNext();) {
				Package* package = it.Next();
				if (package->IsActive()) {
					if (!transaction.AddPackageToActivate(
							package->FileName().String()))
						RETURN_ERROR(B_NO_MEMORY);
				}
			}""",
        """		// Haiku #16573: after install, packages on disk may not yet be marked
		// active. Queue every package file, not only the currently active set.
		bool anyPackages = false;
		for (PackageFileNameHashTable::Iterator it
				= fLatestState->ByFileNameIterator();
			Package* package = it.Next();) {
			(void)package;
			anyPackages = true;
			break;
		}

		if (anyPackages) {
			// Create first boot processing special transaction for current
			// volume, which also creates an empty transaction directory.
			BPackageInstallationLocation location = Location();
			BDirectory transactionDirectory;
			BActivationTransaction transaction;
			error = CreateTransaction(location, transaction,
				transactionDirectory);
			if (error != B_OK)
				RETURN_ERROR(error);

			for (PackageFileNameHashTable::Iterator it
					= fLatestState->ByFileNameIterator();
				Package* package = it.Next();) {
				if (!transaction.AddPackageToActivate(
						package->FileName().String()))
					RETURN_ERROR(B_NO_MEMORY);
			}""",
        "Haiku #16573 first-boot packages",
    )


def patch_input_retry() -> None:
    typesPath = HAIKU / "headers/private/input/InputServerTypes.h"
    replace_once(
        typesPath,
        """#define IS_STOP_DEVICE					'Ispd'
#define IS_CONTROL_DEVICES				'Icnd'
""",
        """#define IS_STOP_DEVICE					'Ispd'
#define IS_CONTROL_DEVICES				'Icnd'
#define IS_RETRY_UNSTARTED_DEVICES		'Irud'
""",
        "Haiku #13754 retry message",
    )

    path = HAIKU / "src/servers/input/InputServer.cpp"
    replace_once(
        path,
        """#include <Locker.h>
#include <Message.h>
#include <OS.h>
""",
        """#include <Locker.h>
#include <Message.h>
#include <MessageRunner.h>
#include <OS.h>
""",
        "Haiku #13754 MessageRunner include",
    )
    replace_once(
        path,
        """void
InputServer::ReadyToRun()
{
	CALLED();

	// say hello to the app_server

	BPrivate::AppServerLink link;
	link.StartMessage(AS_REGISTER_INPUT_SERVER);
	link.Flush();
}
""",
        """void
InputServer::ReadyToRun()
{
	CALLED();

	// say hello to the app_server

	BPrivate::AppServerLink link;
	link.StartMessage(AS_REGISTER_INPUT_SERVER);
	link.Flush();

	// Haiku #13754: USB/PS2 add-ons often register before the HID driver is
	// ready after first boot. Retry Start() a few times.
	BMessage retry(IS_RETRY_UNSTARTED_DEVICES);
	retry.AddInt32("attempt", 0);
	BMessageRunner::StartSending(this, &retry, 2000000, 1);
}
""",
        "Haiku #13754 ReadyToRun retry",
    )
    replace_once(
        path,
        """		case IS_UNFOCUS_IM_AWARE_VIEW:
			status = HandleFocusUnfocusIMAwareView(message, &reply);
			break;

		// app_server communication
""",
        """		case IS_UNFOCUS_IM_AWARE_VIEW:
			status = HandleFocusUnfocusIMAwareView(message, &reply);
			break;
		case IS_RETRY_UNSTARTED_DEVICES:
		{
			int32 attempt = 0;
			message->FindInt32("attempt", &attempt);

			int32 pending = 0;
			{
				BAutolock lock(fInputDeviceListLocker);
				for (int32 i = 0; i < fInputDeviceList.CountItems(); i++) {
					InputDeviceListItem* item
						= (InputDeviceListItem*)fInputDeviceList.ItemAt(i);
					if (item == NULL || item->Running())
						continue;
					item->Start();
					if (!item->Running())
						pending++;
				}
			}

			if (pending > 0 && attempt < 10) {
				BMessage retry(IS_RETRY_UNSTARTED_DEVICES);
				retry.AddInt32("attempt", attempt + 1);
				BMessageRunner::StartSending(this, &retry, 2000000, 1);
			}
			break;
		}

		// app_server communication
""",
        "Haiku #13754 retry handler",
    )


def patch_drivesetup_size() -> None:
    path = HAIKU / "src/apps/drivesetup/Support.cpp"
    replace_once(
        path,
        """	SetKeyIncrementValue(int32(1024 * 1024 * 1.0 * kMaxSliderLimit
		/ ((MaxPartitionSize() - 1) / fGranularity) + 0.5));
""",
        """	off_t units = MaxPartitionSize() / fGranularity;
	if (units < 1)
		units = 1;
	SetKeyIncrementValue(int32(1024 * 1024 * 1.0 * kMaxSliderLimit
		/ units + 0.5));
""",
        "Haiku #10208 slider key increment",
    )
    replace_once(
        path,
        """void
SizeSlider::SetValue(int32 value)
{
	BSlider::SetValue(value);

	fSize = (off_t(1.0 * (MaxPartitionSize() - 1) * Value()
		/ kMaxSliderLimit + 0.5) / fGranularity) * fGranularity;
}
""",
        """void
SizeSlider::SetValue(int32 value)
{
	BSlider::SetValue(value);

	off_t maxSize = MaxPartitionSize();
	if (maxSize <= 0) {
		fSize = 0;
		return;
	}

	off_t units = maxSize / fGranularity;
	if (units < 1)
		units = 1;

	// Haiku #10208: do not drop a byte with MaxPartitionSize()-1, and keep
	// the max thumb on the full aligned capacity.
	if (Value() >= kMaxSliderLimit)
		fSize = units * fGranularity;
	else {
		off_t selected = (off_t)Value() * units / kMaxSliderLimit;
		fSize = selected * fGranularity;
	}
	if (fSize > maxSize)
		fSize = maxSize;
}
""",
        "Haiku #10208 SetValue",
    )
    replace_once(
        path,
        """	SetValue(int32(1.0 * kMaxSliderLimit / fGranularity * size
		/ ((MaxPartitionSize() - 1) / fGranularity) + 0.5));
""",
        """	off_t maxSize = MaxPartitionSize();
	off_t units = maxSize / fGranularity;
	if (units < 1)
		units = 1;
	off_t sizeUnits = size / fGranularity;
	if (sizeUnits < 0)
		sizeUnits = 0;
	if (sizeUnits > units)
		sizeUnits = units;
	SetValue(int32((1.0 * kMaxSliderLimit * sizeUnits) / units + 0.5));
""",
        "Haiku #10208 SetSize",
    )

    panel = HAIKU / "src/apps/drivesetup/CreateParametersPanel.cpp"
    replace_once(
        panel,
        """			off_t size = strtoll(fSizeTextControl->Text(), NULL, 10) * kMegaByte;
			if (size >= 0 && size <= fSizeSlider->MaxPartitionSize())
				fSizeSlider->SetSize(size);
			else
				_UpdateSizeTextControl();
""",
        """			long long megabytes = strtoll(fSizeTextControl->Text(), NULL, 10);
			off_t maxSize = fSizeSlider->MaxPartitionSize();
			off_t size = 0;
			if (megabytes > 0) {
				if (megabytes > maxSize / (off_t)kMegaByte)
					size = maxSize;
				else
					size = (off_t)megabytes * kMegaByte;
			}
			if (size >= 0 && size <= maxSize)
				fSizeSlider->SetSize(size);
			else
				_UpdateSizeTextControl();
""",
        "Haiku #10208 size text overflow",
    )


def copy_package_scripts() -> None:
    root = pathlib.Path(__file__).resolve().parents[1]
    source = root / "distro/package/src/servers/package/CommitTransactionHandler.cpp"
    destination = HAIKU / "src/servers/package/CommitTransactionHandler.cpp"
    if not source.is_file():
        raise SystemExit(f"missing {source}")
    if not destination.parent.is_dir():
        raise SystemExit(f"missing {destination.parent}")
    text = source.read_bytes().replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    destination.write_bytes(text)
    print(f"copied {destination.relative_to(HAIKU)}")


def main() -> int:
    copy_package_scripts()
    patch_packages_first_boot()
    patch_input_retry()
    patch_drivesetup_size()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
