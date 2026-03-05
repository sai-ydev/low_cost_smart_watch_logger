idf_component_register(
    SRCS "main.c" "i2c_bus.c" "max30205.c" "rv8803.c" "lsm6ds3.c" "max30102.c" "spo2_algorithm.c" "ble_server.c"
    INCLUDE_DIRS "."
)

target_link_libraries(${COMPONENT_LIB} m)