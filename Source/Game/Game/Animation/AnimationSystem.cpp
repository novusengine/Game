#include "AnimationSystem.h"
#include "Game/Rendering/Model/ModelRenderer.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <FileFormat/Shared.h>

AutoCVar_Int CVAR_AnimationSystemEnabled("animationSystem.enabled", "Enables the Animation System", 0, CVarFlags::EditCheckbox);
AutoCVar_Float CVAR_AnimationSystemTimeScale("animationSystem.timeScale", "Controls the global speed of all animations", 1.0f);
AutoCVar_Int CVAR_AnimationSystemThrottle("animationSystem.throttle", "Sets the number of dirty instances that can be updated every frame", 64);

namespace Animation
{
	glm::mat4 mul(const glm::mat4& matrix1, const glm::mat4& matrix2)
	{
		return matrix2 * matrix1;
	}

	mat4x4 MatrixTranslate(const vec3& v)
	{
		mat4x4 result =
		{
			1, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 1, 0,
			v[0], v[1], v[2], 1
		};

		return result;
	}

	mat4x4 MatrixRotation(const quat& quat)
	{
		f32 x2 = quat.x * quat.x;
		f32 y2 = quat.y * quat.y;
		f32 z2 = quat.z * quat.z;
		f32 sx = quat.w * quat.x;
		f32 sy = quat.w * quat.y;
		f32 sz = quat.w * quat.z;
		f32 xz = quat.x * quat.z;
		f32 yz = quat.y * quat.z;
		f32 xy = quat.x * quat.y;

		return mat4x4(1.0f - 2.0f * (y2 + z2), 2.0f * (xy + sz), 2.0f * (xz - sy), 0.0f,
			2.0f * (xy - sz), 1.0f - 2.0f * (x2 + z2), 2.0f * (sx + yz), 0.0f,
			2.0f * (sy + xz), 2.0f * (yz - sx), 1.0f - 2.0f * (x2 + y2), 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f);
	}

	mat4x4 MatrixScale(const vec3& v)
	{
		mat4x4 result = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
		result[0] = result[0] * v[0];
		result[1] = result[1] * v[1];
		result[2] = result[2] * v[2];

		return result;
	}

	template <typename T>
	T InterpolateKeyframe(const Model::ComplexModel::AnimationTrack<T>& track, f32 progress)
	{
		u32 numTimeStamps = static_cast<u32>(track.timestamps.size());
		if (numTimeStamps == 1)
		{
			return track.values[0];
		}

		u32 progressInMS = static_cast<u32>(progress * 1000.0f);

		u32 timestamp1 = track.timestamps[0];
		u32 timestamp2 = track.timestamps[1];
		T value1 = track.values[0];
		T value2 = track.values[1];

		for (u32 i = 1; i < numTimeStamps; i++)
		{
			u32 timestamp = track.timestamps[i];

			if (progressInMS < timestamp)
			{
				timestamp1 = track.timestamps[i - 1];
				timestamp2 = track.timestamps[i];

				value1 = track.values[i - 1];
				value2 = track.values[i];
				break;
			}
		}

		f32 currentProgress = static_cast<f32>((progressInMS - timestamp1));
		f32 currentFrameDuration = static_cast<f32>((timestamp2 - timestamp1));
		f32 t = currentProgress / currentFrameDuration;

		if constexpr (std::is_same_v<T, quat>)
		{
			return glm::slerp(value1, value2, t);
		}
		else
		{
			return glm::mix(value1, value2, t);
		}
	}

	AnimationSystem::AnimationSystem(ModelRenderer* modelRenderer) : _modelRenderer(modelRenderer)
	{
		_scheduler.Initialize();
	}

