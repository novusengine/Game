#pragma once

#include <Base/Types.h>
#include <Base/Memory/Bytebuffer.h>

#include <Jolt/Jolt.h>
#include <Jolt/Core/StreamIn.h>
#include <Jolt/Core/StreamOut.h>

class JoltStreamIn : public JPH::StreamIn
{
public:
    JoltStreamIn(std::shared_ptr<Bytebuffer>& buffer);
    ~JoltStreamIn();

    virtual void ReadBytes(void* outData, size_t inNumBytes) override;
    virtual bool IsEOF() const override;
    virtual bool IsFailed() const override;

private:
    bool _isEOF = false;
    std::shared_ptr<Bytebuffer> _buffer = nullptr;
};

class JoltStreamOut : public JPH::StreamOut
{
public:
    JoltStreamOut(std::shared_ptr<Bytebuffer>& buffer);
    ~JoltStreamOut();

    virtual void WriteBytes(const void* inData, size_t inNumBytes) override;
    virtual bool IsFailed() const override;

private:
    bool _didFail = false;
    std::shared_ptr<Bytebuffer> _buffer = nullptr;
};