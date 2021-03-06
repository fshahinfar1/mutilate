// -*- c++-mode -*-
#ifndef OPERATION_H
#define OPERATION_H

#include <string>

using namespace std;

class Operation {
public:
  double start_time, end_time;
  double last_xmit;

  enum type_enum {
    GET, SET, SASL
  };

  type_enum type;

  string key;
  // string value;
  uint64_t value_size;

  double time() const { return (end_time - start_time) * 1000000; }

  int port;

  int req_id;
};


#endif // OPERATION_H
