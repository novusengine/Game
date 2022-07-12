#pragma once
#include <cassert>

class GameRenderer;

class ServiceLocator
{
public:
    static GameRenderer* GetGameRenderer()
    {
        assert(_gameRenderer != nullptr);
        return _gameRenderer;
    }
    static void SetGameRenderer(GameRenderer* gameRenderer);
    
private:
    ServiceLocator() { }
    static GameRenderer* _gameRenderer;

};