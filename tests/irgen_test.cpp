#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>

#define TEST_TYPE(name, src, expectedType) TEST(IrgenTest, name){       \
    Parser *parser = NewParser((char *)src, Lex((char *)src));          \
    Exp *e = ParseType(parser);                                         \
    Irgen *irgen = NewIrgen();                                          \
    LLVMTypeRef type = CompileType(e);                                  \
    ASSERT_TRUE(type == expectedType);                                  \
}                                                                       \

TEST_TYPE(CompileTypeInt, "int", LLVMInt64Type())
TEST_TYPE(CompileTypeI64, "i64", LLVMInt64Type())
TEST_TYPE(CompileTypeI32, "i32", LLVMInt32Type())
TEST_TYPE(CompileTypeI16, "i16", LLVMInt16Type())
TEST_TYPE(CompileTypeI8, "i8", LLVMInt8Type())

TEST_TYPE(CompileTypeFloat, "float", LLVMFloatType())
TEST_TYPE(CompileTypeF64, "f64", LLVMDoubleType())
TEST_TYPE(CompileTypeF32, "f32", LLVMFloatType())

TEST_TYPE(CompileTypeIntArray, "int[3]", LLVMArrayType(LLVMInt64Type(), 3))
TEST_TYPE(CompileTypeFloatArray, "float[100]", LLVMArrayType(LLVMFloatType(), 100))

#define TEST_LITERAL(name, src, expectedType, expectedValue) TEST(IrgenTest, name) {    \
    Parser *parser = NewParser((char *)src, Lex((char *)src));                          \
    Exp *e = ParseExpression(parser, 0);                                                \
    Irgen *irgen = NewIrgen();                                                          \
    LLVMValueRef value = CompileLiteralExp(irgen, e);                                   \
    ASSERT_TRUE(LLVMIsConstant(value));                                                 \
    ASSERT_STREQ(expectedValue, LLVMPrintValueToString(value));                         \
}                                                                                       \

TEST_LITERAL(CompileLiteralInt, "123", LLVMInt64Type(), "i64 123")
TEST_LITERAL(CompileLiteralFloat, "123.321", LLVMFloatType(), "float 0x405ED48B40000000")
TEST_LITERAL(CompileLiteralHex, "0x1000", LLVMInt64Type(), "i64 4096")
TEST_LITERAL(CompileLiteralOctal, "0123", LLVMInt64Type(), "i64 83")

#define TEST_CAST(name, value, cast) TEST(IrgenTest, name) {                        \
    Irgen *irgen = NewIrgen();                                                      \
    irgen->function = LLVMAddFunction(irgen->module, "test", LLVMInt64Type());      \
    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(irgen->function, "entry");       \
    irgen->builder = LLVMCreateBuilder();                                           \
    LLVMPositionBuilderAtEnd(irgen->builder, entry);                                \
    LLVMValueRef castValue = Cast(irgen, value, cast);                              \
    ASSERT_EQ(LLVMTypeOf(castValue), cast);                                         \
}                                                                                   \

TEST_CAST(I64ToI32, LLVMConstIntOfString(LLVMInt64Type(), "123", 10), LLVMInt32Type())
TEST_CAST(I16ToFloat, LLVMConstIntOfString(LLVMInt16Type(), "32", 10), LLVMFloatType())
TEST_CAST(FloatToDouble, LLVMConstRealOfString(LLVMFloatType(), "1.42"), LLVMDoubleType())
TEST_CAST(DoubleToI64, LLVMConstRealOfString(LLVMDoubleType(), "1245.12"), LLVMInt64Type())

LLVMGenericValueRef intArg(int n) {
    return LLVMCreateGenericValueOfInt(LLVMInt64Type(), n, false);
}

LLVMGenericValueRef runLLVMFunction(
    Irgen *irgen, 
    LLVMValueRef function, 
    int paramCount, 
    LLVMGenericValueRef *params) {

    // create an execution engine   
    LLVMExecutionEngineRef engine;
    char *error = (char *)NULL;

    # if 0
        // initialize jit
        LLVMLinkInMCJIT();
    # else
        // Initialize intepreter
        LLVMLinkInInterpreter();
    # endif

    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();
    LLVMInitializeNativeAsmParser();

    assert(LLVMCreateExecutionEngineForModule(&engine, irgen->module, &error) == 0);
    assert(error == NULL);

    LLVMGenericValueRef res = LLVMRunFunction(engine, function, paramCount, params);

    LLVMDisposeExecutionEngine(engine);
    return res;
}

