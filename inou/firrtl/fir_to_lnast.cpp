//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
#include "inou_firrtl.hpp"

#include <fstream>
#include <google/protobuf/util/time_util.h>
#include <iostream>
#include <stdlib.h>

#include "firrtl.pb.h"

using namespace std;

using google::protobuf::util::TimeUtil;

/* For help understanding FIRRTL/Protobuf:
 * 1) Semantics regarding FIRRTL language:
 * www2.eecs.berkeley.edu/Pubs/TechRpts/2019/EECS-2019-168.pdf
 * 2) Structure of FIRRTL Protobuf file:
 * github.com/freechipsproject/firrtl/blob/master/src/main/proto/firrtl.proto */

void Inou_firrtl::toLNAST(Eprp_var &var) {
  Inou_firrtl p(var);

  if(var.has_label("files")) {
    auto files = var.get("files");
    for (const auto &f : absl::StrSplit(files, ",")) {
      cout << "FILE: " << f << "\n";
      firrtl::FirrtlPB firrtl_input;
      fstream input(std::string(f).c_str(), ios::in | ios::binary);
      if (!firrtl_input.ParseFromIstream(&input)) {
        cerr << "Failed to parse FIRRTL from protobuf format." << endl;
        return;
      }
      p.temp_var_count = 0;
      p.seq_counter = 0;
      p.IterateCircuits(var, firrtl_input);
    }
  } else {
    cout << "No file provided. This requires a file input.\n";
    return;
  }

  // Optional:  Delete all global objects allocated by libprotobuf.
  google::protobuf::ShutdownProtobufLibrary();
}

//----------------Helper Functions--------------------------
std::string_view Inou_firrtl::create_temp_var(Lnast& lnast) {
  auto temp_var_name = lnast.add_string(absl::StrCat("___F", temp_var_count));
  temp_var_count++;
  return temp_var_name;
}

std::string_view Inou_firrtl::get_new_seq_name(Lnast& lnast) {
  auto seq_name = lnast.add_string(absl::StrCat("SEQ", seq_counter));
  seq_counter++;
  return seq_name;
}

std::string Inou_firrtl::get_full_name(const std::string term, const bool is_rhs) {
  if(std::find(input_names.begin(), input_names.end(), term) != input_names.end()) {
    //string matching "term" was found to be an input to the module
    I(is_rhs);
    return absl::StrCat("$", term);
    /*auto subfield_loc = term.find(".");
    if (subfield_loc != std::string::npos) {
      return absl::StrCat("$inp_", term);
    } else {
      return absl::StrCat("$", term);
    }*/
  } else if(std::find(output_names.begin(), output_names.end(), term) != output_names.end()) {
    return absl::StrCat("%", term);
    /*auto subfield_loc = term.find(".");
    if (subfield_loc != std::string::npos) {
      return absl::StrCat("%out_", term);
    } else {
      return absl::StrCat("%", term);
    }*/
  } else if(std::find(register_names.begin(), register_names.end(), term) != register_names.end()) {
    if (is_rhs) {
      auto q_pin_str_version = term;
      replace(q_pin_str_version.begin(), q_pin_str_version.end(), '.', '_');
      return absl::StrCat(q_pin_str_version, "__q_pin");
    } else {
      return absl::StrCat("#", term);
    }
  } else {
    return term;
  }
}

//If the bitwidth is specified, in LNAST we have to create a new variable which represents
//  the number of bits that a variable will have.
void Inou_firrtl::create_bitwidth_dot_node(Lnast& lnast, uint32_t bitwidth, Lnast_nid& parent_node, std::string port_id) {
  if (bitwidth <= 0) {
    /* No need to make a bitwidth node, 0 means implicit bitwidth.
     * If -1, then that's how I specify that the "port_id" is not an
     * actual wire but instead the general vector name. */
    return;
  }

  if (port_id.find(".") == std::string::npos) {
    // No tuple/bundles used in this port, so no need for extra dot nodes.
    auto temp_var_name = create_temp_var(lnast);

    auto idx_dot = lnast.add_child(parent_node, Lnast_node::create_dot("dot"));
    lnast.add_child(idx_dot, Lnast_node::create_ref(temp_var_name));
    lnast.add_child(idx_dot, Lnast_node::create_ref(lnast.add_string(port_id)));
    lnast.add_child(idx_dot, Lnast_node::create_ref("__bits"));

    auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
    lnast.add_child(idx_asg, Lnast_node::create_ref(temp_var_name));
    lnast.add_child(idx_asg, Lnast_node::create_const(lnast.add_string(to_string(bitwidth))));
  } else {
    /* This is a bundle so I need to create dot nodes to access the correct bundle member.
     * So instead what we do is take the bundle name and the part we want to access and
     * make a dot for that node, over and over until we finally reach an element, not a
     * bundle (which we then assign the __bits to). */
    auto tup_name = lnast.add_string( port_id.substr(0, port_id.find(".")) );
    port_id.erase(0, port_id.find(".") + 1);
    std::string_view temp_var_name;
    while (port_id.find(".") != std::string::npos) {
      std::string tup_attr = port_id.substr(0, port_id.find("."));
      port_id.erase(0, port_id.find(".") + 1);
      temp_var_name = create_temp_var(lnast);

      auto idx_dot_i = lnast.add_child(parent_node, Lnast_node::create_dot("dot"));
      lnast.add_child(idx_dot_i, Lnast_node::create_ref(temp_var_name));
      lnast.add_child(idx_dot_i, Lnast_node::create_ref(tup_name));
      if (isdigit(tup_attr[0])) {
        lnast.add_child(idx_dot_i, Lnast_node::create_const(lnast.add_string(tup_attr)));
      } else {
        lnast.add_child(idx_dot_i, Lnast_node::create_ref(lnast.add_string(tup_attr)));
      }
      tup_name = temp_var_name;
    }

    temp_var_name = create_temp_var(lnast);
    auto idx_dot = lnast.add_child(parent_node, Lnast_node::create_dot("dot"));
    lnast.add_child(idx_dot, Lnast_node::create_ref(temp_var_name));
    lnast.add_child(idx_dot, Lnast_node::create_ref(tup_name));
    if (isdigit(port_id[0])) {
      lnast.add_child(idx_dot, Lnast_node::create_const(lnast.add_string(port_id))); //port_id is stripped down
    } else {
      lnast.add_child(idx_dot, Lnast_node::create_ref(lnast.add_string(port_id))); //port_id is stripped down
    }

    //Now that we've gotten all the dot nodes to access the right thing, now we do the __bits.
    auto temp_var_name_b = create_temp_var(lnast);
    auto idx_dot_b = lnast.add_child(parent_node, Lnast_node::create_dot("dot"));
    lnast.add_child(idx_dot_b, Lnast_node::create_ref(temp_var_name_b));
    lnast.add_child(idx_dot_b, Lnast_node::create_ref(temp_var_name));
    lnast.add_child(idx_dot_b, Lnast_node::create_ref("__bits"));


    auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
    lnast.add_child(idx_asg, Lnast_node::create_ref(temp_var_name_b));
    lnast.add_child(idx_asg, Lnast_node::create_const(lnast.add_string(to_string(bitwidth))));
  }
}

uint32_t Inou_firrtl::get_bit_count(const firrtl::FirrtlPB_Type type) {
  switch (type.type_case()) {
    case 2: { //UInt type
      return type.uint_type().width().value();
    } case 3: { //SInt type
      return type.sint_type().width().value();
    } case 4: { //Clock type
      return 1;
    } case 5: { //Bundle type
      I(false); //FIXME: Not yet supported. Should it even be?
    } case 6: { //Vector type
      I(false); //FIXME: Not yet supported. Should it even be?
    } case 7: { //Fixed type
      I(false); //FIXME: Not yet supported.
    } case 8: { //Analog type
      return type.analog_type().width().value();
    } case 9: { //AsyncReset type
      return 1;
    } case 10: { //Reset type
      return 1;
    } default:
      cout << "Unknown port type." << endl;
      I(false);
  }
  return -1;
}

void Inou_firrtl::init_wire_dots(Lnast& lnast, const firrtl::FirrtlPB_Type& type, const std::string id, Lnast_nid& parent_node) {//const firrtl::FirrtlPB_Statement_Wire& expr, Lnast_nid& parent_node) {
  switch (type.type_case()) {
    case 5: { //Bundle Type
      for (int i = 0; i < type.bundle_type().field_size(); i++) {
        init_wire_dots(lnast, type.bundle_type().field(i).type(), absl::StrCat(id, ".", type.bundle_type().field(i).id()), parent_node);
      }
      break;
    } case 6: { //Vector Type
      for (uint32_t i = 0; i < type.vector_type().size(); i++) {
        init_wire_dots(lnast, type.vector_type().type(), absl::StrCat(id, ".", i), parent_node);
      }
      break;
    } case 7: { //Fixed Point
      I(false); //FIXME: Unsure how to implement
      break;
    } default: {
      /* UInt SInt Clock Analog AsyncReset Reset Types*/
      auto wire_bits = get_bit_count(type);
      create_bitwidth_dot_node(lnast, wire_bits, parent_node, id);
    }
  }
}

/* When creating a register, we have to set the register's
 * clock, reset, and init values using "dot" nodes in the LNAST.
 * These functions create all of those when a reg is first declared. */
