/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * M11 step 3b: a short conformance check of buffer sharing on the
 * machine (docs/APP-SERVER-COMPOSITOR.md, "Pass and fail lines"). The
 * rules themselves are covered by the host test (scripts/test-r5-share.cpp).
 *
 *   r5-share-check          the cases below (argv[0] app_server as a user,
 *                           an import by a non-app_server, a second import)
 *   r5-share-check hold     exports a buffer and waits; the stick script
 *                           stops it with kill
 *   r5-share-check count    prints the system's buffer count
 *
 * Each case prints "r5-share-check: <case>: pass" or "...: fail <why>".
 */

#include "r5_gpu.h"

#include <AppMisc.h>
#include <AppServerLink.h>
#include <Application.h>
#include <ServerProtocol.h>
#include <View.h>
#include <Window.h>

#include <OS.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>


static const char* kAppServer = "/system/servers/app_server";
static const uint32 kWidth = 64;
static const uint32 kHeight = 64;
static const uint32 kPitch = kWidth * 4;


static int
open_node()
{
	int fd = open("/dev/gpu/radeon_hd/0", O_RDWR);
	if (fd < 0)
		return -1;
	r5_gpu_ctx ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.size = sizeof(ctx);
	ctx.version = 1;
	if (ioctl(fd, R5_GPU_CTX_CREATE, &ctx, sizeof(ctx)) != 0) {
		close(fd);
		return -1;
	}
	return fd;
}


static bool
new_buffer(int fd, uint32* handle)
{
	r5_gpu_bo bo;
	memset(&bo, 0, sizeof(bo));
	bo.size = sizeof(bo);
	bo.version = 1;
	bo.domain = R5_GPU_DOMAIN_GTT;
	bo.bytes = (uint64)kPitch * kHeight;
	if (ioctl(fd, R5_GPU_BO_CREATE, &bo, sizeof(bo)) != 0)
		return false;
	*handle = bo.handle;
	return true;
}


static status_t
export_buffer(int fd, uint32 handle, uint8 token[R5_GPU_SHARE_TOKEN_BYTES])
{
	r5_gpu_bo_export out;
	memset(&out, 0, sizeof(out));
	out.size = sizeof(out);
	out.version = 1;
	out.handle = handle;
	out.target = R5_GPU_SHARE_COMPOSITOR;
	if (ioctl(fd, R5_GPU_BO_EXPORT, &out, sizeof(out)) != 0)
		return errno;
	memcpy(token, out.token, R5_GPU_SHARE_TOKEN_BYTES);
	return B_OK;
}


static void
result(const char* name, bool pass, const char* why)
{
	if (pass)
		printf("r5-share-check: %s: pass\n", name);
	else
		printf("r5-share-check: %s: fail %s\n", name, why);
}


//	Case 1: only app_server imports. This program is not app_server.
static bool
case_not_app_server(int fd)
{
	uint32 handle = 0;
	uint8 token[R5_GPU_SHARE_TOKEN_BYTES];
	if (!new_buffer(fd, &handle) || export_buffer(fd, handle, token) != B_OK) {
		result("import by a program that is not app_server refused", false,
			"export");
		return false;
	}
	r5_gpu_bo_import in;
	memset(&in, 0, sizeof(in));
	in.size = sizeof(in);
	in.version = 1;
	memcpy(in.token, token, sizeof(in.token));
	in.client_team = getpid();
	bool refused = ioctl(fd, R5_GPU_BO_IMPORT, &in, sizeof(in)) != 0
		&& errno == B_NOT_ALLOWED;
	result("import by a program that is not app_server refused", refused,
		"not B_NOT_ALLOWED");
	return refused;
}


/*	Case 3, the copy this program runs of itself with argv[0] set to
	app_server's path (execv chooses argv[0] freely): as an ordinary user
	it must still be refused. Exit 0 refused, 1 accepted, 2 no buffer.
*/
static int
spoofed_child()
{
	int fd = open_node();
	uint32 handle = 0;
	uint8 token[R5_GPU_SHARE_TOKEN_BYTES];
	if (fd < 0 || !new_buffer(fd, &handle)
		|| export_buffer(fd, handle, token) != B_OK)
		return 2;
	r5_gpu_bo_import in;
	memset(&in, 0, sizeof(in));
	in.size = sizeof(in);
	in.version = 1;
	memcpy(in.token, token, sizeof(in.token));
	in.client_team = getpid();
	bool refused = ioctl(fd, R5_GPU_BO_IMPORT, &in, sizeof(in)) != 0
		&& errno == B_NOT_ALLOWED;
	return refused ? 0 : 1;
}


static bool
case_spoofed_app_server()
{
	const char* name = "argv[0] app_server as a user refused";
	if (geteuid() == 0) {
		printf("r5-share-check: %s: skipped (run it as a user)\n", name);
		return true;
	}
	image_info image;
	int32 cookie = 0;
	const char* self = NULL;
	while (get_next_image_info(0, &cookie, &image) == B_OK) {
		if (image.type == B_APP_IMAGE) {
			self = image.name;
			break;
		}
	}
	if (self == NULL) {
		result(name, false, "own path");
		return false;
	}
	pid_t child = fork();
	if (child == 0) {
		char* args[] = { (char*)kAppServer, (char*)"spoofed", NULL };
		execv(self, args);
		_exit(3);
	}
	int status = 0;
	if (child < 0 || waitpid(child, &status, 0) != child
		|| !WIFEXITED(status)) {
		result(name, false, "child");
		return false;
	}
	int code = WEXITSTATUS(status);
	result(name, code == 0, code == 1 ? "import accepted" : "child setup");
	return code == 0;
}


