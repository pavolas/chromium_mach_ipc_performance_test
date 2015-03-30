// This program measures the time it takes to send a message to and from
// another process with mach ipc. The message size is determined by the first
// parameter passed to the program. The second parameter is the default buffer
// size when receiving mach messages.
//
// Compile with: clang++ -std=c++11 -stdlib=libc++ -o posix_ipc_measurement posix_ipc_measurement.cc
// Example usage: ./posix_ipc_measurement 2000
// Output: 12741 (exit code 0) indicates that it took 12741 nanoseconds to
// pass a 2000 byte message.
// compile with: clang++ -std=c++11 -stdlib=libc++ -o mach_ipc_measurement mach_ipc_measurement.cc
#include <assert.h>
#include <chrono>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <servers/bootstrap.h>
#include "measurement_common.h"

#define SERVICE_NAME "com.ipctest.server"

mach_port_t BecomeMachServer() {
  // become a Mach server
  kern_return_t kr;
  mach_port_t private_port;
  mach_port_t server_port;
  kr = bootstrap_create_server(bootstrap_port, "test",
      getuid(), FALSE, &private_port);
  EXIT_ON_MACH_ERROR("bootstrap_create_server", kr, BOOTSTRAP_SUCCESS);

  // This sometimes fails when this program is run in quick succession with
  // small message sizes. This is probably because the program's interaction
  // with launchd is asynchronous, and launchd gets confused if the same
  // service is registered twice.
  kr = bootstrap_create_service(private_port, SERVICE_NAME, &server_port);
  EXIT_ON_MACH_ERROR("bootstrap_create_service", kr, BOOTSTRAP_SUCCESS);
  kr = bootstrap_check_in(private_port, SERVICE_NAME, &server_port);
  EXIT_ON_MACH_ERROR("bootstrap_check_in", kr, BOOTSTRAP_SUCCESS);
  return server_port;
}

mach_port_t GetClientPort(mach_port_t server_port) {
  kern_return_t kr;
  msg1_recv_side recv_msg;
  mach_msg_header_t* recv_hdr = &(recv_msg.header);
  recv_hdr->msgh_local_port = server_port;
  recv_hdr->msgh_size = sizeof(recv_msg);
  kr = mach_msg(recv_hdr,              // message buffer
                MACH_RCV_MSG,          // option indicating service
                0,                     // send size
                recv_hdr->msgh_size,   // size of header + body
                server_port,           // receive name
                MACH_MSG_TIMEOUT_NONE, // no timeout, wait forever
                MACH_PORT_NULL);       // no notification port
  EXIT_ON_MACH_ERROR("error getting client port", kr, KERN_SUCCESS);
  mach_port_t other_task_port = recv_msg.data.name;
  return other_task_port;
}

mach_port_t LookupServer() {
  mach_port_t server_port;
  int kr = bootstrap_look_up(bootstrap_port, SERVICE_NAME, &server_port);
  EXIT_ON_MACH_ERROR("bootstrap_look_up", kr, BOOTSTRAP_SUCCESS);
  return server_port;
}

mach_port_t MakeReceivingPort() {
  mach_port_t client_port;
  int kr = mach_port_allocate(mach_task_self(),        // our task is acquiring
                        MACH_PORT_RIGHT_RECEIVE, // a new receive right
                        &client_port);           // with this name
  EXIT_ON_MACH_ERROR("mach_port_allocate", kr, KERN_SUCCESS);
  return client_port;
}

void GiveServerPort(mach_port_t server_port, mach_port_t client_port) {
  msg1_send_side send_msg;
  mach_msg_header_t *send_hdr;
  send_hdr = &(send_msg.header);
  send_hdr->msgh_bits =
      MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0) | MACH_MSGH_BITS_COMPLEX;
  send_hdr->msgh_size = sizeof(send_msg);
  send_hdr->msgh_remote_port = server_port;
  send_hdr->msgh_local_port = MACH_PORT_NULL;
  send_hdr->msgh_reserved = 0;
  send_msg.body.msgh_descriptor_count = 1;
  send_msg.data.name = client_port;
  send_msg.data.disposition = MACH_MSG_TYPE_MAKE_SEND;
  send_msg.data.type = MACH_MSG_PORT_DESCRIPTOR;
  int kr = mach_msg(send_hdr,              // message buffer
                MACH_SEND_MSG,         // option indicating send
                send_hdr->msgh_size,   // size of header + body
                0,                     // receive limit
                MACH_PORT_NULL,        // receive name
                MACH_MSG_TIMEOUT_NONE, // no timeout, wait forever
                MACH_PORT_NULL);       // no notification port
  EXIT_ON_MACH_ERROR("failed to give server port", kr, MACH_MSG_SUCCESS);
}

void ClientRun(int starting_buffer_size) {
  void* starting_buffer = malloc(starting_buffer_size);
  mach_port_t server_port = LookupServer();
  mach_port_t client_port = MakeReceivingPort();
  GiveServerPort(server_port, client_port);

  for (int i = 0; i < 2; ++i) {
    ReceivedMessage m = ReceiveMessage(client_port, starting_buffer_size, starting_buffer);
    SendMessage(server_port, m.msg, m.msg_size);
  }
}

int main(int argc, char* argv[]) {
  assert(argc == 3);
  int message_size = atoi(argv[1]);
  int starting_buffer_size = atoi(argv[2]);
  void* starting_buffer = malloc(starting_buffer_size);
  mach_port_t server_port = BecomeMachServer();

  pid_t childpid;
  if ((childpid = fork()) == -1) {
    perror("fork");
    exit(1);
  }

  if (childpid == 0) {
    ClientRun(starting_buffer_size);
    return 0;
  }

  mach_port_t client_port = GetClientPort(server_port);

  char* msg = GenerateMessage(message_size);

  // Send and receive a message as warm up.
  SendMessage(client_port, msg, message_size);
  ReceivedMessage m = ReceiveMessage(server_port, starting_buffer_size, starting_buffer);
  assert(VerifyMessage(m.msg, m.msg_size));
  assert(m.msg_size == message_size);

  // Send and receive a message, and measure the interval.
  auto start_time = std::chrono::high_resolution_clock::now();
  SendMessage(client_port, msg, message_size);
  m = ReceiveMessage(server_port, starting_buffer_size, starting_buffer);
  auto end_time = std::chrono::high_resolution_clock::now();
  std::chrono::nanoseconds elapsed_seconds = end_time - start_time;
  assert(VerifyMessage(m.msg, m.msg_size));
  assert(m.msg_size == message_size);

  // Divide by 2 to account for sending, and then receiving the exact same
  // message.
  elapsed_seconds /= 2;

  // Print the result and exit.
  printf("%lld\n", elapsed_seconds.count());
}
