/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * List the video inputs and, when one is there, count its frames for five
 * seconds. This is not camgrab.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Buffer.h>
#include <BufferConsumer.h>
#include <MediaNode.h>
#include <MediaRoster.h>
#include <OS.h>
#include <TimeSource.h>


static const bigtime_t kRunTime = 5000000;


class Grabber : public BMediaNode, public BBufferConsumer {
public:
								Grabber();

			int32				Count() const { return fCount; }
			size_t				LastSize() const { return fLastSize; }
			media_format		Format() const { return fFormat; }
			bool				SaveLast(const char* path) const;

	virtual	BMediaAddOn*		AddOn(int32* id) const;
	virtual	status_t			HandleMessage(int32 message, const void* data,
									size_t size);

protected:
	virtual	status_t			HandleBuffer(const media_timed_event* event);

	virtual	status_t			AcceptFormat(
									const media_destination& destination,
									media_format* format);
	virtual	status_t			GetNextInput(int32* cookie,
									media_input* outInput);
	virtual	void				DisposeInputCookie(int32 cookie);
	virtual	void				BufferReceived(BBuffer* buffer);
	virtual	void				ProducerDataStatus(
									const media_destination& destination,
									int32 status, bigtime_t atTime);
	virtual	status_t			GetLatencyFor(
									const media_destination& destination,
									bigtime_t* outLatency, media_node_id* outId);
	virtual	status_t			Connected(const media_source& source,
									const media_destination& destination,
									const media_format& format,
									media_input* outInput);
	virtual	void				Disconnected(const media_source& source,
									const media_destination& destination);
	virtual	status_t			FormatChanged(const media_source& source,
									const media_destination& destination,
									int32 changeTag,
									const media_format& format);

private:
			int32				fCount;
			size_t				fLastSize;
			uint8*				fLast;
			media_format		fFormat;
};


Grabber::Grabber()
	:
	BMediaNode("camtest"),
	BBufferConsumer(B_MEDIA_RAW_VIDEO),
	fCount(0),
	fLastSize(0),
	fLast(NULL)
{
	memset(&fFormat, 0, sizeof(fFormat));
}


BMediaAddOn*
Grabber::AddOn(int32* id) const
{
	if (id != NULL)
		*id = 0;
	return NULL;
}


status_t
Grabber::HandleMessage(int32 message, const void* data, size_t size)
{
	return B_OK;
}


status_t
Grabber::HandleBuffer(const media_timed_event* event)
{
	return BBufferConsumer::HandleBuffer(event);
}


status_t
Grabber::AcceptFormat(const media_destination& destination,
	media_format* format)
{
	if (format->type != B_MEDIA_UNKNOWN_TYPE
		&& format->type != B_MEDIA_RAW_VIDEO
		&& format->type != B_MEDIA_ENCODED_VIDEO) {
		return B_MEDIA_BAD_FORMAT;
	}
	return B_OK;
}


status_t
Grabber::GetNextInput(int32* cookie, media_input* outInput)
{
	if (*cookie != 0)
		return B_BAD_INDEX;
	*cookie = 1;
	outInput->destination.port = ControlPort();
	outInput->destination.id = 0;
	outInput->node = Node();
	outInput->format.type = B_MEDIA_RAW_VIDEO;
	strcpy(outInput->name, "camtest");
	return B_OK;
}


void
Grabber::DisposeInputCookie(int32 cookie)
{
}


void
Grabber::BufferReceived(BBuffer* buffer)
{
	if (buffer == NULL)
		return;
	const size_t size = buffer->SizeUsed();
	uint8* copy = (uint8*)malloc(size);
	if (copy != NULL && size > 0)
		memcpy(copy, buffer->Data(), size);
	free(fLast);
	fLast = copy;
	fLastSize = size;
	fCount++;
	buffer->Recycle();
}


void
Grabber::ProducerDataStatus(const media_destination& destination,
	int32 status, bigtime_t atTime)
{
}


status_t
Grabber::GetLatencyFor(const media_destination& destination,
	bigtime_t* outLatency, media_node_id* outId)
{
	*outLatency = 0;
	*outId = Node().node;
	return B_OK;
}


