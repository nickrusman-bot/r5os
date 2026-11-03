#!/usr/bin/env python3
"""Apply HIGH boot/install/GPU/DriveSetup fixes:
#16573 #13754 #17377 #17664 #10208, plus the 5625U Barcelo probe."""
from __future__ import annotations

import pathlib
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")


def replace_once(path: pathlib.Path, old: str, new: str, tag: str) -> None:
    text = path.read_text(encoding="utf-8")
    if new in text:
        print(f"already {path.name} ({tag})")
        return
    if old not in text:
        raise SystemExit(f"pattern not found in {path} ({tag})")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"patched {path} ({tag})")


def replace_all(path: pathlib.Path, old: str, new: str, tag: str) -> None:
    text = path.read_text(encoding="utf-8")
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


def patch_radeon_connector() -> None:
    path = HAIKU / "src/add-ons/accelerants/radeon_hd/connector.cpp"
    replace_all(
        path,
        "((uint16*)gAtomContext->bios + tableOffset",
        "(gAtomContext->bios + tableOffset",
        "Haiku #17377 ATOM record byte offsets",
    )
    replace_once(
        path,
        """		uint8* address = (uint8*)pathObject->asDispPath;
		ATOM_DISPLAY_OBJECT_PATH* path;
		address += pathSize;
		path = (ATOM_DISPLAY_OBJECT_PATH*)address;
		pathSize += B_LENDIAN_TO_HOST_INT16(path->usSize);
""",
        """		uint8* address = (uint8*)pathObject->asDispPath;
		ATOM_DISPLAY_OBJECT_PATH* path;
		address += pathSize;
		path = (ATOM_DISPLAY_OBJECT_PATH*)address;
		uint16 thisPathSize = B_LENDIAN_TO_HOST_INT16(path->usSize);
		if (thisPathSize < 8) {
			ERROR("%s: Path #%" B_PRId32 ": invalid usSize, stopping.\\n",
				__func__, i);
			break;
		}
		pathSize += thisPathSize;
""",
        "Haiku #17377 path usSize guard",
    )
    replace_once(
        path,
        """	int deviceSupport = B_LENDIAN_TO_HOST_INT16(objectHeader->usDeviceSupport);

	int pathSize = 0;
""",
        """	int deviceSupport = B_LENDIAN_TO_HOST_INT16(objectHeader->usDeviceSupport);

	if (pathObject == NULL || encoderObject == NULL) {
		ERROR("%s: ERROR: ATOM object tables missing!\\n", __func__);
		return B_ERROR;
	}

	int pathSize = 0;
""",
        "Haiku #17377 null object tables",
    )


def patch_radeon_barcelo() -> None:
    path = HAIKU / "src/add-ons/kernel/drivers/graphics/radeon_hd/driver.cpp"
    # Raven, Vega, and Navi sit inside `#if 0 /* disabled for R1/beta5 */`.
    # A line next to 0x15d8 is not compiled. 0x15e7 has to follow that #endif.
    buried = (
        '\t// Ryzen 5 5625U, read from the USB boot on 3 Nov 2026.\n'
        '\t// Same Raven display path as Picasso 0x15d8. Not modeset-tested.\n'
        '\t{0x15e7, 12, 0, RADEON_RAVEN, CHIP_APU, "Radeon Vega Barcelo"},\n'
    )
    text = path.read_text(encoding="utf-8")
    if buried in text:
        path.write_text(text.replace(buried, "", 1), encoding="utf-8")
        print(f"removed buried 15e7 from {path.name}")
    replace_once(
        path,
        '\t{0x73ff, 13, 5, RADEON_NAVI, CHIP_STD, "Radeon RX Navi (Dimgrey)"},\n'
        "#endif\n};\n",
        '\t{0x73ff, 13, 5, RADEON_NAVI, CHIP_STD, "Radeon RX Navi (Dimgrey)"},\n'
        "#endif\n"
        "\t// Ryzen 5 5625U, read from the USB boot on 3 Nov 2026.\n"
        "\t// Picasso 0x15d8 is the same Raven path, but that line is inside\n"
        "\t// `#if 0 /* disabled for R1/beta5 */` and is not compiled.\n"
        "\t// Not modeset-tested. A bad modeset can leave the panel black.\n"
        '\t{0x15e7, 12, 0, RADEON_RAVEN, CHIP_APU, "Radeon Vega Barcelo"},\n'
        "};\n",
        "5625U 1002:15e7",
    )


