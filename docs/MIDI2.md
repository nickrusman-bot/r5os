# MIDI 2.0 on R5OS

`libmidi2.so` can send and receive a Universal MIDI Packet (UMP). The MIDI 1.0 calls stay. There is no second MIDI kit. Property Exchange, MIDI-CI, and Profiles are not implemented.

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
| 0x3 data, 0x4 MIDI 2.0 channel voice | 2 | Type 0x4 note-on, note-off, and control change are mapped to the 7-bit hooks. |
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

`BMidiLocalConsumer::Data` sees the bytes after the roster header. When the magic matches, it does not treat those bytes as a MIDI 1.0 status byte.

A type 0x4 note calls `NoteOn` or `NoteOff` with the top 7 bits of the 16-bit velocity (`value >> 9`). A type 0x4 control change calls `ControlChange` with the top 7 bits of the 32-bit value (`value >> 25`). Velocity 0 on a MIDI 2.0 note-on stays `NoteOn`. An application that only implements `NoteOn` still hears the note, at MIDI 1.0 resolution.

A type 0x2 packet is turned back into MIDI 1.0 bytes and passed through `Data` again, so the existing `NoteOn` / `ControlChange` / `ProgramChange` hooks run.

An application that wants the full value overrides `Data`, calls `midi2_ump_from_payload`, and returns without the base when it handled the packet. Calling the base as well runs the 7-bit hooks.

```cpp
void
MyConsumer::Data(uchar* data, size_t length, bool atomic, bigtime_t time)
{
	uint32 words[4];
	size_t count = 0;
	if (midi2_ump_from_payload(data, length, words, 4, &count) == B_OK) {
		if ((words[0] >> 28) == 0x4 && ((words[0] >> 20) & 0xf) == 0x9) {
			uint16 velocity = (uint16)(words[1] >> 16);
			(void)velocity;
		}
		return;
	}
	BMidiLocalConsumer::Data(data, length, atomic, time);
}
```

The bytes a hardware port reads are MIDI 1.0. `midi_server` does not put the 16-bit value on the read side. A program that only consumes a USB MIDI producer sees the 7-bit hooks.

## Drivers

`midi_server` sends a recognised UMP with `ioctl(fd, B_MIDI_WRITE_UMP, &packet)`. The argument is `midi_ump_packet` from `<midi_driver.h>`: `word_count` and `words[4]`, native `uint32`. A buffer that is not a UMP still goes out with `write()`, unchanged.

`usb_midi` looks at the class-specific MIDI header on the streaming interface. A version below 2.0 keeps USB MIDI 1.0 CIN packets. On that endpoint a type 0x4 note or control change becomes one CIN packet with the same top 7 bits. Other UMP types return `B_UNSUPPORTED` there.

Version 2.0 or newer writes the UMP words, little-endian, to the bulk OUT endpoint. The group nibble is the cable of that port. A MIDI 1.0 `write()` on that endpoint becomes one type 0x1 or type 0x2 word. SysEx on a version 2.0 endpoint returns `B_UNSUPPORTED`. Bytes read from the device are parsed back into MIDI 1.0 for the existing reader. The 16-bit value is not kept on the way in.

No USB MIDI device was attached when this was added. A physical MIDI 2.0 interface is not a tested path.

## midi2test

`midi2test` prints `ok` or `FAIL` for each check and exits 0 only when every check passed. The first checks are the helpers: word counts, `midi2_encode_note_on`, `midi2_encode_control_change`, and `midi2_ump_from_payload`. They do not need `midi_server`.

The rest registers a local producer and a local consumer, connects them, and sprays:

- a `uchar` note and a `uchar` control change, which stay three MIDI 1.0 bytes
- a `uint16` note-on of `0x8000`, which arrives as type 0x4 and as `NoteOn` velocity `0x40`
- a type 0x4 note-off of velocity `0xFFFF`, which arrives as `NoteOff` velocity `0x7F`
- a `uint32` control change of `0x80000000`, which arrives as type 0x4 and as `ControlChange` value `0x40`
- a type 0x2 channel-voice word, which arrives both as that word and as MIDI 1.0 bytes

It does not open a USB device, and it does not check `B_MIDI_WRITE_UMP`. Run it from Terminal on a booted system:

```
midi2test
```

If `midi_server` never publishes the endpoints, those later checks fail and the helper checks still print `ok`.
