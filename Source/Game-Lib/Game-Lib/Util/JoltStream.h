#pragma once

#include <Base/Types.h>
#include <Base/Memory/Bytebuffer.h>

#include <Jolt/Jolt.h>
#include <Jolt/Core/StreamIn.h>
#include <Jolt/Core/StreamOut.h>

class JoltStreamIn : public JPH::StreamIn
{
public:
    JoltStreamIn(Bytebuffer* buffer);
    ~JoltStreamIn();

    virtual void ReadBytes(void* outData, size_t inNumBytes) override;
    virtual bool IsEOF() const override;
    virtual bool IsFailed() const override;

private:
    bool _isEOF = false;
    Bytebuffer* _buffer = nullptr;
};

class JoltStreamOut : public JPH::StreamOut
{
public:
    JoltStreamOut(Bytebuffer* buffer);
    ~JoltStreamOut();

    virtual void WriteBytes(const void* inData, size_t inNumBytes) override;
    virtual bool IsFailed() const override;

private:
    bool _didFail = false;
    Bytebuffer* _buffer = nullptr;
};