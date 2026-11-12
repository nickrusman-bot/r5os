# Review of USB audio and MIDI 2.0 (12 Nov 2026)

For Cursor. Read-only review of `origin/main` at `9880d5346d`: the `usb_audio` driver (`distro/media/src/add-ons/kernel/drivers/audio/usb/`), the `usb_midi` driver (`.../drivers/midi/usb_midi/`), the MIDI 2.0 kit (`distro/media/src/kits/midi2/`, `headers/os/midi2/`) and `midi_server` (`distro/media/src/servers/midi/PortDrivers.cpp`). Checked against the Haiku tree in WSL (`upstream/master`, `libroot/posix/unistd/ioctl.c`, `bus_managers/usb/Device.cpp`) and the USB-MIDI 2.0 and UMP specifications.

Nothing was built or run for this review. No USB audio or MIDI device was attached. Every point below is from reading the code.

**Verdict: two points break things that worked before (1, 4). MIDI 2.0 over USB cannot switch on with a real device yet (2, 3).** Fix 1 first, in its own PR: it stops MIDI input from every MIDI device, also USB MIDI 1.0 keyboards. Then 2+3, then 4+5, then 6–10. The rest can wait for a later round.

Line numbers are from `9880d5346d`.

## 1. `midi_server` stops reading every MIDI device (blocking)

`PortDrivers.cpp:128-144`. `MidiPortProducer::GetData()` compares the return value of `ioctl()` with `B_TIMED_OUT`, `B_INTERRUPTED`, `B_UNSUPPORTED` and `B_DEV_INVALID_IOCTL`. On Haiku, `ioctl()` in libroot ends with `RETURN_AND_SET_ERRNO(status)`: an error returns `-1` and puts the code in `errno`. None of the four cases ever matches, so every error goes to the `else` branch, which sets `fKeepRunning = false` and leaves the loop.

What that does:
- A USB MIDI 1.0 device: `usb_midi_control` returns `B_UNSUPPORTED` for `B_MIDI_READ_UMP` (`usb_midi.cpp:1539`). The reader thread ends on its first call. No input at all.
- Any other MIDI driver (not `usb_midi`) returns `B_DEV_INVALID_IOCTL`: the same.
- A UMP device: after one second without input, `B_TIMED_OUT` (`usb_midi.cpp:1544`). The reader ends.

`CHANGELOG.md` (around line 2685) says "A driver that does not implement the ioctl is still read as MIDI 1.0 bytes." That is not what the code does. `midi2test` passed because it does not go through a port driver.

Fix: `if (ioctl(...) == 0) { ... } else switch (errno) { ... }`. Then test with a USB MIDI 1.0 keyboard (or at least a driver without the ioctl) and record it.

Same file, `:44-54`: `MidiPortConsumer::Data()` sends a UMP payload with `B_MIDI_WRITE_UMP` and returns, also when the driver does not know that ioctl. A MIDI 2.0 program that plays to a non-USB port is silent. Fall back to the MIDI 1.0 bytes (`midi2_channel_voice_to_midi1`) and `write()` when the ioctl fails.

## 2. The MIDI 2.0 alternate is never selected (blocking for MIDI 2.0)

`usb_midi.cpp:621-694`. A USB MIDI 2.0 device has alternate 0 = USB MIDI 1.0 and alternate 1 = USB MIDI 2.0 (USB-MIDI 2.0 § 3.1). The loop takes the first alternate that matches, which is alternate 0, whose header says `bcdMSC 0x0100`. So `ump_endpoint` stays `false`. The driver also never calls `set_alt_interface()` (there is no call in the file), so alternate 1 and its endpoints are never active even when it would be found.

The second check, `descriptor_type == 0x26` (Group Terminal Block), cannot fire either: Group Terminal Block descriptors are not part of the configuration descriptor. The host reads them with a separate class `GET_DESCRIPTOR` request (wValue `0x2601`, USB-MIDI 2.0 § 5.4). They are never in `intf->generic`.

Result: on a real MIDI 2.0 device, the UMP code in this driver does not run. The device works as MIDI 1.0, which is safe, but nothing of MIDI 2.0 over USB is reachable.

Fix: when an interface has an alternate whose MS header is `>= 0x0200`, select it with `set_alt_interface()`, take the endpoints from that alternate, and read the Group Terminal Blocks with the request (the kit already parses them: `midi2_usb_parse_group_terminal`). Keep alternate 0 when anything fails. `docs/MIDI2.md` already says a physical MIDI 2.0 interface is not tested; keep that until one is.

## 3. The driver listens for the wrong Stream Configuration message

