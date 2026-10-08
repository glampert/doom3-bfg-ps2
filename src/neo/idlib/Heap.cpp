/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").  

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#pragma hdrstop
#include "../idlib/precompiled.h"

// [PS2_D3BFG]: route engine allocations through the backend's tagged, aligned ledger.
#if defined( ID_PS2 ) || defined( ID_HOST_TEST )
#include "ps2/system/heap.h"
static_assert( TAG_NEW == ps2::heap::kNewTag && TAG_NUM_TAGS <= ps2::heap::kTagCount );
#endif

// [PS2_D3BFG]: Tagged C++ new has non-null zero-size behavior and preserves its type's alignment.
#if defined( ID_PS2 ) || defined( ID_HOST_TEST )
void * Mem_AllocAligned( size_t size, size_t alignment, memTag_t tag ) {
	if ( size > static_cast<size_t>( INT_MAX ) || tag < 0 || tag >= TAG_NUM_TAGS ) {
		ps2::heap::Fail( "invalid aligned Doom allocation" );
	}
	return ps2::heap::Alloc( size, static_cast<uint16_t>( tag ), alignment );
}
#endif

//===============================================================
//
//	memory allocation all in one place
//
//===============================================================

#undef new

/*
==================
Mem_Alloc16
==================
*/
void * Mem_Alloc16( const int size, const memTag_t tag ) {
	if ( !size ) {
		return NULL;
	}
	// [PS2_D3BFG]: reject signed overflow/invalid tags before converting to size_t.
#if defined( ID_PS2 ) || defined( ID_HOST_TEST )
	if ( size < 0 || tag < 0 || tag >= TAG_NUM_TAGS ) {
		ps2::heap::Fail( "invalid Doom allocation" );
	}
	return ps2::heap::Alloc( static_cast<size_t>( size ), static_cast<uint16_t>( tag ), 16 );
#else
	const int paddedSize = ( size + 15 ) & ~15;
	return _aligned_malloc( paddedSize, 16 );
#endif
}

/*
==================
Mem_Free16
==================
*/
void Mem_Free16( void *ptr ) {
	if ( ptr == NULL ) {
		return;
	}
	// [PS2_D3BFG]: metadata records the requested size even for unsized deletes.
#if defined( ID_PS2 ) || defined( ID_HOST_TEST )
	ps2::heap::Free( ptr );
#else
	_aligned_free( ptr );
#endif
}

/*
==================
Mem_ClearedAlloc
==================
*/
void * Mem_ClearedAlloc( const int size, const memTag_t tag ) {
	void * mem = Mem_Alloc( size, tag );
	// [PS2_D3BFG]: permit zero-size allocations and allocation before SIMD startup.
	if ( mem != NULL ) {
		memset( mem, 0, size );
	}
	return mem;
}

/*
==================
Mem_CopyString
==================
*/
char *Mem_CopyString( const char *in ) {
	// [PS2_D3BFG]: bound the size before converting it to the engine's signed API.
	const size_t length = strlen( in );
	if ( length >= static_cast<size_t>( INT_MAX ) ) {
		idLib::FatalError( "Mem_CopyString exceeds engine allocation range" );
	}
	char * out = (char *)Mem_Alloc( static_cast<int>( length + 1 ), TAG_STRING );
	strcpy( out, in );
	return out;
}
