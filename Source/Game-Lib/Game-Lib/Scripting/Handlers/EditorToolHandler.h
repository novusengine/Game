#pragma once
#include <Base/Types.h>

#include <Scripting/Defines.h>
#include <Scripting/LuaMethodTable.h>

namespace Scripting::Editor
{
    // Backs the dev-only "Editor" Lua table: shared selection plus the gizmo / debug-draw
    // toggles consumed by the C++ editor systems (picking, gizmo, debug draw). State lives
    // in the EditorSelection singleton; this handler only owns the selection-changed callback.
    class EditorToolHandler : public LuaHandlerBase
    {
    public:
        void Register(Zenith* zenith);
        void Clear(Zenith* zenith);

        void PostLoad(Zenith* zenith) {}
        void Update(Zenith* zenith, f32 deltaTime);

        static i32 GetSelected(Zenith* zenith);
        static i32 SetSelected(Zenith* zenith);
        static i32 SetOnSelectionChanged(Zenith* zenith);
        static i32 SetOnGizmoChanged(Zenith* zenith);

        static i32 SetPickingEnabled(Zenith* zenith);
        static i32 SetGizmoEnabled(Zenith* zenith);
        static i32 GetGizmoOperation(Zenith* zenith);
        static i32 SetGizmoOperation(Zenith* zenith);
        static i32 GetGizmoMode(Zenith* zenith);
        static i32 SetGizmoMode(Zenith* zenith);

        static i32 SetDrawOBB(Zenith* zenith);
        static i32 SetDrawWorldAABB(Zenith* zenith);

        static i32 GetCVars(Zenith* zenith);
        static i32 SetCVar(Zenith* zenith);
        static i32 ResetCVar(Zenith* zenith);
        static i32 ResetAllCVars(Zenith* zenith);
        static i32 ResetAllCVarValues(Zenith* zenith);

        static i32 GetTerrainEditorState(Zenith* zenith);
        static i32 SetTerrainEditorEnabled(Zenith* zenith);
        static i32 SetTerrainEditorPreviewRadius(Zenith* zenith);
        static i32 BeginTerrainEditorStroke(Zenith* zenith);
        static i32 ApplyTerrainEditorSample(Zenith* zenith);
        static i32 SetTerrainEditorPaintTexture(Zenith* zenith);
        static i32 SetTerrainEditorPaintLayer(Zenith* zenith);
        static i32 ApplyTerrainEditorPaintSample(Zenith* zenith);
        static i32 ApplyTerrainEditorVertexColorSample(Zenith* zenith);
        static i32 GetTerrainEditorCursorTextureLayers(Zenith* zenith);
        static i32 CommitTerrainEditorStroke(Zenith* zenith);
        static i32 CancelTerrainEditorStroke(Zenith* zenith);
        static i32 UndoTerrainEditor(Zenith* zenith);
        static i32 RedoTerrainEditor(Zenith* zenith);
        static i32 SaveTerrainEditor(Zenith* zenith);
        static i32 GetTerrainEditorChunkLayout(Zenith* zenith);
        static i32 AddTerrainEditorChunk(Zenith* zenith);
        static i32 RemoveTerrainEditorChunk(Zenith* zenith);
        static i32 ResetTerrainEditorChunk(Zenith* zenith);
        static i32 GoToTerrainEditorChunk(Zenith* zenith);
        static i32 PreviewTerrainEditorHeightFieldImport(Zenith* zenith);
        static i32 ImportTerrainEditorHeightField(Zenith* zenith);

