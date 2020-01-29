//
// Created by wangxin on 20-1-8.
//

#ifndef LLVM_SS_TOY_H
#define LLVM_SS_TOY_H

#include "llvm/IR/Value.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/IR/LegacyPassManager.h"
#include <string>
#include <map>

using namespace llvm;

namespace toy {

    enum Token_Type {
        EOF_TOKEN = 0,
        DEF_TOKEN = -1,
        EXTERN_TOKEN = -2,
        IF_TOKEN = -3,
        THEN_TOKEN = -4,
        ELSE_TOKEN = -5,
        IDENTIFIER_TOKEN = -6,
        NUMERIC_TOKEN = -7
    };

    class ExprAST {
    public:
        virtual ~ExprAST() {}

        virtual llvm::Value *codegen() = 0;
    };

    class IfExprAST : public ExprAST {
        std::unique_ptr<ExprAST> Cond, Then, Else;

    public:
        IfExprAST(std::unique_ptr<ExprAST> Cond, std::unique_ptr<ExprAST> Then,
                std::unique_ptr<ExprAST> Else)
        : Cond(std::move(Cond)), Then(std::move(Then)), Else(std::move(Else)) {}

        llvm::Value *codegen() override;
    };

    class NumericAST : public ExprAST {
        float numVal;

    public:
        NumericAST(float val) : numVal(val) {}

        llvm::Value *codegen() override;
    };

    class VariableAST : public ExprAST {
        std::string varName;

    public:
        VariableAST(const std::string &name) : varName(name) {}

        llvm::Value *codegen() override;
    };

    class BinaryAST : public ExprAST {
        char op;
        std::unique_ptr<ExprAST> LHS, RHS;

    public:
        BinaryAST(char op, std::unique_ptr<ExprAST> lhs, std::unique_ptr<ExprAST> rhs)
                : op(op), LHS(std::move(lhs)), RHS(std::move(rhs)) {}

        llvm::Value *codegen() override;
    };

    class CallExprAST : public ExprAST {
        std::string callee;
        std::vector<std::unique_ptr<ExprAST>> args;

    public:
        CallExprAST(const std::string &callee, 
        std::vector<std::unique_ptr<ExprAST>> args) 
        : callee(callee),args(std::move(args)) {}
        llvm::Value *codegen() override;
    };

    class PrototypeAST {
        std::string name;
        std::vector<std::string> args;

    public:
        PrototypeAST(const std::string &name, const std::vector<std::string> args) 
        : name(name),args(std::move(args)) {};

        const std::string &getName() const {
            return name;
        }

        llvm::Function *codegen();
    };

    class FunctionAST {
        std::unique_ptr<PrototypeAST> proto;
        std::unique_ptr<ExprAST> body;

    public:
        FunctionAST(std::unique_ptr<PrototypeAST> proto, std::unique_ptr<ExprAST> body) 
        : proto(std::move(proto)),body(std::move(body)) {}

        llvm::Function *codegen();
    };

    class Global {
    public:
        LLVMContext context;
        IRBuilder<> builder;
        std::unique_ptr<Module> module;
        std::unique_ptr<legacy::FunctionPassManager> funcPassManager;
        std::map<std::string, Value *> NamedValues;
        std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;
        static Global* global();
    private:
        Global();
    };
}

#endif //LLVM_SS_TOY_H