`usb_midi.cpp:347`. `deliver_ump_packet` takes the protocol from a UMP stream message with status `0x005`. That is the Stream Configuration **Request**, which the host sends. The device answers with the Stream Configuration **Notification**, `0x006` (`Midi2Stream.h:23-24`). So `ump_protocol` never changes, and notes are always sent as MIDI 1.0 in UMP (type 0x2), also to a device in MIDI 2.0 protocol.

Fix: use `B_MIDI2_STREAM_CONFIG_NOTIFY`, and include `Midi2Stream.h` instead of the magic number. The host should also send the Request itself (the kit has `midi2_stream_configure`), or the device stays in its default protocol.

## 4. A USB audio stream that stops itself hangs the media add-on

`Stream.cpp:888-898` stops a stream after 32 transfer errors in a row. The comment says "The next buffer exchange starts it again". But `Device::_MultiBufferExchange` (`Device.cpp:865`) waits on `fBuffersReadySem` with no timeout (`acquire_sem_etc(..., B_CAN_INTERRUPT, 0)` waits forever). When the stream stops while the add-on is already waiting, nothing releases the semaphore and there is no next exchange. A playback-only device then hangs the `multi_audio` thread until the device is unplugged.

Fix: wait with `B_RELATIVE_TIMEOUT` (a few buffer periods), or release `fBuffersReadySem` when the callback stops the stream, and let the exchange return an error so the add-on calls again.

## 5. `Stream::Start()` does not reset the buffer cycle

`Stream.cpp:614-636`. `Start()` queues buffer 0 and then 1, but it does not reset `fCurrentBuffer`, `fProcessedBuffers`, `fCarryFrames` or `fPlayCarryFrames`. Those keep the values from before the stop. Since R5, a restart happens more often: after point 4's error stop, on a latency change (`SetGlobalFormat` → `_SetupBuffers`), and on `_MultiBufferForceStop`.

For recording, the callback joins buffer `(fCurrentBuffer + 1) % 2` (`:866-868`). When `fCurrentBuffer` is 0 after the restart, it joins buffer 1, which is still in flight, while buffer 0 just completed. The reader gets the wrong buffer and stale frames from the old carry. For playback, the same offset re-queues the buffer that is still in flight.

Fix: in `Start()`, set `fCurrentBuffer` to its starting value (`(size_t)-1` for input, as `_ChooseAlternate` does), `fProcessedBuffers = 0`, and clear both carries.

## 6. `B_MULTI_SET_ENABLED_CHANNELS` writes past the caller's bitmap

`Device.cpp:285-286` copies `sizeof(enable_bits)` (the whole kernel bitmap, `kMaxEnabledChannels / 8` bytes) back to the caller. A few lines up, the R5 comment says the caller's bitmap is only sized for this card's channels, and `B_MULTI_GET_ENABLED_CHANNELS` (`:239-240`) was fixed to copy `(channels + 7) / 8` bytes. This copy was missed: it overwrites user memory after a small bitmap in the media add-on. Use the same `(channels + 7) / 8`.

## 7. `_MultiGetBuffers` always returns `B_OK`

`Device.cpp:845`. The function collects `status` from every `Stream::GetBuffers()`, then returns `B_OK`. The R5 checks in `GetBuffers` (`Stream.cpp:1081-1084`, too few buffers; `:1138-1141`, bad user address) stop the loop, but the ioctl still reports success with half-filled buffer lists. Return `status`.

## 8. Two streams in one direction overwrite each other, and leak kernel stack

`Stream.cpp:1117-1141` (Haiku code, but it matters more now that R5 arms every stream and allows 64 channels). `descs` has `fNumChannels` entries. The loop fills entries `startChannel .. fNumChannels-1`, where `startChannel` is the number of channels of the earlier streams. Then the whole `descs` array is copied to the **start** of the caller's array.

With a second playback stream (`startChannel` = 2, 2 channels): the loop does not run, and two uninitialised `buffer_desc` from the kernel stack are copied over the first stream's entries. Kernel stack contents reach user space, and the first stream's buffers are lost. The count is also not checked against `request_playback_channels` / `request_record_channels`.

Fix: fill `descs[0 .. fNumChannels-1]`, copy them to `Buffers[buffer] + startChannel`, and refuse when `startChannel + fNumChannels` is larger than the caller's channel count.

## 9. `B_MULTI_GET_DESCRIPTION` copies uninitialised kernel memory

`Device.cpp:176-199`. `channels` is `malloc`ed for `request_channel_count` entries. `_MultiGetDescription` fills only `min(Channels.Count(), request_channel_count)` of them (`:672-674`). Then all `request_channel_count` entries are copied to the caller, so the rest is kernel heap contents. Use `calloc`, or copy only the filled count.

