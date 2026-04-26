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
- walk around in first person (done)
- collide with objects in the world and heightmap (done)
- dynamic npc/enemies type things who walk around and respond in a basic way, can be talked to
- some kind of in game ui system for player interaction
- different modes of transport????

## remove rotation from heightmaps
our descision is that heightmaps cannot be rotated and we operate on that assumption. the rotation matrix for a heightmap is always identity, the UI does not show rotation and neither does the gizmo and the collision system will presume an axis aligned heightmap. it can be scaled, but not rotated - if we change our mind it will probably sstill be in the yaw axis only.
heightmaps can have two types of collisions shapes: heightmap shape (collide directly with the height data) and flat plane shape (collide with flat plane - the heightmap is for visuals only (for example: a rocky surface))
Heightmaps also can ony be scaled in Y and the X/Z together. The Y scale determines the height of the peaks, and the X/Z must be one number which specifies the horizontal scale, so it is always a square.

## collision with multiple heightmaps

## change allow support of heightmaps of 16bits precision

## controller movement and look

## debug camera offset
## debug player cylinder render

## what to do when we are on slight slope

## enforce uniform scaling only on spheres and cylinders in json files and everwhere else it needs to be

## allow switch of cylinder collided to a box, which means we can rotate

## be able to set models with a sphere collider or box collider, or none


## make basic level
need doors that can be open/shut + locked/unlocked + locked/unlocked from one side (latched)?

## replace iterative position solver with a continuous method for colliding with world objects

## decouple list of visible objects compared with colliders? is this a good idea?


## add a universal "player start" position per scene

## add "object group" (editor only) so i can move all objects in the current group by a common origin (ie move the house) to somewhere else

## add a collision shape field per model object? plus enable/disable collision switch?
- make it so that cylinders can be rotated again, but auto switch to a box collider which is always flat facing up for walking up
- add special internal cylinder which acts as the inside of a circular room or pipe or something????
- maybe make a cascading collision based on rotation (if AABB then fast collision -> then if rotated but still upright then like an upright one -> then arbitrary rotation then most sophisticated collision) (this would be an optimisation)

## api boundaries per object
- sky object only can use sky shader
- heightmap object only can use heightmap shader
perhaps this is only in the selector, or maybe it is a guard in the actual code?


## separate object lists
- sky list (base object 0 is used for lighting for all objects, no alpha. plus up to 8 or so alpha cloud layers on top of that), always render last for minimal overdraw potentially turn off depth test, and potentially turn off world matrix, unless we want a fake sky dome to be part of the enviroment like (Ie. The Truman Show)
- enemy list (draw after environment)
- static object list -> can later go by static spatial partition
- semi-dynamic objects (dynamic objects but roughly stay in one place like a door which rotates) maybe that can go in the static object list, but then i dont want to bog down that list since i just want to blast it out
- fully dynamic object list -> cant make any assumptions about its spatial partition 
- viewmodel list (gun, arms, helmet). perhaps even a different shader that blurs closer objects

## doors
- somehow do a door, need to be able to hinge rotate, also requires moving collision boxes


## draw bot state in bot window

## heightmap stuff
- separate heightmap and other static objects and loaded model into separate list
- maybe draw heightmap second to last (after all objects but before sky) to minimise overdraw?