<h1 align="center">blok</h1>
<p align="center">
    Advanced voxel engine for mesh conversion and terrain exploration.
</p>

<p align="center">
    <a href="#about">About</a>
    |
    <a href="#installation">Installation</a>
    |
    <a href="https://github.com/collinlongoria/blok/wiki">Documentation</href>
</p>

## Table of Contents
- [About](#about)
- [Requirements](#requirements)
- [Installation](#installation)
- [Examples](#examples)
- [FAQ](#faq)
- [License](#license)
- [Citations](#citations)

## About
**blok** is a mesh voxelization and terrain editing toolkit that demonstrates the technical potential of voxel-based rendering pipelines. It combines OpenGL, CUDA, and Vulkan 1.4 backends to support both real-time and path-traced visualization, exploring how hybrid rasterization and compute techniques can be used to represent and manipulate complex 3D geometry at the voxel level.

Designed as a research platform, **blok** investigates the future of voxel rendering—how modern GPU architectures can accelerate voxelization, editing, and rendering workflows for large-scale environments.

*Note: **blok** is a research project being created for the GAM400/CSP400 class at the DigiPen Institute of Technology.*

## Requirements
<p><b>Core:</b> C++20, CMake 3.2</p>
<p><b>Render APIs:</b> OpenGL 4.6, Vulkan 1.4</p>
<p><b>SPIR-V version:</b> 1.6 and below</p>
<p><b>CUDA:</b> CUDA Toolkit >= v12</p>
<p><b>Vulkan Extensions:</b></p>
<oi>
    <li> VK_KHR_swapchain </li>
    <li> VK_KHR_dynamic_rendering </li>
    <li> VK_KHR_synchronization2 </li>
    <li> VK_KHR_deferred_host_operations </li>
    <li> VK_KHR_acceleration_structure </li>
    <li> VK_KHR_ray_tracing_pipeline </li>
</oi>

## Installation
**Step 1: Clone repo**
<pre>
git clone https://github.com/collinlongoria/blok.git
</pre>

**Step 2: Update git submodules (required for some libraries)**
<pre>
git submodule update --init --recursive
</pre>

**Step 3: Run CMake at repo root**
<pre>
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build && ./build/Debug/bin/blok
</pre>

## Examples
Links to examples of project running will go here.

## FAQ
FAQ will go here.

## License
<p>
As a condition of your accessing this area, you agree to be bound by the following terms and conditions: 

The games software was created by students of DigiPen Institute of Technology (DigiPen), and all copyright and other rights in such is owned by DigiPen. While DigiPen allows you to access, download and use the software for non-commercial, home use you hereby expressly agree that you will not otherwise copy, distribute, modify, or (to the extent not otherwise permitted by law) decompile, disassemble or reverse engineer the games software. 

This game may collect player behavior information during game sessions for debugging and gameplay balancing purposes. The game does not collect personally identifiable information of any kind.

THE GAMES SOFTWARE IS MADE AVAILABLE BY DIGIPEN AS-IS AND WITHOUT WARRANTY OF ANY KIND BY DIGIPEN. DIGIPEN HEREBY EXPRESSLY DISCLAIMS ANY SUCH WARRANTY, INCLUDING, WITHOUT LIMITATION, ANY IMPLIED WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. 

WITHOUT LIMITING THE GENERALITY OF THE FOREGOING, DIGIPEN SHALL NOT BE LIABLE IN DAMAGES OR OTHERWISE FOR ANY DAMAGE OR INJURY FROM DOWNLOADING, DECOMPRESSING, RUNNING OR ATTEMPTING TO RUN, USING OR OTHERWISE DEALING WITH, IN ANY WAY, THE GAMES SOFTWARE CONTAINED IN THIS AREA, NOR SHALL DIGIPEN BE LIABLE FOR ANY INCIDENTAL, CONSEQUENTIAL, EXEMPLARY OR OTHER TYPES OF DAMAGES ARISING FROM ACCESS TO OR USE OF THE GAMES SOFTWARE. 

YOU HEREBY AGREE TO INDEMNIFY, DEFEND AND HOLD HARMLESS DIGIPEN AND ITS DIRECTORS, OFFICERS, EMPLOYEES, AGENTS, CONSULTANTS AND CONTRACTORS AGAINST ALL LIABILITY OF ANY KIND ARISING OUT OF YOUR DOWNLOADING, DECOMPRESSING, RUNNING OR ATTEMPTING TO RUN, USING OR OTHERWISE DEALING WITH, IN ANY WAY, THE GAMES SOFTWARE. 

DIGIPEN MAKES NO WARRANTIES OR REPRESENTATIONS THAT THE GAMES SOFTWARE IS FREE OF MALICIOUS PROGRAMMING, INCLUDING, WITHOUT LIMITATION, VIRUSES, TROJAN HORSE PROGRAMS, WORMS, MACROS AND THE LIKE. AS THE PARTY ACCESSING THE GAMES SOFTWARE IT IS YOUR RESPONSIBILITY TO GUARD AGAINST AND DEAL WITH THE EFFECTS OF ANY SUCH MALICIOUS PROGRAMMING. 

</p>

## Citations
<a href="https://graphics.cs.kuleuven.be/publications/BLD14OCCSVO/BLD14OCCSVO_paper.pdf" target="_blank">Baert, Jeroen, et al. “Out-of-Core Construction of Sparse Voxel Octrees.” Computer Graphics Forum, 1 Jan. 1970, graphics.cs.kuleuven.be/publications/BLD14OCCSVO/.</a>

<a href="https://dl.acm.org/doi/abs/10.1145/1882261.1866201" target="_blank">    Michael Schwarz and Hans-Peter Seidel. 2010. Fast parallel surface and solid voxelization on GPUs. ACM Trans. Graph. 29, 6, Article 179 (December 2010), 10 pages. https://doi.org/10.1145/1882261.1866201

</a>


