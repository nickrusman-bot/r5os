/*
 * MIDI USB driver
 * usb_midi.cpp
 *
 * Copyright 2006-2026 Haiku Inc.  All rights reserved.
 * Distributed under the terms of the MIT Licence.
 *
 * Authors:
 *		Jérôme Duval
 *		Pete Goodeve <pete.goodeve@computer.org>
 *		Philippe Houdoin
 *
 *		Some portions of this code were originally derived from
 *		USB Joystick driver for BeOS R5
 *		Copyright 2000 (C) ITO, Takayuki
 *		All rights reserved
 *
 */


/* #define DEBUG 1 */ /* Define this to enable DPRINTF_DEBUG statements */
/* (Other categories of printout set in usb_midi.h) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "midi_driver.h"
#include "usb_midi.h"
#include "usb_midi_velocity.h"
#include "../../../../../../headers/os/midi2/Midi2Translate.h"
#include "usb_midi_parse.h"


const char* midi_base_name = "midi/usb/";

static status_t
GetDescriptorString(const usb_device* device, int stringID, char* output, size_t outputMaxSize)
{
	if (device == NULL || stringID == 0 || output == NULL || outputMaxSize < 2)
		return B_ERROR;

	size_t actualLength = 0;
	uint8 tmp[4];

	// first, fetch default language ID
	status_t status
		= gUSBModule->get_descriptor(device, USB_DESCRIPTOR_STRING, 0, 0, tmp, 4, &actualLength);
	if (status < B_OK || actualLength != 4 || tmp[1] != USB_DESCRIPTOR_STRING)
		return status < B_OK ? status : B_IO_ERROR;

	const uint16 langID = tmp[2] | (tmp[3] << 8);

	// next fetch the UTF16 string length, in bytes
	status = gUSBModule->get_descriptor(device, USB_DESCRIPTOR_STRING, stringID, langID, tmp, 2,
		&actualLength);
	if (status < B_OK || actualLength != 2 || tmp[1] != USB_DESCRIPTOR_STRING)
		return status < B_OK ? status : B_IO_ERROR;

	uint8 stringDescriptorLength = tmp[0];
	if (stringDescriptorLength == 0) {
		output[0] = 0;
		return B_OK;
	}

	char stringDescriptor[256];

	// retrieve the UTF16 string
	status = gUSBModule->get_descriptor(device, USB_DESCRIPTOR_STRING, stringID, langID,
		stringDescriptor, stringDescriptorLength, &actualLength);
	if (status < B_OK || actualLength != stringDescriptorLength)
		return status < B_OK ? status : B_IO_ERROR;

	// lazy UTF16 conversion, assuming default language is english
	// in little-endian, per USB spec

	int len = MIN(outputMaxSize - 1, (size_t)((stringDescriptorLength - 1) / 2));
	char* s = &stringDescriptor[2];
	int i = 0;
	for (; i < len; i++) {
		output[i] = *s;
		s += 2;
	}
	output[i] = 0;

	return B_OK;
}


usbmidi_port_info*
create_usbmidi_port(usbmidi_device_info* devinfo,
	int cable, bool has_in, bool has_out)
{
	usbmidi_port_info* port = NULL;
	assert(gUSBModule != NULL && devinfo != NULL);

	port = (usbmidi_port_info*)malloc(sizeof(usbmidi_port_info));
	if (port == NULL)
		return NULL;

	sprintf(port->name, "%s/%d", devinfo->name, cable);

	port->device = devinfo;
	port->cable = cable;
	port->next = NULL;
	port->open = 0;
	port->open_fd = NULL;
	port->removed = false;
	port->has_in = has_in;
	port->has_out = has_out;
	port->rbuf = create_ring_buffer(1024);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	port->ump_capacity = 64;
	port->ump_packets = (midi_ump_packet*)malloc(
		sizeof(midi_ump_packet) * port->ump_capacity);
	port->ump_head = 0;
	port->ump_tail = 0;
	port->ump_count = 0;
	port->ump_pending_len = 0;
	port->ump_pending_pos = 0;
	port->sysex_hold_len = 0;
	if (port->rbuf == NULL || port->ump_packets == NULL) {
		if (port->rbuf != NULL)
			delete_ring_buffer(port->rbuf);
		free(port->ump_packets);
		free(port);
		return NULL;
	}
// SPDX-SnippetEnd

	devinfo->ports[cable] = port;
	devinfo->port_count++;

	DPRINTF_INFO((MY_ID "Created port %p cable %d: %s\n",
		port, cable, port->name));

	return port;
}


void
remove_port(usbmidi_port_info* port)
{
	assert(port != NULL);
	if (port->rbuf != NULL) {
		delete_ring_buffer(port->rbuf);
		port->rbuf = NULL;
	}
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	free(port->ump_packets);
	port->ump_packets = NULL;
// SPDX-SnippetEnd
	port->device->port_count--;
	DPRINTF_INFO((MY_ID "remove_port %p done\n", port));

	free(port);
}


usbmidi_device_info*
create_device(const usb_device* usbDevice, const usb_device_descriptor* deviceDescriptor,
	uint16 ifno)
{
	usbmidi_device_info* midiDevice = NULL;
	int number;
	area_id area;
	sem_id sem;
	char area_name[32];

	assert(gUSBModule != NULL && usbDevice != NULL);

	number = find_free_device_number();

	midiDevice = (usbmidi_device_info*)malloc(sizeof(usbmidi_device_info));
	if (midiDevice == NULL)
		return NULL;

	memset(midiDevice, 0, sizeof(usbmidi_device_info));
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	/* No Stream Configuration yet: MIDI 1.0 protocol. */
	midiDevice->ump_protocol = 1;
// SPDX-SnippetEnd

	midiDevice->sem_lock = sem = create_sem(1, DRIVER_NAME "_lock");
	if (sem < 0) {
		DPRINTF_ERR((MY_ID "create_sem() failed 0x%" B_PRIx32 "\n", sem));
		free(midiDevice);
		return NULL;
	}

	sprintf(area_name, DRIVER_NAME "_%d_buffers", number);
	midiDevice->buffer_area = area = create_area(area_name,
		(void**)&midiDevice->buffer, B_ANY_KERNEL_ADDRESS,
		B_PAGE_SIZE, B_CONTIGUOUS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (area < 0) {
		DPRINTF_ERR((MY_ID "create_area() failed 0x%" B_PRIx32 "\n", area));
		delete_sem(midiDevice->sem_lock);
		free(midiDevice);
		return NULL;
	}
	/* use half of reserved area for each of in and out buffers: */
	midiDevice->out_buffer = 
		(usb_midi_event_packet*)((uint8*)midiDevice->buffer + B_PAGE_SIZE / 2);
	midiDevice->sem_send =  sem = create_sem(1, DRIVER_NAME "_send");
	if (sem < 0) {
		DPRINTF_ERR((MY_ID "create_sem() failed 0x%" B_PRIx32 "\n", sem));
		delete_sem(midiDevice->sem_lock);
		delete_area(area);
		free(midiDevice);
		return NULL;
	}
	{
		int32 bc;
		get_sem_count(sem, &bc);
		DPRINTF_DEBUG((MY_ID "Allocated %" B_PRId32 " write buffers\n", bc));
	}

	snprintf(midiDevice->name, sizeof(midiDevice->name), "%s%d", midi_base_name, number);
	midiDevice->dev = usbDevice;
	midiDevice->devnum = number;
	midiDevice->ifno = ifno;
	midiDevice->active = true;
	midiDevice->flags = 0;
	midiDevice->consecutive_read_errors = 0;

	memset(midiDevice->ports, 0, sizeof(midiDevice->ports));
	midiDevice->inMaxPkt = midiDevice->outMaxPkt = B_PAGE_SIZE / 2; // Initially -- will get reduced

	midiDevice->vendor_id = deviceDescriptor->vendor_id;
	midiDevice->product_id = deviceDescriptor->product_id;

	GetDescriptorString(usbDevice, deviceDescriptor->manufacturer, midiDevice->manufacturer_name,
		sizeof(midiDevice->manufacturer_name));
	GetDescriptorString(usbDevice, deviceDescriptor->product, midiDevice->product_name,
		sizeof(midiDevice->product_name));
	GetDescriptorString(usbDevice, deviceDescriptor->serial_number, midiDevice->serial_number,
		sizeof(midiDevice->serial_number));

	DPRINTF_INFO((MY_ID "Manufacturer:  0x%04X (%s) \n", midiDevice->vendor_id,
		midiDevice->manufacturer_name));
	DPRINTF_INFO((MY_ID "Product:       0x%04X (%s) \n", midiDevice->product_id,
		midiDevice->product_name));
	DPRINTF_INFO((MY_ID "Serial Number: %s\n", midiDevice->serial_number));

	DPRINTF_INFO((MY_ID "Created device %p\n", midiDevice));

	return midiDevice;
}


void
remove_device(usbmidi_device_info* midiDevice)
{
	assert(midiDevice != NULL);
	DPRINTF_INFO((MY_ID "remove_device %p\n", midiDevice));

	delete_area(midiDevice->buffer_area);
	delete_sem(midiDevice->sem_lock);
	delete_sem(midiDevice->sem_send);
	free(midiDevice);
}


