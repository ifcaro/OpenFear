add_library(openfear_assets STATIC
    AssetLocator.cpp
    AssetManager.cpp
    CdLayout.cpp
    ExeResourceParser.cpp
    Iso9660Reader.cpp
    IsoAssetProvider.cpp
    IsoFileSectorSource.cpp
    NativeAssetProvider.cpp
    PntFile.cpp
    RectFile.cpp
)
target_include_directories(openfear_assets PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

# EmscriptenAssetProvider uses EM_JS/EM_ASYNC_JS - only compile under emcmake.
if(EMSCRIPTEN)
    target_sources(openfear_assets PRIVATE EmscriptenAssetProvider.cpp)
endif()

if(DEFINED ENV{PS2SDK})
    target_sources(openfear_assets PRIVATE Ps2AssetProvider.cpp)
endif()
