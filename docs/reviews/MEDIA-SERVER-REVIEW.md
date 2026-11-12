# Review of media_server and the Media preferences (12 Nov 2026)

For Cursor. Read-only review of `origin/main` at `d988322049`: the R5 changes from 11 Nov (`bc7a9c5c32`..`50d770ad7b`, plus the kit part of `666642a126`) in

- `distro/media/src/servers/media/` (`DefaultManager`, `NotificationManager`, `media_server.cpp`)
- `distro/media/src/servers/media_addon/MediaAddonServer.cpp`
- `distro/media/src/kits/media/` (`MediaAddOn`, `MediaNode`, `MediaFormats`, `DefaultMediaTheme`)
- `distro/media/src/preferences/media/` (`MediaWindow`, `MediaViews`)

Each file was compared with its Haiku original in `haiku/src/`. The `usb_audio` and `usb_midi` drivers and the MIDI 2.0 kit are left out: another round is working on them (`USB-AUDIO-MIDI2-REVIEW.md`).

Nothing was built or run for this review. No sound device was plugged in or removed. Every point below is from reading the code. Line numbers are from `d988322049`.

**Verdict: the direction is right (keep the saved device, refresh on hot-plug, copy unaligned data). Two points can break audio after an unplug (1, 2), one is a thread race (3), and the saved-device logic forgets an explicit choice in two cases (4, 5).** Fix 3 first: it is a memory-safety bug. Then 1+2 together (same code path, one unplug test), then 4+5. The rest can wait.

## 1. After an unplug, the mixer keeps the dead time source

`DefaultManager.cpp:409-420`. When the output node is gone, the rescan clears `fPhysicalAudioOut`, `fMixerConnected` and both connection halves. It does not clear `fTimeSource`. `_FindTimeSource()` only runs when `fTimeSource == -1` (`:454`) or from the disconnect block in `_ConnectMixerToOutput()` (`:757-773`). That block is skipped now, because the code just set `fPhysicalAudioOutFromMixer.node` to null.

Result: the unplugged card was the time source (a `multi_audio` node usually is). The new card gets `GetTimeSource()` → the dead node (`:805-806`, return value not checked), so `SetTimeSourceFor()` gets an id that no longer exists, or an uninitialized `timesource` when the call fails. Expected: no sound, or sound that drifts, on the fallback card until the media services restart.

Fix: in the node-gone branch also set `fTimeSource = -1`. Check the return of `GetTimeSource()` before `SetTimeSourceFor()`.

## 2. After an unplug, the mixer output may still count as connected

Same branch, `:415-418`. The code clears its own copy of the connection, but the mixer node is never told. `Disconnect()` is not called and a deleted consumer does not disconnect its producer in the Media Kit. The mixer has one output. `_ConnectMixerToOutput()` then asks for a free output (`GetFreeOutputsFor(mixer, …)`, `:808-814`), may get none, and fails with "can't find free mixer output". `fMixerConnected` stays false and every later rescan fails the same way.

The new tolerance for a failed `Disconnect()` (`:762-767`) has the same gap: when the disconnect really failed, the output is still taken, so "connecting the new one anyway" cannot succeed.

Not proven: this depends on whether `AudioMixer` gets a `Disconnect()` or `Disconnected()` when the `multi_audio` node is deleted. Test on the 5625U: play sound on a USB card, unplug it, check that HDA plays without restarting media services. If it does not: keep the old `media_source`/`media_destination` and call `fRoster->Disconnect()` with them (the mixer side still exists), or ask the mixer to drop its output.

## 3. media_addon_server changes `fInfoMap` from two threads

`MediaAddonServer.cpp:429-461` (`ADD_ON_SERVER_RESCAN_ADDON_FLAVORS`). `_HandleMessage()` runs on the control thread (`_ControlThread`, `:492`). The new code there writes `found->second.addon`, calls `_InstantiatePhysicalInputsAndOutputs()` (which does `active_flavors.push_back()`), and `_PutAddonIfPossible()`.

The application looper writes the same map at the same time:
- `kMsgNodeInstantiated` (`:370-389`) does `active_flavors.push_back()` on the same vector. The comment there even says the looper owns `fInfoMap`.
- `_AddOnRemoved()` (`:845`) erases the entry and puts the add-on, from the node monitor.

Two `push_back()` calls at once can corrupt the vector. An erase during the rescan leaves `found` dangling. Haiku's original handler only read the map from this thread.

Fix: do the same as `kMsgNodeInstantiated`. The control thread runs `_ScanAddOnFlavors()`, then posts a message with the add-on id. The looper does the find, `_InstantiatePhysicalInputsAndOutputs()`, the put and `SERVER_RESCAN_DEFAULTS`.

## 4. Choosing the fallback device again is not saved

`DefaultManager.cpp:260-266`. `Set(AUDIO_OUTPUT)` returns early when the node and input are already the current ones. That return comes before `fUsingFallback[…] = false`.

Scenario: the saved USB card is absent, HDA is the fallback. The user opens Media and chooses HDA, because that is the one they want now. `Set()` returns at once, the flag stays true, `SaveState()` writes the USB card again (`:170-174`), and the next time the USB card appears the output switches back to it. The user's choice is lost without a message.