void Inou_firrtl::init_reg_dots(Lnast& lnast, const firrtl::FirrtlPB_Type& type, std::string id, const std::string_view clock, const std::string_view reset, const std::string_view init, Lnast_nid& parent_node) {
  //init_wire_dots(lnast, expr.type(), absl::StrCat("#", expr.id()), parent_node);
  switch (type.type_case()) {
    case 5: { //Bundle Type
      for (int i = 0; i < type.bundle_type().field_size(); i++) {
        init_reg_dots(lnast, type.bundle_type().field(i).type(), absl::StrCat(id, ".", type.bundle_type().field(i).id()), clock, reset, init, parent_node);
      }
      break;
    } case 6: { //Vector Type
      for (uint32_t i = 0; i < type.vector_type().size(); i++) {
        init_reg_dots(lnast, type.vector_type().type(), absl::StrCat(id, ".", i), clock, reset, init, parent_node);
      }
      break;
    } case 7: { //Fixed Point
      I(false); //FIXME: Unsure how to implement
      break;
    } default: {
      /* UInt SInt Clock Analog AsyncReset Reset Types*/
      /*auto wire_bits = get_bit_count(type);
      create_bitwidth_dot_node(lnast, wire_bits, parent_node, id);*/
      auto reg_bits = get_bit_count(type);
      init_reg_ref_dots(lnast, id, clock, reset, init, reg_bits, parent_node);
    }
  }
}

//FIXME: Eventually add in other "dot" nodes when supported.
void Inou_firrtl::init_reg_ref_dots(Lnast& lnast, std::string id, const std::string_view clock, const std::string_view reset, const std::string_view init, uint32_t bitwidth, Lnast_nid& parent_node) {

  //Add register's name to the global list.
  register_names.push_back(id.substr(1,id.length()-1)); //Use substr to remove "#"
  fmt::print("put into register_names: {}\n", id.substr(1,id.length()-1));

  // Save 'id' for later use with qpin.
  std::string id_for_qpin = id.substr(1,id.length()-1);

  //The first step is to get a string that allows us to access the register.
  std::string_view accessor_name;
  if (id.find(".") == std::string::npos) {
    // No tuple/bundles used in this port, so no need for extra dot nodes.
    accessor_name = lnast.add_string(id);
  } else {
    /* This is a bundle so I need to create dot nodes to access the correct bundle member.
     * So instead what we do is take the bundle name and the part we want to access and
     * make a dot for that node, over and over until we finally reach an element, not a
     * bundle (which we then assign the __bits to). */
    auto tup_name = lnast.add_string( id.substr(0, id.find(".")) );
    id.erase(0, id.find(".") + 1);
    std::string_view temp_var_name;
    while (id.find(".") != std::string::npos) {
      std::string tup_attr = id.substr(0, id.find("."));
      id.erase(0, id.find(".") + 1);
      temp_var_name = create_temp_var(lnast);

      auto idx_dot_i = lnast.add_child(parent_node, Lnast_node::create_dot("dot"));
      lnast.add_child(idx_dot_i, Lnast_node::create_ref(temp_var_name));
      lnast.add_child(idx_dot_i, Lnast_node::create_ref(tup_name));
      if (isdigit(tup_attr[0])) {
        lnast.add_child(idx_dot_i, Lnast_node::create_const(lnast.add_string(tup_attr)));
      } else {
        lnast.add_child(idx_dot_i, Lnast_node::create_ref(lnast.add_string(tup_attr)));
      }
      tup_name = temp_var_name;
    }

    temp_var_name = create_temp_var(lnast);
    auto idx_dot = lnast.add_child(parent_node, Lnast_node::create_dot("dot"));
    lnast.add_child(idx_dot, Lnast_node::create_ref(temp_var_name));
    lnast.add_child(idx_dot, Lnast_node::create_ref(tup_name));
    if (isdigit(id[0])) {
      lnast.add_child(idx_dot, Lnast_node::create_const(lnast.add_string(id))); //id is stripped down
    } else {
      lnast.add_child(idx_dot, Lnast_node::create_ref(lnast.add_string(id))); //id is stripped down
    }

    accessor_name = temp_var_name;
  }


  /* Now that we have a name to access it by, we can create the
   * relevant dot nodes like: __clk_pin, __q_pin, __bits,
   * __reset_pin, and (init... how to implement?) */

  /* Since FIRRTL designs access register qpin, I need to do:
   * #reg_name.__q_pin. The name will always be ___reg_name__q_pin */
  auto qpin_var_name_temp = create_temp_var(lnast);;
  replace(id_for_qpin.begin(), id_for_qpin.end(), '.', '_');


  auto idx_dot_qp = lnast.add_child(parent_node, Lnast_node::create_dot("dot"));
  lnast.add_child(idx_dot_qp, Lnast_node::create_ref(qpin_var_name_temp));
  lnast.add_child(idx_dot_qp, Lnast_node::create_ref(accessor_name));
  lnast.add_child(idx_dot_qp, Lnast_node::create_ref("__q_pin"));

  //Required to identify ___regname__q_pin as RHS.
  auto idx_asg_qp = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
  lnast.add_child(idx_asg_qp, Lnast_node::create_ref(lnast.add_string(absl::StrCat(id_for_qpin, "__q_pin"))));
  lnast.add_child(idx_asg_qp, Lnast_node::create_ref(qpin_var_name_temp));


  // Specify __clk_pin (all registers should have this set)
  I((std::string)clock != "");
  auto temp_var_name_c = create_temp_var(lnast);

  auto idx_dot_c = lnast.add_child(parent_node, Lnast_node::create_dot("dot"));
  lnast.add_child(idx_dot_c, Lnast_node::create_ref(temp_var_name_c));
  lnast.add_child(idx_dot_c, Lnast_node::create_ref(accessor_name));
  lnast.add_child(idx_dot_c, Lnast_node::create_ref("__clk_pin"));

  auto idx_asg_c = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
  lnast.add_child(idx_asg_c, Lnast_node::create_ref(temp_var_name_c));
  lnast.add_child(idx_asg_c, Lnast_node::create_ref(clock));


  // Specify __bits, if bitwidth is explicit
  if (bitwidth > 0) {
    auto temp_var_name_b = create_temp_var(lnast);

    auto idx_dot_b = lnast.add_child(parent_node, Lnast_node::create_dot("dot"));
    lnast.add_child(idx_dot_b, Lnast_node::create_ref(temp_var_name_b));
    lnast.add_child(idx_dot_b, Lnast_node::create_ref(accessor_name));
    lnast.add_child(idx_dot_b, Lnast_node::create_ref("__bits"));

    auto idx_asg_b = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
    lnast.add_child(idx_asg_b, Lnast_node::create_ref(temp_var_name_b));
    lnast.add_child(idx_asg_b, Lnast_node::create_const(lnast.add_string(to_string(bitwidth))));
  }

  // Specify init.. (how to?)
  // FIXME: Add this eventually... might have to use __reset (code to run when reset occurs)

}

/* When a module instance is created in FIRRTL, we need to do the same
 * in LNAST. Note that the instance command in FIRRTL does not hook
 * any input or outputs. */
//FIXME: I don't think putting inp_inst_name will work since it's not specified beforehand...
void Inou_firrtl::create_module_inst(Lnast& lnast, const firrtl::FirrtlPB_Statement_Instance& inst, Lnast_nid& parent_node) {
  /*                   fn_call
   *                /     |     \
   * out_[inst_name]  mod_name  inp_[inst_name] */
  auto idx_fncall = lnast.add_child(parent_node, Lnast_node::create_func_call("fn_call"));
  lnast.add_child(idx_fncall, Lnast_node::create_ref(lnast.add_string(absl::StrCat("out_", inst.id()))));
  lnast.add_child(idx_fncall, Lnast_node::create_ref(lnast.add_string(inst.module_id())));
  lnast.add_child(idx_fncall, Lnast_node::create_ref(lnast.add_string(absl::StrCat("inp_", inst.id()))));//"null")); //FIXME: Is this correct way to show no inputs specified, but will be later?

  /* Also, I need to record this module instance in
   * a map that maps instance name to module name. */
  inst_to_mod_map[inst.id()] = inst.module_id();
}

/* No mux node type exists in LNAST. To support FIRRTL muxes, we instead
 * map a mux to an if-else statement whose condition is the same condition
 * as the first argument (the condition) of the mux. */
void Inou_firrtl::HandleMuxAssign(Lnast& lnast, const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node, const std::string lhs) {
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());

  auto idx_mux_if    = lnast.add_child(parent_node, Lnast_node::create_if("mux"));
  auto idx_cstmts    = lnast.add_child(idx_mux_if, Lnast_node::create_cstmts(get_new_seq_name(lnast)));
  auto cond_str      = lnast.add_string(ReturnExprString(lnast, expr.mux().condition(), idx_cstmts, true));
  lnast.add_child(idx_mux_if, Lnast_node::create_cond(cond_str));

  auto idx_stmt_tr   = lnast.add_child(idx_mux_if, Lnast_node::create_stmts(get_new_seq_name(lnast)));
  auto idx_stmt_f    = lnast.add_child(idx_mux_if, Lnast_node::create_stmts(get_new_seq_name(lnast)));

  InitialExprAdd(lnast, expr.mux().t_value(), idx_stmt_tr, lhs);
  InitialExprAdd(lnast, expr.mux().f_value(), idx_stmt_f, lhs);
}

/* ValidIfs get detected as the RHS of an assign statement and we can't have a child of
 * an assign be an if-typed node. Thus, we have to detect ahead of time if it is a validIf
 * if we're doing an assign. If that is the case, do this instead of using ListExprType().*/
