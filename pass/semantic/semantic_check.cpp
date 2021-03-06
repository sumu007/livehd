
#include "semantic_check.hpp"

#include <string_view>
#include <iostream>
#include <algorithm>

#include "pass.hpp"

bool Semantic_check::is_primitive_op(const Lnast_ntype node_type) {
  if (node_type.is_logical_op() || node_type.is_unary_op() || node_type.is_nary_op() || node_type.is_assign() ||
      node_type.is_dp_assign() || node_type.is_as() || node_type.is_eq() || node_type.is_select() || node_type.is_bit_select() ||
      node_type.is_logic_shift_right() || node_type.is_arith_shift_right() || node_type.is_arith_shift_left() ||
      node_type.is_rotate_shift_right() || node_type.is_rotate_shift_left() || node_type.is_dynamic_shift_left() ||
      node_type.is_dynamic_shift_right() || node_type.is_dot() || node_type.is_tuple() || node_type.is_tuple_concat()) {
    return true;
  } else {
    return false;
  }
}

bool Semantic_check::is_tree_structs(const Lnast_ntype node_type) {
  if (node_type.is_stmts() || node_type.is_cstmts() || node_type.is_if() || node_type.is_cond() || node_type.is_uif() ||
      node_type.is_elif() || node_type.is_for() || node_type.is_while() || node_type.is_func_call() || node_type.is_func_def()) {
    return true;
  } else {
    return false;
  }
}

bool Semantic_check::in_write_list(std::string_view node_name) {
  if (write_list.contains(node_name)) {
    return true;
  } else {
    return false;
  }
}

bool Semantic_check::in_read_list(std::string_view node_name) {
  if (read_list.contains(node_name)) {
    return true;
  } else {
    return false;
  }
}

bool Semantic_check::in_assign_lhs_list(std::string_view node_name) {
  for (auto name : assign_lhs_list) {
    if (name == node_name) {
      return true;
    }
  }
  return false;
}

bool Semantic_check::in_assign_rhs_list(std::string_view node_name) {
  for (auto name : assign_rhs_list) {
    if (name == node_name) {
      return true;
    }
  }
  return false;
}

bool Semantic_check::in_inefficient_LNAST(std::string_view node_name) {
  for (auto name : inefficient_LNAST) {
    if (name == node_name) {
      return true;
    }
  }
  return false;
}

void Semantic_check::add_to_write_list(std::string_view node_name) {
  if (!in_write_list(node_name)) {
    if (node_name[0] != '%') {
      write_list.insert(node_name);
    }
  } else {
    if (node_name[0] == '_' && node_name[1] == '_' && node_name[2] == '_') {
      Pass::error("Temporary Variable Error: Should be only a single write to temporary variable\n");
    }
  }
}

void Semantic_check::add_to_read_list(std::string_view node_name) {
  if (!in_read_list(node_name)) {
    read_list.insert(node_name);
  }
}

void Semantic_check::add_to_assign_lhs_list(std::string_view node_name) {
  if (!in_assign_lhs_list(node_name)) {
    // std::cout << "Added to LHS " << node_name << "\n";
    assign_lhs_list.push_back(node_name);
  }
}

void Semantic_check::add_to_assign_rhs_list(std::string_view node_name) {
  if (!in_assign_rhs_list(node_name)) {
    // std::cout << "Added to RHS " << node_name << "\n";
    assign_rhs_list.push_back(node_name);
  }
}

void Semantic_check::find_lhs_name(int index) {
  int lhs_index = 0;
  for (auto lhs_name : assign_lhs_list) {
    if (lhs_index == index && !in_inefficient_LNAST(lhs_name)) {
      inefficient_LNAST.push_back(lhs_name);
      break;
    } else {
      lhs_index += 1;
    }
  }
}

