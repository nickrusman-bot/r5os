# MIDI 2.0 on R5

`libmidi2.so` can send and receive a Universal MIDI Packet (UMP). The MIDI 1.0 calls stay. There is no second MIDI kit. Property Exchange, Profiles, per-note controllers, and Endpoint and Function Block discovery are in this same kit.

The check program is `midi2test` in `/system/bin`. This file is the contract for that program and for an application that links `libmidi2.so`.

## What an application calls

Include `<MidiProducer.h>` and `<MidiConsumer.h>`. Link `be` and `libmidi2.so`.

Property Exchange, profiles, stream messages, and the USB descriptor parser are defined in `libmidi2.so`. The headers keep those names and arguments. `midi2_pe_put14`, `midi2_pe_get14`, `midi2_stream_pack`, `midi2_stream_status`, and `midi2_profile_same` stay inline. An application compiled after this change calls the library.

A `uchar` velocity or controller value is still a MIDI 1.0 channel message: two or three bytes, status in the first byte. `SprayNoteOn2` takes a `uint16` velocity and `SprayControlChange2` takes a `uint32` controller value. Each sends one MIDI 2.0 channel-voice packet (message type 0x4, two 32-bit words). `SprayNoteOn(0, 60, 100)` is the MIDI 1.0 call. A bare integer is not ambiguous.

The earlier overloads, `SprayNoteOn(uchar, uchar, uint16)` and `SprayControlChange(uchar, uchar, uint32)`, were exported from `libmidi2.so` in development builds from `f0c74f2520`. They were never in a release: `distro/VERSION` is `2026.1-dev`. Nothing else in this tree calls them. `midi2test` and `PortDrivers.cpp` use the MIDI 1.0 calls, and `midi2test` uses the new names for the wide values. The old symbols are removed. They are not aliases.

```cpp
producer->SprayNoteOn(0, 60, 100);

uint16 velocity = 0x8000;
producer->SprayNoteOn2(0, 60, velocity);

uint32 level = 0x80000000;
producer->SprayControlChange2(0, B_MAIN_VOLUME, level);

uint32 words[2];
midi2_encode_note_on(0, 60, velocity, words);
status_t status = producer->SprayUMP(words, 2);
```

`SprayUMP` accepts one packet. `wordCount` is 1, 2, or 4 and must equal `midi2_ump_word_count(ump[0])`. A null pointer, a count of 0 or more than 4, or a count that disagrees with the message type returns `B_BAD_VALUE`. A packet that passes those checks returns `B_OK`. Nothing is sent when the producer has no consumer. That return does not mean a device received the packet.

`SprayNoteOff` has no 16-bit overload. A MIDI 2.0 note-off is `SprayUMP` with status nibble 0x8 in a type 0x4 packet. `midi2_encode_note_on` always writes status 0x9.

These spray methods are not virtual. A subclass compiled against the older header keeps its vtable. Overriding `SprayNoteOn` was not part of the old kit and is not part of this one.

## Packet layout inside the kit

`BMidiLocalProducer` still writes a 20-byte roster header, then the event. For a UMP the event is:

| Offset | Contents |
|---|---|
| 0 | `B_MIDI2_UMP_MAGIC` (`0x31504d55`, the bytes of `UMP1` on a little-endian CPU) |
| 4 | word count, `uint32` |
| 8 | that many native `uint32` words |

`midi2_ump_from_payload` checks the magic, the count, the buffer length, and that the count matches the message type in the first word. It copies the words into the caller's array.

Message type, in the top 4 bits of word 0:

| Type | Words | Used here |
|---|---|---|
| 0x0 utility, 0x1 system, 0x2 MIDI 1.0 channel voice | 1 | A type 0x2 packet is unpacked to the old byte hooks. Type 0x1 is accepted by `SprayUMP`. |
| 0x3 data, 0x4 MIDI 2.0 channel voice | 2 | Type 0x4 note-on, note-off, and control change keep the full value in `Data` and call the 7-bit hooks. Type 0x3 is SysEx7 and calls `SystemExclusive` when the message is complete. |
| 0x5 data, 0xD Flex Data, 0xF reserved | 4 | The packet is delivered. No extra hook reads it. |
| anything else | 0 | `SprayUMP` and `midi2_ump_from_payload` reject it. |

