

#include "cminusf_builder.hpp"

#define CONST_FP(num) ConstantFP::get((float)num, module.get())
#define CONST_INT(num) ConstantInt::get(num, module.get())

// types
Type *VOID_T;
Type *INT1_T;
Type *INT32_T;
Type *INT32PTR_T;
Type *FLOAT_T;
Type *FLOATPTR_T;

bool promote(IRBuilder *builder, Value **l_val_p, Value **r_val_p) {
    bool is_int = false;
    auto &l_val = *l_val_p;
    auto &r_val = *r_val_p;
    if (l_val->get_type() == r_val->get_type()) {
        is_int = l_val->get_type()->is_integer_type();
    } else {
        if (l_val->get_type()->is_integer_type()) {
            l_val = builder->create_sitofp(l_val, FLOAT_T);
        } else {
            r_val = builder->create_sitofp(r_val, FLOAT_T);
        }
    }
    return is_int;
}

/*
 * use CMinusfBuilder::Scope to construct scopes
 * scope.enter: enter a new scope
 * scope.exit: exit current scope
 * scope.push: add a new binding to current scope
 * scope.find: find and return the value bound to the name
 */

Value *CminusfBuilder::visit(ASTProgram &node) {
    VOID_T = module->get_void_type();
    INT1_T = module->get_int1_type();
    INT32_T = module->get_int32_type();
    INT32PTR_T = module->get_int32_ptr_type();
    FLOAT_T = module->get_float_type();
    FLOATPTR_T = module->get_float_ptr_type();

    Value *ret_val = nullptr;
    for (auto &decl : node.declarations) {
        ret_val = decl->accept(*this);
    }
    return ret_val;
}

Value *CminusfBuilder::visit(ASTNum &node) {
    if (node.type == TYPE_INT) {
        return CONST_INT(node.i_val);
    }
    return CONST_FP(node.f_val);
}

Value *CminusfBuilder::visit(ASTVarDeclaration &node) {
    Type *var_type = nullptr;
    if (node.type == TYPE_INT) {
        var_type = module->get_int32_type();
    } else {
        var_type = module->get_float_type();
    }

    if (node.num == nullptr) {
        if (scope.in_global()) {
            auto *initializer = ConstantZero::get(var_type, module.get());
            auto *var = GlobalVariable::create(node.id, module.get(), var_type,
                                               false, initializer);
            scope.push(node.id, var);
        } else {
            auto *var = builder->create_alloca(var_type);
            scope.push(node.id, var);
        }
    } else {
        auto *array_type = ArrayType::get(var_type, node.num->i_val);
        if (scope.in_global()) {
            auto *initializer = ConstantZero::get(array_type, module.get());
            auto *var = GlobalVariable::create(node.id, module.get(),
                                               array_type, false, initializer);
            scope.push(node.id, var);
        } else {
            auto *var = builder->create_alloca(array_type);
            scope.push(node.id, var);
        }
    }
    return nullptr;
}

