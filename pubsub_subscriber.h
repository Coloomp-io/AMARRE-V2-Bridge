#ifndef PUBSUB_SUBSCRIBER_H
#define PUBSUB_SUBSCRIBER_H

#include <google/cloud/pubsub/subscriber.h>
#include <google/cloud/pubsub/subscription.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

namespace gcp = ::google::cloud;

using PubSubCommandHandler = std::function<bool(const std::string&)>;

class PubSubSubscriber {
private:
    std::string project_id_;
    std::string subscription_id_;

public:
    PubSubSubscriber(const std::string& project_id,
                     const std::string& subscription_id)
        : project_id_(project_id), subscription_id_(subscription_id) {}

    void run(PubSubCommandHandler handler, std::atomic_bool& stop_requested) {
        try {
            std::cout << "[PubSubCommands] Starting subscriber..." << std::endl;
            std::cout << "[PubSubCommands] Project ID: " << project_id_ << std::endl;
            std::cout << "[PubSubCommands] Subscription ID: " << subscription_id_ << std::endl;

            auto subscription =
                gcp::pubsub::Subscription(project_id_, subscription_id_);

            auto subscriber = gcp::pubsub::Subscriber(
                gcp::pubsub::MakeSubscriberConnection(subscription));

            auto session = subscriber.Subscribe(
                [&](gcp::pubsub::Message const& message,
                    gcp::pubsub::AckHandler handler_ack) {
                    std::string payload = message.data();

                    std::cout << "[PubSubCommands] Command received" << std::endl;
                    std::cout << "[PubSubCommands] Payload: " << payload << std::endl;

                    bool published = false;
                    try {
                        published = handler(payload);
                    } catch (const std::exception& e) {
                        std::cerr << "[PubSubCommands] Command handler error: "
                                  << e.what() << std::endl;
                    }

                    if (published) {
                        std::move(handler_ack).ack();
                        std::cout << "[PubSubCommands] Command ACKed" << std::endl;
                    } else {
                        std::move(handler_ack).nack();
                        std::cerr << "[PubSubCommands] Command NACKed" << std::endl;
                    }
                });

            while (!stop_requested.load()) {
                if (session.is_ready()) {
                    auto status = session.get();
                    if (status.ok()) {
                        std::cout << "[PubSubCommands] Subscriber session completed"
                                  << std::endl;
                    } else {
                        std::cerr << "[PubSubCommands] Subscriber stopped early: "
                                  << status.message() << std::endl;
                    }
                    return;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }

            session.cancel();
            auto status = session.get();

            if (!status.ok()) {
                std::cerr << "[PubSubCommands] Subscriber stopped: "
                          << status.message() << std::endl;
            } else {
                std::cout << "[PubSubCommands] Subscriber stopped cleanly"
                          << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "[PubSubCommands] Subscriber error: "
                      << e.what() << std::endl;
        }
    }
};

#endif
