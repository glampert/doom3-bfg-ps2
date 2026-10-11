# Authored headless map

This is one small integration map for native `idGameLocal::InitFromNewMap`, not retail
content. `make test-map` stages these files under an isolated `host:game-fixture` root.

- `headless.map` contains a solid floor from (-128,-128,-8) to (128,128,0), worldspawn
  and one native `idAI` with an explicit 16×16×32 collision box. It has no mesh or
  animation. Its native actor state runs an idle script with `waitFrame`, using static
  movement and `neverDormant` because there is no player supplying visibility.
- `headless.proc` supplies two CPU floor models, two areas split at x=0, one portal
  and solid space below z=0. The portal is tested open, closed and reopened. Models
  use stage-free `solid collision` material, with clockwise triangle indices as
  required by the native plane/trace convention.
- `headless.aas48` is hand-authored navigation data, not dmap/AAS compiler output.
  Two convex cells cover (-120,-120,0) to (120,120,96), with opposite plane pairs,
  one shared face, floor faces, two walk reachabilities and one routing cluster.
  Its (-8,-8,0) to (8,8,32) bounding box matches the actor. The BSP rejects positions
  outside this deliberately bounded navigation region. Native AAS loading validates
  the map geometry CRC `1090519040` (`0x41000000`); recompute it when changing geometry.
- Script events query reachability to the opposite cell and to an out-of-region
  position. The latter must return -1 through the actor's AAS binding, rather than
  the no-AAS straight-line fallback. Map and AI scripts each advance once per game tick.

Collision conversion, navigation loading, PVS construction and query behavior run
through the native engine. No drawing or audible playback is requested. The empty
native sound world supplies startup bookkeeping only. Campaign player startup,
animated AI navigation/combat, Common map transitions and retail content remain later work.
