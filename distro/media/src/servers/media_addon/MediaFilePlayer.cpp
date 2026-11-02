/*
 * Copyright (c) 2004, Marcus Overhagen <marcus@overhagen.de>. All rights reserved.
 * Copyright (c) 2007, Jérôme Duval. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "MediaFilePlayer.h"

#include <Autolock.h>
#include <Locker.h>
#include <MediaFiles.h>
#include <OS.h>
#include <ObjectList.h>

#include <stdlib.h>


static BObjectList<MediaFilePlayer> sPlayers;
static BLocker sPlayersLock("media file players");
static sem_id sReleaseSem = -1;
static thread_id sReleaseThread = -1;


static int32
release_finished_players(void* /*cookie*/)
{
	while (acquire_sem(sReleaseSem) == B_OK) {
		// The callback that asked for this is still on the player thread.
		snooze(30000);

		BAutolock _(sPlayersLock);
		for (int32 i = sPlayers.CountItems() - 1; i >= 0; i--) {
			MediaFilePlayer* player = sPlayers.ItemAt(i);
			if (player != NULL && player->IsFinished()) {
				sPlayers.RemoveItemAt(i);
				delete player;
			}
		}
	}
	return 0;
}


static void
ensure_release_thread()
{
	if (sReleaseThread >= 0)
		return;

	sReleaseSem = create_sem(0, "media file player release");
	if (sReleaseSem < 0)
		return;

	sReleaseThread = spawn_thread(release_finished_players,
		"media file player release", B_NORMAL_PRIORITY, NULL);
	if (sReleaseThread < 0) {
		delete_sem(sReleaseSem);
		sReleaseSem = -1;
		return;
	}
	resume_thread(sReleaseThread);
}


MediaFilePlayer*
FindMediaFilePlayer(MediaFilePlayer* player, void* media_name)
{
	if (strcmp(player->Name(), (const char*)media_name) == 0)
		return player;
	return NULL;
}


void
PlayMediaFile(const char* media_type, const char* media_name)
{
	entry_ref ref;
	if (BMediaFiles().GetRefFor(media_type, media_name, &ref) != B_OK
		|| !BEntry(&ref).Exists())
		return;

	BAutolock _(sPlayersLock);
	ensure_release_thread();

	MediaFilePlayer* player = sPlayers.EachElement(FindMediaFilePlayer,
		(void*)media_name);

	if (player != NULL) {
		if (*(player->Ref()) == ref) {
			player->Restart();
			return;
		}

		sPlayers.RemoveItem(player);
		delete player;
		player = NULL;
	}

	if (player == NULL) {
		player = new MediaFilePlayer(media_type, media_name, &ref);
		if (player->InitCheck() == B_OK)
			sPlayers.AddItem(player);
		else
			delete player;
	}
}



MediaFilePlayer::MediaFilePlayer(const char* media_type,
	const char* media_name, entry_ref* ref)
	:
	fName(media_name),
	fInitCheck(B_ERROR),
	fRef(*ref),
	fSoundPlayer(NULL),
	fPlayTrack(NULL),
	fFinished(false)
{
	fPlayFile = new BMediaFile(&fRef);
	fInitCheck = fPlayFile->InitCheck();
	if (fInitCheck != B_OK)
		return;

	fPlayFormat.Clear();

	for (int i=0; i < fPlayFile->CountTracks(); i++) {
		BMediaTrack* track = fPlayFile->TrackAt(i);
		if (track == NULL)
			continue;
		fPlayFormat.type = B_MEDIA_RAW_AUDIO;
		fPlayFormat.u.raw_audio.buffer_size = 256;
		if ((track->DecodedFormat(&fPlayFormat) == B_OK)
			&& (fPlayFormat.type == B_MEDIA_RAW_AUDIO)) {
			fPlayTrack = track;
			break;
		}
		fPlayFile->ReleaseTrack(track);
	}

	if (fPlayTrack == NULL) {
		fInitCheck = B_BAD_VALUE;
		return;
	}

	fSoundPlayer = new BSoundPlayer(&fPlayFormat.u.raw_audio,
		media_name, PlayFunction, NULL, this);

	fInitCheck = fSoundPlayer->InitCheck();
	if (fInitCheck != B_OK)
		return;

	fSoundPlayer->SetHasData(true);
	fSoundPlayer->Start();
}


MediaFilePlayer::~MediaFilePlayer()
{
	delete fSoundPlayer;
	delete fPlayFile;
}


status_t
MediaFilePlayer::InitCheck()
{
	return fInitCheck;
}


const char*
MediaFilePlayer::Name()
{
	return fName;
}


const entry_ref*
MediaFilePlayer::Ref()
{
	return &fRef;
}


bool
MediaFilePlayer::IsPlaying()
{
	return (fSoundPlayer != NULL && fSoundPlayer->HasData());
}


bool
MediaFilePlayer::IsFinished() const
{
	return fFinished;
}


void
MediaFilePlayer::Restart()
{
	fSoundPlayer->Stop();
	fFinished = false;
	int64 frame = 0;
	fPlayTrack->SeekToFrame(&frame);
	fSoundPlayer->SetHasData(true);
	fSoundPlayer->Start();
}


void
MediaFilePlayer::Stop()
{
	fSoundPlayer->Stop();
}


void
MediaFilePlayer::PlayFunction(void* cookie, void* buffer,
	size_t size, const media_raw_audio_format& format)
{
	MediaFilePlayer* player = (MediaFilePlayer*)cookie;
	int64 frames = 0;
	player->fPlayTrack->ReadFrames(buffer, &frames);

	if (frames <= 0) {
		player->fSoundPlayer->SetHasData(false);
		// Drop the player off this thread. Its destructor disconnects the
		// mixer input and releases the mixer node (Haiku #16739).
		player->fFinished = true;
		if (sReleaseSem >= 0)
			release_sem(sReleaseSem);
	}
}
