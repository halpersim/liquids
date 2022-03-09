
struct MVP_Struct {
  matrix world_ground;
  matrix world_liquid;
  matrix vp;
};

struct Visibility_Struct{
  float3 eye;
  float offset;
};

ConstantBuffer<MVP_Struct> buffer : register(b0);
ConstantBuffer<Visibility_Struct> visibility_information : register(b1);

struct VSOutput {
  float4 pos : SV_POSITION;
  float psize : PSIZE;
};

VSOutput VSMain(float3 position : POSITION){
  VSOutput result;
  float4 world;

  if(abs(visibility_information.offset) > 0.01f) {
    world = mul(buffer.world_liquid, float4(position, 1.f));
    world += float4(visibility_information.offset * normalize(world.xyz - visibility_information.eye), 0.f);
  } else {
    world = mul(buffer.world_ground, float4(position, 1.f));
  }

  result.pos = mul(buffer.vp, world);
  result.psize = 5.f;
  return result;
}

[maxvertexcount(4)]
void GSParticleDraw(point VSOutput input[1], inout TriangleStream<GSParticleDrawOut> SpriteStream){
  GSParticleDrawOut output;

  // Emit two new triangles.
  for(int i = 0; i < 4; i++)
  {
    float3 position = g_positions[i] * g_fParticleRad;
    position = mul(position, (float3x3)g_mInvView) + input[0].pos;
    output.pos = mul(float4(position, 1.0), g_mWorldViewProj);

    output.color = input[0].color;
    output.tex = g_texcoords[i];
    SpriteStream.Append(output);
  }
  SpriteStream.RestartStrip();
}
