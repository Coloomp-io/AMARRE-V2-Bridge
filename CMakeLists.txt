cmake_minimum_required(VERSION 3.14)
project(MqttToPubSubBridge CXX)

# Require C++17 standard for Google Cloud libraries
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Find the packages downloaded by vcpkg
find_package(nlohmann_json CONFIG REQUIRED)
find_package(eclipse-paho-mqtt-c CONFIG REQUIRED)
find_package(google_cloud_cpp_pubsub CONFIG REQUIRED)

# 2. Create the executable
add_executable(MqttToPubSubBridge main.cpp mqtt_callback.h gcp_config.h pubsub_publisher.h pubsub_subscriber.h)

set(APP_CONFIG_FILE "${CMAKE_SOURCE_DIR}/../config.json")
if (NOT EXISTS "${APP_CONFIG_FILE}")
    set(APP_CONFIG_FILE "${CMAKE_SOURCE_DIR}/config.json")
endif()

add_custom_command(TARGET MqttToPubSubBridge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${APP_CONFIG_FILE}
        $<TARGET_FILE_DIR:MqttToPubSubBridge>/config.json)

# Add include directories
target_include_directories(MqttToPubSubBridge PRIVATE 
    ${CMAKE_SOURCE_DIR}
)

# 3. Link the libraries to the executable
target_link_libraries(MqttToPubSubBridge PRIVATE 
    nlohmann_json::nlohmann_json
    eclipse-paho-mqtt-c::paho-mqtt3as
    google-cloud-cpp::pubsub
)
target_include_directories(MqttToPubSubBridge PRIVATE 
    ${ECLIPSE_PAHO_MQTT_C_INCLUDE_DIRS}
    ${GOOGLE_CLOUD_CPP_PUBSUB_INCLUDE_DIRS}
)