	bool AnimationSystem::AddSkeleton(ModelID modelID, Model::ComplexModel& model)
	{
		bool isEnabled = CVAR_AnimationSystemEnabled.Get();
		if (!isEnabled)
		{
			return false;
		}

		if (HasSkeleton(modelID))
		{
			return false;
		}

		u32 numBones = static_cast<u32>(model.bones.size());
		u32 numTextureTransforms = static_cast<u32>(model.textureTransforms.size());
		u32 numSequences = static_cast<u32>(model.sequences.size());

		bool hasAnimations = numBones > 0 || numTextureTransforms > 0 || numSequences > 0;
		if (!hasAnimations)
		{
			return false;
		}

		AnimationSkeleton& skeleton = _storage.skeletons[modelID];
		skeleton.modelID = modelID;

		if (numBones)
		{
			skeleton.bones.resize(numBones);

			for (u32 i = 0; i < numBones; i++)
			{
				const Model::ComplexModel::Bone& bone = model.bones[i];
				Model::ComplexModel::Bone& animBone = skeleton.bones[i];

				animBone = bone;
			}
		}

		if (numTextureTransforms)
		{
			skeleton.textureTransforms.resize(numTextureTransforms);
			memcpy(skeleton.textureTransforms.data(), model.textureTransforms.data(), numTextureTransforms * sizeof(Model::ComplexModel::TextureTransform));
		}

		if (numSequences)
		{
			skeleton.sequences.resize(numSequences);
			memcpy(skeleton.sequences.data(), model.sequences.data(), numSequences * sizeof(Model::ComplexModel::AnimationSequence));
		}

		return true;
	}
	bool AnimationSystem::AddInstance(ModelID modelID, InstanceID instanceID)
	{
		bool isEnabled = CVAR_AnimationSystemEnabled.Get();
		if (!isEnabled)
		{
			return false;
		}

		if (HasInstance(instanceID))
		{
			return false;
		}

		if (!HasSkeleton(modelID))
		{
			return false;
		}

		const AnimationSkeleton& skeleton = _storage.skeletons[modelID];
		u32 numBones = static_cast<u32>(skeleton.bones.size());

		AnimationInstance& instance = _storage.instanceIDToData[instanceID];
		instance.modelID = modelID;

		instance.boneOffset = _storage.boneIndex.fetch_add(numBones);
		instance.boneState.resize(numBones);

		u32 instanceIndex = _storage.instancesIndex.fetch_add(1);
		_storage.instanceIDs[instanceIndex] = instanceID;

		if (HasModelRenderer())
		{
			_modelRenderer->AddAnimationInstance(instanceID);
		}

		return true;
	}
	bool AnimationSystem::PlayAnimation(InstanceID instanceID, u16 sequenceID)
	{
		bool isEnabled = CVAR_AnimationSystemEnabled.Get();
		if (!isEnabled)
		{
			return false;
		}

		if (!HasInstance(instanceID))
		{
			return false;
		}

		AnimationInstance& instance = _storage.instanceIDToData[instanceID];
		const AnimationSkeleton& skeleton = _storage.skeletons[instance.modelID];

		u32 numBones = static_cast<u32>(skeleton.bones.size());
		if (numBones > 0)
		{
			for (u32 i = 0; i < numBones; i++)
			{
				const Model::ComplexModel::Bone& bone = skeleton.bones[i];
				AnimationBoneState& animBone = instance.boneState[i];

				animBone.primary.state = AnimationPlayState::ONESHOT;
				animBone.primary.progress = 0.0f;

				u32 numTranslationTracks = static_cast<u32>(bone.translation.tracks.size());
				u32 translationIndex = AnimationSequenceInfo::InvalidID;
				for (u32 j = 0; j < numTranslationTracks; j++)
				{
					const Model::ComplexModel::AnimationTrack<vec3>& track = bone.translation.tracks[j];

					if (track.sequenceID == sequenceID)
					{
						translationIndex = j;
						break;
					}
				}

				u32 numRotationTracks = static_cast<u32>(bone.rotation.tracks.size());
				u32 rotationIndex = AnimationSequenceInfo::InvalidID;
				for (u32 j = 0; j < numRotationTracks; j++)
				{
					const Model::ComplexModel::AnimationTrack<quat>& track = bone.rotation.tracks[j];

					if (track.sequenceID == sequenceID)
					{
						rotationIndex = j;
						break;
					}
				}

				u32 numScaleTracks = static_cast<u32>(bone.scale.tracks.size());
				u32 scaleIndex = AnimationSequenceInfo::InvalidID;
				for (u32 j = 0; j < numScaleTracks; j++)
				{
					const Model::ComplexModel::AnimationTrack<vec3>& track = bone.scale.tracks[j];

					if (track.sequenceID == sequenceID)
					{
						scaleIndex = j;
						break;
					}
				}

				bool hasValidTrackForSequence = (translationIndex != AnimationSequenceInfo::InvalidID) || (rotationIndex != AnimationSequenceInfo::InvalidID) || (scaleIndex != AnimationSequenceInfo::InvalidID);
				animBone.primary.sequence.sequenceID = (sequenceID * hasValidTrackForSequence) + (AnimationSequenceInfo::InvalidID * !hasValidTrackForSequence);
				animBone.primary.sequence.translationIndex = (translationIndex * hasValidTrackForSequence) + (AnimationSequenceInfo::InvalidID * !hasValidTrackForSequence);
				animBone.primary.sequence.rotationIndex = (rotationIndex * hasValidTrackForSequence) + (AnimationSequenceInfo::InvalidID * !hasValidTrackForSequence);
				animBone.primary.sequence.scaleIndex = (scaleIndex * hasValidTrackForSequence) + (AnimationSequenceInfo::InvalidID * !hasValidTrackForSequence);
				animBone.primary.state = (hasValidTrackForSequence) ? AnimationPlayState::LOOPING : AnimationPlayState::STOPPED;
			}
		}

		return true;
	}