/* driver cookie (per open -- but only one open per port allowed!) */

typedef struct driver_cookie
{
	struct driver_cookie*	next;
	usbmidi_device_info*	device;	/* a bit redundant, but convenient */
	usbmidi_port_info*		port;
	sem_id sem_cb;
} driver_cookie;


/* NB global variables are valid only while driver is loaded */

_EXPORT int32	api_version = B_CUR_DRIVER_API_VERSION;

static const char* usb_midi_driver_name = "usb_midi";

static const int CINbytes[] = {	/* See USB-MIDI Spec */
	0,	/* 0x0 -- undefined Misc -- Reserved */
	0,	/* 0x1 -- undefined  Cable -- Reserved */
	2,	/* 0x2 -- 2-byte system common */
	3,	/* 0x3 -- 3-byte system common */
	3,	/* 0x4 -- SysEx start/continue */
	1,	/* 0x5 -- SysEx single-byte-end, or 1-byte Common */
	2,	/* 0x6 --  SysEx two-byte-end */
	3,	/* 0x7 --  SysEx three-byte-end */
	3,	/* 0x8 --  Note Off */
	3,	/* 0x9 --  Note On */
	3,	/* 0xA --  Poly KeyPress*/
	3,	/* 0xB --  Control Change */
	2,	/* 0xC --  Program Change */
	2,	/* 0xD --  Channel Pressure */
	3,	/* 0xE --  Pitch Bend */
	1,	/* 0xF --  Single Byte */
};

usb_module_info* gUSBModule = NULL;


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
static uint32
ump_word_count(uint32 first)
{
	return usb_midi_ump_word_count(first);
}


static void
push_ump_packet(usbmidi_port_info* port, const uint32* words, uint32 count)
{
	if (port == NULL || port->open_fd == NULL || port->ump_packets == NULL
		|| count == 0 || count > 4)
		return;
	if (port->ump_count >= port->ump_capacity) {
		DPRINTF_ERR((MY_ID "UMP queue full\n"));
		return;
	}

	midi_ump_packet* slot = &port->ump_packets[port->ump_head];
	slot->word_count = count;
	for (uint32 i = 0; i < 4; i++)
		slot->words[i] = i < count ? words[i] : 0;
	port->ump_head++;
	if (port->ump_head >= port->ump_capacity)
		port->ump_head = 0;
	port->ump_count++;
	release_sem_etc(port->open_fd->sem_cb, 1, B_DO_NOT_RESCHEDULE);
}


static int
sysex7_bytes(const uint32* words, uint8* status, uint8 data[6])
{
	return usb_midi_sysex7_bytes(words, status, data);
}


static int
expand_ump_to_midi1(const midi_ump_packet* packet, uint8* out)
{
	return usb_midi_expand_ump(packet->words, packet->word_count, out);
}


static void
deliver_ump_packet(void* cookie, uint32_t group, const uint32_t* words,
	uint32_t count)
{
	usbmidi_device_info* midiDevice = (usbmidi_device_info*)cookie;
	usbmidi_port_info* port = group < 16 ? midiDevice->ports[group] : NULL;
	/* The device reports its protocol with a Stream Configuration
	 * Notification (status 0x006). 0x005 is the Request, which only the host
	 * sends, so the protocol never changed before. */
	if ((words[0] >> 28) == 0xfu && ((words[0] >> 16) & 0x3ffu) == 0x006u
		&& count >= 1) {
		uint8 protocol = (uint8)((words[0] >> 8) & 0xffu);
		if (protocol == 1 || protocol == 2)
			midiDevice->ump_protocol = protocol;
	}
	if (port != NULL)
		push_ump_packet(port, (const uint32*)words, count);
}


static void
interpret_ump_buffer(usbmidi_device_info* midiDevice)
{
	usb_midi_walk_ump((const uint8*)midiDevice->buffer, midiDevice->actual_length,
		deliver_ump_packet, midiDevice);
}
// SPDX-SnippetEnd


static void
interpret_midi_buffer(usbmidi_device_info* midiDevice)
{
	usb_midi_event_packet* packet = midiDevice->buffer;
	size_t bytes_left = midiDevice->actual_length;

	/* buffer may have several packets */
	while (bytes_left >= sizeof(usb_midi_event_packet)) {
		DPRINTF_DEBUG((MY_ID "received packet %x:%d %x %x %x\n",
			packet->cin, packet->cn,
			packet->midi[0], packet->midi[1], packet->midi[2]));

		int pktlen = CINbytes[packet->cin];
		usbmidi_port_info* port = midiDevice->ports[packet->cn];

		/* port matching 'cable number' */
		if (port == NULL) {
			DPRINTF_ERR((MY_ID "no port matching cable number %d!\n",
				packet->cn));
		} else if (port->open_fd == NULL) {
			DPRINTF_DEBUG((MY_ID "received data for port %d but it is closed!\n", packet->cn));
		} else {
			ring_buffer_write(port->rbuf, packet->midi, pktlen);
			release_sem_etc(port->open_fd->sem_cb, pktlen,
				B_DO_NOT_RESCHEDULE);
		}

		packet++;
		bytes_left -= sizeof(usb_midi_event_packet);
	}
}


// #pragma mark - USB transfers callbacks


static void
usb_midi_read_callback(void* cookie, status_t status,
	void* data, size_t actual_len)
{
	status_t st;
	usbmidi_device_info* midiDevice = (usbmidi_device_info*)cookie;

	assert(cookie != NULL);
	if (actual_len > 0) {
		DPRINTF_DEBUG((MY_ID "midi_usb_read_callback() -- packet length %ld\n",	actual_len));
	}

	acquire_sem(midiDevice->sem_lock);
	midiDevice->actual_length = actual_len;
	midiDevice->bus_status = status;	/* B_USB_STATUS_* */
		if (status != B_OK) {
			/* request failed */
		DPRINTF_DEBUG((MY_ID "bus status 0x%" B_PRIx32 "\n", status));
		if (status == B_CANCELED || !midiDevice->active) {
			/* cancelled: device is unplugged */
			DPRINTF_DEBUG((MY_ID "midi_usb_read_callback: cancelled"
				"(status=%" B_PRIx32 " active=%d -- deleting sem_cbs\n",
				status, midiDevice->active));

			// Free any read() still blocked on semaphore
			for (int cable = 0; cable < 16; cable++) {
				usbmidi_port_info* port = midiDevice->ports[cable];
				if (port == NULL)
					continue;
				if (port->open_fd != NULL)
					delete_sem(port->open_fd->sem_cb);
			}
			release_sem(midiDevice->sem_lock);
			return;
		}

		// device still there, but (continuously?) returning errors
		midiDevice->consecutive_read_errors++;
		if (midiDevice->consecutive_read_errors > MAX_CONSECUTIVE_READ_ERRORS) {
			// Stop re-queue expecting a different result
			DPRINTF_ERR((MY_ID "%s: %d consecutive IN transfer errors: giving up. "
				"Device likely unplugged or faulty\n",
				midiDevice->name, midiDevice->consecutive_read_errors));
			release_sem(midiDevice->sem_lock);
			return;
		}
	} else {
		/* got a report */
		midiDevice->timestamp = system_time();	/* not used... */
		midiDevice->consecutive_read_errors = 0;

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
		if (midiDevice->ump_endpoint)
			interpret_ump_buffer(midiDevice);
		else
			interpret_midi_buffer(midiDevice);
// SPDX-SnippetEnd
	}

	release_sem(midiDevice->sem_lock);

	/* issue next request */
	if (midiDevice->in_is_interrupt) {
		st = gUSBModule->queue_interrupt(midiDevice->ept_in->handle,
			midiDevice->buffer, midiDevice->inMaxPkt,
			(usb_callback_func)usb_midi_read_callback, midiDevice);
	} else {
		st = gUSBModule->queue_bulk(midiDevice->ept_in->handle,
			midiDevice->buffer, midiDevice->inMaxPkt,
			(usb_callback_func)usb_midi_read_callback, midiDevice);
	}
	if (st != B_OK) {
		/* probably endpoint stall */
		DPRINTF_ERR((MY_ID "queue_bulk() error 0x%" B_PRIx32 "\n", st));
	}
}


static void
usb_midi_write_callback(void* cookie, status_t status, void* data, size_t actual_len)
{
	usbmidi_device_info* midiDevice = (usbmidi_device_info*)cookie;
#ifdef DEBUG
	usb_midi_event_packet* pkt = (usb_midi_event_packet*)data;
#endif

	assert(cookie != NULL);
	DPRINTF_DEBUG((MY_ID "midi_usb_write_callback()"
		" status %" B_PRId32 " length %" B_PRIuSIZE "  pkt %p cin %x\n",
		status, actual_len, pkt, pkt->cin));
	release_sem(midiDevice->sem_send); /* done with buffer */
}


// #pragma mark - USB module hooks


