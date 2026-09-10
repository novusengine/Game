#pragma once

#include "AnimationControllerRuntime.h"

#include <deque>

namespace Animation
{
    struct AnimationGraphField
    {
        std::string section;
        u32 index = 0;
        std::string name;
        std::string value;
    };
    // Holds an editor draft independently of the graph used by live controllers.
    // Undo history belongs to the document, never to an animator.
    class AnimationGraphDocument
    {
      public:
        explicit AnimationGraphDocument(EditableAnimationGraph source);
        const EditableAnimationGraph& GetSource() const { return _source; }
        const GraphCompileResult& GetCompilation() const { return _compilation; }
        const std::shared_ptr<const CompiledAnimationGraph>& GetLastGoodGraph() const { return _lastGoodGraph; }
        u64 GetRevision() const { return _revision; }
        bool IsDirty() const { return _revision != _savedRevision; }
        bool CanUndo() const { return !_undo.empty(); }
        bool CanRedo() const { return !_redo.empty(); }
        void Edit(EditableAnimationGraph source);
        bool Undo();
        bool Redo();
        void MarkSaved();
        void Revert();
        bool Compile();
        bool EditField(std::string_view section, u32 index, std::string_view field, std::string_view value, std::string& error);
        std::vector<AnimationGraphField> GetFields() const;
        bool DuplicateItem(std::string_view section, u32 index, std::string& error);
        bool RemoveItem(std::string_view section, u32 index, std::string& error);
        static std::vector<u8> Serialize(const EditableAnimationGraph& source);
        static bool Parse(std::span<const u8> bytes, EditableAnimationGraph& source, std::string& error);

      private:
        struct Revision
        {
            EditableAnimationGraph source;
            u64 id = 0;
        };
        void Push(std::deque<Revision>& history);
        EditableAnimationGraph _source;
        EditableAnimationGraph _saved;
        GraphCompileResult _compilation;
        std::shared_ptr<const CompiledAnimationGraph> _lastGoodGraph;
        std::deque<Revision> _undo;
        std::deque<Revision> _redo;
        u64 _revision = 1;
        u64 _nextRevision = 2;
        u64 _savedRevision = 1;
    };
}
