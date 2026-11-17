cmake_minimum_required(VERSION 3.10)
project(frisk C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O2 -g")

# When frisk is configured on its own, build its tests.  When the parent
# AmmarServer project includes this directory, the tests stay in this
# subdirectory and are launched by AmmarServerGames/run_tests.sh; set
# -DFRISK_BUILD_TESTS=ON to build them in the parent tree as well.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(FRISK_STANDALONE ON)
else()
  set(FRISK_STANDALONE OFF)
endif()
option(FRISK_BUILD_TESTS "Build frisk unit and fuzz test binaries"
       ${FRISK_STANDALONE})

option(FRISK_SANITIZE "Build the engine tests with ASan and UBSan" OFF)
if(FRISK_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  set(CMAKE_EXE_LINKER_FLAGS
      "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address,undefined")
endif()

# The engine is plain C and deliberately does not link AmmarServerlib, so the
# same code can be built into frisk_tests and mapcheck without the server.
add_library(frisk_engine STATIC
  json.c
  engine/cards.c
  engine/dice.c
  engine/map.c
  engine/rules.c
  engine/serialize.c
)
target_include_directories(frisk_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(mapcheck tools/mapcheck.c)
target_link_libraries(mapcheck frisk_engine)

# The HTTP service is only built inside the parent AmmarServer tree, where
# AmmarServerDynamic and UserAccounts exist.  A standalone frisk checkout can
# still build and test the pure-C engine.
if(TARGET AmmarServerDynamic AND TARGET UserAccounts)
  add_executable(frisk
    main.c
    http.c
    accounts.c
    admin.c
    lobby.c
    play.c
    games.c
    chat.c
    store.c
  )
  target_link_libraries(frisk frisk_engine pthread rt AmmarServerDynamic UserAccounts)
  set_target_properties(frisk PROPERTIES
                        RUNTIME_OUTPUT_DIRECTORY
                        "${CMAKE_CURRENT_SOURCE_DIR}")
  add_dependencies(frisk AmmarServer)
endif()

if(FRISK_BUILD_TESTS)
  add_executable(frisk_tests tests/test_map.c tests/test_rules.c
                 tests/test_serialize.c tests/test_cards.c)
  target_link_libraries(frisk_tests frisk_engine)

  add_executable(frisk_fuzz tests/fuzz_games.c)
  target_link_libraries(frisk_fuzz frisk_engine)

  enable_testing()
  add_test(NAME mapcheck
           COMMAND mapcheck ${CMAKE_CURRENT_SOURCE_DIR}/res/maps/lotr)
  add_test(NAME frisk_tests
           COMMAND frisk_tests ${CMAKE_CURRENT_SOURCE_DIR}/res/maps/lotr)
  add_test(NAME frisk_fuzz COMMAND frisk_fuzz 200 2000)
endif()