## 10. A rate change on a shared clock reports success but does nothing

`Stream.cpp:1058-1066`. When another stream of the device runs, `SetGlobalFormat` sets up buffers for the new rate and returns `B_OK` without programming the clock. The device keeps the old rate, so the stream plays or records at the wrong speed with no error. Return an error (`B_BUSY`) and keep the old rate on the alternate, or stop the other streams and program the clock once (as `_ArmStreams` does).

## 11. Playback feedback

`Stream.cpp:797-827`, `:756-759`.
- The feedback value is not checked. Linux accepts it only within about ±12.5 % of nominal and detects a full-speed device that sends 16.16 instead of 10.14. A wrong format here is pinned at the ±4 frame cap and does nothing, which is harmless, but it hides the problem. Add the range check and log once when it fails.
- `_QueueFeedback()` ignores the result of `queue_isochronous`. After one failure, feedback stops and the last sample is used for the rest of the stream.
- When `adjust` is +1 and the last packet is already at `nominalMaxFrames`, the clamp at `:758-759` drops the extra frame, while `fFeedbackError` was already lowered by one frame (`:730-731`) and the frame was taken from the carry. That frame is lost (a click). Put the extra frame in a packet that has room, or do not adjust in that buffer.

## 12. MIDI clock and transport are lost for MIDI 1.0 consumers

`MidiLocalConsumer.cpp` `dispatch_ump()` (around `:255-290`) handles UMP types 0x2, 0x3 and 0x4. Type 0x1 (system real-time and common: clock `0xF8`, start, stop, continue, song position, MTC) is dropped. Once point 1 is fixed and a UMP device is read, a program that uses `SystemRealTime()` or `SystemCommon()` gets nothing. Add type 0x1, with the lengths `usb_midi_expand_ump` already uses.

## 13. Two wrong entries in `CINcode` (Haiku table, now also used for UMP)

`usb_midi.cpp:1082-1099`.
- `0xF1` (MTC quarter frame) is `0`. It is a 2-byte system common message, CIN `0x2`. With `0`, `usb_midi_write()` sets `*num_bytes = 0` and returns `B_OK` without sending; a UMP type 0x1 MTC returns `B_UNSUPPORTED`.
- `0xF6` (tune request) is `0x2`. It is one byte, CIN `0x5`. The driver sends one extra byte.

Both are also wrong in Haiku. R5 now uses the table in `send_ump_as_midi1` (`:1238`) as well.

## 14. One unknown UMP drops the rest of the transfer

`usb_midi_parse.h:46-63` and `:136-138`. `usb_midi_ump_word_count` returns 0 for message types 0x6–0xC and 0xE, and `usb_midi_walk_ump` then stops. The UMP specification fixes the size of every message type, also the reserved ones (0x6, 0x7: 1 word; 0x8–0xA: 2; 0xB, 0xC: 3; 0xE: 4), so a receiver can skip what it does not know. Return those sizes for the walk, and drop only that packet. For writing, refusing them is fine.

## 15. MIDI-CI identity: manufacturer 0x7D and a fixed MUID

`Midi2Library.cpp:337-340`, `:664`, `:931-944`.
- Manufacturer ID `0x7D` ("Educational Use") is the SysEx ID for non-commercial and educational use only. A system that is published should not answer with it. Either get an ID from the MIDI Association (R5 already registered with Khronos), or document clearly that it is a placeholder until then.
- `catalog->muid = 1`. MIDI-CI requires a random 28-bit MUID per session, so two devices or two programs can tell each other apart. A fixed value collides the moment there are two R5 programs or two R5 machines. Generate it at init (and never `0x0FFFFF00`–`0x0FFFFFFF`).
- The endpoint name is `"R5OS"` (`:942`). Other systems show it to users. Per `AGENTS.md` that is `"R5"` (the product ID may stay `r5os`).

## 16. `midi2_pe_header_number` can overflow

`Midi2Library.cpp:321-325`. The header comes from the device. A long string of digits overflows `int32` (undefined behaviour in C++). Stop at, for example, 9 digits or at a limit, and return -1.

## Smaller points and oddities

