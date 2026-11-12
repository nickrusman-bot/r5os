/*
 * Copyright 2014, Paweł Dziepak, pdziepak@quarnos.org.
 * Distributed under the terms of the MIT License.
 */

#include "elf_tls.h"

#include <stdlib.h>
#include <string.h>

#include <OS.h>
#include <support/TLS.h>
#include <syscalls.h>

#include <tls.h>

#include <util/kernel_cpp.h>

#include "r5_tpoff.h"


class TLSBlock {
public:
	inline				TLSBlock();
	inline				TLSBlock(void* pointer);

	inline	status_t	Initialize(unsigned dso);

			void		Destroy();

			bool		IsInvalid() const	{ return fPointer == NULL; }

			void*		operator+(addr_t offset) const
							{ return (void*)((addr_t)fPointer + TLS_DTV_OFFSET + offset); }

private:
			void*		fPointer;
};

class Generation {
public:
	inline				Generation();

			unsigned	Counter() const	{ return fCounter; }
			unsigned	Size() const	{ return fSize; }

			void		SetCounter(unsigned counter)	{ fCounter = counter; }
			void		SetSize(unsigned size)	{ fSize = size; }

private:
			unsigned	fCounter;
			unsigned	fSize;
};

class DynamicThreadVector {
public:
	inline				DynamicThreadVector();

			void		DestroyAll();

	inline	TLSBlock&	operator[](unsigned dso);

private:
			bool		_DoesExist() const	{ return *fVector != NULL; }
			unsigned	_Size() const
							{ return _DoesExist()
									? fGeneration->Size() : 0; }

			unsigned	_Generation() const;

			status_t	_ResizeVector(unsigned minimumSize);

			TLSBlock**	fVector;
			Generation*	fGeneration;
			TLSBlock	fNullBlock;
};


TLSBlockTemplates*	TLSBlockTemplates::fInstance;


void
TLSBlockTemplate::SetBaseAddress(addr_t baseAddress)
{
	fAddress = (void*)((addr_t)fAddress + baseAddress);
}


TLSBlock
TLSBlockTemplate::CreateBlock()
{
	void* pointer = malloc(fMemorySize + TLS_DTV_OFFSET);
	if (pointer == NULL)
		return TLSBlock();
	memset(pointer, 0, TLS_DTV_OFFSET);
	memcpy((char*)pointer + TLS_DTV_OFFSET, fAddress, fFileSize);
	if (fMemorySize > fFileSize)
		memset((char*)pointer + TLS_DTV_OFFSET + fFileSize, 0, fMemorySize - fFileSize);
	return TLSBlock(pointer);
}


TLSBlockTemplates&
TLSBlockTemplates::Get()
{
	if (fInstance == NULL)
		fInstance = new TLSBlockTemplates;
	return *fInstance;
}


unsigned
TLSBlockTemplates::Register(const TLSBlockTemplate& block)
{
	unsigned dso;

	if (!fFreeDSOs.empty()) {
		dso = fFreeDSOs.back();
		fFreeDSOs.pop_back();
		fTemplates[dso] = block;
	} else {
		dso = fTemplates.size();
		fTemplates.push_back(block);
	}

	fTemplates[dso].SetGeneration(fGeneration);
	// SPDX-SnippetBegin
	// SPDX-License-Identifier: MIT
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	_AssignStaticOffset(dso);
	// SPDX-SnippetEnd
	return dso;
}


void
TLSBlockTemplates::Unregister(unsigned dso)
{
	if (dso == unsigned(-1))
		return;

	fGeneration++;
	fFreeDSOs.push_back(dso);
}


void
TLSBlockTemplates::SetBaseAddress(unsigned dso, addr_t baseAddress)
{
	if (dso != unsigned(-1))
		fTemplates[dso].SetBaseAddress(baseAddress);
}


unsigned
TLSBlockTemplates::GetGeneration(unsigned dso) const
{
	if (dso == unsigned(-1))
		return fGeneration;
	return fTemplates[dso].Generation();
}


TLSBlock
TLSBlockTemplates::CreateBlock(unsigned dso)
{
	return fTemplates[dso].CreateBlock();
}


