#include "CDBEditor.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/ClientDBCollection.h"
#include "Game-Lib/ECS/Singletons/MapDB.h"
#include "Game-Lib/ECS/Util/MapUtil.h"
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
        entt::registry& registry = *registries->gameRegistry;

        entt::registry::context& ctx = registry.ctx();

        auto& clientDBCollection = ctx.get<ClientDBCollection>();
        auto* mapStorage = clientDBCollection.Get(ClientDBHash::Map);

        // Begin a list box
        ImGui::Text("Select a CDB (Optionally use the filter below)");

        u32 TotalNumDBs = clientDBCollection.Count();
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
                    clientDBCollection.Each([this, &clientDBCollection](ClientDBHash hash, const ClientDB::Data* db)
                    {
                        const std::string& dbName = clientDBCollection.GetDBName(hash);
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
                        
                        clientDBCollection.EachInRange(start, count, [this, &clientDBCollection](ClientDBHash hash, const ClientDB::Data* db)
                        {
                            const std::string& dbName = clientDBCollection.GetDBName(hash);
                            DrawDBItem(hash, db, dbName, currentFilter, _selectedDBHash);
                        });
                    }
                }
            }

            ImGui::EndListBox();
        }

        
        if (ImGui::Button("Create Database"))
        {
            _showCreateDatabaseWindow = true;
            _newDatabaseName = "";
            _newDatabaseFields.clear();
        }

        if (_selectedDBHash != 0)
        {
            ImGui::SameLine();

            if (ImGui::Button("Edit Database"))
            {
                _showEditDatabaseWindow = true;
            }

            ImGui::SameLine();

            if (ImGui::Button("Delete Database"))
            {
                ClientDBHash hash = static_cast<ClientDBHash>(_selectedDBHash);
                clientDBCollection.Remove(hash);

                _selectedDBHash = 0;
                _previousSelectedDBHash = 0;
                _showEditDatabaseWindow = false;
            }
        }

        if (_showCreateDatabaseWindow)
        {
            RenderCreateDatabaseWindow();
        }

        if (_showEditDatabaseWindow)
        {
            RenderEditDatabaseWindow();
        }
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
        entt::registry& registry = *registries->gameRegistry;

        entt::registry::context& ctx = registry.ctx();

        auto& clientDBCollection = ctx.get<ClientDBCollection>();

        static i32 draggedFieldIndex = -1;
        static const f32 rowHeight = 22.5f; // Fixed row height
        static std::string errorMessage = ""; // Error message for validation feedback

        if (ImGui::Begin("Create Database", &_showCreateDatabaseWindow))
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
                ImGui::Text("%zu", i + 1);
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

                    if (clientDBCollection.Has(dbHash))
                    {
                        errorMessage = "A Database with that name already exists.";
                    }
                    else
                    {
                        clientDBCollection.Register(dbHash, _newDatabaseName);
                        
                        auto* newStorage = clientDBCollection.Get(dbHash);
                        newStorage->Initialize(_newDatabaseFields);
                        newStorage->MarkDirty();

                        _showCreateDatabaseWindow = false;
                        draggedFieldIndex = -1;
                        errorMessage = "";
                    }
                }
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel"))
            {
                _showCreateDatabaseWindow = false;
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
        entt::registry& registry = *registries->gameRegistry;

        entt::registry::context& ctx = registry.ctx();

        auto& clientDBCollection = ctx.get<ClientDBCollection>();

        if (ImGui::Begin("Edit Database", &_showEditDatabaseWindow))
        {
            ClientDBHash prevHash = static_cast<ClientDBHash>(_previousSelectedDBHash);
            ClientDBHash hash = static_cast<ClientDBHash>(_selectedDBHash);

            ClientDB::Data* db = clientDBCollection.Get(hash);
            const std::string& databaseName = clientDBCollection.GetDBName(hash);

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
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), errorMsg.c_str());
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
                        ImGui::Text("%zu", idEntry.id);

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
