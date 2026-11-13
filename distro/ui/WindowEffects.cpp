/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


#include <WindowEffects.h>

#include <View.h>
#include <Window.h>

#include <AppServerLink.h>
#include <ServerProtocol.h>
#include <AppMisc.h>


namespace BAppearanceKit {


/*!	The user's choice of B_WINDOW_SHADOW, B_WINDOW_FROSTED_GLASS and
	B_WINDOW_ANIMATIONS, from app_server. All three when it cannot be asked,
	the default of a new system.
*/
uint32
window_effects()
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_R5_GET_WINDOW_EFFECTS);
	int32 code;
	uint32 effects = B_WINDOW_SHADOW | B_WINDOW_FROSTED_GLASS
		| B_WINDOW_ANIMATIONS;
	if (link.FlushWithReply(code) == B_OK && code == B_OK)
		link.Read<uint32>(&effects);
	return effects;
}


status_t
set_window_effects(uint32 effects)
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_R5_SET_WINDOW_EFFECTS);
	link.Attach<int32>((int32)effects);
	int32 code;
	status_t status = link.FlushWithReply(code);
	return status == B_OK ? code : status;
}


static status_t
send_glass(int32 what, int32 token, bool glass)
{
	BPrivate::AppServerLink link;
	link.StartMessage(what);
	link.Attach<int32>(token);
	link.Attach<bool>(glass);
	int32 code;
	status_t status = link.FlushWithReply(code);
	return status == B_OK ? code : status;
}


status_t
set_window_glass(BWindow* window, bool glass)
{
	if (window == NULL)
		return B_BAD_VALUE;
	return send_glass(AS_R5_SET_WINDOW_GLASS, _get_object_token_(window),
		glass);
}


status_t
set_view_glass(BView* view, bool glass)
{
	if (view == NULL || view->Window() == NULL)
		return B_BAD_VALUE;
	// The view is made on the window's link; this call goes on the
	// application's. Sync() lets app_server know the view first.
	view->Sync();
	return send_glass(AS_R5_SET_VIEW_GLASS, _get_object_token_(view), glass);
}


enum window_effect_strength
window_effect_strength()
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_R5_GET_WINDOW_EFFECT_STRENGTH);
	int32 code;
	int32 strength = B_WINDOW_EFFECTS_BALANCED;
	if (link.FlushWithReply(code) == B_OK && code == B_OK)
		link.Read<int32>(&strength);
	if (strength < B_WINDOW_EFFECTS_QUIET || strength > B_WINDOW_EFFECTS_GLASSY)
		strength = B_WINDOW_EFFECTS_BALANCED;
	return (enum window_effect_strength)strength;
}


status_t
set_window_effect_strength(enum window_effect_strength strength)
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_R5_SET_WINDOW_EFFECT_STRENGTH);
	link.Attach<int32>((int32)strength);
	int32 code;
	status_t status = link.FlushWithReply(code);
	return status == B_OK ? code : status;
}


}	// namespace BAppearanceKit
