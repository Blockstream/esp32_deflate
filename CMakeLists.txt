idf_component_register(
    SRCS "deflate.c"
    INCLUDE_DIRS "."
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Ofast)
target_compile_options(${COMPONENT_LIB} PRIVATE -DNDEBUG)
