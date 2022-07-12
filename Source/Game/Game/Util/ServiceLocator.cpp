#include "ServiceLocator.h"

GameRenderer* ServiceLocator::_gameRenderer = nullptr;

void ServiceLocator::SetGameRenderer(GameRenderer* gameRenderer)
{
    assert(_gameRenderer == nullptr);
    _gameRenderer = gameRenderer;
}