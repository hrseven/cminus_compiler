#include "cminusf_builder.hpp"
#include "BasicBlock.hpp"
#include "Constant.hpp"
#include "Function.hpp"
#include "GlobalVariable.hpp"
#include "Type.hpp"
#include "Value.hpp"
#include "logging.hpp"
#include <iostream>
#include <map>
#include <string>

#define CONST_FP(num) ConstantFP::get(num, module.get())
#define CONST_INT(num) ConstantInt::get(num, module.get())

// types
Type *VOID_T ;
Type *INT1_T ;
Type *INT32_T ;
Type *INT32PTR_T ;
Type *FLOAT_T ;
Type *FLOATPTR_T;

/*
 * use CMinusfBuilder::Scope to construct scopes
 * scope.enter: enter a new scope
 * scope.exit: exit current scope
 * scope.push: add a new binding to current scope
 * scope.find: find and return the value bound to the name
 */
std::map<std::string,Value*> dict; 

Value* CminusfBuilder::visit(ASTProgram &node) {
    VOID_T = module->get_void_type();
    INT1_T = module->get_int1_type();
    INT32_T = module->get_int32_type();
    INT32PTR_T = module->get_int32_ptr_type();
    FLOAT_T = module->get_float_type();
    FLOATPTR_T = module->get_float_ptr_type();
    if(context.false_exit)
    {
        return nullptr;
    }
    Value *ret_val = nullptr;
    for (auto &decl : node.declarations) {
        ret_val = decl->accept(*this);
    }
    return ret_val;
}

Value* CminusfBuilder::visit(ASTNum &node) {
    // TODO: This function is empty now.
    // Add some code here.
    if(context.false_exit)
    {
        return nullptr;
    }
    if(node.type==TYPE_INT)
    {
        return CONST_INT(node.i_val);
    }
    else
    {
        return CONST_FP(node.f_val);
    }
    LOG(DEBUG) << "This is DEBUG log item."<<node.i_val;
    LOG(INFO) << "This is INFO log item"<<node.i_val;
    LOG(WARNING) << "This is WARNING log item"<<node.i_val;
    LOG(ERROR) << "This is ERROR log item"<<node.i_val;
}

Value* CminusfBuilder::visit(ASTVarDeclaration &node) {
    // TODO: This function is empty now.
    // Add some code here.
    if(context.false_exit)
    {
        return nullptr;
    }
    VOID_T = module->get_void_type();
    INT1_T = module->get_int1_type();
    INT32_T = module->get_int32_type();
    INT32PTR_T = module->get_int32_ptr_type();
    FLOAT_T = module->get_float_type();
    FLOATPTR_T = module->get_float_ptr_type();
    Type *var_type;
    if (node.type == TYPE_INT) {
        var_type = INT32_T;  
    } else if (node.type == TYPE_FLOAT) {
        var_type = FLOAT_T;  
    } else {
        std::cerr << "Unsupported type for variable declaration" << std::endl;
        return nullptr;
    }

    // ��ʼ������
    Value *var_alloc = nullptr;
    
    if(context.func==nullptr)
    {
        // ȫ�ֱ���
        if (node.num) {
            // ��� num ��Ϊ nullptr����ʾ����һ����������
            int array_size = node.num->i_val;  // ���� ASTNum ������һ����Ա `value` �������С
            ArrayType *array_type = ArrayType::get(var_type, array_size);
            auto zeroinit=ConstantZero::get(array_type,module.get());
            // ��������ռ�
            if(node.type==TYPE_INT)
                var_alloc = GlobalVariable::create(node.id, module.get(), array_type, 0,zeroinit);
            else
                var_alloc = GlobalVariable::create(node.id, module.get(), array_type, 0,zeroinit);
        } else {
            // �����������������䵥�������Ŀռ�
            if(node.type==TYPE_INT)
                var_alloc = GlobalVariable::create(node.id, module.get(), var_type, 0,CONST_INT(0));
            else
                var_alloc = GlobalVariable::create(node.id, module.get(), var_type, 0,CONST_FP(0.));
        }
        
    
    }
    else 
    {
    
        if (node.num) {
            // ��� num ��Ϊ nullptr����ʾ����һ����������
            int array_size = node.num->i_val;  // ���� ASTNum ������һ����Ա `value` �������С
            ArrayType *array_type = ArrayType::get(var_type, array_size);
        
            // ��������ռ�
            var_alloc = builder->create_alloca(array_type);
        } else {
            // �����������������䵥�������Ŀռ�
            var_alloc = builder->create_alloca(var_type);
        }
        
    }
    if(node.num)
        dict[node.id]=CONST_INT(node.num->i_val);
    scope.push(node.id, var_alloc);

    return var_alloc;
}