void Inou_firrtl::HandleValidIfAssign(Lnast& lnast, const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node, const std::string lhs) {
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());

  auto idx_v_if      = lnast.add_child(parent_node, Lnast_node::create_if("validIf"));
  auto idx_cstmts    = lnast.add_child(idx_v_if, Lnast_node::create_cstmts(get_new_seq_name(lnast)));
  auto cond_str      = lnast.add_string(ReturnExprString(lnast, expr.valid_if().condition(), idx_cstmts, true));
  lnast.add_child(idx_v_if, Lnast_node::create_cond(cond_str));

  auto idx_stmt_tr   = lnast.add_child(idx_v_if, Lnast_node::create_stmts(get_new_seq_name(lnast)));
  auto idx_stmt_f    = lnast.add_child(idx_v_if, Lnast_node::create_stmts(get_new_seq_name(lnast)));

  InitialExprAdd(lnast, expr.valid_if().value(), idx_stmt_tr, lhs);

  //For validIf, if the condition is not met then what the LHS equals is undefined. We'll just use 0.
  Lnast_nid idx_asg_false;
  if(lhs.substr(0,1) == "%") {
    idx_asg_false = lnast.add_child(idx_stmt_f, Lnast_node::create_dp_assign("dp_asg"));
  } else {
    idx_asg_false = lnast.add_child(idx_stmt_f, Lnast_node::create_assign("assign"));
  }
  lnast.add_child(idx_asg_false, Lnast_node::create_ref(lnast.add_string(lhs)));
  lnast.add_child(idx_asg_false, Lnast_node::create_const("0"));
}

/* We have to handle NEQ operations different than any other primitive op.
 * This is because NEQ has to be broken down into two sub-operations:
 * checking equivalence and then performing the not. */
void Inou_firrtl::HandleNEQOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, const std::string lhs) {
  /* x = neq(e1, e2) should take graph form:
   *     equal        ~
   *    /  |  \     /   \
   *___F0  e1 e2   x  ___F0  */
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());
  I(op.arg_size() == 2);
  auto e1_str = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));
  auto e2_str = lnast.add_string(ReturnExprString(lnast, op.arg(1), parent_node, true));
  auto temp_var_name = create_temp_var(lnast);

  auto idx_eq = lnast.add_child(parent_node, Lnast_node::create_same("eq_neq"));
  lnast.add_child(idx_eq, Lnast_node::create_ref(temp_var_name));
  AttachExprStrToNode(lnast, e1_str, idx_eq);
  AttachExprStrToNode(lnast, e2_str, idx_eq);

  auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_not("asg_neq"));
  lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(lhs)));
  lnast.add_child(idx_asg, Lnast_node::create_ref(temp_var_name));
}

/* Unary operations are handled in a way where (currently) there is no LNAST
 * node type that supports unary ops. Instead, we would want to have an assign
 * node and have the "rhs" child of the assign node be "[op]temp". */
void Inou_firrtl::HandleUnaryOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, const std::string lhs) {
  //FIXME: May have to change later to accomodate binary reduction op types.
  /* x = not(e1) should take graph form: (xor_/and_/or_reduce all look same just different op)
   *     ~
   *   /   \
   *  x    e1  */
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());
  I(op.arg_size() == 1);

  auto e1_str = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));
  auto idx_not = lnast.add_child(parent_node, Lnast_node::create_not("not"));
  lnast.add_child(idx_not, Lnast_node::create_ref(lnast.add_string(lhs)));
  AttachExprStrToNode(lnast, e1_str, idx_not);
}

void Inou_firrtl::HandleAndReducOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, const std::string lhs) {
  /* x = .andR(e1) is the same as e1 == -1
   *   same
   *  /  |  \
   * x  e1  -1  */
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());
  I(op.arg_size() == 1);

  auto e1_str = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));
  auto idx_eq = lnast.add_child(parent_node, Lnast_node::create_same("andr_same"));
  lnast.add_child(idx_eq, Lnast_node::create_ref(lnast.add_string(lhs)));
  AttachExprStrToNode(lnast, e1_str, idx_eq);
  lnast.add_child(idx_eq, Lnast_node::create_const("-1"));
}

void Inou_firrtl::HandleOrReducOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, const std::string lhs) {
  /* x = .orR(e1) is the same as e1 != 0
   *     same        ~
   *    /  |  \     / \
   *___F0  e1  0   x  ___F0*/
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());
  I(op.arg_size() == 1);

  auto temp_var_name = create_temp_var(lnast);
  auto e1_str = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));

  auto idx_eq = lnast.add_child(parent_node, Lnast_node::create_same("orr_same"));
  lnast.add_child(idx_eq, Lnast_node::create_ref(temp_var_name));
  AttachExprStrToNode(lnast, e1_str, idx_eq);
  lnast.add_child(idx_eq, Lnast_node::create_const("0"));

  auto idx_not = lnast.add_child(parent_node, Lnast_node::create_not("orr_not"));
  lnast.add_child(idx_eq, Lnast_node::create_ref(lnast.add_string(lhs)));
  lnast.add_child(idx_eq, Lnast_node::create_ref(temp_var_name));
}

void Inou_firrtl::HandleXorReducOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, const std::string lhs) {
  /* x = .xorR(e1)
   *  parity_op
   *  / \
   * x  e1 */
  //FIXME: Uncomment once node type is made
  /*I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());
  I(op.arg_size() == 1);

  auto e1_str = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));
  auto idx_par = lnast.add_child(parent_node, Lnast_node::create_and("par_xorR"));
  lnast.add_child(idx_par, Lnast_node::create_ref(lnast.add_string(lhs)));
  AttachExprStrToNode(lnast, e1_str, idx_par);*/
}

void Inou_firrtl::HandleNegateOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, const std::string lhs) {
  /* x = negate(e1) should take graph form:
   *     minus
   *    /  |  \
   *   x   0   e1 */
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());
  I(op.arg_size() == 1);

  auto e1_str = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));
  auto idx_mns = lnast.add_child(parent_node, Lnast_node::create_minus("minus_negate"));
  lnast.add_child(idx_mns, Lnast_node::create_ref(lnast.add_string(lhs)));
  lnast.add_child(idx_mns, Lnast_node::create_const("0"));
  AttachExprStrToNode(lnast, e1_str, idx_mns);
}

/* The Extract Bits primitive op is invoked on some variable
 * and functions as you would expect in a language like Verilog.
 * We have to break this down into multiple statements so
 * LNAST can properly handle it (see diagram below).*/
void Inou_firrtl::HandleExtractBitsOp(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, const std::string lhs) {
  /* x = bits(e1)(numH, numL) should take graph form:
   *      range                 bit_sel               asg
   *    /   |   \             /   |   \             /     \
   *___F0 numH numL        ___F1  e1 ___F0         x    ___F1 */
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());
  I(op.arg_size() == 1 && op.const__size() == 2);

  auto e1_str = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));
  auto temp_var_name_f0 = create_temp_var(lnast);
  auto temp_var_name_f1 = create_temp_var(lnast);

  auto idx_range = lnast.add_child(parent_node, Lnast_node::create_range("range_EB"));
  lnast.add_child(idx_range, Lnast_node::create_ref(temp_var_name_f0));
  lnast.add_child(idx_range, Lnast_node::create_const(lnast.add_string(op.const_(0).value())));
  lnast.add_child(idx_range, Lnast_node::create_const(lnast.add_string(op.const_(1).value())));

  auto idx_bit_sel = lnast.add_child(parent_node, Lnast_node::create_bit_select("bit_sel_EB"));
  lnast.add_child(idx_bit_sel, Lnast_node::create_ref(temp_var_name_f1));
  AttachExprStrToNode(lnast, e1_str, idx_bit_sel);
  lnast.add_child(idx_bit_sel, Lnast_node::create_ref(temp_var_name_f0));

  Lnast_nid idx_asg;
  if(lhs.substr(0,1) == "%") {
    idx_asg = lnast.add_child(parent_node, Lnast_node::create_dp_assign("dp_asg_EB"));
  } else {
    idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("assign_EB"));
  }
  lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(lhs)));
  lnast.add_child(idx_asg, Lnast_node::create_ref(temp_var_name_f1));
}

/* The Head primitive op returns the n most-significant bits
 * from an expression. So if I had an 8-bit variable z and I
 * called head(z)(3), what would return is (in Verilog) z[7:5]. */
void Inou_firrtl::HandleHeadOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, const std::string lhs) {
  /* x = head(e1)(4) should take graph form: (like x = e1 >> (e1.__bits - 4))
   * Note: the parameter (4) has to be non-negative and l.e.q. the bitwidth of e1 in FIRRTL.
   *      dot               minus          shr
   *   /   |   \           /  |   \     /   |   \
   * ___F0 e1 __bits  ___F1 ___F0  4   x   e1  __F1 */
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());
  I(op.arg_size() == 1 && op.const__size() == 1);

  auto e1_str = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));
  auto temp_var_name_f0 = create_temp_var(lnast);
  auto temp_var_name_f1 = create_temp_var(lnast);

  auto idx_dot = lnast.add_child(parent_node, Lnast_node::create_dot("dot_head"));
  lnast.add_child(idx_dot, Lnast_node::create_ref(temp_var_name_f0));
  AttachExprStrToNode(lnast, e1_str, idx_dot);
  lnast.add_child(idx_dot, Lnast_node::create_ref("__bits"));

  auto idx_mns = lnast.add_child(parent_node, Lnast_node::create_minus("minus_head"));
  lnast.add_child(idx_mns, Lnast_node::create_ref(temp_var_name_f1));
  lnast.add_child(idx_mns, Lnast_node::create_ref(temp_var_name_f0));
  lnast.add_child(idx_mns, Lnast_node::create_const(lnast.add_string(op.const_(0).value())));

  auto idx_shr = lnast.add_child(parent_node, Lnast_node::create_shift_right("shr_head"));
  lnast.add_child(idx_shr, Lnast_node::create_ref(lnast.add_string(lhs)));
  AttachExprStrToNode(lnast, e1_str, idx_shr);
  lnast.add_child(idx_shr, Lnast_node::create_ref(temp_var_name_f1));
}