status_t
Grabber::Connected(const media_source& source,
	const media_destination& destination, const media_format& format,
	media_input* outInput)
{
	fFormat = format;
	outInput->destination = destination;
	outInput->source = source;
	outInput->format = format;
	outInput->node = Node();
	return B_OK;
}


void
Grabber::Disconnected(const media_source& source,
	const media_destination& destination)
{
}


status_t
Grabber::FormatChanged(const media_source& source,
	const media_destination& destination, int32 changeTag,
	const media_format& format)
{
	fFormat = format;
	return B_OK;
}


bool
Grabber::SaveLast(const char* path) const
{
	if (fLast == NULL || fLastSize == 0)
		return false;
	FILE* file = fopen(path, "wb");
	if (file == NULL)
		return false;
	const size_t wrote = fwrite(fLast, 1, fLastSize, file);
	fclose(file);
	return wrote == fLastSize;
}


static bool
is_video_input(const live_node_info& info)
{
	if ((info.node.kind & B_BUFFER_PRODUCER) == 0)
		return false;
	if ((info.node.kind & B_PHYSICAL_INPUT) == 0)
		return false;
	if (strstr(info.name, "None") != NULL)
		return false;
	return true;
}


int
main()
{
	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL) {
		fprintf(stderr, "camtest: no media roster\n");
		return 1;
	}

	live_node_info nodes[32];
	int32 count = 32;
	media_format video;
	memset(&video, 0, sizeof(video));
	video.type = B_MEDIA_RAW_VIDEO;
	status_t status = roster->GetLiveNodes(nodes, &count, NULL, &video, NULL,
		B_BUFFER_PRODUCER | B_PHYSICAL_INPUT);
	if (status != B_OK)
		count = 0;

	int32 cameras = 0;
	int32 chosen = -1;
	for (int32 i = 0; i < count; i++) {
		if (!is_video_input(nodes[i]))
			continue;
		printf("camera: %s\n", nodes[i].name);
		if (chosen < 0)
			chosen = i;
		cameras++;
	}
	if (cameras == 0) {
		printf("no camera\n");
		return 0;
	}

	Grabber* grabber = new Grabber();
	status = roster->RegisterNode(grabber);
	if (status != B_OK) {
		fprintf(stderr, "camtest: register failed: %s\n", strerror(status));
		delete grabber;
		return 1;
	}

	media_output outputs[4];
	int32 outputCount = 4;
	status = roster->GetFreeOutputsFor(nodes[chosen].node, outputs, 4,
		&outputCount, B_MEDIA_RAW_VIDEO);
	if (status != B_OK || outputCount < 1) {
		fprintf(stderr, "camtest: no free output\n");
		roster->UnregisterNode(grabber);
		delete grabber;
		return 1;
	}

	media_input inputs[1];
	int32 inputCount = 1;
	status = roster->GetFreeInputsFor(grabber->Node(), inputs, 1, &inputCount);
	if (status != B_OK || inputCount < 1) {
		fprintf(stderr, "camtest: no free input\n");
		roster->UnregisterNode(grabber);
		delete grabber;
		return 1;
	}

	media_format format = outputs[0].format;
	media_output connectedOut;
	media_input connectedIn;
	status = roster->Connect(outputs[0].source, inputs[0].destination,
		&format, &connectedOut, &connectedIn);
	if (status != B_OK) {
		fprintf(stderr, "camtest: connect failed: %s\n", strerror(status));
		roster->UnregisterNode(grabber);
		delete grabber;
		return 1;
	}

	roster->SetRunModeNode(nodes[chosen].node, B_RECORDING);
	roster->StartNode(nodes[chosen].node, 0);
	snooze(kRunTime);
	roster->StopNode(nodes[chosen].node, 0, true);
	roster->Disconnect(nodes[chosen].node.node, connectedOut.source,
		grabber->Node().node, connectedIn.destination);

	const media_raw_video_format& raw = grabber->Format().u.raw_video;
	printf("frames %" B_PRId32 ", %" B_PRIu32 " x %" B_PRIu32 ", %zu bytes\n",
		grabber->Count(), raw.display.line_width, raw.display.line_count,
		grabber->LastSize());
	if (grabber->SaveLast("camtest-last.bin"))
		printf("saved camtest-last.bin\n");

	roster->UnregisterNode(grabber);
	delete grabber;
	return 0;
}
