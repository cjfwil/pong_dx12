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

## shadow system
- self shadowing (for example cliff shadowing own heightmap)
- PCF (Percentage‑Closer Filtering) - most basic version, performance but not fully realistic
- Percentage-Closer Soft Shadows (PCSS) for dynamic over area penumbra
- VSM / ESM (Variance / Exponential Shadow Maps) - baked and fast, but can be innacurate for some types of objects
- world space capsule/sphere occulsion for dynamic objects
- baked lighting
- distinguish or scale smoothly between from overcast days versus clear days

## audio and music

## actually playable thing/"game"
- walk around in first person
- collide with objects in the world and heightmap
- dynamic npc/enemies type things who walk around and respond in a basic way, can be talked to
- some kind of in game ui system for player interaction
- different modes of transport????

## remove rotation from heightmaps
our descision is that heightmaps cannot be rotated and we operate on that assumption. the rotation matrix for a heightmap is always identity, the UI does not show rotation and neither does the gizmo and the collision system will presume an axis aligned heightmap. it can be scaled, but not rotated - if we change our mind it will probably sstill be in the yaw axis only.
heightmaps can have two types of collisions shapes: heightmap shape (collide directly with the height data) and flat plane shape (collide with flat plane - the heightmap is for visuals only (for example: a rocky surface))
Heightmaps also can ony be scaled in Y and the X/Z together. The Y scale determines the height of the peaks, and the X/Z must be one number which specifies the horizontal scale, so it is always a square.

## collision with multiple heightmaps

## collision with primitives
for each scene object that is a cube, cylinder or sphere, we will collide with.

we want to have the "just work" option - we plop down a primitive of cylinder, sphere, cube, and it will just work based on any non-uniform scale that is thrown at it. if it is slow or unoptimised - it doesnt matter. i just want a perfect 1:1 correspondence for what i see rendered and what i can walk on. there is also the triangle prism primtive but i dont know how to calculate collision against that, so we will leave that for later. if it is too slow we can add limitations back in, like uniform scaling only or whatever. because i do want to walk around on top of rotated cubes and such. and have smooth slopes, that is crucial to our gameplay.

when the player is standing on top of the primitive, they will not slide or anything.

perhaps there is a difference between running into a collision box from the side, like going up against a wall and standing on top? perhaps we should split them? we will start with calculate where my Y position should for each primitive(non prism) and heightmap on the scene and the highest Y value would be set as the position of the camera.y+1.7f. maybe we can worry about head on collision later?

#### implementation
start with just cubes
define a line in 3d space - it is defined by the vertical line at y = f(x_0, z_0), where x_0 and z_0 is the camera position in x and z. now we have a vertical line we can intersect with each object. for each object we then do the maths to tranform the line? i guess? or a downward facing vector which is at position {x_0, 0, z_0} transform in reverse so it is mathematically identical, or inverse world matrix? then we can get the the intersection point in local space -> transform it back by the world which gets me, my actual y, which then we overwrite if the next y is greater than that y, skip if it is less

## change allow support of heightmaps of 16bits precision

## controller movement