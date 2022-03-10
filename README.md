# liquids

## Introduction

**liquids** is a collection of different fluid simulations written in C++ using DirectX 12.

The main goal of this project was to get some hands on experience with DirectX 12 and to dive into more advanced rendering topics.

I have written the code all by my self, except for the Setup-Framework, which was copied from the [Microsoft DirectX Samples](https://github.com/Microsoft/DirectX-Graphics-Samples).


## Simulation 1

Simulation 1 uses modified version of the Lattice Boltzmann method for simulating the fluid, and a [particle splatting algorithm](https://www.academia.edu/65014642/Particle_splatting_Interactive_rendering_of_particle_based_simulation_data) proposed by Bart Adams for rendering.

By tweaking different constants it can produce the following results with a frame time of about 150ms (on a Nvidia Geforce 940mx): 

![simulation1_gif1.gif](https://github.com/halpersim/liquids/blob/master/readme/simulation1_gif1.gif)