#include "llvm/IR/Verifier.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include <cctype>
#include <cstdio>
#include <map>
#include <string>
#include <sstream>
#include <vector>
#include <iostream>

using namespace llvm;

enum Token_Type
{
    EOF_TOKEN = 0,
    DEF_TOKEN,
    IDENTIFIER_TOKEN,
    NUMERIC_TOKEN,
    RETURN_TOKEN
};

FILE *file;
static std::string Identifier_string;
static int Numeric_Val;

static int getToken()
{
    static int LastChar = ' ';
    while (isspace(LastChar))
    {
        LastChar = fgetc(file);
    }

    if (isalpha(LastChar))
    {
        Identifier_string = LastChar;
        while (isalnum((LastChar = fgetc(file))))
            Identifier_string += LastChar;

        if (Identifier_string == "def")
            return DEF_TOKEN;
        if (Identifier_string == "return")
            return RETURN_TOKEN;

        return IDENTIFIER_TOKEN;
    }

    if (isdigit(LastChar))
    {
        std::string NumStr;
        do
        {
            NumStr += LastChar;
            LastChar = fgetc(file);
        } while (isdigit(LastChar));

        Numeric_Val = strtod(NumStr.c_str(), 0);
        return NUMERIC_TOKEN;
    }

    if (LastChar == '#')
    {
        do
            LastChar = fgetc(file);
        while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

        if (LastChar != EOF)
            return getToken();
    }

    if (LastChar == EOF)
        return EOF_TOKEN;

    int ThisChar = LastChar;
    LastChar = fgetc(file);
    return ThisChar;
}

namespace
{

class ExprAST
{
public:
    virtual ~ExprAST() {}

    virtual Value *Codegen() = 0;
};

class NumericAST : public ExprAST
{
    float numVal;

public:
    NumericAST(float val) : numVal(val) {}

    Value *Codegen() override;
};

class VariableAST : public ExprAST
{
    std::string varName;

public:
    VariableAST(const std::string &name) : varName(name) {}

    Value *Codegen() override;
};

class BinaryAST : public ExprAST
{
    char op;
    std::unique_ptr<ExprAST> LHS, RHS;

public:
    BinaryAST(char op, std::unique_ptr<ExprAST> lhs, std::unique_ptr<ExprAST> rhs)
        : op(op), LHS(std::move(lhs)), RHS(std::move(rhs)) {}

    Value *Codegen() override;
};

class CallExprAST : public ExprAST
{
    std::string callee;
    std::vector<std::unique_ptr<ExprAST>> args;

public:
    CallExprAST(const std::string &callee, std::vector<std::unique_ptr<ExprAST>> args) : callee(callee), args(std::move(args)) {}

    Value *Codegen() override;
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
    Function *Codegen();
};

class FunctionAST
{
    std::unique_ptr<PrototypeAST> proto;
    std::unique_ptr<ExprAST> body;

public:
    FunctionAST(std::unique_ptr<PrototypeAST> proto, std::unique_ptr<ExprAST> body) : proto(std::move(proto)), body(std::move(body)) {}

    Function *Codegen();
};
} // namespace

static int currentToken;

static int nextToken()
{
    return currentToken = getToken();
}

static std::map<char, int> opPrecedence;

static int getBinOpPrecedence()
{
    if (!isascii(currentToken))
        return -1;

    int TokPrec = opPrecedence[currentToken];
    if (TokPrec <= 0)
        return -1;
    return TokPrec;
}

static std::unique_ptr<ExprAST> parseExpression();

static std::unique_ptr<ExprAST> parseIdentifierExpr()
{
    std::string IdName = Identifier_string;
    nextToken();
    if (currentToken != '(')
        return std::make_unique<VariableAST>(IdName);

    nextToken();

    std::vector<std::unique_ptr<ExprAST>> args;
    if (currentToken != ')')
    {
        while (true)
        {
            auto arg = parseExpression();
            if (!arg)
                return nullptr;
            args.push_back(std::move(arg));

            if (currentToken == ')')
                break;

            if (currentToken != ',')
                return 0;
            nextToken();
        }
    }
    nextToken();

    return std::make_unique<CallExprAST>(IdName, std::move(args));
}

