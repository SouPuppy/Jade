# Stanford Bunny

`bunny.obj` is converted from the local
`.temp/bunny/reconstruction/bun_zipper_res4.ply`, the smallest bunny mesh supplied
in this workspace (34,359 bytes before conversion). It contains 453 positions
and 948 triangles. The conversion preserves the original coordinates, face
winding and connectivity, changing only PLY's zero-based indices to OBJ's
one-based indices. Confidence and intensity are omitted because Jade's vertex
format contains position and normal. The OBJ loader generates smooth normals.

Source and credit: Stanford University Computer Graphics Laboratory,
[Stanford 3D Scanning Repository](https://graphics.stanford.edu/data/3Dscanrep/).
The original reconstruction README dates this dataset to August 4, 1996 and
describes the `res*` files as decimated versions intended for interactive rendering.
This low-resolution mesh retains the original decimation's topology defects.

The repository permits research use and free redistribution with attribution;
commercial use requires Stanford's permission. See the source's “Please
acknowledge” section for its terms. This asset has those terms independently of
Jade's code.

The Demo applies scale and translation through `Object` so the source coordinates
stay intact. CMake copies `assets/` beside `JadeDemo.exe`; run the Demo from either
the repository root or its `bin` directory. `Mesh::load()` resolves paths relative
to the process's working directory.
