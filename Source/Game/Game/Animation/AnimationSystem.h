#pragma once
#include <Base/Types.h>
#include <Base/Container/ConcurrentQueue.h>

#include <FileFormat/Novus/Model/ComplexModel.h>

#include <robinhood/robinhood.h>
#include <enkiTS/TaskScheduler.h>

#include <limits>

namespace Model
{
	struct ComplexModel;
}
class ModelRenderer;

namespace Animation
{
	using ModelID = u32;
	using InstanceID = u32;

	struct AnimationSkeleton
	{
	public:
		static constexpr ModelID InvalidID = std::numeric_limits<ModelID>().max();

		u32 modelID = InvalidID;

		std::vector<Model::ComplexModel::Bone> bones;
		std::vector<Model::ComplexModel::TextureTransform> textureTransforms;
		std::vector<Model::ComplexModel::AnimationSequence> sequences;
	};

	enum class AnimationPlayState : u32
	{
		STOPPED,
		ONESHOT,
		LOOPING
	};

	struct AnimationSequenceInfo
	{
	public:
		static constexpr u16 InvalidID = std::numeric_limits<u16>().max();

		u16 sequenceID = InvalidID;
		u16 translationIndex = InvalidID;
		u16 rotationIndex = InvalidID;
		u16 scaleIndex = InvalidID;
	};

	struct AnimationSequenceState
	{
	public:
		AnimationPlayState state = AnimationPlayState::STOPPED;
		f32 progress = 0.0f;

		AnimationSequenceInfo sequence;
	};
	struct AnimationSequenceTransition
	{
	public:
		AnimationSequenceState state;
		f32 progress = 0.0f;
	};

	struct AnimationBoneState
	{
	public:
		AnimationSequenceState primary;
		AnimationSequenceTransition transition;
	};

	struct AnimationInstance
	{
	public:
		static constexpr InstanceID InvalidID = std::numeric_limits<InstanceID>().max();

		ModelID modelID = AnimationSkeleton::InvalidID;

		u32 boneOffset = InvalidID;
		std::vector<AnimationBoneState> boneState;
	};

	struct AnimationStorage
	{
	public:
		robin_hood::unordered_map<ModelID, AnimationSkeleton> skeletons;

		std::atomic<u32> instancesIndex;
		std::vector<InstanceID> instanceIDs;
		robin_hood::unordered_map<InstanceID, AnimationInstance> instanceIDToData;

		std::atomic<u32> dirtyInstancesIndex;
		std::vector<InstanceID> dirtyInstances;

		std::atomic<u32> boneIndex;
		std::vector<mat4x4> boneMatrices;
	};

	class AnimationSystem
	{
	public:
		AnimationSystem(ModelRenderer* modelRenderer);

		bool HasSkeleton(ModelID modelID) { return _storage.skeletons.contains(modelID); }
		bool AddSkeleton(ModelID modelID, Model::ComplexModel& model);
		bool PlayAnimation(InstanceID instanceID, u16 sequenceID);

		bool HasInstance(InstanceID instanceID) { return _storage.instanceIDToData.contains(instanceID); }
		bool AddInstance(ModelID modelID, InstanceID instanceID);

		void Update(f32 deltaTime);
		
		void Reserve(u32 numSkeletons, u32 numInstances, u32 numBones);
		void FitToBuffersAfterLoad();
		void Clear();

	private:
		mat4x4 GetBoneMatrix(const AnimationSkeleton& skeleton, AnimationBoneState& animBone, const Model::ComplexModel::Bone& bone);
		mat4x4 HandleBoneAnimation(const AnimationSkeleton& skeleton, AnimationBoneState& animBone, const Model::ComplexModel::Bone& bone, f32 deltaTime);

		bool HasModelRenderer() { return _modelRenderer != nullptr; }

	private:
		AnimationStorage _storage;
		ModelRenderer* _modelRenderer = nullptr;

		enki::TaskScheduler _scheduler;
	};
}