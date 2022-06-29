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

	//float3 boundary; float3 doesn't work apparently
	float boundary_x;
	float boundary_y;
	float boundary_z;

	float particle_radius;
	uint3 grid_size;
};

StructuredBuffer<float> density_buffer : register(t4);
RWStructuredBuffer<float3> velocity_buffer : register(u4);
RWStructuredBuffer<float3> pos_buffer : register(u5);
ConstantBuffer<SimulationConstants> constants : register(b4);

float pressure_at(uint idx);
float3x3 get_rotation_matrix(float3 axis);
//kollisionsbehondlung: 
//keine ahnung ob des passt, owa des hätt i va die gonzn skizzen oloartn meng
//alpha = acos(norm(dot((P2-P1)), norm(v1)))
//v1 = (P2 - P1) * rot(90°) * |v1| * sin(alpha)
//v2 += (P2 - P1) * |v1| * cos(alpha)
//normal sullt eh as supapositionsprinzip göltn also sullts scha so funktioniern

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
		if(i != my_idx) {
			float3 diff = pos_buffer[i] - my_pos;
			float r2 = dot(diff, diff);
			float r = sqrt(r2);

			if(r < constants.particle_radius * 2) {
				float cos_alpha = dot(normalize(my_velocity), normalize(diff));

				if(cos_alpha > 0) { // this particle actively collides with the other one
					if(r2 > 0.0001) {
						my_velocity = mul(get_rotation_matrix(cross(my_velocity, diff)), normalize(diff) * length(my_velocity) * sqrt(1 - cos_alpha * cos_alpha));
					} else {
						my_velocity = float3(0.f, 0.f, 0.f);
					}
				} else {	
					cos_alpha = dot(normalize(velocity_buffer[i]), normalize(-diff));

					if(cos_alpha > 0) { // the other particle plays the active role
						my_velocity += normalize(-diff) * length(velocity_buffer[i]) * cos_alpha;
					}
				}

				my_pos += normalize(-diff) * (2 * constants.particle_radius - length(diff));
			}

			if(0.00001f < r2 && r2 < h2) {
				float h = constants.smoothing_radius;

				float their_pressure = pressure_at(i);
				float pressure_kernel_value = constants.pressure_kernel_constant * pow(h - r, 2);
				float3 dir = diff / r;

				pressure_force += (my_pressure + their_pressure) * pressure_kernel_value * dir / (2 * my_density * density_buffer[i]);

				float r3 = r2 * r;
				float h3 = h2 * h;

				float viscosity_kernel_value = -(r3 / (2 * h3)) + (r2 / h2) + (h / (2 * r)) - 1;

				viscosity_force += (velocity_buffer[i] - my_velocity) * viscosity_kernel_value * dir / density_buffer[i];
			}
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
		if(my_pos.x > constants.boundary_x) {
			normal += float3(-1, 0, 0);
		}

		if(my_pos.y < 0.f) {
			normal += float3(0, 1, 0);
		}
		if(my_pos.y > constants.boundary_y) {
			normal += float3(0,-1, 0);
		}
		
		if(my_pos.z < 0.f) {
			normal += float3(0, 0, 1);
		}
		if(my_pos.z > constants.boundary_z) {
			normal += float3(0, 0,-1);
		}

		if(dot(normal, normal) > 0.1f /*&& dot(normal, my_velocity) < 0.f*/) {
			my_velocity = 0.5f * length(my_velocity) * normalize(reflect(normalize(my_velocity), normalize(normal)));
		//	my_pos = max(float3(0.f, 0.f, 0.f), min(constants.boundary, my_pos));
			my_pos.x = max(0.f, min(constants.boundary_x, my_pos.x));
			my_pos.y = max(0.f, min(constants.boundary_y, my_pos.y));
			my_pos.z = max(0.f, min(constants.boundary_z, my_pos.z));
		}
	}
	
	velocity_buffer[my_idx] = my_velocity;
	pos_buffer[my_idx] = my_pos;
}

float pressure_at(uint idx){
	return constants.pressure_constant * (density_buffer[idx] - constants.reference_density);
}

//returns a matrix representing a -90° rotation around the given axis
float3x3 get_rotation_matrix(float3 axis){
	float3 n = normalize(axis);

	return float3x3(
		float3(n.x * n.x, n.x * n.y + n.z, n.x * n.z - n.y),
		float3(n.x * n.y - n.z, n.y * n.y, n.y * n.z + n.x),
		float3(n.x * n.z + n.y, n.y * n.z - n.x, n.z * n.z)
	);
}