TLSBlockTemplates::TLSBlockTemplates()
	:
	fGeneration(0),
	fStaticBaseReady(false),
	fStaticBase(0),
	fStaticNext(0)
{
}


TLSBlock::TLSBlock()
	:
	fPointer(NULL)
{
}


TLSBlock::TLSBlock(void* pointer)
	:
	fPointer(pointer)
{
}


status_t
TLSBlock::Initialize(unsigned dso)
{
	fPointer = TLSBlockTemplates::Get().CreateBlock(dso).fPointer;
	return fPointer != NULL ? B_OK : B_NO_MEMORY;
}


void
TLSBlock::Destroy()
{
	free(fPointer);
	fPointer = NULL;
}


Generation::Generation()
	:
	fCounter(0),
	fSize(0)
{
}


DynamicThreadVector::DynamicThreadVector()
	:
	fVector((TLSBlock**)tls_address(TLS_DYNAMIC_THREAD_VECTOR)),
	fGeneration(NULL)
{
	if (*fVector != NULL)
		fGeneration = (Generation*)*(void**)*fVector;
}


void
DynamicThreadVector::DestroyAll()
{
	for (unsigned i = 0; i < _Size(); i++) {
		TLSBlock& block = (*fVector)[i + 1];
		if (!block.IsInvalid())
			block.Destroy();
	}

	free(*fVector);
	*fVector = NULL;

	delete fGeneration;
}


TLSBlock&
DynamicThreadVector::operator[](unsigned dso)
{
	unsigned generation = TLSBlockTemplates::Get().GetGeneration(-1);
	if (_Generation() < generation) {
		// We need to destroy any blocks whose DSO generation has changed
		// to be greater than our own generation.
		for (unsigned dsoIndex = 0; dsoIndex < _Size(); dsoIndex++) {
			TLSBlock& block = (*fVector)[dsoIndex + 1];
			unsigned dsoGeneration
				= TLSBlockTemplates::Get().GetGeneration(dsoIndex);
			if (dsoGeneration > _Generation() && dsoGeneration <= generation)
				block.Destroy();
		}

		fGeneration->SetCounter(generation);
	}

	if (_Size() <= dso) {
		status_t result = _ResizeVector(dso + 1);
		if (result != B_OK)
			return fNullBlock;
	}

	TLSBlock& block = (*fVector)[dso + 1];
	if (block.IsInvalid()) {
		status_t result = block.Initialize(dso);
		if (result != B_OK)
			return fNullBlock;
	}

	return block;
}


unsigned
DynamicThreadVector::_Generation() const
{
	if (fGeneration != NULL)
		return fGeneration->Counter();
	return unsigned(-1);
}


status_t
DynamicThreadVector::_ResizeVector(unsigned minimumSize)
{
	static const unsigned kInitialSize = 4;
	unsigned size = std::max(minimumSize, kInitialSize);
	unsigned oldSize = _Size();
	if (size <= oldSize)
		return B_OK;

	void* newVector = realloc(*fVector, (size + 1) * sizeof(TLSBlock));
	if (newVector == NULL)
		return B_NO_MEMORY;

	*fVector = (TLSBlock*)newVector;
	memset((void*)(*fVector + oldSize + 1), 0, (size - oldSize) * sizeof(TLSBlock));
	if (fGeneration == NULL) {
		fGeneration = new Generation;
		if (fGeneration == NULL)
			return B_NO_MEMORY;
// SPDX-SnippetBegin
// SPDX-License-Identifier: MIT
// SPDX-SnippetCopyrightText: 2026 Witold Bołt
		// A new vector has no blocks yet, so none of them can be stale.
		// It starts at the templates' generation. At 0, unloading any TLS
		// image makes the next access destroy the blocks just created.
		fGeneration->SetCounter(TLSBlockTemplates::Get().GetGeneration(-1));
// SPDX-SnippetEnd
	}

	*(Generation**)*fVector = fGeneration;
	fGeneration->SetSize(size);

	return B_OK;
}


void*
get_tls_address(unsigned dso, addr_t offset)
{
	DynamicThreadVector dynamicThreadVector;
	TLSBlock& block = dynamicThreadVector[dso];
	if (block.IsInvalid())
		return NULL;
	return block + offset;
}


