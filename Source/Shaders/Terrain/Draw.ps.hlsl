permutation WIRE_FRAME = [0, 1];
permutation DEBUG_MODE = [0, 1];
#include "Terrain/Shared.inc.hlsl"

struct Constants
{
	float triplanarSharpness;
};

struct Material
{
	uint xTextureID;
	uint yTextureID;
	uint zTextureID;
	uint padding;
};

[[vk::push_constant]] Constants _constants;
[[vk::binding(7, PER_DRAW)]] StructuredBuffer<Material> _materials;
[[vk::binding(8, PER_DRAW)]] SamplerState _sampler;
[[vk::binding(9, PER_DRAW)]] Texture2D<float4> _textures[512];

float3 GetTriplanarWeights(float3 normal, float3 height, float sharpness)
{
	float3 weights = abs(normal);
	weights = max(weights, 0.00001f); // Force weights to sum to 1.0f
	weights = normalize(weights);

	//const float heightStrength = 1.0f;
	//weights *= lerp(1.0f, height, heightStrength);

	weights = pow(weights, sharpness);
	float w = (weights.x + weights.y + weights.z);
	return weights / w;
}

float4 CalculateHeightBlending(float4 blending, float4 height)
{
	float4 heightBlending = blending + height;

	float depth = 0.2f;
	float4 ma = max(max(heightBlending.x, heightBlending.y), max(heightBlending.z, heightBlending.w)) - depth;

	float4 b = max((blending + height) - ma, 0);
	return b;
}

float CalculateLighting(float3 normal)
{
	const float3 sunDir = float3(0.0f, 1.0f, 0.0f);//normalize(float3(-0.4f, 1.0f, 0.3f));
	return saturate(dot(normal, sunDir));
}

struct VSOutput
{
	float4 position : SV_POSITION;
	float3 worldPos : TEXCOORD0;
	float3 normal : TEXCOORD1;
	nointerpolation uint packedMaterials : TEXCOORD2;
	float4 blendValues : TEXCOORD3;
	nointerpolation uint meshletDataID : TEXCOORD4;
};

float4 main(VSOutput input) : SV_Target
{
#if DEBUG_MODE
	float3 debugColor = IDToColor3(input.meshletDataID);
	return float4(debugColor, 1.0f);
#endif

#if WIRE_FRAME
	return float4(1.0f, 1.0f, 1.0f, 1.0f);
#else
	const float scale = 0.1f;
	float3 normal = normalize(input.normal);

	float3 materialColors[4];
	float4 materialHeights = float4(0,0,0,0);
	float3 materialNormals[4];

	[unroll]
	for (uint i = 0; i < 4; i++)
	{
		uint materialID = (input.packedMaterials >> (8 * i)) & 0xFF;
		Material material = _materials[materialID];

		// Height texture sampling
		float3 height;
		height.x = _textures[material.xTextureID + 2].Sample(_sampler, input.worldPos.zy * scale).x;
		height.y = _textures[material.yTextureID + 2].Sample(_sampler, input.worldPos.xz * scale).x;
		height.z = _textures[material.zTextureID + 2].Sample(_sampler, input.worldPos.xy * scale).x;

		float3 triplanarWeights = GetTriplanarWeights(normal, height, _constants.triplanarSharpness);
		materialHeights[i] = height.x * triplanarWeights.x + height.y * triplanarWeights.y + height.z * triplanarWeights.z;

		// Color texture sampling	
		float3 xTexture = _textures[material.xTextureID].Sample(_sampler, input.worldPos.zy * scale).xyz;
		float3 yTexture = _textures[material.yTextureID].Sample(_sampler, input.worldPos.xz * scale).xyz;
		float3 zTexture = _textures[material.zTextureID].Sample(_sampler, input.worldPos.xy * scale).xyz;
		materialColors[i] = xTexture * triplanarWeights.x + yTexture * triplanarWeights.y + zTexture * triplanarWeights.z;

		// Normal map sampling
		float3 xNormal = _textures[material.xTextureID + 1].Sample(_sampler, input.worldPos.zy * scale).xyz;
		float3 yNormal = _textures[material.yTextureID + 1].Sample(_sampler, input.worldPos.xz * scale).xyz;
		float3 zNormal = _textures[material.zTextureID + 1].Sample(_sampler, input.worldPos.xy * scale).xyz;

		// Swizzle world normals into tangent space and apply Whiteout blend
		xNormal = float3(xNormal.xy + input.normal.zy, abs(xNormal.z) * input.normal.x);
		yNormal = float3(yNormal.xy + input.normal.xz, abs(yNormal.z) * input.normal.y);
		zNormal = float3(zNormal.xy + input.normal.xy, abs(zNormal.z) * input.normal.z);

		// Swizzle tangent normals to match world orientation and triblend
		materialNormals[i] = normalize(xNormal.zyx * triplanarWeights.x + yNormal.xzy * triplanarWeights.y + zNormal.xyz * triplanarWeights.z);
	}

	float4 heightBlending = CalculateHeightBlending(input.blendValues, materialHeights);
	float4 blending = heightBlending;
	float blendSum = (blending.x + blending.y + blending.z + blending.w);

	float3 materialColor = ((materialColors[0] * blending.x) + (materialColors[1] * blending.y) + (materialColors[2] * blending.z) + (materialColors[3] * blending.w)) / blendSum;
	float3 materialNormal = normalize(((materialNormals[0] * blending.x) + (materialNormals[1] * blending.y) + (materialNormals[2] * blending.z) + (materialNormals[3] * blending.w)) / blendSum);

	float lighting = CalculateLighting(materialNormal);

	const float3 brightColor = materialColor;
	const float3 darkColor = materialColor * 0.7f;
	float3 color = lerp(darkColor, brightColor, lighting);

	return float4(color, 1.0f);
#endif
}