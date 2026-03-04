### Exclusive Fullscreen and window resolutions
This engine will NOT support exclusive fullscreen. To support users who want to run at a different resolution to their monitor there will be an internal scaler option stretching from 50% to 200%.
In windowed mode there will be a drop down list of common resolutions in the supported aspect ratios (4:3, 16:9, 21:9).
Borderless will always display at the resolution of the desktop.

### Heightmap and Heightfield rules
Heightfields cannot be rotated, always assumed to be axis aligned.
Heightfields always have the same X and Z scale.
Defintion of Heightfield: the geometry that is displaced and rendered
Defintion of Heightmap: the texture that provides the displacement information to the heightfield displacer

## Player collision
Player in collision space is always represented by an upright cylinder. This cylinder cannot be rotated. The only permitted changes are: the radius and the top and bottom caps.

## Sphere primtives
Sphere collision primitves may only be scaled uniformly by one scalar radius.

## Cylinder primtives
Cylinder collision primitves may only be scaled uniformly by one scalar radius.

# Potential Rules (review needed)
- Collidable cylinders may be rotated and scaled, but only if they are for walking ontop of only, and they will not be collided with horizontally. Potentially same with spheres. However, do not see the application of this at the moment, and adding this will add complexity to UI (we could potentially have a "allow rotation, walkway only" switch).