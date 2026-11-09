/*
 * Copyright 2012, Alex Smith, alex@alex-smith.me.uk.
 * Distributed under the terms of the MIT License.
 */

/*
 * The R5 change in this file is the SPDX snippet. Copyright 2026 R5OS
 * Contributors. The Haiku copyright and the license above still cover the
 * original work. The snippet is GPL-3.0-or-later.
 * Storing the TLS symbol value alone would be wrong: %fs is the slot array,
 * not the ELF TLS block. The stored value is the offset from %fs to the
 * template bytes placed for the startup images.
 */


#include "runtime_loader_private.h"

#include <runtime_loader.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "elf_tls.h"


static status_t
relocate_rela(image_t* rootImage, image_t* image, Elf64_Rela* rel,
	size_t relLength, SymbolLookupCache* cache)
{
	for (size_t i = 0; i < relLength / sizeof(Elf64_Rela); i++) {
		int type = ELF64_R_TYPE(rel[i].r_info);
		int symIndex = ELF64_R_SYM(rel[i].r_info);
		Elf64_Addr symAddr = 0;
		image_t* symbolImage = NULL;

		// Resolve the symbol, if any.
		if (symIndex != 0) {
			Elf64_Sym* sym = SYMBOL(image, symIndex);

			status_t status = resolve_symbol(rootImage, image, sym, cache,
				&symAddr, &symbolImage);
			if (status != B_OK) {
				TRACE(("resolve symbol \"%s\" returned: %" B_PRId32 "\n",
					SYMNAME(image, sym), status));
				printf("resolve symbol \"%s\" returned: %" B_PRId32 "\n",
					SYMNAME(image, sym), status);
				return status;
			}
		}

		// Address of the relocation.
		Elf64_Addr relocAddr = image->regions[0].delta + rel[i].r_offset;

		// Calculate the relocation value.
		Elf64_Addr relocValue;
		switch (type) {
			case R_X86_64_NONE:
				continue;
			case R_X86_64_64:
			case R_X86_64_GLOB_DAT:
			case R_X86_64_JUMP_SLOT:
				relocValue = symAddr + rel[i].r_addend;
				break;
			case R_X86_64_PC32:
				relocValue = symAddr + rel[i].r_addend - rel[i].r_offset;
				break;
			case R_X86_64_RELATIVE:
				relocValue = image->regions[0].delta + rel[i].r_addend;
				break;
			case R_X86_64_DTPMOD64:
				relocValue = symbolImage == NULL
							? image->dso_tls_id : symbolImage->dso_tls_id;
				break;
			case R_X86_64_DTPOFF32:
			case R_X86_64_DTPOFF64:
				relocValue = symAddr;
				break;
			// SPDX-SnippetBegin
			// SPDX-License-Identifier: GPL-3.0-or-later
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			case R_X86_64_TPOFF32:
			case R_X86_64_TPOFF64: {
				unsigned dso = symbolImage != NULL
					? symbolImage->dso_tls_id : image->dso_tls_id;
				relocValue = r5_initial_exec_offset(dso, symAddr, rel[i].r_addend);
				break;
			}
			// SPDX-SnippetEnd
			default:
				TRACE(("unhandled relocation type %d\n", type));
				return B_BAD_DATA;
		}

		if (type == R_X86_64_PC32 || type == R_X86_64_DTPOFF32
			|| type == R_X86_64_TPOFF32)
			*(Elf32_Addr *)relocAddr = relocValue;
		else
			*(Elf64_Addr *)relocAddr = relocValue;
	}

	return B_OK;
}


status_t
arch_relocate_image(image_t* rootImage, image_t* image,
	SymbolLookupCache* cache)
{
	status_t status;

	// No REL on x86_64.

	// Perform RELA relocations.
	if (image->rela) {
		status = relocate_rela(rootImage, image, image->rela, image->rela_len,
			cache);
		if (status != B_OK)
			return status;
	}

	// PLT relocations (they are RELA on x86_64).
	if (image->pltrel) {
		status = relocate_rela(rootImage, image, (Elf64_Rela*)image->pltrel,
			image->pltrel_len, cache);
		if (status != B_OK)
			return status;
	}

	// SPDX-SnippetBegin
	// SPDX-License-Identifier: GPL-3.0-or-later
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	materialize_initial_exec_tls();
	// SPDX-SnippetEnd

	return B_OK;
}