Value* CminusfBuilder::visit(ASTFunDeclaration &node) {
    if(context.false_exit)
    {
        return nullptr;
    }
    FunctionType *fun_type;
    Type *ret_type;
    std::vector<Type *> param_types;
    if (node.type == TYPE_INT)
        ret_type = INT32_T;
    else if (node.type == TYPE_FLOAT)
        ret_type = FLOAT_T;
    else
        ret_type = VOID_T;

    for (auto &param : node.params) {
        switch (param->type) {
            case TYPE_INT:
            if(!param->isarray)
                param_types.push_back(INT32_T);
            else
                param_types.push_back(INT32PTR_T);
            break;
            case TYPE_FLOAT:
            if(!param->isarray)
                param_types.push_back(FLOAT_T);
            else
                param_types.push_back(FLOATPTR_T);
            break;
            case TYPE_VOID:
            param_types.push_back(VOID_T);
            break;
            default:
            std::cerr << "Unsupported type for function parameter" << std::endl;
        }
    }

    fun_type = FunctionType::get(ret_type, param_types);
    auto func = Function::create(fun_type, node.id, module.get());
    if(node.id=="main")
    {
        context.if_in_main=true;
    }
    scope.push(node.id, func);
    context.func = func;
    auto funBB = BasicBlock::create(module.get(), "entry", func);
    context.bb=funBB;
    builder->set_insert_point(funBB);
    scope.enter();
    std::vector<Value *> args;
    for (auto &arg : func->get_args()) {
        args.push_back(&arg);
    }
    for (unsigned int i = 0; i < node.params.size(); ++i) {
        // TODO: You need to deal with params and store them in the scope.
        auto &param = node.params[i];
        Value *param_value = args[i];
        scope.push(param->id, param_value); // ���������ƺͶ�Ӧ��ֵ����������
    }
    node.compound_stmt->accept(*this);
    if (!builder->get_insert_block()->is_terminated()) 
    {
        if (context.func->get_return_type()->is_void_type())
            builder->create_void_ret();
        else if (context.func->get_return_type()->is_float_type())
            builder->create_ret(CONST_FP(0.));
        else
            builder->create_ret(CONST_INT(0));
    }
    scope.exit();
    return nullptr;
}

Value* CminusfBuilder::visit(ASTParam &node) {
    // TODO: This function is empty now.
    // Add some code here.
    if(context.false_exit)
    {
        return nullptr;
    }
    Type *type = nullptr;
    // ȷ��LLVM�е�����
    if (node.type == TYPE_INT) {
        type = node.isarray ? INT32PTR_T : INT32_T;  // ��������������ʹ��ָ������
    } else if (node.type == TYPE_FLOAT) {
        type = node.isarray ? FLOATPTR_T : FLOAT_T;  // ��������������ʹ��ָ������
    } else {
        // �����������Ϳ��Ժ��Ի�ĳ�ַ�ʽ����
        type = VOID_T;
    }
    return nullptr;


    /*AllocaInst *alloca = builder->create_alloca(type);

    // ��������������飨������ͨ��ָ�봫�ݵģ�������Ҫ�����ݵĲ���ֵ�洢������Ŀռ�
    if (!node.isarray) {
        // �����ں����������п��Ի�ȡ��ǰ������ֵ������:
        scope.enter();
        Value *param_value = scope.find(node.id);
        scope.exit();
        builder->create_store(param_value, alloca);
    }
    

    // ���������뵽�������У�ʹ����������Է���
    scope.push(node.id, alloca);
    */
}

