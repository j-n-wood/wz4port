/****************************************************************************/
/***                                                                      ***/
/***   Including Dear ImGui alongside Altona — wz4port                     ***/
/***                                                                      ***/
/****************************************************************************/
//
// Always include ImGui through this header, never directly.
//
// Altona ends base/types.hpp with
//
//     #define new sDEFINE_NEW                       (types.hpp:1763)
//
// a macro over the `new` keyword, which is a 2005 memory-tracking idiom. Any
// header included afterwards that *declares* an operator new is then mangled
// beyond repair. imgui.h does exactly that, for placement new:
//
//     inline void* operator new(size_t, ImNewWrapper, void* ptr) { return ptr; }
//
// and the result is four cascading parse errors inside imgui.h that say nothing
// about the actual cause — "expected parameter declarator", "function cannot
// return function type".
//
// Include order alone would fix it for one translation unit, and that was the
// first instinct. It is rejected because it is unenforceable: the rule would be
// "ImGui before Altona, in every editor source file, forever", and the failure
// mode when someone gets it wrong is a wall of errors pointing at third-party
// code. push_macro/pop_macro states the hazard once, in the place that owns it,
// and cannot be got wrong by accident.
//
// pop_macro restores rather than drops the definition, so Altona's own
// allocation tracking still applies to the editor's code.

#ifndef FILE_WZ4PORT_EDITOR_IMGUI_WZ4_HPP
#define FILE_WZ4PORT_EDITOR_IMGUI_WZ4_HPP

#pragma push_macro("new")
#undef new

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#pragma pop_macro("new")

/****************************************************************************/

#endif  // FILE_WZ4PORT_EDITOR_IMGUI_WZ4_HPP
