/****************************************************************************/
/***                                                                      ***/
/***   Altona functions declared for every platform but defined only for   ***/
/***   Windows                                                            ***/
/***                                                                      ***/
/****************************************************************************/

// These are gaps in upstream, not in our port: base/system.hpp declares them
// unconditionally and only base/system_win.cpp defines them. Supplying them
// here rather than patching base/system_linux.cpp keeps the upstream footprint
// down, and these are genuinely our decisions about what they should mean on a
// POSIX console.

#include "base/system.hpp"

/****************************************************************************/

// Declared at base/system.hpp:780; defined only at base/system_win.cpp:3050,
// where it polls the PAUSE key with GetAsyncKeyState.
//
// It is a user-abort check for long operator evaluations —
// wExecutive::Execute (doc.cpp:4034, :4047) drains it before a build and then
// tests it per command, and basic.cpp:258/:322 poll it inside long loops.
//
// A headless build has no key state to poll and no interactive user to abort,
// so the answer is always "not pressed". A CLI that wants Ctrl+C to interrupt a
// build should install a SIGINT handler and have this report it; that is worth
// doing when wz4gen exists (phase 3), not before.

sBool sCheckBreakKey()
{
  return 0;
}

/****************************************************************************/
