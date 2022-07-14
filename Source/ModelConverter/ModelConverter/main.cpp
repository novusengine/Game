#include <Base/Types.h>
#include <Base/Util/DebugHandler.h>
#include <Base/Memory/Bytebuffer.h>

#include <FileFormat/Models/Model.h>
#include <FileFormat/Models/ObjLoader.h>
#include <FileFormat/Models/ModelUtils.h>

#include <meshoptimizer.h>

#include <memory>
#include <fstream>
#include <filesystem>
namespace fs = std::filesystem;

i32 main()
{
	//std::string modelPath = "Models/cube.obj";
	std::string modelPath = "Models/Cube.obj";
	fs::path abosluteModelPath = fs::absolute(modelPath);

	objl::Loader objLoader;
	if (objLoader.LoadFile(abosluteModelPath.string()))
	{
		const size_t maxVertices = 64;
		const size_t maxTriangles = 124;
		const float coneWeight = 0.0f;

		u32 numMeshes = static_cast<u32>(objLoader.LoadedMeshes.size());

		Model model;
		model.meshes.resize(numMeshes);

		for (u32 meshIndex = 0; meshIndex < numMeshes; meshIndex++)
		{
			Model::Mesh& mesh = model.meshes[meshIndex];

			size_t maxMeshlets = meshopt_buildMeshletsBound(objLoader.LoadedIndices.size(), maxVertices, maxTriangles);
			std::vector<meshopt_Meshlet> meshlets(maxMeshlets);
			std::vector<u32> meshletVertices(maxMeshlets * maxVertices);
			std::vector<u8> meshletIndices(maxMeshlets * maxTriangles * 3);

			size_t meshletCount = meshopt_buildMeshlets(meshlets.data(), meshletVertices.data(), meshletIndices.data(), objLoader.LoadedIndices.data(), objLoader.LoadedIndices.size(), &objLoader.LoadedVertices[0].position.X, objLoader.LoadedVertices.size(), sizeof(objl::Vertex), maxVertices, maxTriangles, coneWeight);
			if (meshletCount > 0)
			{
				const meshopt_Meshlet& last = meshlets.back();

				// this is an example of how to trim the vertex/triangle arrays when copying data out to GPU storage
				meshlets.resize(meshletCount);
				meshletVertices.resize(last.vertex_offset + last.vertex_count);
				meshletIndices.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3));

				// Handle Vertices
				u32 numVerticesBefore = static_cast<u32>(model.vertices.size());
				u32 numVerticesToAdd = static_cast<u32>(meshletVertices.size());
				{
					model.vertices.resize(numVerticesBefore + numVerticesToAdd);

					for (u32 i = 0; i < numVerticesToAdd; i++)
					{
						u32 vertexIndex = meshletVertices[i];

						// This reinterpret_cast works because both formats share an identical vertex format.
						model.vertices[numVerticesBefore + i] = *reinterpret_cast<Model::Vertex*>(&objLoader.LoadedVertices[vertexIndex]);
					}
				}

				// Handle Indices
				u32 numIndicesBefore = static_cast<u32>(model.indices.size());
				u32 numIndicesToAdd = static_cast<u32>(meshletIndices.size());
				{
					model.indices.resize(numIndicesBefore + numIndicesToAdd);

					for (u32 i = 0; i < numIndicesToAdd; i++)
					{
						model.indices[numIndicesBefore + i] = numVerticesBefore + meshletIndices[i];
					}
				}

				// Handle Meshlets
				{
					u32 numMeshlets = static_cast<u32>(meshletCount);
					mesh.meshlets.resize(numMeshlets);

					for (u32 i = 0; i < numMeshlets; i++)
					{
						const meshopt_Meshlet& meshlet = meshlets[i];

						Model::Meshlet& modelMeshlet = mesh.meshlets[i];
						modelMeshlet.indexStart = numIndicesBefore + meshlet.triangle_offset * 3;
						modelMeshlet.indexCount = meshlet.triangle_count * 3;
					}
				}
			}

			u32 serializedSize = ModelUtils::GetModelSerializedSize(model);
			std::shared_ptr<Bytebuffer> buffer = Bytebuffer::BorrowRuntime(serializedSize);

			if (ModelUtils::Serialize(model, buffer))
			{
				fs::path outputPath = abosluteModelPath.parent_path() / "cube.model";
				fs::create_directories(outputPath.parent_path());

				// Create the file
				std::ofstream output(outputPath, std::ofstream::out | std::ofstream::binary);
				if (!output)
				{
					DebugHandler::PrintError("Failed to create Model file. Check admin permissions");
				}
				else
				{
					output.write(reinterpret_cast<const char*>(buffer->GetDataPointer()), buffer->writtenData);
					output.close();
				}
			}
		}
	}

	return 0;
}