/****************************************************************************/
/***                                                                      ***/
/***   Tripwire: force-included into the headless_core_gate object only    ***/
/***                                                                      ***/
/****************************************************************************/

/* Phase 2 stage 2.1 split wz4lib/doc.hpp so the document model no longer
   drags in Altona's widget toolkit. Nothing about the include path stops
   someone re-adding a gui include to wz4lib/doc_core.hpp, gui/theme.hpp or
   gui/treeinfo.hpp — the real gui headers are still right there and, on this
   platform, they even parse. A build that merely succeeds proves nothing.

   So rather than hiding the headers, we poison identifiers that ONLY the real
   gui and shader headers declare. `#pragma GCC poison` (clang honours it too)
   makes the compiler reject the token wherever it appears, so pulling one of
   those headers back in fails loudly and names the file:

       error: attempt to use a poisoned identifier

   Three tokens cover everything, because of how gui/ is layered:

     sWindow           gui/window.hpp. Every header in gui/ reaches this one,
                       directly or through gui/gui.hpp — checked, and it is
                       the base class of all of them. It appears nowhere in
                       base/ or util/. Note that poisoning matches whole
                       identifiers, so doc_core.hpp's forward declaration of
                       `struct sWindowDrag` is untouched.
     sGui_             gui/manager.hpp, in case the layering ever changes.
     sSimpleMaterial   util/shaders.hpp — generated from shaders.asc by the
                       asc compiler, which this port does not build.

   sMaterialEnv is deliberately absent even though wPaintInfo uses it: it is
   declared in base/graphics.hpp, not in util/shaders.hpp, so poisoning it
   fails the gate on a header we legitimately need. Found by this tripwire.

   gui/theme.hpp, gui/treeinfo.hpp, gui/palette.hpp and gui/guicolor.hpp are
   deliberately NOT represented. They are the pure-data extractions headless
   code is allowed to use, and they include nothing but base/ — or, for
   guicolor.hpp, nothing at all. See patches 04, 06 and 09.

   base/windows.hpp is also allowed, and does not trip this: it declares
   sWindowModeCodes and sHasWindowFocus but never sWindow, and whole-identifier
   matching is what makes the difference. wz4tex includes it when the FreeType
   font backend is enabled, because Altona's 2D software drawing layer lives
   there and needs no GUI. See patch 09.

   Nothing else in the build sees this file. */

#pragma GCC poison sWindow
#pragma GCC poison sGui_
#pragma GCC poison sSimpleMaterial

/****************************************************************************/