void Inou_firrtl::HandleTailOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, const std::string lhs) {
  /* x = tail(expr)(2) should take graph form:
   * NOTE: the shift right is only used to get correct # bits for :=
   *     shr         :=
   *   /  |   \      /  \
   *  x  expr  2    x  expr */
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());
  I(op.arg_size() == 1 && op.const__size() == 1);
  auto lhs_str = lnast.add_string(lhs);
  auto expr_str = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));

  auto idx_shr = lnast.add_child(parent_node, Lnast_node::create_shift_right("shr_tail"));
  //lnast.add_child(idx_shr, Lnast_node::create_ref(lhs_str));
  auto temp_var_name_f1 = create_temp_var(lnast);//FIXME: REMOVE ONCE DUMMY ASSIGNS
  lnast.add_child(idx_shr, Lnast_node::create_ref(temp_var_name_f1));//FIXME: REMOVE ONCE DUMMY ASSIGNS
  AttachExprStrToNode(lnast, expr_str, idx_shr);
  lnast.add_child(idx_shr, Lnast_node::create_const(lnast.add_string(op.const_(0).value())));

  //FIXME: REMOVE ONCE DUMMY ASSIGNS ARE DEALT WITH-----------------------------------
  auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg_tail"));
  lnast.add_child(idx_asg, Lnast_node::create_ref(lhs_str));
  lnast.add_child(idx_asg, Lnast_node::create_ref(temp_var_name_f1));
  //FIXME: REMOVE ONCE DUMMY ASSIGNS ARE DEALT WITH-----------------------------------

  auto idx_dp_asg = lnast.add_child(parent_node, Lnast_node::create_dp_assign("dpasg_tail"));
  lnast.add_child(idx_dp_asg, Lnast_node::create_ref(lhs_str));
  AttachExprStrToNode(lnast, expr_str, idx_dp_asg);
}

void Inou_firrtl::HandleConcatOp(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, const std::string lhs) {
  /* x = concat(e1, e2) is the same as Verilog's x = {e1, e2}
   * In LNAST this looks like x = (e1 << e2.__bits) | e2
   *      dot              shl           or
   *     / | \            / | \        /  |  \
   * ___F0 e2 __bits  ___F1 e1 ___F0  x ___F1 e2  */
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());
  I(op.arg_size() == 2);

  auto e1_str = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));
  auto e2_str = lnast.add_string(ReturnExprString(lnast, op.arg(1), parent_node, true));
  auto temp_var_name_f0 = create_temp_var(lnast);
  auto temp_var_name_f1 = create_temp_var(lnast);

  auto idx_dot = lnast.add_child(parent_node, Lnast_node::create_dot("dot_concat"));
  lnast.add_child(idx_dot, Lnast_node::create_ref(temp_var_name_f0));
  AttachExprStrToNode(lnast, e2_str, idx_dot);
  lnast.add_child(idx_dot, Lnast_node::create_ref("__bits"));

  auto idx_shl = lnast.add_child(parent_node, Lnast_node::create_shift_left("shl_concat"));
  lnast.add_child(idx_shl, Lnast_node::create_ref(temp_var_name_f1));
  AttachExprStrToNode(lnast, e1_str, idx_shl);
  lnast.add_child(idx_shl, Lnast_node::create_ref(temp_var_name_f0));

  auto idx_or = lnast.add_child(parent_node, Lnast_node::create_or("or_concat"));
  lnast.add_child(idx_or, Lnast_node::create_ref(lnast.add_string(lhs)));
  lnast.add_child(idx_or, Lnast_node::create_ref(temp_var_name_f1));
  AttachExprStrToNode(lnast, e2_str, idx_or);
}

void Inou_firrtl::HandlePadOp(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, const std::string lhs) {
  //I(parent_node.is_stmts() | parent_node.is_cstmts());

  /* x = pad(e)(4) sets x = e and sets bw(x) = max(4, bw(e));
   *               [___________________________if_________________________________]                         asg
   *               /               /           /                                 \                         /   \
   *        [__ cstmts__]     cond, ___F1    stmts                     [________stmts________]            x     e
   *        /           \                   /      \                  /          |           \
   *      dot            lt, <           dot         asg          dot            dot           asg
   *    /  | \          /  |   \        / | \       /   \        / | \          / | \         /   \
   * ___F0 e __bits ___F1 ___F0 4   ___F2 x __bits ___F2 4   ___F3 x __bits ___F4 e __bits ___F3 ___F4 */

  //FIXME: Is this the best possible solution?
  I(false);

  /*auto temp_var_name_f0 = create_temp_var(lnast);
  auto temp_var_name_f1 = create_temp_var(lnast);
  auto temp_var_name_f2 = create_temp_var(lnast);
  auto temp_var_name_f3 = create_temp_var(lnast);
  auto temp_var_name_f4 = create_temp_var(lnast);

  auto idx_if = lnast.add_child(parent_node, Lnast_node::create_if("if_pad"));

  auto idx_if_cstmts = lnast.add_child(idx_if, Lnast_node::create_cstmts(get_new_seq_name(lnast)));

  auto idx_dot1 = lnast.add_child(idx_if_cstmts, Lnast_node::create_dot("dot"));
  lnast.add_child(idx_dot1, Lnast_node::create_ref(temp_var_name_f0));
  I(op.arg_size() == 1);
  AttachExprToOperator(lnast, op.arg(0), idx_dot1);
  lnast.add_child(idx_dot1, Lnast_node::create_ref("__bits"));

  auto idx_lt = lnast.add_child(idx_if_cstmts, Lnast_node::create_lt("lt"));
  lnast.add_child(idx_lt, Lnast_node::create_ref(temp_var_name_f1));
  lnast.add_child(idx_lt, Lnast_node::create_ref(temp_var_name_f0));
  I(op.const__size() == 1);
  lnast.add_child(idx_lt, Lnast_node::create_const(lnast.add_string(op.const_(0).value())));


  auto idx_if_cond = lnast.add_child(idx_if, Lnast_node::create_cond(temp_var_name_f1));


  auto idx_if_stmtT = lnast.add_child(idx_if, Lnast_node::create_stmts(get_new_seq_name(lnast)));

  auto idx_dot2 = lnast.add_child(idx_if_stmtT, Lnast_node::create_dot("dot"));
  lnast.add_child(idx_dot2, Lnast_node::create_ref(temp_var_name_f2));
  lnast.add_child(idx_dot2, Lnast_node::create_ref(lnast.add_string(lhs)));
  lnast.add_child(idx_dot2, Lnast_node::create_ref("__bits"));

  auto idx_asgT = lnast.add_child(idx_if_stmtT, Lnast_node::create_assign("asg"));
  lnast.add_child(idx_asgT, Lnast_node::create_ref(temp_var_name_f2));
  I(op.const__size() == 1);
  lnast.add_child(idx_asgT, Lnast_node::create_const(op.const_(0).value()));


  auto idx_if_stmtF = lnast.add_child(idx_if, Lnast_node::create_stmts(get_new_seq_name(lnast)));

  auto idx_dot3 = lnast.add_child(idx_if_stmtF, Lnast_node::create_dot("dot"));
  lnast.add_child(idx_dot3, Lnast_node::create_ref(temp_var_name_f3));
  lnast.add_child(idx_dot3, Lnast_node::create_ref(lnast.add_string(lhs)));
  lnast.add_child(idx_dot3, Lnast_node::create_ref("__bits"));

  auto idx_dot4 = lnast.add_child(idx_if_stmtF, Lnast_node::create_dot("dot"));
  lnast.add_child(idx_dot4, Lnast_node::create_ref(temp_var_name_f4));
  AttachExprToOperator(lnast, op.arg(0), idx_dot4);
  //lnast.add_child(idx_dot4, Lnast_node::create_ref(lnast.add_string(lhs)));
  lnast.add_child(idx_dot4, Lnast_node::create_ref("__bits"));

  auto idx_asgF = lnast.add_child(idx_if_stmtF, Lnast_node::create_assign("asg"));
  lnast.add_child(idx_asgF, Lnast_node::create_ref(temp_var_name_f3));
  lnast.add_child(idx_asgF, Lnast_node::create_ref(temp_var_name_f4));


  auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg_pad"));
  lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(lhs)));
  AttachExprToOperator(lnast, op.arg(0), idx_asg);*/
}

/* This function creates the necessary LNAST nodes to express a
 * primitive operation which takes in two expression arguments
 * (dubbed as arguments in the FIRRTL spec, not parameters).
 * Note: NEQ is not handled here because no NEQ node exists in LNAST. */
