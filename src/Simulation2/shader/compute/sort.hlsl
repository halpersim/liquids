#define THREAD_COUNT 256

struct Pair {
	uint cell_id;
	uint particle_id;
};

struct ConstInput{
	uint global_stepsize;
	uint local_stepsize;
	uint max_idx;
};

ConstantBuffer<ConstInput> constants : register(b1);
RWStructuredBuffer<Pair> grid_buffer : register(u1);

void compare_swap(uint idx, uint id, uint cur_stepsize);
uint get_idx(uint thread_id, uint stepsize);

//sorts the given array with the bitonic merge sort algorithm
//https://en.wikipedia.org/wiki/Bitonic_sorter
[numthreads(THREAD_COUNT, 1, 1)]
void CSMain(uint3 dispatch_thread_id : SV_DispatchThreadID){
	uint id = dispatch_thread_id.x;

	//if the stepsize is too big, groups have to be synched globally
	if((1 << constants.local_stepsize) >= THREAD_COUNT) {
		uint idx = get_idx(id, constants.local_stepsize);

		compare_swap(idx, id, constants.local_stepsize);
		return;
	}

	int cur_stepsize = constants.local_stepsize;

	while(cur_stepsize >= 0) {
		uint idx = get_idx(id, cur_stepsize);
		compare_swap(idx, id, cur_stepsize);

		if(cur_stepsize != 0) {
			AllMemoryBarrier();
		}

		cur_stepsize--;
	}
}

void compare_swap(uint idx, uint thread_id, uint cur_stepsize){
	if(idx < constants.max_idx) {
		uint my_block = idx >> (cur_stepsize + 1);
		uint next_block_max = ((my_block + 2) << (cur_stepsize + 1)) - 1;

		if(next_block_max >= constants.max_idx) {
			idx += (next_block_max - constants.max_idx) + 1;
		}

		uint other_idx = idx + (1 << cur_stepsize);

		if(other_idx < constants.max_idx) {
			bool ascending = (thread_id & (1 << constants.global_stepsize)) != 0;

			if(grid_buffer[idx].cell_id != grid_buffer[other_idx].cell_id) {

				if((grid_buffer[idx].cell_id < grid_buffer[other_idx].cell_id) != ascending) {
					Pair help = grid_buffer[idx];
					grid_buffer[idx] = grid_buffer[other_idx];
					grid_buffer[other_idx] = help;
				}
			}
		}
	}
}

uint get_idx(uint thread_id, uint stepsize){
	return ((thread_id & ~((1 << stepsize) -1)) << 1) + (thread_id & ((1 << stepsize) - 1));
	
	// the function outputs the following pattern

	// stepsize		0 	1 	2 	3 	
	//		id
	//		0		    0		0		0		0
	//		1				2		1		1		1
	//		2				4		4		2		2
	//		3				6		5		3		3
	//		4				8		8		8		4
	//		5			 10		9		9		5
	//		6			 12	 12  10		6
	//		7			 14  13  11		7
}

