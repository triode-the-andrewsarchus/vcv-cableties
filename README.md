# VCV Rack Zip Ties (Wiring Harness Manager)

A VCV Rack v2 utility module that modifies cable behavior, enabling users to bundle patch cables together using interactive **Zip Ties**, mimicking real-world wiring harnesses and cable looms.

![Zip Ties Preview](res/ZipTie.svg)

## Features

- **Interactive Slicing / Tie Tool**:
  - Hold a configurable shortcut key (default: `Z`) and drag across any bundle of cables to slice through them.
  - An electric blue cutting line appears with real-time cable intersection detection and glowing wire highlights.
  - A live tooltip displays the number of captured wires (e.g., `Tie 3 wires`, `Tie 6 wires`).
  - Releasing the mouse button secures the cables into a parallel bundle with a nylon zip tie!
- **Real-World Wiring Harness Topologies**:
  - Support for multiple zip ties along any cable path.
  - Realistic breakouts and branching: bundle 6 wires from module A through a tie, split 4 wires to a second tie heading to module B, and branch 2 wires off towards module C.
  - Parallel ribbon offsets through each zip tie to maintain clean wire order and prevent visual tangling.
  - Smooth $C^1$ cubic Bezier spline interpolation with physical gravity sag that respects VCV Rack's cable tension setting.
- **Conflict-Free Pan & Interaction**:
  - Normal rack navigation (middle-click pan, Alt-click pan, right-click pan, box select, and port patching) is 100% unaffected when the shortcut is not held.
  - Dedicated **ARM TOOL** front-panel lighted button and context menu option allows toggling Tie Mode ON without holding any keyboard shortcut.
- **User-Settable Shortcuts & Custom Key-Combo Binding**:
  - Configured directly in the module's right-click context menu.
  - Click **Bind Custom Key / Combo...** and press any key or key combination (e.g., `Z`, `Shift+Z`, `Ctrl+Shift+T`, or modifier-only like `Shift` or `Ctrl+Shift`).
  - Also includes a **Preset Shortcuts** submenu for quick selection.
- **Interactive Zip Tie Manipulation**:
  - **Move**: Click and drag any zip tie to reposition it in real-time; connected cables dynamically re-route smoothly.
  - **Snip / Remove**: Right-click any zip tie to remove it, releasing the cables.
  - **Snip All**: Front-panel button and context menu action to clear all zip ties at once.
- **Customizable Appearance**:
  - **Wire Spacing Knob**: Adjust bundle ribbon spacing (2.0 px to 6.0 px).
  - **Tie Styles**: White Nylon, Dark Carbon, Hi-Viz Yellow, and Crimson.
- **Full Patch Persistence**:
  - All zip ties, positions, and harness topologies are serialized into patch JSON (`dataToJson` / `dataFromJson`).
  - Automatic cleanup when cables or modules are deleted or disconnected.

## Building

Requires the [VCV Rack SDK](https://vcvrack.com/manual/PluginDevelopmentTutorial) with `RACK_DIR` set in your environment.

```bash
make
make dist
make install
```
