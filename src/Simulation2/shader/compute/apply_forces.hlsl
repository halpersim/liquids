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

StructuredBuffer<float> density_buffer : register(t1);
RWStructuredBuffer<float3> velocity_buffer : register(u1);
RWStructuredBuffer<float3> pos_buffer : register(u2);
ConstantBuffer<SimulationConstants> constants : register(b0);

float pressure_at(uint idx);

[numthreads(THREAD_COUNT, 1, 1)]
void CSMain(uint3 dispatch_thread_id : SV_DispatchThreadID){
	uint my_idx = dispatch_thread_id.x;
	float3 my_pos = pos_buffer[my_idx];

	float my_pressure = pressure_at(my_idx);
	float my_density = density_buffer[my_idx];
	float3 my_velocity = velocity_buffer[my_idx];

	float h2 = constants.smoothing_radius * constants.smoothing_radius;
	
	float3 viscosity_force = float3(0.f, 0.f, 0.f);
	float3 pressure_force = float3(0.f, 0.f, 0.f);

	for(uint i = 0; i < constants.particle_count; i++) {
		float3 diff = pos_buffer[i] - my_pos;
		float r2 = dot(diff, diff);

		if(0.00001f < r2 && r2 < h2) {
			float r = sqrt(r2);
			float h = constants.smoothing_radius;

			float their_pressure = pressure_at(i);
			float pressure_kernel_value = constants.pressure_kernel_constant * pow(h - r, 2);
			float3 dir = diff / r2;

			pressure_force += (my_pressure + their_pressure) * pressure_kernel_value * dir / (2 * my_density * density_buffer[i]);

			float r3 = r2 * r;
			float h3 = h2 * h;

			float viscosity_kernel_value = -(r3 / (2 * h3)) + (r2 / h2) + (h / (2 * r)) - 1;

			viscosity_force += (velocity_buffer[i] - my_velocity) * viscosity_kernel_value * dir / density_buffer[i];
		}
	}

	viscosity_force *= constants.viscosity_constant;

	my_velocity += constants.timestep * ((viscosity_force - pressure_force) / my_density + constants.gravity);
	my_pos += constants.timestep * my_velocity;
	
	//keep the particles in a finite box
	{
		float3 normal = float3(0.f, 0.f, 0.f);

		if(my_pos.x < 0.f) {
			normal += float3(1, 0, 0);
		}
		if(my_pos.x > constants.boundary.x) {
			normal += float3(-1, 0, 0);
		}

		if(my_pos.y < 0.f) {
			normal += float3(0, 1, 0);
		}
		if(my_pos.y > constants.boundary.y) {
			normal += float3(0,-1, 0);
		}
		
		if(my_pos.z < 0.f) {
			normal += float3(0, 0, 1);
		}
		if(my_pos.z > constants.boundary.z) {
			normal += float3(0, 0,-1);
		}

		if(dot(normal, normal) > 0.1f) {
			my_velocity = length(my_velocity) * normalize(reflect(normalize(my_velocity), normalize(normal)));
		}
	}
	
	velocity_buffer[my_idx] = my_velocity;
	pos_buffer[my_idx] = my_pos;
}

float pressure_at(uint idx){
	return constants.pressure_constant * (density_buffer[idx] - constants.reference_density);
}