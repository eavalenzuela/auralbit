# Vendored single-header / amalgamation libs.
# All audio codec deps are vendored; no system codec libraries.
# Opus support intentionally omitted.

add_library(auralbit_thirdparty STATIC
    miniaudio_impl.c
    dr_libs_impl.c
    stb_vorbis_impl.c
    sqlite/sqlite3.c
    kissfft_impl.c
)

# SQLite tunables — disable features we won't use to shrink the binary.
target_compile_definitions(auralbit_thirdparty PRIVATE
    SQLITE_DQS=0
    SQLITE_THREADSAFE=2
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
    SQLITE_LIKE_DOESNT_MATCH_BLOBS
    SQLITE_OMIT_DEPRECATED
    SQLITE_OMIT_PROGRESS_CALLBACK
    SQLITE_OMIT_SHARED_CACHE
    SQLITE_USE_ALLOCA
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_ENABLE_FTS5
)

set_target_properties(auralbit_thirdparty PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

target_include_directories(auralbit_thirdparty PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# These libraries are noisy under our project warning flags. They're vendored,
# not our code to fix — silence warnings just for this target.
target_compile_options(auralbit_thirdparty PRIVATE
    -w
)

# miniaudio backends pull in pthread, dl, libm. PulseAudio/ALSA are dlopen'd at
# runtime by miniaudio so no link-time dependency on them.
find_package(Threads REQUIRED)
target_link_libraries(auralbit_thirdparty PUBLIC
    Threads::Threads
    ${CMAKE_DL_LIBS}
    m
)
