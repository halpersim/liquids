
//number of supported directions
static const uint DIRECTIONS_CNT = 19; 

//size in bits of the unit through which the lattice buffers are accessed
static const uint ADDRESS_UNIT_SIZE = 8 * 4; //sizeof(uint) * 8

static const uint BYTE_PER_LATTICE_FOR_BULK_IO = 40;

//defines the index of the direction, if the sign of one component of the direction vector is flipped
static const uint3 inverse_lookup_table[19] = {
	uint3(0, 0, 0),

	//if the sign of the x component of direction 1 (0, 0, 1) is flipped -> it stays direction 1 (0, 0, 1)
	//if the sign of the z component is filpped it becomes (0, 0, -1) -> direction 13
	uint3(1, 2, 1),
	uint3(2, 1, 2),
	uint3(3, 3, 4),
	uint3(4, 4, 3),
	uint3(6, 5, 5),
	uint3(5, 6, 6),

	uint3(17, 7, 18),
	uint3(18, 8, 17),
	uint3(9, 14, 13),
	uint3(10, 13, 14),
	uint3(16, 15, 11),
	uint3(15, 16, 12),

	uint3(13, 10, 9),
	uint3(14, 9, 10),
	uint3(12, 11, 15),
	uint3(11, 12, 16),
	uint3(7, 17, 8),
	uint3(8, 18, 7),
};

static const int3 direction_vectors[19] = {
	int3(0,  0,  0), //0

	int3(0,  1,  0),  //1
	int3(0, -1,  0),  //2
	int3(0,  0,  1),  //3
	int3(0,  0, -1),  //4
	int3(1,  0,  0),  //5
	int3(-1,  0,  0), //6

	int3(1,  0,  1),  //7
	int3(-1,  0, -1), //8
	int3(0,  1,  1),  //9
	int3(0, -1, -1),  //10
	int3(1,  1,  0),  //11
	int3(-1, -1,  0), //12

	int3(0,  1, -1),  //13
	int3(0, -1,  1),  //14
	int3(1, -1,  0),  //15
	int3(-1,  1,  0), //16
	int3(-1,  0,  1), //17
	int3(1,  0, -1),  //18
};

struct ConstInput{
	uint BitPerLatticePointDirection;
	uint BytePerLatticePoint;
	uint global_X;
	uint global_Y;
	uint global_Z;
	uint particle_threshold;
	float unit_length;
	uint run;
	uint PointsPerLattice;
	float timestep;
	float gravity_factor;
	float momentum_exponent;
	uint include_reality_increasing_terms;
};

struct IndirectCommand{
	uint VertexCountPerInstance;
	uint InstanceCount;
	uint StartVertexLocation;
	uint StartInstanceLocation;
};

ConstantBuffer<ConstInput> constants : register(b0);
ByteAddressBuffer lattice_in : register(t0);
RWByteAddressBuffer lattice_out : register(u0);
AppendStructuredBuffer<float4> point_buffer : register(u1);

struct Directions {
	uint dir[DIRECTIONS_CNT];
};

struct MemoryBuffer40 {
	uint4 mem[3];
};


void firstRun(uint3 groupID);
void secondRun(uint3 groupID);

void appendPoints(uint3 lattice_point, uint sum, uint3 momentum);
float calculateWeight(uint dir, float3 momentum, float constant_factor, float speed_of_sound_squared, uint particle_count);

Directions readDirections(uint3 lattice_point);
void writeDirections(uint3 lattice_point, Directions directions);
uint readWriteDirection(uint3 lattice_point, uint direction, uint value, bool read, bool indirect, MemoryBuffer40 buf);
uint getIndexInBuffer(uint3 lattice_point);

uint getNextIndex(uint dir, uint index, uint offset);

void bulkWrite(uint3 lattice_point, MemoryBuffer40 buf);
MemoryBuffer40 bulkRead(uint3 lattice_point);


[numthreads(1, 1, 1)]
void CSMain(uint3 groupID : SV_GroupID) {

	switch(constants.run) {
		case 0: firstRun(groupID); break;
		case 1: secondRun(groupID); break;
	}
}

