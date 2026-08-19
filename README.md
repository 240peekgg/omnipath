# OmniPath v0.4

Geode mod for Geometry Dash 2.2081 focused on parallel evolutionary solving

## What changed in v0.4

- one generation now runs the whole population at the same time instead of replaying one real player N times
- every candidate has independent position, velocity, gravity, mode, input state and collision state
- every candidate is rendered as its own translucent ghost cube
- cube logic plans around contiguous spike clusters, so double/triple spikes are treated as one jump window
- ship/wave/ufo/robot/ball/spider/swing have separate controllers and lightweight physics models
- portals, pads and rings are applied independently per simulated candidate
- the live Geometry Dash player is only an invulnerable world/camera proxy during training
- when a shadow candidate reaches the end, its exact input trace is replayed once on real Geometry Dash physics at x1 speed
- only a replay that actually reaches 100% is stored as the verified solved macro

Android64 builds are published by GitHub Actions to `dist/OmniPath-Android64.geode`
