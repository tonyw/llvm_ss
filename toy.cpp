#include "llvm/Transforms/Scalar/GVN.h"
#include <cctype>
#include <cstdio>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "toy.h"

using namespace llvm;
using namespace toy;

FILE* file;
static std::string identifierString;
static int numericVal;

/// LogError* - These are little helper functions for error handling.
std::unique_ptr<ExprAST> LogError(const char* Str)
{
    fprintf(stderr, "Error: %s\n", Str);
    return nullptr;
}
std::unique_ptr<PrototypeAST> LogErrorP(const char* Str)
{
    LogError(Str);
    return nullptr;
}

static int getToken()
{
    static int lastChar = ' ';
    while (isspace(lastChar)) {
        lastChar = fgetc(file);
    }

    if (isalpha(lastChar)) {
        identifierString = lastChar;
        while (isalnum((lastChar = fgetc(file)))) {
            identifierString += lastChar;
        }
        if (identifierString == "def") {
            return DEF_TOKEN;
        } else if (identifierString == "extern") {
            return EXTERN_TOKEN;
        } else if (identifierString == "if") {
            return IF_TOKEN;
        } else if (identifierString == "then") {
            return THEN_TOKEN;
        } else if (identifierString == "else") {
            return ELSE_TOKEN;
        } else {
            return IDENTIFIER_TOKEN;
        }
    }

    if (isdigit(lastChar)) {
        std::string numStr;
        do {
            numStr += lastChar;
            lastChar = fgetc(file);
        } while (isdigit(lastChar));

        numericVal = strtod(numStr.c_str(), 0);
        return NUMERIC_TOKEN;
    }

    if (lastChar == '#') {
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
    if (currentToken != ')') {
        while (true) {
            auto arg = parseExpression();
            if (!arg)
                return nullptr;
            args.push_back(std::move(arg));

            if (currentToken == ')')
                break;

            if (currentToken != ',')
                return LogError("Expected ')' or ',' in argument list");
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
    if (!V) {
        return nullptr;
    }
    if (currentToken != ')') {
        return nullptr;
    }
    nextToken();
    return V;
}

static std::unique_ptr<ExprAST> parseIfExpr()
{
    nextToken();
    auto Cond = parseExpression();
    if (!Cond) {
        return nullptr;
    }
    printf("the cur tok is %c \n", currentToken);
    if (currentToken != THEN_TOKEN)
        return LogError("expected then");
    nextToken(); // eat the then

    auto Then = parseExpression();
    if (!Then)
        return nullptr;

    if (currentToken != ELSE_TOKEN)
        return LogError("expected else");

    nextToken();

    auto Else = parseExpression();
    if (!Else)
        return nullptr;

    return std::make_unique<IfExprAST>(std::move(Cond), std::move(Then),
        std::move(Else));
}

static std::unique_ptr<ExprAST> parsePrimary()
{
    switch (currentToken) {
    default:
        return LogError("unknown token when expecting an expression");
    case IDENTIFIER_TOKEN:
        return parseIdentifierExpr();
    case NUMERIC_TOKEN:
        return parseNumericExpr();
    case '(':
        return parseParenExpr();
    case IF_TOKEN:
        return parseIfExpr();
    }
}

static std::unique_ptr<ExprAST> parseBinOp(int exprPrec, std::unique_ptr<ExprAST> LHS)
{
    while (1) {
        if (currentToken == ';') {
            return LHS;
        }
        int opPrec = getBinOpPrecedence();
        if (opPrec < exprPrec) {
            return LHS;
        }
        char BinOp = currentToken;
        nextToken();
        std::unique_ptr<ExprAST> RHS = parsePrimary();
        if (!RHS)
            return nullptr;

        int nextPrec = getBinOpPrecedence();
        if (opPrec < nextPrec) {
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
    if (!LHS) {
        return nullptr;
    }
    return parseBinOp(0, std::move(LHS));
}

static std::unique_ptr<PrototypeAST> parsePrototype()
{
    if (currentToken != IDENTIFIER_TOKEN) {
        return LogErrorP("Expected function name in prototype");
    }
    std::string FnName = identifierString;
    nextToken();
    if (currentToken != '(') {
        return LogErrorP("Expected '(' in prototype");
    }

    std::vector<std::string> Function_Argument_Names;
    while (nextToken() == IDENTIFIER_TOKEN || currentToken == ',') {
        if (currentToken != ',') {
            Function_Argument_Names.push_back(identifierString);
        }
    }
    if (currentToken != ')') {
        return LogErrorP("Expected ')' in prototype");
    }
    nextToken();
    return std::make_unique<PrototypeAST>(FnName, std::move(Function_Argument_Names));
}

static std::unique_ptr<FunctionAST> parseFunctionDef()
{
    nextToken();
    std::unique_ptr<PrototypeAST> proto = parsePrototype();
    if (proto == 0) {
        return 0;
    }
    if (std::unique_ptr<ExprAST> body = parseExpression()) {
        return std::make_unique<FunctionAST>(std::move(proto), std::move(body));
    }
    return 0;
}

static std::unique_ptr<PrototypeAST> parseExtern()
{
    nextToken(); // eat extern.
    return parsePrototype();
}

static std::unique_ptr<FunctionAST> parseTopLevelExpr()
{
    if (std::unique_ptr<ExprAST> E = parseExpression()) {
        std::unique_ptr<PrototypeAST> Func_Decl = std::make_unique<PrototypeAST>("_anon_expr", std::vector<std::string>());
        return std::make_unique<FunctionAST>(std::move(Func_Decl), std::move(E));
    }
    return 0;
}

static void initPrecedence()
{
    opPrecedence['<'] = 2;
    opPrecedence['>'] = 3;
    opPrecedence['+'] = 5;
    opPrecedence['-'] = 6;
    opPrecedence['*'] = 7;
    opPrecedence['/'] = 8;
}
static void handleFunctionDef()
{
    if (std::unique_ptr<FunctionAST> F = parseFunctionDef()) {
        if (auto LF = F->codegen()) {
            //LF->print(errs());
        }
    } else {
        nextToken();
    }
}

static void handleExtern()
{
    if (parseExtern()) {
        //fprintf(stderr, "Parsed an extern\n");
    } else {
        // Skip token for error recovery.
        nextToken();
    }
}

static void handleTopExpression()
{
    if (std::unique_ptr<FunctionAST> F = parseTopLevelExpr()) {

        if (auto LF = F->codegen()) {
            //LF->print(errs());
        }
    } else {
        nextToken();
    }
}

static void driver()
{
    while (1) {
        switch (currentToken) {
        case EOF_TOKEN:
            return;
        case ';':
            nextToken();
            break;
        case DEF_TOKEN:
            handleFunctionDef();
            break;
        case EXTERN_TOKEN:
            handleExtern();
            break;
        default:
            handleTopExpression();
            break;
        }
    }
}

int main(int argc, char* argv[])
{
    initPrecedence();
    char* filename = argv[1];
    file = fopen(filename, "r");
    if (file == 0) {
        printf("Could not open file\n");
    }
    nextToken();
    driver();
    Global::global()->module->print(errs(), nullptr);
    return 0;
}
