#ifndef MQTT_CALLBACK_H
#define MQTT_CALLBACK_H

#include <MQTTAsync.h>
#include <iostream>
#include <string>
#include <memory>
#include <functional>
#include <cstring>
#include <atomic>

// Type alias for message handler function
using MessageHandler = std::function<void(const std::string&, const std::string&)>;

/**
 * Global context structure to pass to Paho callbacks
 */
struct MqttContext {
    MessageHandler message_handler;
    std::atomic_bool connected{false};
    MQTTAsync client = nullptr;
    std::atomic_bool subscribed{false};
};

/**
 * Callback function for successful connection
 */
inline void on_connect_success(void* context, MQTTAsync_successData* response) {
    auto ctx = static_cast<MqttContext*>(context);
    std::cout << "[MQTT] Connected successfully" << std::endl;
    ctx->connected = true;
}

/**
 * Callback function for failed connection
 */
inline void on_connect_failure(void* context, MQTTAsync_failureData* response) {
    std::cerr << "[MQTT] Connection failed with code: " 
              << (response ? response->code : -1) << std::endl;
    auto ctx = static_cast<MqttContext*>(context);
    ctx->connected = false;
}

/**
 * Callback function for incoming messages
 */
inline int on_message_arrived(void* context, char* topicName, int topicLen, 
                             MQTTAsync_message* message) {
    auto ctx = static_cast<MqttContext*>(context);
    
    std::string topic;
    if (topicLen > 0) {
        topic.assign(topicName, topicLen);
    } else if (topicName != nullptr) {
        topic.assign(topicName);
    }
    std::string payload((char*)message->payload, message->payloadlen);
    
    std::cout << "[MQTT] Message arrived on topic: " << topic << std::endl;
    std::cout << "[MQTT] Payload: " << payload << std::endl;
    
    // Call custom message handler if provided
    if (ctx->message_handler) {
        try {
            ctx->message_handler(topic, payload);
        } catch (const std::exception& e) {
            std::cerr << "[MQTT] Error in message handler: " << e.what() << std::endl;
        }
    }
    
    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(topicName);
    return 1;
}

/**
 * Callback function for connection lost
 */
inline void on_connection_lost(void* context, char* cause) {
    std::cout << "[MQTT] Connection lost!" << std::endl;
    if (cause && strlen(cause) > 0) {
        std::cout << "[MQTT] Cause: " << cause << std::endl;
    }
    auto ctx = static_cast<MqttContext*>(context);
    ctx->connected = false;
}

/**
 * Callback function for delivery complete
 */
inline void on_delivery_complete(void* context, MQTTAsync_token dt) {
    std::cout << "[MQTT] Message delivery complete for token: " << dt << std::endl;
}

/**
 * Callback function for successful subscription
 */
inline void on_subscribe_success(void* context, MQTTAsync_successData* response) {
    auto ctx = static_cast<MqttContext*>(context);
    ctx->subscribed = true;
    std::cout << "[MQTT] Subscribed successfully" << std::endl;
}

/**
 * Callback function for failed subscription
 */
inline void on_subscribe_failure(void* context, MQTTAsync_failureData* response) {
    auto ctx = static_cast<MqttContext*>(context);
    ctx->subscribed = false;
    std::cerr << "[MQTT] Subscribe failed with code: "
              << (response ? response->code : -1) << std::endl;
}

inline bool publish_mqtt_message(MqttContext& ctx, const std::string& topic, const std::string& payload, int qos) {
    if (!ctx.client || !ctx.connected.load()) {
        std::cerr << "[MQTT] Cannot publish command, client not connected" << std::endl;
        return false;
    }

    MQTTAsync_message message = MQTTAsync_message_initializer;
    message.payload = const_cast<char*>(payload.c_str());
    message.payloadlen = static_cast<int>(payload.size());
    message.qos = qos;
    message.retained = 0;

    MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;

    int rc = MQTTAsync_sendMessage(ctx.client, topic.c_str(), &message, &opts);

    if (rc != MQTTASYNC_SUCCESS) {
        std::cerr << "[MQTT] Command Publish failed with code: " << rc << std::endl;
        return false; 
    }

    std::cout << "[MQTT] Command published to topic: " << topic << std::endl;
    return true;
}

#endif // MQTT_CALLBACK_H
