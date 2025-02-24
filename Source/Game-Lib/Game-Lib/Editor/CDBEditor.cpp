#include "CDBEditor.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/MapSingleton.h"
#include "Game-Lib/Gameplay/MapLoader.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystemPrivate.h>

#include <entt/entt.hpp>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <filesystem>
#include <string>

using namespace ClientDB;
using namespace ECS::Singletons;
using namespace ECS::Singletons::Database;
namespace fs = std::filesystem;

namespace Editor
{
    CDBEditor::CDBEditor()
        : BaseEditor(GetName(), true)
    {
    }

    void DrawDBItem(const ClientDBHash hash, const ClientDB::Data* db, const std::string& name, std::string& filter, u32& selectedDBHash)
    {
        bool hasFilter = filter.length() > 0;
        if (hasFilter)
        {
            std::string dbNameLowered = name;
            std::transform(dbNameLowered.begin(), dbNameLowered.end(), dbNameLowered.begin(), ::tolower);

            if (!StringUtils::Contains(dbNameLowered.c_str(), filter))
                return;
        }

        u32 dbHashAsInteger = static_cast<u32>(hash);
        ImGui::PushID(dbHashAsInteger);
        {
            ImGui::BeginGroup(); // Start group for the whole row
            {
                ImGui::Text("%s", name.c_str());
            }
            ImGui::EndGroup();

            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            max.x = ImGui::GetWindowContentRegionMax().x + ImGui::GetWindowPos().x; // Extend to full width

            bool isSelected = selectedDBHash == dbHashAsInteger;
            bool isHovered = ImGui::IsMouseHoveringRect(min, max);
            static u32 hoveredColor = ImGui::GetColorU32(ImVec4(0.7f, 0.8f, 1.0f, 0.3f));

            if (!isSelected && isHovered)
            {
                ImGui::GetWindowDrawList()->AddRectFilled(min, max, hoveredColor);

                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    selectedDBHash = dbHashAsInteger;
            }

            if (isSelected)
            {
                ImGui::GetWindowDrawList()->AddRectFilled(min, max, hoveredColor);
            }
        }
        ImGui::PopID();
    }
    
    void CDBEditor::ShowListView()
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        entt::registry& registry = *registries->dbRegistry;

        entt::registry::context& ctx = registry.ctx();

        auto& clientDBSingleton = ctx.get<ClientDBSingleton>();
        auto* mapStorage = clientDBSingleton.Get(ClientDBHash::Map);

        // Begin a list box
        ImGui::Text("Select a CDB (Optionally use the filter below)");

        u32 TotalNumDBs = clientDBSingleton.Count();
        u32 numVisibleItems = 10; // Define number of visible items
        f32 itemHeight = 18.0f; // Estimate height of one item

        // Set the size of the ListBox
        ImGui::SetNextItemWidth(-1); // Full width

        static std::string currentFilter;
        static bool hasFilter = false;
        ImGui::InputText("Filter", &currentFilter);

        bool hadFilterLastFrame = hasFilter;
        hasFilter = currentFilter.length() > 0;
        if (hasFilter)
        {
            std::transform(currentFilter.begin(), currentFilter.end(), currentFilter.begin(), ::tolower);
        }

        ImGui::SetNextItemWidth(-1); // Full width
        if (ImGui::BeginListBox("##listboxwithicons", vec2(-1.0f, itemHeight * numVisibleItems)))
        {
            static u32 currentMapIDSelected = 0;
            static u32 popupMapID = 0;

            if (TotalNumDBs)
            {
                if (hasFilter)
                {
                    clientDBSingleton.Each([this, &clientDBSingleton](ClientDBHash hash, const ClientDB::Data* db)
                    {
                        const std::string& dbName = clientDBSingleton.GetDBName(hash);
                        DrawDBItem(hash, db, dbName, currentFilter, _selectedDBHash);
                    });
                }
                else
                {
                    ImGuiListClipper clipper;
                    clipper.Begin(TotalNumDBs, itemHeight);

                    while (clipper.Step())
                    {
                        u32 start = clipper.DisplayStart;
                        u32 count = (clipper.DisplayEnd - clipper.DisplayStart) - 1;
                        
                        clientDBSingleton.EachInRange(start, count, [this, &clientDBSingleton](ClientDBHash hash, const ClientDB::Data* db)
                        {
                            const std::string& dbName = clientDBSingleton.GetDBName(hash);
                            DrawDBItem(hash, db, dbName, currentFilter, _selectedDBHash);
                        });
                    }
                }
            }

            ImGui::EndListBox();
        }

        f32 buttonWidth = 150.0f;

        if (ImGui::Button("Create Database", ImVec2(buttonWidth, 0.0f)))
        {
            _editMode = EditMode::CreateDatabase;

            _newDatabaseName = "";
            _newDatabaseFields.clear();
        }

