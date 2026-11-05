# Semantic tests for the walker area: one unit binary per batch of the review's
# rows, so the batches can land independently. Registered here, never in
# OpenGladTests.cmake (see docs/sim-semantics.md).

og_add_unit_group(og_unit_sem_walker_1 FILES
    ${CMAKE_SOURCE_DIR}/tests/unit/test_sem_walker_1.cpp
)
og_add_unit_group(og_unit_sem_walker_2 FILES
    ${CMAKE_SOURCE_DIR}/tests/unit/test_sem_walker_2.cpp
)
og_add_unit_group(og_unit_sem_walker_3 FILES
    ${CMAKE_SOURCE_DIR}/tests/unit/test_sem_walker_3.cpp
)
og_add_unit_group(og_unit_sem_walker_4 FILES
    ${CMAKE_SOURCE_DIR}/tests/unit/test_sem_walker_4.cpp
)
# What the simulation hands each machine for its sounds and display effects
# (#468, #484): placed sounds, cues, and the display marks every toggle off.
og_add_unit_group(og_unit_sem_presentation_1 FILES
    ${CMAKE_SOURCE_DIR}/tests/unit/test_sem_presentation_1.cpp
)
