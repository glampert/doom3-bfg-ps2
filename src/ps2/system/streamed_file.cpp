// ================================================================================================
// File: streamed_file.cpp
// Brief: The streamed idFile: reads copy out of one window of the file, refilled where the read
// continues; a read of a window or more goes straight into the caller's buffer.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/streamed_file.h"

#include <idlib/precompiled.h>

#include <cstring>

namespace ps2::system::fs
{

namespace
{

class StreamedFile final : public idFile
{
public:
    StreamedFile(const char * name, int length) : m_name(name), m_length(length)
    {
    }

    const char * GetName() const override
    {
        return m_name.c_str();
    }

    const char * GetFullPath() const override
    {
        return m_name.c_str();
    }

    int Read(void * buffer, int len) override;

    int Length() const override
    {
        return m_length;
    }

    ID_TIME_T Timestamp() const override
    {
        return 0;
    }

    int Tell() const override
    {
        return m_position;
    }

    int Seek(long offset, fsOrigin_t origin) override;

private:
    idStrStatic<MAX_OSPATH> m_name;
    int m_length = 0;
    int m_position = 0;
    int m_windowStart = 0;
    int m_windowBytes = 0;
    byte m_window[kStreamedWindowBytes];
};

int StreamedFile::Read(void * buffer, int len)
{
    if (len <= 0 || m_position >= m_length)
    {
        return 0;
    }

    if (len > m_length - m_position)
    {
        len = m_length - m_position;
    }

    byte * const output = static_cast<byte *>(buffer);
    int copied = 0;

    while (copied < len)
    {
        const int windowEnd = m_windowStart + m_windowBytes;
        const int wanted = len - copied;

        if (m_position >= m_windowStart && m_position < windowEnd)
        {
            const int count = wanted < windowEnd - m_position ? wanted : windowEnd - m_position;
            std::memcpy(output + copied, m_window + (m_position - m_windowStart), static_cast<size_t>(count));
            copied += count;
            m_position += count;
            continue;
        }

        if (wanted >= kStreamedWindowBytes)
        {
            if (!fileSystem->ReadFileRange(m_name.c_str(), m_position, output + copied, wanted)) [[unlikely]]
            {
                break;
            }

            copied += wanted;
            m_position += wanted;
            continue;
        }

        const int fill = m_length - m_position < kStreamedWindowBytes ? m_length - m_position : kStreamedWindowBytes;
        if (!fileSystem->ReadFileRange(m_name.c_str(), m_position, m_window, fill)) [[unlikely]]
        {
            m_windowBytes = 0;
            break;
        }

        m_windowStart = m_position;
        m_windowBytes = fill;
    }

    return copied;
}

// idFile_InnerResource's conventions: 0 on success, -1 outside the file.
int StreamedFile::Seek(long offset, fsOrigin_t origin)
{
    long position = offset;

    if (origin == FS_SEEK_CUR)
    {
        position = m_position + offset;
    }
    else if (origin == FS_SEEK_END)
    {
        position = m_length - offset;
    }

    if (position < 0 || position > m_length)
    {
        return -1;
    }

    m_position = static_cast<int>(position);
    return 0;
}

} // namespace

PS2_COLD_FUNC idFile * OpenStreamed(const char * relativePath)
{
    const int length = fileSystem->GetFileLength(relativePath);

    if (length < 0)
    {
        return nullptr;
    }

    return new (TAG_TEMP) StreamedFile(relativePath, length);
}

} // namespace ps2::system::fs