        static i32 GetSpellEditorSnapshot(Zenith* zenith);
        static i32 GetSpellEditorDraft(Zenith* zenith);
        static i32 GetSpellEditorCatalog(Zenith* zenith);
        static i32 SearchSpellEditorIcons(Zenith* zenith);
        static i32 RequestSpellEditorSnapshot(Zenith* zenith);
        static i32 OpenSpellEditorDraft(Zenith* zenith);
        static i32 CreateSpellEditorDraft(Zenith* zenith);
        static i32 DuplicateSpellEditorDraft(Zenith* zenith);
        static i32 DiscardSpellEditorDraft(Zenith* zenith);
        static i32 SetSpellEditorField(Zenith* zenith);
        static i32 SetSpellEditorAuraEnabled(Zenith* zenith);
        static i32 SetSpellEditorAuraField(Zenith* zenith);
        static i32 AddSpellEditorEffect(Zenith* zenith);
        static i32 RemoveSpellEditorEffect(Zenith* zenith);
        static i32 MoveSpellEditorEffect(Zenith* zenith);
        static i32 SetSpellEditorEffectField(Zenith* zenith);
        static i32 SetSpellEditorEffectParameter(Zenith* zenith);
        static i32 AddSpellEditorConstraint(Zenith* zenith);
        static i32 RemoveSpellEditorConstraint(Zenith* zenith);
        static i32 SetSpellEditorConstraintField(Zenith* zenith);
        static i32 ResetSpellEditorConstraintField(Zenith* zenith);
        static i32 CreateSpellEditorConstraintGroup(Zenith* zenith);
        static i32 UpdateSpellEditorConstraintGroup(Zenith* zenith);
        static i32 DeleteSpellEditorConstraintGroup(Zenith* zenith);
        static i32 CreateSpellEditorProcData(Zenith* zenith);
        static i32 UpdateSpellEditorProcData(Zenith* zenith);
        static i32 DeleteSpellEditorProcData(Zenith* zenith);
        static i32 AddSpellEditorProcLink(Zenith* zenith);
        static i32 RemoveSpellEditorProcLink(Zenith* zenith);
        static i32 SetSpellEditorProcLinkData(Zenith* zenith);
        static i32 SetSpellEditorProcLinkEffect(Zenith* zenith);
        static i32 ValidateSpellEditorDraft(Zenith* zenith);
        static i32 SubmitSpellEditorDraft(Zenith* zenith);
        static i32 DeleteSpellEditorSpell(Zenith* zenith);

        static i32 GetMapEditorSnapshot(Zenith* zenith);
        static i32 GetMapEditorState(Zenith* zenith);
        static i32 RequestMapEditorSnapshot(Zenith* zenith);
        static i32 CreateMapEditorMap(Zenith* zenith);
        static i32 UpdateMapEditorMap(Zenith* zenith);
        static i32 TakeMapEditorMutationResult(Zenith* zenith);

        static i32 GetInteractionEditorSnapshot(Zenith* zenith);
        static i32 GetInteractionEditorState(Zenith* zenith);
        static i32 GetInteractionEditorRevision(Zenith* zenith);
        static i32 RequestInteractionEditorSnapshot(Zenith* zenith);
        static i32 CreateInteractionEditorText(Zenith* zenith);
        static i32 UpdateInteractionEditorText(Zenith* zenith);
        static i32 DeleteInteractionEditorText(Zenith* zenith);
        static i32 CreateInteractionEditorTranslation(Zenith* zenith);
        static i32 UpdateInteractionEditorTranslation(Zenith* zenith);
        static i32 DeleteInteractionEditorTranslation(Zenith* zenith);
        static i32 CreateInteractionEditorConditionSet(Zenith* zenith);
        static i32 UpdateInteractionEditorConditionSet(Zenith* zenith);
        static i32 DeleteInteractionEditorConditionSet(Zenith* zenith);
        static i32 CreateInteractionEditorConditionGroup(Zenith* zenith);
        static i32 UpdateInteractionEditorConditionGroup(Zenith* zenith);
        static i32 DeleteInteractionEditorConditionGroup(Zenith* zenith);
        static i32 CreateInteractionEditorCondition(Zenith* zenith);
        static i32 UpdateInteractionEditorCondition(Zenith* zenith);
        static i32 DeleteInteractionEditorCondition(Zenith* zenith);
        static i32 CreateInteractionEditorGossipMenu(Zenith* zenith);
        static i32 UpdateInteractionEditorGossipMenu(Zenith* zenith);
        static i32 DeleteInteractionEditorGossipMenu(Zenith* zenith);
        static i32 CreateInteractionEditorGossipMenuOption(Zenith* zenith);
        static i32 UpdateInteractionEditorGossipMenuOption(Zenith* zenith);
        static i32 ReorderInteractionEditorGossipMenuOption(Zenith* zenith);
        static i32 DeleteInteractionEditorGossipMenuOption(Zenith* zenith);
        static i32 CreateInteractionEditorCreatureTemplateInteraction(Zenith* zenith);
        static i32 UpdateInteractionEditorCreatureTemplateInteraction(Zenith* zenith);
        static i32 DeleteInteractionEditorCreatureTemplateInteraction(Zenith* zenith);
        static i32 CreateInteractionEditorCreatureTemplateGossip(Zenith* zenith);
        static i32 UpdateInteractionEditorCreatureTemplateGossip(Zenith* zenith);
        static i32 DeleteInteractionEditorCreatureTemplateGossip(Zenith* zenith);
        static i32 TakeInteractionEditorMutationResult(Zenith* zenith);

