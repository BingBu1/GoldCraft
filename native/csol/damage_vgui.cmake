# Compile the matching upstream controls in a private static library. No copied
# SDK sources or modifications to Renderer / VGUI2Extension are required.
set(METAHOOK_SOURCE_PATH "${METAHOOK_ROOT}/MetaHook")
include("${METAHOOK_ROOT}/Plugins/CaptionMod/cmake/Sources.cmake")
set(CSOL_VGUI_SOURCES ${CAPTIONMOD_SOURCES})
list(FILTER CSOL_VGUI_SOURCES INCLUDE REGEX "/include/(SourceSDK|vgui_controls)/")
add_library(csol_damage_vgui STATIC ${CSOL_VGUI_SOURCES})
target_compile_features(csol_damage_vgui PRIVATE cxx_std_20)
get_target_property(CSOL_DAMAGE_INCLUDES CSOLSupport INCLUDE_DIRECTORIES)
set(CSOL_DAMAGE_VGUI_INCLUDES "${SDK}/VGUI" "${SDK}/Interface/VGUI"
    "${SDK}/vgui_controls" "${SDK}/SourceSDK/vstdlib"
    "${METAHOOK_ROOT}/Plugins/VGUI2Extension/include/Interface")
set(CSOL_DAMAGE_VGUI_DEFINITIONS VGUI_USE_SURFACE2 VGUI_USE_SCHEME2 VGUI_USE_INPUT2
    NO_MALLOC_OVERRIDE NO_TIER0_MINIDUMP NO_VCR)
target_include_directories(csol_damage_vgui PRIVATE ${CSOL_DAMAGE_INCLUDES} ${CSOL_DAMAGE_VGUI_INCLUDES})
target_compile_definitions(csol_damage_vgui PRIVATE WIN32 _WINDOWS NOMINMAX _CRT_SECURE_NO_WARNINGS ${CSOL_DAMAGE_VGUI_DEFINITIONS})
target_compile_options(csol_damage_vgui PRIVATE /W3 /wd4996)
set_source_files_properties(client/damage_options.cpp client/buy_menu.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${CSOL_DAMAGE_VGUI_INCLUDES}"
    COMPILE_DEFINITIONS "${CSOL_DAMAGE_VGUI_DEFINITIONS}")
target_link_libraries(CSOLSupport PRIVATE csol_damage_vgui imm32 winmm)
