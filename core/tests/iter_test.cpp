//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgedgeiter.hpp"
#include "lgraph.hpp"

#include <set>

bool failed = false;

#define VERBOSE
//#define VERBOSE2
//#define VERBOSE3

void generate_graphs(int n) {

  unsigned int rseed = 123;

  for(int i = 0; i < n; i++) {
    std::string           gname = "test_" + std::to_string(i);
    LGraph *              g     = LGraph::create("lgdb_iter_test", gname, "test");
    std::vector<Node_pin::Compact> spins;
    std::vector<Node_pin::Compact> dpins;

    int inps = 10 + rand_r(&rseed) % 100;
    for(int j = 0; j < inps; j++) {
      auto pin = g->add_graph_input("i" + std::to_string(j), j, 1);
      dpins.push_back(pin.get_compact());
    }

    int outs = 10 + rand_r(&rseed) % 100;
    for(int j = 0; j < outs; j++) {
      auto pin = g->add_graph_output("o" + std::to_string(j), inps+j,1);
      spins.push_back(pin.get_compact());
      dpins.push_back(g->get_graph_output_driver(("o" + std::to_string(j))).get_compact());
    }

    int nnodes = 100 + rand_r(&rseed) % 1000;
    for(int j = 0; j < nnodes; j++) { // Simple output nodes
      auto node = g->create_node();
      Node_Type_Op op  = (Node_Type_Op)(1 + rand_r(&rseed) % 22); // regular node types range
      node.set_type(op);
      dpins.push_back(node.setup_driver_pin(0).get_compact());
      spins.push_back(node.setup_sink_pin(0).get_compact());
    }

    int const_nodes = 10 + rand_r(&rseed) % 100;
    for(int j = 0; j < const_nodes; j++) { // Simple output nodes
      auto node = g->create_node();
      node.set_type(U32Const_Op);
      dpins.push_back(node.setup_driver_pin().get_compact());
    }

    int cnodes = 100 + rand_r(&rseed) % 1000;
    for(int j = 0; j < cnodes; j++) { // complex nodes
      auto node = g->create_node(FFlop_Op);
      auto d1 = rand_r(&rseed)%3;
      auto s1 = rand_r(&rseed)%6;
      dpins.push_back(node.setup_driver_pin(d1).get_compact());
      spins.push_back(node.setup_sink_pin(s1).get_compact());
      if (rand_r(&rseed)&1) {
        auto d2 = rand_r(&rseed)%3;
        auto s2 = rand_r(&rseed)%6;
        if (d1!=d2)
          dpins.push_back(node.setup_driver_pin(d2).get_compact());
        if (s1!=s2)
          spins.push_back(node.setup_sink_pin(s2).get_compact());
      }
    }

    int nedges = 1000 + rand_r(&rseed) % 8000;
    absl::flat_hash_set<std::pair<Node_pin::Compact, Node_pin::Compact>> edges;
    for(int j = 0; j < nedges; j++) {
      int      counter = 0;
      Node_pin::Compact src;
      Node_pin::Compact dst;
      do {
        src = dpins[rand_r(&rseed) % (dpins.size())];
        dst = spins[rand_r(&rseed) % (spins.size())];
        counter++;
      } while(edges.find(std::make_pair(src, dst)) != edges.end() && counter < 1000);

      if (edges.find(std::make_pair(src, dst)) != edges.end())
        break;

      Node_pin dpin(g,src);
      Node_pin spin(g,dst);
      if (dpin.get_node() == spin.get_node())
        continue; // No self-loops

      edges.insert(std::make_pair(src, dst));
      I(!g->has_edge(dpin, spin));
      g->add_edge(dpin, spin);
      I(g->has_edge(dpin, spin));
    }

  }
}

