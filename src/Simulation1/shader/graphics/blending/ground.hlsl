struct MVP_Struct {
  matrix world_ground;
  matrix world_liquid;
  matrix vp;
};

struct PointInformation_Struct {
  float radius;
  float epsilon;
};

ConstantBuffer<MVP_Struct> matrices : register(b0);
ConstantBuffer<PointInformation_Struct> point_info : register(b2);

struct PSOutput {
  float4 pos : SV_TARGET0;
  float4 normal : SV_TARGET1;
  float4 color : SV_TARGET2;
};


struct VSOutput {
  float3 world_pos : WORLD_POS;
  float2 tc : TC;
  float3 normal : NORMAL;
  float4 nds_pos : SV_POSITION;
};

VSOutput VSMain(float3 position : POSITION, float2 tc : TC, float3 normal : NORMAL){
  VSOutput result;

  result.world_pos = mul(matrices.world_ground, float4(position, 1.f)).xyz;
  result.nds_pos = mul(matrices.vp, float4(result.world_pos, 1.f));
  result.tc = tc;
  result.normal = mul(matrices.world_ground, float4(normal, 0.f)).xyz;

  return result;
}

PSOutput PSMain(VSOutput input){
  PSOutput result;

  //output a simple chessboard pattern
  int x = int(input.tc.x * 10);
  int y = int(input.tc.y * 10);

  float value = (x+y)&1 ? 0.2f : 0.8f;

  result.pos = float4(input.world_pos, 1.f);
  result.normal = float4(normalize(input.normal), 1.f);
  result.color = float4(value, value, value, 1.f);

  return result;
}