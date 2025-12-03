- sometimes, the terrain "flashes" (as if reseting ? caching ? I don't know)
- camera is still looking from top (flying creatures, etc)

- don't think that the depth part of the depth camera is really taken into account.
For example, ash camera depth = 20.0 so it should only look 30cm above ground, but the terrain deform is bigger than the feets

- the trails created by the deformation are not precise enough that we have the definition to the scale of the foot. For example in ash I'd like to have two trails for each feet. 
- the trails have a "darkening" effect, it seems. There is also a kind of "shadow" inside the groove, on the side that is alway the closest to the camera. This is not taking into account the lighting of the scene. When the camera angle is low, close to the ground, this fake shadowing becomes pure black !

- it seems that the industry standard is to deform the terrain + add parallax textures on top. We haven't done that part

- when distant land isn't activated : shader doesn't compile