Value *CminusfBuilder::visit(ASTFunDeclaration &node) {
    FunctionType *fun_type = nullptr;
    Type *ret_type = nullptr;
    std::vector<Type *> param_types;
    if (node.type == TYPE_INT) {
        ret_type = INT32_T;
    } else if (node.type == TYPE_FLOAT) {
        ret_type = FLOAT_T;
    } else {
        ret_type = VOID_T;
    }

    for (auto &param : node.params) {
        if (param->type == TYPE_INT) {
            if (param->isarray) {
                param_types.push_back(INT32PTR_T);
            } else {
                param_types.push_back(INT32_T);
            }
        } else {
            if (param->isarray) {
                param_types.push_back(FLOATPTR_T);
            } else {
                param_types.push_back(FLOAT_T);
            }
        }
    }

    fun_type = FunctionType::get(ret_type, param_types);
    auto *func = Function::create(fun_type, node.id, module.get());
    scope.push(node.id, func);
    context.func = func;
    auto *funBB = BasicBlock::create(module.get(), "entry", func);
    builder->set_insert_point(funBB);
    scope.enter();
    context.pre_enter_scope = true;
    std::vector<Value *> args;
    for (auto &arg : func->get_args()) {
        args.push_back(&arg);
    }
    for (unsigned i = 0; i < node.params.size(); ++i) {
        if (node.params[i]->isarray) {
            Value *array_alloc = nullptr;
            if (node.params[i]->type == TYPE_INT) {
                array_alloc = builder->create_alloca(INT32PTR_T);
            } else {
                array_alloc = builder->create_alloca(FLOATPTR_T);
            }
            builder->create_store(args[i], array_alloc);
            scope.push(node.params[i]->id, array_alloc);
        } else {
            Value *alloc = nullptr;
            if (node.params[i]->type == TYPE_INT) {
                alloc = builder->create_alloca(INT32_T);
            } else {
                alloc = builder->create_alloca(FLOAT_T);
            }
            builder->create_store(args[i], alloc);
            scope.push(node.params[i]->id, alloc);
        }
    }
    node.compound_stmt->accept(*this);
    // can't deal with return in both blocks
    if (not builder->get_insert_block()->is_terminated()) {
        if (context.func->get_return_type()->is_void_type()) {
            builder->create_void_ret();
        } else if (context.func->get_return_type()->is_float_type()) {
            builder->create_ret(CONST_FP(0.));
        } else {
            builder->create_ret(CONST_INT(0));
        }
    }
    scope.exit();
    return nullptr;
}

Value *CminusfBuilder::visit(ASTParam &node) { return nullptr; }

Value *CminusfBuilder::visit(ASTCompoundStmt &node) {
    bool need_exit_scope = !context.pre_enter_scope;
    if (context.pre_enter_scope) {
        context.pre_enter_scope = false;
    } else {
        scope.enter();
    }

    for (auto &decl : node.local_declarations) {
        decl->accept(*this);
    }

    for (auto &stmt : node.statement_list) {
        stmt->accept(*this);
        if (builder->get_insert_block()->is_terminated()) {
            break;
        }
    }

    if (need_exit_scope) {
        scope.exit();
    }
    return nullptr;
}

Value *CminusfBuilder::visit(ASTExpressionStmt &node) {
    if (node.expression != nullptr) {
        return node.expression->accept(*this);
    }
    return nullptr;
}

Value *CminusfBuilder::visit(ASTSelectionStmt &node) {
    auto *ret_val = node.expression->accept(*this);
    auto *trueBB = BasicBlock::create(module.get(), "", context.func);
    BasicBlock *falseBB{};
    auto *contBB = BasicBlock::create(module.get(), "", context.func);
    Value *cond_val = nullptr;
    if (ret_val->get_type()->is_integer_type()) {
        cond_val = builder->create_icmp_ne(ret_val, CONST_INT(0));
    } else {
        cond_val = builder->create_fcmp_ne(ret_val, CONST_FP(0.));
    }

    if (node.else_statement == nullptr) {
        builder->create_cond_br(cond_val, trueBB, contBB);
    } else {
        falseBB = BasicBlock::create(module.get(), "", context.func);
        builder->create_cond_br(cond_val, trueBB, falseBB);
    }
    builder->set_insert_point(trueBB);
    node.if_statement->accept(*this);

    if (not builder->get_insert_block()->is_terminated()) {
        builder->create_br(contBB);
    }

    if (node.else_statement == nullptr) {
        // falseBB->erase_from_parent(); // did not clean up memory
    } else {
        builder->set_insert_point(falseBB);
        node.else_statement->accept(*this);
        if (not builder->get_insert_block()->is_terminated()) {
            builder->create_br(contBB);
        }
    }

    builder->set_insert_point(contBB);
    return nullptr;
}

Value *CminusfBuilder::visit(ASTIterationStmt &node) {
    auto *exprBB = BasicBlock::create(module.get(), "", context.func);
    if (not builder->get_insert_block()->is_terminated()) {
        builder->create_br(exprBB);
    }
    builder->set_insert_point(exprBB);

    auto *ret_val = node.expression->accept(*this);
    auto *trueBB = BasicBlock::create(module.get(), "", context.func);
    auto *contBB = BasicBlock::create(module.get(), "", context.func);
    Value *cond_val = nullptr;
    if (ret_val->get_type()->is_integer_type()) {
        cond_val = builder->create_icmp_ne(ret_val, CONST_INT(0));
    } else {
        cond_val = builder->create_fcmp_ne(ret_val, CONST_FP(0.));
    }

    builder->create_cond_br(cond_val, trueBB, contBB);
    builder->set_insert_point(trueBB);
    node.statement->accept(*this);
    if (not builder->get_insert_block()->is_terminated()) {
        builder->create_br(exprBB);
    }
    builder->set_insert_point(contBB);

    return nullptr;
}