A type 0x4 note-on from `midi2_encode_note_on`:

- bits 31..28: `0x4`
- bits 23..20: `0x9` (note-on). Note-off is `0x8`. Control change is `0xB`
- bits 19..16: channel 0..15
- bits 15..8: note or controller index
- word 1, bits 31..16: 16-bit velocity. A 32-bit controller value occupies all of word 1

The group nibble (bits 27..24) is left 0 by the helpers. `usb_midi` replaces it with the cable number of the open port before the bytes leave the machine.

## Reading a packet

`BMidiLocalConsumer::Data` sees the bytes after the roster header. When the magic matches, it does not treat those bytes as a MIDI 1.0 status byte. The full packet stays in that buffer. `midi2_note_velocity` reads the 16-bit velocity. `midi2_control_value` reads the 32-bit controller. `midi2_sysex_payload` reads one SysEx7 packet.

The base `Data` still calls the MIDI 1.0 hooks for the messages in Appendix D, Translation (D.2). A type 0x4 note-off calls `NoteOff` with the top 7 bits of the 16-bit velocity (`value >> 9`). A type 0x4 note-on calls `NoteOn` with those bits, and with at least 1, including when the 16-bit velocity is 0. That floor is Appendix D, MIDI 2.0 Note On Velocity (D.2.1). Poly pressure calls `KeyPressure` and control change calls `ControlChange`, each with the top 7 bits of the 32-bit value (`value >> 25`). Channel pressure calls `ChannelPressure` the same way (D.2.2). Pitch bend calls `PitchBend` with the top 14 bits, least significant byte first (D.2.5). A program change with the bank bit clear calls `ProgramChange` only. With the bank bit set it calls control change 0, control change 32, then `ProgramChange` (D.2.4). A registered controller calls control changes 101, 100, 6 and 38. An assignable controller calls 99, 98, 6 and 38 (D.2.3). An application that only implements `NoteOn` still hears the note, at MIDI 1.0 resolution.

A type 0x2 packet is turned back into MIDI 1.0 bytes and passed through `Data` again, so the existing `NoteOn` / `ControlChange` / `ProgramChange` hooks run.

A type 0x3 packet is SysEx7. Status 0 is the whole message. Status 1 starts it, 2 continues it, and 3 ends it. The bytes are assembled per consumer and per group (bits 27..24 of word 0). Two groups sending to the same consumer do not share one buffer. The base `Data` calls `SystemExclusive` once, with the bytes between `F0` and `F7`, when the message is complete. `SpraySystemExclusive` is still the MIDI 1.0 call: the consumer sees `F0`, the payload, and `F7`.

An application that wants the full value overrides `Data`, calls `midi2_ump_from_payload`, and returns without the base when it handled the packet. Calling the base as well runs the 7-bit hooks and `SystemExclusive`.

```cpp
void
MyConsumer::Data(uchar* data, size_t length, bool atomic, bigtime_t time)
{
	uint32 words[4];
	size_t count = 0;
	if (midi2_ump_from_payload(data, length, words, 4, &count) == B_OK) {
		if (midi2_is_note(words, count)) {
			uint16 velocity = midi2_note_velocity(words, count);
			(void)velocity;
		}
		return;
	}
	BMidiLocalConsumer::Data(data, length, atomic, time);
}
```

`midi_server` reads `B_MIDI_READ_UMP` from a MIDI 2.0 port and sprays that packet, so `Data` on a connected consumer has the same full value. The 7-bit hooks still run. A port that does not implement the ioctl is read as MIDI 1.0 bytes, as before. `read()` expands one packet from that queue into MIDI 1.0 bytes. `midi_server` uses the ioctl for a MIDI 2.0 port and does not also `read()` it, so each packet is sprayed once.

## Property Exchange

Property Exchange is MIDI-CI System Exclusive. The payload is the bytes between `F0` and `F7`. Those bytes travel as SysEx7: `midi2_encode_sysex` turns them into type 0x3 packets, and `SystemExclusive` hands the same bytes back when the message is complete. `usb_midi` already carries that SysEx. There is no separate Property Exchange transport.

