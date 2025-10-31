include(FetchContent)

FetchContent_Declare(
        libuv
        GIT_REPOSITORY https://github.com/libuv/libuv.git
        GIT_TAG v1.48.0 # Use a specific, stable version tag
)

FetchContent_Declare(
        picoquic
        GIT_REPOSITORY https://github.com/private-octopus/picoquic.git
        GIT_TAG 226e2af7b29c2561ce95d34b1cf27eaf44505f24 # Most recent commit as of writing
)

set(PICOQUIC_FETCH_PTLS ON)

FetchContent_MakeAvailable(libuv picoquic)