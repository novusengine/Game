local function SetupLib()
    local basePath = path.getabsolute("jolt/", Game.dependencyDir)
    local dependencies = { }
    local defines = { "_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS", "_SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS" }

    ProjectTemplate("Jolt", "StaticLib", nil, Game.binDir, dependencies, defines)

    local sourceDir = path.getabsolute("Jolt", basePath)
    local includeDir = { basePath, sourceDir }
    local files =
    {
        sourceDir .. "/AABBTree/AABBTreeBuilder.cpp",
        sourceDir .. "/AABBTree/AABBTreeBuilder.h",
        sourceDir .. "/AABBTree/AABBTreeToBuffer.h",
        sourceDir .. "/AABBTree/NodeCodec/NodeCodecQuadTreeHalfFloat.h",
        sourceDir .. "/AABBTree/TriangleCodec/TriangleCodecIndexed8BitPackSOA4Flags.h",
        sourceDir .. "/Core/ARMNeon.h",
        sourceDir .. "/Core/Atomics.h",
        sourceDir .. "/Core/ByteBuffer.h",
        sourceDir .. "/Core/Color.cpp",
        sourceDir .. "/Core/Color.h",
        sourceDir .. "/Core/Core.h",
        sourceDir .. "/Core/Factory.cpp",
        sourceDir .. "/Core/Factory.h",
        sourceDir .. "/Core/FixedSizeFreeList.h",
        sourceDir .. "/Core/FixedSizeFreeList.inl",
        sourceDir .. "/Core/FPControlWord.h",
        sourceDir .. "/Core/FPException.h",
        sourceDir .. "/Core/FPFlushDenormals.h",
        sourceDir .. "/Core/HashCombine.h",
        sourceDir .. "/Core/InsertionSort.h",
        sourceDir .. "/Core/IssueReporting.cpp",
        sourceDir .. "/Core/IssueReporting.h",
        sourceDir .. "/Core/JobSystem.h",
        sourceDir .. "/Core/JobSystem.inl",
        sourceDir .. "/Core/JobSystemSingleThreaded.cpp",
        sourceDir .. "/Core/JobSystemSingleThreaded.h",
        sourceDir .. "/Core/JobSystemThreadPool.cpp",
        sourceDir .. "/Core/JobSystemThreadPool.h",
        sourceDir .. "/Core/JobSystemWithBarrier.cpp",
        sourceDir .. "/Core/JobSystemWithBarrier.h",
        sourceDir .. "/Core/LinearCurve.cpp",
        sourceDir .. "/Core/LinearCurve.h",
        sourceDir .. "/Core/LockFreeHashMap.h",
        sourceDir .. "/Core/LockFreeHashMap.inl",
        sourceDir .. "/Core/Memory.cpp",
        sourceDir .. "/Core/Memory.h",
        sourceDir .. "/Core/Mutex.h",
        sourceDir .. "/Core/MutexArray.h",
        sourceDir .. "/Core/NonCopyable.h",
        sourceDir .. "/Core/Profiler.cpp",
        sourceDir .. "/Core/Profiler.h",
        sourceDir .. "/Core/Profiler.inl",
        sourceDir .. "/Core/QuickSort.h",
        sourceDir .. "/Core/Reference.h",
        sourceDir .. "/Core/Result.h",
        sourceDir .. "/Core/RTTI.cpp",
        sourceDir .. "/Core/RTTI.h",
        sourceDir .. "/Core/Semaphore.cpp",
        sourceDir .. "/Core/Semaphore.h",
        sourceDir .. "/Core/StaticArray.h",
        sourceDir .. "/Core/StreamIn.h",
        sourceDir .. "/Core/StreamOut.h",
        sourceDir .. "/Core/StreamUtils.h",
        sourceDir .. "/Core/StreamWrapper.h",
        sourceDir .. "/Core/StringTools.cpp",
        sourceDir .. "/Core/StringTools.h",
        sourceDir .. "/Core/STLAlignedAllocator.h",
        sourceDir .. "/Core/STLAllocator.h",
        sourceDir .. "/Core/STLTempAllocator.h",
        sourceDir .. "/Core/TempAllocator.h",
        sourceDir .. "/Core/TickCounter.cpp",
        sourceDir .. "/Core/TickCounter.h",
        sourceDir .. "/Core/UnorderedMap.h",
        sourceDir .. "/Core/UnorderedSet.h",
        sourceDir .. "/Geometry/AABox.h",
        sourceDir .. "/Geometry/AABox4.h",
        sourceDir .. "/Geometry/ClipPoly.h",
        sourceDir .. "/Geometry/ClosestPoint.h",
        sourceDir .. "/Geometry/ConvexHullBuilder.cpp",
        sourceDir .. "/Geometry/ConvexHullBuilder.h",
        sourceDir .. "/Geometry/ConvexHullBuilder2D.cpp",
        sourceDir .. "/Geometry/ConvexHullBuilder2D.h",
        sourceDir .. "/Geometry/ConvexSupport.h",
        sourceDir .. "/Geometry/Ellipse.h",
        sourceDir .. "/Geometry/EPAConvexHullBuilder.h",
        sourceDir .. "/Geometry/EPAPenetrationDepth.h",
        sourceDir .. "/Geometry/GJKClosestPoint.h",
        sourceDir .. "/Geometry/IndexedTriangle.h",
        sourceDir .. "/Geometry/Indexify.cpp",
        sourceDir .. "/Geometry/Indexify.h",
        sourceDir .. "/Geometry/MortonCode.h",
        sourceDir .. "/Geometry/OrientedBox.cpp",
        sourceDir .. "/Geometry/OrientedBox.h",
        sourceDir .. "/Geometry/Plane.h",
        sourceDir .. "/Geometry/RayAABox.h",
        sourceDir .. "/Geometry/RayAABox8.h",
        sourceDir .. "/Geometry/RayCapsule.h",
        sourceDir .. "/Geometry/RayCylinder.h",
        sourceDir .. "/Geometry/RaySphere.h",
        sourceDir .. "/Geometry/RayTriangle.h",
        sourceDir .. "/Geometry/RayTriangle8.h",
        sourceDir .. "/Geometry/Sphere.h",
        sourceDir .. "/Geometry/Triangle.h",
        sourceDir .. "/Jolt.h",
        sourceDir .. "/Math/DMat44.h",
        sourceDir .. "/Math/DMat44.inl",
        sourceDir .. "/Math/Double3.h",
        sourceDir .. "/Math/DVec3.h",
        sourceDir .. "/Math/DVec3.inl",
        sourceDir .. "/Math/DynMatrix.h",
        sourceDir .. "/Math/EigenValueSymmetric.h",
        sourceDir .. "/Math/FindRoot.h",
        sourceDir .. "/Math/Float2.h",
        sourceDir .. "/Math/Float3.h",
        sourceDir .. "/Math/Float4.h",
        sourceDir .. "/Math/GaussianElimination.h",
        sourceDir .. "/Math/HalfFloat.h",
        sourceDir .. "/Math/Mat44.h",
        sourceDir .. "/Math/Mat44.inl",
        sourceDir .. "/Math/Math.h",
        sourceDir .. "/Math/MathTypes.h",
        sourceDir .. "/Math/Matrix.h",
        sourceDir .. "/Math/Quat.h",
        sourceDir .. "/Math/Quat.inl",
        sourceDir .. "/Math/Real.h",
        sourceDir .. "/Math/Swizzle.h",
        sourceDir .. "/Math/Trigonometry.h",
        sourceDir .. "/Math/UVec4.h",
        sourceDir .. "/Math/UVec4.inl",
        sourceDir .. "/Math/UVec8.h",
        sourceDir .. "/Math/UVec8.inl",
        sourceDir .. "/Math/Vec3.cpp",
        sourceDir .. "/Math/Vec3.h",
        sourceDir .. "/Math/Vec3.inl",
        sourceDir .. "/Math/Vec4.h",
        sourceDir .. "/Math/Vec4.inl",
        sourceDir .. "/Math/Vec8.h",
        sourceDir .. "/Math/Vec8.inl",
        sourceDir .. "/Math/Vector.h",
        sourceDir .. "/ObjectStream/GetPrimitiveTypeOfType.h",
        sourceDir .. "/ObjectStream/ObjectStream.cpp",
        sourceDir .. "/ObjectStream/ObjectStream.h",
        sourceDir .. "/ObjectStream/ObjectStreamBinaryIn.cpp",
        sourceDir .. "/ObjectStream/ObjectStreamBinaryIn.h",
        sourceDir .. "/ObjectStream/ObjectStreamBinaryOut.cpp",
        sourceDir .. "/ObjectStream/ObjectStreamBinaryOut.h",
        sourceDir .. "/ObjectStream/ObjectStreamIn.cpp",
        sourceDir .. "/ObjectStream/ObjectStreamIn.h",
        sourceDir .. "/ObjectStream/ObjectStreamOut.cpp",
        sourceDir .. "/ObjectStream/ObjectStreamOut.h",
        sourceDir .. "/ObjectStream/ObjectStreamTextIn.cpp",
        sourceDir .. "/ObjectStream/ObjectStreamTextIn.h",
        sourceDir .. "/ObjectStream/ObjectStreamTextOut.cpp",
        sourceDir .. "/ObjectStream/ObjectStreamTextOut.h",
        sourceDir .. "/ObjectStream/ObjectStreamTypes.h",
        sourceDir .. "/ObjectStream/SerializableAttribute.h",
        sourceDir .. "/ObjectStream/SerializableAttributeEnum.h",
        sourceDir .. "/ObjectStream/SerializableAttributeTyped.h",
        sourceDir .. "/ObjectStream/SerializableObject.cpp",
        sourceDir .. "/ObjectStream/SerializableObject.h",
        sourceDir .. "/ObjectStream/TypeDeclarations.cpp",
        sourceDir .. "/ObjectStream/TypeDeclarations.h",
        sourceDir .. "/Physics/Body/AllowedDOFs.h",
        sourceDir .. "/Physics/Body/Body.cpp",
        sourceDir .. "/Physics/Body/Body.h",
        sourceDir .. "/Physics/Body/Body.inl",
        sourceDir .. "/Physics/Body/BodyAccess.cpp",
        sourceDir .. "/Physics/Body/BodyAccess.h",
        sourceDir .. "/Physics/Body/BodyActivationListener.h",
        sourceDir .. "/Physics/Body/BodyCreationSettings.cpp",
        sourceDir .. "/Physics/Body/BodyCreationSettings.h",
        sourceDir .. "/Physics/Body/BodyFilter.h",
        sourceDir .. "/Physics/Body/BodyID.h",
        sourceDir .. "/Physics/Body/BodyInterface.cpp",
        sourceDir .. "/Physics/Body/BodyInterface.h",
        sourceDir .. "/Physics/Body/BodyLock.h",
        sourceDir .. "/Physics/Body/BodyLockInterface.h",
        sourceDir .. "/Physics/Body/BodyLockMulti.h",
        sourceDir .. "/Physics/Body/BodyManager.cpp",
        sourceDir .. "/Physics/Body/BodyManager.h",
        sourceDir .. "/Physics/Body/BodyPair.h",
        sourceDir .. "/Physics/Body/BodyType.h",
        sourceDir .. "/Physics/Body/MassProperties.cpp",
        sourceDir .. "/Physics/Body/MassProperties.h",
        sourceDir .. "/Physics/Body/MotionProperties.cpp",
        sourceDir .. "/Physics/Body/MotionProperties.h",
        sourceDir .. "/Physics/Body/MotionProperties.inl",
        sourceDir .. "/Physics/Body/MotionQuality.h",
        sourceDir .. "/Physics/Body/MotionType.h",
        sourceDir .. "/Physics/Character/Character.cpp",
        sourceDir .. "/Physics/Character/Character.h",
        sourceDir .. "/Physics/Character/CharacterBase.cpp",
        sourceDir .. "/Physics/Character/CharacterBase.h",
        sourceDir .. "/Physics/Character/CharacterVirtual.cpp",
        sourceDir .. "/Physics/Character/CharacterVirtual.h",
        sourceDir .. "/Physics/Collision/AABoxCast.h",
        sourceDir .. "/Physics/Collision/ActiveEdgeMode.h",
        sourceDir .. "/Physics/Collision/ActiveEdges.h",
        sourceDir .. "/Physics/Collision/BackFaceMode.h",
        sourceDir .. "/Physics/Collision/BroadPhase/BroadPhase.cpp",
        sourceDir .. "/Physics/Collision/BroadPhase/BroadPhase.h",
        sourceDir .. "/Physics/Collision/BroadPhase/BroadPhaseBruteForce.cpp",
        sourceDir .. "/Physics/Collision/BroadPhase/BroadPhaseBruteForce.h",
        sourceDir .. "/Physics/Collision/BroadPhase/BroadPhaseLayer.h",
        sourceDir .. "/Physics/Collision/BroadPhase/BroadPhaseQuadTree.cpp",
        sourceDir .. "/Physics/Collision/BroadPhase/BroadPhaseQuadTree.h",
        sourceDir .. "/Physics/Collision/BroadPhase/BroadPhaseQuery.h",
        sourceDir .. "/Physics/Collision/BroadPhase/QuadTree.cpp",
        sourceDir .. "/Physics/Collision/BroadPhase/QuadTree.h",
        sourceDir .. "/Physics/Collision/CastConvexVsTriangles.cpp",
        sourceDir .. "/Physics/Collision/CastConvexVsTriangles.h",
        sourceDir .. "/Physics/Collision/CastSphereVsTriangles.cpp",
        sourceDir .. "/Physics/Collision/CastSphereVsTriangles.h",
        sourceDir .. "/Physics/Collision/CastResult.h",
        sourceDir .. "/Physics/Collision/CollectFacesMode.h",
        sourceDir .. "/Physics/Collision/CollideConvexVsTriangles.cpp",
        sourceDir .. "/Physics/Collision/CollideConvexVsTriangles.h",
        sourceDir .. "/Physics/Collision/CollidePointResult.h",
        sourceDir .. "/Physics/Collision/CollideShape.h",
        sourceDir .. "/Physics/Collision/CollideSphereVsTriangles.cpp",
        sourceDir .. "/Physics/Collision/CollideSphereVsTriangles.h",
        sourceDir .. "/Physics/Collision/CollisionCollector.h",
        sourceDir .. "/Physics/Collision/CollisionCollectorImpl.h",
        sourceDir .. "/Physics/Collision/CollisionDispatch.cpp",
        sourceDir .. "/Physics/Collision/CollisionDispatch.h",
        sourceDir .. "/Physics/Collision/CollisionGroup.cpp",
        sourceDir .. "/Physics/Collision/CollisionGroup.h",
        sourceDir .. "/Physics/Collision/ContactListener.h",
        sourceDir .. "/Physics/Collision/EstimateCollisionResponse.cpp",
        sourceDir .. "/Physics/Collision/EstimateCollisionResponse.h",
        sourceDir .. "/Physics/Collision/GroupFilter.cpp",
        sourceDir .. "/Physics/Collision/GroupFilter.h",
        sourceDir .. "/Physics/Collision/GroupFilterTable.cpp",
        sourceDir .. "/Physics/Collision/GroupFilterTable.h",
        sourceDir .. "/Physics/Collision/ManifoldBetweenTwoFaces.cpp",
        sourceDir .. "/Physics/Collision/ManifoldBetweenTwoFaces.h",
        sourceDir .. "/Physics/Collision/NarrowPhaseQuery.cpp",
        sourceDir .. "/Physics/Collision/NarrowPhaseQuery.h",
        sourceDir .. "/Physics/Collision/NarrowPhaseStats.cpp",
        sourceDir .. "/Physics/Collision/NarrowPhaseStats.h",
        sourceDir .. "/Physics/Collision/ObjectLayer.h",
        sourceDir .. "/Physics/Collision/PhysicsMaterial.cpp",
        sourceDir .. "/Physics/Collision/PhysicsMaterial.h",
        sourceDir .. "/Physics/Collision/PhysicsMaterialSimple.cpp",
        sourceDir .. "/Physics/Collision/PhysicsMaterialSimple.h",
        sourceDir .. "/Physics/Collision/RayCast.h",
        sourceDir .. "/Physics/Collision/Shape/BoxShape.cpp",
        sourceDir .. "/Physics/Collision/Shape/BoxShape.h",
        sourceDir .. "/Physics/Collision/Shape/CapsuleShape.cpp",
        sourceDir .. "/Physics/Collision/Shape/CapsuleShape.h",
        sourceDir .. "/Physics/Collision/Shape/CompoundShape.cpp",
        sourceDir .. "/Physics/Collision/Shape/CompoundShape.h",
        sourceDir .. "/Physics/Collision/Shape/CompoundShapeVisitors.h",
        sourceDir .. "/Physics/Collision/Shape/ConvexHullShape.cpp",
        sourceDir .. "/Physics/Collision/Shape/ConvexHullShape.h",
        sourceDir .. "/Physics/Collision/Shape/ConvexShape.cpp",
        sourceDir .. "/Physics/Collision/Shape/ConvexShape.h",
        sourceDir .. "/Physics/Collision/Shape/CylinderShape.cpp",
        sourceDir .. "/Physics/Collision/Shape/CylinderShape.h",
        sourceDir .. "/Physics/Collision/Shape/DecoratedShape.cpp",
        sourceDir .. "/Physics/Collision/Shape/DecoratedShape.h",
        sourceDir .. "/Physics/Collision/Shape/GetTrianglesContext.h",
        sourceDir .. "/Physics/Collision/Shape/HeightFieldShape.cpp",
        sourceDir .. "/Physics/Collision/Shape/HeightFieldShape.h",
        sourceDir .. "/Physics/Collision/Shape/MeshShape.cpp",
        sourceDir .. "/Physics/Collision/Shape/MeshShape.h",
        sourceDir .. "/Physics/Collision/Shape/MutableCompoundShape.cpp",
        sourceDir .. "/Physics/Collision/Shape/MutableCompoundShape.h",
        sourceDir .. "/Physics/Collision/Shape/OffsetCenterOfMassShape.cpp",
        sourceDir .. "/Physics/Collision/Shape/OffsetCenterOfMassShape.h",
        sourceDir .. "/Physics/Collision/Shape/PolyhedronSubmergedVolumeCalculator.h",
        sourceDir .. "/Physics/Collision/Shape/RotatedTranslatedShape.cpp",
        sourceDir .. "/Physics/Collision/Shape/RotatedTranslatedShape.h",
        sourceDir .. "/Physics/Collision/Shape/ScaledShape.cpp",
        sourceDir .. "/Physics/Collision/Shape/ScaledShape.h",
        sourceDir .. "/Physics/Collision/Shape/ScaleHelpers.h",
        sourceDir .. "/Physics/Collision/Shape/Shape.cpp",
        sourceDir .. "/Physics/Collision/Shape/Shape.h",
        sourceDir .. "/Physics/Collision/Shape/SphereShape.cpp",
        sourceDir .. "/Physics/Collision/Shape/SphereShape.h",
        sourceDir .. "/Physics/Collision/Shape/StaticCompoundShape.cpp",
        sourceDir .. "/Physics/Collision/Shape/StaticCompoundShape.h",
        sourceDir .. "/Physics/Collision/Shape/SubShapeID.h",
        sourceDir .. "/Physics/Collision/Shape/SubShapeIDPair.h",
        sourceDir .. "/Physics/Collision/Shape/TaperedCapsuleShape.cpp",
        sourceDir .. "/Physics/Collision/Shape/TaperedCapsuleShape.h",
        sourceDir .. "/Physics/Collision/Shape/TriangleShape.cpp",
        sourceDir .. "/Physics/Collision/Shape/TriangleShape.h",
        sourceDir .. "/Physics/Collision/ShapeCast.h",
        sourceDir .. "/Physics/Collision/ShapeFilter.h",
        sourceDir .. "/Physics/Collision/SortReverseAndStore.h",
        sourceDir .. "/Physics/Collision/TransformedShape.cpp",
        sourceDir .. "/Physics/Collision/TransformedShape.h",
        sourceDir .. "/Physics/Constraints/ConeConstraint.cpp",
        sourceDir .. "/Physics/Constraints/ConeConstraint.h",
        sourceDir .. "/Physics/Constraints/Constraint.cpp",
        sourceDir .. "/Physics/Constraints/Constraint.h",
        sourceDir .. "/Physics/Constraints/ConstraintManager.cpp",
        sourceDir .. "/Physics/Constraints/ConstraintManager.h",
        sourceDir .. "/Physics/Constraints/ConstraintPart/AngleConstraintPart.h",
        sourceDir .. "/Physics/Constraints/ConstraintPart/AxisConstraintPart.h",
        sourceDir .. "/Physics/Constraints/ConstraintPart/DualAxisConstraintPart.h",
        sourceDir .. "/Physics/Constraints/ConstraintPart/GearConstraintPart.h",
        sourceDir .. "/Physics/Constraints/ConstraintPart/HingeRotationConstraintPart.h",
        sourceDir .. "/Physics/Constraints/ConstraintPart/IndependentAxisConstraintPart.h",
        sourceDir .. "/Physics/Constraints/ConstraintPart/PointConstraintPart.h",
        sourceDir .. "/Physics/Constraints/ConstraintPart/RackAndPinionConstraintPart.h",
        sourceDir .. "/Physics/Constraints/ConstraintPart/RotationEulerConstraintPart.h",
        sourceDir .. "/Physics/Constraints/ConstraintPart/RotationQuatConstraintPart.h",
        sourceDir .. "/Physics/Constraints/ConstraintPart/SpringPart.h",
        sourceDir .. "/Physics/Constraints/ConstraintPart/SwingTwistConstraintPart.h",
        sourceDir .. "/Physics/Constraints/ContactConstraintManager.cpp",
        sourceDir .. "/Physics/Constraints/ContactConstraintManager.h",
        sourceDir .. "/Physics/Constraints/DistanceConstraint.cpp",
        sourceDir .. "/Physics/Constraints/DistanceConstraint.h",
        sourceDir .. "/Physics/Constraints/FixedConstraint.cpp",
        sourceDir .. "/Physics/Constraints/FixedConstraint.h",
        sourceDir .. "/Physics/Constraints/GearConstraint.cpp",
        sourceDir .. "/Physics/Constraints/GearConstraint.h",
        sourceDir .. "/Physics/Constraints/HingeConstraint.cpp",
        sourceDir .. "/Physics/Constraints/HingeConstraint.h",
        sourceDir .. "/Physics/Constraints/MotorSettings.cpp",
        sourceDir .. "/Physics/Constraints/MotorSettings.h",
        sourceDir .. "/Physics/Constraints/PathConstraint.cpp",
        sourceDir .. "/Physics/Constraints/PathConstraint.h",
        sourceDir .. "/Physics/Constraints/PathConstraintPath.cpp",
        sourceDir .. "/Physics/Constraints/PathConstraintPath.h",
        sourceDir .. "/Physics/Constraints/PathConstraintPathHermite.cpp",
        sourceDir .. "/Physics/Constraints/PathConstraintPathHermite.h",
        sourceDir .. "/Physics/Constraints/PointConstraint.cpp",
        sourceDir .. "/Physics/Constraints/PointConstraint.h",
        sourceDir .. "/Physics/Constraints/PulleyConstraint.cpp",
        sourceDir .. "/Physics/Constraints/PulleyConstraint.h",
        sourceDir .. "/Physics/Constraints/RackAndPinionConstraint.cpp",
        sourceDir .. "/Physics/Constraints/RackAndPinionConstraint.h",
        sourceDir .. "/Physics/Constraints/SixDOFConstraint.cpp",
        sourceDir .. "/Physics/Constraints/SixDOFConstraint.h",
        sourceDir .. "/Physics/Constraints/SliderConstraint.cpp",
        sourceDir .. "/Physics/Constraints/SliderConstraint.h",
        sourceDir .. "/Physics/Constraints/SpringSettings.cpp",
        sourceDir .. "/Physics/Constraints/SpringSettings.h",
        sourceDir .. "/Physics/Constraints/SwingTwistConstraint.cpp",
        sourceDir .. "/Physics/Constraints/SwingTwistConstraint.h",
        sourceDir .. "/Physics/Constraints/TwoBodyConstraint.cpp",
        sourceDir .. "/Physics/Constraints/TwoBodyConstraint.h",
        sourceDir .. "/Physics/DeterminismLog.cpp",
        sourceDir .. "/Physics/DeterminismLog.h",
        sourceDir .. "/Physics/EActivation.h",
        sourceDir .. "/Physics/EPhysicsUpdateError.h",
        sourceDir .. "/Physics/IslandBuilder.cpp",
        sourceDir .. "/Physics/IslandBuilder.h",
        sourceDir .. "/Physics/LargeIslandSplitter.cpp",
        sourceDir .. "/Physics/LargeIslandSplitter.h",
        sourceDir .. "/Physics/PhysicsLock.cpp",
        sourceDir .. "/Physics/PhysicsLock.h",
        sourceDir .. "/Physics/PhysicsScene.cpp",
        sourceDir .. "/Physics/PhysicsScene.h",
        sourceDir .. "/Physics/PhysicsSettings.h",
        sourceDir .. "/Physics/PhysicsStepListener.h",
        sourceDir .. "/Physics/PhysicsSystem.cpp",
        sourceDir .. "/Physics/PhysicsSystem.h",
        sourceDir .. "/Physics/PhysicsUpdateContext.cpp",
        sourceDir .. "/Physics/PhysicsUpdateContext.h",
        sourceDir .. "/Physics/Ragdoll/Ragdoll.cpp",
        sourceDir .. "/Physics/Ragdoll/Ragdoll.h",
        sourceDir .. "/Physics/SoftBody/SoftBodyCreationSettings.cpp",
        sourceDir .. "/Physics/SoftBody/SoftBodyCreationSettings.h",
        sourceDir .. "/Physics/SoftBody/SoftBodyMotionProperties.h",
        sourceDir .. "/Physics/SoftBody/SoftBodyMotionProperties.cpp",
        sourceDir .. "/Physics/SoftBody/SoftBodyShape.cpp",
        sourceDir .. "/Physics/SoftBody/SoftBodyShape.h",
        sourceDir .. "/Physics/SoftBody/SoftBodySharedSettings.cpp",
        sourceDir .. "/Physics/SoftBody/SoftBodySharedSettings.h",
        sourceDir .. "/Physics/SoftBody/SoftBodyVertex.h",
        sourceDir .. "/Physics/StateRecorder.h",
        sourceDir .. "/Physics/StateRecorderImpl.cpp",
        sourceDir .. "/Physics/StateRecorderImpl.h",
        sourceDir .. "/Physics/Vehicle/MotorcycleController.cpp",
        sourceDir .. "/Physics/Vehicle/MotorcycleController.h",
        sourceDir .. "/Physics/Vehicle/TrackedVehicleController.cpp",
        sourceDir .. "/Physics/Vehicle/TrackedVehicleController.h",
        sourceDir .. "/Physics/Vehicle/VehicleAntiRollBar.cpp",
        sourceDir .. "/Physics/Vehicle/VehicleAntiRollBar.h",
        sourceDir .. "/Physics/Vehicle/VehicleCollisionTester.cpp",
        sourceDir .. "/Physics/Vehicle/VehicleCollisionTester.h",
        sourceDir .. "/Physics/Vehicle/VehicleConstraint.cpp",
        sourceDir .. "/Physics/Vehicle/VehicleConstraint.h",
        sourceDir .. "/Physics/Vehicle/VehicleController.cpp",
        sourceDir .. "/Physics/Vehicle/VehicleController.h",
        sourceDir .. "/Physics/Vehicle/VehicleDifferential.cpp",
        sourceDir .. "/Physics/Vehicle/VehicleDifferential.h",
        sourceDir .. "/Physics/Vehicle/VehicleEngine.cpp",
        sourceDir .. "/Physics/Vehicle/VehicleEngine.h",
        sourceDir .. "/Physics/Vehicle/VehicleTrack.cpp",
        sourceDir .. "/Physics/Vehicle/VehicleTrack.h",
        sourceDir .. "/Physics/Vehicle/VehicleTransmission.cpp",
        sourceDir .. "/Physics/Vehicle/VehicleTransmission.h",
        sourceDir .. "/Physics/Vehicle/Wheel.cpp",
        sourceDir .. "/Physics/Vehicle/Wheel.h",
        sourceDir .. "/Physics/Vehicle/WheeledVehicleController.cpp",
        sourceDir .. "/Physics/Vehicle/WheeledVehicleController.h",
        sourceDir .. "/RegisterTypes.cpp",
        sourceDir .. "/RegisterTypes.h",
        sourceDir .. "/Renderer/DebugRenderer.cpp",
        sourceDir .. "/Renderer/DebugRenderer.h",
        sourceDir .. "/Renderer/DebugRendererPlayback.cpp",
        sourceDir .. "/Renderer/DebugRendererPlayback.h",
        sourceDir .. "/Renderer/DebugRendererRecorder.cpp",
        sourceDir .. "/Renderer/DebugRendererRecorder.h",
        sourceDir .. "/Skeleton/SkeletalAnimation.cpp",
        sourceDir .. "/Skeleton/SkeletalAnimation.h",
        sourceDir .. "/Skeleton/Skeleton.cpp",
        sourceDir .. "/Skeleton/Skeleton.h",
        sourceDir .. "/Skeleton/SkeletonMapper.cpp",
        sourceDir .. "/Skeleton/SkeletonMapper.h",
        sourceDir .. "/Skeleton/SkeletonPose.cpp",
        sourceDir .. "/Skeleton/SkeletonPose.h",
        sourceDir .. "/TriangleGrouper/TriangleGrouper.h",
        sourceDir .. "/TriangleGrouper/TriangleGrouperClosestCentroid.cpp",
        sourceDir .. "/TriangleGrouper/TriangleGrouperClosestCentroid.h",
        sourceDir .. "/TriangleGrouper/TriangleGrouperMorton.cpp",
        sourceDir .. "/TriangleGrouper/TriangleGrouperMorton.h",
        sourceDir .. "/TriangleSplitter/TriangleSplitter.cpp",
        sourceDir .. "/TriangleSplitter/TriangleSplitter.h",
        sourceDir .. "/TriangleSplitter/TriangleSplitterBinning.cpp",
        sourceDir .. "/TriangleSplitter/TriangleSplitterBinning.h",
        sourceDir .. "/TriangleSplitter/TriangleSplitterFixedLeafSize.cpp",
        sourceDir .. "/TriangleSplitter/TriangleSplitterFixedLeafSize.h",
        sourceDir .. "/TriangleSplitter/TriangleSplitterLongestAxis.cpp",
        sourceDir .. "/TriangleSplitter/TriangleSplitterLongestAxis.h",
        sourceDir .. "/TriangleSplitter/TriangleSplitterMean.cpp",
        sourceDir .. "/TriangleSplitter/TriangleSplitterMean.h",
        sourceDir .. "/TriangleSplitter/TriangleSplitterMorton.cpp",
        sourceDir .. "/TriangleSplitter/TriangleSplitterMorton.h"
    }
    AddFiles(files)

    AddIncludeDirs(includeDir)

    local enableDebugRenderer = BuildSettings:Get("Jolt Enable Debug Renderer")
    if enableDebugRenderer ~= nil and enableDebugRenderer == true then
        AddDefines({"JPH_DEBUG_RENDERER"})
    end

    local floatPointExceptions = BuildSettings:Get("Jolt Floating Point Exceptions")
    if floatPointExceptions ~= nil and floatPointExceptions == true then
        AddDefines({"JPH_FLOATING_POINT_EXCEPTIONS_ENABLED"})
    end

    local doublePrecision = BuildSettings:Get("Jolt Double Precision")
    if doublePrecision ~= nil and doublePrecision == true then
        AddDefines({"JPH_DOUBLE_PRECISION"})
    end

    local crossPlatformDeterministic = BuildSettings:Get("Jolt Cross Platform Deterministic")
    if crossPlatformDeterministic ~= nil and crossPlatformDeterministic == true then
        AddDefines({"JPH_CROSS_PLATFORM_DETERMINISTIC"})
    end

    local objectLayerBits = BuildSettings:Get("Jolt Object Layer Bits")
    if objectLayerBits ~= nil then
        if objectLayerBits ~= 16 and objectLayerBits ~= 32 then
            error("Jolt Object Layer Bits must be set to either 16 or 32")
        end

        AddDefines({"JPH_OBJECT_LAYER_BITS=" .. tostring(objectLayerBits)})
    end

    local trackBroadphaseStats = BuildSettings:Get("Jolt Track Broadphase Stats")
    if trackBroadphaseStats ~= nil and trackBroadphaseStats == true then
        AddDefines({"JPH_TRACK_BROADPHASE_STATS"})
    end

    local trackNarrowphaseStats = BuildSettings:Get("Jolt Track Narrowphase Stats")
    if trackNarrowphaseStats ~= nil and trackNarrowphaseStats == true then
        AddDefines({"JPH_TRACK_NARROWPHASE_STATS"})
    end

    -- TODO : Support EMIT_X86_INSTRUCTION_SET_DEFINITIONS

    filter "configurations:Debug"
        AddDefines({"_DEBUG"})

    filter "configurations:RelDebug"
        AddDefines({"NDEBUG"})

    filter "configurations:Release"
        AddDefines({"NDEBUG"})

    filter { }
    AddDefines({"JPH_PROFILE_ENABLED", "CROSS_PLATFORM_DETERMINISTIC"})
    AddLinks("wininet")
