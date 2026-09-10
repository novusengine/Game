#pragma once

#include "AnimationAssetReader.h"

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace Animation
{
    enum class AnimationDocumentType : u8 { Skeleton, RigFamily, Clip, Set, Mask, IK };

    struct AnimationDocumentField
    {
        std::string path;
        std::string value;
    };

    // Keeps editable asset metadata separate from the last validated binary payload.
    // Clip sample streams are shared by revisions rather than copied into undo history.
    class AnimationAssetDocument
    {
      public:
        AnimationAssetDocument();
        ~AnimationAssetDocument();
        bool Open(AnimationDocumentType type, std::span<const u8> payload, std::string& error);
        bool Edit(std::string_view path, std::string_view value, std::string& error);
        bool Insert(std::string_view path, std::string& error);
        bool Remove(std::string_view path, u32 index, std::string& error);
        bool PaintMask(std::span<const FileFormat::Animation::SkeletonJoint> joints, u32 joint, f32 weight, f32 falloff, bool subtree, std::string& error);
        bool MirrorMask(std::span<const std::pair<u32, u32>> pairs, std::string& error);
        bool Undo();
        bool Redo();
        void MarkSaved();
        void Revert();
        bool Compile(std::string& error);
        std::vector<u8> Serialize() const;
        bool LoadSource(std::span<const u8> bytes, std::string& error);
        std::vector<AnimationDocumentField> GetFields() const;
        const std::vector<u8>& GetPayload() const;
        AnimationDocumentType GetType() const;
        u64 GetRevision() const;
        bool IsDirty() const;

      private:
        struct Impl;
        std::unique_ptr<Impl> _impl;
    };
}
