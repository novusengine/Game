#include <Base/Types.h>
#include <Base/Util/DebugHandler.h>
#include <Base/Memory/Bytebuffer.h>

#include <FileFormat/Models/Model.h>
#include <FileFormat/Models/ModelUtils.h>
#include <FileFormat/Models/TinyObjLoader.h>

#include <meshoptimizer.h>

#include <memory>
#include <fstream>
#include <filesystem>
namespace fs = std::filesystem;

i32 main()
{
	std::string modelName = "BeetleWarrior_Low.obj";

	fs::path currentPath = fs::current_path();
	fs::path modelPath = currentPath / "Models/" / modelName;

	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn;
	std::string err;

	if (tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, modelPath.string().c_str(), modelPath.parent_path().string().c_str(), true))
	{
		std::vector<vec3> objVertices(attrib.vertices.size() / 3);
		std::vector<vec3> objNormals(attrib.normals.size() / 3);
		std::vector<vec2> objUVs(attrib.texcoords.size() / 2);

		// Convert Obj Structure to Reasonable Structure
		{
			for (u32 i = 0; i < objVertices.size(); i++)
			{
				size_t attributeBaseOffset = i * 3;
				objVertices[i] = vec3(attrib.vertices[attributeBaseOffset + 0], attrib.vertices[attributeBaseOffset + 1], attrib.vertices[attributeBaseOffset + 2]);
			}

			for (u32 i = 0; i < objNormals.size(); i++)
			{
				size_t attributeBaseOffset = i * 3;
				objNormals[i] = vec3(attrib.normals[attributeBaseOffset + 0], attrib.normals[attributeBaseOffset + 1], attrib.normals[attributeBaseOffset + 2]);
			}

			for (u32 i = 0; i < objUVs.size(); i++)
			{
				size_t attributeBaseOffset = i * 2;
				objUVs[i] = vec2(attrib.texcoords[attributeBaseOffset + 0], attrib.texcoords[attributeBaseOffset + 1]);
			}
		}

		const size_t maxVertices = 64;
		const size_t maxTriangles = 124;
		const float coneWeight = 0.0f;

		u32 numMeshes = static_cast<u32>(shapes.size());

		Model model;
		model.meshes.resize(numMeshes);

		for (u32 meshIndex = 0; meshIndex < numMeshes; meshIndex++)
		{
			const tinyobj::shape_t& shape = shapes[meshIndex];

			std::vector<Model::Vertex> shapeVertices(shape.mesh.indices.size());
			std::vector<u32> shapeIndices(shape.mesh.indices.size());
			for (u32 i = 0; i < shapeVertices.size(); i++)
			{
				Model::Vertex& vertex = shapeVertices[i];

				tinyobj::index_t index = shape.mesh.indices[i];
				vertex.position = objVertices[index.vertex_index];

				i32 normalIndex = index.normal_index;
				if (normalIndex == -1)
				{
					vertex.normal = vec3(0.0f, 1.0f, 0.0f);
				}
				else
				{
					vertex.normal = objNormals[normalIndex];
				}

				i32 uvIndex = index.texcoord_index;
				if (uvIndex == -1)
				{
					vertex.uv = vec2(0.0f, 0.0f);
				}
				else
				{
					vertex.uv = objUVs[uvIndex];
				}

				shapeIndices[i] = i;
			}

			size_t maxMeshlets = meshopt_buildMeshletsBound(shape.mesh.indices.size(), maxVertices, maxTriangles);
			std::vector<meshopt_Meshlet> meshlets(maxMeshlets);
			std::vector<u32> meshletVertices(maxMeshlets * maxVertices);
			std::vector<u8> meshletIndices(maxMeshlets * maxTriangles * 3);

			size_t meshletCount = meshopt_buildMeshlets(meshlets.data(), meshletVertices.data(), meshletIndices.data(), shapeIndices.data(), shapeIndices.size(), &shapeVertices[0].position.x, shapeVertices.size(), sizeof(Model::Vertex), maxVertices, maxTriangles, coneWeight);
			if (meshletCount > 0)
			{
				// this is an example of how to trim the vertex/triangle arrays when copying data out to GPU storage
				meshlets.resize(meshletCount);

				const meshopt_Meshlet& last = meshlets.back();
				meshletVertices.resize(last.vertex_offset + last.vertex_count);
				meshletIndices.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3));

				// Handle Vertices
				u32 numVerticesBefore = static_cast<u32>(model.vertices.size());
				u32 numVerticesToAdd = static_cast<u32>(shapeVertices.size());
				{
					model.vertices.resize(numVerticesBefore + numVerticesToAdd);
					memcpy(&model.vertices[numVerticesBefore], shapeVertices.data(), numVerticesToAdd * sizeof(Model::Vertex));

					for (u32 i = 0; i < meshletVertices.size(); i++)
					{
						u32 index = meshletVertices[i];

						Model::Vertex& vertex = model.vertices[numVerticesBefore + i];
						vertex = shapeVertices[index];
					}
				}

				// Handle Meshlets
				{
					Model::Mesh& mesh = model.meshes[meshIndex];

					u32 numMeshlets = static_cast<u32>(meshletCount);
					mesh.meshlets.resize(numMeshlets);

					u32 numIndicesBefore = static_cast<u32>(model.indices.size());
					u32 numIndicesToAdd = static_cast<u32>(meshletIndices.size());
					model.indices.resize(numIndicesBefore + numIndicesToAdd);

					for (u32 i = 0; i < numMeshlets; i++)
					{
						const meshopt_Meshlet& meshlet = meshlets[i];
						Model::Meshlet& modelMeshlet = mesh.meshlets[i];

						modelMeshlet.indexStart = numIndicesBefore + meshlet.triangle_offset;
						modelMeshlet.indexCount = meshlet.triangle_count * 3;

						// Handle Indices
						{
							for (u32 i = 0; i < modelMeshlet.indexCount; i++)
							{
								model.indices[modelMeshlet.indexStart + i] = numVerticesBefore + meshlet.vertex_offset + meshletIndices[i];
							}
						}
					}
				}

			}
		}

		u32 serializedSize = ModelUtils::GetModelSerializedSize(model);
		std::shared_ptr<Bytebuffer> buffer = Bytebuffer::BorrowRuntime(serializedSize);

		if (ModelUtils::Serialize(model, buffer))
		{
			fs::path outputPath = modelPath.replace_extension("model");
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

	return 0;
}