void Inou_firrtl::HandleTwoExprPrimOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, const std::string lhs) {
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());
  I(op.arg_size() == 2);
  auto e1_str = ReturnExprString(lnast, op.arg(0), parent_node, true);
  auto e2_str = ReturnExprString(lnast, op.arg(1), parent_node, true);
  Lnast_nid idx_primop;

  switch(op.op()) {
    case 1: { //Op_Add
      idx_primop = lnast.add_child(parent_node, Lnast_node::create_plus("plus"));
      break;
    } case 2: { //Op_Sub
      idx_primop = lnast.add_child(parent_node, Lnast_node::create_minus("minus"));
      break;
    } case 5: { //Op_Times
      idx_primop = lnast.add_child(parent_node, Lnast_node::create_mult("mult"));
      break;
    } case 6: { //Op_Divide
      idx_primop = lnast.add_child(parent_node, Lnast_node::create_div("div"));
      break;
    } case 7: { //Op_Rem
      fmt::print("Error: Op_Rem not yet supported in LNAST.\n");//FIXME...
      I(false);
      break;
    } case 10: { //Op_Dynamic_Shift_Left
      idx_primop = lnast.add_child(parent_node, Lnast_node::create_shift_left("dshl"));
      break;
    } case 11: { //Op_Dynamic_Shift_Right
      idx_primop = lnast.add_child(parent_node, Lnast_node::create_shift_right("dshr"));
      break;
    } case 12: { //Op_Bit_And
      idx_primop = lnast.add_child(parent_node, Lnast_node::create_and("and"));
      break;
    } case 13: { //Op_Bit_Or
      idx_primop = lnast.add_child(parent_node, Lnast_node::create_or("or"));
      break;
    } case 14: { //Op_Bit_Xor
      idx_primop = lnast.add_child(parent_node, Lnast_node::create_xor("xor"));
      break;
    } case 17: { //Op_Less
      idx_primop = lnast.add_child(parent_node, Lnast_node::create_lt("lt"));
      break;
    } case 18: { //Op_Less_Eq
      idx_primop = lnast.add_child(parent_node, Lnast_node::create_le("le"));
      break;
    } case 19: { //Op_Greater
      idx_primop = lnast.add_child(parent_node, Lnast_node::create_lt("gt"));
      break;
    } case 20: { //Op_Greater_Eq
      idx_primop = lnast.add_child(parent_node, Lnast_node::create_ge("ge"));
      break;
    } case 21: { //Op_Equal
      idx_primop = lnast.add_child(parent_node, Lnast_node::create_same("same"));
      break;
    } default: {
      fmt::print("Error: expression directed into HandleTwoExprPrimOp that shouldn't have been.\n");
      I(false);
    }
  }

  lnast.add_child(idx_primop, Lnast_node::create_ref(lnast.add_string(lhs)));
  AttachExprStrToNode(lnast, lnast.add_string(e1_str), idx_primop);
  AttachExprStrToNode(lnast, lnast.add_string(e2_str), idx_primop);
}

void Inou_firrtl::HandleStaticShiftOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, const std::string lhs) {
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());
  I(op.arg_size() == 1 || op.const__size() == 1);
  auto e1_str = ReturnExprString(lnast, op.arg(0), parent_node, true);
  Lnast_nid idx_shift;

  switch(op.op()) {
    case 8: { //Op_ShiftLeft
      idx_shift = lnast.add_child(parent_node, Lnast_node::create_shift_left("shl"));
      break;
    } case 9: { //Op_Shift_Right
      idx_shift = lnast.add_child(parent_node, Lnast_node::create_shift_right("shr"));
      break;
    } default: {
      fmt::print("Error: expression directed into HandleStaticShiftOp that shouldn't have been.\n");
      I(false);
    }
  }

  lnast.add_child(idx_shift, Lnast_node::create_ref(lnast.add_string(lhs)));
  AttachExprStrToNode(lnast, lnast.add_string(e1_str), idx_shift);
  lnast.add_child(idx_shift, Lnast_node::create_const(lnast.add_string(op.const_(0).value())));
}

/* TODO:
 * May have to modify some of these? */
void Inou_firrtl::HandleTypeConvOp(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, const std::string lhs) {
  if (op.op() == firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_UINT ||
      op.op() == firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_UINT) {
    I(op.arg_size() == 1 && op.const__size() == 0);
    // Set lhs.__sign, then lhs = rhs
    auto lhs_ref = lnast.add_string(lhs);
    auto e1_str = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));
    auto temp_var_name = create_temp_var(lnast);

    auto idx_dot = lnast.add_child(parent_node, Lnast_node::create_dot("dot"));
    lnast.add_child(idx_dot, Lnast_node::create_ref(temp_var_name));
    lnast.add_child(idx_dot, Lnast_node::create_ref(lhs_ref));
    lnast.add_child(idx_dot, Lnast_node::create_ref("__sign"));

    auto idx_dot_a = lnast.add_child(parent_node, Lnast_node::create_assign("asg_d"));
    lnast.add_child(idx_dot_a, Lnast_node::create_ref(temp_var_name));
    if (op.op() == firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_UINT) {
      lnast.add_child(idx_dot_a, Lnast_node::create_ref("false"));
    } else {
      lnast.add_child(idx_dot_a, Lnast_node::create_ref("true"));
    }

    auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
    lnast.add_child(idx_asg, Lnast_node::create_ref(lhs_ref));
    lnast.add_child(idx_asg, Lnast_node::create_ref(e1_str));

  } else if (op.op() == firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_CLOCK) {
    //FIXME?: Does anything need to be done here? Other than set lhs = rhs
    I(op.arg_size() == 1 && op.const__size() == 0);
    auto lhs_ref = lnast.add_string(lhs);
    auto e1_str = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));

    auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
    lnast.add_child(idx_asg, Lnast_node::create_ref(lhs_ref));
    lnast.add_child(idx_asg, Lnast_node::create_ref(e1_str));

  } else if (op.op() == firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_FIXED_POINT) {
    fmt::print("Error: fixed point not yet supported\n");
    I(false);

  } else if (op.op() == firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_ASYNC_RESET) {
    //FIXME?: Does anything need to be done here? Other than set lhs = rhs
    I(op.arg_size() == 1 && op.const__size() == 0);
    auto lhs_ref = lnast.add_string(lhs);
    auto e1_str = lnast.add_string(ReturnExprString(lnast, op.arg(0), parent_node, true));

    auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
    lnast.add_child(idx_asg, Lnast_node::create_ref(lhs_ref));
    lnast.add_child(idx_asg, Lnast_node::create_ref(e1_str));

  } else if (op.op() == firrtl::FirrtlPB_Expression_PrimOp_Op_OP_AS_INTERVAL) {
    fmt::print("Error: intervals not yet supported\n");
    I(false);
  } else {
    fmt::print("Error: unknown type conversion op in HandleTypeConvOp\n");
    I(false);
  }
}

/* A SubField access is equivalent to accessing an element
 * of a tuple in LNAST. We have to create the associated "dot"
 * node(s) to be able to access the correct element of the
 * correct tuple. This function returns the string needed
 * to access it
 * As an example, here's the LNAST for "submod.io.a":
 *        dot              dot
 *     /   |   \         /  |  \
 * ___F0 submod io  ___F1 ___F0 a
 * where the string "___F1" would be returned by this function. */
std::string Inou_firrtl::HandleSubfieldAcc(Lnast& ln, const firrtl::FirrtlPB_Expression_SubField sub_field, Lnast_nid& parent_node, const bool is_rhs) {
  //Create a list of each tuple + the element... So submoid.io.a becomes [submod, io, a]
  std::stack<std::string> names;
  auto flattened_str = CreateNameStack(sub_field, names);
  fmt::print("flattened_str: {}, {}\n", flattened_str, is_rhs);
  auto full_str = get_full_name(flattened_str, is_rhs);
  fmt::print("\tfull_str: {}\n", full_str);
  //fmt::print("HandleSubfieldAcc: {}\n", full_str);

  /*fmt::print("[");
  while(!names.empty()) {
    fmt::print("{}, ", names.top());
    names.pop();
  }
  fmt::print("]\n");*/

  //Create each dot node
  bool first = true;
  std::string bundle_accessor;
  if (full_str.substr(0,1) == "$") {
    bundle_accessor = absl::StrCat("$inp_", names.top());
  } else if (full_str.substr(0,1) == "%") {
    bundle_accessor = absl::StrCat("%out_", names.top());
  } else if (full_str.substr(0,1) == "#") {
    bundle_accessor = absl::StrCat("#", names.top());
  } else if (full_str.length() > 7 && full_str.substr(full_str.length()-7,7) == "__q_pin") {
    return full_str;
  } else if (inst_to_mod_map.count(names.top())) {
    // If wire is part of a module instance.
    auto module_name = inst_to_mod_map[names.top()];
    auto str_without_inst = full_str.substr(full_str.find(".") + 1);
    auto dir = mod_to_io_map[std::make_pair(module_name, str_without_inst)];
    if (dir == 1) { //PORT_DIRECTION_IN
      bundle_accessor = absl::StrCat("inp_", names.top());
    } else if (dir == 2) {
      bundle_accessor = absl::StrCat("out_", names.top());
    } else {
      fmt::print("Error: direction unknown of {}\n", full_str);
      I(false);
    }
  } else {
    bundle_accessor = names.top();
  }
  //fmt::print("Bundle accessor: {}\n", bundle_accessor);
  names.pop();

  do {
    auto temp_var_name = create_temp_var(ln);
    auto element_name = names.top();
    names.pop();
    //fmt::print("dot: {} {} {}\n", temp_var_name, bundle_accessor, element_name);
    auto idx_dot = ln.add_child(parent_node, Lnast_node::create_dot(""));
    if (names.empty()) {
      ln.add_child(idx_dot, Lnast_node::create_ref(temp_var_name));
      ln.add_child(idx_dot, Lnast_node::create_ref(ln.add_string(bundle_accessor)));
      ln.add_child(idx_dot, Lnast_node::create_ref(ln.add_string(element_name)));
    } else {
      ln.add_child(idx_dot, Lnast_node::create_ref(temp_var_name));
      ln.add_child(idx_dot, Lnast_node::create_ref(ln.add_string(bundle_accessor)));
      ln.add_child(idx_dot, Lnast_node::create_ref(ln.add_string(element_name)));
    }

    bundle_accessor = temp_var_name;
  } while (!names.empty());

  /*if ((full_str.length() > 7) && (full_str.substr(full_str.length()-7,7) == "__q_pin")) {
    auto temp_var_name = create_temp_var(ln);
    auto idx_dot_q = ln.add_child(parent_node, Lnast_node::create_dot("dot_q"));
    ln.add_child(idx_dot_q, Lnast_node::create_ref(temp_var_name));
    ln.add_child(idx_dot_q, Lnast_node::create_ref(bundle_accessor));
    ln.add_child(idx_dot_q, Lnast_node::create_ref("__q_pin"));

    bundle_accessor = temp_var_name;
  }*/

  //fmt::print("return {}\n", bundle_accessor);
  return bundle_accessor;
}

