/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


/*!	A decorator add-on as one built before the R5 hooks: it has the entry
	point but no ABI version. DecorManager must refuse it and keep the
	default decorator. It is never part of an image.
*/


#include <new>

#include "DecorManager.h"


extern "C" DecorAddOn*
(instantiate_decor_addon)(image_id id, const char* name)
{
	return new (std::nothrow) DecorAddOn(id, name);
}
