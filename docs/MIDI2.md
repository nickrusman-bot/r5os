# MIDI 2.0 on R5OS

`libmidi2.so` can send and receive a Universal MIDI Packet (UMP). The MIDI 1.0 calls stay. There is no second MIDI kit. Property Exchange is the MIDI-CI request, reply, and subscription layer in this same kit. Profiles are not implemented.

The check program is `midi2test` in `/system/bin`. This file is the contract for that program and for an application that links `libmidi2.so`.

## What an application calls

Include `<MidiProducer.h>` and `<MidiConsumer.h>`. Link `be` and `libmidi2.so`.

A `uchar` velocity or controller value is still a MIDI 1.0 channel message: two or three bytes, status in the first byte. A `uint16` velocity or a `uint32` controller value is one MIDI 2.0 channel-voice packet (message type 0x4, two 32-bit words). The numeric range does not choose the path. The argument type does. `SprayNoteOn(0, 60, (uint16)100)` is a MIDI 2.0 packet whose velocity is 100, not a MIDI 1.0 note.

A bare integer is ambiguous between `uchar` and `uint16`, and between `uchar` and `uint32`. The call does not compile until it is cast, or stored in a variable of the type you mean.

```cpp
uchar classic = 100;
producer->SprayNoteOn(0, 60, classic);

uint16 velocity = 0x8000;
producer->SprayNoteOn(0, 60, velocity);

uint32 level = 0x80000000;
producer->SprayControlChange(0, B_MAIN_VOLUME, level);

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

The base `Data` still calls the MIDI 1.0 hooks. A type 0x4 note calls `NoteOn` or `NoteOff` with the top 7 bits of the 16-bit velocity (`value >> 9`). A type 0x4 control change calls `ControlChange` with the top 7 bits of the 32-bit value (`value >> 25`). Velocity 0 on a MIDI 2.0 note-on stays `NoteOn`. An application that only implements `NoteOn` still hears the note, at MIDI 1.0 resolution.

A type 0x2 packet is turned back into MIDI 1.0 bytes and passed through `Data` again, so the existing `NoteOn` / `ControlChange` / `ProgramChange` hooks run.

A type 0x3 packet is SysEx7. Status 0 is the whole message. Status 1 starts it, 2 continues it, and 3 ends it. The base `Data` calls `SystemExclusive` once, with the bytes between `F0` and `F7`, when the message is complete. `SpraySystemExclusive` is still the MIDI 1.0 call: the consumer sees `F0`, the payload, and `F7`.

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

## Drivers

`midi_server` sends a recognised UMP with `ioctl(fd, B_MIDI_WRITE_UMP, &packet)`. The argument is `midi_ump_packet` from `<midi_driver.h>`: `word_count` and `words[4]`, native `uint32`. A buffer that is not a UMP still goes out with `write()`, unchanged.

`usb_midi` looks at the class-specific MIDI header on the streaming interface. A version below 2.0 keeps USB MIDI 1.0 CIN packets. On that endpoint a type 0x4 note or control change becomes one CIN packet with the same top 7 bits. Other UMP types return `B_UNSUPPORTED` there.

Version 2.0 or newer writes the UMP words, little-endian, to the bulk OUT endpoint. The group nibble is the cable of that port. A MIDI 1.0 `write()` on that endpoint becomes one type 0x1 or type 0x2 word. A SysEx `write()` (`F0` … `F7`) becomes one or more type 0x3 packets, six payload bytes each. `B_MIDI_READ_UMP` returns the packet that arrived, including type 0x3 and type 0x4. `read()` expands that same queue to MIDI 1.0 bytes: a type 0x4 note keeps the top 7 bits, and type 0x3 becomes `F0`, the payload, and `F7`.

On a version 1.0 endpoint, a type 0x3 packet is sent as USB MIDI CIN SysEx. A type 0x4 note or control change is still one CIN packet with the top 7 bits.

On the VirtualBox guest, `midi2test` printed `all checks passed` on 3 Nov 2026 (reported at 10:26 Europe/Amsterdam), including Property Exchange. No USB MIDI device was attached. A physical MIDI 2.0 interface is not a tested path.

## midi2test

`midi2test` prints `ok` or `FAIL` for each check and exits 0 only when every check passed. The first checks are the helpers: word counts, `midi2_encode_note_on`, `midi2_encode_control_change`, and `midi2_ump_from_payload`. They do not need `midi_server`.

The rest registers a local producer and a local consumer, connects them, and sprays:

- a `uchar` note and a `uchar` control change, which stay three MIDI 1.0 bytes
- a `uint16` note-on of `0x8000`, which arrives as type 0x4 and as `NoteOn` velocity `0x40`
- a type 0x4 note-off of velocity `0xFFFF`, which arrives as `NoteOff` velocity `0x7F`
- a `uint32` control change of `0x80000000`, which arrives as type 0x4 and as `ControlChange` value `0x40`
- a type 0x2 channel-voice word, which arrives both as that word and as MIDI 1.0 bytes
- the 16-bit velocity and the 32-bit controller, read back with `midi2_note_velocity` and `midi2_control_value` from the bytes in `Data`
- a MIDI 1.0 SysEx, which arrives as `F0` … `F7` and as `SystemExclusive` without those two bytes
- an eight-byte SysEx7 in two type 0x3 packets, which arrives as one `SystemExclusive` after the end packet

The helper checks also cover `midi2_encode_sysex`, `midi2_sysex_payload`, Property Exchange encode and decode, a two-chunk reply, `DeviceInfo`, an unknown resource, subscribe, unsubscribe, and Notify status 144. They do not need `midi_server`.

The spray checks also send one Get Property Data inquiry as SysEx7. `SystemExclusive` receives the same payload. It does not open a USB device, and it does not check `B_MIDI_WRITE_UMP`. Run it from Terminal on a booted system:

```
midi2test
```

If `midi_server` never publishes the endpoints, those later checks fail and the helper checks still print `ok`.
