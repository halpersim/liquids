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


StructuredBuffer<float3> pos_buffer : register(t0);
RWStructuredBuffer<Pair> grid_buffer : register(u0);
ConstantBuffer<SimulationConstants> constants : register(b0);

//assignes a cell_id to each particle
//each cell is one smoothing radius wide, 
//so only particles in adjecent cells have to be considered for the fluid calculations
[numthreads(THREAD_COUNT, 1, 1)]
void CSMain(uint3 dispatch_thread_id : SV_DispatchThreadID){
	uint my_idx = dispatch_thread_id.x;
	float3 my_pos = pos_buffer[my_idx];
	int3 cell_id = floor(my_pos / constants.smoothing_radius);

	int flat_cell_id = cell_id.z * constants.grid_size.x * constants.grid_size.y + cell_id.y * constants.grid_size.x + cell_id.x;

	grid_buffer[my_idx].cell_id = flat_cell_id;
	grid_buffer[my_idx].particle_id = my_idx;
}