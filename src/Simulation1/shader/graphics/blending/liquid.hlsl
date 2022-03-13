struct MVP_Struct {
  matrix world_ground;
  matrix world_liquid;
  matrix vp;
};

struct PointInformation_Struct {
  matrix invers_vw;
  float radius;
  float epsilon;
  uint particle_threshold;
  uint color_upper_bound;
};

static float4 TriangleOffsets[4] = {
    float4(-1, 1, 0, 0),
    float4(1, 1, 0, 0),
    float4(-1, -1, 0, 0),
    float4(1, -1, 0, 0),
};

ConstantBuffer<MVP_Struct> matrices : register(b0);
ConstantBuffer<PointInformation_Struct> point_info : register(b2);

struct VSOutput {
  float3 world_pos : WORLD_POS;
  float4 nds_pos : NDS_POS;
  float rad : RAD;
  float cnt : CNT;
};

struct GSOutput {
  float3 world_pos : WORLD_POS;
  float4 nds_pos : SV_POSITION;
  float2 offset : OFFSET;
  float rad : RAD;
  float cnt : CNT;
};

struct PSOutput {
  float4 pos : SV_TARGET0;
  float4 normal : SV_TARGET1; 
  float4 color : SV_TARGET2;
};

float3 lerp3(float3 A, float3 B, float3 C, float t){
  return lerp(lerp(A, B, t), lerp(B, C, t), t);
}

float3 lerp4(float3 A, float3 B, float3 C, float3 D, float t){
  return lerp3(lerp(A, B, t), lerp(B, C, t), lerp(C, D, t), t);
}

VSOutput VSMain(float4 position : POSITION) {
  VSOutput result;

  result.cnt = position.w;
  result.world_pos = mul(matrices.world_liquid, float4(position.xyz, 1.f)).xyz;
  result.nds_pos = mul(matrices.vp, float4(result.world_pos, 1.f));
  result.rad = point_info.radius;

  return result;
}

//transform the point into a quad in screen space
[maxvertexcount(4)]
void GSMain(point VSOutput input[1], inout TriangleStream<GSOutput> outStream){
  GSOutput output;

  //emit two new triangles and pass on the vertex output
  for(int i = 0; i < 4; i++) {
    float4 offset = TriangleOffsets[i] * input[0].rad;
    
    output.offset = float2(offset.x, offset.y);
    output.nds_pos = offset + input[0].nds_pos;
    output.world_pos = input[0].world_pos;
    output.rad = input[0].rad;
    output.cnt = input[0].cnt;

    outStream.Append(output);
  }
  outStream.RestartStrip();
}


PSOutput PSMain(GSOutput input) {
  PSOutput result;
  
  //the point is considered the midpoint of a sphere with radius rad
  //the screen space quad is therfore an approximation of the visible hemisphere
  //the position of the fragment on the quad approximates the difference to the sphere's x and y coordinates in view space
  float rad = input.rad;
  float2 offset = input.offset;
  
  //discard the fragment, if it is not on the projected sphere
  if(dot(offset, offset) > rad*rad) {
    discard;
  }

  //the fragment is closer to the viewer than the sphere's midpoint 
  //therfore its view space depth is negative
  float frag_depth = -sqrt(rad*rad - dot(offset, offset));

  float3 view_space_offset = float3(offset, frag_depth);
  float3 world_space_offset = mul(point_info.invers_vw, float4(view_space_offset, 0.f)).xyz;

  float3 pos = input.world_pos + world_space_offset;
  float3 normal = normalize(world_space_offset);
  float3 color = float3(0.1f, 0.58f, 0.69f);

  //weights according to the paper by Adams et. al.
  float weights[2];
  weights[0] = max(0.f, 1.f - sqrt(dot(offset, offset))/rad);
  weights[1] = max(0.f, (point_info.epsilon + frag_depth) / point_info.epsilon);

  float weight = weights[0] * weights[1];

  result.pos = float4(pos, 1.f);
  result.normal = float4(weight * normal, 1.f);
  
  if(point_info.particle_threshold > point_info.color_upper_bound) {
    //give the liquid one uniform color      
    result.color = float4(weight * color, weight);
  } else {
    //color the liquid differently based on the number of particles in the given region
    float t = smoothstep(point_info.particle_threshold, point_info.color_upper_bound, input.cnt);
    result.color = float4(lerp3(float3(0.9f, 0.2f, 0.1f), float3(0.f, 0.f, 1.f), float3(0.2f, 0.9f, 0.3f), t) * weight, weight);
    //result.color = float4(lerp4(float3(0.8f, 0.2f, 0.1f), float3(0.7f, 0.f, 0.9f), float3(0.f, 0.5f, 1.f), float3(0.2f, 0.9f, 0.3f), t) * weight, weight);
  }
  
  return result;
}