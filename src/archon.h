// Archon drill-down data: the run list and one run's workflow graph, as served by the host's
// /api/archon/runs and /api/archon/graph (host/providers/archon.py does the digest and the layout).
#pragma once
#include <Arduino.h>
#include <time.h>

#define AR_MAX_RUNS 8
#define AR_MAX_NODES 40
#define AR_MAX_EDGES 96

enum class NodeState : uint8_t { Pending, Running, Completed, Failed, Skipped, Cancelled };

struct ArRun {
  char id[40];
  char name[32];
  char status[12];
  char node[28];         // where it is now ("" when unknown / finished)
  char project[24];
  time_t startedAt, endedAt;   // 0 = unknown / still going
};

struct ArRuns {
  uint32_t seq;          // bumped on every finished fetch, good or bad (0 = never fetched)
  bool ok;
  char error[48];
  uint8_t n, nActive;    // active runs come first
  ArRun runs[AR_MAX_RUNS];
};

struct ArNode {
  char label[32];
  char at[24];           // active child of a sub-workflow node, "" otherwise
  NodeState state;
  uint8_t layer;         // row, 0 = top
  float x;               // column offset in column units, 0 = centre
};

struct ArEdge {
  uint8_t from, to;      // node indices, from is always in a higher row
  int8_t lane;           // 0 = straight to the next row; -k / +k = k-th side lane on the left / right
};

struct ArGraph {
  uint32_t seq;
  bool ok;
  char error[48];
  char id[40];
  char name[32];
  char status[12];
  time_t startedAt, endedAt;
  uint8_t nNodes, nEdges, layers, width, lanesL, lanesR, done, total;
  ArNode nodes[AR_MAX_NODES];   // sorted by (layer, x): the dial walks them in this order
  ArEdge edges[AR_MAX_EDGES];
};