std::string Inou_firrtl::CreateNameStack(const firrtl::FirrtlPB_Expression_SubField sub_field, std::stack<std::string>& names) {
  names.push(sub_field.field());
  if (sub_field.expression().has_sub_field()) {
    return absl::StrCat(CreateNameStack(sub_field.expression().sub_field(), names), ".", sub_field.field());
  } else if (sub_field.expression().has_reference()) {
    names.push(sub_field.expression().reference().id());
    return absl::StrCat(sub_field.expression().reference().id(), ".", sub_field.field());
  } else {
    I(false);
    return "";
  }
}


//----------Ports-------------------------
/* This function is used for the following syntax rules in FIRRTL:
 * creating a wire, creating a register, instantiating an input/output (port),
 *
 * This function returns a pair which holds the full name of a wire/output/input/register
 * and the bitwidth of it (if the bw is 0, that means the bitwidth will be inferred later.
 */
void Inou_firrtl::create_io_list(const firrtl::FirrtlPB_Type& type, uint8_t dir, std::string port_id,
                                    std::vector<std::tuple<std::string, uint8_t, uint32_t>>& vec) {
  switch (type.type_case()) {
    case 2: { //UInt type
      vec.push_back(std::make_tuple(port_id, dir, type.uint_type().width().value()));
      break;

    } case 3: { //SInt type
      vec.push_back(std::make_tuple(port_id, dir, type.sint_type().width().value()));
      break;

    } case 4: { //Clock type
      vec.push_back(std::make_tuple(port_id, dir, 1));
      break;

    } case 5: { //Bundle type
      const firrtl::FirrtlPB_Type_BundleType btype = type.bundle_type();
      for (int i = 0; i < type.bundle_type().field_size(); i++) {
        if(btype.field(i).is_flipped()) {
          uint8_t new_dir;
          if (dir == 1) {
            new_dir = 2;
          } else if (dir == 2) {
            new_dir = 1;
          }
          create_io_list(btype.field(i).type(), new_dir, port_id + "." + btype.field(i).id(), vec);
        } else {
          create_io_list(btype.field(i).type(), dir, port_id + "." + btype.field(i).id(), vec);
        }
      }
      break;

    } case 6: { //Vector type
      for (uint32_t i = 0; i < type.vector_type().size(); i++) {
        vec.push_back(std::make_tuple(port_id, dir, -1));
        create_io_list(type.vector_type().type(), dir, absl::StrCat(port_id, ".", i), vec);
      }
      //I(false);//FIXME: Not yet supported.
      //const firrtl::FirrtlPB_Type_VectorType vtype = type.vector_type();
      //cout << "FIXME: Vector[" << vtype.size()  << "]" << endl;
      //FIXME: How do we want to handle Vectors for LNAST? Should I flatten?
      //ListTypeInfo(vtype.type(), parent_node, );//FIXME: Should this be parent_idx?
      break;

    } case 7: { //Fixed type
      //cout << "Fixed[" << type.fixed_type().width().value() << "." << type.fixed_type().point().value() << "]" << endl;
      I(false);//FIXME: Not yet supported.
      break;

    } case 8: { //Analog type
      //cout << "Analog[" << type.uint_type().width().value() << "]" << endl;
      I(false);//FIXME: Not yet supported.
      break;

    } case 9: { //AsyncReset type
      //cout << "AsyncReset" << endl;
      vec.push_back(std::make_tuple(port_id, dir, 1));//FIXME: Anything else I need to do?
      break;

    } case 10: { //Reset type
      vec.push_back(std::make_tuple(port_id, dir, 1));
      break;

    } default:
      cout << "Unknown port type." << endl;
      I(false);
  }
}

/* This function iterates over the IO of a module and
 * sets the bitwidth of each using a dot node in LNAST. */
void Inou_firrtl::ListPortInfo(Lnast &lnast, const firrtl::FirrtlPB_Port& port, Lnast_nid parent_node) {
  std::vector<std::tuple<std::string, uint8_t, uint32_t>> port_list;//Terms are as follows: name, direction, # of bits.
  create_io_list(port.type(), port.direction(), port.id(), port_list);

  fmt::print("Port_list:\n");
  for(auto val : port_list) {
    auto subfield_loc = std::get<0>(val).find(".");
    if(std::get<1>(val) == 1) { //PORT_DIRECTION_IN
      input_names.push_back(std::get<0>(val));
      if (subfield_loc != std::string::npos) {
        create_bitwidth_dot_node(lnast, std::get<2>(val), parent_node, absl::StrCat("$inp_", std::get<0>(val)));
      } else {
        create_bitwidth_dot_node(lnast, std::get<2>(val), parent_node, absl::StrCat("$", std::get<0>(val)));
      }
    } else if (std::get<1>(val) == 2) { //PORT_DIRECTION_OUT
      output_names.push_back(std::get<0>(val));
      if (subfield_loc != std::string::npos) {
        create_bitwidth_dot_node(lnast, std::get<2>(val), parent_node, absl::StrCat("%out_", std::get<0>(val)));
      } else {
        create_bitwidth_dot_node(lnast, std::get<2>(val), parent_node, absl::StrCat("%", std::get<0>(val)));
      }
    } else {
      I(false);//FIXME: I'm not sure yet how to deal with PORT_DIRECTION_UNKNOWN
    }
    fmt::print("\tname:{} dir:{} bits:{}\n", std::get<0>(val), std::get<1>(val), std::get<2>(val));
  }
}



//-----------Primitive Operations---------------------
/* TODO:
 * Need review/testing:
 *   Tail
 *   Head
 *   Neg
 *   Extract_Bits
 *   Shift_Left/Right -- In FIRRTL these are different than what is used in Verilog. May need other way to represent.
 *   Or/And/Xor_Reduce -- Reductions use same node type as normal, but will only have 1 input "ref". Is this ok?
 *   Bit_Not
 *   Not_Equal
 *   Pad
 *   As_UInt
 *   As_SInt
 *   As_Clock
 *   As_Async_Reset
 * Not yet implemented node types (?):
 *   Rem
 * Rely upon intervals:
 *   Wrap
 *   Clip
 *   Squeeze
 *   As_Interval
 * Rely upon precision/fixed point:
 *   Increase_Precision
 *   Decrease_Precision
 *   Set_Precision
 *   As_Fixed_Point
 */
void Inou_firrtl::ListPrimOpInfo(Lnast& lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, std::string lhs) {
  switch(op.op()) {
    case 1:  //Op_Add
    case 2:  //Op_Sub
    case 5:  //Op_Times
    case 6:  //Op_Divide
    case 7:  //Op_Rem
    case 10: //Op_Dynamic_Shift_Left
    case 11: //Op_Dynamic_Shift_Right
    case 12: //Op_Bit_And
    case 13: //Op_Bit_Or
    case 14: //Op_Bit_Xor
    case 17: //Op_Less
    case 18: //Op_Less_Eq
    case 19: //Op_Greater
    case 20: //Op_Greater_Eq
    case 21: { //Op_Equal
      HandleTwoExprPrimOp(lnast, op, parent_node, lhs);
      break;

    } case 3: { //Op_Tail -- take in some 'n', returns value with 'n' MSBs removed
      HandleTailOp(lnast, op, parent_node, lhs);
      break;

    } case 4: { //Op_Head -- take in some 'n', returns 'n' MSBs of variable invoked on
      HandleHeadOp(lnast, op, parent_node, lhs);
      break;

    } case 8: //Op_ShiftLeft
      case 9: { //Op_Shift_Right
      HandleStaticShiftOp(lnast, op, parent_node, lhs);
      break;

    } case 15: { //Op_Bit_Not
      HandleUnaryOp(lnast, op, parent_node, lhs);
      break;

    } case 16: { //Op_Concat
      HandleConcatOp(lnast, op, parent_node, lhs);
      break;

    } case 22: { //Op_Pad
      HandlePadOp(lnast, op, parent_node, lhs);
      break;

    } case 23: { //Op_Not_Equal
      HandleNEQOp(lnast, op, parent_node, lhs);
      break;

    } case 24: { //Op_Negate -- this takes a # (UInt or SInt) and returns it's negative value 10 -> -10 or -20 -> 20.
      //Note: the output's bitwidth = bitwidth of the input + 1.
      HandleNegateOp(lnast, op, parent_node, lhs);
      break;

    } case 27: { //Op_Convert
      cout << "primOp: " << op.op() << " not yet supported (Arithmetic convert to signed operation?)...\n";
      I(false);
      break;

    } case 30: { //Op_Extract_Bits
      HandleExtractBitsOp(lnast, op, parent_node, lhs);
      break;

    } case 28: //Op_As_UInt
      case 29: //Op_As_SInt
      case 31: //Op_As_Clock
      case 32: //Op_As_Fixed_Point
      case 38: { //Op_As_Async_Reset
      HandleTypeConvOp(lnast, op, parent_node, lhs);
      I(false);
      break;


    } case 26: { //Op_Xor_Reduce
      HandleXorReducOp(lnast, op, parent_node, lhs);
      break;

    } case 33: { //Op_And_Reduce
      HandleAndReducOp(lnast, op, parent_node, lhs);
      break;

    } case 34: { //Op_Or_Reduce
      HandleOrReducOp(lnast, op, parent_node, lhs);
      break;

    } case 35: //Op_Increase_Precision
      case 36: //Op_Decrease_Precision
      case 37: { //Op_Set_Precision
      cout << "primOp: " << op.op() << " not yet supported (FloatingPoint)...\n";
      I(false);
      break;

    } case 39: //Op_Wrap ----- FIXME: Rely upon Intervals (not supported in LNAST yet?)
      case 40: //Op_Clip ----- FIXME: Rely upon Intervals (not supported in LNAST yet?)
      case 41: //Op_Squeeze ----- FIXME: Rely upon Intervals (not supported in LNAST yet?)
      case 42: { //Op_As_interval ----- FIXME: Rely upon Intervals (not supported in LNAST yet?)
      cout << "primOp: " << op.op() << " not yet supported (Intervals)...\n";
      I(false);
      break;

    } default:
      cout << "Unknown PrimaryOp\n";
      I(false);
  }
}

