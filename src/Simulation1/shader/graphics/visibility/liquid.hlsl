
struct MVP_Struct {
  matrix world_ground;
  matrix world_liquid;
  matrix vp;
};

struct Visibility_Struct{
  float3 eye;
  float radius;
  float offset;
};

ConstantBuffer<MVP_Struct> buffer : register(b0);
ConstantBuffer<Visibility_Struct> visibility_information : register(b1);

static float4 TriangleOffsets[4] = {
    float4(-1, 1, 0, 0),
    float4(1, 1, 0, 0),
    float4(-1, -1, 0, 0),
    float4(1, -1, 0, 0),
};

struct VSOutput {
  float4 pos : POS;
  float rad : RAD;
};

struct GSOutput {
  float4 pos : SV_POSITION;
  float2 offset : OFFSET;
  float radius : RADIUS;
};

VSOutput VSMain(float3 position : POSITION){
  VSOutput result;
  float4 world;

  world = mul(buffer.world_liquid, float4(position, 1.f));
  world += float4(visibility_information.offset * normalize(world.xyz - visibility_information.eye), 0.f);

  result.pos = mul(buffer.vp, world);
  result.rad = visibility_information.radius;
  return result;
}

[maxvertexcount(4)]
void GSMain(point VSOutput input[1], inout TriangleStream<GSOutput> outStream){
  GSOutput output;

  for(int i = 0; i < 4; i++) {
    float4 offset = TriangleOffsets[i] * input[0].rad;
    
    output.pos = TriangleOffsets[i] * input[0].rad + input[0].pos;
    output.offset = float2(offset.x, offset.y);
    output.radius = input[0].rad;

    outStream.Append(output);
  }
  outStream.RestartStrip();
}

void PSMain(GSOutput input){

  if(dot(input.offset, input.offset) > input.radius* input.radius) {
    discard;
  }
}