	void AnimationSystem::Update(f32 deltaTime)
	{
		bool isEnabled = CVAR_AnimationSystemEnabled.Get();
		if (!isEnabled)
		{
			return;
		}

		u32 numInstances = static_cast<u32>(_storage.instanceIDs.size());
		enki::TaskSet updateAnimationsTask(numInstances, [&](enki::TaskSetPartition range, u32 threadNum)
		{
			bool hasRenderer = HasModelRenderer();

			for (u32 i = range.start; i < range.end; i++)
			{
				InstanceID instanceID = _storage.instanceIDs[i];
				AnimationInstance& instance = _storage.instanceIDToData[instanceID];
				const AnimationSkeleton& skeleton = _storage.skeletons[instance.modelID];
		
				u32 numBones = static_cast<u32>(instance.boneState.size());
		
				bool isInstanceDirty = false;

				for (u32 boneIndex = 0; boneIndex < numBones; boneIndex++)
				{
					const Model::ComplexModel::Bone& bone = skeleton.bones[boneIndex];
					AnimationBoneState& animBone = instance.boneState[boneIndex];
		
					mat4x4 originalMatrix = _storage.boneMatrices[instance.boneOffset + boneIndex];
					mat4x4 boneMatrix = HandleBoneAnimation(skeleton, animBone, bone, deltaTime);
		
					// Apply parent's transformation
					if (bone.parentBoneID != -1)
					{
						boneMatrix = mul(boneMatrix, _storage.boneMatrices[instance.boneOffset + bone.parentBoneID]);
					}
					
					bool isDirty = originalMatrix != boneMatrix;
					if (isDirty)
					{
						// Store final transformation
						isInstanceDirty = true;
						_storage.boneMatrices[instance.boneOffset + boneIndex] = boneMatrix;
					}
				}

				if (hasRenderer && isInstanceDirty)
				{
					u32 dirtyIndex = _storage.dirtyInstancesIndex.fetch_add(1);
					_storage.dirtyInstances[dirtyIndex] = instanceID;
				}
			}
		});
		
		_scheduler.AddTaskSetToPipe(&updateAnimationsTask);
		_scheduler.WaitforTask(&updateAnimationsTask);

		bool hasRenderer = HasModelRenderer();
		if (hasRenderer)
		{
			u32 throttle = CVAR_AnimationSystemThrottle.Get();

			u32 numDirty = _storage.dirtyInstancesIndex.load();
			u32 numInstancesToUpdate = glm::min(numDirty, throttle);
			
			for (u32 i = 0; i < numInstancesToUpdate; i++)
			{
				InstanceID instanceID = _storage.dirtyInstances[i];
				AnimationInstance& instance = _storage.instanceIDToData[instanceID];

 				u32 numBones = static_cast<u32>(instance.boneState.size());
				_modelRenderer->SetBoneMatricesAsDirty(instanceID, 0, numBones, &_storage.boneMatrices[instance.boneOffset]);
			}
		}

		_storage.dirtyInstancesIndex.store(0);
	}