Value *CminusfBuilder::visit(ASTReturnStmt &node) {
    if (node.expression == nullptr) {
        builder->create_void_ret();
    } else {
        auto *fun_ret_type =
            context.func->get_function_type()->get_return_type();
        auto *ret_val = node.expression->accept(*this);
        if (fun_ret_type != ret_val->get_type()) {
            if (fun_ret_type->is_integer_type()) {
                ret_val = builder->create_fptosi(ret_val, INT32_T);
            } else {
                ret_val = builder->create_sitofp(ret_val, FLOAT_T);
            }
        }

        builder->create_ret(ret_val);
    }

    return nullptr;
}

Value *CminusfBuilder::visit(ASTVar &node) {
    auto *var = scope.find(node.id);
    auto is_int =
        var->get_type()->get_pointer_element_type()->is_integer_type();
    auto is_float =
        var->get_type()->get_pointer_element_type()->is_float_type();
    auto is_ptr =
        var->get_type()->get_pointer_element_type()->is_pointer_type();
    bool should_return_lvalue = context.require_lvalue;
    context.require_lvalue = false;
    Value *ret_val = nullptr;
    if (node.expression == nullptr) {
        if (should_return_lvalue) {
            ret_val = var;
            context.require_lvalue = false;
        } else {
            if (is_int || is_float || is_ptr) {
                ret_val = builder->create_load(var);
            } else {
                ret_val =
                    builder->create_gep(var, {CONST_INT(0), CONST_INT(0)});
            }
        }
    } else {
        auto *val = node.expression->accept(*this);
        Value *is_neg = nullptr;
        auto *exceptBB = BasicBlock::create(module.get(), "", context.func);
        auto *contBB = BasicBlock::create(module.get(), "", context.func);
        if (val->get_type()->is_float_type()) {
            val = builder->create_fptosi(val, INT32_T);
        }

        is_neg = builder->create_icmp_lt(val, CONST_INT(0));

        builder->create_cond_br(is_neg, exceptBB, contBB);
        builder->set_insert_point(exceptBB);
        auto *neg_idx_except_fun = scope.find("neg_idx_except");
        builder->create_call(dynamic_cast<Function *>(neg_idx_except_fun), {});
        if (context.func->get_return_type()->is_void_type()) {
            builder->create_void_ret();
        } else if (context.func->get_return_type()->is_float_type()) {
            builder->create_ret(CONST_FP(0.));
        } else {
            builder->create_ret(CONST_INT(0));
        }

        builder->set_insert_point(contBB);
        Value *tmp_ptr = nullptr;
        if (is_int || is_float) {
            tmp_ptr = builder->create_gep(var, {val});
        } else if (is_ptr) {
            auto *array_load = builder->create_load(var);
            tmp_ptr = builder->create_gep(array_load, {val});
        } else {
            tmp_ptr = builder->create_gep(var, {CONST_INT(0), val});
        }
        if (should_return_lvalue) {
            ret_val = tmp_ptr;
            context.require_lvalue = false;
        } else {
            ret_val = builder->create_load(tmp_ptr);
        }
    }
    return ret_val;
}

Value *CminusfBuilder::visit(ASTAssignExpression &node) {
    auto *expr_result = node.expression->accept(*this);
    context.require_lvalue = true;
    auto *var_addr = node.var->accept(*this);
    if (var_addr->get_type()->get_pointer_element_type() !=
        expr_result->get_type()) {
        if (expr_result->get_type() == INT32_T) {
            expr_result = builder->create_sitofp(expr_result, FLOAT_T);
        } else {
            expr_result = builder->create_fptosi(expr_result, INT32_T);
        }
    }
    builder->create_store(expr_result, var_addr);
    return expr_result;
}