Fix: clear the flag before the early return.

## 5. A choice made in this session is not the device that is restored

`fMsgList` is only filled in `LoadState()` (`:131`). `Set()` changes the current node but not `fMsgList`. `_FindPhysical()` and `SaveState()` both use `_SavedDefault()`, which reads `fMsgList`.

Scenario: the saved device is A. The user chooses B in Media. B is unplugged. The rescan looks for A, not for B. When A is absent, the first real device is used and marked as a fallback, and `SaveState()` writes A. When B comes back, the savedOnly search looks for A, so B is never chosen again. The user chose B and gets A.

Fix: on an explicit `Set()`, replace the `fMsgList` entry for that type with the new node's info (the same message `SaveState()` builds). Then "saved" always means "last chosen".

## 6. The Media settings files are only read for the administrator

`MediaViews.cpp:343-374`. `driver_settings_file()` uses `find_directory(B_USER_SETTINGS_DIRECTORY)` of the person running Media. The HDA and `usb_audio` drivers read the file with `load_driver_settings()` in the kernel. In the kernel, `B_USER_SETTINGS_DIRECTORY` is always `/boot/home/config/settings` (`libroot/os/driver_settings.cpp:778-790`, `directories.h:58`).

For the administrator (`/boot/home`) these are the same. For a standard person (`/boot/home/users/<name>`) Media writes the latency and `channel_layout` lines into their own home and shows "Saved". The drivers never read it. `virtual_surround` is read by the mixer (`MixerCore.cpp:78`) through `find_directory()` in media_addon_server, so it depends on which user that process runs as.

Fix (choose one, and write it in `docs/MULTIUSER.md`): these are machine settings, so write them to `/boot/system/settings/kernel/drivers` like Virtual memory does, gray the controls until the lock is open, or say in the page that only the administrator can change them. Until then, at least do not show "Saved" for a standard person.

## 7. The Media app leaks a node reference every time it refreshes

`MediaViews.cpp:193-205`. `_GetNode()` returns a node from `GetNodeFor()` or `InstantiateDormantNode()`. Both take a reference. No caller releases it: `_ChangeDefaultInput/Output()` (four places) and `_FillChannelMenu()` (`:982`).

`_FillChannelMenu()` already leaked in Haiku. What is new: it now runs much more often, because every `B_MEDIA_FLAVORS_CHANGED` and every `ML_UPDATE_DEFAULTS` calls `_InitMedia(false)` → `SetDefaultOutput()` → `_FillChannelMenu()`. Each hot-plug event and each failed choice adds a reference. The node then cannot be deleted while Media is open, which also keeps point 2 from cleaning up.

Fix: `roster->ReleaseNode(node)` after use in each caller (an `ObjectDeleter`-style guard keeps it short).

## 8. `active_flavors` only grows

`MediaAddonServer.cpp:370-389`. Every node an application instantiates is added to `active_flavors`. Nothing removes it when the application releases the node. So:
- `_PutAddonIfPossible()` never unloads that add-on again (`active_flavors` is never empty).
- The list grows with every instantiation. A program that opens and closes a node repeatedly grows it without limit.
- On quit, `_DestroyInstantiatedFlavors()` walks all of it with a `snooze(50000)` per entry (`:744`), also for nodes that are long gone. 100 stale entries = 5 seconds extra shutdown.

Fix: drop entries whose node no longer exists (`GetNodeFor()` fails) before adding, and before the quit loop. Better: remove the entry on `B_MEDIA_NODE_DELETED` (watch it once in the looper).

## 9. `GetInstancesFor(flavor id)` can skip a new card

`MediaAddonServer.cpp:775-779`. The skip uses `flavorinfo->internal_id`. The R5 comment in `DefaultManager.cpp:535-539` says it well: a flavor id is the device's index, and one absent USB device renumbers the rest. Example: cards A (0) and B (1) are live. A is unplugged; B becomes flavor 0. C is plugged in as flavor 1. `GetInstancesFor(addon, 1)` finds B's old node (still registered as flavor 1) and C is not instantiated.

Not proven: depends on whether the `multi_audio` node of A is deleted before the rescan, and how its flavor is registered. Test with two USB cards. A fix is to compare the device path in the flavor info instead of the index.

## 10. Media rebuilds the whole window on every flavor change

`MediaWindow.cpp:373-383`. `B_MEDIA_FLAVORS_CHANGED` and `ML_UPDATE_DEFAULTS` run `_InitMedia(false)`: all node lists, the default menus, the channel menu and the parameter view of the selected item. One USB card with input and output sends several flavor notices in a row. Each one rebuilds the window, and a parameter view the user is changing is replaced.

Also: the notice arrives before media_server's rescan has switched the default (add-on server → `SERVER_RESCAN_DEFAULTS` → rescan thread). The menu can show the old default, and nothing refreshes it once the rescan finishes.