	void AnimationSystem::Reserve(u32 numSkeletons, u32 numInstances, u32 numBones)
	{
		bool isEnabled = CVAR_AnimationSystemEnabled.Get();
		if (!isEnabled)
		{
			return;
		}

		u32 currentNumSkeletons = static_cast<u32>(_storage.skeletons.size());
		_storage.skeletons.reserve(currentNumSkeletons + numSkeletons);

		u32 currentNumInstances = static_cast<u32>(_storage.instanceIDs.size());
		_storage.instanceIDs.resize(currentNumInstances + numInstances);
		_storage.instanceIDToData.reserve(currentNumInstances + numInstances);
		_storage.dirtyInstances.resize(currentNumInstances + numInstances);

		u32 currentNumBones = static_cast<u32>(_storage.boneMatrices.size());
		_storage.boneMatrices.resize(currentNumBones + numBones);
	}

	void AnimationSystem::FitToBuffersAfterLoad()
	{
		bool isEnabled = CVAR_AnimationSystemEnabled.Get();
		if (!isEnabled)
		{
			return;
		}

		u32 numInstances = _storage.instancesIndex.load();

		_storage.instanceIDs.resize(numInstances);
		_storage.dirtyInstances.resize(numInstances);
	}

	void AnimationSystem::Clear()
	{
		bool isEnabled = CVAR_AnimationSystemEnabled.Get();
		if (!isEnabled)
		{
			return;
		}

		_storage.skeletons.clear();

		_storage.instancesIndex.store(0);
		_storage.instanceIDs.clear();
		_storage.instanceIDToData.clear();

		_storage.dirtyInstancesIndex.store(0);
		_storage.dirtyInstances.clear();

		_storage.boneIndex.store(0);
		_storage.boneMatrices.clear();
	}

	mat4x4 AnimationSystem::GetBoneMatrix(const AnimationSkeleton& skeleton, AnimationBoneState& animBone, const Model::ComplexModel::Bone& bone)
	{
		mat4x4 boneMatrix = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
		vec3 translationValue = vec3(0.f, 0.f, 0.f);
		quat rotationValue = quat(1.f, 0.f, 0.f, 0.f);
		vec3 scaleValue = vec3(1.f, 1.f, 1.f);

		// Primary Sequence
		{
			const AnimationSequenceState& state = animBone.primary;

			u16 translationIndex = state.sequence.translationIndex;
			if (translationIndex != AnimationSequenceInfo::InvalidID)
			{
				const Model::ComplexModel::AnimationTrack<vec3>& track = bone.translation.tracks[translationIndex];
				translationValue = InterpolateKeyframe(track, state.progress);
			}

			u16 rotationIndex = state.sequence.rotationIndex;
			if (rotationIndex != AnimationSequenceInfo::InvalidID)
			{
				const Model::ComplexModel::AnimationTrack<quat>& track = bone.rotation.tracks[rotationIndex];
				rotationValue = InterpolateKeyframe(track, state.progress);
			}

			u16 scaleIndex = state.sequence.scaleIndex;
			if (scaleIndex != AnimationSequenceInfo::InvalidID)
			{
				const Model::ComplexModel::AnimationTrack<vec3>& track = bone.scale.tracks[scaleIndex];
				scaleValue = InterpolateKeyframe(track, state.progress);
			}
		}

		// Transition Sequence
		{
			const AnimationSequenceState& state = animBone.transition.state;
			f32 transitionProgress = glm::clamp(animBone.transition.progress / 0.15f, 0.0f, 1.0f);

			u16 translationIndex = state.sequence.translationIndex;
			if (translationIndex != AnimationSequenceInfo::InvalidID)
			{
				const Model::ComplexModel::AnimationTrack<vec3>& track = bone.translation.tracks[translationIndex];
				vec3 translation = InterpolateKeyframe(track, state.progress);

				translationValue = glm::mix(translationValue, translation, transitionProgress);
			}

			u16 rotationIndex = state.sequence.rotationIndex;
			if (rotationIndex != AnimationSequenceInfo::InvalidID)
			{
				const Model::ComplexModel::AnimationTrack<quat>& track = bone.rotation.tracks[rotationIndex];
				quat rotation = InterpolateKeyframe(track, state.progress);

				rotationValue = glm::mix(rotationValue, rotation, transitionProgress);
			}

			u16 scaleIndex = state.sequence.scaleIndex;
			if (scaleIndex != AnimationSequenceInfo::InvalidID)
			{
				const Model::ComplexModel::AnimationTrack<vec3>& track = bone.scale.tracks[scaleIndex];
				vec3 scale = InterpolateKeyframe(track, state.progress);

				scaleValue = glm::mix(scaleValue, scale, transitionProgress);
			}
		}

		const vec3& pivot = bone.pivot;
		boneMatrix = mul(glm::translate(mat4x4(1.0f), pivot), boneMatrix);

		mat4x4 translationMatrix = glm::translate(mat4x4(1.0f), translationValue);
		mat4x4 rotationMatrix = glm::toMat4(glm::normalize(rotationValue));
		mat4x4 scaleMatrix = glm::scale(mat4x4(1.0f), scaleValue);

		boneMatrix = mul(translationMatrix, boneMatrix);
		boneMatrix = mul(rotationMatrix, boneMatrix);
		boneMatrix = mul(scaleMatrix, boneMatrix);

		boneMatrix = mul(glm::translate(mat4x4(1.0f), -pivot), boneMatrix);

		return boneMatrix;
	}

