#include "Game-Lib/Application/Application.h"
#include "Game-Lib/Application/Message.h"
#include "Game-Lib/Application/ConsoleCommandHandler.h"

#include <Base/Types.h>
#include <Base/Container/ConcurrentQueue.h>
#include <Base/Util/StringUtils.h>
#include <Base/Util/DebugHandler.h>

#include <quill/Backend.h>

#include <atomic>
#include <iostream>
#include <thread>

#if WIN32
#include <Windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#else
#include <poll.h>
#include <unistd.h>
#endif

i32 main()
{
#if WIN32
    timeBeginPeriod(1);
#endif

    quill::Backend::start();

    auto console_sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console_sink_1");
    quill::Logger* logger = quill::Frontend::create_or_get_logger("root", std::move(console_sink), "%(time:<16) LOG_%(log_level:<11) %(message)", "%H:%M:%S.%Qms", quill::Timezone::LocalTime, quill::ClockSourceType::System);
    
    Application app;
    app.Start(true);

    ConsoleCommandHandler commandHandler;
#if WIN32
    moodycamel::ConcurrentQueue<std::string> consoleCommands;
    std::atomic_bool consoleInputRunning = true;
    HANDLE consoleInput = GetStdHandle(STD_INPUT_HANDLE);
    std::thread consoleInputThread;
    if (consoleInput != nullptr && consoleInput != INVALID_HANDLE_VALUE)
    {
        consoleInputThread = std::thread([&consoleCommands, &consoleInputRunning]()
        {
            while (consoleInputRunning)
            {
                std::string command = StringUtils::GetLineFromCin();
                if (!consoleInputRunning || std::cin.fail())
                    break;

                consoleCommands.enqueue(std::move(command));
            }
        });
    }
#else
    pollfd consoleInput =
    {
        .fd = STDIN_FILENO,
        .events = POLLIN,
        .revents = 0
    };
    bool consoleInputOpen = true;
#endif

    while (true)
    {
        bool shouldExit = false;

        MessageOutbound message;
        while (app.TryGetMessageOutbound(message))
        {
            assert(message.type != MessageOutbound::Type::Invalid);

            switch (message.type)
            {
                case MessageOutbound::Type::Print:
                {
                    NC_LOG_INFO("{0}", message.data);
                    break;
                }

                case MessageOutbound::Type::Pong:
                {
                    NC_LOG_INFO("Application Thread -> Main Thread : Pong");
                    break;
                }

                case MessageOutbound::Type::Exit:
                {
                    shouldExit = true;
                    break;
                }

                default: break;
            }
        }

        if (shouldExit)
            break;

#if WIN32
        std::string command;
        while (consoleCommands.try_dequeue(command))
        {
            commandHandler.HandleCommand(app, command);
        }
#else
        if (consoleInputOpen && poll(&consoleInput, 1, 0) > 0)
        {
            if ((consoleInput.revents & POLLIN) != 0)
            {
                std::string command = StringUtils::GetLineFromCin();
                if (std::cin.fail())
                    consoleInputOpen = false;
                else
                    commandHandler.HandleCommand(app, command);
            }

            if ((consoleInput.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
                consoleInputOpen = false;
        }
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

#if WIN32
    consoleInputRunning = false;
    if (consoleInputThread.joinable())
        CancelSynchronousIo(consoleInputThread.native_handle());

    FreeConsole();

    if (consoleInputThread.joinable())
        consoleInputThread.join();

    timeEndPeriod(1);
#endif

    return 0;
}