Fix: coalesce (start a 300 ms `BMessageRunner` on the first notice, ignore the rest until it fires). For `ML_UPDATE_DEFAULTS`, only re-mark the default menus. For the late default, watch `B_MEDIA_DEFAULT_CHANGED` or refresh once more after the timer.

## 11. A failed save leaves the new choice marked

`MediaViews.cpp:745-813`. The latency, layout and surround menus use `SetLabelFromMarked(true)`. When writing the file fails, an alert appears but the menu keeps the new item marked, so the page shows a value that was not saved. Latency writes `hda.settings` and then `usb_audio`; when the second write fails, the first stays changed. The default menus do this right with `ML_UPDATE_DEFAULTS` (`:221`).

Fix: re-mark the item that matches the file (`read_*()`) after a failure.

## 12. Settings files are truncated and then written

`MediaViews.cpp:464`, `:572`, `:625`. `open(O_TRUNC)` and then `write()`. A crash or full disk in between leaves an empty `hda.settings` or `usb_audio`, which also holds lines Media does not own. Write a temporary file in the same directory and `rename()` it. A file larger than 32 KB has a second problem: `read()` takes at most 32767 bytes (`:388`, `:433`, `:498`, `:543`) and the write then drops the rest.

## Smaller points

- **Duplicate code.** `read_latency_frames()`/`read_channel_layout()` and `write_latency_file()`/`write_channel_layout()` are the same function four times (read, split lines, filter keys, write). One `read_setting(file, key)` and one `write_setting(file, keys, line)` remove about 150 lines and make point 12 a single fix. `is_latency_key()` is also used for `channel_layout`; call it `is_settings_key()`.
- **`write_virtual_surround()` replaces the whole file** and `read_virtual_surround()` only reads the first line. Fine while that file has one key, but it behaves differently from the other two. With the shared helper above it is the same.
- **Reading creates directories.** `driver_settings_file()` does `mkdir()` also when it is only called to read. Opening Media as a person without write rights fails silently, but should not create anything.
- **Stack use.** Each write function has a 32 KB buffer plus `BString` copies, and three read functions run while the view is built. Fine on the window thread. Reading into the `BString` directly (`LockBuffer`) saves the copy.
- **`AllAttached()` sets the targets again** (`MediaViews.cpp:710-716`), which `AttachedToWindow()` already did (`:702-704`). One of them is enough.
- **"Restart media services to apply."** is shown from the start (`:669`), also when nothing was changed. Show it after a change.
- **"USB layout"** is shown without a USB audio device. Gray it when there is none, or say what it applies to.
- **Two identical USB cards** (same name, same add-on) match by name in `_FindPhysical()` (`:592-600`). The first one wins, which may be the other card. Acceptable, but log it.
- **Video defaults never return to the saved device.** `fUsingFallback` is set for video too, and `SaveState()` keeps the saved one, but the rescan only switches back for audio (`:428-447`). Either add video or do not mark video as a fallback.
- **`NotificationManager::RegisterNotifications/CancelNotifications`** (`:93`, `:133`) still `memcpy` from `nodeData` when `FindData()` fails. `ASSERT` is gone in a release build, so that is an uninitialized pointer. `SendNotifications()` checks it; do the same here and return.
- **Reads from shared fields.** `fUsingFallback[]` is written by `Set()` (server looper) and the rescan thread without `fRescanLock`. Haiku's own fields have the same pattern (`volatile`), so it is not new, but the new array is not even `volatile`. Take `fRescanLock` in `Set()` or keep the flags behind the same lock as the rescan.
- **"None Out" / "None In".** No node in the tree has those names, so the fallback branches for them in `_FindPhysical()` are dead code. Not new; leave them, but the new `savedOnly` checks there are not tested by anything.
- **License markers.** None of the eleven files has an SPDX-Snippet tag. `THIRD_PARTY_NOTICES.md` says the ported RenkuOS behavior keeps MIT, which is fine, but the R5-own parts (latency, layout and surround in `MediaViews.cpp`, the fallback flag in `DefaultManager`) need the GPL-3.0-or-later snippet tags per `AGENTS.md`. The same open point is in `USB-AUDIO-MIDI2-REVIEW.md`.
- **Good:** the aligned port buffers (`media_server.cpp:911`, `MediaNode.cpp:362`, `MediaAddonServer.cpp:487`), `read_flat()`/`write_flat()` in `MediaAddOn.cpp`, the size checks in `MediaFormats.cpp` and `SendNotifications()`, `_SavedDefault()` replacing the loop that kept a pointer to the wrong type, and the `MediaWindow` destructor fix (`StartWatching` → `StopWatching`).

## Tests that would show points 1, 2, 4, 5, 9

One boot in VirtualBox or on the 5625U is enough for most of it:

1. Choose a USB card in Media, play sound, unplug it. Expect HDA to play within a few seconds without a restart (points 1, 2).
2. With the USB card absent, choose HDA in Media, reboot, plug the USB card in. Expect HDA to stay (point 4).
3. Choose card B, unplug B, plug B in again. Expect B (point 5).
4. With two USB cards: unplug the first, plug in a third. Expect the third in the Media list (point 9).

None of these were run for this review.
