//
// Created by wangxin on 20-1-8.
//

#ifndef LLVM_SS_TOY_H
#define LLVM_SS_TOY_H

#include "llvm/IR/Value.h"
#include "llvm/IR/Function.h"
#include <string>
#include <map>



namespace {

    enum Token_Type
    {
        EOF_TOKEN = 0,
        DEF_TOKEN,
        IDENTIFIER_TOKEN,
        NUMERIC_TOKEN,
        RETURN_TOKEN
    };

    class ExprAST
    {
    public:
        virtual ~ExprAST() {}

        virtual llvm::Value *codegen() = 0;
    };

    class NumericAST : public ExprAST
    {
        float numVal;

    public:
        NumericAST(float val) : numVal(val) {}

        llvm::Value *codegen() override;
    };

    class VariableAST : public ExprAST
    {
        std::string varName;

    public:
        VariableAST(const std::string &name) : varName(name) {}

        llvm::Value *codegen() override;
    };

    class BinaryAST : public ExprAST
    {
        char op;
        std::unique_ptr<ExprAST> LHS, RHS;

    public:
        BinaryAST(char op, std::unique_ptr<ExprAST> lhs, std::unique_ptr<ExprAST> rhs)
                : op(op), LHS(std::move(lhs)), RHS(std::move(rhs)) {}

        llvm::Value *codegen() override;
    };

    class CallExprAST : public ExprAST
    {
        std::string callee;
        std::vector<std::unique_ptr<ExprAST>> args;

    public:
        CallExprAST(const std::string &callee, std::vector<std::unique_ptr<ExprAST>> args) : callee(callee), args(std::move(args)) {}

        llvm::Value *codegen() override;
    };

    class PrototypeAST
    {
        std::string name;
        std::vector<std::string> args;

    public:
        PrototypeAST(const std::string &name, const std::vector<std::string> args) : name(name), args(std::move(args)){};
        const std::string &getName() const
        {
            return name;
        }
        llvm::Function *Codegen();
    };

    class FunctionAST
    {
        std::unique_ptr<PrototypeAST> proto;
        std::unique_ptr<ExprAST> body;

    public:
        FunctionAST(std::unique_ptr<PrototypeAST> proto, std::unique_ptr<ExprAST> body) : proto(std::move(proto)), body(std::move(body)) {}

        llvm::Function *Codegen();
    };
}

#endif //LLVM_SS_TOY_H
