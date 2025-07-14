add_executable(generate_dataset generate_dataset.cc ${OPEN_SPIEL_OBJECTS})


target_link_libraries (generate_dataset ${TORCH_LIBRARIES})
