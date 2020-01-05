#include "llvm/IR/Verifier.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include <cctype>
#include <cstdio>
#include <map>
#include <string>
#include <sstream>
#include <vector>
#include <iostream>

using namespace llvm;

enum Token_Type {
    EOF_TOKEN = 0,
    DEF_TOKEN,
    IDENTIFIER_TOKEN,
    NUMERIC_TOKEN,
    RETURN_TOKEN
};

FILE *file;
static std::string Identifier_string;
static int Numeric_Val;

static int get_token() {
    static int LastChar = ' ';
    while (isspace(LastChar)) {
        LastChar = fgetc(file);
    }


    if (isalpha(LastChar)) {
        Identifier_string = LastChar;
        while (isalnum((LastChar = fgetc(file))))
            Identifier_string += LastChar;

        if (Identifier_string == "def") return DEF_TOKEN;
        if (Identifier_string == "return") return RETURN_TOKEN;

        return IDENTIFIER_TOKEN;
    }

    if (isdigit(LastChar)) {
        std::string NumStr;
        do {
            NumStr += LastChar;
            LastChar = fgetc(file);
        } while (isdigit(LastChar));

        Numeric_Val = strtod(NumStr.c_str(), 0);
        return NUMERIC_TOKEN;
    }

    if (LastChar == '#') {
        do LastChar = fgetc(file);
        while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

        if (LastChar != EOF) return get_token();
    }

    if (LastChar == EOF) return EOF_TOKEN;

    int ThisChar = LastChar;
    LastChar = fgetc(file);
    return ThisChar;
}

namespace {

    class BaseAST {
    public:
        virtual ~BaseAST() {}

        virtual Value *Codegen() = 0;
    };

    class NumericAST : public BaseAST {
        float numeric_val;
    public :
        NumericAST(float val) : numeric_val(val) {}

        Value *Codegen() override;
    };

    class VariableAST : public BaseAST {
        std::string Var_Name;
    public:
        VariableAST(const std::string &name) : Var_Name(name) {}

        Value *Codegen() override;
    };

    class BinaryAST : public BaseAST {
        char Bin_Operator;
        BaseAST *LHS, *RHS;
    public:
        BinaryAST(char op, BaseAST *lhs, BaseAST *rhs)
                : Bin_Operator(op), LHS(lhs), RHS(rhs) {}

        Value *Codegen() override;
    };

    class FunctionCallAST : public BaseAST {
        std::string Function_Callee;
        std::vector<BaseAST *> Function_Arguments;
    public:
        FunctionCallAST(const std::string &callee, std::vector<BaseAST *> &args) :
                Function_Callee(callee), Function_Arguments(args) {}

        Value *Codegen() override;
    };

    class FunctionDeclAST {
        std::string Func_Name;
        std::vector<std::string> Arguments;
    public:
        FunctionDeclAST(const std::string &name, const std::vector<std::string> &args) :
                Func_Name(name), Arguments(args) {};

        Function *Codegen();
    };

    class FunctionDefnAST {
        FunctionDeclAST *Func_Decl;
        BaseAST *Body;
    public:
        FunctionDefnAST(FunctionDeclAST *proto, BaseAST *body) :
                Func_Decl(proto), Body(body) {}

        Function *Codegen();
    };
}

static int Current_token;

static int next_token() {
    return Current_token = get_token();
}

static std::map<char, int> Operator_Precedence;

static int getBinOpPrecedence() {
    if (!isascii(Current_token))
        return -1;

    int TokPrec = Operator_Precedence[Current_token];
    if (TokPrec <= 0) return -1;
    return TokPrec;
}

static BaseAST *expression_parser();

static BaseAST *identifier_parser() {
    std::string IdName = Identifier_string;
    next_token();
    if (Current_token != '(')
        return new VariableAST(IdName);

    next_token();

    std::vector<BaseAST *> Args;
    if (Current_token != ')') {
        while (1) {
            BaseAST *Arg = expression_parser();
            if (!Arg) return 0;
            Args.push_back(Arg);

            if (Current_token == ')') break;

            if (Current_token != ',')
                return 0;
            next_token();
        }
    }
    next_token();

    return new FunctionCallAST(IdName, Args);
}

static BaseAST *numeric_parser() {
    BaseAST *Result = new NumericAST(Numeric_Val);
    next_token();
    return Result;
}

static BaseAST *paran_parser() {
    next_token();
    BaseAST *V = expression_parser();
    if (!V) return 0;

    if (Current_token != ')')
        return 0;
    return V;
}

static BaseAST *Base_Parser() {
    switch (Current_token) {
        default:
            return 0;
        case IDENTIFIER_TOKEN :
            return identifier_parser();
        case NUMERIC_TOKEN :
            return numeric_parser();
        case '(' :
            return paran_parser();
    }
}

static BaseAST *binary_op_parser(int Old_Prec, BaseAST *LHS) {
    while (1) {
        if (Current_token == ';') return LHS;
        int Operator_Prec = getBinOpPrecedence();

        if (Operator_Prec < Old_Prec)
            return LHS;

        char BinOp = Current_token;
        next_token();

        BaseAST *RHS = Base_Parser();
        if (!RHS) return 0;

        int Next_Prec = getBinOpPrecedence();
        if (Operator_Prec < Next_Prec) {
            RHS = binary_op_parser(Operator_Prec + 1, RHS);
            if (RHS == 0) return 0;
        }

        LHS = new BinaryAST(BinOp, LHS, RHS);
    }
}