Value* CminusfBuilder::visit(ASTCompoundStmt &node) {
    // TODO: This function is not complete.
    // You may need to add some code here
    // to deal with complex statements. 
    // ����һ���µ�������
    if(context.false_exit)
    {
        return nullptr;
    }
    scope.enter();

    for (auto &decl : node.local_declarations) {
        decl->accept(*this);
    }

    for (auto &stmt : node.statement_list) {
        stmt->accept(*this);
        if (builder->get_insert_block()->is_terminated())
            break;
    }

    // �˳���ǰ������
    scope.exit();

    return nullptr;
}

Value* CminusfBuilder::visit(ASTExpressionStmt &node) {
    // TODO: This function is empty now.
    // Add some code here.
    if(context.false_exit)
    {
        return nullptr;
    }
    if (node.expression) {
        node.expression->accept(*this);
    }
    return nullptr;
}

Value* CminusfBuilder::visit(ASTSelectionStmt &node) {
    // �����������ʽ
    if(context.false_exit)
    {
        return nullptr;
    }
    Value *condValue_ptr = node.expression->accept(*this);
    Value *condValue_ex;
    Value *condValue;
    if(condValue_ptr->get_type()->is_pointer_type())
    {
        condValue_ex=builder->create_load(condValue_ptr);
    }
    else 
    {
        condValue_ex=condValue_ptr;
    }
    if(condValue_ex->get_type()->is_float_type())
    {
        condValue=builder->create_fcmp_ne(condValue_ex,CONST_FP(0.));
    }
    else if(condValue_ex->get_type()->is_integer_type())
    {
        condValue=builder->create_icmp_ne(condValue_ex,CONST_INT(0));
    }
    else {
    
        condValue=condValue_ex;
    
    }
    // ����������
    // ����������
    auto str=std::to_string(context.select_count);
    auto *func = builder->get_insert_block()->get_parent();
    auto *thenBB = BasicBlock::create(module.get(), "then"+str, func);
    auto *elseBB = BasicBlock::create(module.get(), "else"+str, func);
    auto *mergeBB = BasicBlock::create(module.get(), "ifcont"+str, func);
    context.select_count++;
    // ����������֧
    if (node.else_statement) {
        builder->create_cond_br(condValue, thenBB, elseBB);
    } else {
        builder->create_cond_br(condValue, thenBB, mergeBB);
    }

    // ���� then ��֧
    builder->set_insert_point(thenBB);
    node.if_statement->accept(*this);
    if (!builder->get_insert_block()->is_terminated()) {
        builder->create_br(mergeBB);
    }

    // ׼�� else ��֧��
    if (node.else_statement) {
        
        builder->set_insert_point(elseBB);
        node.else_statement->accept(*this);
        if (!builder->get_insert_block()->is_terminated()) {
            builder->create_br(mergeBB);
        }
    }
    else
    {
        builder->set_insert_point(elseBB);
        builder->create_br(mergeBB);
    }

    // ���� merge ��
    builder->set_insert_point(mergeBB);

    return nullptr;
}


Value* CminusfBuilder::visit(ASTIterationStmt &node) {
    // TODO: This function is empty now.
    // Add some code here.
    // ��ȡ��ǰ����������
    if(context.false_exit)
    {
        return nullptr;
    }
    Function *func = builder->get_insert_block()->get_parent();

    // ����������
    BasicBlock *condBB = BasicBlock::create(module.get(), "loopcond", func);
    BasicBlock *bodyBB = BasicBlock::create(module.get(), "loopbody", func);
    BasicBlock *afterBB = BasicBlock::create(module.get(), "afterloop", func);

    // ��ת��������
    builder->create_br(condBB);

    // ����������
    builder->set_insert_point(condBB);
    // �����������ʽ�Ĵ���
    Value *cond = node.expression->accept(*this);

    Value *condload;
    if(cond->get_type()->is_pointer_type())
    {
        condload=builder->create_load(cond);
    }
    else {
        condload=cond;
    }
    
    // ����������ת
    auto condValue=builder->create_icmp_ne(condload, CONST_INT(0));

    builder->create_cond_br(condValue, bodyBB, afterBB);

    // ��ѭ�������
    builder->set_insert_point(bodyBB);
    // ����ѭ����Ĵ���
    node.statement->accept(*this);
    // ִ����ѭ�������ת�������飬���¼������
    builder->create_br(condBB);

    // ����ѭ��������Ŀ�
    builder->set_insert_point(afterBB);
    return nullptr;
}