void Semantic_check::resolve_read_write_lists() {
  for (auto it : write_list) {
    if (in_read_list(it)) {
      write_list.erase(it);
    }
  }
  if (write_list.size() != 0) {
    auto first = write_list.begin();
    std::cout << "Temporary Variable Warning: " << *first;
    for (auto name : write_list) {
      if (name == *first) {
        continue;
      }
      std::cout << ", " << name;
    }
    std::cout << " were written but never read\n";
  }
}

void Semantic_check::resolve_assign_lhs_rhs_lists() {
  int index_lhs = 0;
  for (auto lhs_name : assign_lhs_list) {
    int index_rhs = 0;
    for (auto rhs_name : assign_rhs_list) {
      if (rhs_name == lhs_name && index_rhs > index_lhs) {
        // std::cout << "Found one: " << index_rhs << "\n";
        find_lhs_name(index_rhs);
      } else {
        index_rhs += 1;
      }
    }
    index_lhs += 1;
  }
  if (inefficient_LNAST.size() != 0) {
    auto first = inefficient_LNAST.begin();
    std::cout << "Inefficient LNAST Warning: " << *first;
    for (auto name : inefficient_LNAST) {
      if (name == *first) {
        continue;
      }
      std::cout << ", " << name;
    }
    std::cout << " may be unnecessary\n";
  }
}

void Semantic_check::check_primitive_ops(Lnast *lnast, const Lnast_nid &lnidx_opr, const Lnast_ntype node_type) {
  if (!lnast->has_single_child(lnidx_opr)) {
    // Unary Operations
    if (node_type.is_assign() || node_type.is_dp_assign() || node_type.is_not() || node_type.is_logical_not() ||
        node_type.is_as()) {
      auto lhs      = lnast->get_first_child(lnidx_opr);
      auto lhs_type = lnast->get_data(lhs).type;
      auto rhs      = lnast->get_sibling_next(lhs);
      auto rhs_type = lnast->get_data(rhs).type;

      if (!lhs_type.is_ref()) {
        Pass::error("Unary Operation Error: LHS Node must be Node type 'ref'\n");
      }
      if (!rhs_type.is_ref() && !rhs_type.is_const()) {
        Pass::error("Unary Operation Error: RHS Node must be Node type 'ref' or 'const'\n");
      }
      // Store type 'ref' variables
      add_to_write_list(lnast->get_name(lhs));
      if (rhs_type.is_ref()) {
        add_to_read_list(lnast->get_name(rhs));
      }
      if (node_type.is_assign()) {
        add_to_assign_lhs_list(lnast->get_name(lhs));
        add_to_assign_rhs_list(lnast->get_name(rhs));
      }
      // N-ary Operations (need to add node_type.is_select())
    } else if (node_type.is_dot() || node_type.is_logical_and() || node_type.is_logical_or() || node_type.is_nary_op() ||
               node_type.is_eq() || node_type.is_bit_select() || node_type.is_logic_shift_right() ||
               node_type.is_arith_shift_right() || node_type.is_arith_shift_left() || node_type.is_rotate_shift_right() ||
               node_type.is_rotate_shift_left() || node_type.is_dynamic_shift_right() || node_type.is_dynamic_shift_left() ||
               node_type.is_tuple_concat()) {
      for (const auto &lnidx_opr_child : lnast->children(lnidx_opr)) {
        const auto node_type_child = lnast->get_data(lnidx_opr_child).type;

        if (lnidx_opr_child == lnast->get_first_child(lnidx_opr)) {
          if (!node_type_child.is_ref()) {
            Pass::error("N-ary Operation Error: LHS Node must be Node type 'ref'\n");
          }
          // Store type 'ref' variables
          add_to_write_list(lnast->get_name(lnidx_opr_child));
          continue;
        } else if (!node_type_child.is_ref() && !node_type_child.is_const()) {
          Pass::error("N-ary Operation Error!: RHS Node(s) must be Node type 'ref' or 'const'\n");
        }
        // Store type 'ref' variables
        if (node_type_child.is_ref()) {
          add_to_read_list(lnast->get_name(lnidx_opr_child));
        }
      }
    } else if (node_type.is_tuple()) {
      int num_of_ref    = 0;
      int num_of_assign = 0;
      for (const auto &lnidx_opr_child : lnast->children(lnidx_opr)) {
        const auto node_type_child = lnast->get_data(lnidx_opr_child).type;

        if (node_type_child.is_ref()) {
          num_of_ref += 1;
          // Store type 'ref' variables
          add_to_write_list(lnast->get_name(lnidx_opr_child));
        } else if (node_type_child.is_assign()) {
          check_primitive_ops(lnast, lnidx_opr_child, node_type_child);
          num_of_assign += 1;
        }
      }
      if (num_of_ref != 1) {
        Pass::error("Tuple Operation Error: Missing Reference Node\n");
      } else if (num_of_assign != 2) {
        Pass::error("Tuple Operation Error: Missing Assign Node(s)\n");
      }
    } else if (node_type.is_select()) {
      int num_of_ref = 0;
      for (const auto &lnidx_opr_child : lnast->children(lnidx_opr)) {
        const auto node_type_child = lnast->get_data(lnidx_opr_child).type;

        if (node_type_child.is_ref()) {
          num_of_ref += 1;
          // Store type 'ref' variables
          add_to_read_list(lnast->get_name(lnidx_opr_child));
        }
      }
      if (num_of_ref != 3) {
        // std::cout << num_of_ref << "\n";
        Pass::error("Select Operation Error: Missing Reference Node(s)\n");
      }
    } else {
      Pass::error("Primitive Operation Error: Not a Valid Node Type\n");
    }
  } else {
    Pass::error("Primitive Operation Error: Requires at least 2 LNAST Nodes (lhs, rhs)\n");
  }
}