end
SetupLib()

local function Include()
    local basePath = path.getabsolute("jolt/", Game.dependencyDir)
    local includeDir = basePath

    AddIncludeDirs(includeDir)

    local enableDebugRenderer = BuildSettings:Get("Jolt Enable Debug Renderer")
    if enableDebugRenderer ~= nil and enableDebugRenderer == true then
        AddDefines({"JPH_DEBUG_RENDERER"})
    end

    local floatPointExceptions = BuildSettings:Get("Jolt Floating Point Exceptions")
    if floatPointExceptions ~= nil and floatPointExceptions == true then
        AddDefines({"JPH_FLOATING_POINT_EXCEPTIONS_ENABLED"})
    end

    local doublePrecision = BuildSettings:Get("Jolt Double Precision")
    if doublePrecision ~= nil and doublePrecision == true then
        AddDefines({"JPH_DOUBLE_PRECISION"})
    end

    local crossPlatformDeterministic = BuildSettings:Get("Jolt Cross Platform Deterministic")
    if crossPlatformDeterministic ~= nil and crossPlatformDeterministic == true then
        AddDefines({"JPH_CROSS_PLATFORM_DETERMINISTIC"})
    end

    local objectLayerBits = BuildSettings:Get("Jolt Object Layer Bits")
    if objectLayerBits ~= nil then
        if objectLayerBits ~= 16 and objectLayerBits ~= 32 then
            error("Jolt Object Layer Bits must be set to either 16 or 32")
        end

        AddDefines({"JPH_OBJECT_LAYER_BITS=" .. tostring(objectLayerBits)})
    end

    local trackBroadphaseStats = BuildSettings:Get("Jolt Track Broadphase Stats")
    if trackBroadphaseStats ~= nil and trackBroadphaseStats == true then
        AddDefines({"JPH_TRACK_BROADPHASE_STATS"})
    end

    local trackNarrowphaseStats = BuildSettings:Get("Jolt Track Narrowphase Stats")
    if trackNarrowphaseStats ~= nil and trackNarrowphaseStats == true then
        AddDefines({"JPH_TRACK_NARROWPHASE_STATS"})
    end

    AddDefines({"JPH_PROFILE_ENABLED", "CROSS_PLATFORM_DETERMINISTIC"})

    AddLinks("Jolt")
end
CreateDep("jolt", Include)