Value* CminusfBuilder::visit(ASTReturnStmt &node) {
    if(context.false_exit)
    {
        return nullptr;
    }
    if (node.expression == nullptr) {
        builder->create_void_ret();
        return nullptr;
    } else {
        // TODO: The given code is incomplete.
        // You need to solve other return cases (e.g. return an integer).
        // ���㷵�ر��ʽ��ֵ
        Value *retValue = node.expression->accept(*this);
        // ��������ָ����ظ�ֵ
        builder->create_ret(retValue);
    }
    return nullptr;
}

Value* CminusfBuilder::visit(ASTVar &node) {
    if(context.false_exit)
    {
        return nullptr;
    }
    auto varPtr = scope.find(node.id);
    
    if (!varPtr) {
        std::cerr << "Error: Variable " << node.id << " not found." << std::endl;
        return nullptr;
    }
    
    if (node.expression == nullptr) 
    {
        return varPtr;
    } 
    else 
    {
        // ��������ֵ
        Value *indexValue_ptr = node.expression->accept(*this);
        if (!indexValue_ptr) {
            std::cerr << "Error: Failed to compute index for array " << node.id << std::endl;
            return nullptr;
        }

        // ��ָ�����͵�����ֵ���س�ʵ��ֵ
        Value *indexValue;
        if (indexValue_ptr->get_type()->is_pointer_type()) {
            indexValue = builder->create_load(indexValue_ptr);
        } 
        else 
        {
            indexValue = indexValue_ptr;
        }
        auto IsNotInteger=indexValue->get_type()->is_float_type();
        if(IsNotInteger)
            indexValue=builder->create_fptosi(indexValue, INT32_T);
        Value *IsMinus=builder->create_icmp_lt(indexValue,CONST_INT(0));
        Value *IsOutBound;
        if(context.if_in_main)
        {
            IsOutBound=builder->create_icmp_ge(indexValue,dict[node.id]);
        }
        else {
            IsOutBound=builder->create_icmp_ge(indexValue,CONST_INT(10000));
        }
        auto str=std::to_string(context.judge_count);
        auto falsebb=BasicBlock::create(module.get(), "falsebb"+str, context.func);
        auto IsNotMinusbb=BasicBlock::create(module.get(), "IsNotMinusbb"+str, context.func);
        auto Valuebb=BasicBlock::create(module.get(), "Valuebb"+str, context.func);
        auto nextbb=BasicBlock::create(module.get(), "nextbb"+str, context.func);
        context.judge_count++;
        if(IsNotInteger)
        {
            builder->create_br(falsebb);
        }
        else
        {
            builder->create_cond_br(IsMinus,  falsebb, IsNotMinusbb);
        }
        builder->set_insert_point(falsebb);
        Function* false_func=static_cast<Function*>(scope.find("neg_idx_except"));
        builder->create_call(false_func, {});
        builder->create_br(nextbb);

        builder->set_insert_point(IsNotMinusbb);
        builder->create_cond_br(IsOutBound, falsebb, Valuebb);

        builder->set_insert_point(Valuebb);
        auto varValue=builder->create_gep(varPtr, {CONST_INT(0), indexValue});
        builder->create_br(nextbb);
        builder->set_insert_point(nextbb);
        
        return varValue;
        }
}