//in the first run the future distribution for the current lattice point is calculated
void firstRun(uint3 groupID){
	uint i;
	Directions directions = readDirections(groupID);
	uint num_particles = 0;
	uint sum_after = 0;
	float3 momentum = float3(0, 0, 0);
	float weight_sum = 0;
	const uint MAX_VALUE = (1 << constants.BitPerLatticePointDirection) - 1;
	
	//calculate the number of particles and the overall momentum of the particles in the lattice
	for(i = 0; i<DIRECTIONS_CNT; i++) {
		num_particles += directions.dir[i];
		momentum += direction_vectors[i] * int(directions.dir[i]);
	}

	if(num_particles > 0) {
		//physic terms
		float speed_of_sound_squared = 0.f;
		float equalibrium_factor = 0.f;

		//only calculate those physic therms, if it is desired
		if(constants.include_reality_increasing_terms != 0) {
			speed_of_sound_squared = pow(constants.unit_length / constants.timestep, 2) / 3;
			equalibrium_factor = float(dot(momentum, momentum)) / (2 * speed_of_sound_squared);
		}

		for(i = 0; i<DIRECTIONS_CNT; i++) {
			weight_sum += calculateWeight(i, momentum, equalibrium_factor, speed_of_sound_squared, num_particles);
		}


		//change the particles directions according to certain weights
		//this is analoguos of how real particles within the space described by this lattice point
		//would collide and change direction according to differend physics equations
		[unroll(DIRECTIONS_CNT)]
		for(i = 0; i<DIRECTIONS_CNT; i++) {
			//directions are weighted differntly to produce desired results
			//the difference between different simulations lies within those weights
			//to ensure that no particles are created in the distribution step, all weights must sum up to 1
			float weight = calculateWeight(i, momentum, equalibrium_factor, speed_of_sound_squared, num_particles) / weight_sum;

			float equalibrium = weight * float(num_particles);

			directions.dir[i] -= uint(round(constants.timestep * (float(directions.dir[i]) - equalibrium)));

			//at maximum 2^BitsPerLatticePointDirection - 1 can be stored in memory, so the value must no exceed that value 
			directions.dir[i] = min(MAX_VALUE, directions.dir[i]);
			sum_after += directions.dir[i];
		}

		//if there are particles lost or created due to rounding errors in the redistribution process,
		//the values of all directions are somewhat evenly adjusted to ensure that the number of particles stays the same
		//however, this distribution is heavily dependent on the order in which the direction vectors are defined,
		//because directions which are defined first are more likely to be affected by this process
		//therefore the directions vectors have to be defined as uniform as possible 

		//however this method is good enough, if it is not important to produce a flat rectangular prism in the end
		//(also, as of now, it is the only compileable method, because the compiler crashes if the index variable is included in any calculation)
		int particle_diff = num_particles - sum_after;
		int equal_correction = int(particle_diff < 0 ? floor(particle_diff / float(DIRECTIONS_CNT)) : ceil(particle_diff / float(DIRECTIONS_CNT)));

		[unroll(DIRECTIONS_CNT)]
		for(i = 0; particle_diff != 0 && i<DIRECTIONS_CNT; i++) {
			int int_dir = int(directions.dir[i]);
			int optimal_correction = equal_correction;
			int cor = 0;

			//checking for over- and underflows	in directions.dir[i]
			//as well as ensuring that particle_diff becomes exactly 0
			if(particle_diff < 0) {
				cor = max(max(particle_diff, optimal_correction), -int_dir);
			} else if(particle_diff > 0) {
				cor = min(min(particle_diff, optimal_correction), MAX_VALUE - int_dir);
			}

			directions.dir[i] += cor;
			particle_diff -= cor;
		}
	}
	
	writeDirections(groupID, directions);
}

float calculateWeight(uint dir, float3 momentum, float constant_factor, float speed_of_sound_squared, uint particle_count){
	if(dir == 0) {
		return 1.f;
	}

	float direction_component = max(0.f,
		1.f / length(direction_vectors[dir])
		+ constants.gravity_factor * dot(float3(0.f, -1.f, 0.f), normalize(direction_vectors[dir])));

	float momentum_component = 1.f;
	
	if(constants.momentum_exponent > 0.01f) {
		momentum_component = pow(max(0.01f, min(20, constants.particle_threshold * 2.f / float(particle_count) * dot(normalize(direction_vectors[dir]), normalize(momentum)))), constants.momentum_exponent);
	}

	if(constants.include_reality_increasing_terms == 0) {
		return direction_component * momentum_component;
	}

	float momemtum_dot = dot(direction_vectors[dir], momentum);
	float physics_component_squared = 1 + momemtum_dot/speed_of_sound_squared + pow(momemtum_dot / speed_of_sound_squared, 2)/2 - constant_factor;

	if(physics_component_squared < 0.f) {
		return direction_component * momentum_component;
	}
	return direction_component * momentum_component * sqrt(physics_component_squared);
}

