# Semantic tests for the world area: one unit binary per batch of the review's
# rows, so the batches can land independently. Registered here, never in
# OpenGladTests.cmake (see docs/sim-semantics.md).

og_add_unit_group(og_unit_sem_world_1 FILES
    ${CMAKE_SOURCE_DIR}/tests/unit/test_sem_world_1.cpp
)
og_add_unit_group(og_unit_sem_world_2 FILES
    ${CMAKE_SOURCE_DIR}/tests/unit/test_sem_world_2.cpp
)
og_add_unit_group(og_unit_sem_world_3 FILES
    ${CMAKE_SOURCE_DIR}/tests/unit/test_sem_world_3.cpp
)
og_add_unit_group(og_unit_sem_world_4 FILES
    ${CMAKE_SOURCE_DIR}/tests/unit/test_sem_world_4.cpp
)