static BaseAST *expression_parser() {
    BaseAST *LHS = Base_Parser();
    if (!LHS) return 0;
    return binary_op_parser(0, LHS);
}

static FunctionDeclAST *func_decl_parser() {
    if (Current_token != IDENTIFIER_TOKEN)
        return 0;

    std::string FnName = Identifier_string;
    next_token();

    if (Current_token != '(')
        return 0;

    std::vector<std::string> Function_Argument_Names;
    while (next_token() == IDENTIFIER_TOKEN || Current_token == ',') {
        if (Current_token != ',') {
            Function_Argument_Names.push_back(Identifier_string);
        }
    }

    if (Current_token != ')')
        return 0;
    next_token();
    return new FunctionDeclAST(FnName, Function_Argument_Names);
}

static FunctionDefnAST *func_defn_parser() {
    next_token();
    FunctionDeclAST *Decl = func_decl_parser();
    if (Decl == 0) return 0;
    if (BaseAST *Body = expression_parser()) {
        return new FunctionDefnAST(Decl, Body);
    }

    return 0;
}

static FunctionDefnAST *top_level_parser() {
    if (BaseAST *E = expression_parser()) {
        FunctionDeclAST *Func_Decl = new FunctionDeclAST("_anon_expr", std::vector<std::string>());
        return new FunctionDefnAST(Func_Decl, E);
    }
    return 0;
}

static void init_precedence() {
    Operator_Precedence['-'] = 1;
    Operator_Precedence['+'] = 2;
    Operator_Precedence['/'] = 3;
    Operator_Precedence['*'] = 4;
}

//codegen

static LLVMContext context;
static IRBuilder<> builder(context);
static std::unique_ptr<Module> module;
static std::map<std::string, Value *> NamedValues;

Value *NumericAST::Codegen() {
    return ConstantFP::get(context, APFloat(numeric_val));
}

Value *VariableAST::Codegen() {
    Value *v = NamedValues[Var_Name];
    if (!v) return 0;
    return v;
}

Value *BinaryAST::Codegen() {
    Value *L = LHS->Codegen();
    Value *R = RHS->Codegen();
    if (L == 0 || R == 0) return 0;

    switch (Bin_Operator) {
        case '+' :
            return builder.CreateAdd(L, R, "addtmp");
        case '-' :
            return builder.CreateSub(L, R, "subtmp");
        case '*' :
            return builder.CreateMul(L, R, "multmp");
        case '/' :
            return builder.CreateUDiv(L, R, "divtmp");
        default :
            return 0;
    }
}

Value *FunctionCallAST::Codegen() {
    Function *CalleeF = module->getFunction(Function_Callee);

    std::vector<Value *> ArgsV;
    for (unsigned i = 0, e = Function_Arguments.size(); i != e; ++i) {
        ArgsV.push_back(Function_Arguments[i]->Codegen());
        if (ArgsV.back() == 0) return 0;
    }

    return builder.CreateCall(CalleeF, ArgsV, "calltmp");
}

Function *FunctionDeclAST::Codegen() {
    std::vector<Type *> Integers(Arguments.size(), Type::getInt32Ty(context));
    FunctionType *FT = FunctionType::get(Type::getInt32Ty(context), Integers, false);
    Function *F = Function::Create(FT, Function::ExternalLinkage, Func_Name, module.get());

    if (F->getName() != Func_Name) {
        F->eraseFromParent();
        F = module->getFunction(Func_Name);

        if (!F->empty()) return 0;

        if (F->arg_size() != Arguments.size()) return 0;

    }

    unsigned Idx = 0;
    for (Function::arg_iterator Arg_It = F->arg_begin(); Idx != Arguments.size(); ++Arg_It, ++Idx) {
        Arg_It->setName(Arguments[Idx]);
        NamedValues[Arguments[Idx]] = Arg_It;
    }

    return F;

}

Function *FunctionDefnAST::Codegen() {
    NamedValues.clear();

    Function *TheFunction = Func_Decl->Codegen();
    if (TheFunction == 0) return 0;

    BasicBlock *BB = BasicBlock::Create(context, "entry", TheFunction);
    builder.SetInsertPoint(BB);

    if (Value *RetVal = Body->Codegen()) {
        builder.CreateRet(RetVal);
        verifyFunction(*TheFunction);
        return TheFunction;
    }

    TheFunction->eraseFromParent();
    return 0;
}


static void HandleDefn() {
    if (FunctionDefnAST *F = func_defn_parser()) {
        if (Function *LF = F->Codegen()) {
            //LF->print(errs());
        }
    } else {
        next_token();
    }
}

static void HandleTopExpression() {
    if (FunctionDefnAST *F = top_level_parser()) {

        if (Function *LF = F->Codegen()) {
            //LF->print(errs());
        }
    } else {
        next_token();
    }
}

static void Driver() {
    while (1) {
        switch (Current_token) {
            case EOF_TOKEN :
                return;
            case ';' :
                next_token();
                break;
            case DEF_TOKEN :
                HandleDefn();
                break;
            default :
                HandleTopExpression();
                break;
        }
    }
}

int main(int argc, char *argv[]) {
    init_precedence();
    char* filename = argv[1];
    file = fopen(filename, "r");
    if (file == 0) {
        printf("Could not open file\n");
    }
    next_token();
    module = std::make_unique<Module>(filename,context);
    Driver();
    module->print(errs(), nullptr);
    return 0;
}
	
    

  
  
	
	
	  
