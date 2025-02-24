#pragma once
#include "BaseEditor.h"

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
        void RenderEditRowsWindow();

    private:
        enum class EditMode : u8
        {
            None,
            CreateDatabase,
            EditDatabase,
            EditRows
        };

        struct EditFieldMapping
        {
        public:
            u32 originalFieldIndex = 0;
            u32 newFieldIndex = 0;
        };

        EditMode _editMode = EditMode::None;
        bool _editWindowOpen = false;

        u32 _selectedDBHash = 0;
        u32 _previousSelectedDBHash = 0;

        std::string _newDatabaseName = "";
        std::vector<ClientDB::FieldInfo> _newDatabaseFields;

        std::string _editOriginalDatabaseName = "";
        std::string _editDatabaseName = "";
        std::vector<ClientDB::FieldInfo> _editDatabaseFields;
        std::vector<EditFieldMapping> _editDatabaseFieldMapping;
        robin_hood::unordered_map<u32, u32> _editDatabaseFieldToMappingIndex;
    };
}