A data message (get `0x34`, reply `0x35`, set `0x36`, reply `0x37`, subscription `0x38`, reply `0x39`, notify `0x3F`) is:

| Field | Size |
|---|---|
| `7E`, device id `7F`, sub-id `0x0D`, Property Exchange sub-id, MIDI-CI version `1` | 5 |
| source MUID, destination MUID, 28-bit, least significant 7-bit byte first | 8 |
| request id, 0..127 | 1 |
| header length, then the JSON header | 2 + n |
| chunk count, this chunk (first chunk is 1), body length, body | 6 + n |

Each 14-bit length is two bytes, 7 bits each, least significant byte first. The header is only in chunk 1. Later chunks set the header length to 0. A capabilities inquiry (`0x30`) and its reply (`0x31`) stop after the MUIDs and one byte, the number of simultaneous requests.

```cpp
midi2_pe_message request;
memset(&request, 0, sizeof(request));
request.subId = B_MIDI2_PE_GET;
request.sourceMuid = 2;
request.destinationMuid = 1;
request.requestId = 3;
request.header = "{\"resource\":\"DeviceInfo\"}";
request.headerLength = strlen(request.header);
request.chunkCount = 1;
request.chunkNumber = 1;

uint8 payload[128];
size_t length = 0;
midi2_pe_encode(&request, payload, sizeof(payload), &length);
```

