
struct DeferredShading_Struct {
	float4 eye;
	float4 light;
};

Texture2D<float4> pos_texture : register(t0);
Texture2D<float4> normal_texture : register(t1);
Texture2D<float4> color_texture : register(t2);

ConstantBuffer<DeferredShading_Struct> shading_info : register(b3);

SamplerState texSampler : register(s0);

struct PSInput {
	float4 pos : SV_POSITION;
	float2 tc : TC;
};

PSInput VSMain(float2 tc : TC) {
	PSInput result;

	float x = tc.x * 2.f - 1.f;
	float y = 1.f - tc.y * 2.f;

	result.pos = float4(x, y, 0.5f, 1.f);
	result.tc = tc;
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET{
	float3 pos = pos_texture.Sample(texSampler, input.tc).xyz / pos_texture.Sample(texSampler, input.tc).w;
	
	if(!isnan(pos.x)) {
		float4 col = color_texture.Sample(texSampler, input.tc);
		float3 color = col.xyz / col.w;

		float3 L = normalize(shading_info.light.xyz - pos);
		float3 V = normalize(shading_info.eye.xyz - pos);
		float3 N = normalize(normal_texture.Sample(texSampler, input.tc).xyz);
		float3 R = reflect(-V, N);

		float ambient = 0.2f;

		float3 final_color = color * ambient +
			color * max(0.f, (1 - ambient) * dot(N, L))
		  + float3(1.f, 1.f, 1.f) * pow(max(0.f, dot(L, R)), 64);

		return float4(final_color, 1.f);
	}

	return float4(0.1f, 0.1f, 0.1f, 1.f);
}