/* Roland and Edirol Advanced mode have no USB-MIDI descriptors. The
 * interface number and cable masks are per product. midi_interface -1
 * means "the interface that has a non-isochronous IN and OUT". Facts
 * from the device descriptors, cross-checked with ALSA's tables; no
 * ALSA code. RenkuOS cb61494f02, David Beckman, MIT.
 */
struct roland_midi_quirk {
	uint16	product_id;
	int		midi_interface;
	uint16	in_mask;
	uint16	out_mask;
};

static const roland_midi_quirk kRolandMidiQuirks[] = {
	{ 0x0000,  2, 0x0007, 0x0007 },		/* UA-100 */
	{ 0x0002,  2, 0x000f, 0x000f },		/* UM-4 */
	{ 0x0003,  2, 0x003f, 0x003f },		/* SC-8850 */
	{ 0x0004,  2, 0x0005, 0x0005 },		/* U-8 */
	{ 0x0005,  2, 0x0003, 0x0003 },		/* UM-2 */
	{ 0x0007,  2, 0x0013, 0x0013 },		/* SC-8820 */
	{ 0x0008,  2, 0x0001, 0x0001 },		/* PC-300 */
	{ 0x0009,  2, 0x0001, 0x0001 },		/* UM-1 */
	{ 0x000b,  2, 0x0013, 0x0013 },		/* SK-500 */
	{ 0x000c,  2, 0x0007, 0x0007 },		/* SC-D70 */
	{ 0x0012,  0, 0x0001, 0x0001 },		/* XV-5050 */
	{ 0x0014,  0, 0x01ff, 0x01ff },		/* UM-880 */
	{ 0x0016,  2, 0x000f, 0x000f },		/* SD-90 */
	{ 0x001b,  2, 0x0001, 0x0001 },		/* MMP-2 */
	{ 0x001d,  0, 0x0001, 0x0001 },		/* V-SYNTH */
	{ 0x0023,  0, 0x003f, 0x003f },		/* UM-550 */
	{ 0x0025,  3, 0x0001, 0x0001 },		/* UA-20 */
	{ 0x0027,  0, 0x0007, 0x0003 },		/* SD-20 */
	{ 0x0029,  0, 0x000f, 0x000f },		/* SD-80 */
	{ 0x002b, -1, 0x0003, 0x0003 },		/* UA-700 */
	{ 0x002d,  0, 0x0001, 0x0001 },		/* XV-2020 */
	{ 0x002f,  0, 0x0007, 0x0007 },		/* VariOS */
	{ 0x0033,  0, 0x0007, 0x0003 },		/* PCR */
	{ 0x0037,  0, 0x0001, 0x0001 },		/* Digital Piano */
	{ 0x0040,  0, 0x0001, 0x0001 },		/* GI-20 */
	{ 0x0042,  0, 0x0001, 0x0001 },		/* RS-70 */
	{ 0x0048,  0, 0x0007, 0x0003 },		/* UR-80 */
	{ 0x004d,  0, 0x0007, 0x0003 },		/* PCR-A */
	{ 0x0065,  0, 0x0003, 0x0001 },		/* PCR-1 */
	{ 0x006d,  0, 0x0001, 0x0001 },		/* FANTOM-X */
	{ 0x0074, -1, 0x0001, 0x0001 },		/* UA-25 */
	{ 0x0075,  0, 0x0001, 0x0001 },		/* DR-880 */
	{ 0x007a,  0, 0x0003, 0x0003 },		/* RD-700SX */
	{ 0x0080,  0, 0x0001, 0x0001 },		/* G-70 */
	{ 0x008b,  0, 0x0001, 0x0001 },		/* PC-50 */
	{ 0x00a3, -1, 0x0001, 0x0001 },		/* UA-4FX */
	{ 0x00c4,  2, 0x0001, 0x0001 },		/* M-16DX */
	{ 0x00e6, -1, 0x0001, 0x0001 },		/* UA-25EX */
	{ 0x0108,  0, 0x0007, 0x0007 },		/* UM-3G */
	{ 0x0113,  2, 0x0001, 0x0001 },		/* BOSS ME-25 */
	{ 0x0120,  2, 0x0001, 0x0001 },		/* OCTO-CAPTURE */
	{ 0x012f,  2, 0x0001, 0x0001 },		/* QUAD-CAPTURE */
	{ 0x0159,  2, 0x0001, 0x0001 },		/* UA-22 */
};


static const roland_midi_quirk*
find_roland_quirk(uint16 vendor, uint16 product)
{
	if (vendor != 0x0582)
		return NULL;
	const int count = (int)(sizeof(kRolandMidiQuirks)
		/ sizeof(kRolandMidiQuirks[0]));
	for (int i = 0; i < count; i++) {
		if (kRolandMidiQuirks[i].product_id == product)
			return &kRolandMidiQuirks[i];
	}
	return NULL;
}


