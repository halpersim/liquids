
struct constant_input{
	float time;
};

ConstantBuffer<constant_input> cs_in : register(b0);
AppendStructuredBuffer<float3> out_buffer : register(u0);

[numthreads(3, 1, 1)]
void CSMain(uint3 id : SV_GroupThreadID){
	float3 vecs[3] = {
		float3(-0.33f, -0.33f, 1.f),	
		float3(0.33f, -0.33f, 1.f), 
		float3(0.f, 0.33f, 1.f)};

	float3 offset = 0.5f * float3(cos(cs_in.time), sin(cs_in.time), 2 * cos(cs_in.time));
	//float3 offset = 0.5f * float3(0.f, 0.f, 2 * cos(cs_in.time));
//	float3 offset = 0.5f * float3(0.f, 0.f, 0.f);

	out_buffer.Append(vecs[id.x] + offset);
}