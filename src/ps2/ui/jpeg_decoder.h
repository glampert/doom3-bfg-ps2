// ================================================================================================
// File: jpeg_decoder.h
// Brief: Stateful SWF JPEG decoding with bounded input and explicit cleanup before fatal errors.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

namespace ps2::jpeg
{

// Preserve JPEG tables between SWF tags. Returned RGBA storage uses the Doom tagged heap;
// callers transfer/free it with Mem_Free. Tables-only input returns null with zero dimensions.
void * Create();
void Destroy(void * decoder);
unsigned char * Decode(void * decoder, const unsigned char * input, int inputSize, int & width, int & height);

} // namespace ps2::jpeg