Value* CminusfBuilder::visit(ASTAssignExpression &node) {
    // TODO: This function is empty now.
    // Add some code here.
    // ���� LHS����ֵ����������ȡ���ַ
    if(context.false_exit)
    {
        return nullptr;
    }
    Value *varPtr = node.var->accept(*this);
    if (!varPtr) {
        std::cerr << "Error: Failed to resolve variable for assignment." << std::endl;
        return nullptr;
    }

    // ���� RHS����ֵ�����ʽ��ֵ
    Value *rhsValue = node.expression->accept(*this);
    if (!rhsValue) {
        std::cerr << "Error: Failed to calculate right-hand side expression for assignment." << std::endl;
        return nullptr;
    }

    auto var=builder->create_load(varPtr);
    // ����ֵ�洢����ֵ�ı�����
    if(var->get_type()!=rhsValue->get_type())
    {
        if(var->get_type()->is_integer_type())
        {
            rhsValue=builder->create_fptosi(rhsValue,var->get_type());
        }
        else if(var->get_type()->is_float_type())
        {
            rhsValue=builder->create_sitofp(rhsValue,var->get_type());
        }
    }
    builder->create_store(rhsValue, varPtr);
    
    return rhsValue;
}

Value* CminusfBuilder::visit(ASTAdditiveExpression &node) {
    if(context.false_exit)
    {
        return nullptr;
    }
    // ���ȼ���term��ֵ����Ϊ�ڹ�����ʽ��ʱ��term�ǻ����ļ��㵥λ
    Value *termValue = node.term->accept(*this);
    if (!termValue) {
        std::cerr << "Error: Failed to evaluate term expression." << std::endl;
        return nullptr;
    }

    // ���û�н�һ����additive_expression����ֱ�ӷ���term��ֵ
    if (!node.additive_expression) {
        return termValue;
    }

    // �ݹ�ؼ�����ʽ��additive_expression��ֵ
    Value *addExValue_ptr = node.additive_expression->accept(*this);
    if (!addExValue_ptr) {
        std::cerr << "Error: Failed to evaluate additive expression." << std::endl;
        return nullptr;
    }
    Value *addExValue;
    // ȷ������һ���ԣ�Ȼ����������ִ�мӷ����������
    if (addExValue_ptr->get_type()->is_float_type() || termValue->get_type()->is_float_type()) {
        // �������һ���Ǹ������ͣ��轫����ת��Ϊ��������
        if (!addExValue_ptr->get_type()->is_float_type()) {
            addExValue_ptr = builder->create_sitofp(addExValue_ptr, termValue->get_type());
        }
        if (!termValue->get_type()->is_float_type()) {
            termValue = builder->create_sitofp(termValue, addExValue_ptr->get_type());
        }
        if(addExValue_ptr->get_type()->is_pointer_type())
        {
            addExValue=builder->create_load(addExValue_ptr);
        }
        else {
        
            addExValue=addExValue_ptr;
        }
        
        // ��Ը�������������
        switch (node.op) {
            case AddOp::OP_PLUS:
                return builder->create_fadd(addExValue, termValue); // ����ӷ�
            case AddOp::OP_MINUS:
                return builder->create_fsub(addExValue, termValue); // �������
            default:
                std::cerr << "Error: Unknown additive operator." << std::endl;
                return nullptr;
        }
    } else {
        // ���������������
        if(addExValue_ptr->get_type()->is_pointer_type())
        {
            addExValue=builder->create_load(addExValue_ptr);
        }
        else {
        
            addExValue=addExValue_ptr;
        }
        switch (node.op) {
            case AddOp::OP_PLUS:
                return builder->create_iadd(addExValue, termValue); // �����ӷ�
            case AddOp::OP_MINUS:
                return builder->create_isub(addExValue, termValue); // ��������
            default:
                std::cerr << "Error: Unknown additive operator." << std::endl;
                return nullptr;
        }
    }
}

