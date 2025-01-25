#pragma once
#include "BaseEditor.h"
#include "Game-Lib/ECS/Singletons/ClientDBCollection.h"

#include <Base/Types.h>

#include <FileFormat/Novus/ClientDB/ClientDB.h>

namespace Editor
{
    class CDBEditor : public BaseEditor
    {
    public:
        CDBEditor();

        virtual const char* GetName() override { return "CDB"; }

        virtual void DrawImGui() override;

    private:
        void ShowListView();
        void RenderCreateDatabaseWindow();
        void RenderEditDatabaseWindow();

        // Custom Editors

    private:
        u32 _selectedDBHash = 0;
        u32 _previousSelectedDBHash = 0;

        bool _showCreateDatabaseWindow = false;
        std::string _newDatabaseName = "";
        std::vector<ClientDB::FieldInfo> _newDatabaseFields;

        bool _showEditDatabaseWindow = false;
    };
}