static bool
interface_has_midi_io(const usb_interface_info* intf)
{
	bool hasIn = false;
	bool hasOut = false;
	for (uint16 i = 0; i < intf->endpoint_count; i++) {
		usb_endpoint_descriptor* ed = intf->endpoint[i].descr;
		if ((ed->attributes & 0x03) == 0x01)
			continue;
		if ((ed->endpoint_address & 0x80) != 0)
			hasIn = true;
		else
			hasOut = true;
	}
	return hasIn && hasOut;
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
/* USB-MIDI 2.0 § 3.1: alternate 0 of a MIDIStreaming interface is USB MIDI
 * 1.0. The alternate whose MS header has bcdMSC 0x0200 carries Universal
 * MIDI Packets. Returns NULL when the interface has none.
 */
static const usb_interface_info*
find_midi2_alternate(const usb_configuration_info* conf, uint16 ifno)
{
	for (size_t alt = 0; alt < conf->interface[ifno].alt_count; alt++) {
		const usb_interface_info* candidate = &conf->interface[ifno].alt[alt];
		if (candidate->descr == NULL || candidate->endpoint_count == 0)
			continue;
		for (size_t g = 0; g < candidate->generic_count; g++) {
			usb_generic_descriptor* gd = &candidate->generic[g]->generic;
			if (gd->length >= 5
				&& gd->descriptor_type == USB_DESCRIPTOR_CS_INTERFACE
				&& gd->data[0] == USB_MS_HEADER_DESCRIPTOR
				&& (gd->data[1] | ((uint16)gd->data[2] << 8)) >= 0x0200) {
				return candidate;
			}
		}
	}
	return NULL;
}


/* The Group Terminal Blocks are not in the configuration descriptor. The
 * host asks for them with GET_DESCRIPTOR, wValue CS_GR_TRM_BLOCK (0x26) and
 * the alternate setting, wIndex the interface (USB-MIDI 2.0 § 5.4). First
 * the 5-byte header for wTotalLength, then the whole set, as Linux does.
 * Returns the groups the blocks use, as a mask; 0 when they cannot be read.
 */
static uint16
read_group_terminal_blocks(const usb_device* dev, const usb_interface_info* intf)
{
	uint8 buffer[256];
	size_t actual = 0;
	uint16 value = (0x26 << 8) | intf->descr->alternate_setting;
	uint16 index = intf->descr->interface_number;
	status_t status = gUSBModule->send_request(dev,
		USB_REQTYPE_INTERFACE_IN | USB_REQTYPE_STANDARD,
		USB_REQUEST_GET_DESCRIPTOR, value, index, 5, buffer, 5, &actual);
	if (status != B_OK || actual < 5 || buffer[0] < 5 || buffer[1] != 0x26
		|| buffer[2] != 0x01) {
		return 0;
	}
	size_t total = buffer[3] | ((size_t)buffer[4] << 8);
	if (total > sizeof(buffer))
		total = sizeof(buffer);
	if (total < 5)
		return 0;
	status = gUSBModule->send_request(dev,
		USB_REQTYPE_INTERFACE_IN | USB_REQTYPE_STANDARD,
		USB_REQUEST_GET_DESCRIPTOR, value, index, total, buffer, total,
		&actual);
	if (status != B_OK || actual < 5)
		return 0;

	uint16 groups = 0;
	size_t offset = buffer[0];
	while (offset + 2 <= actual) {
		const uint8* block = buffer + offset;
		uint8 length = block[0];
		if (length < 3 || offset + length > actual)
			break;
		/* bDescriptorSubtype 0x02 is GR_TRM_BLOCK; 0x01 is the header. */
		if (block[1] == 0x26 && block[2] == 0x02 && length >= 13) {
			uint8 first = block[5];
			uint8 count = block[6];
			for (uint16 g = first; g < (uint16)first + count && g < 16; g++)
				groups |= (uint16)(1u << g);
		}
		offset += length;
	}
	return groups;
}
// SPDX-SnippetEnd


static status_t
usb_midi_added(const usb_device* dev, void** cookie)
{
	/* This seems overcomplicated, but endpoints can be in either order...
	and could possibly have different number of connectors! */
	int in_cables = 0, out_cables = 0;
	int cable_count[2] = {0, 0};
	int iep = 0;
	status_t status;

	assert(dev != NULL && cookie != NULL);
	DPRINTF_INFO((MY_ID "usb_midi_added(%p, %p)\n", dev, cookie));

	const usb_device_descriptor* dev_desc = gUSBModule->get_device_descriptor(dev);

	DPRINTF_INFO((MY_ID "vendor ID 0x%04X, product ID 0x%04X\n",
		dev_desc->vendor_id, dev_desc->product_id));

	const roland_midi_quirk* roland
		= find_roland_quirk(dev_desc->vendor_id, dev_desc->product_id);

	/* check interface class */
	const usb_configuration_info* conf;
	if ((conf = gUSBModule->get_nth_configuration(dev, DEFAULT_CONFIGURATION)) == NULL) {
		DPRINTF_ERR((MY_ID "cannot get default configuration\n"));
		return B_ERROR;
	}
	DPRINTF_INFO((MY_ID "Interface count = %ld\n", conf->interface_count));

	uint16 alt, ifno;
	const usb_interface_info* intf;
	for (ifno = 0; ifno < conf->interface_count; ifno++) {
		int devclass, subclass, protocol;

		for (alt = 0; alt < conf->interface[ifno].alt_count; alt++) {
			intf = &conf->interface[ifno].alt[alt];
			devclass    = intf->descr->interface_class;
			subclass = intf->descr->interface_subclass;
			protocol = intf->descr->interface_protocol;
			DPRINTF_INFO((
				MY_ID "interface %d, alt : %d: class %d,"
				" subclass %d, protocol %d\n",
                ifno, alt, devclass, subclass, protocol));

			if (roland != NULL
				&& (roland->midi_interface < 0
					|| (int)ifno == roland->midi_interface)
				&& interface_has_midi_io(intf))
				goto got_one;

			if (devclass == USB_AUDIO_DEVICE_CLASS
				&& subclass == USB_AUDIO_INTERFACE_MIDISTREAMING_SUBCLASS)
				goto got_one;

			/* Yamaha puts standard USB-MIDI descriptors in a vendor-specific
			 * interface (RenkuOS fe2de4aa60, David Beckman, MIT). Skip audio
			 * class: a UAC header is also CS_INTERFACE subtype 0x01 and would
			 * match before the real MIDIStreaming interface.
			 */
			if (devclass == USB_AUDIO_DEVICE_CLASS)
				continue;
			for (uint16 g = 0; g < intf->generic_count; g++) {
				usb_generic_descriptor* gd = &intf->generic[g]->generic;
				if (gd->length >= 3
					&& gd->descriptor_type == USB_DESCRIPTOR_CS_INTERFACE
					&& gd->data[0] == USB_MS_HEADER_DESCRIPTOR)
					goto got_one;
			}
		}
	}

	DPRINTF_INFO((MY_ID "Midi interface not found\n"));
	return B_ERROR;

got_one:

	if ((status = gUSBModule->set_configuration(dev, conf)) != B_OK) {
		DPRINTF_ERR((MY_ID "set_configuration() failed 0x%" B_PRIx32 "\n",
			status));
		return B_ERROR;
	}

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	/* A MIDI 2.0 device starts in alternate 0, USB MIDI 1.0. Select the
	 * MIDI 2.0 alternate; when that fails, stay on MIDI 1.0. */
	if (roland == NULL) {
		const usb_interface_info* midi2 = find_midi2_alternate(conf, ifno);
		if (midi2 != NULL && midi2 != intf) {
			status = gUSBModule->set_alt_interface(dev, midi2);
			if (status == B_OK) {
				intf = midi2;
				DPRINTF_INFO((MY_ID "interface %u: USB MIDI 2.0 alternate %u\n",
					ifno, midi2->descr->alternate_setting));
			} else {
				DPRINTF_ERR((MY_ID "interface %u: cannot select the MIDI 2.0 "
					"alternate (0x%" B_PRIx32 "), staying on MIDI 1.0\n",
					ifno, status));
			}
		}
	}
// SPDX-SnippetEnd

	usbmidi_device_info* midiDevice;
	if ((midiDevice = create_device(dev, dev_desc, ifno)) == NULL)
		return B_ERROR;

	/* get the actual number of  ports in and out */
	for (uint16 i = 0; i < intf->generic_count; i++) {
		usb_generic_descriptor *generic = &intf->generic[i]->generic;
		DPRINTF_DEBUG((MY_ID "descriptor %d: type %x sub %x\n",
			i, generic->descriptor_type, generic->data[0]));
		if (generic->descriptor_type == USB_DESCRIPTOR_CS_INTERFACE
			&& generic->length >= 5
			&& generic->data[0] == USB_MS_HEADER_DESCRIPTOR) {
			uint16 version = generic->data[1] | ((uint16)generic->data[2] << 8);
			if (version >= 0x0200)
				midiDevice->ump_endpoint = true;
		}
		if (generic->descriptor_type == USB_DESCRIPTOR_CS_ENDPOINT
			&& generic->data[0] == USB_MS_GENERAL_DESCRIPTOR) {
			/* These *better* be in the same order as the endpoints! */
			cable_count[iep] = generic->data[1];
			iep = 1;
		}
	}

	DPRINTF_DEBUG((MY_ID "midiDevice = %p endpoint count = %ld\n",
		midiDevice, intf->endpoint_count));
	midiDevice->ept_in = midiDevice->ept_out = NULL;

	for (uint16 i = 0; i < intf->endpoint_count; i++) {
		usb_endpoint_descriptor* ed = intf->endpoint[i].descr;
		bool isIn = (ed->endpoint_address & 0x80) != 0;
		bool isInterrupt = (ed->attributes & 0x03) == 0x03;
		DPRINTF_INFO((MY_ID "endpoint %d = %p  %s %s maxPkt=%d\n",
			i, &intf->endpoint[i], isIn ? "IN" : "OUT",
			isInterrupt ? "interrupt" : "bulk", ed->max_packet_size));
		if (isIn) {
			if (midiDevice->ept_in == NULL) {
				midiDevice->ept_in = &intf->endpoint[i];
				midiDevice->in_is_interrupt = isInterrupt;
				in_cables = (i < 2) ? cable_count[i] : 0;
				if (ed->max_packet_size < midiDevice->inMaxPkt)
					midiDevice->inMaxPkt = ed->max_packet_size;
			}
		} else if (midiDevice->ept_out == NULL) {
			midiDevice->ept_out = &intf->endpoint[i];
			midiDevice->out_is_interrupt = isInterrupt;
			out_cables = (i < 2) ? cable_count[i] : 0;
			if (ed->max_packet_size < midiDevice->outMaxPkt)
				midiDevice->outMaxPkt = ed->max_packet_size;
		}
	}

	/* No CS_ENDPOINT cable count: count MIDI IN and OUT jack descriptors.
	 * Yamaha marks them EXTERNAL. Cap at 16. RenkuOS fe2de4aa60.
	 */
	if (roland != NULL) {
		DPRINTF_INFO((MY_ID "roland cables: in_mask=0x%x out_mask=0x%x\n",
			(unsigned int)roland->in_mask, (unsigned int)roland->out_mask));
	} else if (midiDevice->ump_endpoint) {
		/* UMP groups come from the Group Terminal Blocks, below. */
	} else if (in_cables == 0 && out_cables == 0) {
		for (uint16 i = 0; i < intf->generic_count; i++) {
			usb_generic_descriptor* gd = &intf->generic[i]->generic;
			if (gd->length < 3
				|| gd->descriptor_type != USB_DESCRIPTOR_CS_INTERFACE)
				continue;
			if (gd->data[0] == USB_MS_MIDI_IN_JACK_DESCRIPTOR)
				in_cables++;
			else if (gd->data[0] == USB_MS_MIDI_OUT_JACK_DESCRIPTOR)
				out_cables++;
		}
		if (in_cables > 16)
			in_cables = 16;
		if (out_cables > 16)
			out_cables = 16;
		DPRINTF_INFO((MY_ID "jack-counted cables: in=%d out=%d\n",
			in_cables, out_cables));
	}

	midiDevice->timestamp = system_time();	/* This never seems to be used */

	/* Create the actual device ports */
	usbmidi_port_info* port;
	if (roland != NULL) {
		for (uint16 c = 0; c < 16; c++) {
			bool cin = (roland->in_mask & (1u << c)) != 0;
			bool cout = (roland->out_mask & (1u << c)) != 0;
			if (!cin && !cout)
				continue;
			port = create_usbmidi_port(midiDevice, c, cin, cout);
			if (port != NULL)
				add_port_info(port);
		}
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	} else if (midiDevice->ump_endpoint) {
		/* One port per UMP group, the port number is the group. Without
		 * readable blocks, group 1 (index 0) only. */
		uint16 groups = read_group_terminal_blocks(dev, intf);
		if (groups == 0) {
			DPRINTF_ERR((MY_ID "no Group Terminal Blocks, using group 1\n"));
			groups = 1;
		}
		for (uint16 g = 0; g < 16; g++) {
			if ((groups & (1u << g)) == 0)
				continue;
			port = create_usbmidi_port(midiDevice, g,
				midiDevice->ept_in != NULL, midiDevice->ept_out != NULL);
			if (port != NULL)
				add_port_info(port);
		}
// SPDX-SnippetEnd
	} else {
		for (uint16 i = 0; in_cables > 0 || out_cables > 0; i++) {
			port = create_usbmidi_port(midiDevice, i,
				(bool)in_cables, (bool)out_cables);
			midiDevice->ports[i] = port;
			if (in_cables)
				in_cables--;
			if (out_cables)
				out_cables--;
			add_port_info(port);
		}
	}

	/* issue bulk input transfer */
	if (midiDevice->ept_in != NULL) {
		DPRINTF_DEBUG((MY_ID "queueing bulk xfer IN endpoint\n"));
		if (midiDevice->in_is_interrupt) {
			status = gUSBModule->queue_interrupt(midiDevice->ept_in->handle,
				midiDevice->buffer, midiDevice->inMaxPkt,
				(usb_callback_func)usb_midi_read_callback, midiDevice);
		} else {
			status = gUSBModule->queue_bulk(midiDevice->ept_in->handle,
				midiDevice->buffer, midiDevice->inMaxPkt,
				(usb_callback_func)usb_midi_read_callback, midiDevice);
		}
		if (status != B_OK) {
			DPRINTF_ERR((MY_ID "queue_bulk() error 0x%" B_PRIx32 "\n", status));
			return B_ERROR;
		}
	}

	*cookie = midiDevice;
	DPRINTF_INFO((MY_ID "usb_midi_added: %s\n", midiDevice->name));

	return B_OK;
}


static status_t
usb_midi_removed(void* cookie)
{
	usbmidi_device_info* midiDevice = (usbmidi_device_info*)cookie;

	assert(cookie != NULL);

	DPRINTF_INFO((MY_ID "usb_midi_removed(%s)\n", midiDevice->name));
	midiDevice->active = false;

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	/* A program can still be inside read(), write() or ioctl() on a port of
	 * this device. Those hooks hold the device and port pointers, so both
	 * used to be freed under them. Now a port that is open, and the device
	 * while any of its ports is open, stay allocated until usb_midi_free().
	 * The hooks see active == false and return; deleting the semaphore
	 * wakes a blocked reader.
	 */
	if (midiDevice->ept_in != NULL)
		gUSBModule->cancel_queued_transfers(midiDevice->ept_in->handle);
	if (midiDevice->ept_out != NULL)
		gUSBModule->cancel_queued_transfers(midiDevice->ept_out->handle);

	usbmidi_port_info* ports[16];
	for (int cable = 0; cable < 16; cable++) {
		ports[cable] = midiDevice->ports[cable];
		if (ports[cable] != NULL)
			remove_port_info(ports[cable]);
	}

	acquire_sem(usbmidi_port_list_lock);
	for (int cable = 0; cable < 16; cable++) {
		usbmidi_port_info* port = ports[cable];
		if (port == NULL)
			continue;
		DPRINTF_DEBUG((MY_ID "removing port %d\n", cable));
		if (port->open_fd != NULL) {
			delete_sem(port->open_fd->sem_cb);
			port->removed = true;
			ports[cable] = NULL;
				/* freed by usb_midi_free() */
		}
	}
	bool freeDevice = midiDevice->open_count == 0;
	midiDevice->removed = true;
	release_sem(usbmidi_port_list_lock);

	for (int cable = 0; cable < 16; cable++) {
		if (ports[cable] != NULL) {
			midiDevice->ports[cable] = NULL;
			remove_port(ports[cable]);
		}
	}
	if (freeDevice) {
		DPRINTF_DEBUG((MY_ID "usb_midi_removed: doing remove: %s\n",
			midiDevice->name));
		remove_device(midiDevice);
	}
// SPDX-SnippetEnd
	return B_OK;
}


static usb_notify_hooks my_notify_hooks = {
	usb_midi_added,
	usb_midi_removed
};

#define	SUPPORTED_DEVICES	3
usb_support_descriptor my_supported_devices[SUPPORTED_DEVICES] =
{
	{
		USB_AUDIO_DEVICE_CLASS,
		USB_AUDIO_INTERFACE_MIDISTREAMING_SUBCLASS,
		0, 0, 0
	},
	/* Yamaha 0x0499. usb_midi_added() still requires a MIDI header, so a
	 * Yamaha device that is only audio is rejected there.
	 */
	{ 0, 0, 0, 0x0499, 0 },
	/* Roland / Edirol 0x0582. Advanced mode is the quirk table. Standard
	 * mode still matches the class descriptors. Anything else is rejected.
	 */
	{ 0, 0, 0, 0x0582, 0 },
};


// #pragma mark device hooks


static status_t
usb_midi_open(const char* name, uint32 flags,
	driver_cookie** out_cookie)
{
	driver_cookie* cookie;
	usbmidi_port_info* port;
	int mode = flags & O_RWMASK;

	assert(name != NULL);
	assert(out_cookie != NULL);
	DPRINTF_INFO((MY_ID "usb_midi_open(%s) flags=%" B_PRIx32 "\n", name,
		flags));

	if ((port = search_port_info(name)) == NULL)
		return B_ENTRY_NOT_FOUND;

	if (!port->has_in && mode != O_WRONLY)
		return B_PERMISSION_DENIED;	 /* == EACCES */
	else if (!port->has_out && mode != O_RDONLY)
		return B_PERMISSION_DENIED;

	if ((cookie = (driver_cookie*)malloc(sizeof(driver_cookie))) == NULL)
		return B_NO_MEMORY;

	cookie->sem_cb = create_sem(0, DRIVER_NAME "_cb");
	if (cookie->sem_cb < 0) {
		DPRINTF_ERR((MY_ID "create_sem() failed 0x%" B_PRIx32 "\n",
			cookie->sem_cb));
		free(cookie);
		return B_ERROR;
	}

	cookie->port = port;
	cookie->device = port->device;

	acquire_sem(usbmidi_port_list_lock);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	if (port->device->removed) {
		delete_sem(cookie->sem_cb);
		free(cookie);
		release_sem(usbmidi_port_list_lock);
		return B_ENTRY_NOT_FOUND;
	}
// SPDX-SnippetEnd
	if (port->open_fd != NULL) {
		/* there can only be one open channel to the device */
		delete_sem(cookie->sem_cb);
		free(cookie);
		release_sem(usbmidi_port_list_lock);
		return B_BUSY;
	}
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	port->ump_head = 0;
	port->ump_tail = 0;
	port->ump_count = 0;
	port->ump_pending_len = 0;
	port->ump_pending_pos = 0;
	port->sysex_hold_len = 0;
// SPDX-SnippetEnd
	port->open_fd = cookie;
	port->open++;
	port->device->open_count++;
	release_sem(usbmidi_port_list_lock);

	*out_cookie = cookie;
	DPRINTF_INFO((MY_ID "usb_midi_open: device %s open (%d)\n",
		name, port->open));
	return B_OK;
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
static bool
pop_ump_packet(usbmidi_port_info* port, midi_ump_packet* packet)
{
	if (port->ump_count <= 0 || port->ump_packets == NULL)
		return false;
	*packet = port->ump_packets[port->ump_tail];
	port->ump_tail++;
	if (port->ump_tail >= port->ump_capacity)
		port->ump_tail = 0;
	port->ump_count--;
	return true;
}


static status_t
read_ump_as_midi1(driver_cookie* cookie, void* buf, size_t* num_bytes)
{
	usbmidi_port_info* port = cookie->port;
	usbmidi_device_info* midiDevice = cookie->device;

	if (*num_bytes == 0)
		return B_OK;

	while (midiDevice != NULL && midiDevice->active) {
		acquire_sem(midiDevice->sem_lock);
		if (port->ump_pending_pos < port->ump_pending_len) {
			size_t have = port->ump_pending_len - port->ump_pending_pos;
			size_t count = have < *num_bytes ? have : *num_bytes;
			status_t copied = user_memcpy((uint8*)buf,
				port->ump_pending + port->ump_pending_pos, count);
			port->ump_pending_pos += count;
			release_sem(midiDevice->sem_lock);
			if (copied != B_OK)
				return B_BAD_ADDRESS;
			*num_bytes = count;
			return B_OK;
		}
		release_sem(midiDevice->sem_lock);

		status_t status = acquire_sem_etc(cookie->sem_cb, 1,
			B_RELATIVE_TIMEOUT | B_CAN_INTERRUPT, 1000000);
		if (status == B_TIMED_OUT)
			continue;
		if (status != B_OK) {
			*num_bytes = 0;
			return status;
		}

		acquire_sem(midiDevice->sem_lock);
		midi_ump_packet packet;
		bool havePacket = pop_ump_packet(port, &packet);
		if (!havePacket) {
			release_sem(midiDevice->sem_lock);
			continue;
		}
		uint8 expanded[16];
		int expandedLength = expand_ump_to_midi1(&packet, expanded);
		if (expandedLength <= 0) {
			release_sem(midiDevice->sem_lock);
			continue;
		}
		size_t count = (size_t)expandedLength < *num_bytes
			? (size_t)expandedLength : *num_bytes;
		status_t copied = user_memcpy(buf, expanded, count);
		if (copied == B_OK && count < (size_t)expandedLength) {
			port->ump_pending_len = (uint8)(expandedLength - count);
			port->ump_pending_pos = 0;
			memcpy(port->ump_pending, expanded + count, port->ump_pending_len);
		}
		release_sem(midiDevice->sem_lock);
		if (copied != B_OK)
			return B_BAD_ADDRESS;
		*num_bytes = count;
		return B_OK;
	}

	*num_bytes = 0;
	return B_ERROR;
}
// SPDX-SnippetEnd


static status_t
usb_midi_read(driver_cookie* cookie, off_t position,
	void* buf,	size_t* num_bytes)
{
	assert(cookie != NULL);
	status_t status = B_ERROR;
	usbmidi_port_info* port = cookie->port;
	usbmidi_device_info* midiDevice = cookie->device;

	if (midiDevice == NULL || !midiDevice->active)
		return B_ERROR;	/* already unplugged */

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	if (midiDevice->ump_endpoint)
		return read_ump_as_midi1(cookie, buf, num_bytes);
// SPDX-SnippetEnd

	DPRINTF_DEBUG((MY_ID "usb_midi_read: (%" B_PRIuSIZE " byte buffer at %"
		B_PRIdOFF " cookie %p)\n", *num_bytes, position, cookie));

	while (midiDevice && midiDevice->active) {
		ZDPRINTF_DEBUG((MY_ID "waiting on acquire_sem_etc\n"));

		status = acquire_sem_etc(cookie->sem_cb, 1, B_RELATIVE_TIMEOUT | B_CAN_INTERRUPT, 1000000);

		if (status == B_TIMED_OUT) {
			ZDPRINTF_DEBUG((MY_ID "acquire_sem_etc timed out\n"));
			continue;	/* see if we're still active */
		}
		if (status == B_INTERRUPTED) {
			// reader got interrupted while device idle
			*num_bytes = 0;
			return status;
		}
		if (status != B_OK) {
			DPRINTF_DEBUG((MY_ID "acquire_sem_etc aborted\n"));
			break;
		}

		// how more byte(s) can we actually read?
		int32 bytesCount = 0;
		if (get_sem_count(cookie->sem_cb, &bytesCount) == B_BAD_SEM_ID)
			break;

		bytesCount = MIN(bytesCount + 1, (int32)*num_bytes);
		if (bytesCount > 1 // the first byte was "acquired" already
			&& acquire_sem_etc(cookie->sem_cb, bytesCount - 1, B_RELATIVE_TIMEOUT, 0) != B_OK) {
			// midi_server is the only reader, get_sem_count() reported at least bytesCount more
			// are available / were release_sem()'d in usb_midi_read_callback().
			// At this stage, failure can only be due to B_BAD_SEM_ID: device was removed between
			// the initial acquire_sem() and this one.
			// Can't be B_INTERRUPTED (no B_CAN_INTERRUPT flag), nor B_TIMED_OUT
			// So, let's giving up.
			break;
		}

		acquire_sem(midiDevice->sem_lock);
		ring_buffer_user_read(port->rbuf, (uint8*)buf, bytesCount);
		release_sem(midiDevice->sem_lock);

		*num_bytes = bytesCount;

		DPRINTF_DEBUG((MY_ID "%p: read %u byte(s)\n", cookie, bytesCount));
		return B_OK;
	}

	DPRINTF_INFO((MY_ID "usb_midi_read: loop terminated"
		" -- Device no longer active\n"));
	*num_bytes = 0;
	return B_CANCELED;
}


const uint8 CINcode[] = {	/* see USB-MIDI Spec */
	0x4,	/* 0x0 - sysex start */
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	/* 0xF1 is a 2-byte system common message (CIN 0x2); it was 0 and was
	 * never sent. 0xF6 is one byte (CIN 0x5); it was sent as two. */
	0x2,	/* 0x1 -- MTC quarter frame */
	0x3,	/* 0x2  -- song pos */
	0x2,	/* 0x3 -- song select */
	0,	/* 0x4 -- undefined */
	0,	/* 0x5 -- undefined */
	0x5,	/* 0x6  -- tune request */
// SPDX-SnippetEnd
	0x5,	/* 0x7 -- sysex end */
	0x5,	/* 0x8 -- clock */
	0,	/* 0x9 -- undefined */
	0x5,	/* 0xA -- start */
	0x5,	/* 0xB -- continue */
	0x5,	/* 0xC -- stop */
	0,	/* 0xD -- undefined */
	0x5,	/* 0xE -- active sensing */
	0x5,	/* 0x0 -- system reset */
};


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
static void
store_le32(uint8* out, uint32 word)
{
	out[0] = (uint8)word;
	out[1] = (uint8)(word >> 8);
	out[2] = (uint8)(word >> 16);
	out[3] = (uint8)(word >> 24);
}


static status_t
queue_out(usbmidi_device_info* midiDevice, const void* data, size_t size)
{
	if (midiDevice->ept_out == NULL || size == 0 || size > midiDevice->outMaxPkt)
		return B_BAD_VALUE;

	status_t status = acquire_sem_etc(midiDevice->sem_send, 1,
		B_RELATIVE_TIMEOUT, 2000000LL);
	if (status != B_OK)
		return status;

	memcpy(midiDevice->out_buffer, data, size);
	if (midiDevice->out_is_interrupt) {
		status = gUSBModule->queue_interrupt(midiDevice->ept_out->handle,
			midiDevice->out_buffer, size,
			(usb_callback_func)usb_midi_write_callback, midiDevice);
	} else {
		status = gUSBModule->queue_bulk(midiDevice->ept_out->handle,
			midiDevice->out_buffer, size,
			(usb_callback_func)usb_midi_write_callback, midiDevice);
	}
	if (status != B_OK) {
		DPRINTF_ERR((MY_ID "midi write queue_bulk() error 0x%" B_PRIx32 "\n",
			status));
		release_sem(midiDevice->sem_send);
		return B_ERROR;
	}
	return B_OK;
}


static status_t
send_cin_packet(usbmidi_port_info* port, uint8 cin, const uint8 midi[3])
{
	usb_midi_event_packet packet;
	memset(&packet, 0, sizeof(packet));
	packet.cin = cin;
	packet.cn = port->cable;
	packet.midi[0] = midi[0];
	packet.midi[1] = midi[1];
	packet.midi[2] = midi[2];
	return queue_out(port->device, &packet, sizeof(packet));
}


static void
encode_sysex7(uint8 status, const uint8* data, int count, uint32 words[2])
{
	words[0] = (0x3u << 28) | ((uint32)status << 20) | ((uint32)count << 16);
	words[1] = 0;
	for (int i = 0; i < count; i++) {
		uint32 byte = data[i] & 0x7f;
		if (i == 0)
			words[0] |= byte << 8;
		else if (i == 1)
			words[0] |= byte;
		else
			words[1] |= byte << (8 * (5 - i));
	}
}


static status_t
send_sysex_cin(usbmidi_port_info* port, const uint8* extra, size_t extraLength,
	bool finish)
{
	uint8 bytes[16];
	size_t total = port->sysex_hold_len + extraLength;
	if (total > sizeof(bytes))
		return B_BAD_VALUE;
	memcpy(bytes, port->sysex_hold, port->sysex_hold_len);
	if (extraLength > 0)
		memcpy(bytes + port->sysex_hold_len, extra, extraLength);

	size_t send = finish ? total : total - (total % 3);
	size_t offset = 0;
	while (offset < send) {
		size_t left = send - offset;
		uint8 count = left > 3 ? 3 : (uint8)left;
		uint8 cin = 4;
		if (finish && offset + count == send)
			cin = 4 + count;
		uint8 midi[3];
		memset(midi, 0, sizeof(midi));
		memcpy(midi, bytes + offset, count);
		status_t status = send_cin_packet(port, cin, midi);
		if (status != B_OK)
			return status;
		offset += count;
	}

	port->sysex_hold_len = (uint8)(total - send);
	if (port->sysex_hold_len > 0)
		memcpy(port->sysex_hold, bytes + send, port->sysex_hold_len);
	return B_OK;
}


static status_t
send_ump_as_midi1(usbmidi_port_info* port, const uint32* words, uint32 wordCount)
{
	uint32 messageType = words[0] >> 28;
	uint8 midi[3];
	uint8 cin;

	if (messageType == 0x2 && wordCount == 1) {
		midi[0] = (words[0] >> 16) & 0xff;
		midi[1] = (words[0] >> 8) & 0xff;
		midi[2] = words[0] & 0xff;
		cin = (midi[0] & 0xf0) == 0xf0 ? CINcode[midi[0] & 0x0f] : (midi[0] >> 4);
	} else if (messageType == 0x4 && wordCount == 2) {
		uint8 translated[12];
		int length = midi2_channel_voice_to_midi1(words, wordCount, translated);
		if (length <= 0)
			return B_UNSUPPORTED;
		int offset = 0;
		while (offset < length) {
			uint8 kind = translated[offset] & 0xf0;
			int messageLength = (kind == 0xc0 || kind == 0xd0) ? 2 : 3;
			uint8 event[3];
			memset(event, 0, sizeof(event));
			memcpy(event, translated + offset, messageLength);
			status_t sent = send_cin_packet(port, kind >> 4, event);
			if (sent != B_OK)
				return sent;
			offset += messageLength;
		}
		return B_OK;
	} else if (messageType == 0x1 && wordCount == 1) {
		midi[0] = (words[0] >> 16) & 0xff;
		midi[1] = (words[0] >> 8) & 0xff;
		midi[2] = words[0] & 0xff;
		cin = CINcode[midi[0] & 0x0f];
	} else if (messageType == 0x3 && wordCount == 2) {
		uint8 status = 0;
		uint8 data[6];
		int count = sysex7_bytes(words, &status, data);
		if (count < 0)
			return B_UNSUPPORTED;
		uint8 midi[8];
		size_t length = 0;
		if (status == 0 || status == 1)
			midi[length++] = 0xf0;
		for (int i = 0; i < count; i++)
			midi[length++] = data[i] & 0x7f;
		bool finish = status == 0 || status == 3;
		if (finish)
			midi[length++] = 0xf7;
		if (status == 0)
			port->sysex_hold_len = 0;
		return send_sysex_cin(port, midi, length, finish);
	} else {
		port->sysex_hold_len = 0;
		return B_UNSUPPORTED;
	}

	if (cin == 0 || CINbytes[cin] == 0)
		return B_UNSUPPORTED;
	return send_cin_packet(port, cin, midi);
}


static status_t
send_ump_words(usbmidi_port_info* port, uint32* words, uint32 wordCount)
{
	if (port == NULL || port->device == NULL || words == NULL
		|| wordCount == 0 || wordCount > 4)
		return B_BAD_VALUE;
	if (ump_word_count(words[0]) != wordCount)
		return B_BAD_VALUE;

	words[0] = (words[0] & ~((uint32)0x0f << 24))
		| ((uint32)(port->cable & 0x0f) << 24);

	if (!port->device->ump_endpoint)
		return send_ump_as_midi1(port, words, wordCount);

	uint8 raw[16];
	for (uint32 i = 0; i < wordCount; i++)
		store_le32(raw + i * 4, words[i]);
	return queue_out(port->device, raw, wordCount * 4);
}


static status_t
write_sysex_as_ump(driver_cookie* cookie, const uint8* midiseq, size_t* num_bytes)
{
	if (*num_bytes < 1)
		return B_BAD_DATA;

	uint8 first = 0;
	if (user_memcpy(&first, midiseq, 1) != B_OK)
		return B_BAD_ADDRESS;
	if (first != 0xf0)
		return B_BAD_DATA;

	size_t payload = *num_bytes - 1;
	if (payload > 0) {
		uint8 last = 0;
		if (user_memcpy(&last, midiseq + *num_bytes - 1, 1) != B_OK)
			return B_BAD_ADDRESS;
		if (last == 0xf7)
			payload--;
	}

	if (payload == 0) {
		uint32 words[2];
		encode_sysex7(0, NULL, 0, words);
		return send_ump_words(cookie->port, words, 2);
	}

	const uint8* source = midiseq + 1;
	size_t offset = 0;
	while (offset < payload) {
		size_t count = payload - offset;
		if (count > 6)
			count = 6;
		uint8 data[6];
		memset(data, 0, sizeof(data));
		if (user_memcpy(data, source + offset, count) != B_OK)
			return B_BAD_ADDRESS;

		bool firstPacket = offset == 0;
		bool lastPacket = offset + count == payload;
		uint8 status = 0;
		if (!firstPacket && lastPacket)
			status = 3;
		else if (!firstPacket)
			status = 2;
		else if (!lastPacket)
			status = 1;

		uint32 words[2];
		encode_sysex7(status, data, (int)count, words);
		status_t statusCode = send_ump_words(cookie->port, words, 2);
		if (statusCode != B_OK)
			return statusCode;
		offset += count;
	}
	return B_OK;
}


static status_t
write_midi1_as_ump(driver_cookie* cookie, const uint8* midiseq,
	size_t* num_bytes, uint8 midicode, uint8 cin)
{
	/* A MIDI 2.0 USB endpoint does not accept CIN packets. Channel voice
	 * and system messages become one UMP word. SysEx becomes type 0x3.
	 */
	if (midicode == 0xf0)
		return write_sysex_as_ump(cookie, midiseq, num_bytes);
	if (midicode == 0xf7)
		return B_BAD_DATA;
	if (cin >= 16 || CINbytes[cin] == 0)
		return B_BAD_DATA;
	if ((int)*num_bytes < CINbytes[cin])
		return B_BAD_DATA;

	*num_bytes = CINbytes[cin];
	uint8 midi[3];
	memset(midi, 0, sizeof(midi));
	if (user_memcpy(midi, midiseq, *num_bytes) != B_OK)
		return B_BAD_ADDRESS;

	/* MIDI 2.0 protocol translates every channel voice (M2-104-UM D.3).
	 * MIDI 1.0 protocol, and a protocol that has not been set, stays
	 * type 0x2, including a note-on with velocity 0. */
	if ((midicode & 0xf0) >= 0x80 && (midicode & 0xf0) <= 0xe0) {
		uint32 words[2];
		int count = usb_midi_translate_channel(cookie->device->ump_protocol,
			midi[0], midi[1], midi[2], &cookie->device->translate, words);
		if (count < 0)
			return B_OK;
		if (count == 2)
			return send_ump_words(cookie->port, words, 2);
	}

	uint32 messageType = (midicode & 0xf0) == 0xf0 ? 0x1u : 0x2u;
	uint32 words[1];
	words[0] = (messageType << 28)
		| ((uint32)midi[0] << 16)
		| ((uint32)midi[1] << 8)
		| (uint32)midi[2];
	return send_ump_words(cookie->port, words, 1);
}
// SPDX-SnippetEnd


static status_t
usb_midi_write(driver_cookie* cookie, off_t position,
	const void* buf, size_t* num_bytes)
{
	uint8* midiseq = (uint8*)buf;
	uint8 midicode;	/* preserved for reference */
	if (user_memcpy(&midicode, midiseq, sizeof(midicode)) != B_OK)
		return B_BAD_ADDRESS;
	status_t status;
	size_t buff_lim;
	uint8 cin = ((midicode & 0xF0) == 0xF0) ? CINcode[midicode & 0x0F]
		 : (midicode >> 4);

	assert(cookie != NULL);
	usbmidi_port_info* port = cookie->port;
	usbmidi_device_info* midiDevice = cookie->device;

	if (!midiDevice || !midiDevice->active)
		return B_ERROR;		/* already unplugged */

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	if (midiDevice->ump_endpoint)
		return write_midi1_as_ump(cookie, midiseq, num_bytes, midicode, cin);
// SPDX-SnippetEnd

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	/* max MIDI bytes per transfer: whole 4-byte packets of 3 MIDI bytes */
	buff_lim = (midiDevice->outMaxPkt / sizeof(usb_midi_event_packet)) * 3;
	if (buff_lim < 3)
		return B_ERROR;
// SPDX-SnippetEnd

	DPRINTF_DEBUG((MY_ID "MIDI write (%" B_PRIuSIZE " bytes at %" B_PRIdOFF
		")\n", *num_bytes, position));
	// Make sure we always write exactly one MIDI event at a time.
	// SysEx can be of arbitrary sizes, for all others, we check that the
	// complete event was passed in and do not use more than that.
	// TODO add a loop to allow writing multiple events in a single write()
	// call if desired.
	if (midicode != 0xF0) {
		if ((int)*num_bytes < CINbytes[cin]) {
			DPRINTF_ERR((MY_ID "Expected %d bytes for MIDI command %" B_PRIu8
				" but got only %" B_PRIuSIZE ".\n", CINbytes[cin], cin,
				*num_bytes));
			return B_BAD_DATA;
		}
		*num_bytes = CINbytes[cin];
	}

	size_t bytes_left = *num_bytes;
	while (bytes_left) {
		size_t xfer_bytes = (bytes_left < buff_lim) ?  bytes_left : buff_lim;
		usb_midi_event_packet* pkt = midiDevice->out_buffer;
		int packet_count = 0;

		status = acquire_sem_etc(midiDevice->sem_send,
			1, B_RELATIVE_TIMEOUT, 2000000LL);
		if (status != B_OK)
			return status;

		while (xfer_bytes) {
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
			/* A SysEx packet takes its CIN and length from the bytes left in
			 * the message, so a message of one or two bytes no longer
			 * subtracts three and runs past out_buffer, and the last packet
			 * of a three-byte message is an end packet (CIN 7). */
			uint8 pkt_bytes = CINbytes[cin];
			if (midicode == 0xF0)
				usb_midi_sysex_packet(bytes_left, &cin, &pkt_bytes);
			if (pkt_bytes > xfer_bytes)
				pkt_bytes = (uint8)xfer_bytes;
			if (pkt_bytes == 0)
				break;
// SPDX-SnippetEnd
			memset(pkt, 0, sizeof(usb_midi_event_packet));
			pkt->cin = cin;
			pkt->cn = port->cable;

			DPRINTF_DEBUG((MY_ID "using packet data (code %x -- %d bytes)\n",
				pkt->cin, pkt_bytes));

			if (user_memcpy(pkt->midi, midiseq, pkt_bytes) != B_OK) {
				release_sem(midiDevice->sem_send);
				return B_BAD_ADDRESS;
			}

			DPRINTF_DEBUG((MY_ID "built packet %p %x:%d %x %x %x\n",
				pkt, pkt->cin, pkt->cn,
				pkt->midi[0], pkt->midi[1], pkt->midi[2]));

			xfer_bytes -= pkt_bytes;
			bytes_left -= pkt_bytes;
			midiseq += pkt_bytes;
			packet_count++;
			pkt++;
		}
		if (packet_count == 0) {
			release_sem(midiDevice->sem_send);
			break;
		}

		status = gUSBModule->queue_bulk(midiDevice->ept_out->handle, midiDevice->out_buffer,
			sizeof(usb_midi_event_packet) * packet_count,
			(usb_callback_func)usb_midi_write_callback,	midiDevice);

		if (status != B_OK) {
			DPRINTF_ERR((MY_ID "midi write queue_bulk() error 0x%" B_PRIx32
				"\n", status));
			release_sem(midiDevice->sem_send);
			return B_ERROR;
		}
	}
	return B_OK;
}


static status_t
usb_midi_control(driver_cookie* cookie, uint32 op, void* data, size_t len)
{
	assert(cookie != NULL);

	usbmidi_port_info* port = cookie->port;
	usbmidi_device_info* midiDevice = cookie->device;
	if (midiDevice == NULL || !midiDevice->active || port == NULL)
		return B_ERROR; // already disconnected

	switch (op) {
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
		case B_MIDI_WRITE_UMP:
		{
			midi_ump_packet packet;
			if (len < sizeof(packet))
				return B_BAD_VALUE;
			if (user_memcpy(&packet, data, sizeof(packet)) < B_OK)
				return B_BAD_ADDRESS;
			return send_ump_words(port, packet.words, packet.word_count);
		}

		case B_MIDI_READ_UMP:
		{
			if (!midiDevice->ump_endpoint)
				return B_UNSUPPORTED;
			if (data == NULL || len < sizeof(midi_ump_packet))
				return B_BAD_VALUE;

			status_t status = acquire_sem_etc(cookie->sem_cb, 1,
				B_RELATIVE_TIMEOUT | B_CAN_INTERRUPT, 1000000);
			if (status != B_OK)
				return status;

			acquire_sem(midiDevice->sem_lock);
			midi_ump_packet packet;
			bool havePacket = pop_ump_packet(port, &packet);
			release_sem(midiDevice->sem_lock);
			if (!havePacket)
				return B_ERROR;
			if (user_memcpy(data, &packet, sizeof(packet)) != B_OK)
				return B_BAD_ADDRESS;
			return B_OK;
		}
// SPDX-SnippetEnd

		case B_MIDI_GET_PORT_INFO:
		{
			midi_device_port_info info;
			if (len < sizeof(info))
				return B_ERROR;

			info.port_index = port->cable;

			if (midiDevice->port_count > 1) {
				// multiple ports => needs a distinct port name
				snprintf(info.port_name, sizeof(info.port_name), "%c", 'A' + port->cable);
			} else {
				// single port device, no need for a distinct port name
				info.port_name[0] = '\0';
			}

			return user_memcpy(data, &info, sizeof(info)) >= 0 ? B_OK : B_BAD_ADDRESS;
		}

		case B_GET_ICON_NAME:
		{
			const char* iconName = "devices/midi-usb";
			return user_strlcpy((char*)data, iconName, len) >= 0 ? B_OK : B_BAD_ADDRESS;
		}

		case B_GET_DEVICE_NAME:
		{
			size_t nameLength = sizeof(midiDevice->manufacturer_name)
				+ sizeof(midiDevice->product_name) + 1;

			// shorten the device name: vendors loves
			// to put their full company/division name, like "Xxxx Limited Inc.",
			// "Xxxx Professional blablabla" etc.
			// Let's stop after the vendor name first word, Xxxx
			char* p = midiDevice->manufacturer_name;
			for (size_t i = 0; i < sizeof(midiDevice->manufacturer_name); i++, p++) {
				if (*p == ' ')
					break;
			}

			char name[nameLength];
			snprintf(name, nameLength, "%.*s %s", (int)(p - midiDevice->manufacturer_name),
				midiDevice->manufacturer_name, midiDevice->product_name);

			return user_strlcpy((char*)data, name, len) >= 0
				? B_OK : B_BAD_ADDRESS;
		}

		case B_GET_DEVICE_SERIAL_NUMBER:
		{
			return user_strlcpy((char*)data, midiDevice->serial_number, len) > 0
				? B_OK : B_BAD_ADDRESS;
		}
	}

	return B_DEV_INVALID_IOCTL;
}


static status_t
usb_midi_close(driver_cookie* cookie)
{
	assert(cookie != NULL);
	delete_sem(cookie->sem_cb);

	usbmidi_port_info* port = cookie->port;
	usbmidi_device_info* midiDevice = cookie->device;
	
	DPRINTF_INFO((MY_ID "usb_midi_close(%p device=%p port=%p)\n",
		cookie, midiDevice, port));

	acquire_sem(usbmidi_port_list_lock);
	if (port != NULL) {
		/* detach the cookie from port */
		port->open_fd = NULL;
		--port->open;
	}
	release_sem(usbmidi_port_list_lock);
	DPRINTF_DEBUG((MY_ID "usb_midi_close: complete\n"));

	return B_OK;
}


static status_t
usb_midi_free(driver_cookie* cookie)
{
	assert(cookie != NULL);
	usbmidi_device_info* midiDevice = cookie->device;
	DPRINTF_INFO((MY_ID "usb_midi_free(%p device=%p)\n", cookie, midiDevice));

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	/* The last reference to a port or device that was unplugged while open
	 * frees it here; no hook of this cookie can run any more. */
	usbmidi_port_info* port = cookie->port;
	bool freePort = false;
	bool freeDevice = false;
	acquire_sem(usbmidi_port_list_lock);
	if (midiDevice != NULL) {
		midiDevice->open_count--;
		freeDevice = midiDevice->removed && midiDevice->open_count == 0;
	}
	if (port != NULL)
		freePort = port->removed;
	release_sem(usbmidi_port_list_lock);

	if (freePort) {
		if (midiDevice != NULL && port->cable >= 0 && port->cable < 16
			&& midiDevice->ports[port->cable] == port) {
			midiDevice->ports[port->cable] = NULL;
		}
		remove_port(port);
	}
	if (freeDevice)
		remove_device(midiDevice);
// SPDX-SnippetEnd

	free(cookie);

	return B_OK;
}


static device_hooks usb_midi_hooks = {
	(device_open_hook)usb_midi_open,
	(device_close_hook)usb_midi_close,
	(device_free_hook)usb_midi_free,
	(device_control_hook)usb_midi_control,
	(device_read_hook)usb_midi_read,
	(device_write_hook)usb_midi_write,
	NULL, NULL, NULL, NULL
};


// #pragma mark - driver registration


_EXPORT status_t
init_hardware(void)
{
	DPRINTF_DEBUG((MY_ID "init_hardware() version:"
		__DATE__ " " __TIME__ "\n"));
	return B_OK;
}


_EXPORT status_t
init_driver(void)
{
	DPRINTF_INFO((MY_ID "init_driver() version:" __DATE__ " " __TIME__ "\n"));

	if (get_module(B_USB_MODULE_NAME, (module_info**)&gUSBModule) != B_OK)
		return B_ERROR;

	if ((usbmidi_port_list_lock = create_sem(1, "dev_list_lock")) < 0) {
		put_module(B_USB_MODULE_NAME);
		return usbmidi_port_list_lock;		/* error code */
	}

	gUSBModule->register_driver(usb_midi_driver_name, my_supported_devices, SUPPORTED_DEVICES,
		NULL);
	gUSBModule->install_notify(usb_midi_driver_name, &my_notify_hooks);
	DPRINTF_INFO((MY_ID "init_driver() OK\n"));

	return B_OK;
}


_EXPORT void
uninit_driver(void)
{
	DPRINTF_INFO((MY_ID "uninit_driver()\n"));
	gUSBModule->uninstall_notify(usb_midi_driver_name);

	delete_sem(usbmidi_port_list_lock);
	put_module(B_USB_MODULE_NAME);
	free_port_names();
	DPRINTF_INFO((MY_ID "uninit complete\n"));
}


// #pragma mark - devices paths

_EXPORT const char**
publish_devices(void)
{
	DPRINTF_INFO((MY_ID "publish_devices()\n"));

	if (usbmidi_port_list_changed) {
		free_port_names();
		alloc_port_names();
		if (usbmidi_port_names != NULL)
			rebuild_port_names();
		usbmidi_port_list_changed = false;
	}
	assert(usbmidi_port_names != NULL);
	return (const char**)usbmidi_port_names;
}


_EXPORT device_hooks*
find_device(const char* name)
{
	assert(name != NULL);
	DPRINTF_INFO((MY_ID "find_device(%s)\n", name));
	if (search_port_info(name) == NULL)
		return NULL;
	return &usb_midi_hooks;
}

