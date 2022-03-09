
struct MVP_Struct {
  matrix world_ground;
  matrix world_liquid;
  matrix vp;
};

ConstantBuffer<MVP_Struct> buffer : register(b0);

float4 VSMain(float3 position : POSITION) : SV_POSITION {
  float4 world = mul(buffer.world_ground, float4(position, 1.f));
 
  return mul(buffer.vp, world);
}