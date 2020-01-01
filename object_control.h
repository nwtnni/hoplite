#ifndef OBJECT_CONTROL_H
#define OBJECT_CONTROL_H

#include <grpcpp/server.h>
#include <plasma/client.h>
#include <plasma/common.h>
#include <thread>

#include "object_sender.h"
#include "object_store_state.h"

class ObjectStoreServiceImpl;

class GrpcServer {
public:
  GrpcServer(ObjectSender &object_sender, plasma::PlasmaClient &plasma_client,
             ObjectStoreState &state, const std::string &ip, int port);

  inline std::thread Run() {
    return std::thread(&GrpcServer::worker_loop, this);
  }

  bool PullObject(const std::string &remote_grpc_address,
                  const plasma::ObjectID &object_id);

  bool InvokeReduceTo(const std::string &remote_address,
                      const plasma::ObjectID &reduction_id,
                      const plasma::ObjectID &dst_object_id,
                      const std::string &dst_address,
                      const plasma::ObjectID *src_object_id = nullptr);

private:
  void worker_loop();
  const int grpc_port_;
  const std::string &my_address_;
  ObjectStoreState &state_;
  std::unique_ptr<grpc::Server> grpc_server_;
  std::shared_ptr<ObjectStoreServiceImpl> service_;
};

#endif // OBJECT_CONTROL_H