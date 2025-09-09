<h1 align="center">blok</h1>
<p align="center">
    Efficient voxel raymarcher and terrain editor.
</p>

<p align="center">
    <a href="#-features">Features</a>
    |
    <a href="#-installation">Installation</a>
    |
    <a href="#-wiki">Documentation</a>
</p>

## Table of Contents
- [About](#-about)
- [Requirements](#-requirements)
- [Installation](#-installation)
- [Examples](#-examples)
- [FAQ](#-faq)
- [License](#-license)
- [Citations](#-citations)

## About
**blok** is a voxel path-tracing renderer and terrain editor that serves as a research project into the viability of native *WebGPU* to handle voxel rendering, comparing performance to an *OpenGL* and *CUDA* compute pipeline.

In addition, **blok** serves as an exploration into voxel representations of large terrain simulations, and utilizing SDF to edit said terrain.

*Note: **blok** is a research project being created for the GAM400/CSP400 class at the DigiPen Institute of Technology.*

## Requirements
<p><b>Core:</b> C++20, CMake 3.2</p>
<p><b>Render API:</b> OpenGL 4.6, WebGPU (Dawn)</p>
<p><b>CUDA:</b> CUDA Toolkit >= v12</p>

## Installation
**Step 1: Clone repo**
<pre>
git clone https://github.com/collinlongoria/blok.git
</pre>

**Step 2: Update git submodules (required for some libraries)**
<pre>
git submodule update --init
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
IDK where to find the DigiPen License stuff someone please help me LOL. Here is a link to the DP website: <a href="https://www.digipen.edu/student-portal/student-services/ip-policy#ip-policy" target=_blank>Link</a>

## Citations
<a href="https://graphics.cs.kuleuven.be/publications/BLD14OCCSVO/BLD14OCCSVO_paper.pdf" target="_blank">Baert, Jeroen, et al. “Out-of-Core Construction of Sparse Voxel Octrees.” Computer Graphics Forum, 1 Jan. 1970, graphics.cs.kuleuven.be/publications/BLD14OCCSVO/.</a>

<a href="https://dl.acm.org/doi/abs/10.1145/1882261.1866201" target="_blank">    Michael Schwarz and Hans-Peter Seidel. 2010. Fast parallel surface and solid voxelization on GPUs. ACM Trans. Graph. 29, 6, Article 179 (December 2010), 10 pages. https://doi.org/10.1145/1882261.1866201
</a>(Paywalled) -  sorry