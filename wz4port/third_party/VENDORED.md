# Vendored third-party sources

Pinned, not tracked. Every entry records the exact release and the checksum of
the archive it came from, so the tree can be reproduced or re-verified without
guessing.

The reason for pinning rather than following a branch is the same one that
applies to `sse2neon.h`: a moving dependency makes a byte-exact test suite
meaningless, because a golden mismatch could always be someone else's change.

| What | Version | Source archive SHA-256 |
|---|---|---|
| `sse2neon.h` | v1.9.1 | (single header, see `docs/03-phase-toolchain.md`) |
| `imgui/` | v1.92.9b | `21d8a0a565e85dce943e375db00812c2f3f0ab21f3f0f7964e364a63422d7f99` |
| `glfw/` | 3.5.1 | `5234f4f29473e9a06bc7847d8371858dd135d38466eeeaa652fdc9f8f9ff0c20` |

Archives are `https://github.com/<project>/archive/refs/tags/<tag>.tar.gz`.

## Dear ImGui — v1.92.9b

Only the core plus the two backends this build uses. `docs/`, `examples/` and
`misc/` are dropped; the backends directory keeps GLFW and OpenGL 3 and nothing
else.

```
imgui/imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp
      imgui.h imgui_internal.h imconfig.h imstb_*.h
      imgui_demo.cpp                      -- kept deliberately, see below
      backends/imgui_impl_glfw.{cpp,h}
      backends/imgui_impl_opengl3.{cpp,h} imgui_impl_opengl3_loader.h
```

`imgui_demo.cpp` is kept even though it is not part of the editor. It is the
reference implementation of every widget ImGui has, and phase 5.5 has to build a
parameter panel covering roughly twenty widget kinds from metadata — having the
canonical usage of each one in-tree, at the exact version being compiled against,
is worth the 9,000 lines. It is reachable in the editor under Help.

**No docking.** The phase plan called for a docking layout, and docking is still
not in a tagged ImGui release — it lives on the long-running `docking` branch,
and `ImGuiConfigFlags_DockingEnable` does not exist in v1.92.9b. Pinning a
release matters more here than dockable panes, so the editor lays its panes out
itself. `docs/07-phase-texture-gui.md` records the change.

`imgui_impl_opengl3_loader.h` is ImGui's own minimal GL loader, which is why
there is no GLEW or GLAD here.

## GLFW — 3.5.1

`src/`, `include/`, `CMake/`, `CMakeLists.txt`, `LICENSE.md`. `docs/`,
`examples/`, `tests/` and `deps/` are dropped — all four are only reachable
through `GLFW_BUILD_DOCS`, `GLFW_BUILD_EXAMPLES` and `GLFW_BUILD_TESTS`, which
the editor's CMake sets `OFF` before `add_subdirectory`.

Built from source rather than taken from the system, unlike FreeType. The two are
treated differently on purpose:

- **FreeType is optional.** `GenBitmap.Text` degrades to a documented stub
  without it, so a system package with a graceful fallback is the right trade —
  and it is what let the x86-64 parity build keep working when Homebrew's
  arm64-only dylib would not link.
- **A window is mandatory.** The editor cannot degrade without one, so it is
  vendored: the build works on a machine with nothing installed, and needs no
  `brew install` to modify the user's system.

## Licences

Both are permissive and their licence files are kept alongside the sources:
ImGui is MIT (`imgui/LICENSE.txt`), GLFW is zlib/libpng (`glfw/LICENSE.md`).
