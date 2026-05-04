set(AURALBIT_AUDIO_SOURCES
    audio/Decoder.cpp
    audio/Decoder.h
    audio/RingBuffer.h
    audio/AudioOutput.cpp
    audio/AudioOutput.h
    audio/Player.cpp
    audio/Player.h
    audio/decoders/Mp3Decoder.cpp
    audio/decoders/Mp3Decoder.h
    audio/decoders/FlacDecoder.cpp
    audio/decoders/FlacDecoder.h
    audio/decoders/WavDecoder.cpp
    audio/decoders/WavDecoder.h
    audio/decoders/VorbisDecoder.cpp
    audio/decoders/VorbisDecoder.h
)

add_library(auralbit_audio STATIC ${AURALBIT_AUDIO_SOURCES})
set_target_properties(auralbit_audio PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
)
target_include_directories(auralbit_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(auralbit_audio PUBLIC auralbit_thirdparty)

set(AURALBIT_LIBRARY_SOURCES
    library/Schema.h
    library/Database.cpp
    library/Database.h
    library/Scanner.cpp
    library/Scanner.h
)

add_library(auralbit_library STATIC ${AURALBIT_LIBRARY_SOURCES})
set_target_properties(auralbit_library PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
)
target_include_directories(auralbit_library PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(auralbit_library PUBLIC
    auralbit_thirdparty
    PkgConfig::TAGLIB
)

qt_add_executable(auralbit
    main.cpp
    ui/MainWindow.cpp
    ui/MainWindow.h
    ui/LibraryTree.cpp
    ui/LibraryTree.h
    ui/TransportBar.cpp
    ui/TransportBar.h
    ${CMAKE_SOURCE_DIR}/resources/auralbit.qrc
)
target_link_libraries(auralbit PRIVATE
    Qt6::Widgets
    auralbit_audio
    auralbit_library
)
target_include_directories(auralbit PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(auralbit PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

add_executable(auralbit-cli cli_main.cpp)
set_target_properties(auralbit-cli PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
)
target_link_libraries(auralbit-cli PRIVATE
    auralbit_audio
    auralbit_library
)
