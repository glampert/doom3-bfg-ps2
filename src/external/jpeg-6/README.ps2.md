# Doom PS2 JPEG configuration

These files are Doom 3 BFG's modified IJG JPEG 6, moved from
`src/neo/renderer/jpeg-6/`. The engine's RGBA channel layout and float DCT remain.
The existing `[PS2_D3BFG]` fix in `jcparam.cpp` bounds the encoder Huffman-symbol copy.
This is not an upgrade to IJG 6a: `README.ijg` restores the original distribution terms
from the [archived IJG 6a README](https://www.ijg.org/files/jpegsrc.v6a.tar.gz).

The target selects 25 decompression sources explicitly in `config/sources.mk`.
`ps2/system/codec_memory.cpp` replaces the no-backing-store malloc/free implementation
with tagged, aligned allocations; no temporary-file manager is built. The codec handles
allocation rejection through its error manager. The strict `ps2/ui/jpeg_decoder.cpp`
supplies bounded complete-memory input and a 1024-pixel dimension / 4 MiB output limit.
Table-only and abbreviated SWF images preserve the native decoder lifetime.

The legacy `jdatasrc.cpp` memory reader has no input length and is excluded from the EE.
Portable `LoadJPG` explicitly fails until converted source-image loading is supplied.
The full encoder is retained only in the host fixture list; it is not in a target link.
Original vendor notices and source formatting are preserved.