bool fwd(int n) {

  for(int i = 0; i < n; i++) {
    std::string gname = "test_" + std::to_string(i);
    LGraph *    g     = LGraph::open("lgdb_iter_test", gname);
    if(g == 0)
      return false;

    //g->dump();
    //fmt::print("----------------------\n");
    absl::flat_hash_set<Node::Compact> visited;
    for(auto node : g->forward()) {

      if (!node.get_type().is_pipelined() && node.get_type().op != GraphIO_Op) {
        // check if all incoming edges were visited
        for(auto &inp : node.inp_edges()) {
          if (!inp.driver.get_node().get_type().is_pipelined() && inp.driver.get_node().get_type().op != GraphIO_Op) {
            if(visited.find(inp.driver.get_node().get_compact()) == visited.end()) {
              fmt::print("fwd failed for lgraph node:{} fwd:{}\n", node.debug_name(), inp.driver.get_node().debug_name());
              I(false);
              return false;
            }
          }
        }
      }

      visited.insert(node.get_compact());
    }
    visited.clear();
    for(auto node : g->forward(true)) {

      if (!node.get_type().is_pipelined() && node.get_type().op != GraphIO_Op) {
        // check if all incoming edges were visited
        for(auto &inp : node.inp_edges()) {
          if (!inp.driver.get_node().get_type().is_pipelined() && inp.driver.get_node().get_type().op != GraphIO_Op) {
            if(visited.find(inp.driver.get_node().get_compact()) == visited.end()) {
              fmt::print("fwd failed for lgraph node:{} fwd:{}\n", node.debug_name(), inp.driver.get_node().debug_name());
              I(false);
              return false;
            }
          }
        }
      }

      visited.insert(node.get_compact());
    }

  }

  return true;
}

bool bwd(int n) {
  for(int i = 0; i < n; i++) {
    std::string gname = "test_" + std::to_string(i);
    LGraph *    g     = LGraph::open("lgdb_iter_test", gname);
    if(g == 0)
      return false;

    //g->dump();
    //fmt::print("----------------------\n");
    absl::flat_hash_set<Node::Compact> visited;
    for(auto node : g->backward()) {
      //fmt::print(" bwd {}\n", node.debug_name());

      visited.insert(node.get_compact());

      if (!node.get_type().is_pipelined() && node.get_type().op != GraphIO_Op) {
        // check if all incoming edges were visited
        for(auto &out : node.out_edges()) {
          if (!out.sink.get_node().get_type().is_pipelined() && out.sink.get_node().get_type().op != GraphIO_Op) {
            if(visited.find(out.sink.get_node().get_compact()) == visited.end()) {
              fmt::print("bwd failed for lgraph node:{} bwd:{}\n", node.debug_name(), out.sink.get_node().debug_name());
              I(false);
              return false;
            }
          }
        }
      }

    }
    visited.clear();
    for(auto node : g->backward(true)) {
      //fmt::print(" bwd {}\n", node.debug_name());

      visited.insert(node.get_compact());

      if (!node.get_type().is_pipelined() && node.get_type().op != GraphIO_Op) {
        // check if all incoming edges were visited
        for(auto &out : node.out_edges()) {
          if (!out.sink.get_node().get_type().is_pipelined() && out.sink.get_node().get_type().op != GraphIO_Op) {
            if(visited.find(out.sink.get_node().get_compact()) == visited.end()) {
              fmt::print("bwd failed for lgraph node:{} bwd:{}\n", node.debug_name(), out.sink.get_node().debug_name());
              I(false);
              return false;
            }
          }
        }
      }

    }

  }
  return true;
}

void topo_add_chain_fwd(absl::flat_hash_set<Node::Compact> &discovered_node
    , std::vector<Node> &node_stack
    , const XEdge &edge) {

  const auto dst_node = edge.driver.get_node(); // fwd

  if (hier) {
    if (dst_node.is_type_sub() && !dst_node.is_type_sub_empty()) { // DOWN??
      fmt::print("dfs adding subnode:{} (many types, opt for speed!)\n", dst_node.debug_name());

      auto down_pin = edge.driver.get_down_pin(); // fwd
      I(down_pin.is_driver_pin()); // fwd

      for (auto &edge2 : down_pin.inp_edges()) {  // fwd
        topo_add_chain_fwd(discovered_node, node_stack, edge2);
      }

      return;
    }else if (node2.is_graph_input() && !node2.is_root()) { // UP??
      auto up_hidx = node2.hierarchy_go_up();
      auto up_node = node2;
      up_node.update(up_hidx);
      node_stack.push_back(up_node);

      fmt::print("prop_up node:{} lg:{} -> lg:{}\n", node2.debug_name(), node2.get_class_lgraph()->get_name(),
          up_node.get_class_lgraph()->get_name());
    }
  }

  if (discovered_node.count(dst_node.get_compact()))
    return;

  node_stack.push_back(dst_node);
}