//--------------Expressions-----------------------
/*TODO:
 * UIntLiteral (make sure used correct syntax: #u(bits))
 * SIntLiteral (make sure used correct syntax: #s(bits))
 * FixedLiteral
 * SubField (figure out how to include #/$/% if necessary)
 * SubAccess
 * SubIndex
 */

/* */
void Inou_firrtl::InitialExprAdd(Lnast& lnast, const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node, std::string lhs_noprefixes) {
  //Note: here, parent_node is the "stmt" node above where this expression will go.
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());
  auto lhs = get_full_name(lhs_noprefixes, false);
  switch(expr.expression_case()) {
    case 1: { //Reference
      Lnast_nid idx_asg;
      if (lhs.substr(0,1) == "%") {
        idx_asg = lnast.add_child(parent_node, Lnast_node::create_dp_assign("dp_asg"));
      } else {
        idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
      }
      lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(lhs)));
      auto full_name = get_full_name(expr.reference().id(), true);
      lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(full_name)));//expr.reference().id()));
      break;

    } case 2: { //UIntLiteral
      Lnast_nid idx_asg;
      if (lhs.substr(0,1) == "%") {
        idx_asg = lnast.add_child(parent_node, Lnast_node::create_dp_assign("dp_asg"));
      } else {
        idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
      }
      lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(lhs)));
      auto str_val = expr.uint_literal().value().value();// + "u" + to_string(expr.uint_literal().width().value());
      lnast.add_child(idx_asg, Lnast_node::create_const(lnast.add_string(str_val)));
      break;

    } case 3: { //SIntLiteral
      Lnast_nid idx_asg;
      if (lhs.substr(0,1) == "%") {
        idx_asg = lnast.add_child(parent_node, Lnast_node::create_dp_assign("dp_asg"));
      } else {
        idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
      }
      lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(lhs)));
      auto str_val = expr.sint_literal().value().value();// + "s" + to_string(expr.sint_literal().width().value());
      lnast.add_child(idx_asg, Lnast_node::create_const(lnast.add_string(str_val)));
      break;

    } case 4: { //ValidIf
      HandleValidIfAssign(lnast, expr, parent_node, lhs);
      break;

    } case 6: { //Mux
      HandleMuxAssign(lnast, expr, parent_node, lhs);
      break;

    } case 7: { //SubField
      std::string rhs = HandleSubfieldAcc(lnast, expr.sub_field(), parent_node, true);

      Lnast_nid idx_asg;
      if (lhs.substr(0,1) == "%") {
        idx_asg = lnast.add_child(parent_node, Lnast_node::create_dp_assign("dp_asg"));
      } else {
        idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg"));
      }
      lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(lhs)));
      lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(rhs)));
      break;

    } case 8: { //SubIndex
      auto expr_name = lnast.add_string(ReturnExprString(lnast, expr.sub_index().expression(), parent_node, true));

      auto idx_select = lnast.add_child(parent_node, Lnast_node::create_select("selectSI"));
      lnast.add_child(idx_select, Lnast_node::create_ref(lnast.add_string(lhs)));
      AttachExprStrToNode(lnast, expr_name, idx_select);
      lnast.add_child(idx_select, Lnast_node::create_const(lnast.add_string(expr.sub_index().index().value())));
      break;

    } case 9: { //SubAccess
      auto expr_name = lnast.add_string(ReturnExprString(lnast, expr.sub_access().expression(), parent_node, true));
      auto index_name = lnast.add_string(ReturnExprString(lnast, expr.sub_access().index(), parent_node, true));

      auto idx_select = lnast.add_child(parent_node, Lnast_node::create_select("selectSA"));
      lnast.add_child(idx_select, Lnast_node::create_ref(lnast.add_string(lhs)));
      AttachExprStrToNode(lnast, expr_name, idx_select);
      AttachExprStrToNode(lnast, index_name, idx_select);
      break;

    } case 10: { //PrimOp
      ListPrimOpInfo(lnast, expr.prim_op(), parent_node, lhs);
      break;

    } case 11: { //FixedLiteral
      auto idx_asg = lnast.add_child(parent_node, Lnast_node::create_assign("asg_FP"));
      lnast.add_child(idx_asg, Lnast_node::create_ref(lnast.add_string(lhs)));
      //FIXME: How do I represent a FixedPoint literal???
      break;

    } default:
      cout << "ERROR in InitialExprAdd ... unknown expression type: " << expr.expression_case() << endl;
      assert(false);
  }
}

/* TODO:
 *   Make sure all of these are formatted consistently with other LNAST representations.
 */
/* This function is used when I need the string to access something.
 * If it's a Reference or a Const, we format them as a string and return.
 * If it's a SubField, we have to create dot nodes and get the variable
 * name that points to the right bundle element (see HandleSubfieldAcc function). */
std::string Inou_firrtl::ReturnExprString(Lnast& lnast, const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node, const bool is_rhs) {
  I(lnast.get_data(parent_node).type.is_stmts() || lnast.get_data(parent_node).type.is_cstmts());

  std::string expr_string = "";
  switch(expr.expression_case()) {
    case 1: { //Reference
      expr_string = get_full_name(expr.reference().id(), is_rhs);
      break;
    } case 2: { //UIntLiteral
      expr_string = expr.uint_literal().value().value();//absl::StrCat(expr.uint_literal().value().value(), "u", to_string(expr.uint_literal().width().value()));;
      break;
    } case 3: { //SIntLiteral
      expr_string = expr.sint_literal().value().value();//absl::StrCat(expr.uint_literal().value().value(), "s", to_string(expr.sint_literal().width().value()));;
      //expr_string = expr.sint_literal().value().value();// + "s" + to_string(expr.sint_literal().width().value());
      break;
    } case 4: { //ValidIf
      expr_string = create_temp_var(lnast);
      HandleValidIfAssign(lnast, expr, parent_node, expr_string);
      break;
    } case 6: { //Mux
      expr_string = create_temp_var(lnast);
      HandleMuxAssign(lnast, expr, parent_node, expr_string);
      break;
    } case 7: { //SubField
      expr_string = HandleSubfieldAcc(lnast, expr.sub_field(), parent_node, is_rhs);
      break;
    } case 8: { //SubIndex
      I(false); //FIXME: Need to implement.
      break;
    } case 9: { //SubAccess
      I(false); //FIXME: Need to implement.
      break;
    } case 10: { //PrimOp
      // This case is special. We need to create a set of nodes for it and return the lhs of that node.
      expr_string = create_temp_var(lnast);
      ListPrimOpInfo(lnast, expr.prim_op(), parent_node, expr_string);
      break;
    } case 11: { //FixedLiteral
      //FIXME: Unsure of how this should be.
      I(false);
      break;
    } default: {
      //Error: I don't think this should occur if we're using Chisel's protobuf utility.
      fmt::print("Failure: {}\n", expr.expression_case());
      I(false);
    }
  }
  return expr_string;
}

/* This function takes in a string and adds it into the LNAST as
 * a child of the provided "parent_node". Note: the access_str should
 * already have any $/%/#/__q_pin added to it before this is called. */
void Inou_firrtl::AttachExprStrToNode(Lnast& lnast, const std::string_view access_str, Lnast_nid& parent_node) {
  I(!lnast.get_data(parent_node).type.is_stmts() && !lnast.get_data(parent_node).type.is_cstmts());

  auto first_char = ((std::string)access_str)[0];
  if (isdigit(first_char) || first_char == '-' || first_char == '+') {
    // Represents an integer value.
    lnast.add_child(parent_node, Lnast_node::create_const(access_str));
  } else {
    // Represents a wire/variable/io.
    lnast.add_child(parent_node, Lnast_node::create_ref(access_str));
  }
}