//in the second run, the particle redistribution within the lattice calculated in the first run
//is propagated towards the other lattice nodes 
//and the point list for rendering is generated
void secondRun(uint3 groupID){
	Directions directions;
	uint sum = 0;
	uint3 momentum = uint3(0, 0, 0);
	MemoryBuffer40 dummy;
	uint3 max = uint3(constants.global_X, constants.global_Y, constants.global_Z);

	[unroll(DIRECTIONS_CNT)]
	for(uint i = 0; i<DIRECTIONS_CNT; i++) {
		//to avoid race conditions in the lattice buffer, 
		//each lattice node looks up the values of the next step at its neighbors and stores it
		//rather than writing its new values into their neighbors' storage
		int3 other_point = int3(groupID) - direction_vectors[i];
		uint other_dir = i;
		bool reflect = false;

		for(uint k = 0; k<3; k++) {
			//if the other point is not in the lattice (= this point is on the boundary)
			//flip the sign of the corresponding component in the direction vector
			//and sample from the current point
			if(other_point[k] < 0 || uint(other_point[k]) >= max[k]) {
				//the inverse lookup table contains the index of the direction that you get, 
				//if you flip the sign of the current direction vector in the dimension k
				other_dir = inverse_lookup_table[other_dir][k];
				reflect = true;
			}
		}

		if(reflect) {
			other_point = groupID;
		}

		directions.dir[i] = readWriteDirection(uint3(other_point), other_dir, 0, true, false, dummy);
		sum += directions.dir[i];
		momentum += direction_vectors[i] * directions.dir[i];
	}

	//to avoid artifacts, a lattice point is rendered only,
	//if the number of its particles described by it surpases a certain threshold
	if(sum > constants.particle_threshold) {
		appendPoints(groupID, sum, momentum);
	} 

	writeDirections(groupID, directions);
}

void appendPoints(uint3 lattice_point, uint sum, uint3 momentum){
	float3 _point = float3(
		lattice_point.x * constants.unit_length,
		lattice_point.y * constants.unit_length,
		lattice_point.z * constants.unit_length);

	switch(constants.PointsPerLattice) {
		case 8:
		{
			float offset = constants.unit_length * 0.25f;

			for(int i = 0; i<8; i++) {
				float3 offset_vector;

				offset_vector.x = (i&1) ? offset : -offset;
				offset_vector.y = (i&2) ? offset : -offset;
				offset_vector.z = (i&4) ? offset : -offset;

				point_buffer.Append(float4(_point + offset_vector, sum));
			} 
		} break;
		case 1: 
		default: point_buffer.Append(float4(_point, sum)); break;
	}
}

//----------------- input output functions ----------------------
Directions readDirections(uint3 lattice_point){
	Directions directions;
	MemoryBuffer40 buf;
	
	if(constants.BytePerLatticePoint != BYTE_PER_LATTICE_FOR_BULK_IO) {
		//i kept the slower but more flexible implementation, so i can play around changing different constants

		[unroll(DIRECTIONS_CNT)]
		for(uint i = 0; i < DIRECTIONS_CNT; i++) {
			directions.dir[i] = readWriteDirection(lattice_point, i, 0, true, false, buf);
		}
	} else {
		//bulk read the directions
		buf = bulkRead(lattice_point);

		[unroll(DIRECTIONS_CNT)]
		for(uint i = 0; i < DIRECTIONS_CNT; i++) {
			directions.dir[i] = readWriteDirection(lattice_point, i, 0, true, true, buf);
		}
	}

	return directions;
}

