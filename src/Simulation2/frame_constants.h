#pragma once 

#pragma warning(disable:4244) //double - float missmatch

namespace frame_constants {
	static constexpr unsigned int FRAME_COUNT = 2;

	static constexpr unsigned int PARTICLE_COUNT = 1 << 12;

	static constexpr unsigned int COMPUTE_SHADE_GROUP_SIZE = 256;

	static constexpr float KERNEL_RADIUS = 1.5f;
	static constexpr float PRESSURE_CONSTANT = 250.f;
	static constexpr float VISCOSITY_CONSANT = 0.018f;
	static constexpr float REFERENCE_DENSITY = 1.f;
	static constexpr float TIMESTEP = 0.005f;
	static constexpr float INITIAL_DISPLACEMENT = 0.2f;
	static constexpr float PARTICLE_RADIUS = 0.1f;

	static constexpr float SIMULATION_BOX_BOUNDARY[3] = {7.f, 10.f, 7.f};
	static constexpr float GRAVITY[3] = {0.f, -9.81f, 0.f};


	static constexpr float EYE[3] = {0.f, -2.f, -10.f};
	static constexpr float POI[3] = {0.f, -5.f, 0.f};

	static constexpr unsigned int GRID_SIZE[3] = {SIMULATION_BOX_BOUNDARY[0] / KERNEL_RADIUS + 1, SIMULATION_BOX_BOUNDARY[1] / KERNEL_RADIUS + 1, SIMULATION_BOX_BOUNDARY[2] / KERNEL_RADIUS + 1};
	static constexpr unsigned int GRID_SIZE_FLAT = GRID_SIZE[0] * GRID_SIZE[1] * GRID_SIZE[2];
}