void Semantic_check::check_if_op(Lnast *lnast, const Lnast_nid &lnidx_opr) {
  bool is_cstmts = false;
  bool is_cond   = false;
  bool is_stmts  = false;
  for (const auto &lnidx_opr_child : lnast->children(lnidx_opr)) {
    const auto ntype_child = lnast->get_data(lnidx_opr_child).type;

    if (ntype_child.is_cstmts() || ntype_child.is_stmts()) {
      if (ntype_child.is_cstmts()) {
        is_cstmts = true;
      } else {
        is_stmts = true;
      }

      for (const auto &lnidx_opr_child_child : lnast->children(lnidx_opr_child)) {
        const auto ntype_child_child = lnast->get_data(lnidx_opr_child_child).type;

        if (is_primitive_op(ntype_child_child)) {
          check_primitive_ops(lnast, lnidx_opr_child_child, ntype_child_child);
        } else if (is_tree_structs(ntype_child_child)) {
          check_if_op(lnast, lnidx_opr_child_child);
        }
      }
    } else if (ntype_child.is_cond()) {
      if (lnast->has_single_child(lnidx_opr_child)) {
        is_cond                          = true;
        const auto lnidx_opr_child_child = lnast->get_first_child(lnidx_opr_child);
        const auto ntype_child_child     = lnast->get_data(lnidx_opr_child_child).type;
        if (!ntype_child_child.is_ref()) {
          Pass::error("If Operation Error: Condition must be Node type 'ref'\n");
        }
        // Store type 'ref' variables
        if (ntype_child_child.is_ref()) {
          add_to_read_list(lnast->get_name(lnidx_opr_child_child));
        }
      } else {
        Pass::error("If Operation Error: Missing Condition Node\n");
      }
    } else {
      Pass::error("If Operation Error: Not a Valid Node Type\n");
    }
  }
  if (!is_cstmts) {
    Pass::error("If Operation Error: Missing Condition Statments Node\n");
  } else if (!is_cond) {
    Pass::error("If Operation Error: Missing Condition Node\n");
  } else if (!is_stmts) {
    Pass::error("If Operation Error: Missing Statements Node\n");
  }
}