Value *CminusfBuilder::visit(ASTSimpleExpression &node) {
    if (node.additive_expression_r == nullptr) {
        return node.additive_expression_l->accept(*this);
    }

    auto *l_val = node.additive_expression_l->accept(*this);
    auto *r_val = node.additive_expression_r->accept(*this);
    bool is_int = promote(&*builder, &l_val, &r_val);
    Value *cmp = nullptr;
    switch (node.op) {
    case OP_LT:
        if (is_int) {
            cmp = builder->create_icmp_lt(l_val, r_val);
        } else {
            cmp = builder->create_fcmp_lt(l_val, r_val);
        }
        break;
    case OP_LE:
        if (is_int) {
            cmp = builder->create_icmp_le(l_val, r_val);
        } else {
            cmp = builder->create_fcmp_le(l_val, r_val);
        }
        break;
    case OP_GE:
        if (is_int) {
            cmp = builder->create_icmp_ge(l_val, r_val);
        } else {
            cmp = builder->create_fcmp_ge(l_val, r_val);
        }
        break;
    case OP_GT:
        if (is_int) {
            cmp = builder->create_icmp_gt(l_val, r_val);
        } else {
            cmp = builder->create_fcmp_gt(l_val, r_val);
        }
        break;
    case OP_EQ:
        if (is_int) {
            cmp = builder->create_icmp_eq(l_val, r_val);
        } else {
            cmp = builder->create_fcmp_eq(l_val, r_val);
        }
        break;
    case OP_NEQ:
        if (is_int) {
            cmp = builder->create_icmp_ne(l_val, r_val);
        } else {
            cmp = builder->create_fcmp_ne(l_val, r_val);
        }
        break;
    }

    return builder->create_zext(cmp, INT32_T);
}

Value *CminusfBuilder::visit(ASTAdditiveExpression &node) {
    if (node.additive_expression == nullptr) {
        return node.term->accept(*this);
    }

    auto *l_val = node.additive_expression->accept(*this);
    auto *r_val = node.term->accept(*this);
    bool is_int = promote(&*builder, &l_val, &r_val);
    Value *ret_val = nullptr;
    switch (node.op) {
    case OP_PLUS:
        if (is_int) {
            ret_val = builder->create_iadd(l_val, r_val);
        } else {
            ret_val = builder->create_fadd(l_val, r_val);
        }
        break;
    case OP_MINUS:
        if (is_int) {
            ret_val = builder->create_isub(l_val, r_val);
        } else {
            ret_val = builder->create_fsub(l_val, r_val);
        }
        break;
    }
    return ret_val;
}

Value *CminusfBuilder::visit(ASTTerm &node) {
    if (node.term == nullptr) {
        return node.factor->accept(*this);
    }

    auto *l_val = node.term->accept(*this);
    auto *r_val = node.factor->accept(*this);
    bool is_int = promote(&*builder, &l_val, &r_val);

    Value *ret_val = nullptr;
    switch (node.op) {
    case OP_MUL:
        if (is_int) {
            ret_val = builder->create_imul(l_val, r_val);
        } else {
            ret_val = builder->create_fmul(l_val, r_val);
        }
        break;
    case OP_DIV:
        if (is_int) {
            ret_val = builder->create_isdiv(l_val, r_val);
        } else {
            ret_val = builder->create_fdiv(l_val, r_val);
        }
        break;
    }
    return ret_val;
}

Value *CminusfBuilder::visit(ASTCall &node) {
    auto *func = dynamic_cast<Function *>(scope.find(node.id));
    std::vector<Value *> args;
    auto param_type = func->get_function_type()->param_begin();
    for (auto &arg : node.args) {
        auto *arg_val = arg->accept(*this);
        if (!arg_val->get_type()->is_pointer_type() &&
            *param_type != arg_val->get_type()) {
            if (arg_val->get_type()->is_integer_type()) {
                arg_val = builder->create_sitofp(arg_val, FLOAT_T);
            } else {
                arg_val = builder->create_fptosi(arg_val, INT32_T);
            }
        }
        args.push_back(arg_val);
        param_type++;
    }

    return builder->create_call(static_cast<Function *>(func), args);
}
