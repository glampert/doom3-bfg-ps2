// ================================================================================================
// File: vertex_stubs.cpp
// Brief: Retain empty native buffer metadata while rejecting unavailable vertex/index/joint storage.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/null_render.h"
#include <idlib/precompiled.h>
#include <renderer/tr_local.h>

idVertexCache vertexCache{};

// Empty construction must work before main without allocating desktop buffer pools.
idVertexBuffer::idVertexBuffer()
    : size(0), offsetInOtherBuffer(0), apiObject(nullptr) {}
idIndexBuffer::idIndexBuffer()
    : size(0), offsetInOtherBuffer(0), apiObject(nullptr) {}
idJointBuffer::idJointBuffer()
    : numJoints(0), offsetInOtherBuffer(0), apiObject(nullptr) {}
idVertexBuffer::~idVertexBuffer() { FreeBufferObject(); }
idIndexBuffer::~idIndexBuffer() { FreeBufferObject(); }
idJointBuffer::~idJointBuffer() { FreeBufferObject(); }

void idVertexBuffer::FreeBufferObject()
{
    if (apiObject != nullptr || size != 0 || offsetInOtherBuffer != 0)
    {
        ps2::render::Unavailable("idVertexBuffer::FreeBufferObject with backend state");
    }
}
void idIndexBuffer::FreeBufferObject()
{
    if (apiObject != nullptr || size != 0 || offsetInOtherBuffer != 0)
    {
        ps2::render::Unavailable("idIndexBuffer::FreeBufferObject with backend state");
    }
}
void idJointBuffer::FreeBufferObject()
{
    if (apiObject != nullptr || numJoints != 0 || offsetInOtherBuffer != 0)
    {
        ps2::render::Unavailable("idJointBuffer::FreeBufferObject with backend state");
    }
}

namespace
{
void ClearEmptySet(geoBufferSet_t & buffers)
{
    buffers.vertexBuffer.FreeBufferObject();
    buffers.indexBuffer.FreeBufferObject();
    buffers.jointBuffer.FreeBufferObject();
    if (buffers.mappedVertexBase != nullptr || buffers.mappedIndexBase != nullptr || buffers.mappedJointBase != nullptr ||
        buffers.vertexMemUsed.GetValue() != 0 || buffers.indexMemUsed.GetValue() != 0 || buffers.jointMemUsed.GetValue() != 0 ||
        buffers.allocations != 0)
    {
        ps2::render::Unavailable("idVertexCache empty cleanup with live storage");
    }
}
} // namespace

void idVertexCache::FreeStaticData()
{
    ClearEmptySet(staticData);
    mostUsedVertex = mostUsedIndex = mostUsedJoint = 0;
}
void idVertexCache::Shutdown()
{
    FreeStaticData();
    for (geoBufferSet_t & buffers : frameData)
    {
        ClearEmptySet(buffers);
    }
}

#define RENDER_UNAVAILABLE(result, method, parameters) \
    PS2_COLD_FUNC result method parameters { ps2::render::Unavailable(#method); }

RENDER_UNAVAILABLE(void, idVertexCache::Init, (bool))
RENDER_UNAVAILABLE(void, idVertexCache::PurgeAll, ())
RENDER_UNAVAILABLE(vertCacheHandle_t, idVertexCache::ActuallyAlloc, (geoBufferSet_t &, const void *, int, cacheType_t))
RENDER_UNAVAILABLE(bool, idVertexCache::GetVertexBuffer, (vertCacheHandle_t, idVertexBuffer *))
RENDER_UNAVAILABLE(bool, idVertexCache::GetIndexBuffer, (vertCacheHandle_t, idIndexBuffer *))
RENDER_UNAVAILABLE(bool, idVertexCache::GetJointBuffer, (vertCacheHandle_t, idJointBuffer *))
RENDER_UNAVAILABLE(void, idVertexCache::BeginBackEnd, ())

RENDER_UNAVAILABLE(bool, idVertexBuffer::AllocBufferObject, (const void *, int))
RENDER_UNAVAILABLE(void, idVertexBuffer::Reference, (const idVertexBuffer &))
RENDER_UNAVAILABLE(void, idVertexBuffer::Reference, (const idVertexBuffer &, int, int))
RENDER_UNAVAILABLE(void, idVertexBuffer::Update, (const void *, int) const)
RENDER_UNAVAILABLE(void *, idVertexBuffer::MapBuffer, (bufferMapType_t) const)
RENDER_UNAVAILABLE(void, idVertexBuffer::UnmapBuffer, () const)
RENDER_UNAVAILABLE(bool, idIndexBuffer::AllocBufferObject, (const void *, int))
RENDER_UNAVAILABLE(void, idIndexBuffer::Reference, (const idIndexBuffer &))
RENDER_UNAVAILABLE(void, idIndexBuffer::Reference, (const idIndexBuffer &, int, int))
RENDER_UNAVAILABLE(void, idIndexBuffer::Update, (const void *, int) const)
RENDER_UNAVAILABLE(void *, idIndexBuffer::MapBuffer, (bufferMapType_t) const)
RENDER_UNAVAILABLE(void, idIndexBuffer::UnmapBuffer, () const)
RENDER_UNAVAILABLE(bool, idJointBuffer::AllocBufferObject, (const float *, int))
RENDER_UNAVAILABLE(void, idJointBuffer::Reference, (const idJointBuffer &))
RENDER_UNAVAILABLE(void, idJointBuffer::Reference, (const idJointBuffer &, int, int))
RENDER_UNAVAILABLE(void, idJointBuffer::Update, (const float *, int) const)
RENDER_UNAVAILABLE(float *, idJointBuffer::MapBuffer, (bufferMapType_t) const)
RENDER_UNAVAILABLE(void, idJointBuffer::UnmapBuffer, () const)
RENDER_UNAVAILABLE(void, idJointBuffer::Swap, (idJointBuffer &))
RENDER_UNAVAILABLE(void, UnbindBufferObjects, ())
RENDER_UNAVAILABLE(void, CopyBuffer, (byte *, const byte *, int))

#undef RENDER_UNAVAILABLE
