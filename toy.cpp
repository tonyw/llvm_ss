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

#include "toy.h"

using namespace llvm;


FILE *file;
static std::string identifierString;
static int numericVal;

static int getToken()
{
    static int lastChar = ' ';
    while (isspace(lastChar))
    {
        lastChar = fgetc(file);
    }

    if (isalpha(lastChar))
    {
        identifierString = lastChar;
        while (isalnum((lastChar = fgetc(file))))
            identifierString += lastChar;

        if (identifierString == "def")
            return DEF_TOKEN;
        if (identifierString == "return")
            return RETURN_TOKEN;

        return IDENTIFIER_TOKEN;
    }

    if (isdigit(lastChar))
    {
        std::string numStr;
        do
        {
            numStr += lastChar;
            lastChar = fgetc(file);
        } while (isdigit(lastChar));

        numericVal = strtod(numStr.c_str(), 0);
        return NUMERIC_TOKEN;
    }

    if (lastChar == '#')
    {
        do
            lastChar = fgetc(file);
        while (lastChar != EOF && lastChar != '\n' && lastChar != '\r');

        if (lastChar != EOF)
            return getToken();
    }

    if (lastChar == EOF)
        return EOF_TOKEN;

    int theChar = lastChar;
    lastChar = fgetc(file);
    return theChar;
}


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

    int tokPrec = opPrecedence[currentToken];
    if (tokPrec <= 0)
        return -1;
    return tokPrec;
}

static std::unique_ptr<ExprAST> parseExpression();

static std::unique_ptr<ExprAST> parseIdentifierExpr()
{
    std::string idName = identifierString;
    nextToken();
    if (currentToken != '(')
        return std::make_unique<VariableAST>(idName);

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

    return std::make_unique<CallExprAST>(idName, std::move(args));
}

static std::unique_ptr<ExprAST> parseNumericExpr()
{
    auto Result = std::make_unique<NumericAST>(numericVal);
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
        if (currentToken == ';'){
            return LHS;
        }
        int opPrec = getBinOpPrecedence();
        if (opPrec < exprPrec){
            return LHS;
        }
        char BinOp = currentToken;
        nextToken();
        std::unique_ptr<ExprAST> RHS = parsePrimary();
        if (!RHS)
            return nullptr;

        int nextPrec = getBinOpPrecedence();
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
    if (!LHS){
        return nullptr;
    }
    return parseBinOp(0, std::move(LHS));
}

static std::unique_ptr<PrototypeAST> parsePrototype()
{
    if (currentToken != IDENTIFIER_TOKEN){
        return 0;
    }
    std::string FnName = identifierString;
    nextToken();

    if (currentToken != '('){
        return 0;
    }

    std::vector<std::string> Function_Argument_Names;
    while (nextToken() == IDENTIFIER_TOKEN || currentToken == ',')
    {
        if (currentToken != ',')
        {
            Function_Argument_Names.push_back(identifierString);
        }
    }

    if (currentToken != ')'){
        return 0;
    }
    nextToken();
    return std::make_unique<PrototypeAST>(FnName, std::move(Function_Argument_Names));
}

static std::unique_ptr<FunctionAST> parseFunctionDef()
{
    nextToken();
    std::unique_ptr<PrototypeAST> proto = parsePrototype();
    if (proto == 0){
        return 0;
    }
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
static std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;


Function *getFunction(std::string Name) {
  // First, see if the function has already been added to the current module.
  if (auto *F = module->getFunction(Name)){
      return F;
  }
  // If not, check whether we can codegen the declaration from some existing
  // prototype.
  auto func = FunctionProtos.find(Name);
  if (func != FunctionProtos.end()){
    return func->second->Codegen();
  }
  // If no existing prototype exists, return null.
  return nullptr;
}

Value *NumericAST::codegen()
{
    return ConstantFP::get(context, APFloat(numVal));
}

Value *VariableAST::codegen()
{
    Value *v = NamedValues[varName];
    if (!v)
        return 0;
    return v;
}

Value *BinaryAST::codegen()
{
    Value *L = LHS->codegen();
    Value *R = RHS->codegen();
    if (L == 0 || R == 0){
        return 0;
    }
    switch (op)
    {
    case '+':
        return builder.CreateFAdd(L, R, "addtmp");
    case '-':
        return builder.CreateFSub(L, R, "subtmp");
    case '*':
        return builder.CreateFMul(L, R, "multmp");
    case '/':
        return builder.CreateUDiv(L, R, "divtmp");
    default:
        return 0;
    }
}

Value *CallExprAST::codegen()
{
    Function *CalleeF = module->getFunction(callee);

    std::vector<Value *> ArgsV;
    for (unsigned i = 0, e = args.size(); i != e; ++i)
    {
        ArgsV.push_back(args[i]->codegen());
        if (ArgsV.back() == 0)
            return 0;
    }

    return builder.CreateCall(CalleeF, ArgsV, "calltmp");
}

Function *PrototypeAST::Codegen()
{
    std::vector<Type *> floats(args.size(), Type::getFloatTy(context));
    FunctionType *funcType = FunctionType::get(Type::getFloatTy(context), floats, false);
    Function *func = Function::Create(funcType, Function::ExternalLinkage, name, module.get());
    size_t idx = 0;
    for (auto & arg:func->args())
    {
        arg.setName(args[idx++]);
    }
    return func;
}

Function *FunctionAST::Codegen()
{
    auto &p=*proto;
    FunctionProtos[proto->getName()]=std::move(proto);
    Function *theFunc = getFunction(p.getName());
    if (!theFunc)
    {
        return nullptr;
    }
    BasicBlock *bb = BasicBlock::Create(context, "entry", theFunc);
    builder.SetInsertPoint(bb);
    NamedValues.clear();
    for(auto & arg:theFunc->args()){
        NamedValues[arg.getName()]=&arg;
    }

    if (Value *retVal = body->codegen())
    {
        builder.CreateRet(retVal);
        verifyFunction(*theFunc);
        funcPassManager->run(*theFunc);
        verifyFunction(*theFunc);
        return theFunc;
    }

    theFunc->eraseFromParent();
    return nullptr;
}

static void initModuleAndPassManager()
{
    module = std::make_unique<Module>("toy", context);
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

static void driver()
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
    initModuleAndPassManager();
    driver();
    module->print(errs(), nullptr);
    return 0;
}
