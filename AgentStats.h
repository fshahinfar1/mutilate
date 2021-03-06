/* -*- c++ -*- */
#ifndef AGENTSTATS_H
#define AGENTSTATS_H

class AgentStats {
public:
  uint64_t rx_bytes, tx_bytes;
  uint64_t gets, sets, get_misses;
  uint64_t skips;
  uint64_t retransmits;
  uint64_t issue_gets;

  double start, stop;
};

#endif // AGENTSTATS_H
