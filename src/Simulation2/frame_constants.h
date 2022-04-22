#pragma once 

namespace frame_constants {
	static constexpr unsigned int FRAME_COUNT = 2;

	static constexpr unsigned int PARTICLE_COUNT = 1 << 12;

	static constexpr unsigned int COMPUTE_SHADE_GROUP_SIZE = 256;

	static constexpr float KERNEL_RADIUS = 1.f;
	static constexpr float PRESSURE_CONSTANT = 250.f;
	static constexpr float VISCOSITY_CONSANT = 0.018f;
	static constexpr float REFERENCE_DENSITY = 1.f;
	static constexpr float TIMESTEP = 0.005f;
	static constexpr float INITIAL_DISPLACEMENT = 0.2f;

	static constexpr float SIMULATION_BOX_BOUNDARY[3] = {2.f, 2.f, 2.f};
}