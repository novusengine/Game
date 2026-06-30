#include "EditorTools.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/AABB.h"
#include "Game-Lib/ECS/Components/Camera.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Singletons/ActiveCamera.h"
#include "Game-Lib/ECS/Singletons/EditorSelection.h"
#include "Game-Lib/ECS/Singletons/UISingleton.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Rendering/PixelQuery.h"
#include "Game-Lib/Scripting/Handlers/AssetHandler.h"
#include "Game-Lib/Scripting/Handlers/EditorToolHandler.h"
#include "Game-Lib/Scripting/Util/ZenithUtil.h"
#include "Game-Lib/Util/PhysicsUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Math/Color.h>

#include <Input/InputManager.h>
#include <Input/KeybindGroup.h>

#include <MetaGen/Game/Lua/Lua.h>

#include <Renderer/Renderer.h>

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <limits>
#include <string>

namespace ECS::Systems::Editor
{
    // Mirrors Editor::QueryObjectType (kept local so we don't pull in the ImGui Inspector header).
    enum PickType : u32
    {
        PickNone = 0,
        PickTerrain = 1,
        PickModelOpaque = 2,
        PickModelTransparent = 3,
    };

    static constexpr i32 MOUSE_BUTTON_LEFT = 0; // GLFW_MOUSE_BUTTON_LEFT
    static constexpr i32 KEY_DELETE = 261;      // GLFW_KEY_DELETE
    // The gizmo is drawn on top at a constant screen size (like ImGuizmo): its world length is a
    // fraction of the camera->pivot distance, which keeps it the same apparent size regardless of
    // distance and independent of the object's (collider-inflated) bounds.
    static constexpr f32 GIZMO_SCREEN_SCALE = 0.09f;

    struct EditorToolsState
    {
        bool prevMouseDown = false;

        // PixelQuery picking (async, token based).
        u32 queriedToken = 0;
        u32 activeToken = 0;

        // Gizmo drag. The drag is solved in world space against the mouse ray (see Update), so we
        // only need the transform + the picked axis captured at press time, plus the ray's starting
        // parameter/angle along/around that axis.
        bool dragging = false;
        i32 dragAxis = -1;
        vec3 startPos = vec3(0.0f);
        vec3 startScale = vec3(1.0f);
        quat startRot = quat(1.0f, 0.0f, 0.0f, 0.0f);
        vec3 axisDir[3] = { vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1) };
        f32 dragStartAxisT = 0.0f;  // translate/scale: ray's closest param along the axis at press
        f32 dragStartAngle = 0.0f;  // rotate: ray-plane hit angle around the axis at press
        f32 dragRefLength = 1.0f;   // gizmo size at press, used to map scale drag distance to a factor

        // Drag-spawn from the Asset Browser: spawn a model under the cursor, follow it, then drop.
        bool dragSpawnActive = false;
        entt::entity dragSpawnEntity = entt::null;
        std::string dragSpawnPath;
        f32 dragSpawnYaw = 0.0f;
        f32 scrollAccum = 0.0f;                  // wheel notches accumulated by the keybind callback
        bool dragSpawnTransparencyApplied = false;