	mat4x4 AnimationSystem::HandleBoneAnimation(const AnimationSkeleton& skeleton, AnimationBoneState& animBone, const Model::ComplexModel::Bone& bone, f32 deltaTime)
	{
		u16 primarySequenceID = animBone.primary.sequence.sequenceID;
		if (primarySequenceID != AnimationSequenceInfo::InvalidID)
		{
			if (animBone.primary.state != AnimationPlayState::STOPPED)
			{
				animBone.primary.progress += deltaTime;

				const Model::ComplexModel::AnimationSequence& sequence = skeleton.sequences[primarySequenceID];
				u32 progress = static_cast<u32>(animBone.primary.progress * 1000.f);

				if (progress >= sequence.duration)
				{
					if (animBone.primary.state == AnimationPlayState::ONESHOT)
					{
						animBone.primary.state = AnimationPlayState::STOPPED;
						animBone.primary.progress = 0.0f;
					}
					else if (animBone.primary.state == AnimationPlayState::LOOPING)
					{
						animBone.primary.progress -= static_cast<f32>(sequence.duration) / 1000.f;
					}
				}

				bool isLooping = animBone.primary.state == AnimationPlayState::LOOPING;
				bool hasTransition = animBone.transition.state.sequence.sequenceID != AnimationSequenceInfo::InvalidID;
				if (isLooping && !hasTransition && (progress + 150 >= sequence.duration))
				{
					animBone.transition.progress = 0.0f;

					animBone.transition.state.state = AnimationPlayState::LOOPING;
					animBone.transition.state.progress = 0.0f;
					animBone.transition.state.sequence = animBone.primary.sequence;
				}
			}
		}

		u16 transitionSequenceID = animBone.transition.state.sequence.sequenceID;
		if (transitionSequenceID != AnimationSequenceInfo::InvalidID)
		{
			const Model::ComplexModel::AnimationSequence& sequence = skeleton.sequences[transitionSequenceID];

			if (animBone.transition.state.state != AnimationPlayState::STOPPED)
			{
				animBone.transition.state.progress += deltaTime;
				u32 progress = static_cast<u32>(animBone.transition.state.progress * 1000.f);

				if (progress >= sequence.duration)
				{
					if (animBone.transition.state.state == AnimationPlayState::ONESHOT)
					{
						animBone.transition.state.state = AnimationPlayState::STOPPED;
						animBone.transition.state.progress = 0.0f;
					}
					else if (animBone.transition.state.state == AnimationPlayState::LOOPING)
					{
						animBone.transition.state.progress -= static_cast<f32>(sequence.duration) / 1000.f;
					}
				}
			}

			animBone.transition.progress += deltaTime;
			if (animBone.transition.progress >= 0.15f)
			{
				animBone.primary = animBone.transition.state;

				// Reset transition state
				animBone.transition = { };
			}
		}

		mat4x4 boneMatrix = GetBoneMatrix(skeleton, animBone, bone);
		return boneMatrix;
	}
}