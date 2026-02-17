## license stuff
write a program which automatically detects all required libraries, pulls all the required license texts from the internet and produces them in the release

## include a .md document that dictates style and architecture etc...
also dicate the project folder names and where files go. clean up this mess of a root project folder
generated code goes in src/generated

## merge PrimitiveType and g_primitiveTypeNames into the same KVP hash table?
have a giant list which includes all geometry, the KVP primtive names map to the index in that list. when more geometry is loaded or generated at runtime it gets added to that list.

## abstract render target creation
need this for:
- internal reolution scaler
- crt filter
- cubemap rendering (for light probes)

## prebaked static AO
needs multiple vertex types, so lots of requirements there (forgot which ones though)
or maybe light probes?

## todo heightmap as a primitive? or maybe 1 heightmap per scene only?
gen heightmap on vertex shader
need to abstract shader creation (done)

## make custom ingester which outputs the whole project and structure to a text file for copy/paste

# merge all the scripts into a common folder

## modify loc.py to separate written code from the generated code

## visualise normals as lines
needs multiple shaders at once

## abstract out constant buffer updating because we wil need to be doing it 3+ times in the code

## internal resolution scale slider

# load and render a 3d model
drop down of cube, torus, sphere etc...?

## shader based UVs that get transformed by scale so greybox texture always 1m by 1m (done triplanar)

## fix triplanar normals

## generate greyboxing texture offline (bake in world dimensions)
multiple textures, like one for the cube, the cylinder, sphere, prism etc...

## generate sky texture offline
use above to have a skelelton for spheremapped textures



## remove all std::vector and move towards custom linear allocator for those particular things

## metaprogram vertex stuff?
should allow for different kinds of vertices?

## figure out how to speed up intellisense vscode