#define _TEST_FUNC(src, params, paramCount, out) {                                      \
    /* generate function */                                                             \
    Parser *parser = NewParser(src, Lex(src));                                          \
    Dcl *d = ParseFunction(parser);                                                     \
    Irgen *irgen = NewIrgen();                                                          \
    LLVMValueRef function = CompileFunction(irgen, d);                                  \
                                                                                        \
    /* LLVMDumpModule(irgen->module); */                                                \
                                                                                        \
    /* check for errors in module */                                                    \
    char *error = (char *)NULL;                                                         \
    LLVMVerifyModule(irgen->module, LLVMPrintMessageAction, &error);                    \
    LLVMDisposeMessage(error);                                                          \
                                                                                        \
    /* run the function */                                                              \
    LLVMGenericValueRef res = runLLVMFunction(irgen, function, paramCount, params);     \
    ASSERT_EQ((int)LLVMGenericValueToInt(res, 0), out);                                 \
                                                                                        \
    /* dispose of builder */                                                            \
    LLVMDisposeBuilder(irgen->builder);                                                 \
}                                                                                       \

#define TEST_FUNC_0(name, src, out) TEST(IrgenTest, name) {                     \
    LLVMGenericValueRef *params = NULL;                                         \
    char *source = (char *)src;                                                         \
    _TEST_FUNC(source, params, 0, out)                                          \
}                                                                               \

#define TEST_FUNC_1(name, src, param1, out) TEST(IrgenTest, name) {             \
    LLVMGenericValueRef params[1] = { param1 };                                 \
    char *source = (char *)src;                                                 \
    _TEST_FUNC(source, params, 1, out)                                          \
}                                                                               \

#define TEST_FUNC_2(name, src, param1, param2, out) TEST(IrgenTest, name) {     \
    LLVMGenericValueRef params[2] = { param1, param2 };                         \
    char *source = (char *)src;                                                 \
    _TEST_FUNC(source, params, 2, out)                                          \
}                                                                               \

char *loadTest(std::string name) {
    // build path to the file
    char *cname = (char *)name.c_str();
    char *pre = (char *)"../tests/tests/";
    int pathLen = strlen(pre) + strlen(cname) + 1;
    char *path = alloca(pathLen);
    sprintf(path, "%s%s", pre, cname);
    // printf("Path: %s\n", path);
    
    // open file
    FILE *f = fopen(path, "rb");
    assert(f != NULL);

    // get the length of the file
    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);

    // read file into buffer
    char *buffer = (char *)malloc(length + 1);
    fread(buffer, 1, length, f);
    fclose(f);
    buffer[length] = '\0';
    
    return buffer;
}

TEST_FUNC_0(CompileFunctionLiteral, loadTest("literal.fur"), 123)
TEST_FUNC_0(CompileFunctionBinaryInt, loadTest("binaryInt.fur"), 123)
TEST_FUNC_0(CompileFunctionBinaryFloat, loadTest("binaryFloat.fur"), 123)
TEST_FUNC_0(CompileFunctionLongVar, loadTest("longVar.fur"), 123)
TEST_FUNC_0(CompileFunctionShortVar, loadTest("shortVar.fur"), 123)
TEST_FUNC_0(CompileFunctionIf, loadTest("if.fur"), 123)
TEST_FUNC_0(CompileFunctionIfElse, loadTest("ifElse.fur"), 123)
TEST_FUNC_0(CompileFunctionIfElseIfElse, loadTest("ifElseIfElse.fur"), 123)
TEST_FUNC_0(CompileFunctionIfElseIfElseIfElse, loadTest("ifElseIfElseIfElse.fur"), 123)
TEST_FUNC_0(CompileFunctionFor, loadTest("for.fur"), 123)
TEST_FUNC_0(CompileFunctionArrayInit, loadTest("arrayInit.fur"), 123);
TEST_FUNC_2(CompileFunctionAdd, loadTest("add.fur"), intArg(100), intArg(23), 123)
TEST_FUNC_1(CompileFunctionUnary, loadTest("unary.fur"), intArg(-123), 123) 
TEST_FUNC_1(CompileFunctionReassignArg, loadTest("reassignArg.fur"), intArg(321), 123)

TEST(IrgenTest, CallTest) {
    const char *src = "proc add :: int a, int b -> int { return a + b }\n"
                "proc test :: -> int { return add(120, 3) }";

    Parser *parser = NewParser((char *)src, Lex((char *)src));
    Dcl *addDcl = ParseFunction(parser);
    Dcl *testDcl = ParseFunction(parser);
    
    Irgen *irgen = NewIrgen();
    LLVMValueRef addFunction = CompileFunction(irgen, addDcl);
    LLVMValueRef testFunction = CompileFunction(irgen, testDcl);

    // LLVMDumpModule(irgen->module);

    // check for errors in module
    char *error = (char *)NULL;
    LLVMVerifyModule(irgen->module, LLVMPrintMessageAction, &error);
    LLVMDisposeMessage(error);

    // run the function
    LLVMGenericValueRef res = runLLVMFunction(irgen, testFunction, 0, (LLVMGenericValueRef *)NULL);
    ASSERT_EQ((int)LLVMGenericValueToInt(res, 0), 123);

    // dispose of builder
    LLVMDisposeBuilder(irgen->builder);
}

