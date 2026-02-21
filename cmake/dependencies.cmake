include(FetchContent)

FetchContent_Declare(
        libuv
        GIT_REPOSITORY https://github.com/libuv/libuv.git
        GIT_TAG v1.48.0 # Use a specific, stable version tag
)

FetchContent_Declare(
        picoquic
        GIT_REPOSITORY https://github.com/private-octopus/picoquic.git
        GIT_TAG d399d3a0689b6add881d4a1b48b21d374b56fbf1
)

set(PICOQUIC_FETCH_PTLS ON)

FetchContent_MakeAvailable(libuv picoquic)
