#pragma once
#include <Base/Types.h>

#include <string>
#include <utility>

struct MessageInbound
{
public:
    enum class Type
    {
        Invalid,
        Print,
        Ping,
        DoString,
        AutomationRun,
        ReloadScripts,
        RefreshDB,
        Exit
    };

public:
    MessageInbound() { }
    MessageInbound(Type inType, std::string inData = "", std::string inRequestId = "")
        : type(inType), data(std::move(inData)), requestId(std::move(inRequestId)) { }

    Type type = Type::Invalid;
    std::string data = "";
    std::string requestId = "";
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