Value* CminusfBuilder::visit(ASTSimpleExpression &node) {
    if(context.false_exit)
    {
        return nullptr;
    }
    // ������ӷ����ʽ��ֵ
    Value *leftValue_ptr = node.additive_expression_l->accept(*this);
    if (!leftValue_ptr) {
        std::cerr << "Error: Failed to evaluate left additive expression." << std::endl;
        return nullptr;
    }

    // ���û�й�ϵ��������������������ֵ
    if (!node.additive_expression_r) {
        return leftValue_ptr;
    }

    // �����Ҽӷ����ʽ��ֵ
    Value *rightValue_ptr = node.additive_expression_r->accept(*this);
    if (!rightValue_ptr) {
        std::cerr << "Error: Failed to evaluate right additive expression." << std::endl;
        return nullptr;
    }
    Value * leftValue;
    Value * rightValue;
    if(leftValue_ptr->get_type()->is_pointer_type())
    {
         leftValue=builder->create_load(leftValue_ptr);
    }
    else {
         leftValue=leftValue_ptr;
    }
    if(rightValue_ptr->get_type()->is_pointer_type())
    {
         rightValue=builder->create_load(rightValue_ptr);
    }
    else {
         rightValue=rightValue_ptr;
    }
    // ��ȡ����������
    auto leftType = leftValue->get_type();
    auto rightType = rightValue->get_type();

    
    // ȷ������һ����
    if (!leftType->is_int32_type() || !rightType->is_int32_type()) {
        if (!leftType->is_float_type()) {
            leftValue = builder->create_sitofp(leftValue, rightValue->get_type());
        }
        if (!rightType->is_float_type()) {
            rightValue = builder->create_sitofp(rightValue, leftValue->get_type());
        }
    

        // ʹ�ø���Ƚ�ָ��
        switch (node.op) {
            case RelOp::OP_LT:
                return builder->create_zext(builder->create_fcmp_lt(leftValue, rightValue), INT32_T);
            case RelOp::OP_LE:
                return builder->create_zext(builder->create_fcmp_le(leftValue, rightValue), INT32_T);
            case RelOp::OP_GT:
                return builder->create_zext(builder->create_fcmp_gt(leftValue, rightValue), INT32_T);
            case RelOp::OP_GE:
                return builder->create_zext(builder->create_fcmp_ge(leftValue, rightValue), INT32_T);
            case RelOp::OP_EQ:
                return builder->create_zext(builder->create_fcmp_eq(leftValue, rightValue), INT32_T);
            case RelOp::OP_NEQ:
                return builder->create_zext(builder->create_fcmp_ne(leftValue, rightValue), INT32_T);
            default:
                std::cerr << "Error: Unknown relational operator." << std::endl;
                return nullptr;
        }
    } else {
        // ʹ�������Ƚ�ָ��
        switch (node.op) {
            case RelOp::OP_LT:
                return builder->create_zext(builder->create_icmp_lt(leftValue, rightValue), INT32_T);
            case RelOp::OP_LE:
                return builder->create_zext(builder->create_icmp_le(leftValue, rightValue), INT32_T);
            case RelOp::OP_GT:
                return builder->create_zext(builder->create_icmp_gt(leftValue, rightValue), INT32_T);
            case RelOp::OP_GE:
                return builder->create_zext(builder->create_icmp_ge(leftValue, rightValue), INT32_T);
            case RelOp::OP_EQ:
                return builder->create_zext(builder->create_icmp_eq(leftValue, rightValue), INT32_T);
            case RelOp::OP_NEQ:
                return builder->create_zext(builder->create_icmp_ne(leftValue, rightValue), INT32_T);
            default:
                std::cerr << "Error: Unknown relational operator." << std::endl;
                return nullptr;
        }
    }
}

