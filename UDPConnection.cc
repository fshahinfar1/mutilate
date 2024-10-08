#include <arpa/inet.h>
#include <event2/event.h>
#include <sys/uio.h>
#include <unistd.h>

#include <string>

#include "ConnectionOptions.h"
#include "ConnectionStats.h"
#include "Operation.h"
#include "UDPConnection.h"
#include "binary_protocol.h"
#include "log.h"

#define RETRANSMIT_INTERVAL 0.01 /* seconds */

size_t OpQueue::size()
{
  return list.size();
}

void OpQueue::pop(Operation *op)
{
  for (auto i = list.begin(); i != list.end(); ++i) {
    if (&*i == op) {
      list.erase(i);
      return;
    }
  }
  assert(0);
}

void OpQueue::push(Operation op)
{
  list.push_back(op);
}

Operation *OpQueue::find(uint16_t req_id)
{
  for (Operation &op: list)
    if (op.req_id == req_id)
      return &op;
  return NULL;
}

Operation *OpQueue::earliest_last_xmit(void)
{
  assert(!list.empty());
  Operation *ret = &list.front();
  for (Operation &op: list)
    if (op.last_xmit < ret->last_xmit)
      ret = &op;
  return ret;
}

std::list<Operation>::iterator OpQueue::begin()
{
  return list.begin();
}

std::list<Operation>::iterator OpQueue::end()
{
  return list.end();
}

void UDPConnection::pop_op(Operation *op)
{
  assert(op_queue.size() > 0);

  op_queue.pop(op);

  if (op_queue.size() > 0)
    read_state = WAITING_FOR_GET;
  else
    read_state = IDLE;
}

Operation *UDPConnection::consume_udp_binary_response(char *data, size_t length)
{
  assert(length >= sizeof(udp_header_t) + 24);
  udp_header_t *udp_header = (udp_header_t *) data;
  binary_header_t *h = (binary_header_t *) (udp_header + 1);

  Operation *op = op_queue.find(ntohs(udp_header->req_id));
  assert(udp_header->seq_no == 0);
  assert(udp_header->datagrams == ntohs(1));
  assert(udp_header->reserved == 0);

  assert(h->magic == 0x81);
  assert(h->opcode == CMD_GET);

  size_t targetLen = sizeof(udp_header_t) + 24 + ntohl(h->body_len);
  assert(length == targetLen);
  op->value_size = h->body_len;

  // if something other than success, count it as a miss
  if (op && h->status)
    stats.get_misses++;

  stats.rx_bytes += targetLen;
  return op;
}