void writeDirections(uint3 lattice_point, Directions directions){
	MemoryBuffer40 buf;

	if(constants.BytePerLatticePoint != BYTE_PER_LATTICE_FOR_BULK_IO) {
		//i kept the slower but more flexible implementation, so i can play around changing different constants

		for(uint dir = 0; dir < DIRECTIONS_CNT; dir++) {
			readWriteDirection(lattice_point, dir, directions.dir[dir], false, false, buf);
		}
	} else {		
		//bulk write the directions

		buf.mem[0] = uint4(0, 0, 0, 0);
		buf.mem[1] = uint4(0, 0, 0, 0);
		buf.mem[2] = uint4(0, 0, 0, 0);

		/*
		//loop as it should be
		//however it causes the compiler to crash somehow

		[unroll(DIRECTIONS_CNT)]
		for(uint dir = 0; dir < DIRECTIONS_CNT; dir++) {
			uint i = (dir * constants.BitPerLatticePointDirection) / ADDRESS_UNIT_SIZE;
			buf.mem[i >> 2][i & 3] = readWriteDirection(lattice_point, dir, directions.dir[dir], false, true, buf);  // <----------- Compiler Crash
		}
		*/
		/*
		// workaround 1
		// increases compile time significantly
		// turned out to be even worse than storing the values individually

		for(uint dir = 0; dir < DIRECTIONS_CNT; dir++) {
			uint i = (dir * constants.BitPerLatticePointDirection) / ADDRESS_UNIT_SIZE;
			uint x = i >> 2;
			uint y = i & 3;
			uint temp = readWriteDirection(lattice_point, dir, directions.dir[dir], false, true, buf);

			switch(x) {
				case 0:
					switch(y) {
						case 0: buf.mem[0][0] = temp; break;
						case 1: buf.mem[0][1] = temp; break;
						case 2: buf.mem[0][2] = temp; break;
						case 3: buf.mem[0][3] = temp; break;
					}
					break;

				case 1:
					switch(y) {
						case 0: buf.mem[1][0] = temp; break;
						case 1: buf.mem[1][1] = temp; break;
						case 2: buf.mem[1][2] = temp; break;
						case 3: buf.mem[1][3] = temp; break;
					}
					break;
				case 2:
					switch(y) {
						case 0: buf.mem[2][0] = temp; break;
						case 1: buf.mem[2][1] = temp; break;
					}
					break;
			}
		}
		*/

		// workaround 2
		// still increases compile time 
		// however is faster than storing the values indiviually by around a third

		buf.mem[0][0] = readWriteDirection(lattice_point, 0, directions.dir[0], false, true, buf);
		buf.mem[0][0] = readWriteDirection(lattice_point, 1, directions.dir[1], false, true, buf);

		buf.mem[0][1] = readWriteDirection(lattice_point, 2, directions.dir[2], false, true, buf);
		buf.mem[0][1] = readWriteDirection(lattice_point, 3, directions.dir[3], false, true, buf);

		buf.mem[0][2] = readWriteDirection(lattice_point, 4, directions.dir[4], false, true, buf);
		buf.mem[0][2] = readWriteDirection(lattice_point, 5, directions.dir[5], false, true, buf);

		buf.mem[0][3] = readWriteDirection(lattice_point, 6, directions.dir[6], false, true, buf);
		buf.mem[0][3] = readWriteDirection(lattice_point, 7, directions.dir[7], false, true, buf);

		buf.mem[1][0] = readWriteDirection(lattice_point, 8, directions.dir[8], false, true, buf);
		buf.mem[1][0] = readWriteDirection(lattice_point, 9, directions.dir[9], false, true, buf);

		buf.mem[1][1] = readWriteDirection(lattice_point, 10, directions.dir[10], false, true, buf);
		buf.mem[1][1] = readWriteDirection(lattice_point, 11, directions.dir[11], false, true, buf);

		buf.mem[1][2] = readWriteDirection(lattice_point, 12, directions.dir[12], false, true, buf);
		buf.mem[1][2] = readWriteDirection(lattice_point, 13, directions.dir[13], false, true, buf);

		buf.mem[1][3] = readWriteDirection(lattice_point, 14, directions.dir[14], false, true, buf);
		buf.mem[1][3] = readWriteDirection(lattice_point, 15, directions.dir[15], false, true, buf);

		buf.mem[2][0] = readWriteDirection(lattice_point, 16, directions.dir[16], false, true, buf);
		buf.mem[2][0] = readWriteDirection(lattice_point, 17, directions.dir[17], false, true, buf);

		buf.mem[2][1] = readWriteDirection(lattice_point, 18, directions.dir[18], false, true, buf);


		bulkWrite(lattice_point, buf);
	}
}

uint getIndexInBuffer(uint3 lattice_point){
	//the number of the particles for all directions for a given lattice point are stored
	//in consecutive constants.BytePerLatticePoint-byte blocks 
	//for which the index is calculated as follows
	uint lattice_point_index = lattice_point.z * constants.global_Y * constants.global_X + lattice_point.y * constants.global_X + lattice_point.x;
	//however, the contents of a ByteAddressBuffer are adressed by the offset in bytes from the beginning
	return lattice_point_index * constants.BytePerLatticePoint;
}