Value* CminusfBuilder::visit(ASTTerm &node) {
    // ������ǰ���ӵ�ֵ
    if(context.false_exit)
    {
        return nullptr;
    }
    Value *factorValue = node.factor->accept(*this);
    if (!factorValue) {
        std::cerr << "Error: Failed to evaluate factor in term." << std::endl;
        return nullptr;
    }

    // ���û�еݹ��term��ֱ�ӷ��ص�ǰ���ӵ�ֵ
    if (!node.term) {
        return factorValue;
    }

    // �ݹ���������������ۻ���term��
    Value *leftValue = node.term->accept(*this);
    if (!leftValue) {
        std::cerr << "Error: Failed to evaluate left term." << std::endl;
        return nullptr;
    }

    // ȷ�����Ͳ���������
    if (leftValue->get_type()->is_int32_type() && factorValue->get_type()->is_int32_type()) {
        switch (node.op) {
            case MulOp::OP_MUL:
                return builder->create_imul(leftValue, factorValue);
            case MulOp::OP_DIV:
                return builder->create_isdiv(leftValue, factorValue);
            default:
                std::cerr << "Error: Unknown multiplication operator." << std::endl;
                return nullptr;
        }
    } else if (leftValue->get_type()->is_float_type() && factorValue->get_type()->is_float_type()) {
        switch (node.op) {
            case MulOp::OP_MUL:
                return builder->create_fmul(leftValue, factorValue);
            case MulOp::OP_DIV:
                return builder->create_fdiv(leftValue, factorValue);
            default:
                std::cerr << "Error: Unknown multiplication operator." << std::endl;
                return nullptr;
        }
    } else {
        // ���Ͳ�һ�£����б�Ҫ������ת��
        if (leftValue->get_type()->is_int32_type()) {
            leftValue = builder->create_sitofp(leftValue, factorValue->get_type());
        } else {
            factorValue = builder->create_sitofp(factorValue, leftValue->get_type());
        }

        switch (node.op) {
            case MulOp::OP_MUL:
                return builder->create_fmul(leftValue, factorValue);
            case MulOp::OP_DIV:
                return builder->create_fdiv(leftValue, factorValue);
            default:
                std::cerr << "Error: Unknown multiplication operator." << std::endl;
                return nullptr;
        }
    }
}

Value* CminusfBuilder::visit(ASTCall &node) {
    if(context.false_exit)
    {
        return nullptr;
    }
    // ��ȡ��������
    Function *callee = static_cast<Function*>(scope.find(node.id)); 
    if (!callee) {
        std::cerr << "Error: Function " << node.id << " not found." << std::endl;
        return nullptr;
    }
    Value *argValue;
    // ����ÿ���������ʽ��ֵ
    std::vector<Value*> argsValue;
    std::vector<Value *> args;
    
    for (auto &arg : callee->get_args()) {
        args.push_back(&arg);
    }
    for (auto &arg : node.args) {
        Value *argValue_ptr = arg->accept(*this);
        if (!argValue_ptr) {
            std::cerr << "Error: Failed to evaluate argument in call to " << node.id << "." << std::endl;
            return nullptr;
        }
        
        argValue=argValue_ptr;
        
        argsValue.push_back(argValue);
    }

    // ����������������
    if (callee->get_num_of_args()!= argsValue.size()) {
        std::cerr << "Error: Incorrect number of arguments in call to " << node.id << "." << std::endl;
        return nullptr;
    }

    // ���ͼ����ת��
    
    for (size_t i = 0; i < argsValue.size(); ++i) {
        Type *expectedType = args[i]->get_type();
        Type *actualType = argsValue[i]->get_type();

        if (expectedType != actualType) {
            if (expectedType->is_int32_type() && actualType->is_float_type()) {
                argsValue[i] = builder->create_fptosi(argsValue[i], expectedType);
            } 
            else if (actualType->is_pointer_type())
            {
                argsValue[i]=builder->create_load(argsValue[i]);
            }
            else if (expectedType->is_float_type() && actualType->is_int32_type()) {
                argsValue[i] = builder->create_sitofp(argsValue[i], expectedType);
            } else {
                std::cerr << "Error: Type mismatch in argument " << i << " in call to " << node.id << "." << std::endl;
                return nullptr;
            }
        }
    }

    // ���ú���
    Value *callResult = builder->create_call(callee, argsValue);
    return callResult;
}