        if (_selectedDBHash != 0)
        {
            ImGui::SameLine();

            if (ImGui::Button("Delete Database", ImVec2(buttonWidth, 0.0f)))
            {
                ClientDBHash hash = static_cast<ClientDBHash>(_selectedDBHash);
                clientDBSingleton.Remove(hash);

                _selectedDBHash = 0;
                _previousSelectedDBHash = 0;
                _editMode = EditMode::None;
            }

            if (ImGui::Button("Edit Header", ImVec2(buttonWidth, 0.0f)))
            {
                ClientDBHash hash = static_cast<ClientDBHash>(_selectedDBHash);
                const std::string& dbName = clientDBSingleton.GetDBName(hash);

                if (_editMode != EditMode::EditDatabase || _editDatabaseName != dbName)
                {
                    auto* db = clientDBSingleton.Get(hash);

                    _editOriginalDatabaseName = dbName;
                    _editDatabaseName = dbName;
                    _editDatabaseFields = db->GetFields();
                    _editDatabaseFieldMapping.clear();
                    _editDatabaseFieldToMappingIndex.clear();

                    u32 numFields = static_cast<u32>(_editDatabaseFields.size());
                    _editDatabaseFieldMapping.resize(numFields);
                    _editDatabaseFieldToMappingIndex.reserve(numFields);
                    for (u32 i = 0; i < numFields; i++)
                    {
                        EditFieldMapping& fieldMapping = _editDatabaseFieldMapping[i];
                        fieldMapping.originalFieldIndex = i;
                        fieldMapping.newFieldIndex = i;

                        _editDatabaseFieldToMappingIndex[i] = i;
                    }

                    _editMode = EditMode::EditDatabase;
                }
            }

            ImGui::SameLine();

            if (ImGui::Button("Edit Rows", ImVec2(buttonWidth, 0.0f)))
            {
                _editMode = EditMode::EditRows;
            }
        }

        _editWindowOpen = _editMode != EditMode::None;

        if (_editMode == EditMode::CreateDatabase)
        {
            RenderCreateDatabaseWindow();
        }
        else if (_editMode == EditMode::EditDatabase)
        {
            RenderEditDatabaseWindow();
        }
        else if (_editMode == EditMode::EditRows)
        {
            RenderEditRowsWindow();
        }

