#define THREAD_COUNT 256

struct Pair {
	uint cell_id;
	uint particle_id;
};

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

	//float3 boundary; float3 doesn't work apparently
	float boundary_x;
	float boundary_y;
	float boundary_z;

	float particle_radius;
	uint3 grid_size;
};

RWStructuredBuffer<uint> lookup_buffer : register(u2);
StructuredBuffer<Pair> grid_buffer : register(t2);
ConstantBuffer<SimulationConstants> constants : register(b2);

//this shader calculates the index at which a given cell first appears in the sorted array
[numthreads(THREAD_COUNT, 1, 1)]
void CSMain(uint3 dispatch_thread_id : SV_DispatchThreadID){
	uint id = dispatch_thread_id.x;
	uint cell_id = grid_buffer[id].cell_id;

	InterlockedMin(lookup_buffer[cell_id], id);
}
