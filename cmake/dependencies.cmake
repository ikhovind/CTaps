include(FetchContent)

FetchContent_Declare(
        libuv
        GIT_REPOSITORY https://github.com/libuv/libuv.git
        GIT_TAG v1.48.0 # Use a specific, stable version tag
)

FetchContent_MakeAvailable(libuv)