void Semantic_check::check_for_op(Lnast *lnast, const Lnast_nid &lnidx_opr) {
  bool stmts      = false;
  int  num_of_ref = 0;
  for (const auto &lnidx_opr_child : lnast->children(lnidx_opr)) {
    const auto ntype_child = lnast->get_data(lnidx_opr_child).type;

    if (ntype_child.is_stmts()) {
      stmts = true;
      for (const auto &lnidx_opr_child_child : lnast->children(lnidx_opr_child)) {
        const auto ntype_child_child = lnast->get_data(lnidx_opr_child_child).type;
        if (is_primitive_op(ntype_child_child)) {
          check_primitive_ops(lnast, lnidx_opr_child_child, ntype_child_child);
        } else if (is_tree_structs(ntype_child_child)) {
          check_if_op(lnast, lnidx_opr_child_child);
        }
      }
    } else if (ntype_child.is_ref()) {
      num_of_ref += 1;
      // Store type 'ref' variables
      add_to_read_list(lnast->get_name(lnidx_opr_child));
    } else {
      Pass::error("For Operation Error: Not a Valid Node Type\n");
    }
  }
  if (num_of_ref < 2) {
    Pass::error("For Operation Error: Missing Reference Node(s)\n");
  } else if (!stmts) {
    Pass::error("For Operation Error: Missing Statements Node\n");
  }
}

void Semantic_check::check_while_op(Lnast *lnast, const Lnast_nid &lnidx_opr) {
  bool cond = false;
  bool stmt = false;
  for (const auto &lnidx_opr_child : lnast->children(lnidx_opr)) {
    const auto ntype_child = lnast->get_data(lnidx_opr_child).type;

    if (ntype_child.is_cond()) {
      cond = true;
      if (lnast->has_single_child(lnidx_opr_child)) {
        auto lnidx_opr_child_child = lnast->get_first_child(lnidx_opr_child);
        auto ntype_child_child     = lnast->get_data(lnidx_opr_child_child).type;
        if (!ntype_child_child.is_ref()) {
          Pass::error("While Operation Error: Condition must be Node type 'ref'\n");
        }
        // Store type 'ref' variables
        if (ntype_child_child.is_ref()) {
          add_to_read_list(lnast->get_name(lnidx_opr_child_child));
        }
      } else {
        Pass::error("While Operation Error: Missing Condition Node\n");
      }
    } else if (ntype_child.is_stmts()) {
      stmt = true;
      for (const auto &lnidx_opr_child_child : lnast->children(lnidx_opr_child)) {
        const auto ntype_child_child = lnast->get_data(lnidx_opr_child_child).type;
        if (is_primitive_op(ntype_child_child)) {
          check_primitive_ops(lnast, lnidx_opr_child_child, ntype_child_child);
        } else if (is_tree_structs(ntype_child_child)) {
          check_if_op(lnast, lnidx_opr_child_child);
        }
      }
    } else {
      Pass::error("While Operation Error: Not a Valid Node Type\n");
    }
  }
  if (!cond) {
    Pass::error("While Operation Error: Missing Condition Node\n");
  } else if (!stmt) {
    Pass::error("While Operation Error: Missing Statement Node\n");
  }
}