//------------Statements----------------------
/*TODO:
 * Memory
 * CMemory
 * Instances
 * Stop
 * Printf
 * Connect
 * PartialConnect
 * IsInvalid
 * MemoryPort
 * Attach
*/
void Inou_firrtl::ListStatementInfo(Lnast& lnast, const firrtl::FirrtlPB_Statement& stmt, Lnast_nid& parent_node) {
  //Print out statement
  switch(stmt.statement_case()) {
    case 1: { //Wire
      init_wire_dots(lnast, stmt.wire().type(), stmt.wire().id(), parent_node);
      break;

    } case 2: { //Register
      register_names.push_back(stmt.register_().id());
      auto clk_name  = lnast.add_string(ReturnExprString(lnast, stmt.register_().clock(), parent_node, true));
      auto rst_name  = lnast.add_string(ReturnExprString(lnast, stmt.register_().reset(), parent_node, true));
      auto init_name = lnast.add_string(ReturnExprString(lnast, stmt.register_().init(),  parent_node, true));
      init_reg_dots(lnast, stmt.register_().type(), absl::StrCat("#", stmt.register_().id()), clk_name, rst_name, init_name, parent_node);
      break;

    } case 3: { //Memory
      cout << "mem " << stmt.memory().id() << " :\n\t";
      //ListTypeInfo(
      cout << "\tdepth => ";
      switch(stmt.memory().depth_case()) {
        case 0: {
          cout << "Depth not set, ERROR\n";
          break;
        } case 3: {
          cout << stmt.memory().uint_depth() << "\n";
          break;
        } case 9: {
          //FIXME: Not sure this case will work properly... More testing needed.
          std::string depth = stmt.memory().bigint_depth().value();//2s complement binary rep.
          cout << depth << "\n";
          break;
        } default:
          cout << "Memory depth error\n";
      }
      cout << "\tread-latency => " << stmt.memory().read_latency() << "\n";
      cout << "\twrite-latency => " << stmt.memory().write_latency() << "\n";
      for (int i = 0; i < stmt.memory().reader_id_size(); i++) {
        cout << "\treader => " << stmt.memory().reader_id(i) << "\n";
      }
      for (int j = 0; j < stmt.memory().writer_id_size(); j++) {
        cout << "\twriter => " << stmt.memory().writer_id(j) << "\n";
      }
      for (int k = 0; k < stmt.memory().readwriter_id_size(); k++) {
        cout << "\tread-writer => " << stmt.memory().readwriter_id(k) << "\n";
      }
      cout << "\tread-under-write <= ";
      switch(stmt.memory().read_under_write()) {
        case 0:
          cout << "undefined\n";
          break;
        case 1:
          cout << "old\n";
          break;
        case 2:
          cout << "new\n";
          break;
        default:
          cout << "RUW Error...\n";
      }
      I(false);//FIXME: Memory not yet supported.
      break;

    } case 4: { //CMemory
      I(false);//FIXME: Memory not yet supported.
      break;

    } case 5: { //Instance -- creating an instance of a module inside another
      fmt::print("----Instance!\n");
      fmt::print("id: {}\n", stmt.instance().id());
      fmt::print("module_id: {}\n", stmt.instance().module_id());

      create_module_inst(lnast, stmt.instance(), parent_node);
      break;

    } case 6: { //Node -- nodes are simply named intermediates in a circuit
      InitialExprAdd(lnast, stmt.node().expression(), parent_node, stmt.node().id());
      break;

    } case 7: { //When
      auto idx_when = lnast.add_child(parent_node, Lnast_node::create_if("when"));
      auto idx_csts = lnast.add_child(idx_when, Lnast_node::create_cstmts(get_new_seq_name(lnast)));
      auto cond_str = lnast.add_string(ReturnExprString(lnast, stmt.when().predicate(), idx_csts, true));
      lnast.add_child(idx_when, Lnast_node::create_cond(cond_str));

      auto idx_stmts_t = lnast.add_child(idx_when, Lnast_node::create_stmts(get_new_seq_name(lnast)));

      for (int i = 0; i < stmt.when().consequent_size(); i++) {
        ListStatementInfo(lnast, stmt.when().consequent(i), idx_stmts_t);
      }
      if(stmt.when().otherwise_size() > 0) {
        auto idx_stmts_f = lnast.add_child(idx_when, Lnast_node::create_stmts(get_new_seq_name(lnast)));
        for (int j = 0; j < stmt.when().otherwise_size(); j++) {
          ListStatementInfo(lnast, stmt.when().otherwise(j), idx_stmts_f);
        }
      }
      break;

    } case 8: { //Stop
      cout << "stop(" << stmt.stop().return_value() << ")\n";
      I(false);
      break;

    } case 10: { //Printf
      //FIXME: Not fully implemented, I think.
      cout << "printf(" << stmt.printf().value() << ")\n";
      I(false);
      break;

    } case 14: { //Skip
      cout << "skip;\n";
      break;

    } case 15: { //Connect -- Must have form (female/bi-gender expression) <= (male/bi-gender/passive expression)
      std::string lhs_string = ReturnExprString(lnast, stmt.connect().location(), parent_node, false);
      InitialExprAdd(lnast, stmt.connect().expression(), parent_node, lhs_string);

      break;

    } case 16: { //PartialConnect
      cout << "Error: need to design partialConnect in ListStatementInfo.\n";
      I(false);
      break;

    } case 17: { //IsInvalid
      I(false);
      break;

    } case 18: { //MemoryPort
      I(false);
      break;

    } case 20: { //Attach
      cout << "Attach\n";
      I(false);
      break;

    } default:
      cout << "Unknown statement type." << endl;
      I(false);
      return;
  }

  //TODO: Attach source info into node creation (line #, col #).
}

//--------------Modules/Circuits--------------------
//Create basis of LNAST tree. Set root to "top" and have "stmts" be top's child.
void Inou_firrtl::ListUserModuleInfo(Eprp_var &var, const firrtl::FirrtlPB_Module& module) {
  cout << "Module (user): " << module.user_module().id() << endl;
  std::unique_ptr<Lnast> lnast = std::make_unique<Lnast>(module.user_module().id());

  const firrtl::FirrtlPB_Module_UserModule& user_module = module.user_module();

  //lnast = std::make_unique<Lnast>("top_module_name"); // NOTE: no need to transfer ownership (no parser)

  lnast->set_root(Lnast_node(Lnast_ntype::create_top(), Token(0, 0, 0, 0, "top")));
  auto idx_stmts = lnast->add_child(lnast->get_root(), Lnast_node::create_stmts(get_new_seq_name(*lnast)));

  //Iterate over I/O of the module.
  for (int i = 0; i < user_module.port_size(); i++) {
    const firrtl::FirrtlPB_Port& port = user_module.port(i);
    ListPortInfo(*lnast, port, idx_stmts);
  }

  //Iterate over statements of the module.
  for (int j = 0; j < user_module.statement_size(); j++) {
    const firrtl::FirrtlPB_Statement& stmt = user_module.statement(j);
    ListStatementInfo(*lnast, stmt, idx_stmts);
    //lnast->dump();
  }
  lnast->dump();
  var.add(std::move(lnast));
}

//TODO: External module handling.
void Inou_firrtl::ListModuleInfo(Eprp_var &var, const firrtl::FirrtlPB_Module& module) {
  if(module.module_case() == 1) {
    cout << "External module.\n";
    I(false); //not yet implemented
  } else if (module.module_case() == 2) {
    ListUserModuleInfo(var, module);
  } else {
    cout << "Module not set.\n";
    I(false);
  }
}

void Inou_firrtl::CreateModToIOMap(const firrtl::FirrtlPB_Circuit& circuit) {
  for (int i = 0; i < circuit.module_size(); i++) {
    //std::vector<std::pair<std::string, uint8_t>> vec;
    if (circuit.module(i).has_external_module()) {
      //mod_to_io_map[circuit.module(i).external_module().id()] = vec;
    } else if (circuit.module(i).has_user_module()) {
      for (int j = 0; j < circuit.module(i).user_module().port_size(); j++) {
        auto port = circuit.module(i).user_module().port(j);
        AddPortToMap(circuit.module(i).user_module().id(), port.type(), port.direction(), port.id());
      }
    } else {
      cout << "Module not set.\n";
      I(false);
    }
  }

  for (auto map_elem : mod_to_io_map) {
    fmt::print("Module: {}, io:{}, dir:{}\n", map_elem.first.first, map_elem.first.second, map_elem.second);
  }
}

void Inou_firrtl::AddPortToMap(const std::string mod_id, const firrtl::FirrtlPB_Type& type, uint8_t dir, std::string port_id) {
  switch (type.type_case()) {
    case 2: //UInt type
    case 3: //SInt type
    case 4: //Clock type
    case 9: //AsyncReset type
    case 10: { //Reset type
      mod_to_io_map[std::make_pair(mod_id, port_id)] = dir;
      break;

    } case 5: { //Bundle type
      const firrtl::FirrtlPB_Type_BundleType btype = type.bundle_type();
      for (int i = 0; i < type.bundle_type().field_size(); i++) {
        if(btype.field(i).is_flipped()) {
          uint8_t new_dir;
          if (dir == 1) {
            new_dir = 2;
          } else if (dir == 2) {
            new_dir = 1;
          }
          AddPortToMap(mod_id, btype.field(i).type(), new_dir, port_id + "." + btype.field(i).id());
        } else {
          AddPortToMap(mod_id, btype.field(i).type(), dir, port_id + "." + btype.field(i).id());
        }
      }
      break;

    } case 6: { //Vector type
      mod_to_io_map[std::make_pair(mod_id, port_id)] = dir;
      for (uint32_t i = 0; i < type.vector_type().size(); i++) {
        AddPortToMap(mod_id, type.vector_type().type(), dir, absl::StrCat(port_id, ".", i));
      }
      break;

    } case 7: { //Fixed type
      I(false);//FIXME: Not yet supported.
      break;

    } case 8: { //Analog type
      I(false);//FIXME: Not yet supported.
      break;

    } default:
      cout << "Unknown port type." << endl;
      I(false);
  }
}

void Inou_firrtl::IterateModules(Eprp_var &var, const firrtl::FirrtlPB_Circuit& circuit) {
  if (circuit.top_size() > 1) {
    cout << "ERROR: More than 1 top module?\n";
    I(false);
  }

  //Create ModuleName to I/O Pair List
  CreateModToIOMap(circuit);

  // For each module, create an LNAST pointer
  for (int i = 0; i < circuit.module_size(); i++) {
    //FIXME: I should empty input, output, and register lists
    ListModuleInfo(var, circuit.module(i));
  }
}

//Iterate over every FIRRTL circuit (design), each circuit can contain multiple modules.
void Inou_firrtl::IterateCircuits(Eprp_var &var, const firrtl::FirrtlPB& firrtl_input) {
  for (int i = 0; i < firrtl_input.circuit_size(); i++) {
    const firrtl::FirrtlPB_Circuit& circuit = firrtl_input.circuit(i);
    IterateModules(var, circuit);
  }
}
