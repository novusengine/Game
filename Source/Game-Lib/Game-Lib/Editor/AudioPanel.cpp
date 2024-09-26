#include "AudioPanel.h"

#include <Game-Lib/Util/ServiceLocator.h>

#include <Base/Util/DebugHandler.h>
#include <Base/Util/StringUtils.h>


namespace Editor
{
    AudioPanel::AudioPanel()
        : BaseEditor(GetName(), true)
    {
        _audioManager = ServiceLocator::GetAudioManager();
    }

    void AudioPanel::DrawImGui()
    {
        if (ImGui::Begin(GetName()))
        {
            if (_audioManager->GetFileName() == "")
            {
                ImGui::Text("No Audio File Selected.");
            }
            else
            {
                std::string selectedText = "Selected File: " + _audioManager->GetFileName();
                ImGui::Text(selectedText.c_str());
            }

            if (ImGui::Button("Play", ImVec2(200.0f, 20.0f)))
            {
                _audioManager->PlaySoundFile(_volume, false, true);
            }
            if (ImGui::Button("Play as loop", ImVec2(200.0f, 20.0f)))
            {
                _audioManager->PlaySoundFile(_volume, true, true);
            }
            if (ImGui::Button("Stop looping", ImVec2(200.0f, 20.0f)))
            {
                _audioManager->EndLooping();
            }
            if (ImGui::Button("Pause", ImVec2(200.0f, 20.0f)))
            {
                _audioManager->PauseSoundFile();
            }
            if (ImGui::Button("Resume", ImVec2(200.0f, 20.0f)))
            {
                _audioManager->ResumeSoundFile();
            }
            if (ImGui::Button("Restart", ImVec2(200.0f, 20.0f)))
            {
                _audioManager->RestartSoundFile();
            }
            ImGui::SetNextItemWidth(200.0f);
            if (ImGui::SliderFloat("Volume", &_volume, 0.0f, 1.0f, "%.2f"))
            {
                _audioManager->SetVolume(_volume);
            }
        }
        ImGui::End();
    }

    void AudioPanel::OnModeUpdate(bool mode)
    {
        SetIsVisible(mode);
    }
}