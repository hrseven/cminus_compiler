add_executable(
    cminusfc
    main.cpp
    cminusf_builder.cpp
)

target_link_libraries(
    cminusfc
    IR_lib
    common
    codegen
    syntax
    stdc++fs
    passes
)

install(
    TARGETS cminusfc
    RUNTIME DESTINATION bin
)
