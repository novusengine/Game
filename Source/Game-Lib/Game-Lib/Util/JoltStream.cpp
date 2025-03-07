#include "JoltStream.h"

#include <Base/Util/DebugHandler.h>


JoltStreamIn::JoltStreamIn(Bytebuffer* buffer)
{
    _buffer = buffer;
    _isEOF = false;
}

JoltStreamIn::~JoltStreamIn()
{
    _buffer = nullptr;
}

void JoltStreamIn::ReadBytes(void* outData, size_t inNumBytes)
{
    if (!_buffer)
        return;

    if (!_buffer->GetBytes(outData, inNumBytes))
    {
        _isEOF = true;
    }
}

bool JoltStreamIn::IsEOF() const
{
    return _isEOF;
}

bool JoltStreamIn::IsFailed() const
{
    return false;
}

JoltStreamOut::JoltStreamOut(Bytebuffer* buffer)
{
    _buffer = buffer;
    _didFail = false;
}

JoltStreamOut::~JoltStreamOut()
{
    _buffer = nullptr;
}

void JoltStreamOut::WriteBytes(const void* inData, size_t inNumBytes)
{
    if (!_buffer)
        return;

    if (!_buffer->PutBytes(inData, inNumBytes))
    {
        _didFail = true;
        NC_LOG_ERROR("Failed to write bytes to JoltStreamOut");
    }
}

bool JoltStreamOut::IsFailed() const
{
    return _didFail;
}