//	The message the R5 WSI sends once per swapchain, with one image.
static status_t
offer(BView* view, const uint8 token[R5_GPU_SHARE_TOKEN_BYTES], sem_id sem,
	int32* shareId)
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_R5_SHARE_IMAGES);
	link.Attach<int32>(_get_object_token_(view));
	link.Attach<uint32>(1);
	link.Attach(token, R5_GPU_SHARE_TOKEN_BYTES);
	link.Attach<int32>(sem);
	link.Attach<uint32>(kWidth);
	link.Attach<uint32>(kHeight);
	link.Attach<uint32>(kPitch);
	link.Attach<uint32>((uint32)B_RGBA32);
	int32 code = B_ERROR;
	if (link.FlushWithReply(code) != B_OK)
		return B_ERROR;
	if (code == B_OK)
		link.Read<int32>(shareId);
	return code;
}


static void
end(BView* view, int32 shareId)
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_R5_SHARE_END);
	link.Attach<int32>(_get_object_token_(view));
	link.Attach<int32>(shareId);
	int32 code;
	link.FlushWithReply(code);
}


/*	Case 2: the same number offered twice. The first offer is imported
	(needs the compositor on RADV); the second is refused, because a
	number is good for one import.
*/
static bool
case_second_import(int fd, BView* view)
{
	const char* name = "second import of the same number refused";
	uint32 handle = 0;
	uint8 token[R5_GPU_SHARE_TOKEN_BYTES];
	sem_id sem = create_sem(0, "r5-share-check free");
	if (sem < 0 || !new_buffer(fd, &handle)
		|| export_buffer(fd, handle, token) != B_OK) {
		result(name, false, "export");
		return false;
	}
	int32 first = -1;
	int32 second = -1;
	status_t one = offer(view, token, sem, &first);
	status_t two = offer(view, token, sem, &second);
	if (one == B_OK)
		end(view, first);
	if (two == B_OK)
		end(view, second);
	delete_sem(sem);
	if (one != B_OK) {
		result(name, false, "first offer refused (compositor on RADV?)");
		return false;
	}
	result(name, two != B_OK, "second offer accepted");
	return two != B_OK;
}


static int
count_buffers(int fd)
{
	r5_gpu_counters counters;
	memset(&counters, 0, sizeof(counters));
	counters.size = sizeof(counters);
	counters.version = 1;
	if (ioctl(fd, R5_GPU_GET_COUNTERS, &counters, sizeof(counters)) != 0)
		return -1;
	return (int)counters.bo_count;
}


class CheckApp : public BApplication {
public:
	CheckApp()
		:
		BApplication("application/x-vnd.R5-share-check"),
		fStatus(1)
	{
	}

	virtual void ReadyToRun()
	{
		int fd = open_node();
		if (fd < 0) {
			printf("r5-share-check: fail no render node\n");
			PostMessage(B_QUIT_REQUESTED);
			return;
		}
		BWindow* window = new BWindow(BRect(80, 80, 80 + kWidth - 1,
			80 + kHeight - 1), "r5-share-check", B_TITLED_WINDOW,
			B_NOT_RESIZABLE | B_NOT_ZOOMABLE);
		BView* view = new BView(window->Bounds(), "share", B_FOLLOW_ALL, 0);
		window->AddChild(view);
		window->Show();
		// The view exists in app_server before it is named in a message.
		if (window->Lock()) {
			window->Sync();
			window->Unlock();
		}
		bool pass = case_not_app_server(fd);
		pass = case_second_import(fd, view) && pass;
		close(fd);
		fStatus = pass && fSpoofed ? 0 : 1;
		if (window->Lock())
			window->Quit();
		PostMessage(B_QUIT_REQUESTED);
	}

	int Status() const { return fStatus; }
	void SetSpoofed(bool pass) { fSpoofed = pass; }

private:
	int fStatus;
	bool fSpoofed = true;
};


int
main(int argc, char** argv)
{
	if (argc == 2 && strcmp(argv[1], "spoofed") == 0
		&& strcmp(argv[0], kAppServer) == 0)
		return spoofed_child();
	if (argc == 2 && strcmp(argv[1], "count") == 0) {
		int fd = open_node();
		int count = fd >= 0 ? count_buffers(fd) : -1;
		if (fd >= 0)
			close(fd);
		// this file's own context holds no buffer
		printf("r5-share-check: buffers %d\n", count);
		return count < 0 ? 1 : 0;
	}
	if (argc == 2 && strcmp(argv[1], "hold") == 0) {
		int fd = open_node();
		uint32 handle = 0;
		uint8 token[R5_GPU_SHARE_TOKEN_BYTES];
		if (fd < 0 || !new_buffer(fd, &handle)
			|| export_buffer(fd, handle, token) != B_OK) {
			printf("r5-share-check: hold: fail export\n");
			return 1;
		}
		printf("r5-share-check: hold: exported, team %" B_PRId32 "\n",
			getpid());
		fflush(stdout);
		while (true)
			snooze(1000000);
	}
	// Before the application: fork in a running BApplication is not safe.
	bool spoofed = case_spoofed_app_server();
	CheckApp app;
	app.SetSpoofed(spoofed);
	app.Run();
	return app.Status();
}