static std::unique_ptr<ExprAST> parseNumericExpr()
{
    auto Result = std::make_unique<NumericAST>(Numeric_Val);
    nextToken();
    return std::move(Result);
}

static std::unique_ptr<ExprAST> parseParenExpr()
{
    nextToken();
    auto V = parseExpression();
    if (!V)
    {
        return nullptr;
    }
    if (currentToken != ')')
    {
        return nullptr;
    }
    nextToken();
    return V;
}

static std::unique_ptr<ExprAST> parsePrimary()
{
    switch (currentToken)
    {
    default:
        return 0;
    case IDENTIFIER_TOKEN:
        return parseIdentifierExpr();
    case NUMERIC_TOKEN:
        return parseNumericExpr();
    case '(':
        return parseParenExpr();
    }
}

static std::unique_ptr<ExprAST> parseBinOp(int exprPrec, std::unique_ptr<ExprAST> LHS)
{
    while (1)
    {
        if (currentToken == ';')
            return LHS;
        int opPrec = getBinOpPrecedence();
        printf("the op prece is %d \n", opPrec);
        if (opPrec < exprPrec)
            return LHS;

        char BinOp = currentToken;
        nextToken();

        std::unique_ptr<ExprAST> RHS = parsePrimary();
        if (!RHS)
            return nullptr;

        int nextPrec = getBinOpPrecedence();
        printf("the next prece is %d \n", opPrec);
        if (opPrec < nextPrec)
        {
            RHS = parseBinOp(opPrec + 1, std::move(RHS));
            if (!RHS)
                return nullptr;
        }

        LHS = std::make_unique<BinaryAST>(BinOp, std::move(LHS), std::move(RHS));
    }
}

static std::unique_ptr<ExprAST> parseExpression()
{
    auto LHS = parsePrimary();
    if (!LHS)
        return nullptr;
    return parseBinOp(0, std::move(LHS));
}

static std::unique_ptr<PrototypeAST> parsePrototype()
{
    if (currentToken != IDENTIFIER_TOKEN)
        return 0;

    std::string FnName = Identifier_string;
    nextToken();

    if (currentToken != '(')
        return 0;

    std::vector<std::string> Function_Argument_Names;
    while (nextToken() == IDENTIFIER_TOKEN || currentToken == ',')
    {
        if (currentToken != ',')
        {
            Function_Argument_Names.push_back(Identifier_string);
        }
    }

    if (currentToken != ')')
        return 0;
    nextToken();
    return std::make_unique<PrototypeAST>(FnName, std::move(Function_Argument_Names));
}

static std::unique_ptr<FunctionAST> parseFunctionDef()
{
    nextToken();
    std::unique_ptr<PrototypeAST> proto = parsePrototype();
    if (proto == 0)
        return 0;
    if (std::unique_ptr<ExprAST> body = parseExpression())
    {
        return std::make_unique<FunctionAST>(std::move(proto), std::move(body));
    }

    return 0;
}

static std::unique_ptr<FunctionAST> parseTopLevelExpr()
{
    if (std::unique_ptr<ExprAST> E = parseExpression())
    {
        std::unique_ptr<PrototypeAST> Func_Decl = std::make_unique<PrototypeAST>("_anon_expr", std::vector<std::string>());
        return std::make_unique<FunctionAST>(std::move(Func_Decl), std::move(E));
    }
    return 0;
}

static void initPrecedence()
{
    opPrecedence['+'] = 1;
    opPrecedence['-'] = 2;
    opPrecedence['*'] = 3;
    opPrecedence['/'] = 4;
}

//codegen

static LLVMContext context;
static IRBuilder<> builder(context);
static std::unique_ptr<Module> module;
static std::unique_ptr<legacy::FunctionPassManager> funcPassManager;
static std::map<std::string, Value *> NamedValues;

Value *NumericAST::Codegen()
{
    return ConstantFP::get(context, APFloat(numVal));
}

