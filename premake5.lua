local sdk = os.getenv("PLUGIN_SDK_DIR")
assert(sdk, "PLUGIN_SDK_DIR must be set")
workspace "LibertyBounce"
    location "build"
    configurations { "Release" }
    architecture "x86"
    system "windows"
    systemversion "latest"
project "LibertyBounce"
    kind "SharedLib"
    language "C++"
    cppdialect "C++latest"
    toolset "v143"
    staticruntime "On"
    runtime "Release"
    optimize "On"
    characterset "MBCS"
    targetextension ".asi"
    targetdir "bin"
    objdir "build/obj"
    files { "src/**.cpp", "src/**.h" }
    defines { "GTA3", "PLUGIN_SGV_10EN", "RW", "_CRT_SECURE_NO_WARNINGS", "_CRT_NON_CONFORMING_SWPRINTFS" }
    includedirs { sdk .. "/plugin_III", sdk .. "/plugin_III/game_III", sdk .. "/plugin_III/game_III/enums", sdk .. "/plugin_III/game_III/rw", sdk .. "/shared", sdk .. "/shared/game" }
    libdirs { sdk .. "/output/lib" }
    links { "Plugin_III", "user32", "winmm" }