void
destroy_thread_tls()
{
	DynamicThreadVector().DestroyAll();
}


// SPDX-SnippetBegin
// SPDX-License-Identifier: MIT
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors

void
TLSBlockTemplates::_AssignStaticOffset(unsigned dso)
{
	if (!fStaticBaseReady) {
		fStaticBase = _GapFromThreadPointer();
		// The first 16 bytes of the static image hold how many templates
		// this thread has already copied.
		fStaticNext = 16;
		fStaticBaseReady = true;
	}

	TLSBlockTemplate& stored = fTemplates[dso];
	size_t alignment = stored.Alignment();
	if (alignment < 1)
		alignment = 1;
	size_t offset = (fStaticNext + alignment - 1) & ~(alignment - 1);
	stored.SetStaticOffset(offset);
	fStaticNext = offset + stored.MemorySize();
}


addr_t
TLSBlockTemplates::_GapFromThreadPointer() const
{
	addr_t threadPointer = (addr_t)tls_get(TLS_BASE_ADDRESS_SLOT);
	area_info info;
	ssize_t cookie = 0;
	while (_kern_get_next_area_info(B_CURRENT_TEAM, &cookie, &info) == B_OK) {
		addr_t start = (addr_t)info.address;
		addr_t end = start + info.size;
		if (threadPointer >= start && threadPointer < end) {
			addr_t gap = end - threadPointer;
			return (gap + 15) & ~(addr_t)15;
		}
	}

	// Past the 64 TLS slots when the stack area cannot be seen yet.
	return 4096;
}


bool
TLSBlockTemplates::_EnsureMapped(addr_t threadPointer, addr_t bytesFromTp)
{
	area_info info;
	ssize_t cookie = 0;
	while (_kern_get_next_area_info(B_CURRENT_TEAM, &cookie, &info) == B_OK) {
		addr_t start = (addr_t)info.address;
		addr_t end = start + info.size;
		if (threadPointer < start || threadPointer >= end)
			continue;

		addr_t need = threadPointer + bytesFromTp;
		if (need <= end)
			return true;

		size_t newSize = info.size + (need - end);
		newSize = (newSize + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE - 1);
		return _kern_resize_area(info.area, newSize) == B_OK;
	}
	return false;
}


uint64
TLSBlockTemplates::StaticPlacement(unsigned dso) const
{
	if (dso >= fTemplates.size() || !fTemplates[dso].HasStaticOffset())
		return fStaticBase;
	return fStaticBase + fTemplates[dso].StaticOffset();
}


void
TLSBlockTemplates::Materialize()
{
	if (!fStaticBaseReady || fStaticNext <= 16)
		return;

	addr_t threadPointer = (addr_t)tls_get(TLS_BASE_ADDRESS_SLOT);
	if (!_EnsureMapped(threadPointer, fStaticBase + fStaticNext))
		return;

	uint8* base = (uint8*)(threadPointer + fStaticBase);
	uint64 done = *(uint64*)base;
	for (unsigned i = 0; i < fTemplates.size(); i++) {
		if ((uint64)i < done)
			continue;
		TLSBlockTemplate& templ = fTemplates[i];
		if (!templ.HasStaticOffset())
			continue;
		uint8* dest = base + templ.StaticOffset();
		if (templ.Address() != NULL && templ.FileSize() > 0)
			memcpy(dest, templ.Address(), templ.FileSize());
		if (templ.MemorySize() > templ.FileSize()) {
			memset(dest + templ.FileSize(), 0,
				templ.MemorySize() - templ.FileSize());
		}
	}
	*(uint64*)base = fTemplates.size();
}


void
materialize_initial_exec_tls()
{
	TLSBlockTemplates::Get().Materialize();
}


uint64
r5_initial_exec_offset(unsigned dso, uint64 symbolOffset, int64 addend)
{
	uint64 placement = 0;
	if (dso != unsigned(-1))
		placement = TLSBlockTemplates::Get().StaticPlacement(dso);
	return r5_tpoff_offset(placement, symbolOffset, addend);
}

// SPDX-SnippetEnd