- **Unplug while reading (Haiku, extended).** `usb_midi_removed` frees the device (`remove_device`, `:838`) while a reader may still be in `read_ump_as_midi1` or `usb_midi_read`, which hold a local `midiDevice` pointer and check `midiDevice->active` after the semaphore wakes them (`:953`, `:1029`). That is a use-after-free. It is in Haiku's read path too; the new UMP read path copies it. Free the device in `usb_midi_free` when the last cookie goes.
- **`B_MIDI_READ_UMP` is not symmetric.** `B_MIDI_WRITE_UMP` works on a MIDI 1.0 device (it translates), but `B_MIDI_READ_UMP` returns `B_UNSUPPORTED` there. A MIDI 2.0 program can send to a MIDI 1.0 keyboard but not receive from it as UMP. Either translate on read too (CIN → type 0x2), or document the difference in `docs/MIDI2.md`.
- **In-band marker.** A UMP is recognised in `Data()` by the 4 bytes `'UMP1'`. MIDI 1.0 data that starts with `55 4D 50 31` (running status data bytes) is taken for a UMP. Rare, but worth one line in `docs/MIDI2.md`.
- **Bus speed is guessed.** `usb_audio_high_speed()` (`usb_audio_packets.h:58-66`) assumes every UAC 2.0 device is high speed and a UAC 1.0 device is high speed only when 1 ms does not fit in a packet. A UAC 2.0 device on full speed (some small USB-C adapters) gets 8 times too few frames per packet; a UAC 1.0 device on high speed that fits 1 ms gets 8 times too many. The bus manager knows the speed (`Device::fSpeed`). An additive call in the USB module would remove the guess.
- **Recording latency grows.** `usb_audio_compact_record` (`usb_audio_packets.h:213-220`) keeps up to a whole buffer in the carry before it drops frames. A device clock that runs fast adds up to one buffer (43 ms at 2048 frames, 48 kHz) of latency before the first drop. Keep the carry to a few packets.
- **`padded` is unused.** `usb_audio_compact_record` takes `padded` and ignores it, and `fPadded` is never counted. Remove the parameter, or count something.
- **Product name.** `Device::_ReadProductString` (`Device.cpp:602-607`) takes the low byte of each UTF-16 character. A name with é or ü becomes invalid UTF-8 in Media. Convert to UTF-8.
- **Log level.** `ProgramClock` logs every successful rate change with `TRACE(ERR, ...)` (`Stream.cpp:595`, `:607`). That goes against the work in `df06704e6b` that stopped logging USB events that are not faults.
- **One rate list for all alternates.** `_QueryClockRates` (`Stream.cpp:289-299`) writes the rates of this stream's clock into every alternate of the interface. Fine while they share one clock, which is normal, but it is not checked.
- **`new` without a check.** `Device::_SetupEndpoints` (`Device.cpp:1018-1020`, Haiku code) calls `stream->Init()` on the result of `new(std::nothrow)` without checking for `NULL`.
- **Licensing marks.** Most of the R5 UMP code in `usb_midi.cpp` (`push_ump_packet`, `deliver_ump_packet`, `read_ump_as_midi1`, `send_ump_as_midi1`, `send_ump_words`, `write_sysex_as_ump`, the `B_MIDI_*_UMP` cases, the new fields in `usb_midi.h`) is not inside SPDX snippet tags. The file has 3 snippets. `CHANGELOG.md` (around line 1762) says the R5 parts are marked. Add the snippet tags; do not change the MIT header. `usb_midi_parse.h` says `Copyright 2026, R5OS`, other new files say `R5OS Contributors`; use one form.

## What is good

- The user-pointer handling in the `usb_audio` ioctls is much better than in Haiku: counts are capped, every pointer is checked with `IS_USER_ADDRESS`, and the mix arrays are no longer on the kernel stack.
- `usb_audio_packets.h` is clear, shared with a host test, and gets 44.1 kHz exact.
- The callback no longer sleeps in the USB finisher thread.
- The MIDI 1.0 SysEx fix (`usb_midi_sysex_packet`) has a host test, and the UMP walk and translation are shared with the fuzz harness.
- The translation in `Midi2Translate.h` follows Appendix D of the UMP specification (velocity 0 → 1, RPN/NRPN as four controllers, bank select before program change).

## Order

1. Point 1 alone, with a test on a USB MIDI 1.0 device (or QEMU with a USB MIDI device if that is possible) and a `CHANGELOG.md` correction.
2. Points 2 and 3 together. Not testable without a MIDI 2.0 device; say so.
3. Points 4 and 5 together, test with a USB sound card: unplug during playback, change the latency during playback.
4. Points 6–10, one commit each.
5. The rest when there is time.

Per `AGENTS.md`: build, boot, and record in `CHANGELOG.md` what was tested where. Do not claim a MIDI 2.0 device test that did not happen.

PR titles: "Area - what the user gets", in plain English, without code words. Example: "MIDI - Read keyboards again after the MIDI 2.0 change".
