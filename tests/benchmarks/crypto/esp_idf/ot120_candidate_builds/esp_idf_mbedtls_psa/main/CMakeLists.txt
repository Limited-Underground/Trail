idf_component_register(
    SRCS "candidate_anchor.c"
    REQUIRES mbedtls
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
