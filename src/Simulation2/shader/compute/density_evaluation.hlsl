#define THREAD_COUNT 256

struct SimulationConstants{
	float smoothing_radius;
	float density_kernel_constant;
	float pressure_kernel_constant;
	float pressure_constant;
	float viscosity_constant;
	float timestep;
	float reference_density;
	uint particle_count;
	float3 gravity;
	float3 boundary;
};

StructuredBuffer<float3> pos_buffer : register(t0);
RWStructuredBuffer<float> density_buffer : register(u0);
ConstantBuffer<SimulationConstants> constants : register(b0);

[numthreads(THREAD_COUNT, 1, 1)]
void CSMain(uint3 dispatch_thread_id : SV_DispatchThreadID){
	uint my_idx = dispatch_thread_id.x;
	float3 my_pos = pos_buffer[my_idx];
	float h2 = constants.smoothing_radius * constants.smoothing_radius;
	float density = 0;

	for(uint i = 0; i < constants.particle_count; i++) {
		float3 diff = pos_buffer[i] - my_pos;
		float r2 = dot(diff, diff);

		if(r2 < h2) {
			density += constants.density_kernel_constant * pow(h2 - r2, 3);
		}
	}
	
	density_buffer[my_idx] = max(constants.reference_density, density);
}