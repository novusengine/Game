#pragma once
#include <Base/Types.h>

struct MessageInbound
{
public:
    enum class Type
    {
        Invalid,
        Print,
        Ping,
        DoString,
        ReloadScripts,
        Exit
    };

public:
    MessageInbound() { }
    MessageInbound(Type inType, std::string inData = "") : type(inType), data(inData) { }

    Type type = Type::Invalid;
    std::string data = "";
};

struct MessageOutbound
{
public:
    enum class Type
    {
        Invalid,
        Print,
        Pong,
        Exit
    };

public:
    MessageOutbound() { }
    MessageOutbound(Type inType, std::string inData = "") : type(inType), data(inData) { }

    Type type = Type::Invalid;
    std::string data = "";
};