void UDPConnection::read_callback()
{
  Operation *op = nullptr;
  double now = 0;
  char buf[1501];
  ssize_t length;

  while (1) {
    switch (read_state) {
      case IDLE:
        return;
      case WAITING_FOR_GET:
        /* memset(buf, 0, 1501); */
        length = read(fd, buf, sizeof(buf));
        buf[length] = 0;
        if (length == -1 && errno == EAGAIN)
          return;
        if (options.binary) {
          assert(length > 0 && (size_t) length < sizeof(buf));
          op = consume_udp_binary_response(buf, length);

          // in case of duplicate response op will be NULL
          if (op) {
            now = get_time();
            op->end_time = now;
            stats.log_get(*op);
            pop_op(op);
          }
        } else {
          udp_header_t *udp_header = (udp_header_t *)buf;
          int data_len = length - sizeof(udp_header_t); // minus udp header size
          char *data = (char *) (udp_header + 1);
          /* printf("* response:\n%s\n", data); */
          // assert(udp_header->seq_no == 0);
          // assert(udp_header->datagrams == ntohs(1));
          int datagrams = ntohs(udp_header->datagrams);
          int seq_no = ntohs(udp_header->seq_no);
          assert(seq_no < datagrams);
          assert(udp_header->reserved == 0);
          if (seq_no != datagrams - 1) {
            // more messages to be received
            /* printf("%d != %d\n", seq_no, datagrams - 1); */
            break;
          }

          int s = 0;
          int pre_s = 0;
          const int VALUE = 0;
          const int DATA = 1;
          const int END = 2;
          int state = VALUE;
          bool fail = false;
          int value_len = 0;

          if (seq_no > 0) {
            // It has hit the server (because multiple sequence
            // has been sent)
            goto skip_resp_parsing;
          }

          while (s < data_len && !fail) {
            // printf("%d/%d\n%s\n", s, data_len, (data + s));
            pre_s = s;
            for (int e = s; e < data_len; e++) {
              if (data[e] != '\n')
                continue;
              // States .........
              if (state == VALUE) {
                if (!strncmp(data + s, "VALUE", 5)) {
                  sscanf(data + s, "VALUE %*s %*d %d", &value_len);
                  state = DATA;
                } else {
                  printf("fail: %s\n",data +s);
                  fail = true;
                  break;
                }
              } else if (state == DATA) {
                // if(line_len != value_len) {
                //   printf("fail: llen: %d vlen: %d\n", line_len, value_len);
                //   fail = true;
                //   break;
                // }
                if (!strcmp(data + s, "END")) {
                  state = END;
                  // TODO(Farbod): Implement multiple get-key request support
                } else {}
              }
              // next time start from after "\r\n"
              s = e + 2;
              break;
            }
            if (!fail && pre_s == s) {
              printf("\nfail: deadlock state: %d %d/%d\n", state, s, data_len);
              for (int i = 0; i < data_len; i++) {
                if (data[i] > 31 && data[i] < 127) {
                  putchar(data[i]);
                } else if (data[i] == '\n') {
                  putchar('\\');
                  putchar('n');
                  putchar('\n');
                } else if (data[i] == '\r') {
                  putchar('\\');
                  putchar('r');
                } else {
                  printf(" 0x%x ", data[i]);
                }
              }
              putchar('\n');
              putchar('\n');
              DIE("Parsing error\n");
            }
          }
skip_resp_parsing:
          // printf("Done\n");
          int reqid = ntohs(udp_header->req_id);
          Operation *op = op_queue.find(reqid);
          if (op) {
            now = get_time();
            op->end_time = now;
            stats.rx_bytes += length;
            if (fail) {
              stats.get_misses++;
            } else {
              op->value_size = value_len;
            }
            stats.log_get(*op);
            pop_op(op);
          }
        }
        drive_write_machine(now);
        break;
      default:
        DIE("not implemented");
    }
  }
}

void read_cb(evutil_socket_t sock, short what, void *ptr)
{
  UDPConnection *conn = (UDPConnection *) ptr;
  conn->read_callback();
}

void UDPConnection::timer_callback()
{
  drive_write_machine();
}

void timer_cb(evutil_socket_t fd, short what, void *ptr)
{
  UDPConnection *conn = (UDPConnection *) ptr;
  conn->timer_callback();
}

void UDPConnection::issue_get(string *key, double now)
{
  Operation op;

  if (now == 0.0)
    now = get_time();

  op.start_time = now;
  op.last_xmit = now;
  op.type = Operation::GET;
  op.req_id = req_id++;
  op.key = *key;
  op_queue.push(op);

  issue_get(op);
}

void UDPConnection::issue_get(Operation &op)
{
  int l;
  uint16_t keylen = op.key.size();

  if (read_state == IDLE)
    read_state = WAITING_FOR_GET;

  // each line is 4-bytes
  udp_header_t udp_header = {
    .req_id = htons(op.req_id),
    .seq_no = htons(0),
    .datagrams = htons(1),
    .reserved = 0,
  };

  struct iovec iov[3];
  iov[0].iov_base = &udp_header;
  iov[0].iov_len = sizeof(udp_header);
  // Farbod: I have added this if statement and add non-binary
  // protocol only for get. It might not work correctly.
  if (options.binary) {
    binary_header_t h = {0x80, CMD_GET, htons(keylen),
      0x00, 0x00, {htons(0)},
      htonl(keylen)};
    iov[1].iov_base = &h;
    iov[1].iov_len = 24;
    iov[2].iov_base = (void *) op.key.c_str();
    iov[2].iov_len = keylen;
    l = sizeof(udp_header) + 24 + keylen;
    size_t ret = writev(fd, iov, sizeof(iov) / sizeof(iov[0]));
    assert(ret == (size_t) l);
  } else {
    char buf[512];
    buf[0] = 'g';
    buf[1] = 'e';
    buf[2] = 't';
    buf[3] = ' ';
    buf[4 + keylen] = '\r';
    buf[5 + keylen] = '\n';
    buf[6 + keylen] = '\0';
    memcpy(buf + 4, op.key.c_str(), keylen);
    // reqval = string("get ");
    // reqval += op.key;
    // reqval += "\r\n";
    // printf("req: %s, klen: %d\n", buf, keylen);
    // iov[1].iov_base = (void *)reqval.c_str();
    // iov[1].iov_len = reqval.size() + 1;
    iov[1].iov_base = buf;
    iov[1].iov_len = keylen + 7;
    iov[2].iov_base = 0;
    iov[2].iov_len = 0;
    // l = sizeof(udp_header) + reqval.size() + 1;
    l = sizeof(udp_header) + keylen + 7;
    size_t ret = writev(fd, iov, 2);
    assert(ret == (size_t) l);
  }

  stats.tx_bytes += l;
  stats.issue_gets++;
}

