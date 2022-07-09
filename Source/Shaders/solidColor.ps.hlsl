struct Color
{
    float4 value;
};
[[vk::push_constant]] Color _color;

float4 main() : SV_Target
{
    return _color.value;
}