def patch_radeon_cedar_fb() -> None:
    path = HAIKU / "src/add-ons/kernel/drivers/graphics/radeon_hd/radeon_hd.cpp"
    replace_once(
        path,
        """			default:
				// Evergreen+ has memory stored in MB
				info.shared_info->graphics_memory_size
					= read32(info.registers + CONFIG_MEMSIZE) * 1024;
				break;
""",
        """			default:
				// Evergreen+ has memory stored in MB
				info.shared_info->graphics_memory_size
					= read32(info.registers + CONFIG_MEMSIZE) * 1024;
				// Haiku #17664: Cedar sometimes leaves CONFIG_MEMSIZE at 0.
				// Retry as bytes (R700-style) before falling back to the BAR.
				if (info.shared_info->graphics_memory_size == 0) {
					uint32 memSize = read32(info.registers + CONFIG_MEMSIZE);
					if (memSize > 1024)
						info.shared_info->graphics_memory_size = memSize / 1024;
				}
				break;
""",
        "Haiku #17664 Cedar MEMSIZE",
    )
    replace_once(
        path,
        """	if (info.shared_info->frame_buffer_size < 8192) {
		ERROR("%s: Error: frame buffer is less than 8 MiB. I give up.\\n",
			__func__);
		return B_ERROR;
	}
""",
        """	if (info.shared_info->frame_buffer_size == 0) {
		ERROR("%s: Error: frame buffer size is 0. I give up.\\n",
			__func__);
		return B_ERROR;
	}
	if (info.shared_info->frame_buffer_size < 8192) {
		// Haiku #17664: still map a small BAR rather than leaving app_server
		// with a NULL framebuffer.
		ERROR("%s: Warning: frame buffer is less than 8 MiB (%" B_PRIu32
			" KiB), continuing.\\n",
			__func__, info.shared_info->frame_buffer_size);
	}
""",
        "Haiku #17664 small FB continue",
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


def patch_radeon_display_object() -> None:
    path = HAIKU / "src/add-ons/accelerants/radeon_hd/connector.cpp"
    replace_once(
        path,
        '// r600+\nstatus_t\nconnector_probe()\n',
        'static uint16\natom_le16(const uint8* bytes)\n{\n\treturn (uint16)bytes[0] | ((uint16)bytes[1] << 8);\n}\n\n\nstatic void\nconnector_apply_encoder(encoder_info* encoder, uint16 objectRaw,\n\tuint16 connectorFlags, uint16 caps, bool external)\n{\n\tuint32 encoderID = (objectRaw & OBJECT_ID_MASK) >> OBJECT_ID_SHIFT;\n\tuint32 encoderType = encoder_type_lookup(encoderID, connectorFlags);\n\tif (encoderType == VIDEO_ENCODER_NONE)\n\t\treturn;\n\n\tencoder->valid = true;\n\tencoder->capabilities = caps;\n\tencoder->objectID = encoderID;\n\tencoder->type = encoderType;\n\tencoder->linkEnumeration\n\t\t= (objectRaw & ENUM_ID_MASK) >> ENUM_ID_SHIFT;\n\tencoder->isExternal = external;\n\tencoder->isDPBridge = external ? encoder_is_dp_bridge(encoderID) : false;\n\tpll_limit_probe(&encoder->pll);\n}\n\n\nstatic void\nconnector_walk_object_records(uint16 tableOffset, uint16 tableSize,\n\tuint16 recordOffset, uint32 connectorIndex, bool encoderRecords,\n\tuint16* caps)\n{\n\tif (recordOffset < 8 || recordOffset >= tableSize)\n\t\treturn;\n\n\tuint8* bios = gAtomContext->bios;\n\tuint32 position = (uint32)tableOffset + recordOffset;\n\tuint32 end = (uint32)tableOffset + tableSize;\n\n\tfor (int32 record = 0; record < 16; record++) {\n\t\tif (position + 2 > end)\n\t\t\treturn;\n\n\t\tuint8 type = bios[position];\n\t\tuint8 size = bios[position + 1];\n\t\tif (type == 0 || type == 0xff || size < 2)\n\t\t\treturn;\n\t\tif (position + size > end)\n\t\t\treturn;\n\n\t\tif (!encoderRecords && type == ATOM_I2C_RECORD_TYPE && size >= 4)\n\t\t\tconnector_attach_gpio_i2c(connectorIndex, bios[position + 2]);\n\t\telse if (!encoderRecords && type == ATOM_HPD_INT_RECORD_TYPE\n\t\t\t&& size >= 4) {\n\t\t\tconnector_attach_gpio_hpd(connectorIndex, bios[position + 2]);\n\t\t} else if (encoderRecords && caps != NULL\n\t\t\t&& type == ATOM_ENCODER_CAP_RECORD_TYPE && size >= 4) {\n\t\t\t*caps = (uint16)bios[position + 2]\n\t\t\t\t| ((uint16)bios[position + 3] << 8);\n\t\t}\n\n\t\tposition += size;\n\t}\n}\n\n\nstatic status_t\nconnector_probe_object_v14(uint16 tableOffset, uint16 tableSize,\n\tuint8 tableMinor)\n{\n\t// AtomBIOS master data tables v2.1 store a flat display-object path\n\t// list (table 1.4 / 1.5). The older ATOM_OBJECT_HEADER offsets land\n\t// in the middle of that list, which is the "235 paths" / usSize stop\n\t// seen on the 5625U.\n\tif (tableSize < 8) {\n\t\tERROR("%s: display object table is too small\\n", __func__);\n\t\treturn B_ERROR;\n\t}\n\n\tuint8* table = gAtomContext->bios + tableOffset;\n\tuint16 deviceSupport = atom_le16(table + 4);\n\tuint32 pathCount = table[6];\n\tuint32 pathBytes = 16;\n\tuint32 available = (tableSize - 8) / pathBytes;\n\tif (pathCount > available)\n\t\tpathCount = available;\n\tif (pathCount > ATOM_MAX_SUPPORTED_DEVICE)\n\t\tpathCount = ATOM_MAX_SUPPORTED_DEVICE;\n\n\tTRACE("%s: display object table 1.%" B_PRIu8 ", %" B_PRIu32 " paths.\\n",\n\t\t__func__, tableMinor, pathCount);\n\n\tuint32 connectorIndex = 0;\n\tfor (uint32 i = 0; i < pathCount; i++) {\n\t\tconst uint8* path = table + 8 + (i * pathBytes);\n\t\tuint16 displayObject = atom_le16(path + 0);\n\t\tuint16 displayRecord = atom_le16(path + 2);\n\t\tuint16 encoderObject = atom_le16(path + 4);\n\t\tuint16 externalObject = tableMinor >= 5 ? 0 : atom_le16(path + 6);\n\t\tuint16 encoderRecord = tableMinor >= 5 ? 0 : atom_le16(path + 8);\n\t\tuint16 externalRecord = tableMinor >= 5 ? 0 : atom_le16(path + 10);\n\t\tuint16 connectorFlags = atom_le16(path + 12);\n\n\t\tif (displayObject == 0 || connectorFlags == 0)\n\t\t\tcontinue;\n\t\tif (deviceSupport != 0 && (deviceSupport & connectorFlags) == 0)\n\t\t\tcontinue;\n\t\tif (connectorFlags == ATOM_DEVICE_CV_SUPPORT)\n\t\t\tcontinue;\n\t\tif (connectorIndex >= ATOM_MAX_SUPPORTED_DEVICE)\n\t\t\tbreak;\n\n\t\tuint16 connectorObjectID\n\t\t\t= (displayObject & OBJECT_ID_MASK) >> OBJECT_ID_SHIFT;\n\t\tif (connectorObjectID >= B_COUNT_OF(kConnectorConvert)) {\n\t\t\tERROR("%s: path %" B_PRIu32 ": unknown connector object\\n",\n\t\t\t\t__func__, i);\n\t\t\tcontinue;\n\t\t}\n\n\t\tuint32 connectorType = kConnectorConvert[connectorObjectID];\n\t\tif (connectorType == VIDEO_CONNECTOR_UNKNOWN)\n\t\t\tcontinue;\n\n\t\tconnector_info* connector = gConnector[connectorIndex];\n\t\tconnector_walk_object_records(tableOffset, tableSize, displayRecord,\n\t\t\tconnectorIndex, false, NULL);\n\n\t\tuint16 caps = 0;\n\t\tconnector_walk_object_records(tableOffset, tableSize, encoderRecord,\n\t\t\tconnectorIndex, true, &caps);\n\t\tif (encoderObject != 0) {\n\t\t\tbool external = encoder_is_external(\n\t\t\t\t(encoderObject & OBJECT_ID_MASK) >> OBJECT_ID_SHIFT);\n\t\t\tconnector_apply_encoder(external\n\t\t\t\t? &connector->encoderExternal : &connector->encoder,\n\t\t\t\tencoderObject, connectorFlags, caps, external);\n\t\t}\n\n\t\tif (externalObject != 0) {\n\t\t\tuint16 externalCaps = 0;\n\t\t\tconnector_walk_object_records(tableOffset, tableSize,\n\t\t\t\texternalRecord, connectorIndex, true, &externalCaps);\n\t\t\tconnector_apply_encoder(&connector->encoderExternal,\n\t\t\t\texternalObject, connectorFlags, externalCaps, true);\n\t\t}\n\n\t\tconnector->valid = true;\n\t\tconnector->flags = connectorFlags;\n\t\tconnector->type = connectorType;\n\t\tconnector->objectID = connectorObjectID;\n\n\t\tTRACE("%s: path %" B_PRIu32 " connector %s flags 0x%" B_PRIX16 "\\n",\n\t\t\t__func__, i, get_connector_name(connectorType), connectorFlags);\n\n\t\tconnectorIndex++;\n\t}\n\n\tif (connectorIndex == 0) {\n\t\tERROR("%s: display object table listed no connectors\\n", __func__);\n\t\treturn B_ERROR;\n\t}\n\n\treturn B_OK;\n}\n\n\n// r600+\nstatus_t\nconnector_probe()\n',
        'Raven display object table 1.4',
    )


def patch_radeon_display_object_branch() -> None:
    path = HAIKU / "src/add-ons/accelerants/radeon_hd/connector.cpp"
    replace_once(
        path,
        '\t\treturn B_ERROR;\n\t}\n\n\tATOM_CONNECTOR_OBJECT_TABLE* connectorObject;\n',
        '\t\treturn B_ERROR;\n\t}\n\n\tif (tableMajor == 1 && tableMinor >= 4)\n\t\treturn connector_probe_object_v14(tableOffset, tableSize, tableMinor);\n\n\tATOM_CONNECTOR_OBJECT_TABLE* connectorObject;\n',
        'Raven display object branch',
    )


def patch_radeon_gpio_v21() -> None:
    path = HAIKU / "src/add-ons/accelerants/radeon_hd/connector.cpp"
    replace_once(
        path,
        '\tint index = GetIndexIntoMasterTable(DATA, GPIO_Pin_LUT);\n\tuint16 tableOffset;\n\tuint16 tableSize;\n\n\tstruct _ATOM_GPIO_PIN_LUT* gpioInfo;\n\n\tif (atom_parse_data_header(gAtomContext, index, &tableSize, NULL, NULL,\n\t\t&tableOffset)) {\n\t\tERROR("%s: could\'t read GPIO_Pin_LUT table from AtomBIOS index %d!\\n",\n\t\t\t__func__, index);\n\t}\n\tgpioInfo = (struct _ATOM_GPIO_PIN_LUT*)(gAtomContext->bios + tableOffset);\n',
        '\tint index = GetIndexIntoMasterTable(DATA, GPIO_Pin_LUT);\n\t// Master data table format 2 (Raven and later) stores gpio_pin_lut at\n\t// index 12. Index 11 is vram_usagebyfirmware, which this walk was reading.\n\tif (gAtomContext->bios[gAtomContext->data_table + 2] >= 2)\n\t\tindex = 12;\n\n\tuint16 tableOffset;\n\tuint16 tableSize;\n\n\tstruct _ATOM_GPIO_PIN_LUT* gpioInfo;\n\n\tif (atom_parse_data_header(gAtomContext, index, &tableSize, NULL, NULL,\n\t\t&tableOffset) != B_OK) {\n\t\tTRACE("%s: no GPIO pin LUT at index %d\\n", __func__, index);\n\t\treturn B_OK;\n\t}\n\tgpioInfo = (struct _ATOM_GPIO_PIN_LUT*)(gAtomContext->bios + tableOffset);\n',
        'Raven GPIO pin LUT index 12',
    )


def patch_radeon_gpio_i2c_optional() -> None:
    path = HAIKU / "src/add-ons/accelerants/radeon_hd/connector.cpp"
    replace_once(
        path,
        '\tif (atom_parse_data_header(gAtomContext, index, &tableSize,\n\t\tNULL, NULL, &tableOffset) != B_OK) {\n\t\tERROR("%s: could\'t read GPIO_I2C_Info table from AtomBIOS index %d!\\n",\n\t\t\t__func__, index);\n\t\treturn B_ERROR;\n\t}\n',
        '\tif (atom_parse_data_header(gAtomContext, index, &tableSize,\n\t\tNULL, NULL, &tableOffset) != B_OK) {\n\t\t// v2.1 has no GPIO_I2C_Info. I2C lives on the display-path records.\n\t\tTRACE("%s: no GPIO_I2C_Info at index %d\\n", __func__, index);\n\t\treturn B_OK;\n\t}\n',
        'Raven GPIO_I2C optional',
    )


def patch_radeon_dce_clock() -> None:
    path = HAIKU / "src/add-ons/accelerants/radeon_hd/pll.cpp"
    replace_once(
        path,
        '\t\t\t\tcase 1:\n\t\t\t\t\targs.v2.asParam.ulDCEClkFreq\n\t\t\t\t\t\t= B_HOST_TO_LENDIAN_INT32(clock / 10);\n\t\t\t\t\targs.v2.asParam.ucDCEClkType = clockType;\n\t\t\t\t\targs.v2.asParam.ucDCEClkSrc = clockSource;\n\t\t\t\t\tbreak;\n',
        '\t\t\t\tcase 1:\n\t\t\t\tcase 2:\n\t\t\t\t\t// Table 2.2 keeps the 2.1 parameter block. Linux has no\n\t\t\t\t\t// separate 2.2 struct. Raven reports 2.2.\n\t\t\t\t\targs.v2.asParam.ulDCEClkFreq\n\t\t\t\t\t\t= B_HOST_TO_LENDIAN_INT32(clock / 10);\n\t\t\t\t\targs.v2.asParam.ucDCEClkType = clockType;\n\t\t\t\t\targs.v2.asParam.ucDCEClkSrc = clockSource;\n\t\t\t\t\tbreak;\n',
        'SetDCEClock 2.2',
    )


def patch_radeon_edp_edid() -> None:
    path = HAIKU / "src/add-ons/accelerants/radeon_hd/display.cpp"
    replace_once(
        path,
        '\t\t\t\tgInfo->shared_info->has_edid = true;\n\t\t\t\tedid_dump(edid);\n\t\t\t}\n\t\t}\n',
        '\t\t\t\tgInfo->shared_info->has_edid = true;\n\t\t\t\tedid_dump(edid);\n\t\t\t}\n\t\t\tif (!gDisplay[displayIndex]->attached\n\t\t\t\t&& gConnector[id]->type == VIDEO_CONNECTOR_EDP\n\t\t\t\t&& gInfo->shared_info->has_edid) {\n\t\t\t\tTRACE("%s: connector(%" B_PRIu32\n\t\t\t\t\t"): using boot VESA EDID for eDP.\\n", __func__, id);\n\t\t\t\tmemcpy(&gDisplay[displayIndex]->edidData,\n\t\t\t\t\t&gInfo->shared_info->edid_info, sizeof(edid1_info));\n\t\t\t\tgDisplay[displayIndex]->attached = true;\n\t\t\t}\n\t\t}\n',
        'eDP boot EDID',
    )


def patch_radeon_vram_sentinel() -> None:
    path = HAIKU / "src/add-ons/kernel/drivers/graphics/radeon_hd/radeon_hd.cpp"
    replace_once(
        path,
        '\tif (info.chipsetID >= RADEON_TAHITI) {\n\t\t// Tahiti+ has memory stored in MB\n\t\tinfo.shared_info->graphics_memory_size\n\t\t\t= read32(info.registers + CONFIG_MEMSIZE_TAHITI) * 1024;\n\t} else if (info.chipsetID >= RADEON_CEDAR) {\n',
        '\tif (info.chipsetID >= RADEON_TAHITI) {\n\t\t// Tahiti+ stores this in MB. Raven leaves the register at\n\t\t// 0x3fffff, which prints as 4194303 MB. Treat that as unknown.\n\t\tuint32 memMb = read32(info.registers + CONFIG_MEMSIZE_TAHITI);\n\t\tif (memMb == 0 || memMb > 65536)\n\t\t\tinfo.shared_info->graphics_memory_size = 0;\n\t\telse\n\t\t\tinfo.shared_info->graphics_memory_size = memMb * 1024;\n\t} else if (info.chipsetID >= RADEON_CEDAR) {\n',
        'Raven CONFIG_MEMSIZE sentinel',
    )


def patch_radeon_vram_bar() -> None:
    path = HAIKU / "src/add-ons/kernel/drivers/graphics/radeon_hd/radeon_hd.cpp"
    replace_once(
        path,
        '\t\tinfo.shared_info->frame_buffer_size = fbBarSize;\n\t} else if (info.shared_info->graphics_memory_size > fbBarSize) {\n',
        '\t\tinfo.shared_info->frame_buffer_size = fbBarSize;\n\t\tinfo.shared_info->graphics_memory_size = fbBarSize;\n\t} else if (info.shared_info->graphics_memory_size > fbBarSize) {\n',
        'report BAR as video memory',
    )


def patch_radeon_raven_mc() -> None:
    path = HAIKU / "src/add-ons/accelerants/radeon_hd/gpu.cpp"
    replace_once(
        path,
        '\tif (info.chipsetID >= RADEON_CAYMAN)\n\t\treturn radeon_gpu_mc_setup_evergreen();\t// also for ni\n',
        '\tif (info.chipsetID >= RADEON_RAVEN) {\n\t\t// Evergreen MC registers are not the Raven display controller.\n\t\t// Programming them halts a controller this driver does not own.\n\t\tTRACE("%s: leaving the boot memory aperture alone\\n", __func__);\n\t\treturn B_OK;\n\t}\n\tif (info.chipsetID >= RADEON_CAYMAN)\n\t\treturn radeon_gpu_mc_setup_evergreen();\t// also for ni\n',
        'skip Evergreen MC on Raven',
    )


def main() -> int:
    patch_packages_first_boot()
    patch_input_retry()
    patch_radeon_connector()
    patch_radeon_barcelo()
    patch_radeon_display_object()
    patch_radeon_display_object_branch()
    patch_radeon_gpio_v21()
    patch_radeon_gpio_i2c_optional()
    patch_radeon_dce_clock()
    patch_radeon_edp_edid()
    patch_radeon_vram_sentinel()
    patch_radeon_vram_bar()
    patch_radeon_raven_mc()
    patch_radeon_cedar_fb()
    patch_drivesetup_size()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
