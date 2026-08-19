/****************************************************************************/
/***                                                                      ***/
/***   Altona installation configuration — wz4port build                  ***/
/***                                                                      ***/
/***   Derived from altona_wz4/altona/altona_config_sample.hpp.           ***/
/***                                                                      ***/
/***   Upstream expects this file at "altona/altona_config.hpp" and       ***/
/***   includes it as "../altona_config.hpp" from base/types.hpp. We do   ***/
/***   NOT put it there — instead it lives here and is found via the      ***/
/***   include directory wz4port/compat/include, because a quoted include ***/
/***   that fails to resolve next to the including file is retried        ***/
/***   against each -I directory with the relative path intact:           ***/
/***                                                                      ***/
/***       <compat/include>/../altona_config.hpp  ==  this file           ***/
/***                                                                      ***/
/***   That keeps altona_wz4/ completely untouched. See wz4port/README.md ***/
/***                                                                      ***/
/****************************************************************************/

#ifndef FILE_WZ4PORT_ALTONA_CONFIG_HPP
#define FILE_WZ4PORT_ALTONA_CONFIG_HPP

/****************************************************************************/
/***   Directories                                                        ***/
/****************************************************************************/

// Only consumed by makeproject and a few tool paths. We do not build
// makeproject, so these exist to satisfy the preprocessor.

#define sCONFIG_CODEROOT_WINDOWS  L"."
#define sCONFIG_CODEROOT_LINUX    L"."
#define sCONFIG_DATAROOT          L"."
#define sCONFIG_MP_TEMPLATES      L"altona/tools/makeproject/makeproject.txt"
#define sCONFIG_CONFIGFILE        L"altona/altona_config.hpp"
#define sCONFIG_TOOLBIN           L"."
#define sCONFIG_VSVERSION         L"2010"

#define sCONFIG_MP_EXEPOSTFIX_CONFIG  0

#define sCONFIG_INTERMEDIATEROOT  L""
#define sCONFIG_OUTPUTROOT        L""

/****************************************************************************/
/***   Project GUID                                                       ***/
/****************************************************************************/

// base/types.hpp:2143 defines sGetProjectGUID() as
//     sGUID g = sCONFIG_GUID;
// but types.hpp itself only supplies sCONFIG_GUID on iOS (:590). Every
// other platform expects the build system to pass it per project —
// makeproject emits it into each .vcproj, and the upstream Linux bootstrap
// makefiles pass it on the command line.
//
// Nothing in the headless path depends on this value being distinct, so we
// define one here rather than fighting brace-and-comma escaping in -D
// flags. sGUID is { sU32 Data32; sU16 Data16[3]; sU8 Data8[6]; }.

#define sCONFIG_GUID  { 0xB405548B, { 0xF306, 0x45C9, 0x856B }, \
                        { 0x76, 0x7B, 0xB3, 0xF6, 0xB6, 0xE0 } }

/****************************************************************************/
/***   SDKs — all off                                                     ***/
/****************************************************************************/

// Direct3D is Windows-only; Cg has been discontinued since 2012; XSI and
// Gecko are irrelevant here. Nothing we build needs any of them: the
// headless libraries use the "blank" renderer and no shader compiler.

#define sCONFIG_SDK_CHAOS         0
#define sCONFIG_SDK_DX9           0
#define sCONFIG_SDK_DX11          0
#define sCONFIG_SDK_CG            0
#define sCONFIG_SDK_XSI           0
#define sCONFIG_SDK_GECKO         0

/****************************************************************************/
/***   Project file generation — unused, we build with CMake              ***/
/****************************************************************************/

#define sCONFIG_MP_VS_WIN32       0
#define sCONFIG_MP_VS_WIN64       0
#define sCONFIG_MP_MAKE_MINGW     0
#define sCONFIG_MP_MAKE_LINUX     0

/****************************************************************************/

#endif  // FILE_WZ4PORT_ALTONA_CONFIG_HPP
