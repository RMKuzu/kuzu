add_library(kuzu_parser_transform
        OBJECT
        transform_copy.cpp
        transform_ddl.cpp
        transform_expression.cpp
        transform_graph_pattern.cpp
        transform_macro.cpp
        transform_projection.cpp
        transform_query.cpp
        transform_reading_clause.cpp
        transform_standalone_call.cpp
        transform_updating_clause.cpp)

set(ALL_OBJECT_FILES
        ${ALL_OBJECT_FILES} $<TARGET_OBJECTS:kuzu_parser_transform>
        PARENT_SCOPE)