void UDPConnection::retransmit(double now)
{
  for (Operation &op: op_queue) {
    if (now < op.last_xmit + RETRANSMIT_INTERVAL)
      continue;
    op.last_xmit = now;
    issue_something(op);
    stats.retransmits++;
  }
}

void UDPConnection::issue_something(Operation &op)
{
  switch (op.type) {
    case Operation::GET:
      issue_get(op);
      break;
    default:
      DIE("Not implemented");
  }
}

void UDPConnection::issue_something(double now)
{
  // string key = keygen->generate(lrand48() % options.records);
  string key = keygen->generate(popularity->generate());
  issue_get(&key, now);
}

void UDPConnection::drive_write_machine(double now)
{
  if (now == 0.0)
    now = get_time();

  double delay;
  struct timeval tv;

  retransmit(now);
  while (1) {
    switch (write_state) {
      case INIT_WRITE:
        delay = iagen->generate();
        next_time = now + delay;
        double_to_tv(delay, &tv);
        evtimer_add(timer, &tv);
        write_state = WAITING_FOR_TIME;
        break;
      case ISSUING:
        if (op_queue.size() >= (size_t) options.depth) {
          write_state = WAITING_FOR_OPQ;
          break;
        } else if (now < next_time) {
          write_state = WAITING_FOR_TIME;
          break;
        }
        issue_something(now);
        stats.log_op(op_queue.size());
        delay = iagen->generate();
        next_time += delay;
        break;
      case WAITING_FOR_TIME:
        if (now < next_time) {
          if (!event_pending(timer, EV_TIMEOUT, NULL)) {
            delay = next_time - now;
            double_to_tv(delay, &tv);
            evtimer_add(timer, &tv);
          }
          return;
        }
        write_state = ISSUING;
        break;
      case WAITING_FOR_OPQ:
        if (op_queue.size() >= (size_t) options.depth) {
          if (!event_pending(timer, EV_TIMEOUT, NULL)) {
            Operation *op = op_queue.earliest_last_xmit();
            delay = op->last_xmit + RETRANSMIT_INTERVAL - now;
            double_to_tv(delay, &tv);
            evtimer_add(timer, &tv);
          }
          return;
        }
        write_state = ISSUING;
        break;
      default:
        DIE("Not implemented");
    }
  }
}

UDPConnection::UDPConnection(struct event_base* base, string hostname, int port, options_t options, bool sampling) :
  Connection(sampling), options(options)
{
  fd = socket(AF_INET, SOCK_DGRAM, 0);
  assert(fd != -1);

  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  int ret = inet_aton(hostname.c_str(), &addr.sin_addr);
  assert(ret);

  addr.sin_port = htons(port);
  ret = connect(fd, (sockaddr *) &addr, sizeof(addr));
  assert(!ret);

  ret = evutil_make_socket_nonblocking(fd);
  assert(!ret);

  event *ev = event_new(base, fd, EV_READ | EV_PERSIST, read_cb, this);
  assert(ev);

  event_add(ev, NULL);

  timer = evtimer_new(base, timer_cb, this);

  read_state = IDLE;
  write_state = INIT_WRITE;
  iagen = createGenerator(options.ia);
  iagen->set_lambda(options.lambda);
  auto keysize = createGenerator(options.keysize);
  keygen = new KeyGenerator(keysize, options.records);
  popularity = createPopularityGenerator(options.popularity, options.records, options.permutation_seed);
}
