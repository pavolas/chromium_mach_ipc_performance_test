#include <mach/mach.h>
#include <mach/mach_vm.h>

#define EXIT_ON_MACH_ERROR(msg, retval, success_retval)                        \
  if (kr != success_retval) {                                                  \
    mach_error(msg ":", kr);                                                   \
    exit((retval));                                                            \
  }

bool VerifyMessage(char* message, int size) {
  for (int i = 0; i < size; ++i) {
    if (message[i] != 'a')
      return false;
  }
  return true;
}

char* GenerateMessage(int size) {
  char* msg = (char*)malloc(size);
  for (int i = 0; i < size; i++) {
    msg[i] = 'a';
  }
  return msg;
}

// Sends a mach message.
void SendMessage(mach_port_t other_port, char*msg, int msg_size) {
  int total_size = msg_size + sizeof(mach_msg_header_t);
  void* buffer = malloc(total_size);
  mach_msg_header_t* header = (mach_msg_header_t*)buffer;
  header->msgh_remote_port = other_port;
  header->msgh_local_port = MACH_PORT_NULL;
  header->msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
  header->msgh_reserved = 0;
  header->msgh_size = total_size;
  memcpy((char*)buffer + sizeof(mach_msg_header_t), msg, msg_size);

  kern_return_t kr;
  kr = mach_msg((mach_msg_header_t*)buffer,              // message buffer
                MACH_SEND_MSG,         // option indicating send
                total_size,            // size of header + body
                0,                     // receive limit
                MACH_PORT_NULL,        // receive name
                MACH_MSG_TIMEOUT_NONE, // no timeout, wait forever
                MACH_PORT_NULL);       // no notification port
  EXIT_ON_MACH_ERROR("error sending message", kr, KERN_SUCCESS);
  free(buffer);
}

struct ReceivedMessage {
  char* msg;
  int msg_size;
};

// Receives a mach message. |buffer| is a region of memory that is already
// allocated, and that this function can freely write over.
ReceivedMessage ReceiveMessage(mach_port_t other_port, int starting_buffer_size, void* buffer) {
  int options = MACH_RCV_MSG | MACH_RCV_LARGE;
  bool needs_free = false;

  int kr = mach_msg((mach_msg_header_t*)buffer,                // message buffer
                options,          // option indicating receive
                0,                     // send size
                starting_buffer_size,  // size of header + body
                other_port,            // receive name
                MACH_MSG_TIMEOUT_NONE, // no timeout, wait forever
                MACH_PORT_NULL);       // no notification port
  if (kr == MACH_RCV_TOO_LARGE) {
    mach_msg_header_t* header = (mach_msg_header_t*)buffer;
    int new_buffer_size = header->msgh_size + sizeof(mach_msg_trailer_t);
    buffer = malloc(new_buffer_size);
    needs_free = true;

    kr = mach_msg((mach_msg_header_t*)buffer,                // message buffer
                  options,          // option indicating receive
                  0,                     // send size
                  new_buffer_size,  // size of header + body
                  other_port,            // receive name
                  MACH_MSG_TIMEOUT_NONE, // no timeout, wait forever
                  MACH_PORT_NULL);       // no notification port
  }
  EXIT_ON_MACH_ERROR("error receiving message", kr, KERN_SUCCESS);

  mach_msg_header_t* header = (mach_msg_header_t*)buffer;
  int full_message_size = header->msgh_size + sizeof(mach_msg_trailer_t);
  int char_message_size = full_message_size - sizeof(mach_msg_header_t) - sizeof(mach_msg_trailer_t);
  char* char_message = (char*)malloc(char_message_size);
  memcpy(char_message, (char*)buffer + sizeof(mach_msg_header_t), char_message_size);
  ReceivedMessage result;
  result.msg = char_message;
  result.msg_size = char_message_size;

  if (needs_free)
    free(buffer);
  return result;
}

// Structs used to pass a mach port from client to server.
// send-side version of the request message (as seen by the client)
typedef struct {
  mach_msg_header_t header;
  mach_msg_body_t body;            // start of kernel processed data
  mach_msg_port_descriptor_t data; // end of kernel processed data
} msg1_send_side;
// receive-side version of the request message (as seen by the server)
typedef struct {
  mach_msg_header_t header;
  mach_msg_body_t body;            // start of kernel processed data
  mach_msg_port_descriptor_t data; // end of kernel processed data
  mach_msg_trailer_t trailer;
} msg1_recv_side;
// send-side version of the response message (as seen by the server)
