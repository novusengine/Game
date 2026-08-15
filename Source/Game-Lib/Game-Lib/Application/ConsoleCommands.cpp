#include "ConsoleCommands.h"

#include "Application.h"
#include "Message.h"

#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Util/DebugHandler.h>
#include <Renderer/Renderer.h>

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

void ConsoleCommands::CommandAutomationRun(Application& app, std::vector<std::string>& subCommands)
{
    if (subCommands.size() != 2)
    {
        NC_LOG_ERROR("Usage: automation_run <request-id> Scripts/<path>.luau");
        return;
    }

    MessageInbound message(MessageInbound::Type::AutomationRun, subCommands[1], subCommands[0]);
    app.PassMessage(message);
}

void ConsoleCommands::CommandReloadScripts(Application& app, std::vector<std::string>& subCommands)
{
    MessageInbound message(MessageInbound::Type::ReloadScripts);
    app.PassMessage(message);
}

void ConsoleCommands::CommandRefreshDB(Application& app, std::vector<std::string>& subCommands)
{
    MessageInbound message(MessageInbound::Type::RefreshDB);
    app.PassMessage(message);
}

void ConsoleCommands::CommandDescriptorPoolStats(Application&, std::vector<std::string>&)
{
    const Renderer::DescriptorPoolStats stats = ServiceLocator::GetGameRenderer()->GetRenderer()->GetDescriptorPoolStats();
    NC_LOG_INFO("DESCRIPTOR_POOL sets={}/{}/{} uniform={}/{}/{} sampled={}/{}/{} storage_buffers={}/{}/{} storage_images={}/{}/{} samplers={}/{}/{}",
        stats.liveSets, stats.peakSets, stats.setCapacity,
        stats.liveUniformBuffers, stats.peakUniformBuffers, stats.uniformBufferCapacity,
        stats.liveSampledImages, stats.peakSampledImages, stats.sampledImageCapacity,
        stats.liveStorageBuffers, stats.peakStorageBuffers, stats.storageBufferCapacity,
        stats.liveStorageImages, stats.peakStorageImages, stats.storageImageCapacity,
        stats.liveSamplers, stats.peakSamplers, stats.samplerCapacity);
}
