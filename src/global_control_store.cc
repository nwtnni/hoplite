#include <grpc/grpc.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <string.h>

#include "global_control_store.h"
#include "logging.h"

using objectstore::GetObjectLocationReply;
using objectstore::GetObjectLocationRequest;
using objectstore::ObjectCompleteReply;
using objectstore::ObjectCompleteRequest;
using objectstore::ObjectIsReadyReply;
using objectstore::ObjectIsReadyRequest;
using objectstore::SubscriptionReply;
using objectstore::SubscriptionRequest;
using objectstore::UnsubscriptionReply;
using objectstore::UnsubscriptionRequest;
using objectstore::WriteObjectLocationReply;
using objectstore::WriteObjectLocationRequest;

using namespace plasma;

class NotificationListenerImpl final
    : public objectstore::NotificationListener::Service {
public:
  NotificationListenerImpl(
      std::unordered_set<ObjectNotifications *> &object_notifications)
      : objectstore::NotificationListener::Service(),
        object_notifications_(object_notifications) {}

  grpc::Status ObjectIsReady(grpc::ServerContext *context,
                             const ObjectIsReadyRequest *request,
                             ObjectIsReadyReply *reply) {
    for (auto notifications : object_notifications_) {
      notifications->ReceiveObjectNotification(
          ObjectID::from_binary(request->object_id()));
    }
    reply->set_ok(true);
    return grpc::Status::OK;
  }

private:
  std::unordered_set<ObjectNotifications *> &object_notifications_;
};

ObjectNotifications::ObjectNotifications(std::vector<ObjectID> object_ids) {
  for (auto object_id : object_ids) {
    pending_.insert(object_id);
  }
}

std::vector<ObjectID> ObjectNotifications::GetNotifications() {
  std::unique_lock<std::mutex> l(notification_mutex_);

  std::vector<ObjectID> notifications;
  notification_cv_.wait(l, [this]() { return !ready_.empty(); });
  for (auto &object_id : ready_) {
    notifications.push_back(object_id);
  }
  ready_.clear();
  return notifications;
}

void ObjectNotifications::ReceiveObjectNotification(const ObjectID &object_id) {
  std::unique_lock<std::mutex> l(notification_mutex_);
  if (pending_.find(object_id) == pending_.end()) {
    return;
  }
  pending_.erase(object_id);
  ready_.insert(object_id);
  l.unlock();
  notification_cv_.notify_one();
}

GlobalControlStoreClient::GlobalControlStoreClient(
    const std::string &redis_address, int port, const std::string &my_address,
    int notification_port, int notification_listen_port)
    : redis_address_(redis_address), my_address_(my_address),
      notification_port_(notification_port),
      notification_listen_port_(notification_listen_port),
      service_(std::make_shared<NotificationListenerImpl>(notifications_)) {
  std::string grpc_address =
      my_address + ":" + std::to_string(notification_listen_port);
  grpc::ServerBuilder builder;
  builder.AddListeningPort(grpc_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&*service_);
  grpc_server_ = builder.BuildAndStart();
  auto remote_notification_server_address =
      redis_address_ + ":" + std::to_string(notification_port_);
  notification_channel_ = grpc::CreateChannel(
      remote_notification_server_address, grpc::InsecureChannelCredentials());
  notification_stub_ =
      objectstore::NotificationServer::NewStub(notification_channel_);
}

void GlobalControlStoreClient::write_object_location(
    const ObjectID &object_id, const std::string &my_address) {
  LOG(INFO) << "[RedisClient] Adding object " << object_id.hex()
            << " to Redis with address = " << my_address << ".";
  grpc::ClientContext context;
  WriteObjectLocationRequest request;
  WriteObjectLocationReply reply;
  request.set_object_id(object_id.binary());
  request.set_ip(my_address);
  notification_stub_->WriteObjectLocation(&context, request, &reply);
  DCHECK(reply.ok()) << "WriteWriteObjectLocation for " << object_id.binary()
                     << " failed.";
}

std::string
GlobalControlStoreClient::get_object_location(const ObjectID &object_id) {
  grpc::ClientContext context;
  GetObjectLocationRequest request;
  GetObjectLocationReply reply;
  request.set_object_id(object_id.binary());
  notification_stub_->GetObjectLocation(&context, request, &reply);
  return std::string(reply.ip());
}

ObjectNotifications *GlobalControlStoreClient::subscribe_object_locations(
    const std::vector<ObjectID> &object_ids, bool include_completed_objects) {
  ObjectNotifications *notifications = new ObjectNotifications(object_ids);
  {
    std::lock_guard<std::mutex> guard(gcs_mutex_);
    notifications_.insert(notifications);
  }

  for (auto object_id : object_ids) {
    grpc::ClientContext context;
    SubscriptionRequest request;
    SubscriptionReply reply;
    request.set_subscriber_ip(my_address_);
    request.set_object_id(object_id.binary());
    notification_stub_->Subscribe(&context, request, &reply);

    DCHECK(reply.ok()) << "Subscribing object " << object_id.hex()
                       << " failed.";
  }

  if (include_completed_objects) {
    for (auto object_id : object_ids) {
      if ("" != get_object_location(object_id)) {
        notifications->ReceiveObjectNotification(object_id);
      }
    }
  }

  return notifications;
}

void GlobalControlStoreClient::unsubscribe_object_locations(
    ObjectNotifications *notifications) {
  std::lock_guard<std::mutex> guard(gcs_mutex_);

  notifications_.erase(notifications);

  delete notifications;
}

void GlobalControlStoreClient::PublishObjectCompletionEvent(
    const ObjectID &object_id) {

  grpc::ClientContext context;
  ObjectCompleteRequest request;
  ObjectCompleteReply reply;
  request.set_object_id(object_id.binary());
  auto status = notification_stub_->ObjectComplete(&context, request, &reply);
  DCHECK(status.ok()) << "ObjectComplete gRPC failure, message: "
                      << status.error_message();
  DCHECK(reply.ok()) << "Object completes " << object_id.hex() << " failed.";
}

void GlobalControlStoreClient::worker_loop() {
  LOG(INFO) << "[GCSClient] Gcs client " << my_address_ << " started";

  grpc_server_->Wait();
}
