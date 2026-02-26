## license stuff
write a program which automatically detects all required libraries, pulls all the required license texts from the internet and produces them in the release

## include a .md document that dictates style and architecture etc...
also dicate the project folder names and where files go. clean up this mess of a root project folder

## merge PrimitiveType and g_primitiveTypeNames into the same KVP hash table?
have a giant list which includes all geometry, the KVP primtive names map to the index in that list. when more geometry is loaded or generated at runtime it gets added to that list.

## abstract render target creation
need this for:
- internal reolution scaler
- crt filter
- cubemap rendering? (for light probes)
## internal resolution scale slider

## multiple scenes loadable

## scene baker for release mode which is just the binary

## prebaked static AO
needs multiple vertex types, so lots of requirements there (forgot which ones though)
or maybe light probes?

## todo heightmap as a primitive? or maybe 1 heightmap per scene only? (basic version done)
gen heightmap on vertex shader
->>> sample texture that is editable
change vertex type of heightmap to smallest possible (two 16 bit integers)
(this is a different type of vertex? same with loaded model? and rejig primitive to not use UVs in the vertex?)
quadtree heightmap structure?

## multiple vertex types? (leave one vertex type for now)
- textured lit (pos, norm, uv) usage: loaded models
- textured unlit (pos, uv) usage: skyspheres? unless i do layered onion for clouds?
- position only (pos) usage: greyboxed primitives, fullscreen triangle
- heightfield (x, y, 16bit integers , perhaps UVs or index for texture?) usage: heighfield

## do not duplicate load already loaded textures/heightmap textres

## update objectType in json scene generator to be string text

## only allow certain pipelines for certain objects
- no heighfield or water pipeline for primitive objects etc...
at least in gui so we dont press the wrong one and wonder why it looks weird.

## improved topology of heightfield?
for looking better in low poly style so we do not have the same grid with the same diagonal, use triangle fan structure

## player collision
- collide with heightmap
- collide with objects/primitives

## change primitive render to not sample the texture, just set the greyboxed texture by mathematics in the shader?
maybe not because we need a texture for the sky sphere inverted sphere?

## move heightmap texture to 16-bit

# merge all the scripts into a common folder

## visualise normals as lines
needs multiple shaders at once (done 'pipelines')

## abstract out constant buffer updating because we wil need to be doing it 3+ times in the code

## load and render a 3d model

## shader based UVs that get transformed by scale so greybox texture always 1m by 1m (done triplanar)

## fix triplanar normals

## generate greyboxing texture offline (bake in world dimensions)
multiple textures, like one for the cube, the cylinder, sphere, prism etc...

## generate sky texture offline
use above to have a skelelton for spheremapped textures

## remove all std::vector and move towards custom linear allocator for those particular things

## metaprogram vertex stuff?
should we allow for different kinds of vertices?

## figure out how to speed up intellisense vscode

## global debug mode that temporarily overrides rendering (wireframe, normals visualisation)


## if all heightmaps are found to have the same size and format then switch to Texture2DArray???

## deletable objects in scene

## solve problem of wasd being typed in the UI fields
massive repeating even if input while switching mode on/off, enough to crash program

## non wrapping sampling for heightmaps

## add billboard objecttype and also imposter (animated billboard by viewing angle, with blending between frames), and animated sprite (animated billboard by time t)

## make descision about milk truck wheels problem (child models problem)

## always group sky objects to be rendered last to prevent overdraw and do alpha blending properly

## turn off writing to depth buffer when writing sky clouds?

## instead of writing into the actual field for name, bring up an input box that forces me to press yes/no so i am not stuck typing into the field when i am trying to WASD move.

## aliasing on truck part