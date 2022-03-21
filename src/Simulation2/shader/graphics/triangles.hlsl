
struct PSInput {
	float3 color : COLOR;
	float4 pos : SV_POSITION;
};

PSInput VSMain(float2 pos : POSITION){
	PSInput ret;

	ret.color = float3((pos * 0.5 + 0.5) * float2(1.f, 1.f), 0.5f);
	ret.pos = float4(pos, 0.5, 1);

	return ret;
}


float4 PSMain(PSInput input) : SV_TARGET{
	return float4(input.color, 1.f);
}