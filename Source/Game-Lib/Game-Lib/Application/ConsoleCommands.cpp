#include "ConsoleCommands.h"

#include "Application.h"
#include "Message.h"

void ConsoleCommands::CommandPrint(Application& app, std::vector<std::string>& subCommands)
{
    if (subCommands.size() == 0)
        return;

    MessageInbound message(MessageInbound::Type::Print);
        
    for (u32 i = 0; i < subCommands.size(); i++)
    {
        if (i > 0)
        {
            message.data += " ";
        }

        message.data += subCommands[i];
    }

    app.PassMessage(message);
}

void ConsoleCommands::CommandPing(Application& app, std::vector<std::string>& subCommands)
{
    MessageInbound message(MessageInbound::Type::Ping);
    app.PassMessage(message);
}

void ConsoleCommands::CommandExit(Application& app, std::vector<std::string>& subCommands)
{
    MessageInbound message(MessageInbound::Type::Exit);
    app.PassMessage(message);
}

void ConsoleCommands::CommandDoString(Application& app, std::vector<std::string>& subCommands)
{
    if (subCommands.size() == 0)
        return;

    MessageInbound message(MessageInbound::Type::DoString);

    for (u32 i = 0; i < subCommands.size(); i++)
    {
        if (i > 0)
        {
            message.data += " ";
        }

        message.data += subCommands[i];
    }

    app.PassMessage(message);
}

void ConsoleCommands::CommandReloadScripts(Application& app, std::vector<std::string>& subCommands)
{
    MessageInbound message(MessageInbound::Type::ReloadScripts);
    app.PassMessage(message);
}
