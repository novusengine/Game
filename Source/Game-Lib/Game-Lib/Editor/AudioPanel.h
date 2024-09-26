#pragma once
#include "BaseEditor.h"

#include <Audio/AudioManager.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/ImGuiNotify.hpp>

namespace Editor
{
    class AudioPanel : public BaseEditor
    {
    public:
        AudioPanel();

        virtual const char* GetName() override
        {
            return "Audio Panel";
        }

        virtual void DrawImGui() override;

    private:
        virtual void OnModeUpdate(bool mode) override;

    private:
        AudioManager* _audioManager = nullptr;
        f32 _volume = 0.5f;
    };
}