// performs Topological Sort on a given DAG
void doTopologicalSort(LGraph *lg) {

  absl::flat_hash_set<Node::Compact>     discovered_node;
  std::vector<Node> node_stack;

  discovered_node.clear();

  bool hier= true;

  for (auto node : lg->fast()) { // FIXME: to flow, no fast (start with inputs for fwd, outputs for bwd)
    if (discovered_node.count(node.get_compact())) continue;

    node_stack.push_back(node);
    while(!node_stack.empty()) {
      auto node2 = node_stack.back();
      node_stack.pop_back();

      if (!discovered_node.count(node2.get_compact())) {
        fmt::print("topo node:{} lg:{}\n", node2.debug_name(), node2.get_class_lgraph()->get_name());
        discovered_node.insert(node2.get_compact());
      }
      // forward traversal : inp_edges ; out.driver
      // backward traversal: out_edges ; out.sink
      for (auto &edge : node2.inp_edges()) {
        topo_add_chain_fwd(discovered_node, node_stack, edge);
      }
    }
  }
}

void simple() {
  std::string gname = "simple_iter";
  LGraph *g     = LGraph::create("lgdb_iter_test", gname, "test");
  LGraph *sub_g = LGraph::create("lgdb_iter_test", "sub", "test");

  for (int i = 0; i < 256; i++) {
    auto ipin = sub_g->add_graph_input("i" + std::to_string(i), i+1, 0);
    auto opin = sub_g->add_graph_output("o" + std::to_string(i), 256+i+1, 0);
    auto node = sub_g->create_node(Or_Op);
    sub_g->add_edge(ipin, node.setup_sink_pin(0));
    sub_g->add_edge(node.setup_driver_pin(rand()&1), opin);
  }

  int pos = 1; // Start with pos 1
  auto i1 = g->add_graph_input("i0", pos++, 0); // 1
  i1.set_bits(1);
  auto i2 = g->add_graph_input("i1", pos++, rand()&0xF); // 2
  i2.set_bits(1);
  auto i3 = g->add_graph_input("i2", pos++, rand()&0xF); // 3
  i3.set_bits(1);
  auto i4 = g->add_graph_input("i3", pos++, rand()&0xF); // 4
  i4.set_bits(1);

  auto o5 = g->add_graph_output("o0", pos++, rand()&0xF); // 5
  auto o6 = g->add_graph_output("o1", pos++, rand()&0xF); // 6
  auto o7 = g->add_graph_output("o2", pos++, rand()&0xF); // 7
  auto o8 = g->add_graph_output("o3", pos++, rand()&0xF); // 8

  auto c9 = g->create_node_const(1); //  9
  auto c10 = g->create_node_const(21); //  10
  auto c11 = g->create_node_const("xxx",3); //  11
  auto c12 = g->create_node_const("yyyy",4); // 12

  auto t13 = g->create_node_sub(sub_g->get_lgid()); // 13
  auto t14 = g->create_node_sub(sub_g->get_lgid()); // 14
  auto t15 = g->create_node_sub(sub_g->get_lgid()); // 15
  auto t16 = g->create_node_sub(sub_g->get_lgid()); // 16
  auto t17 = g->create_node_sub(sub_g->get_lgid()); // 17
  auto t18 = g->create_node_sub(sub_g->get_lgid()); // 18
  auto t19 = g->create_node_sub(sub_g->get_lgid()); // 19
  auto t20 = g->create_node_sub(sub_g->get_lgid()); // 20
  auto t21 = g->create_node_sub(sub_g->get_lgid()); // 21
  auto t22 = g->create_node_sub(sub_g->get_lgid()); // 22
  (void)t22; // Disconnected node. Source of bug when fully disconnected
  auto t23 = g->create_node_sub(sub_g->get_lgid()); // 23

  /*
  // nodes:
  //     1i    2i    3i    4i    9c 23g 10c   11c   12c
  //       \  /       \   /  \    \  | /      |   /   \
  //        13g        14g    15g  16g        17g    18g  22g
  //        | \       / \           \        /  \   /   \
  //        |  \     /   \           \      20   19g    21g
  //        |   \   /     \           \   /
  //        5o    6o       7o           8o
  //
  // node_debug_name:
  //
  //     1i    1i    1i    1i   11c 25g 12c  13c   14c
  //       \  /       \   /  \    \  | /     |   /   \
  //        15g        16g    17g  18g       19g    20g  24g
  //        | \       / \           \       /  \   /   \
  //        |  \     /   \           \     22   21g    23g
  //        |   \   /     \           \  /
  //        2o   2o       2o           2o
  */

  g->add_edge(i1, t13.setup_sink_pin(random()&0xFF));
  g->add_edge(i2, t13.setup_sink_pin(random()&0xFF));

  g->add_edge(i3, t14.setup_sink_pin(random()&0xFF));
  g->add_edge(i4, t14.setup_sink_pin(random()&0xFF));
  g->add_edge(i4, t15.setup_sink_pin(random()&0xFF));

  g->add_edge(c9.setup_driver_pin(), t16.setup_sink_pin(random()&0xFF));
  g->add_edge(c10.setup_driver_pin(), t16.setup_sink_pin(random()&0xFF));
  g->add_edge(t23.setup_driver_pin(random()&0xFF), t16.setup_sink_pin(random()&0xFF));

  g->add_edge(c11.setup_driver_pin(), t17.setup_sink_pin(random()&0xFF));
  if (rand()&1)
    g->add_edge(c12.setup_driver_pin(), t17.setup_sink_pin(1000+(random()&0xFF)));
  g->add_edge(c12.setup_driver_pin(), t17.setup_sink_pin(random()&0xFF));
  g->add_edge(c12.setup_driver_pin(), t18.setup_sink_pin(random()&0xFF));

  g->add_edge(t13.setup_driver_pin(random()&0xFF), o5);
  g->add_edge(t13.setup_driver_pin(random()&0xFF), o6);
  if (rand()&1)
    g->add_edge(t14.setup_driver_pin(1000+(random()&0xFF)), o6);
  g->add_edge(t14.setup_driver_pin(random()&0xFF), o6);
  g->add_edge(t14.setup_driver_pin(random()&0xFF), o7);

  g->add_edge(t17.setup_driver_pin(random()&0xFF), t20.setup_sink_pin(random()&0xFF));
  if (rand()&1)
    g->add_edge(t17.setup_driver_pin(1000+(random()&0xFF)), t20.setup_sink_pin(random()&0xFF));
  g->add_edge(t17.setup_driver_pin(random()&0xFF), t19.setup_sink_pin(random()&0xFF));
  g->add_edge(t18.setup_driver_pin(random()&0xFF), t19.setup_sink_pin(random()&0xFF));
  g->add_edge(t18.setup_driver_pin(random()&0xFF), t21.setup_sink_pin(random()&0xFF));

  g->add_edge(t16.setup_driver_pin(random()&0xFF), o8);
  g->add_edge(t20.setup_driver_pin(random()&0xFF), o8);

#ifdef VERBOSE
  for(const auto &node : g->fast()) {

    fmt::print("node:{}\n", node.debug_name());
    fmt::print("  inp_edges");
    for(const auto &edge : node.inp_edges()) {
      fmt::print("  {}", edge.driver.debug_name());
    }
    fmt::print("\n");
    fmt::print("  out_edges");
    for(const auto &edge : node.out_edges()) {
      fmt::print("  {}", edge.sink.debug_name());
    }
    fmt::print("\n");
  }
#endif

  doTopologicalSort(g);

  exit(0); // FIXME

  std::vector<std::string> fast;
  int conta =0;
  for(const auto node : g->fast()) {
#ifdef VERBOSE3
    fmt::print(" fast1:{} lg:{}\n", node.debug_name(), node.get_class_lgraph()->get_name());
#endif
    fast.emplace_back(node.debug_name());
    conta++;
  }
  std::vector<std::string> fast_true;
  conta =0;
  for(const auto node : g->fast(true)) {
#ifdef VERBOSE3
    fmt::print(" fast2:{} lg:{}\n", node.debug_name(), node.get_class_lgraph()->get_name());
#endif
    fast_true.emplace_back(node.debug_name());
    conta++;
  }

  std::vector<std::string> fwd;
  conta=0;
  //for(const auto node : g->forward()) {
  auto iter = g->forward();
  for(auto it = iter.begin() ; it!= iter.end() ; ++it) {
    auto node = *it;
#ifdef VERBOSE2
    fmt::print(" fwd1:{} lg:{}\n", node.debug_name(), node.get_class_lgraph()->get_name());
#endif
    fwd.emplace_back(node.debug_name());
    conta++;
  }
  if (conta!=16) {
    fmt::print("ERROR. expected 16 nodes in forward traversal. Found {}\n",conta);
    failed = true;
  }
  std::vector<std::string> fwd_true;
  conta =0;
  for(const auto node : g->forward(true)) {
#ifdef VERBOSE2
    fmt::print(" fwd2:{} lg:{}\n", node.debug_name(), node.get_class_lgraph()->get_name());
#endif
    fwd_true.emplace_back(node.debug_name());
    conta++;
  }
#ifdef VERBOSE2
  return;
#endif

  std::vector<std::string> bwd;
  conta =0;
  for(const auto node : g->backward()) {
#ifdef VERBOSE2
    fmt::print(" bwd:{}\n", node.debug_name());
#endif
    bwd.emplace_back(node.debug_name());
    conta++;
  }
  if (conta!=16) {
    fmt::print("ERROR. expected 16 nodes in backward traversal. Found {}\n",conta);
    failed = true;
  }

  std::vector<std::string> bwd_true;
  conta =0;
  for(const auto node : g->backward(true)) {
    bwd_true.emplace_back(node.debug_name());
    conta++;
  }

  std::sort(fwd_true.begin() , fwd_true.end());
  std::sort(bwd_true.begin() , bwd_true.end());
  std::sort(fast_true.begin(), fast_true.end());

  std::sort(fwd.begin() , fwd.end());
  std::sort(bwd.begin() , bwd.end());
  std::sort(fast.begin(), fast.end());

  I(fwd.size() == fwd_true.size());
  I(bwd.size() == bwd_true.size());
  I(fast.size() == fast_true.size());
  {
    auto it1 = fast.begin();
    auto it2 = fast_true.begin();
    while(it1 != fast.end()) {
      if (*it1 != *it2) {
        fmt::print("missmatch fast {} vs fast_true {}\n", *it1, *it2);
        failed = true;
      }
      it1++;
      it2++;
      if (it1 == fast.end() && it2 == fast_true.end()) break;
      if (it1 != fast.end() && it2 != fast_true.end()) continue;
      fmt::print("fast fast_true not matching size\n");

      fmt::print("fast    :");
      for(auto txt:fast)
        fmt::print(" {}",txt);
      fmt::print("\n");
      fmt::print("fast_tru:");
      for(auto txt:fast_true)
        fmt::print(" {}",txt);
      fmt::print("\n");
      failed = true;
      return;
    }
  }
  {
    auto it1 = fwd.begin();
    auto it2 = fwd_true.begin();
    while(it1 != fwd.end()) {
      if (*it1 != *it2) {
        fmt::print("missmatch fwd {} vs fwd_true {}\n", *it1, *it2);
        failed = true;
        fmt::print("fwd     :");
        for(auto txt:fwd)
          fmt::print(" {}",txt);
        fmt::print("\n");
        fmt::print("fwd_tru :");
        for(auto txt:fwd_true)
          fmt::print(" {}",txt);
        fmt::print("\n");
        return;
      }
      it1++;
      it2++;
      if (it1 == fwd.end() && it2 == fwd_true.end()) break;
      if (it1 != fwd.end() && it2 != fwd_true.end()) continue;
      fmt::print("fwd fwd_true not matching size\n");
      failed = true;
      return;
    }
  }
  {
    auto it1 = bwd.begin();
    auto it2 = bwd_true.begin();
    while(it1 != bwd.end()) {
      if (*it1 != *it2) {
        fmt::print("missmatch bwd {} vs bwd_true {}\n", *it1, *it2);
        failed = true;
        return;
      }
      it1++;
      it2++;
      if (it1 == bwd.end() && it2 == bwd_true.end()) break;
      if (it1 != bwd.end() && it2 != bwd_true.end()) continue;
      fmt::print("bwd bwd_true not matching size\n");
      failed = true;
      return;
    }
  }

#if 0
  fmt::print("fwd :");
  for(auto txt:fwd)
    fmt::print(" {}",txt);
  fmt::print("\n");

  fmt::print("bwd :");
  for(auto txt:bwd)
    fmt::print(" {}",txt);
  fmt::print("\n");

  fmt::print("fast:");
  for(auto txt:fast)
    fmt::print(" {}",txt);
  fmt::print("\n");
#endif

  auto fwd_it  = fwd.begin();
  auto bwd_it  = bwd.begin();

  while(fwd_it != fwd.end()) {
    if (*bwd_it != *fwd_it) {
      fmt::print("missmatch bwd {} vs fwd {}\n",*bwd_it, *fwd_it);
      failed = true;
    }
    fwd_it++;
    bwd_it++;
    if (fwd_it == fwd.end() && bwd_it == bwd.end())
      break;

    if (fwd_it == fwd.end()) {
      fmt::print("fwd is shorter\n");
      failed = true;
      break;
    }
    if (bwd_it == bwd.end()) {
      fmt::print("bwd is shorter\n");
      failed = true;
      break;
    }
  }
}

int main() {

  simple();

#if 0
  for(int i=0;i<40;i++) {
    simple();
    if (failed)
      return -3;
  }
#endif

#if 0
  int n = 40;
  generate_graphs(n);

  if(!fwd(n)) {
    failed = true;
  }

  if(!bwd(n)) {
    failed = true;
  }
#endif

  return failed ? 1 : 0;
}