        static i32 GetCreatureAIEditorState(Zenith* zenith);
        static i32 RequestCreatureAIEditorCatalog(Zenith* zenith);
        static i32 InspectCreatureAIEditorUnit(Zenith* zenith);
        static i32 ClearCreatureAIEditorInspection(Zenith* zenith);
        static i32 ViewCreatureAIEditorScript(Zenith* zenith);
        static i32 EditCreatureAIEditorScript(Zenith* zenith);
        static i32 CreateCreatureAIEditorScript(Zenith* zenith);
        static i32 DuplicateCreatureAIEditorScript(Zenith* zenith);
        static i32 FinishEditingCreatureAIEditorScript(Zenith* zenith);
        static i32 LinkCreatureAIEditorScript(Zenith* zenith);
        static i32 UnlinkCreatureAIEditorScript(Zenith* zenith);

        // Fires the registered selection-changed callback. Called from Lua (after SetSelected)
        // and from C++ (after picking updates the selection).
        void OnSelectionChanged(Zenith* zenith);

        // Fires the registered gizmo-changed callback. Called from C++ after a gizmo drag mutates
        // the selected entity's transform, so the Inspector can refresh its fields.
        void OnGizmoChanged(Zenith* zenith);

    private:
        i32 _onSelectionChangedRef = LUA_NOREF;
        i32 _onGizmoChangedRef = LUA_NOREF;
    };

