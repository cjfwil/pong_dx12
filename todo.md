## license stuff
write a program which automatically detects all required libraries, pulls all the required license texts from the internet and produces them in the release

## basic lighting model with static directional light
need normals generated in metaprogram

## visualise normals as lines
needs multiple shaders at once

## abstract out constant buffer updating because we wil need to be doing it 3+ times in the code

## internal resolution scale slider

# load and render a 3d model
drop down of cube, torus, sphere etc...?

## work on total object list -> draw list
parallelise later

## greyboxing?
requires object list and saving/loading

## gizmos for easy control over transform/rot/scaling greyboxes

## fix UVs on cylinder

## shader based UVs that get transformed by scale so greybox texture always 1m by 1m

## generate greyboxing texture offline (bake in world dimensions)
multiple textures, like one for the cube, the cylinder, sphere, prism etc...

## generate sky texture offline
use above to have a skelelton for spheremapped textures



## remove all std::vector and move towards custom linear allocator for those particular things

## metaprogram vertex stuff?
should allow for different kinds of vertices?