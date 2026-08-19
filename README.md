# OmniPath v0.5

OmniPath is an experimental Geode solver for Geometry Dash 2.2081 classic mode, with Android64 as the primary target

## v0.5 architecture

- generation 1 is pure exploration: every agent receives its own random press/release genome and has no obstacle-aware policy
- later generations evolve the input timeline itself through elite selection, block crossover, frontier-focused mutation, and random immigrants
- every shadow owns independent position, velocity, gravity, game mode, button state, collision state, and life state
- collision uses small swept substeps and kills side/embedded overlaps instead of allowing shadows to tunnel through walls or spikes
- each agent's progress comes from its own X position rather than the hidden real player's percent
- the real Geometry Dash player is hidden during training and used only as a camera/speed proxy; it can no longer appear to noclip on screen
- a shadow that reaches the end is replayed from 0% at x1 on the actual Geometry Dash player
- only a real replay that reaches 100% is saved as a verified solution
- the training HUD and launch popup were rebuilt around clear EXPLORE / EVOLVE / VERIFY phases

## build

GitHub Actions builds Android64 and publishes the current package to `dist/OmniPath-Android64.geode`

The workflow also compiles and runs the standalone evolution smoke test before the Geode build