    static LuaRegister<> editorGlobalMethods[] =
    {
        { "GetSelected",            EditorToolHandler::GetSelected,           Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetSelected",            EditorToolHandler::SetSelected,           Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetOnSelectionChanged",  EditorToolHandler::SetOnSelectionChanged, Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetOnGizmoChanged",      EditorToolHandler::SetOnGizmoChanged,     Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetPickingEnabled",      EditorToolHandler::SetPickingEnabled,     Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetGizmoEnabled",        EditorToolHandler::SetGizmoEnabled,       Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetGizmoOperation",      EditorToolHandler::GetGizmoOperation,     Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetGizmoOperation",      EditorToolHandler::SetGizmoOperation,     Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetGizmoMode",           EditorToolHandler::GetGizmoMode,          Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetGizmoMode",           EditorToolHandler::SetGizmoMode,          Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetDrawOBB",             EditorToolHandler::SetDrawOBB,            Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetDrawWorldAABB",       EditorToolHandler::SetDrawWorldAABB,      Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetCVars",               EditorToolHandler::GetCVars,              Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetCVar",                EditorToolHandler::SetCVar,               Scripting::LuaMethodFlags::DeveloperOnly },
        { "ResetCVar",              EditorToolHandler::ResetCVar,             Scripting::LuaMethodFlags::DeveloperOnly },
        { "ResetAllCVars",          EditorToolHandler::ResetAllCVars,         Scripting::LuaMethodFlags::DeveloperOnly },
        { "ResetAllCVarValues",     EditorToolHandler::ResetAllCVarValues,    Scripting::LuaMethodFlags::DeveloperOnly },
    };

    static LuaRegister<> spellEditorGlobalMethods[] =
    {
        { "GetSnapshot",            EditorToolHandler::GetSpellEditorSnapshot,        Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetDraft",               EditorToolHandler::GetSpellEditorDraft,           Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetCatalog",             EditorToolHandler::GetSpellEditorCatalog,         Scripting::LuaMethodFlags::DeveloperOnly },
        { "SearchIcons",            EditorToolHandler::SearchSpellEditorIcons,        Scripting::LuaMethodFlags::DeveloperOnly },
        { "RequestSnapshot",        EditorToolHandler::RequestSpellEditorSnapshot,    Scripting::LuaMethodFlags::DeveloperOnly },
        { "OpenDraft",              EditorToolHandler::OpenSpellEditorDraft,           Scripting::LuaMethodFlags::DeveloperOnly },
        { "CreateDraft",            EditorToolHandler::CreateSpellEditorDraft,         Scripting::LuaMethodFlags::DeveloperOnly },
        { "DuplicateDraft",         EditorToolHandler::DuplicateSpellEditorDraft,      Scripting::LuaMethodFlags::DeveloperOnly },
        { "DiscardDraft",           EditorToolHandler::DiscardSpellEditorDraft,        Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetField",               EditorToolHandler::SetSpellEditorField,            Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetAuraEnabled",         EditorToolHandler::SetSpellEditorAuraEnabled,      Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetAuraField",           EditorToolHandler::SetSpellEditorAuraField,        Scripting::LuaMethodFlags::DeveloperOnly },
        { "AddEffect",              EditorToolHandler::AddSpellEditorEffect,           Scripting::LuaMethodFlags::DeveloperOnly },
        { "RemoveEffect",           EditorToolHandler::RemoveSpellEditorEffect,        Scripting::LuaMethodFlags::DeveloperOnly },
        { "MoveEffect",             EditorToolHandler::MoveSpellEditorEffect,          Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetEffectField",         EditorToolHandler::SetSpellEditorEffectField,      Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetEffectParameter",     EditorToolHandler::SetSpellEditorEffectParameter,  Scripting::LuaMethodFlags::DeveloperOnly },
        { "AddConstraint",          EditorToolHandler::AddSpellEditorConstraint,       Scripting::LuaMethodFlags::DeveloperOnly },
        { "RemoveConstraint",       EditorToolHandler::RemoveSpellEditorConstraint,    Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetConstraintField",     EditorToolHandler::SetSpellEditorConstraintField,  Scripting::LuaMethodFlags::DeveloperOnly },
        { "ResetConstraintField",   EditorToolHandler::ResetSpellEditorConstraintField, Scripting::LuaMethodFlags::DeveloperOnly },
        { "CreateConstraintGroup",  EditorToolHandler::CreateSpellEditorConstraintGroup, Scripting::LuaMethodFlags::DeveloperOnly },
        { "UpdateConstraintGroup",  EditorToolHandler::UpdateSpellEditorConstraintGroup, Scripting::LuaMethodFlags::DeveloperOnly },
        { "DeleteConstraintGroup",  EditorToolHandler::DeleteSpellEditorConstraintGroup, Scripting::LuaMethodFlags::DeveloperOnly },
        { "CreateProcData",         EditorToolHandler::CreateSpellEditorProcData,      Scripting::LuaMethodFlags::DeveloperOnly },
        { "UpdateProcData",         EditorToolHandler::UpdateSpellEditorProcData,      Scripting::LuaMethodFlags::DeveloperOnly },
        { "DeleteProcData",         EditorToolHandler::DeleteSpellEditorProcData,      Scripting::LuaMethodFlags::DeveloperOnly },
        { "AddProcLink",            EditorToolHandler::AddSpellEditorProcLink,         Scripting::LuaMethodFlags::DeveloperOnly },
        { "RemoveProcLink",         EditorToolHandler::RemoveSpellEditorProcLink,      Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetProcLinkData",        EditorToolHandler::SetSpellEditorProcLinkData,     Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetProcLinkEffect",      EditorToolHandler::SetSpellEditorProcLinkEffect,   Scripting::LuaMethodFlags::DeveloperOnly },
        { "Validate",               EditorToolHandler::ValidateSpellEditorDraft,       Scripting::LuaMethodFlags::DeveloperOnly },
        { "Submit",                 EditorToolHandler::SubmitSpellEditorDraft,         Scripting::LuaMethodFlags::DeveloperOnly },
        { "Delete",                 EditorToolHandler::DeleteSpellEditorSpell,         Scripting::LuaMethodFlags::DeveloperOnly },
    };

    static LuaRegister<> terrainEditorGlobalMethods[] =
    {
        { "GetState",         EditorToolHandler::GetTerrainEditorState,         Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetEnabled",       EditorToolHandler::SetTerrainEditorEnabled,       Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetPreviewRadius", EditorToolHandler::SetTerrainEditorPreviewRadius, Scripting::LuaMethodFlags::DeveloperOnly },
        { "BeginStroke",      EditorToolHandler::BeginTerrainEditorStroke,      Scripting::LuaMethodFlags::DeveloperOnly },
        { "ApplySample",      EditorToolHandler::ApplyTerrainEditorSample,      Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetPaintTexture",  EditorToolHandler::SetTerrainEditorPaintTexture,  Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetPaintLayer",    EditorToolHandler::SetTerrainEditorPaintLayer,    Scripting::LuaMethodFlags::DeveloperOnly },
        { "ApplyPaintSample", EditorToolHandler::ApplyTerrainEditorPaintSample, Scripting::LuaMethodFlags::DeveloperOnly },
        { "ApplyVertexColorSample", EditorToolHandler::ApplyTerrainEditorVertexColorSample, Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetCursorLayers",  EditorToolHandler::GetTerrainEditorCursorTextureLayers, Scripting::LuaMethodFlags::DeveloperOnly },
        { "CommitStroke",     EditorToolHandler::CommitTerrainEditorStroke,     Scripting::LuaMethodFlags::DeveloperOnly },
        { "CancelStroke",     EditorToolHandler::CancelTerrainEditorStroke,     Scripting::LuaMethodFlags::DeveloperOnly },
        { "Undo",             EditorToolHandler::UndoTerrainEditor,             Scripting::LuaMethodFlags::DeveloperOnly },
        { "Redo",             EditorToolHandler::RedoTerrainEditor,             Scripting::LuaMethodFlags::DeveloperOnly },
        { "Save",             EditorToolHandler::SaveTerrainEditor,             Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetChunkLayout",   EditorToolHandler::GetTerrainEditorChunkLayout,   Scripting::LuaMethodFlags::DeveloperOnly },
        { "AddChunk",         EditorToolHandler::AddTerrainEditorChunk,         Scripting::LuaMethodFlags::DeveloperOnly },
        { "RemoveChunk",      EditorToolHandler::RemoveTerrainEditorChunk,      Scripting::LuaMethodFlags::DeveloperOnly },
        { "ResetChunk",       EditorToolHandler::ResetTerrainEditorChunk,       Scripting::LuaMethodFlags::DeveloperOnly },
        { "GoToChunk",        EditorToolHandler::GoToTerrainEditorChunk,        Scripting::LuaMethodFlags::DeveloperOnly },
        { "PreviewHeightFieldImport", EditorToolHandler::PreviewTerrainEditorHeightFieldImport, Scripting::LuaMethodFlags::DeveloperOnly },
        { "ImportHeightField",        EditorToolHandler::ImportTerrainEditorHeightField,        Scripting::LuaMethodFlags::DeveloperOnly },
    };

    static LuaRegister<> mapEditorGlobalMethods[] =
    {
        { "GetSnapshot",        EditorToolHandler::GetMapEditorSnapshot,          Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetState",           EditorToolHandler::GetMapEditorState,             Scripting::LuaMethodFlags::DeveloperOnly },
        { "RequestSnapshot",    EditorToolHandler::RequestMapEditorSnapshot,      Scripting::LuaMethodFlags::DeveloperOnly },
        { "Create",             EditorToolHandler::CreateMapEditorMap,            Scripting::LuaMethodFlags::DeveloperOnly },
        { "Update",             EditorToolHandler::UpdateMapEditorMap,            Scripting::LuaMethodFlags::DeveloperOnly },
        { "TakeMutationResult", EditorToolHandler::TakeMapEditorMutationResult,   Scripting::LuaMethodFlags::DeveloperOnly },
    };

    static LuaRegister<> interactionEditorGlobalMethods[] =
    {
        { "GetSnapshot", EditorToolHandler::GetInteractionEditorSnapshot, Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetState", EditorToolHandler::GetInteractionEditorState, Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetRevision", EditorToolHandler::GetInteractionEditorRevision, Scripting::LuaMethodFlags::DeveloperOnly },
        { "RequestSnapshot", EditorToolHandler::RequestInteractionEditorSnapshot, Scripting::LuaMethodFlags::DeveloperOnly },
        { "CreateText", EditorToolHandler::CreateInteractionEditorText, Scripting::LuaMethodFlags::DeveloperOnly },
        { "UpdateText", EditorToolHandler::UpdateInteractionEditorText, Scripting::LuaMethodFlags::DeveloperOnly },
        { "DeleteText", EditorToolHandler::DeleteInteractionEditorText, Scripting::LuaMethodFlags::DeveloperOnly },
        { "CreateTranslation", EditorToolHandler::CreateInteractionEditorTranslation, Scripting::LuaMethodFlags::DeveloperOnly },
        { "UpdateTranslation", EditorToolHandler::UpdateInteractionEditorTranslation, Scripting::LuaMethodFlags::DeveloperOnly },
        { "DeleteTranslation", EditorToolHandler::DeleteInteractionEditorTranslation, Scripting::LuaMethodFlags::DeveloperOnly },
        { "CreateConditionSet", EditorToolHandler::CreateInteractionEditorConditionSet, Scripting::LuaMethodFlags::DeveloperOnly },
        { "UpdateConditionSet", EditorToolHandler::UpdateInteractionEditorConditionSet, Scripting::LuaMethodFlags::DeveloperOnly },
        { "DeleteConditionSet", EditorToolHandler::DeleteInteractionEditorConditionSet, Scripting::LuaMethodFlags::DeveloperOnly },
        { "CreateConditionGroup", EditorToolHandler::CreateInteractionEditorConditionGroup, Scripting::LuaMethodFlags::DeveloperOnly },
        { "UpdateConditionGroup", EditorToolHandler::UpdateInteractionEditorConditionGroup, Scripting::LuaMethodFlags::DeveloperOnly },
        { "DeleteConditionGroup", EditorToolHandler::DeleteInteractionEditorConditionGroup, Scripting::LuaMethodFlags::DeveloperOnly },
        { "CreateCondition", EditorToolHandler::CreateInteractionEditorCondition, Scripting::LuaMethodFlags::DeveloperOnly },
        { "UpdateCondition", EditorToolHandler::UpdateInteractionEditorCondition, Scripting::LuaMethodFlags::DeveloperOnly },
        { "DeleteCondition", EditorToolHandler::DeleteInteractionEditorCondition, Scripting::LuaMethodFlags::DeveloperOnly },
        { "CreateGossipMenu", EditorToolHandler::CreateInteractionEditorGossipMenu, Scripting::LuaMethodFlags::DeveloperOnly },
        { "UpdateGossipMenu", EditorToolHandler::UpdateInteractionEditorGossipMenu, Scripting::LuaMethodFlags::DeveloperOnly },
        { "DeleteGossipMenu", EditorToolHandler::DeleteInteractionEditorGossipMenu, Scripting::LuaMethodFlags::DeveloperOnly },
        { "CreateGossipMenuOption", EditorToolHandler::CreateInteractionEditorGossipMenuOption, Scripting::LuaMethodFlags::DeveloperOnly },
        { "UpdateGossipMenuOption", EditorToolHandler::UpdateInteractionEditorGossipMenuOption, Scripting::LuaMethodFlags::DeveloperOnly },
        { "ReorderGossipMenuOption", EditorToolHandler::ReorderInteractionEditorGossipMenuOption, Scripting::LuaMethodFlags::DeveloperOnly },
        { "DeleteGossipMenuOption", EditorToolHandler::DeleteInteractionEditorGossipMenuOption, Scripting::LuaMethodFlags::DeveloperOnly },
        { "CreateCreatureTemplateInteraction", EditorToolHandler::CreateInteractionEditorCreatureTemplateInteraction, Scripting::LuaMethodFlags::DeveloperOnly },
        { "UpdateCreatureTemplateInteraction", EditorToolHandler::UpdateInteractionEditorCreatureTemplateInteraction, Scripting::LuaMethodFlags::DeveloperOnly },
        { "DeleteCreatureTemplateInteraction", EditorToolHandler::DeleteInteractionEditorCreatureTemplateInteraction, Scripting::LuaMethodFlags::DeveloperOnly },
        { "CreateCreatureTemplateGossip", EditorToolHandler::CreateInteractionEditorCreatureTemplateGossip, Scripting::LuaMethodFlags::DeveloperOnly },
        { "UpdateCreatureTemplateGossip", EditorToolHandler::UpdateInteractionEditorCreatureTemplateGossip, Scripting::LuaMethodFlags::DeveloperOnly },
        { "DeleteCreatureTemplateGossip", EditorToolHandler::DeleteInteractionEditorCreatureTemplateGossip, Scripting::LuaMethodFlags::DeveloperOnly },
        { "TakeMutationResult", EditorToolHandler::TakeInteractionEditorMutationResult, Scripting::LuaMethodFlags::DeveloperOnly },
    };

    static LuaRegister<> creatureAIEditorGlobalMethods[] =
    {
        { "GetState",       EditorToolHandler::GetCreatureAIEditorState,       Scripting::LuaMethodFlags::DeveloperOnly },
        { "RequestCatalog", EditorToolHandler::RequestCreatureAIEditorCatalog, Scripting::LuaMethodFlags::DeveloperOnly },
        { "InspectUnit",    EditorToolHandler::InspectCreatureAIEditorUnit,    Scripting::LuaMethodFlags::DeveloperOnly },
        { "ClearInspection", EditorToolHandler::ClearCreatureAIEditorInspection, Scripting::LuaMethodFlags::DeveloperOnly },
        { "View",           EditorToolHandler::ViewCreatureAIEditorScript,     Scripting::LuaMethodFlags::DeveloperOnly },
        { "Edit",           EditorToolHandler::EditCreatureAIEditorScript,     Scripting::LuaMethodFlags::DeveloperOnly },
        { "Create",         EditorToolHandler::CreateCreatureAIEditorScript,   Scripting::LuaMethodFlags::DeveloperOnly },
        { "Duplicate",      EditorToolHandler::DuplicateCreatureAIEditorScript, Scripting::LuaMethodFlags::DeveloperOnly },
        { "FinishEditing",  EditorToolHandler::FinishEditingCreatureAIEditorScript, Scripting::LuaMethodFlags::DeveloperOnly },
        { "Link",           EditorToolHandler::LinkCreatureAIEditorScript,     Scripting::LuaMethodFlags::DeveloperOnly },
        { "Unlink",         EditorToolHandler::UnlinkCreatureAIEditorScript,   Scripting::LuaMethodFlags::DeveloperOnly },
    };
}