        if (!_editWindowOpen && _editMode != EditMode::None)
            _editMode = EditMode::None;
    }
    
    bool RenderFieldTypeSelector(const char* label, FieldType& currentType)
    {
        i32 currentTypeIndex = static_cast<i32>(currentType);
        if (ImGui::Combo(label, &currentTypeIndex, ClientDB::FieldTypeNames, IM_ARRAYSIZE(ClientDB::FieldTypeNames)))
        {
            currentType = static_cast<FieldType>(currentTypeIndex);
            return true;
        }

        return false;
    }

    void CDBEditor::RenderCreateDatabaseWindow()
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        entt::registry& registry = *registries->dbRegistry;

        entt::registry::context& ctx = registry.ctx();

        auto& clientDBSingleton = ctx.get<ClientDBSingleton>();

        static i32 draggedFieldIndex = -1;
        static const f32 rowHeight = 22.5f; // Fixed row height
        static std::string errorMessage = ""; // Error message for validation feedback

        if (ImGui::Begin("Create Database", &_editWindowOpen))
        {
            ImGui::Text("Database Name");
            ImGui::InputText("##Database Name", &_newDatabaseName);

            if (ImGui::Button("Add Field"))
            {
                _newDatabaseFields.push_back({ "", FieldType::I32 });
            }

            ImGui::Separator();

            u32 numFields = static_cast<u32>(_newDatabaseFields.size());
            for (u32 i = 0; i < numFields; ++i)
            {
                ImGui::PushID(i);

                // Row Start// Align row height
                f32 startY = ImGui::GetCursorPosY();
                ImGui::SetCursorPosY(startY + (rowHeight - ImGui::GetTextLineHeightWithSpacing()) * 0.5f);

                // Create a full-row drag and drop zone
                ImGui::Selectable("##DragArea", false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap, ImVec2(0.0f, rowHeight));

                // Drag Source
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                {
                    draggedFieldIndex = i;
                    ImGui::SetDragDropPayload("FIELD_REORDER", &draggedFieldIndex, sizeof(i32));
                    ImGui::Text("Dragging: %s", _newDatabaseFields[i].name.c_str());
                    ImGui::EndDragDropSource();
                }

                // Drop Target
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FIELD_REORDER"))
                    {
                        i32 sourceIndex = *reinterpret_cast<const i32*>(payload->Data);
                        if (sourceIndex != i)
                        {
                            std::swap(_newDatabaseFields[sourceIndex], _newDatabaseFields[i]);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                // Adjust back to render inline widgets
                ImGui::SetCursorPosY(startY);

                // Render field index
                ImGui::Text("%d", i + 1);
                ImGui::SameLine(0.0f, 8.0f);

                // Make field name and type draggable
                ImGui::PushItemWidth(150.0f);
                if (ImGui::InputText("##FieldName", &_newDatabaseFields[i].name))
                {
                    // Editing logic if necessary
                }
                ImGui::PopItemWidth();
                ImGui::SameLine();

                ImGui::PushItemWidth(100.0f);
                RenderFieldTypeSelector("##FieldType", _newDatabaseFields[i].type);
                ImGui::PopItemWidth();
                ImGui::SameLine();

                // Remove Button
                if (ImGui::Button("Remove"))
                {
                    _newDatabaseFields.erase(_newDatabaseFields.begin() + i);
                    --i; // Adjust for removed element
                    --numFields;
                    ImGui::PopID();
                    continue;
                }

                ImGui::PopID();
            }

            ImGui::Separator();

            // Create button with validation
            if (ImGui::Button("Create"))
            {
                errorMessage = ""; // Clear previous error message

                // Validate database name
                if (_newDatabaseName.empty())
                {
                    errorMessage = "Database name cannot be empty.\n";
                }
                else
                {
                    if (!std::isalpha(_newDatabaseName[0]))
                    {
                        errorMessage = "Database name must start with a letter.\n";
                    }
                    else
                    {
                        if (!std::all_of(_newDatabaseName.begin(), _newDatabaseName.end(), [](char c) { return std::isalnum(c); }))
                        {
                            errorMessage = "Database name can only contain alphanumeric characters.\n";
                        }
                    }
                }

                numFields = static_cast<u32>(_newDatabaseFields.size());
                if (numFields == 0)
                {
                    errorMessage += "Database must have at least one field";
                }
                else
                {
                    // Validate fields
                    for (const auto& field : _newDatabaseFields)
                    {
                        if (field.name.empty())
                        {
                            errorMessage += "All fields must have a name.";
                            break;
                        }

                        // Ensure names are alphanumeric and do not start with a number
                        if (!std::isalpha(field.name[0]))
                        {
                            errorMessage += "Field names must start with a letter.";
                            break;
                        }

                        if (!std::all_of(field.name.begin(), field.name.end(), [](char c) { return std::isalnum(c); }))
                        {
                            errorMessage += "Field names can only contain alphanumeric characters.";
                            break;
                        }
                    }
                }

                // If validation passes, proceed
                if (errorMessage.empty())
                {
                    u32 newDatabaseNameHash = StringUtils::fnv1a_32(_newDatabaseName.c_str(), _newDatabaseName.size());
                    ClientDBHash dbHash = static_cast<ClientDBHash>(newDatabaseNameHash);

                    if (clientDBSingleton.Has(dbHash))
                    {
                        errorMessage = "A Database with that name already exists.";
                    }
                    else
                    {
                        clientDBSingleton.Register(dbHash, _newDatabaseName);
                        
                        auto* newStorage = clientDBSingleton.Get(dbHash);
                        newStorage->Initialize(_newDatabaseFields);
                        newStorage->MarkDirty();

                        _editMode = EditMode::None;
                        draggedFieldIndex = -1;
                        errorMessage = "";
                    }
                }
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel"))
            {
                _editMode = EditMode::None;
                errorMessage = "";
                draggedFieldIndex = -1;
            }

            // Display error message, if any
            if (!errorMessage.empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s", errorMessage.c_str());
            }
        }
        ImGui::End();
    }

    void CDBEditor::RenderEditDatabaseWindow()
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        entt::registry& registry = *registries->dbRegistry;

        entt::registry::context& ctx = registry.ctx();

        auto& clientDBSingleton = ctx.get<ClientDBSingleton>();

        static i32 draggedFieldIndex = -1;
        static const f32 rowHeight = 22.5f; // Fixed row height
        static std::string errorMessage = ""; // Error message for validation feedback

        if (ImGui::Begin("Edit Database", &_editWindowOpen))
        {
            ImGui::Text("Database Name");
            ImGui::InputText("##Database Name", &_editDatabaseName);

            u32 numFields = static_cast<u32>(_editDatabaseFields.size());
            if (ImGui::Button("Add Field"))
            {
                _editDatabaseFields.push_back({ "", FieldType::I32 });
            }

            ImGui::Separator();

            for (u32 i = 0; i < numFields; ++i)
            {
                ImGui::PushID(i);

                // Row Start// Align row height
                f32 startY = ImGui::GetCursorPosY();
                ImGui::SetCursorPosY(startY + (rowHeight - ImGui::GetTextLineHeightWithSpacing()) * 0.5f);

                // Create a full-row drag and drop zone
                ImGui::Selectable("##DragArea", false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap, ImVec2(0.0f, rowHeight));

                // Drag Source
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                {
                    draggedFieldIndex = i;
                    ImGui::SetDragDropPayload("FIELD_REORDER", &draggedFieldIndex, sizeof(i32));
                    ImGui::Text("Dragging: %s", _editDatabaseFields[i].name.c_str());
                    ImGui::EndDragDropSource();
                }

                // Drop Target
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FIELD_REORDER"))
                    {
                        i32 sourceIndex = *reinterpret_cast<const i32*>(payload->Data);
                        if (sourceIndex != i)
                        {
                            std::swap(_editDatabaseFields[sourceIndex], _editDatabaseFields[i]);

                            // Handle unmapped fields or additions
                            bool sourceMapped = _editDatabaseFieldToMappingIndex.contains(sourceIndex);
                            bool targetMapped = _editDatabaseFieldToMappingIndex.contains(i);

                            if (sourceMapped && targetMapped)
                            {
                                // Regular mapped field swap
                                u32 sourceFieldIndex = _editDatabaseFieldToMappingIndex[sourceIndex];
                                u32 targetFieldIndex = _editDatabaseFieldToMappingIndex[i];

                                std::swap(
                                    _editDatabaseFieldMapping[sourceFieldIndex].newFieldIndex,
                                    _editDatabaseFieldMapping[targetFieldIndex].newFieldIndex
                                );

                                // Update the mapping indices
                                std::swap(_editDatabaseFieldToMappingIndex[sourceIndex], _editDatabaseFieldToMappingIndex[i]);
                            }
                            else if (sourceMapped)
                            {
                                // Move source field to an unmapped slot
                                u32 sourceFieldIndex = _editDatabaseFieldToMappingIndex[sourceIndex];
                                _editDatabaseFieldMapping[sourceFieldIndex].newFieldIndex = i;

                                // Update mapping index for source
                                _editDatabaseFieldToMappingIndex[i] = sourceFieldIndex;

                                // Remove source from mapping
                                _editDatabaseFieldToMappingIndex.erase(sourceIndex);
                            }
                            else if (targetMapped)
                            {
                                // Move target field to an unmapped slot
                                u32 targetFieldIndex = _editDatabaseFieldToMappingIndex[i];
                                _editDatabaseFieldMapping[targetFieldIndex].newFieldIndex = sourceIndex;

                                // Update mapping index for target
                                _editDatabaseFieldToMappingIndex[sourceIndex] = targetFieldIndex;

                                // Remove target from mapping
                                _editDatabaseFieldToMappingIndex.erase(i);
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                // Adjust back to render inline widgets
                ImGui::SetCursorPosY(startY);

                // Render field index
                ImGui::Text("%d", i + 1);
                ImGui::SameLine(0.0f, 8.0f);

                // Make field name and type draggable
                ImGui::PushItemWidth(150.0f);
                if (ImGui::InputText("##FieldName", &_editDatabaseFields[i].name))
                {
                    // Editing logic if necessary
                }
                ImGui::PopItemWidth();
                ImGui::SameLine();

                ImGui::PushItemWidth(100.0f);
                RenderFieldTypeSelector("##FieldType", _editDatabaseFields[i].type);
                ImGui::PopItemWidth();
                ImGui::SameLine();

                // Remove Button
                if (ImGui::Button("Remove"))
                {
                    bool isMapped = _editDatabaseFieldToMappingIndex.contains(i);
                    if (isMapped)
                    {
                        // Get the mapping index for the field
                        u32 mappedFieldIndex = _editDatabaseFieldToMappingIndex[i];

                        // Remove from the mapping array
                        _editDatabaseFieldMapping.erase(_editDatabaseFieldMapping.begin() + mappedFieldIndex);

                        // Remove the mapping entry for the current field
                        _editDatabaseFieldToMappingIndex.erase(i);
                    }

                    _editDatabaseFields.erase(_editDatabaseFields.begin() + i);

                    // Update all keys in _editDatabaseFieldToMappingIndex that reference fields after the removed field
                    robin_hood::unordered_map<u32, u32> updatedMappingIndex;
                    for (auto& [fieldIndex, mappingIndex] : _editDatabaseFieldToMappingIndex)
                    {
                        u32 keyIndex = fieldIndex > i ? fieldIndex - 1 : fieldIndex;
                        u32 valueIndex = isMapped && mappingIndex > i ? mappingIndex - 1 : mappingIndex;

                        updatedMappingIndex[keyIndex] = valueIndex;
                    }
                    _editDatabaseFieldToMappingIndex = std::move(updatedMappingIndex);

                    // Update newFieldIndex for all fields in _editDatabaseFieldMapping
                    for (EditFieldMapping& fieldMapping : _editDatabaseFieldMapping)
                    {
                        if (fieldMapping.newFieldIndex > i)
                        {
                            --fieldMapping.newFieldIndex;
                        }
                    }

                    --i; // Adjust for removed element
                    --numFields;
                    ImGui::PopID();
                    continue;
                }

                ImGui::PopID();
            }

            ImGui::Separator();

            // Create button with validation
            if (ImGui::Button("Save"))
            {
                errorMessage = ""; // Clear previous error message

                // Validate database name
                if (_editDatabaseName.empty())
                {
                    errorMessage = "Database name cannot be empty.\n";
                }
                else
                {
                    if (!std::isalpha(_editDatabaseName[0]))
                    {
                        errorMessage = "Database name must start with a letter.\n";
                    }
                    else
                    {
                        if (!std::all_of(_editDatabaseName.begin(), _editDatabaseName.end(), [](char c) { return std::isalnum(c); }))
                        {
                            errorMessage = "Database name can only contain alphanumeric characters.\n";
                        }
                    }
                }

                numFields = static_cast<u32>(_editDatabaseFields.size());
                if (numFields == 0)
                {
                    errorMessage += "Database must have at least one field";
                }
                else
                {
                    // Validate fields
                    for (const auto& field : _editDatabaseFields)
                    {
                        if (field.name.empty())
                        {
                            errorMessage += "All fields must have a name.";
                            break;
                        }

                        // Ensure names are alphanumeric and do not start with a number
                        if (!std::isalpha(field.name[0]))
                        {
                            errorMessage += "Field names must start with a letter.";
                            break;
                        }

                        if (!std::all_of(field.name.begin(), field.name.end(), [](char c) { return std::isalnum(c); }))
                        {
                            errorMessage += "Field names can only contain alphanumeric characters.";
                            break;
                        }
                    }
                }

                // If validation passes, proceed
                if (errorMessage.empty())
                {
                    bool preservedName = _editDatabaseName == _editOriginalDatabaseName;

                    u32 newDatabaseNameHash = StringUtils::fnv1a_32(_editDatabaseName.c_str(), _editDatabaseName.size());
                    ClientDBHash dbHash = static_cast<ClientDBHash>(newDatabaseNameHash);

                    if (!preservedName && clientDBSingleton.Has(dbHash))
                    {
                        errorMessage = "A Database with that name already exists.";
                    }
                    else
                    {
                        u32 originalDatabaseNameHash = StringUtils::fnv1a_32(_editOriginalDatabaseName.c_str(), _editOriginalDatabaseName.size());
                        ClientDBHash originalDBHash = static_cast<ClientDBHash>(originalDatabaseNameHash);

                        struct FieldMapping
                        {
                        public:
                            u16 oldFieldInfoIndex;
                            u16 newFieldInfoIndex;

                            u32 oldOffset;
                            u32 newOffset;
                        };

                        auto* oldStorage = clientDBSingleton.Get(originalDBHash);
                        auto* newStorage = new ClientDB::Data();
                        newStorage->Initialize(_editDatabaseFields);

                        u32 numOldRows = oldStorage->GetNumRows();
                        u32 numFieldMappings = static_cast<u32>(_editDatabaseFieldMapping.size());
                        const auto& oldFields = oldStorage->GetFields();
                        const auto& oldFieldOffsets = oldStorage->GetFieldOffsets();

                        u32 numNewFields = newStorage->GetNumFields();
                        const auto& newFields = newStorage->GetFields();
                        const auto& newFieldOffsets = newStorage->GetFieldOffsets();

                        std::vector<FieldMapping> fieldMapping;
                        fieldMapping.reserve(numFieldMappings);

                        for (u32 i = 0; i < numFieldMappings; i++)
                        {
                            const EditFieldMapping& editFieldMapping = _editDatabaseFieldMapping[i];

                            const FieldInfo& oldFieldInfo = oldFields[editFieldMapping.originalFieldIndex];
                            const FieldInfo& newFieldInfo = newFields[editFieldMapping.newFieldIndex];

                            bool canMakeMapping = false;

                            // Check if we have a way to map old -> new based on FieldType (Integer -> Integer, Float -> Float, StringRef -> StringRef)
                            canMakeMapping |= oldFieldInfo.type >= FieldType::I8 && oldFieldInfo.type <= FieldType::F64 && newFieldInfo.type >= FieldType::I8 && newFieldInfo.type <= FieldType::F64;
                            canMakeMapping |= oldFieldInfo.type == FieldType::StringRef && newFieldInfo.type == FieldType::StringRef;

                            if (canMakeMapping)
                            {
                                FieldMapping& integerMapping = fieldMapping.emplace_back();
                                integerMapping.oldFieldInfoIndex = editFieldMapping.originalFieldIndex;
                                integerMapping.newFieldInfoIndex = editFieldMapping.newFieldIndex;
                                integerMapping.oldOffset = oldFieldOffsets[editFieldMapping.originalFieldIndex];
                                integerMapping.newOffset = newFieldOffsets[editFieldMapping.newFieldIndex];
                            }
                        }

                        u32 numMappings = static_cast<u32>(fieldMapping.size());
                        if (numMappings)
                        {
                            // Ensure Old Storage is Compact, so we can smoothly copy over
                            oldStorage->Compact();
                            
                            // Copy the Old Storage's StringTable
                            newStorage->GetStringTable().CopyFrom(oldStorage->GetStringTable());

                            auto& oldIDList = oldStorage->GetIDList();
                            auto& newIDList = newStorage->GetIDList();
                            for (auto& idEntry : oldIDList)
                            {
                                bool didOverride = false;
                                newStorage->Clone(0u, idEntry.id, didOverride);
                            }

                            auto& oldData = oldStorage->GetData();
                            auto& newData = newStorage->GetData();

                            u8 oldFieldData[8] = { };

                            for (u32 i = 0; i < numOldRows; i++)
                            {
                                u32 oldRowOffset = oldIDList[i].index;
                                u32 newRowOffset = newIDList[i].index;

                                for (const auto& fieldToMap : fieldMapping)
                                {
                                    const auto& oldField = oldFields[fieldToMap.oldFieldInfoIndex];
                                    const auto& newField = newFields[fieldToMap.newFieldInfoIndex];
                                    
                                    u32 oldFieldOffset = oldFieldOffsets[fieldToMap.oldFieldInfoIndex];
                                    u32 newFieldOffset = newFieldOffsets[fieldToMap.newFieldInfoIndex];

                                    u32 oldRowFieldOffset = oldRowOffset + oldFieldOffset;
                                    u32 newRowFieldOffset = newRowOffset + newFieldOffset;

                                    // Promote all Integers/StringRef to I64, all floats to F64
                                    switch (oldField.type)
                                    {
                                        case FieldType::I8:
                                        {
                                            i8 val = oldData[oldRowFieldOffset];
                                            *reinterpret_cast<i64*>(&oldFieldData[0]) = static_cast<i64>(val);
                                            break;
                                        }
                                        case FieldType::I16:
                                        {
                                            i16 val = *reinterpret_cast<i16*>(&oldData[oldRowFieldOffset]);
                                            *reinterpret_cast<i64*>(&oldFieldData[0]) = static_cast<i64>(val);
                                            break;
                                        }
                                        case FieldType::I32:
                                        {
                                            i32 val = *reinterpret_cast<i32*>(&oldData[oldRowFieldOffset]);
                                            *reinterpret_cast<i64*>(&oldFieldData[0]) = static_cast<i64>(val);
                                            break;
                                        }
                                        case FieldType::I64:
                                        {
                                            i64 val = *reinterpret_cast<i64*>(&oldData[oldRowFieldOffset]);
                                            *reinterpret_cast<i64*>(&oldFieldData[0]) = val;
                                            break;
                                        }

                                        case FieldType::F32:
                                        {
                                            f32 val = *reinterpret_cast<f32*>(&oldData[oldRowFieldOffset]);
                                            *reinterpret_cast<f64*>(&oldFieldData[0]) = static_cast<f64>(val);
                                            break;
                                        }
                                        case FieldType::F64:
                                        {
                                            f64 val = *reinterpret_cast<f64*>(&oldData[oldRowFieldOffset]);
                                            *reinterpret_cast<f64*>(&oldFieldData[0]) = val;
                                            break;
                                        }

                                        case FieldType::StringRef:
                                        {
                                            i32 val = *reinterpret_cast<i32*>(&oldData[oldRowFieldOffset]);
                                            *reinterpret_cast<i64*>(&oldFieldData[0]) = static_cast<i64>(val);
                                        }
                                    }

                                    // Check if we need to transform oldFieldData from Integer Bits -> Floating Bits or Floating Bits -> Integer Bits
                                    if (oldField.type >= FieldType::I8 && oldField.type <= FieldType::I64)
                                    {
                                        if (newField.type >= FieldType::F32 && newField.type <= FieldType::F64)
                                        {
                                            i64 val = *reinterpret_cast<i64*>(&oldFieldData[0]);
                                            *reinterpret_cast<f64*>(&oldFieldData[0]) = static_cast<f64>(val);
                                        }
                                    }
                                    else if (oldField.type >= FieldType::F32 && oldField.type <= FieldType::F64)
                                    {
                                        if (newField.type >= FieldType::I8 && newField.type <= FieldType::I64)
                                        {
                                            f64 val = *reinterpret_cast<f64*>(&oldFieldData[0]);
                                            *reinterpret_cast<i64*>(&oldFieldData[0]) = static_cast<i64>(val);
                                        }
                                    }

                                    // Set the row's field based on the type of the new field (oldFieldData is assured to either be I64 or F64)
                                    switch (newField.type)
                                    {
                                        case FieldType::I8:
                                        {
                                            i64 val = *reinterpret_cast<i64*>(&oldFieldData[0]);
                                            *reinterpret_cast<i8*>(&newData[newRowFieldOffset]) = static_cast<i8>(val);
                                            break;
                                        }
                                        case FieldType::I16:
                                        {
                                            i64 val = *reinterpret_cast<i64*>(&oldFieldData[0]);
                                            *reinterpret_cast<i16*>(&newData[newRowFieldOffset]) = static_cast<i16>(val);
                                            break;
                                        }
                                        case FieldType::I32:
                                        case FieldType::StringRef:
                                        {
                                            i64 val = *reinterpret_cast<i64*>(&oldFieldData[0]);
                                            *reinterpret_cast<i32*>(&newData[newRowFieldOffset]) = static_cast<i32>(val);
                                            break;
                                        }
                                        case FieldType::I64:
                                        {
                                            i64 val = *reinterpret_cast<i64*>(&oldFieldData[0]);
                                            *reinterpret_cast<i64*>(&newData[newRowFieldOffset]) = val;
                                            break;
                                        }

                                        case FieldType::F32:
                                        {
                                            f64 val = *reinterpret_cast<f64*>(&oldFieldData[0]);
                                            *reinterpret_cast<f32*>(&newData[newRowFieldOffset]) = static_cast<f32>(val);
                                            break;
                                        }
                                        case FieldType::F64:
                                        {
                                            f64 val = *reinterpret_cast<f64*>(&oldFieldData[0]);
                                            *reinterpret_cast<f64*>(&newData[newRowFieldOffset]) = val;
                                            break;
                                        }
                                    }
                                }
                            }

                            newStorage->Compact();
                        }

                        if (!preservedName)
                        {
                            clientDBSingleton.Remove(originalDBHash);
                            clientDBSingleton.Register(dbHash, _editDatabaseName);
                        }

                        clientDBSingleton.Replace(dbHash, newStorage);
                        newStorage->MarkDirty();

                        _editMode = EditMode::None;
                        draggedFieldIndex = -1;
                        errorMessage = "";
                    }
                }
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel"))
            {
                _editMode = EditMode::None;
                errorMessage = "";
                draggedFieldIndex = -1;
            }

            // Display error message, if any
            if (!errorMessage.empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s", errorMessage.c_str());
            }
        }
        ImGui::End();
    }

    void CDBEditor::RenderEditRowsWindow()
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        entt::registry& registry = *registries->dbRegistry;

        entt::registry::context& ctx = registry.ctx();

        auto& clientDBSingleton = ctx.get<ClientDBSingleton>();

        if (ImGui::Begin("Edit Rows", &_editWindowOpen))
        {
            ClientDBHash prevHash = static_cast<ClientDBHash>(_previousSelectedDBHash);
            ClientDBHash hash = static_cast<ClientDBHash>(_selectedDBHash);

            ClientDB::Data* db = clientDBSingleton.Get(hash);
            const std::string& databaseName = clientDBSingleton.GetDBName(hash);

            ImGui::Text("Database Name: %s", databaseName.c_str());
            ImGui::Separator();

            const std::vector<FieldInfo>& fields = db->GetFields();
            const std::vector<u32>& fieldOffsets = db->GetFieldOffsets();
            const std::vector<IDListEntry>& idList = db->GetIDList();
            std::vector<u8>& data = db->GetData();

            u32 numFields = static_cast<u32>(db->GetNumFields());
            u32 numColumns = numFields + 1;

            constexpr f32 COLUMN_MIN_WIDTH = 30.0f; // Define a constant for column width
            constexpr f32 COLUMN_MAX_WIDTH = 1000.0f; // Define a constant for column width
            f32 tableHeight = ImGui::GetContentRegionAvail().y - 30.0f; // Use remaining height
            ImVec2 outerSize = ImVec2(0.0f, tableHeight);        // Constrain width and height

            static std::unordered_map<size_t, f32> columnWidthCache; // Cache for column widths

            bool recalculateColumnWidth = prevHash != hash;
            if (recalculateColumnWidth)
                {
                    columnWidthCache.clear();

                    // Compute initial column widths
                    columnWidthCache[0] = 70.0f; // ID column
                    for (size_t i = 0; i < fields.size(); ++i)
                    {
                        f32 width = COLUMN_MIN_WIDTH;
                        const FieldInfo& field = fields[i];

                        f32 columnNameWidth = ImGui::CalcTextSize(field.name.c_str()).x + (ImGui::GetStyle().FramePadding.x * 2) + 5.0f;
                        width = glm::max(width, columnNameWidth);

                        // Scan all rows to determine the max width for this field
                        for (const IDListEntry& idEntry : idList)
                        {
                            if (!db->Has(idEntry.id))
                                continue;

                            u32 rowOffset = idEntry.index;
                            u32 fieldOffset = fieldOffsets[i];

                            std::string content;
                            switch (field.type)
                            {
                                case FieldType::I8:
                                    content = std::to_string(*reinterpret_cast<i8*>(&data[rowOffset + fieldOffset]));
                                    break;
                                case FieldType::I16:
                                    content = std::to_string(*reinterpret_cast<i16*>(&data[rowOffset + fieldOffset]));
                                    break;
                                case FieldType::I32:
                                    content = std::to_string(*reinterpret_cast<i32*>(&data[rowOffset + fieldOffset]));
                                    break;
                                case FieldType::I64:
                                    content = std::to_string(*reinterpret_cast<i64*>(&data[rowOffset + fieldOffset]));
                                    break;
                                case FieldType::F32:
                                    content = std::to_string(*reinterpret_cast<f32*>(&data[rowOffset + fieldOffset]));
                                    break;
                                case FieldType::F64:
                                    content = std::to_string(*reinterpret_cast<f64*>(&data[rowOffset + fieldOffset]));
                                    break;
                                case FieldType::StringRef:
                                    content = db->GetString(*reinterpret_cast<i32*>(&data[rowOffset + fieldOffset]));
                                    break;
                                default:
                                    content = "Unsupported";
                                    break;
                            }

                            f32 textWidth = COLUMN_MIN_WIDTH + ImGui::CalcTextSize(content.c_str()).x + (ImGui::GetStyle().FramePadding.x * 2) + 5.0f;
                            width = glm::max(width, textWidth);
                        }

                        columnWidthCache[i + 1] = width; // Cache column width
                    }
                }

            f32 totalWidth = 0.0f;
            for (const auto& [index, width] : columnWidthCache)
            {
                totalWidth += width;
            }

            // Ensure innerWidth is at least the available space to avoid column truncation
            f32 scrollbarWidth = ImGui::GetStyle().ScrollbarSize; // Horizontal scrollbar size
            f32 padding = ((ImGui::GetStyle().CellPadding.x * 2.0f) + 0.75f) * numColumns; // Padding across all columns
            f32 innerWidth = std::max(totalWidth + scrollbarWidth + padding, ImGui::GetContentRegionAvail().x);

            bool markAsDirty = false;
            if (ImGui::Button("Add Row"))
            {
                u32 newRowID = 0;
                markAsDirty |= db->Copy(0, newRowID);
            }

            ImGui::SameLine();

            static bool showRemoveRowPopup = false;
            static i32 removeRowID = 0;
            if (ImGui::Button("Remove a Row"))
            {
                showRemoveRowPopup = true;
            }

            if (showRemoveRowPopup)
            {
                ImGui::OpenPopup("Remove Row##CDBRowEditor");
            }

            if (ImGui::BeginPopupModal("Remove Row##CDBRowEditor", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                static std::string errorMsg = "";

                ImGui::Text("Enter the ID of the row to remove:");
                ImGui::InputInt("##RemoveARowIDInput", &removeRowID);

                if (!errorMsg.empty())
                {
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "%s", errorMsg.c_str());
                }

                if (ImGui::Button("Confirm"))
                {
                    bool hasRow = db->Has(removeRowID);

                    if (hasRow)
                    {
                        if (db->Remove(removeRowID))
                        {
                            markAsDirty = true;

                            errorMsg = "";
                            showRemoveRowPopup = false;
                            ImGui::CloseCurrentPopup();
                        }
                        else
                        {
                            errorMsg = "You cannot delete ID 0";
                        }
                    }
                    else
                    {
                        errorMsg = "You specified an ID that does not exist!";
                    }
                }

                ImGui::SameLine();

                if (ImGui::Button("Cancel"))
                {
                    errorMsg = "";
                    showRemoveRowPopup = false;
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }

            if (ImGui::BeginTable(("CDB-Editor-DataTable-" + databaseName).c_str(), numColumns, ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg, outerSize, innerWidth))
            {
                ImGui::TableSetupScrollFreeze(1, 1); // Freeze 1 column and 1 row
                ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, columnWidthCache[0]);

                for (u32 i = 0; i < numFields; i++)
                {
                    const FieldInfo& field = fields[i];
                    ImGui::TableSetupColumn(field.name.c_str(), ImGuiTableColumnFlags_WidthFixed, columnWidthCache[i + 1]);
                }

                ImGui::TableHeadersRow();

                u32 numRows = db->GetNumRows();
                u32 maxRowIndex = numRows - 1u;

                ImGuiListClipper clipper;
                clipper.Begin(numRows);

                while (clipper.Step())
                {
                    for (i32 row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
                    {
                        const IDListEntry& idEntry = idList[row];

                        if (!db->Has(idEntry.id))
                            continue;

                        ImGui::TableNextRow();

                        // ID Column
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("%d", idEntry.id);

                        u32 rowOffset = idEntry.index;

                        // Data Columns
                        u32 numFields = static_cast<u32>(db->GetNumFields());
                        for (u32 i = 0; i < numFields; i++)
                        {
                            const FieldInfo& fieldInfo = fields[i];
                            u32 fieldOffset = fieldOffsets[i];

                            ImGui::TableSetColumnIndex(i + 1);

                            // Calculate the column width dynamically
                            f32 columnWidth = ImGui::GetContentRegionAvail().x; // Available space in the current column
                            ImGui::PushItemWidth(columnWidth); // Set input field to use the full width of the column

                            // Render appropriate editor based on FieldType
                            switch (fieldInfo.type)
                            {
                                case FieldType::I8:
                                {
                                    i32 value = *reinterpret_cast<i8*>(&data[rowOffset + fieldOffset]);
                                    if (ImGui::InputInt(("##" + std::to_string(idEntry.id) + "_" + std::to_string(i)).c_str(), &value, 0))
                                    {
                                        markAsDirty = true;
                                        *reinterpret_cast<i8*>(&data[rowOffset + fieldOffset]) = static_cast<i8>(value);
                                    }

                                    break;
                                }
                                case FieldType::I16:
                                {
                                    i32 value = *reinterpret_cast<i16*>(&data[rowOffset + fieldOffset]);
                                    if (ImGui::InputInt(("##" + std::to_string(idEntry.id) + "_" + std::to_string(i)).c_str(), &value, 0))
                                    {
                                        markAsDirty = true;
                                        *reinterpret_cast<i16*>(&data[rowOffset + fieldOffset]) = static_cast<i16>(value);
                                    }

                                    break;
                                }
                                case FieldType::I32:
                                {
                                    i32 value = *reinterpret_cast<i32*>(&data[rowOffset + fieldOffset]);
                                    if (ImGui::InputInt(("##" + std::to_string(idEntry.id) + "_" + std::to_string(i)).c_str(), &value, 0))
                                    {
                                        markAsDirty = true;
                                        *reinterpret_cast<i32*>(&data[rowOffset + fieldOffset]) = static_cast<i32>(value);
                                    }

                                    break;
                                }

                                case FieldType::I64:
                                {
                                    i64 value = *reinterpret_cast<i64*>(&data[rowOffset + fieldOffset]);
                                    if (ImGui::InputScalar(("##" + std::to_string(idEntry.id) + "_" + std::to_string(i)).c_str(), ImGuiDataType_S64, &value))
                                    {
                                        markAsDirty = true;
                                        *reinterpret_cast<i64*>(&data[rowOffset + fieldOffset]) = static_cast<i64>(value);
                                    }
                                    break;
                                }

                                case FieldType::F32:
                                {
                                    f32 value = *reinterpret_cast<f32*>(&data[rowOffset + fieldOffset]);
                                    if (ImGui::InputFloat(("##" + std::to_string(idEntry.id) + "_" + std::to_string(i)).c_str(), &value, 0.0f))
                                    {
                                        markAsDirty = true;
                                        *reinterpret_cast<f32*>(&data[rowOffset + fieldOffset]) = static_cast<f32>(value);
                                    }

                                    break;
                                }
                                case FieldType::F64:
                                {
                                    f64 value = *reinterpret_cast<f64*>(&data[rowOffset + fieldOffset]);
                                    if (ImGui::InputDouble(("##" + std::to_string(idEntry.id) + "_" + std::to_string(i)).c_str(), &value, 0.0))
                                    {
                                        markAsDirty = true;
                                        *reinterpret_cast<f64*>(&data[rowOffset + fieldOffset]) = static_cast<f64>(value);
                                    }

                                    break;
                                }
                                case FieldType::StringRef:
                                {
                                    i32 value = *reinterpret_cast<i32*>(&data[rowOffset + fieldOffset]);
                                    const std::string& stringVal = db->GetString(value);

                                    std::string str = stringVal;
                                    ImGui::InputText(("##" + std::to_string(idEntry.id) + "_" + std::to_string(i)).c_str(), &str);

                                    if (ImGui::IsItemDeactivatedAfterEdit())
                                    {
                                        markAsDirty = true;

                                        u32 stringRefVal = db->AddString(str);
                                        *reinterpret_cast<i32*>(&data[rowOffset + fieldOffset]) = stringRefVal;
                                    }

                                    break;
                                }

                                default:
                                {
                                    ImGui::Text("Unsupported");
                                    break;
                                }
                            }

                            ImGui::PopItemWidth(); // Restore the previous item width
                        }
                    }
                }

                ImGui::EndTable();
            }
        
            if (markAsDirty)
            {
                db->MarkDirty();
            }
            _previousSelectedDBHash = _selectedDBHash;
        }

        ImGui::End();
    }

    void CDBEditor::DrawImGui()
    {
        if (ImGui::Begin(GetName()))
        {
            ShowListView();
        }
        ImGui::End();
    }
}