void Semantic_check::check_func_def(Lnast *lnast, const Lnast_nid &lnidx_opr) {
  int  num_of_refs = 0;
  bool cond        = false;
  bool stmts       = false;
  for (const auto &lnidx_opr_child : lnast->children(lnidx_opr)) {
    const auto ntype_child = lnast->get_data(lnidx_opr_child).type;

    if (lnidx_opr_child == lnast->get_first_child(lnidx_opr)) {
      num_of_refs += 1;
      // Store type 'ref' variables
      add_to_write_list(lnast->get_name(lnidx_opr_child));
      continue;
    }
    if (ntype_child.is_cstmts() | ntype_child.is_stmts()) {
      if (ntype_child.is_stmts()) {
        stmts = true;
      }
      for (const auto &lnidx_opr_child_child : lnast->children(lnidx_opr_child)) {
        const auto ntype_child_child = lnast->get_data(lnidx_opr_child_child).type;
        if (is_primitive_op(ntype_child_child)) {
          check_primitive_ops(lnast, lnidx_opr_child_child, ntype_child_child);
        } else if (is_tree_structs(ntype_child_child)) {
          check_if_op(lnast, lnidx_opr_child_child);
        }
      }
    } else if (ntype_child.is_cond()) {
      if (lnast->has_single_child(lnidx_opr_child)) {
        cond                       = true;
        auto lnidx_opr_child_child = lnast->get_first_child(lnidx_opr_child);
        auto ntype_child_child     = lnast->get_data(lnidx_opr_child_child).type;
        if (!ntype_child_child.is_const() && !ntype_child_child.is_ref()) {
          Pass::error("Func Def Operation Error: Condition must be Node type 'ref' or 'const'\n");
        }
        // Store type 'ref' variables
        if (ntype_child_child.is_ref()) {
          add_to_read_list(lnast->get_name(lnidx_opr_child_child));
        }
      } else {
        Pass::error("Func Def Operation Error: Missing Condition Node\n");
      }
    } else if (ntype_child.is_ref()) {
      add_to_read_list(lnast->get_name(lnidx_opr_child));
      num_of_refs += 1;
    } else {
      Pass::error("Func Def Operation Error: Not a Valid Node Type\n");
    }
  }
  if (num_of_refs < 1) {
    Pass::error("Func Def Operation Error: Missing Reference Node\n");
  } else if (!cond) {
    Pass::error("Func Def Operation Error: Missing Condition Node\n");
  } else if (!stmts) {
    Pass::error("Func Def Operation Error: Missing Statement Node\n");
  }
}

void Semantic_check::check_func_call(Lnast *lnast, const Lnast_nid &lnidx_opr) {
  int num_of_refs = 0;
  for (const auto &lnidx_opr_child : lnast->children(lnidx_opr)) {
    const auto ntype_child = lnast->get_data(lnidx_opr_child).type;

    if (lnidx_opr_child == lnast->get_first_child(lnidx_opr)) {
      num_of_refs += 1;
      add_to_write_list(lnast->get_name(lnidx_opr_child));
      continue;
    }
    if (ntype_child.is_ref()) {
      num_of_refs += 1;
      add_to_read_list(lnast->get_name(lnidx_opr_child));
    } else if (!ntype_child.is_ref()) {
      Pass::error("Func Call Operation Error: Condition must be Node type 'ref'\n");
    } else {
      Pass::error("Func Call Operation Error: Not a Valid Node Type\n");
    }
  }
  if (num_of_refs != 3) {
    Pass::error("Func Call Operation Error: Missing Reference Node(s)\n");
  }
}

// NOTE: Test does only consider tuple and tuple concat operations
void Semantic_check::do_check(Lnast *lnast) {
  // Get Lnast Root
  const auto top = lnast->get_root();
  // Get Lnast top statements
  const auto stmts = lnast->get_first_child(top);
  // Iterate through Lnast top statements
  for (const auto &stmt : lnast->children(stmts)) {
    const auto ntype = lnast->get_data(stmt).type;

    if (is_primitive_op(ntype)) {
      check_primitive_ops(lnast, stmt, ntype);
    } else if (ntype.is_if()) {
      check_if_op(lnast, stmt);
    } else if (ntype.is_for()) {
      check_for_op(lnast, stmt);
    } else if (ntype.is_while()) {
      check_while_op(lnast, stmt);
    } else if (ntype.is_func_call()) {
      check_func_call(lnast, stmt);
    } else if (ntype.is_func_def()) {
      check_func_def(lnast, stmt);
    }
  }

  resolve_assign_lhs_rhs_lists();
  resolve_read_write_lists();
}