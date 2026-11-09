/*
 * Copyright 2014, Paweł Dziepak, pdziepak@quarnos.org.
 * Distributed under the terms of the MIT License.
 */

/*
 * The R5 change in this file is the SPDX snippet. Copyright 2026 R5OS
 * Contributors. The Haiku copyright and the license above still cover the
 * original work. The snippet is GPL-3.0-or-later.
 */
#ifndef ELF_TLS_H
#define ELF_TLS_H


#include "runtime_loader_private.h"
#include "utility.h"


class TLSBlock;

class TLSBlockTemplate {
public:
						TLSBlockTemplate()
							: fGeneration(0), fAddress(NULL), fFileSize(0),
								fMemorySize(0), fAlignment(16), fStaticOffset(0),
								fHasStaticOffset(false) { }
	inline				TLSBlockTemplate(void* address, size_t fileSize,
							size_t memorySize);

			void		SetGeneration(unsigned generation)
							{ fGeneration = generation; }
			unsigned	Generation() const	{ return fGeneration; }

			void		SetBaseAddress(addr_t baseAddress);

			TLSBlock	CreateBlock();

			// SPDX-SnippetBegin
			// SPDX-License-Identifier: GPL-3.0-or-later
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			size_t		Alignment() const	{ return fAlignment; }
			size_t		FileSize() const	{ return fFileSize; }
			size_t		MemorySize() const	{ return fMemorySize; }
			const void*	Address() const	{ return fAddress; }
			void		SetStaticOffset(size_t offset)
							{ fStaticOffset = offset; fHasStaticOffset = true; }
			size_t		StaticOffset() const	{ return fStaticOffset; }
			bool		HasStaticOffset() const	{ return fHasStaticOffset; }
			// SPDX-SnippetEnd

private:
			unsigned	fGeneration;

			void*		fAddress;
			size_t		fFileSize;
			size_t		fMemorySize;
			// SPDX-SnippetBegin
			// SPDX-License-Identifier: GPL-3.0-or-later
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			size_t		fAlignment;
			size_t		fStaticOffset;
			bool		fHasStaticOffset;
			// SPDX-SnippetEnd
};

class TLSBlockTemplates {
public:
	static	TLSBlockTemplates&	Get();

			unsigned	Register(const TLSBlockTemplate& block);
			void		Unregister(unsigned dso);

			void		SetBaseAddress(unsigned dso, addr_t baseAddress);

			unsigned	GetGeneration(unsigned dso) const;

			TLSBlock	CreateBlock(unsigned dso);

			// SPDX-SnippetBegin
			// SPDX-License-Identifier: GPL-3.0-or-later
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			uint64		StaticPlacement(unsigned dso) const;
			void		Materialize();
			// SPDX-SnippetEnd

private:
	inline				TLSBlockTemplates();

			// SPDX-SnippetBegin
			// SPDX-License-Identifier: GPL-3.0-or-later
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			void		_AssignStaticOffset(unsigned dso);
			addr_t		_GapFromThreadPointer() const;
			bool		_EnsureMapped(addr_t threadPointer, addr_t bytesFromTp);
			// SPDX-SnippetEnd

	static	TLSBlockTemplates*	fInstance;

			unsigned	fGeneration;

			utility::vector<TLSBlockTemplate>	fTemplates;
			utility::vector<unsigned>	fFreeDSOs;

			// SPDX-SnippetBegin
			// SPDX-License-Identifier: GPL-3.0-or-later
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			bool		fStaticBaseReady;
			addr_t		fStaticBase;
			size_t		fStaticNext;
			// SPDX-SnippetEnd
};


TLSBlockTemplate::TLSBlockTemplate(void* address, size_t fileSize,
	size_t memorySize)
	:
	fGeneration(0),
	fAddress(address),
	fFileSize(fileSize),
	fMemorySize(memorySize),
	fAlignment(16),
	fStaticOffset(0),
	fHasStaticOffset(false)
{
}


void*	get_tls_address(unsigned dso, addr_t offset);
void	destroy_thread_tls();
void	materialize_initial_exec_tls();
uint64	r5_initial_exec_offset(unsigned dso, uint64 symbolOffset, int64 addend);


#endif	// ELF_TLS_H