        bool deleteSelectedRequested = false;    // set by the Delete keybind, processed in Update
    };
    static EditorToolsState s;

    void EditorTools::Init(entt::registry& /*registry*/)
    {
        // Editor input. Priority above the cameras (10) but below the UI (200), so focused text
        // fields keep their keys; we consume the wheel (so cameras don't zoom) only mid-drag, and
        // Delete only when there's a selection to remove.
        InputManager* inputManager = ServiceLocator::GetInputManager();
        if (inputManager)
        {
            KeybindGroup* group = inputManager->CreateKeybindGroup("EditorTools", 100);
            group->SetActive(true);

            // Scroll-to-rotate the object being drag-spawned.
            group->AddMouseScrollCallback([](f32 /*x*/, f32 y) -> bool
            {
                if (s.dragSpawnActive && s.dragSpawnEntity != entt::null)
                {
                    s.scrollAccum += y;
                    return true;
                }
                return false;
            });

            // Delete removes the selected entity (deferred to Update for a safe ECS mutation point).
            group->AddKeyboardCallback("DeleteSelected", KEY_DELETE, KeybindAction::Press, KeybindModifier::Any, [](i32 /*key*/, KeybindAction /*action*/, KeybindModifier /*modifier*/) -> bool
            {
                EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
                if (!registries || !registries->gameRegistry)
                    return false;

                entt::registry::context& ctx = registries->gameRegistry->ctx();
                if (!ctx.contains<Singletons::EditorSelection>())
                    return false;

                auto& selection = ctx.get<Singletons::EditorSelection>();
                if (selection.selectedEntity == entt::null || !registries->gameRegistry->valid(selection.selectedEntity))
                    return false;

                s.deleteSelectedRequested = true;
                return true;
            });
        }
    }

    // Builds a world-space ray through the mouse pixel by unprojecting the near and far clip points.
    static void ScreenPointToRay(const mat4x4& invViewProj, const vec2& renderSize, const vec2& mouse, vec3& outOrigin, vec3& outDir)
    {
        f32 ndcX = (mouse.x / renderSize.x) * 2.0f - 1.0f;
        f32 ndcY = 1.0f - (mouse.y / renderSize.y) * 2.0f; // mouse is top-left origin, NDC y is up

        vec4 nearH = invViewProj * vec4(ndcX, ndcY, 0.0f, 1.0f);
        vec4 farH = invViewProj * vec4(ndcX, ndcY, 1.0f, 1.0f);
        vec3 nearP = vec3(nearH) / nearH.w;
        vec3 farP = vec3(farH) / farH.w;

        outOrigin = nearP;
        outDir = glm::normalize(farP - nearP);
    }

    // Parameter along the axis line (origin + axisDir*t) of the point closest to the mouse ray.
    // Returns false when the axis is nearly parallel to the ray (the solve is unstable).
    static bool ClosestAxisParam(const vec3& axisOrigin, const vec3& axisDir, const vec3& rayOrigin, const vec3& rayDir, f32& outT)
    {
        vec3 w0 = axisOrigin - rayOrigin;
        f32 b = glm::dot(axisDir, rayDir);
        f32 denom = 1.0f - b * b; // axisDir and rayDir are unit length
        if (denom < 1e-4f)
            return false;

        f32 d = glm::dot(axisDir, w0);
        f32 e = glm::dot(rayDir, w0);
        outT = (b * e - d) / denom;
        return true;
    }

    static bool RayPlaneIntersect(const vec3& planePoint, const vec3& planeNormal, const vec3& rayOrigin, const vec3& rayDir, vec3& outPoint)
    {
        f32 denom = glm::dot(planeNormal, rayDir);
        if (glm::abs(denom) < 1e-6f)
            return false;

        f32 t = glm::dot(planePoint - rayOrigin, planeNormal) / denom;
        if (t < 0.0f)
            return false;

        outPoint = rayOrigin + rayDir * t;
        return true;
    }

    // Shortest distance between the mouse ray and the segment [segA, segB], used to pick a handle
    // against its actual world-space geometry instead of a screen-space line. outSegParam reports
    // where the closest approach falls along the segment (0 at segA .. 1 at segB) so callers can
    // compare against a radius that varies along the handle (thin shaft, wide cone).
    static f32 RaySegmentDistance(const vec3& rayOrigin, const vec3& rayDir, const vec3& segA, const vec3& segB, f32& outSegParam)
    {
        vec3 d1 = segB - segA;
        vec3 r = segA - rayOrigin;
        f32 a = glm::dot(d1, d1); // segment length squared
        f32 b = glm::dot(d1, rayDir);
        f32 d = glm::dot(d1, r);
        f32 e = glm::dot(rayDir, r);
        f32 denom = a - b * b; // rayDir is unit length

        f32 sc = (denom > 1e-6f) ? glm::clamp((b * e - d) / denom, 0.0f, 1.0f) : 0.0f;
        f32 tc = e + b * sc; // param along the ray
        if (tc < 0.0f)
        {
            tc = 0.0f;
            sc = glm::clamp(-d / a, 0.0f, 1.0f);
        }

        outSegParam = sc;
        vec3 pSeg = segA + d1 * sc;
        vec3 pRay = rayOrigin + rayDir * tc;
        return glm::distance(pSeg, pRay);
    }

    // Two perpendicular unit vectors spanning the plane whose normal is `n` (deterministic basis).
    static void PerpendicularBasis(const vec3& n, vec3& outU, vec3& outV)
    {
        vec3 ref = (glm::abs(n.y) < 0.99f) ? vec3(0, 1, 0) : vec3(1, 0, 0);
        outU = glm::normalize(glm::cross(n, ref));
        outV = glm::cross(n, outU);
    }

    // All gizmo geometry is drawn through the overlay solid path (DrawTriangleSolid3DOverlay), which
    // renders over the scene with no depth test and no back-face culling, so single-winding triangles
    // are enough and the gizmo is never buried inside an object.

    // Filled cylinder from `from` to `to`, used for the shafts.
    static void DrawCylinderOverlay(DebugRenderer* dr, const vec3& from, const vec3& to, f32 radius, Color color, i32 segments)
    {
        vec3 n = glm::normalize(to - from);
        vec3 u, v;
        PerpendicularBasis(n, u, v);

        vec3 prevU = u * radius;
        for (i32 i = 1; i <= segments; ++i)
        {
            f32 ang = (static_cast<f32>(i) / static_cast<f32>(segments)) * glm::two_pi<f32>();
            vec3 curU = (u * glm::cos(ang) + v * glm::sin(ang)) * radius;
            dr->DrawTriangleSolid3DOverlay(from + prevU, to + prevU, to + curU, color, true);
            dr->DrawTriangleSolid3DOverlay(from + prevU, to + curU, from + curU, color, true);
            prevU = curU;
        }
    }

    // Filled arrowhead cone: base circle of `radius` at `base`, apex at base + dir*length, with base cap.
    static void DrawArrowHeadOverlay(DebugRenderer* dr, const vec3& base, const vec3& dir, f32 length, f32 radius, Color color, i32 segments)
    {
        vec3 n = glm::normalize(dir);
        vec3 u, v;
        PerpendicularBasis(n, u, v);
        vec3 apex = base + n * length;

        vec3 prev = base + u * radius;
        for (i32 i = 1; i <= segments; ++i)
        {
            f32 ang = (static_cast<f32>(i) / static_cast<f32>(segments)) * glm::two_pi<f32>();
            vec3 cur = base + (u * glm::cos(ang) + v * glm::sin(ang)) * radius;
            dr->DrawTriangleSolid3DOverlay(apex, prev, cur, color, true); // side
            dr->DrawTriangleSolid3DOverlay(base, cur, prev, color, true); // base cap
            prev = cur;
        }
    }

    // Filled flat ring (annulus) in the plane perpendicular to axis, used for the rotate handle.
    static void DrawSolidRingOverlay(DebugRenderer* dr, const vec3& center, const vec3& axis, f32 radius, f32 thickness, Color color, i32 segments)
    {
        vec3 u, v;
        PerpendicularBasis(glm::normalize(axis), u, v);
        f32 inner = radius - thickness;
        f32 outer = radius + thickness;

        vec3 prevDir = u;
        for (i32 i = 1; i <= segments; ++i)
        {
            f32 ang = (static_cast<f32>(i) / static_cast<f32>(segments)) * glm::two_pi<f32>();
            vec3 curDir = u * glm::cos(ang) + v * glm::sin(ang);

            vec3 a = center + prevDir * inner;
            vec3 b = center + prevDir * outer;
            vec3 c = center + curDir * outer;
            vec3 d = center + curDir * inner;
            dr->DrawTriangleSolid3DOverlay(a, b, c, color, true);
            dr->DrawTriangleSolid3DOverlay(a, c, d, color, true);
            prevDir = curDir;
        }
    }

    // Filled axis-aligned box (12 triangles), used for the scale handle tips.
    static void DrawBoxOverlay(DebugRenderer* dr, const vec3& center, f32 halfSize, Color color)
    {
        vec3 e = vec3(halfSize);
        vec3 c[8] = {
            center + vec3(-e.x, -e.y, -e.z), center + vec3( e.x, -e.y, -e.z),
            center + vec3( e.x,  e.y, -e.z), center + vec3(-e.x,  e.y, -e.z),
            center + vec3(-e.x, -e.y,  e.z), center + vec3( e.x, -e.y,  e.z),
            center + vec3( e.x,  e.y,  e.z), center + vec3(-e.x,  e.y,  e.z),
        };
        static const i32 faces[6][4] = {
            {0,1,2,3}, {5,4,7,6}, {4,0,3,7}, {1,5,6,2}, {3,2,6,7}, {4,5,1,0}
        };
        for (i32 f = 0; f < 6; ++f)
        {
            dr->DrawTriangleSolid3DOverlay(c[faces[f][0]], c[faces[f][1]], c[faces[f][2]], color, true);
            dr->DrawTriangleSolid3DOverlay(c[faces[f][0]], c[faces[f][2]], c[faces[f][3]], color, true);
        }
    }

    static void NotifySelectionChanged()
    {
        Scripting::LuaManager* luaManager = ServiceLocator::GetLuaManager();
        Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        if (!luaManager || !zenith)
            return;

        auto* handler = luaManager->GetLuaHandler<Scripting::Editor::EditorToolHandler>(static_cast<u16>(MetaGen::Game::Lua::LuaHandlerTypeEnum::Editor));
        if (handler)
            handler->OnSelectionChanged(zenith);
    }

    static void NotifyGizmoChanged()
    {
        Scripting::LuaManager* luaManager = ServiceLocator::GetLuaManager();
        Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        if (!luaManager || !zenith)
            return;

        auto* handler = luaManager->GetLuaHandler<Scripting::Editor::EditorToolHandler>(static_cast<u16>(MetaGen::Game::Lua::LuaHandlerTypeEnum::Editor));
        if (handler)
            handler->OnGizmoChanged(zenith);
    }

    void EditorTools::SetSelectedEntity(entt::registry& registry, entt::entity entity)
    {
        entt::registry::context& ctx = registry.ctx();
        if (!ctx.contains<Singletons::EditorSelection>())
            return;

        auto& selection = ctx.get<Singletons::EditorSelection>();
        if (selection.selectedEntity == entity)
            return;

        selection.selectedEntity = entity;
        NotifySelectionChanged();
    }

    void EditorTools::Update(entt::registry& registry, f32 /*deltaTime*/)
    {
        entt::registry::context& ctx = registry.ctx();
        if (!ctx.contains<Singletons::EditorSelection>())
            return;

        auto& selection = ctx.get<Singletons::EditorSelection>();

        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        if (!gameRenderer)
            return;
        DebugRenderer* debugRenderer = gameRenderer->GetDebugRenderer();

        // ---- Delete the selected entity (Delete key) ----
        if (s.deleteSelectedRequested)
        {
            s.deleteSelectedRequested = false;
            if (selection.selectedEntity != entt::null && registry.valid(selection.selectedEntity))
            {
                entt::entity toDelete = selection.selectedEntity;
                if (ECS::Components::Model* model = registry.try_get<ECS::Components::Model>(toDelete))
                    gameRenderer->GetModelLoader()->UnloadModelForEntity(toDelete, *model);

                registry.destroy(toDelete);
                SetSelectedEntity(registry, entt::null);
            }
        }

        entt::entity selected = selection.selectedEntity;
        bool hasSelection = selected != entt::null && registry.valid(selected);

        // ---- Camera ----
        if (!ctx.contains<Singletons::ActiveCamera>())
            return;
        entt::entity cameraEntity = ctx.get<Singletons::ActiveCamera>().entity;
        if (cameraEntity == entt::null)
            return;
        ECS::Components::Camera* camera = registry.try_get<ECS::Components::Camera>(cameraEntity);
        ECS::Components::Transform* cameraTransform = registry.try_get<ECS::Components::Transform>(cameraEntity);
        if (!camera || !cameraTransform)
            return;

        mat4x4 viewProj = camera->viewToClip * camera->worldToView;
        mat4x4 invViewProj = glm::inverse(viewProj);
        vec2 renderSize = gameRenderer->GetRenderer()->GetRenderSize();
        vec3 cameraPos = cameraTransform->GetWorldPosition();

        // ---- Input state ----
        InputManager* inputManager = ServiceLocator::GetInputManager();
        vec2 mouse = inputManager->GetMousePosition();
        bool mouseDown = inputManager->IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        bool mousePressed = mouseDown && !s.prevMouseDown;
        bool mouseReleased = !mouseDown && s.prevMouseDown;

        // Block picking/gizmo when the cursor is over any interactable UI widget. This mirrors the
        // UI keybind group's own consume rule (allHoveredEntities non-empty), rather than hoveredEntity
        // which is only set for widgets with hover/mouse effects and so leaks clicks on inert panels.
        bool overUI = false;
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        if (registries && registries->uiRegistry)
        {
            auto& uiCtx = registries->uiRegistry->ctx();
            if (uiCtx.contains<Singletons::UISingleton>())
                overUI = !uiCtx.get<Singletons::UISingleton>().allHoveredEntities.empty();
        }

        // ---- Drag-spawn from the Asset Browser ----
        // Lua flips dragSpawnRequested on mouse-down on a model; from there we own the drag globally
        // via the raw mouse state (independent of UI hover): spawn once the cursor is over the world,
        // follow the cursor (world raycast, ignoring the dragged model's own collider), render at 50%
        // alpha, scroll-rotate around Y, and drop + select on release. While active it suppresses the
        // normal picking/gizmo below.
        if (selection.dragSpawnRequested)
        {
            selection.dragSpawnRequested = false;
            s.dragSpawnActive = true;
            s.dragSpawnEntity = entt::null;
            s.dragSpawnPath = selection.dragSpawnModelPath;
            s.dragSpawnYaw = 0.0f;
            s.scrollAccum = 0.0f;
            s.dragSpawnTransparencyApplied = false;
        }

        bool dragSpawnHandledThisFrame = s.dragSpawnActive;
        if (s.dragSpawnActive)
        {
            ModelLoader* modelLoader = gameRenderer->GetModelLoader();

            if (!mouseDown)
            {
                // Drop: keep the placed object (opaque + selected); cancel cleanly if never spawned.
                if (s.dragSpawnEntity != entt::null && registry.valid(s.dragSpawnEntity))
                {
                    modelLoader->SetEntityTransparent(s.dragSpawnEntity, false, 1.0f);
                    if (ECS::Components::Model* model = registry.try_get<ECS::Components::Model>(s.dragSpawnEntity))
                    {
                        model->flags.forcedTransparency = false;
                        model->opacity = 1.0f;
                    }
                    SetSelectedEntity(registry, s.dragSpawnEntity);
                }
                s.dragSpawnActive = false;
                s.dragSpawnEntity = entt::null;
                s.scrollAccum = 0.0f;
            }
            else if (!overUI)
            {
                vec3 rayOrigin, rayDir;
                ScreenPointToRay(invViewProj, renderSize, mouse, rayOrigin, rayDir);
                // Cast from the camera through the cursor's unprojected point (matches
                // Util::Physics::GetMouseWorldPosition). ScreenPointToRay's direction is built for the
                // gizmo's line math and points the wrong way under the engine's reverse-Z, which made
                // the physics cast miss and the object fall to the y=0 plane.
                vec3 castDir = glm::normalize(rayOrigin - cameraPos);

                // Ignore the dragged model's own collision body so the ray doesn't hit it and creep up.
                u32 ignoreBody = std::numeric_limits<u32>::max();
                if (s.dragSpawnEntity != entt::null)
                {
                    if (ECS::Components::Model* model = registry.try_get<ECS::Components::Model>(s.dragSpawnEntity))
                    {
                        u32 bodyID;
                        if (model->instanceID != std::numeric_limits<u32>::max() && modelLoader->GetBodyIDFromInstanceID(model->instanceID, bodyID))
                            ignoreBody = bodyID;
                    }
                }

                vec3 hitPos;
                bool hasPos = Util::Physics::CastRayWorld(cameraPos, castDir, ignoreBody, hitPos);
                if (!hasPos)
                    hasPos = RayPlaneIntersect(vec3(0.0f), vec3(0.0f, 1.0f, 0.0f), cameraPos, castDir, hitPos);

                if (hasPos)
                {
                    if (s.dragSpawnEntity == entt::null)
                        s.dragSpawnEntity = Scripting::Asset::AssetHandler::CreateModelAtPosition(s.dragSpawnPath, hitPos);
                    else
                        ECS::TransformSystem::Get(registry).SetWorldPosition(s.dragSpawnEntity, hitPos);
                }

                if (s.dragSpawnEntity != entt::null)
                {
                    // Apply 50% transparency once the instance exists (instanceID is assigned a few
                    // frames after the load request; before that the model isn't rendered anyway).
                    if (!s.dragSpawnTransparencyApplied)
                    {
                        if (ECS::Components::Model* model = registry.try_get<ECS::Components::Model>(s.dragSpawnEntity))
                        {
                            if (model->instanceID != std::numeric_limits<u32>::max())
                            {
                                modelLoader->SetEntityTransparent(s.dragSpawnEntity, true, 0.5f);
                                model->flags.forcedTransparency = true;
                                model->opacity = 0.5f;
                                s.dragSpawnTransparencyApplied = true;
                            }
                        }
                    }

                    if (s.scrollAccum != 0.0f)
                    {
                        s.dragSpawnYaw += s.scrollAccum * glm::radians(15.0f);
                        s.scrollAccum = 0.0f;
                    }
                    ECS::TransformSystem::Get(registry).SetWorldRotation(s.dragSpawnEntity, glm::angleAxis(s.dragSpawnYaw, vec3(0.0f, 1.0f, 0.0f)));
                }
            }
        }

        // ---- Resolve any outstanding pixel query (entity picking) ----
        PixelQuery* pixelQuery = gameRenderer->GetPixelQuery();
        if (pixelQuery && !dragSpawnHandledThisFrame)
        {
            bool hasNewSelection = false;
            if (s.queriedToken != 0)
            {
                PixelQuery::PixelData pixelData;
                if (pixelQuery->GetQueryResult(s.queriedToken, pixelData))
                {
                    if (s.activeToken != 0)
                    {
                        pixelQuery->FreeToken(s.activeToken);
                        s.activeToken = 0;
                    }

                    if (pixelData.type == PickNone)
                    {
                        pixelQuery->FreeToken(s.queriedToken);
                        s.queriedToken = 0;
                    }
                    else
                    {
                        s.activeToken = s.queriedToken;
                        s.queriedToken = 0;
                        hasNewSelection = true;
                    }
                }
            }

            if (s.activeToken != 0 && hasNewSelection)
            {
                PixelQuery::PixelData pixelData;
                if (pixelQuery->GetQueryResult(s.activeToken, pixelData))
                {
                    if (pixelData.type == PickModelOpaque || pixelData.type == PickModelTransparent)
                    {
                        entt::entity picked;
                        if (gameRenderer->GetModelLoader()->GetEntityIDFromInstanceID(pixelData.value, picked))
                        {
                            SetSelectedEntity(registry, picked);
                        }
                    }
                    // Terrain has no entity, so it doesn't change the selection.
                }
            }
        }

        // ---- Gizmo ----
        bool startedGizmoDrag = false;
        ECS::Components::Transform* selectedTransform = hasSelection ? registry.try_get<ECS::Components::Transform>(selected) : nullptr;
        if (selection.gizmoEnabled && selectedTransform && debugRenderer && !dragSpawnHandledThisFrame)
        {
            vec3 pivot = selectedTransform->GetWorldPosition();

            // Constant screen size: scale with camera distance so the gizmo looks the same regardless
            // of how far the object is, and never depends on the object's bounds. Drawn as an overlay
            // (below) so it's always visible.
            f32 gizmoSize = glm::max(glm::distance(cameraPos, pivot) * GIZMO_SCREEN_SCALE, 0.001f);

            bool useLocalAxes = (selection.gizmoMode == Singletons::GizmoMode::Local) || (selection.gizmoOperation == Singletons::GizmoOperation::Scale);
            quat basis = useLocalAxes ? selectedTransform->GetWorldRotation() : quat(1.0f, 0.0f, 0.0f, 0.0f);
            vec3 axes[3] = { basis * vec3(1, 0, 0), basis * vec3(0, 1, 0), basis * vec3(0, 0, 1) };
            Color colors[3] = { Color::Red, Color::Green, Color::Blue };

            // Handle dimensions, shared by drawing and picking so the clickable volume matches the
            // drawn geometry (thin shaft, flared cone / cube at the tip).
            const f32 headLen = gizmoSize * 0.3f;
            const f32 headRadius = gizmoSize * 0.1f;
            const f32 shaftRadius = gizmoSize * 0.025f;
            const f32 shaftLen = gizmoSize - headLen;
            const f32 scaleCubeHalf = gizmoSize * 0.07f;

            // Begin drag — pick the axis by testing the mouse ray against the actual handle geometry
            // in world space (axis segment for translate/scale, ring plane for rotate), so the
            // clickable area matches what's drawn (the old screen-space line missed the wide cone base).
            if (!s.dragging && mousePressed && !overUI)
            {
                vec3 rayOrigin, rayDir;
                ScreenPointToRay(invViewProj, renderSize, mouse, rayOrigin, rayDir);

                i32 bestAxis = -1;
                if (selection.gizmoOperation == Singletons::GizmoOperation::Rotate)
                {
                    f32 bestErr = gizmoSize * 0.2f; // tolerance band around the ring radius
                    for (i32 i = 0; i < 3; ++i)
                    {
                        vec3 hit;
                        if (RayPlaneIntersect(pivot, axes[i], rayOrigin, rayDir, hit))
                        {
                            f32 err = glm::abs(glm::distance(hit, pivot) - gizmoSize);
                            if (err < bestErr)
                            {
                                bestErr = err;
                                bestAxis = i;
                            }
                        }
                    }
                }
                else
                {
                    // Radius profile along the handle, matching the drawn geometry: thin shaft, then
                    // the flared cone (translate) or the cube (scale) at the tip. A small slack keeps
                    // the thin parts grabbable.
                    bool isScale = selection.gizmoOperation == Singletons::GizmoOperation::Scale;
                    f32 slack = gizmoSize * 0.02f;
                    f32 bestScore = -1.0f; // smallest (dist - radius), most negative is the best hit
                    for (i32 i = 0; i < 3; ++i)
                    {
                        f32 segParam;
                        f32 dist = RaySegmentDistance(rayOrigin, rayDir, pivot, pivot + axes[i] * gizmoSize, segParam);
                        f32 along = segParam * gizmoSize; // world units from pivot

                        f32 radius;
                        if (isScale)
                        {
                            radius = (along >= gizmoSize - scaleCubeHalf * 2.0f) ? (scaleCubeHalf * 1.5f) : shaftRadius;
                        }
                        else if (along <= shaftLen)
                        {
                            radius = shaftRadius;
                        }
                        else
                        {
                            f32 f = (along - shaftLen) / headLen;          // 0 at cone base .. 1 at apex
                            radius = glm::max(shaftRadius, headRadius * (1.0f - f));
                        }
                        radius += slack;

                        if (dist < radius && (bestAxis < 0 || dist - radius < bestScore))
                        {
                            bestScore = dist - radius;
                            bestAxis = i;
                        }
                    }
                }

                if (bestAxis >= 0)
                {
                    s.dragging = true;
                    startedGizmoDrag = true;
                    s.dragAxis = bestAxis;
                    s.startPos = pivot;
                    s.startScale = selectedTransform->GetLocalScale();
                    s.startRot = selectedTransform->GetWorldRotation();
                    s.dragRefLength = gizmoSize;
                    for (i32 i = 0; i < 3; ++i)
                        s.axisDir[i] = axes[i];

                    // Capture where the mouse ray starts along/around the picked axis, so the drag
                    // can be solved purely in world space against the ray (no screen-space drift).
                    const vec3& axis = s.axisDir[bestAxis];
                    if (selection.gizmoOperation == Singletons::GizmoOperation::Rotate)
                    {
                        vec3 hit;
                        if (RayPlaneIntersect(pivot, axis, rayOrigin, rayDir, hit))
                        {
                            vec3 u, v;
                            PerpendicularBasis(axis, u, v);
                            vec3 radial = hit - pivot;
                            s.dragStartAngle = glm::atan(glm::dot(radial, v), glm::dot(radial, u));
                        }
                    }
                    else
                    {
                        f32 t;
                        if (ClosestAxisParam(pivot, axis, rayOrigin, rayDir, t))
                            s.dragStartAxisT = t;
                    }
                }
            }

            // Continue drag — solved in world space against the current mouse ray. The axis line /
            // rotation plane pass through the press-time position (s.startPos), so applying the
            // result each frame doesn't feed back into the solve.
            bool draggedThisFrame = s.dragging && mouseDown && s.dragAxis >= 0;
            vec3 debugPos = vec3(0);
            if (draggedThisFrame)
            {
                i32 a = s.dragAxis;
                const vec3& axis = s.axisDir[a];

                vec3 rayOrigin, rayDir;
                ScreenPointToRay(invViewProj, renderSize, mouse, rayOrigin, rayDir);

                if (selection.gizmoOperation == Singletons::GizmoOperation::Translate)
                {
                    f32 t;
                    if (ClosestAxisParam(s.startPos, axis, rayOrigin, rayDir, t))
                    {
                        debugPos = s.startPos + axis * (t - s.dragStartAxisT);
                        ECS::TransformSystem::Get(registry).SetWorldPosition(selected, s.startPos + axis * (t - s.dragStartAxisT));
                    }
                }
                else if (selection.gizmoOperation == Singletons::GizmoOperation::Scale)
                {
                    f32 t;
                    if (ClosestAxisParam(s.startPos, axis, rayOrigin, rayDir, t))
                    {
                        f32 factor = 1.0f + (t - s.dragStartAxisT) / s.dragRefLength;
                        vec3 newScale = s.startScale;
                        newScale[a] = glm::max(0.0001f, s.startScale[a] * factor);
                        ECS::TransformSystem::Get(registry).SetLocalScale(selected, newScale);
                    }
                }
                else // Rotate
                {
                    vec3 hit;
                    if (RayPlaneIntersect(s.startPos, axis, rayOrigin, rayDir, hit))
                    {
                        vec3 u, v;
                        PerpendicularBasis(axis, u, v);
                        vec3 radial = hit - s.startPos;
                        f32 angleNow = glm::atan(glm::dot(radial, v), glm::dot(radial, u));
                        quat delta = glm::angleAxis(angleNow - s.dragStartAngle, axis);
                        ECS::TransformSystem::Get(registry).SetWorldRotation(selected, delta * s.startRot);
                    }
                }

            }

            // Let the Inspector refresh its transform fields while the gizmo drags the object.
            if (draggedThisFrame)
                NotifyGizmoChanged();

            // Draw the handles from the post-drag transform so they track the object exactly instead
            // of lagging a frame behind it (the drag above already mutated the transform this frame).
            pivot = selectedTransform->GetWorldPosition();
            quat drawBasis = useLocalAxes ? selectedTransform->GetWorldRotation() : quat(1.0f, 0.0f, 0.0f, 0.0f);
            vec3 drawAxes[3] = { drawBasis * vec3(1, 0, 0), drawBasis * vec3(0, 1, 0), drawBasis * vec3(0, 0, 1) };

            if (debugPos == vec3(0))
                debugPos = pivot;

            for (i32 i = 0; i < 3; ++i)
            {
                vec3 tip = pivot + drawAxes[i] * gizmoSize;
                if (selection.gizmoOperation == Singletons::GizmoOperation::Rotate)
                {
                    DrawSolidRingOverlay(debugRenderer, pivot, drawAxes[i], gizmoSize, shaftRadius, colors[i], 48);
                }
                else if (selection.gizmoOperation == Singletons::GizmoOperation::Scale)
                {
                    DrawCylinderOverlay(debugRenderer, pivot, tip, shaftRadius, colors[i], 10);
                    DrawBoxOverlay(debugRenderer, tip, scaleCubeHalf, colors[i]);
                }
                else // Translate — cylinder shaft + arrowhead cone (ImGuizmo style)
                {
                    vec3 shaftEnd = pivot + drawAxes[i] * shaftLen;
                    DrawCylinderOverlay(debugRenderer, pivot, shaftEnd, shaftRadius, colors[i], 10);
                    DrawArrowHeadOverlay(debugRenderer, shaftEnd, drawAxes[i], headLen, headRadius, colors[i], 12);
                }
            }
        }

        if (mouseReleased || !mouseDown)
            s.dragging = false;

        // ---- Initiate a pick (only if the click wasn't consumed by the gizmo or a drag-spawn) ----
        if (pixelQuery && selection.pickingEnabled && mousePressed && !overUI && !startedGizmoDrag && !dragSpawnHandledThisFrame)
        {
            if (s.queriedToken != 0)
            {
                pixelQuery->FreeToken(s.queriedToken);
                s.queriedToken = 0;
            }
            s.queriedToken = pixelQuery->PerformQuery(uvec2(mouse));
        }

        s.prevMouseDown = mouseDown;

        // ---- Debug shapes for the selection (replaces the Inspector's CVar-driven draw) ----
        if (hasSelection && debugRenderer)
        {
            ECS::Components::Transform* transform = registry.try_get<ECS::Components::Transform>(selected);
            if (selection.drawOBB)
            {
                ECS::Components::AABB* aabb = registry.try_get<ECS::Components::AABB>(selected);
                if (transform && aabb)
                {
                    vec3 center = transform->GetWorldPosition() + transform->GetWorldRotation() * (aabb->centerPos * transform->GetLocalScale());
                    vec3 extents = aabb->extents * transform->GetLocalScale();
                    debugRenderer->DrawOBB3D(center, extents, transform->GetWorldRotation(), Color::Red);
                }
            }
            if (selection.drawWorldAABB)
            {
                ECS::Components::WorldAABB* worldAABB = registry.try_get<ECS::Components::WorldAABB>(selected); // TODO: This lags 1 frame behind, because the WorldAABB has not been updated by the UpdateAABBs::Update system yet
                if (worldAABB)
                {
                    vec3 center = (worldAABB->min + worldAABB->max) * 0.5f;
                    vec3 extents = (worldAABB->max - worldAABB->min) * 0.5f;
                    debugRenderer->DrawAABB3D(center, extents, Color::Green);
                }
            }
        }
    }
}