Value *VariableAST::Codegen()
{
    Value *v = NamedValues[varName];
    if (!v)
        return 0;
    return v;
}

Value *BinaryAST::Codegen()
{
    Value *L = LHS->Codegen();
    Value *R = RHS->Codegen();
    if (L == 0 || R == 0)
        return 0;

    switch (op)
    {
    case '+':
        return builder.CreateAdd(L, R, "addtmp");
    case '-':
        return builder.CreateSub(L, R, "subtmp");
    case '*':
        return builder.CreateMul(L, R, "multmp");
    case '/':
        return builder.CreateUDiv(L, R, "divtmp");
    default:
        return 0;
    }
}

Value *CallExprAST::Codegen()
{
    Function *CalleeF = module->getFunction(callee);

    std::vector<Value *> ArgsV;
    for (unsigned i = 0, e = args.size(); i != e; ++i)
    {
        ArgsV.push_back(args[i]->Codegen());
        if (ArgsV.back() == 0)
            return 0;
    }

    return builder.CreateCall(CalleeF, ArgsV, "calltmp");
}

Function *PrototypeAST::Codegen()
{
    std::vector<Type *> Integers(args.size(), Type::getInt32Ty(context));
    FunctionType *FT = FunctionType::get(Type::getInt32Ty(context), Integers, false);
    Function *F = Function::Create(FT, Function::ExternalLinkage, name, module.get());

    if (F->getName() != name)
    {
        F->eraseFromParent();
        F = module->getFunction(name);

        if (!F->empty())
            return 0;

        if (F->arg_size() != args.size())
            return 0;
    }

    unsigned Idx = 0;
    for (Function::arg_iterator Arg_It = F->arg_begin(); Idx != args.size(); ++Arg_It, ++Idx)
    {
        Arg_It->setName(args[Idx]);
        NamedValues[args[Idx]] = Arg_It;
    }

    return F;
}

Function *FunctionAST::Codegen()
{
    NamedValues.clear();

    Function *TheFunction = proto->Codegen();
    if (TheFunction == 0)
        return 0;

    BasicBlock *BB = BasicBlock::Create(context, "entry", TheFunction);
    builder.SetInsertPoint(BB);

    if (Value *RetVal = body->Codegen())
    {
        builder.CreateRet(RetVal);
        verifyFunction(*TheFunction);
        funcPassManager->run(*TheFunction);
        return TheFunction;
    }

    TheFunction->eraseFromParent();
    return 0;
}

void initModuleAndPassManager(char *moduleName)
{
    module = std::make_unique<Module>(moduleName, context);
    funcPassManager = std::make_unique<legacy::FunctionPassManager>(module.get());
    funcPassManager->add(createInstructionCombiningPass());
    funcPassManager->add(createReassociatePass());
    funcPassManager->add(createGVNPass());
    funcPassManager->add(createCFGSimplificationPass());
    funcPassManager->doInitialization();
}

static void handleFunctionDef()
{
    if (std::unique_ptr<FunctionAST> F = parseFunctionDef())
    {
        if (auto LF = F->Codegen())
        {
            //LF->print(errs());
        }
    }
    else
    {
        nextToken();
    }
}

static void handleTopExpression()
{
    if (std::unique_ptr<FunctionAST> F = parseTopLevelExpr())
    {

        if (auto LF = F->Codegen())
        {
            //LF->print(errs());
        }
    }
    else
    {
        nextToken();
    }
}

static void Driver()
{
    while (1)
    {
        switch (currentToken)
        {
        case EOF_TOKEN:
            return;
        case ';':
            nextToken();
            break;
        case DEF_TOKEN:
            handleFunctionDef();
            break;
        default:
            handleTopExpression();
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    initPrecedence();
    char *filename = argv[1];
    file = fopen(filename, "r");
    if (file == 0)
    {
        printf("Could not open file\n");
    }
    nextToken();
    initModuleAndPassManager(filename);
    Driver();
    module->print(errs(), nullptr);
    return 0;
}