`midi2_pe_decode` reads one payload. `midi2_pe_split` breaks a body that does not fit in `maxPayload` (the other device's maximum SysEx size, counting these bytes and not `F0` or `F7`). Pass 0 to keep a single chunk. `midi2_pe_assembly_add` joins chunks that share a request id. A gap, or chunk 0, drops the assembly. The header in the assembly is a C string. The body is not.

Header JSON has no whitespace. A request starts with `resource`. A reply starts with `status`. A subscription starts with `command`. `midi2_pe_header_string` and `midi2_pe_header_number` read one of those fields. Reply status values used here are 200, 400, 404, 405, and 413. A notify uses 100 (wait), 144 (terminate), or 408 (timeout) and has no reply.

`midi2_pe_catalog` is one local property set. `midi2_pe_catalog_init` fills:

| Resource | Body |
|---|---|
| `ResourceList` | `DeviceInfo`, `ChannelList`, `ProgramList`, `CtrlMapList` |
| `DeviceInfo` | educational manufacturer id 125,0,0, family `R5OS`, model `midi2` |
| `ChannelList` | channel 1, program Piano, bank 0,0,0 |
| `ProgramList` | Piano and Strings. `resId` is `programs` when a request sends one |
| `CtrlMapList` | values Off and Full. `resId` is `volume` when a request sends one |

`ControllerMap` is the same body as `CtrlMapList`. The specification name is `CtrlMapList`. `ChannelList` and `CtrlMapList` accept `{"command":"start"}`. The reply is `{"status":200,"subscribeId":"sub1"}`. This catalog keeps one subscription. `{"command":"end"}` clears it. `midi2_pe_subscription_full` then writes `{"command":"full","subscribeId":"sub1"}` and points at the current body, which is the notification. A `full`, `partial`, or `notify` command received from the other device is acknowledged with status 200.

`midi2_pe_respond` answers one complete inquiry. The caller passes the header and body buffers. The reply message points into those buffers. Set replaces the matching catalog string when the new text fits and every byte is 7-bit. `setPartial` is refused with status 400.

## Per-note controllers

A per-note controller is one MIDI 2.0 channel-voice packet, message type 0x4. Status 0 is registered. Status 1 is assignable. The note is 7 bits. The controller index is the low byte of word 0. The value is all of word 1. The group nibble stays 0, as with the other helpers.

```cpp
uint32 words[2];
midi2_encode_per_note(1, 60, 7, 0x80000000, false, words);
producer->SprayUMP(words, 2);
```

`midi2_is_per_note`, `midi2_per_note_note`, `midi2_per_note_index`, `midi2_per_note_value`, and `midi2_per_note_registered` read it back from `Data`. `NoteOn` and `ControlChange` are not called. A MIDI 1.0 application does not see the message as a channel controller.

Appendix D, Messages That Cannot Be Translated to MIDI 1.0 (D.2.8), also leaves these as UMP in `Data` only: relative registered controllers (status `0x4`), relative assignable controllers (status `0x5`), per-note management (status `0xF`), and per-note pitch bend (status `0x6`). `read()` and a MIDI 1.0 device do not receive them.

## Profiles

Profile messages are MIDI-CI System Exclusive, sub-id `0x0D`, on the same bytes-between-`F0`-and-`F7` path as Property Exchange.

| Sub-id | Message |
|---|---|
| `0x20` / `0x21` | Profile Inquiry and the reply |
| `0x22` / `0x23` | Set Profile On and Set Profile Off |
| `0x24` / `0x25` | Profile Enabled and Profile Disabled reports |
| `0x2F` | Profile Specific Data |

A profile id is 5 bytes. `midi2_profile_catalog_init` holds one standard id, `7E 00 00 00 01`, and it starts disabled. An inquiry reply lists it under the disabled ids. Set Profile On answers with a Profile Enabled report and the catalog marks it enabled. Set Profile Off answers with Profile Disabled. Profile Specific Data is stored, up to 32 bytes, and the reply carries those bytes. An id that is not in the catalog returns `B_NAME_NOT_FOUND`.

## Endpoint and Function Block discovery

These are UMP Stream messages, message type `0xF`, four words. The format is 0 (one packet). The status is the next 10 bits.

`midi2_stream_endpoint_discovery` writes status `0x000`, UMP version 1.1, and a filter in the low byte of word 1. The bits are endpoint info `0x01`, device identity `0x02`, endpoint name `0x04`, product instance id `0x08`, and stream configuration `0x10`.

`midi2_stream_block_discovery` writes status `0x010`. The block number is the next byte (`0xFF` asks for every block). The filter is info `0x01` and name `0x02`.

`midi2_endpoint_init` is one local endpoint named `R5OS`, protocol MIDI 2.0, with one bidirectional function block named `midi2` on group 0. `midi2_stream_reply` turns a discovery packet into the notifications that filter asked for. A block number this endpoint does not have returns `B_NAME_NOT_FOUND`. Names here fit in one packet.

## USB MIDI 2.0 descriptors

`midi2_usb_write_interface` and `midi2_usb_parse_interface` are the standard interface descriptor: type `0x04`, 9 bytes, audio class `0x01`, MIDI streaming subclass `0x03`. `bAlternateSetting` is the caller's value. Alternate setting 0 is the MIDI 1.0 interface. A later setting is the MIDI 2.0 interface when its class-specific header is version `0x0200`.

`midi2_usb_write_header` and `midi2_usb_parse_header` are that class-specific header: type `0x24`, subtype `0x01`, version `0x0200`, then the total length. `midi2_usb_write_group_terminal` and `midi2_usb_parse_group_terminal` are one Group Terminal Block: type `0x26`, subtype `0x01`, 13 bytes. Direction 0 is bidirectional, 1 is input, 2 is output. Protocol `0x11` is MIDI 2.0. `usb_midi` treats that block, and a header version of `0x0200` or newer, as a MIDI 2.0 endpoint. No device was attached for this.

## Drivers

`midi_server` sends a recognised UMP with `ioctl(fd, B_MIDI_WRITE_UMP, &packet)`. The argument is `midi_ump_packet` from `<midi_driver.h>`: `word_count` and `words[4]`, native `uint32`. A buffer that is not a UMP still goes out with `write()`, unchanged.

`usb_midi` looks at the class-specific MIDI header on the streaming interface. A version below 2.0 keeps USB MIDI 1.0 CIN packets. On that endpoint a type 0x4 packet becomes the MIDI 1.0 messages in Appendix D, Translation (D.2), one CIN packet each. D.2.8 returns `B_UNSUPPORTED`.

Version 2.0 or newer writes the UMP words, little-endian, to the bulk OUT endpoint. The group nibble is the cable of that port. A MIDI 1.0 note-on or note-off `write()` on that endpoint becomes one type 0x4 packet, Appendix D, Note On/Off (D.3.1). Velocity 0 on a note-on becomes a note-off with velocity `0x8000`. Velocity 1 becomes `0x0200`. Other channel bytes stay one type 0x2 word, and system bytes stay type 0x1. A group then carries both type 0x4 and type 0x2. A SysEx `write()` (`F0` … `F7`) becomes one or more type 0x3 packets, six payload bytes each. `B_MIDI_READ_UMP` returns the packet that arrived, including type 0x3 and type 0x4. `read()` expands that same queue to MIDI 1.0 bytes. Appendix D, Translation (D.2), covers note-off, note-on (at least velocity 1), poly pressure, control change, channel pressure, pitch bend (least significant byte first), program change (bank bit, then control change 0, control change 32, and the program), and registered or assignable controllers (four control changes, D.2.3). Messages in D.2.8 are not expanded. Type 0x3 becomes `F0`, the payload, and `F7`. A type 0x4 packet sent toward a MIDI 1.0 device uses the same expansion, one CIN packet per MIDI 1.0 message.

On a version 1.0 endpoint, a type 0x3 packet is sent as USB MIDI CIN SysEx. A type 0x4 packet uses the same D.2 expansion as `read()`.

On the VirtualBox guest, `midi2test` printed `all checks passed` on 3 Nov 2026 (reported at 10:55 Europe/Amsterdam). That run includes per-note controllers, profiles, endpoint and function-block discovery, the USB descriptor helpers, Property Exchange, and the earlier MIDI 1.0 and UMP checks. No USB MIDI device was attached. A physical MIDI 2.0 interface is not a tested path. The program name is `midi2test`.

## midi2test

`midi2test` prints `ok` or `FAIL` for each check and exits 0 only when every check passed. The first checks are the helpers: word counts, `midi2_encode_note_on`, `midi2_encode_control_change`, and `midi2_ump_from_payload`. They do not need `midi_server`.

`scripts/fuzz-midi2.sh` builds `scripts/midi2-fuzz.cpp` with AddressSanitizer and UndefinedBehaviorSanitizer. It runs the kernel walk, expansion, and SysEx7 parser first (`usb_midi_parse.h`, the same file the driver includes), then the payload helper, Property Exchange, profiles, stream messages, and the USB descriptor parser. Each target runs for the number of seconds on the command line. The stub headers in `scripts/midi2-host` exist only for that host build.

The rest registers a local producer and a local consumer, connects them, and sprays:

- a `uchar` note and a `uchar` control change, which stay three MIDI 1.0 bytes
- a `uint16` note-on of `0x8000`, which arrives as type 0x4 and as `NoteOn` velocity `0x40`
- a type 0x4 note-off of velocity `0xFFFF`, which arrives as `NoteOff` velocity `0x7F`
- a `uint32` control change of `0x80000000`, which arrives as type 0x4 and as `ControlChange` value `0x40`
- a type 0x2 channel-voice word, which arrives both as that word and as MIDI 1.0 bytes
- the 16-bit velocity and the 32-bit controller, read back with `midi2_note_velocity` and `midi2_control_value` from the bytes in `Data`
- a MIDI 1.0 SysEx, which arrives as `F0` … `F7` and as `SystemExclusive` without those two bytes
- an eight-byte SysEx7 in two type 0x3 packets, which arrives as one `SystemExclusive` after the end packet

The helper checks also cover per-note controllers, profile inquiry, enable, and specific data, endpoint and function-block discovery, and the USB MIDI 2.0 interface alternate setting, header, and Group Terminal Block. They do not need `midi_server`.

The spray checks also send one per-note controller, one endpoint-discovery packet, and one Profile Enabled report. `NoteOn` and `ControlChange` do not run for the per-note controller. The profile report arrives as one `SystemExclusive`.

The spray checks also send one Get Property Data inquiry as SysEx7. `SystemExclusive` receives the same payload. It does not open a USB device, and it does not check `B_MIDI_WRITE_UMP`. Run it from Terminal on a booted system:

```
midi2test
```

If `midi_server` never publishes the endpoints, those later checks fail and the helper checks still print `ok`.