MemoryBuffer40 bulkRead(uint3 lattice_point){
	uint idx = getIndexInBuffer(lattice_point);
	MemoryBuffer40 buf;

	buf.mem[0] = lattice_in.Load4(idx);
	buf.mem[1] = lattice_in.Load4(idx + 16);
	buf.mem[2] = uint4(lattice_in.Load2(idx + 32), 0, 0);

	return buf;
}

void bulkWrite(uint3 lattice_point, MemoryBuffer40 buf){
	uint idx = getIndexInBuffer(lattice_point);
	
	lattice_out.Store4(idx, buf.mem[0]);
	lattice_out.Store4(idx + 16, buf.mem[1]);
	lattice_out.Store2(idx + 32, buf.mem[2].xy);
}

//reads or writes the number of particles for one direction from the corresponding buffer
uint readWriteDirection(uint3 lattice_point, uint direction, uint value, bool read, bool indirect, MemoryBuffer40 buf) {
	uint original_idx = getIndexInBuffer(lattice_point);
	uint idx = original_idx;

	//directions are stored in consecutive constants.BitPerLatticePointDirection-bit blocks beginning at first bit of the lattice point
	//the offset in bits of a given direction within the Lattice Point Block is calculated as follows
	uint offset = direction * constants.BitPerLatticePointDirection;

	//the index given to the read write buffer is the offset in bytes from the beginning
	//however, it can only load and store 4 Byte values to indices divisible by 4, which is why the ADDRESS_UNIT_SIZE is 32 Bit.
	//therefore the index has to be increased by 4 for every 32 Bit step away from the beginning of the block
	idx += (offset / ADDRESS_UNIT_SIZE) << 2;

	//the offset has to be reduced by the amount the index has increased
	offset %= ADDRESS_UNIT_SIZE;
	
	uint value_out = 0;
	uint bits_left = constants.BitPerLatticePointDirection;

	//for each bit pattern the requested value is contained in
	do {
		//the offset of the most significant bit of the value to read in the bit pattern 
		uint start = offset;
		//the offset of the least significant bit of the value to read in the bit pattern 
		uint end = start + bits_left - 1;

		//if the end is bigger than the ADDRESS_UNIT_SIZE, parts of the requested value are in the next bit pattern
		//these are read in the next iteration
		bits_left = max(0, (int)(end - ADDRESS_UNIT_SIZE + 1));

		if(end >= ADDRESS_UNIT_SIZE) {
			end = ADDRESS_UNIT_SIZE - 1;
		}

		//the mask is initialised with as many 1s as there are bits of the requested value contained in the current pattern
		uint mask = ~0;
		mask >>= ADDRESS_UNIT_SIZE - (end - start + 1);

		//how many times the mask has to be shiffed to the left so that its 1s align with the bits of interest in the current pattern
		uint in_value_shift = (ADDRESS_UNIT_SIZE - end - 1);
		//how many times the mask has to be shifted to the left so that its 1s represent the bits of the value retrieved in this loop iteration
		uint out_value_shift = bits_left;

		//the values have to be switched in case of writing
		if(!read) {
			uint temp = in_value_shift;
			in_value_shift = out_value_shift;
			out_value_shift = temp;
		}

		mask <<= in_value_shift;

		int value_in;
		
		if(read) {
	
			if(indirect) {
				uint i = idx - original_idx;

				value_in = buf.mem[i >> 4][((i >> 2)&3)] & mask;
			} else {
				value_in = lattice_in.Load(idx) & mask;
			}
			
			
			//value_in = lattice_in.Load(idx) & mask;
		} else {
			value_in = value & mask;
		}
		
		//now all bits of interest are in value_in,
		//but they might not be at the right position, so that they can simply be OR combined with the already retrieved values
		int shift_diff = in_value_shift - out_value_shift;

		if(shift_diff > 0) {
			value_in >>= shift_diff;
			mask >>= shift_diff;
		} else if(shift_diff < 0) {
			value_in <<= (-shift_diff);
			mask <<= (-shift_diff);
		}

		
		if(read) {
			//in case of read, value_out is initialized with 0, so it can be safely combined
			value_out |= value_in;
		} else {
			if(indirect) {
				uint i = idx - original_idx;

				value_out = ((~mask) & buf.mem[i >> 4][(i >> 2)&3]) | value_in;

				
			} else {
				lattice_out.Store(idx, ((~mask) & lattice_out.Load(idx)) | value_in);
			}
		}

		offset = end + 1;
		if(offset == ADDRESS_UNIT_SIZE) {
			idx += 4;
			offset = 0;
		}
	} while(bits_left > 0);


	return value_out;
}