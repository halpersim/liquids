
struct Matrix_Struct {
  float4x4 mvp;
};

struct VSOutput {
  float4 pos : POS;
  float rad : RAD;
};

struct GSOutput {
  float4 pos : SV_POSITION;
  float2 offset : OFFSET;
  float rad : RAD;
};

static float4 TriangleOffsets[4] = {
    float4(-1, 1, 0, 0),
    float4(1, 1, 0, 0),
    float4(-1, -1, 0, 0),
    float4(1, -1, 0, 0),
};

ConstantBuffer<Matrix_Struct> matrices : register(b0);

VSOutput VSMain(float3 pos : POSITION){
  VSOutput ret;

  ret.pos = mul(matrices.mvp, float4(pos, 1.f));
  ret.rad = 0.1f;
  
	return ret;
}

//transform the point into a quad in screen space
[maxvertexcount(4)]
void GSMain(point VSOutput input[1], inout TriangleStream<GSOutput> outStream){
  GSOutput output;

  //emit two new triangles and pass on the vertex output
  for(int i = 0; i < 4; i++) {
    float4 offset = TriangleOffsets[i] * input[0].rad;

    output.pos = offset + input[0].pos;
    
    output.offset = float2(offset.x, offset.y);
    output.rad = input[0].rad;

    outStream.Append(output);
  }
  outStream.RestartStrip();
}


float4 PSMain(GSOutput input) : SV_TARGET {
  //the point is considered the midpoint of a sphere with radius rad
  //the screen space quad is therfore an approximation of the visible hemisphere
  //the position of the fragment on the quad approximates the difference to the sphere's x and y coordinates in view space
  float rad = input.rad;
  float2 offset = input.offset;

  //discard the fragment, if it is not on the projected sphere
  if(dot(offset, offset) > rad * rad) {
    discard;
  }

  //the fragment is closer to the viewer than the sphere's midpoint 
  //therfore its view space depth is negative
  float frag_depth = -sqrt(rad*rad - dot(offset, offset));
  float3 view_space_offset = float3(offset, frag_depth);
  
  return float4(float3(0.4f, 0.3f, 0.2f) + float3(1.f, 1.f, 1.f) * pow(dot(normalize(view_space_offset), float3(0.f, 0.f, -1.f)), 4), 1.f);
}