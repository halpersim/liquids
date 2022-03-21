
struct constant_input{
	float time;
};


ConstantBuffer<constant_input> cs_in : register(b0);
AppendStructuredBuffer<float2> out_buffer : register(u0);


[numthreads(3, 1, 1)]
void CSMain(uint3 id : SV_GroupThreadID){
	float2 vecs[3] = {float2(-0.33f, -0.33f),	float2(0.33f, -0.33f), float2(0.f, 0.33f)};

	float2 offset = 0.5f * float2(cos(cs_in.time), sin(cs_in.time));

	out_buffer.Append(vecs[id.x] + offset);
}