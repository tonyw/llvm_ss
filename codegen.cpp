#include <cctype>
#include <cstdio>
#include <map>
#include <string>
#include <sstream>
#include <vector>
#include <iostream>

#include "toy.h"

using namespace llvm;



namespace toy {

    /// LogError* - These are little helper functions for error handling.
    std::unique_ptr<ExprAST> LogError(const char *Str) {
    fprintf(stderr, "Error: %s\n", Str);
    return nullptr;
    }
    std::unique_ptr<PrototypeAST> LogErrorP(const char *Str) {
    LogError(Str);
    return nullptr;
    }


Function *getFunction(std::string Name) {
    // First, see if the function has already been added to the current module.
    if (auto *F = Global::global()->module->getFunction(Name)) {
        return F;
    }
    // If not, check whether we can codegen the declaration from some existing
    // prototype.
    auto func = Global::global()->FunctionProtos.find(Name);
    if (func != Global::global()->FunctionProtos.end()) {
        return func->second->codegen();
    }
    // If no existing prototype exists, return null.
    return nullptr;
}

Value *BinaryAST::codegen() {
    Value *L = LHS->codegen();
    Value *R = RHS->codegen();
    if (L == 0 || R == 0) {
        return 0;
    }
    switch (op) {
        case '<':
            return Global::global()->builder.CreateFCmpOLT(L, R, "cmplt");
        case '>':
            return Global::global()->builder.CreateFCmpOGT(L, R, "cmpgt");
        case '+':
            return Global::global()->builder.CreateFAdd(L, R, "addtmp");
        case '-':
            return Global::global()->builder.CreateFSub(L, R, "subtmp");
        case '*':
            return Global::global()->builder.CreateFMul(L, R, "multmp");
        case '/':
            return Global::global()->builder.CreateUDiv(L, R, "divtmp");
        default:
            return 0;
    }
}

Value *CallExprAST::codegen() {
    Function *CalleeF = Global::global()->module->getFunction(callee);

    if(!CalleeF){
        fprintf(stderr, "Error: can't find function %s\n", callee.c_str());
        return nullptr;
    }

    std::vector<Value *> ArgsV;
    for (unsigned i = 0, e = args.size(); i != e; ++i) {
        ArgsV.push_back(args[i]->codegen());
        if (ArgsV.back() == 0)
            return 0;
    }

    return Global::global()->builder.CreateCall(CalleeF, ArgsV, "calltmp");
}

Function *PrototypeAST::codegen() {
    std::vector<Type *> floats(args.size(), Type::getFloatTy(Global::global()->context));
    FunctionType *funcType = FunctionType::get(Type::getFloatTy(Global::global()->context), floats, false);
    Function *func = Function::Create(funcType, 
                        Function::ExternalLinkage, 
                        name, 
                        Global::global()->module.get());
    size_t idx = 0;
    for (auto &arg:func->args()) {
        arg.setName(args[idx++]);
    }
    return func;
}

Function *FunctionAST::codegen() {
    auto &p = *proto;
    Global::global()->FunctionProtos[proto->getName()] = std::move(proto);
    Function *theFunc = getFunction(p.getName());
    if (!theFunc) {
        return nullptr;
    }
    BasicBlock *bb = BasicBlock::Create(Global::global()->context, "entry", theFunc);
    Global::global()->builder.SetInsertPoint(bb);
    Global::global()->NamedValues.clear();
    for (auto &arg:theFunc->args()) {
        Global::global()->NamedValues[arg.getName()] = &arg;
    }

    if (Value *retVal = body->codegen()) {
        Global::global()->builder.CreateRet(retVal);
        verifyFunction(*theFunc);
        Global::global()->funcPassManager->run(*theFunc);
        return theFunc;
    }

    theFunc->eraseFromParent();
    return nullptr;
}

Value *NumericAST::codegen() {
    return ConstantFP::get(Global::global()->context, APFloat(numVal));
}

Value *VariableAST::codegen() {
    Value *v = Global::global()->NamedValues[varName];
    if (!v)
        return 0;
    return v;
}

Value *IfExprAST::codegen(){
    Value *CondV = Cond->codegen();
    if (!CondV)
        return nullptr;

    // Convert condition to a bool by comparing non-equal to 0.0.
    CondV = Global::global()->builder.CreateFCmpONE(
        CondV, ConstantFP::get(Global::global()->context, APFloat(0.0)), "ifcond");

    Function *TheFunction = Global::global()->builder.GetInsertBlock()->getParent();

    // Create blocks for the then and else cases.  Insert the 'then' block at the
    // end of the function.
    BasicBlock *ThenBB = BasicBlock::Create(Global::global()->context, "then", TheFunction);
    BasicBlock *ElseBB = BasicBlock::Create(Global::global()->context, "else");
    BasicBlock *MergeBB = BasicBlock::Create(Global::global()->context, "ifcont");

    Global::global()->builder.CreateCondBr(CondV, ThenBB, ElseBB);

    // Emit then value.
    Global::global()->builder.SetInsertPoint(ThenBB);

    Value *ThenV = Then->codegen();
    if (!ThenV)
        return nullptr;

    Global::global()->builder.CreateBr(MergeBB);
    // Codegen of 'Then' can change the current block, update ThenBB for the PHI.
    ThenBB = Global::global()->builder.GetInsertBlock();

    // Emit else block.
    TheFunction->getBasicBlockList().push_back(ElseBB);
    Global::global()->builder.SetInsertPoint(ElseBB);

    Value *ElseV = Else->codegen();
    if (!ElseV)
        return nullptr;

    Global::global()->builder.CreateBr(MergeBB);
    // Codegen of 'Else' can change the current block, update ElseBB for the PHI.
    ElseBB = Global::global()->builder.GetInsertBlock();

    // Emit merge block.
    TheFunction->getBasicBlockList().push_back(MergeBB);
    Global::global()->builder.SetInsertPoint(MergeBB);
    PHINode *PN = Global::global()->builder.CreatePHI(Type::getFloatTy(Global::global()->context), 2, "iftmp");

    PN->addIncoming(ThenV, ThenBB);
    PN->addIncoming(ElseV, ElseBB);
    return PN;
}

Global::Global():context(),builder(context){
    module = std::make_unique<Module>("toy", context);
    funcPassManager= std::make_unique<legacy::FunctionPassManager>(module.get());
    funcPassManager->add(createInstructionCombiningPass());
    funcPassManager->doInitialization();

}
Global* Global::global(){
    static Global g;
    return &g;
}

}