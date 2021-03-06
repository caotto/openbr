#include <QMetaProperty>
#include <llvm/Intrinsics.h>
#include <llvm/LLVMContext.h>
#include <llvm/DerivedTypes.h>
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/IRBuilder.h>
#include <llvm/Module.h>
#include <llvm/Operator.h>
#include <llvm/PassManager.h>
#include <llvm/Type.h>
#include <llvm/Value.h>
#include <llvm/Analysis/Passes.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/Assembly/PrintModulePass.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Transforms/Scalar.h>
#include <openbr_plugin.h>

#include "core/opencvutils.h"
#include "jitcv/jitcv.h"

using namespace br;
using namespace cv;
using namespace jitcv;
using namespace llvm;

static Module *TheModule = NULL;
static ExecutionEngine *TheExecutionEngine = NULL;
static FunctionPassManager *TheFunctionPassManager = NULL;
static FunctionPassManager *TheExtraFunctionPassManager = NULL;
static StructType *TheMatrixStruct = NULL;

static QString MatrixToString(const Matrix &m)
{
    return QString("%1%2%3%4%5%6%7").arg(QString::number(m.bits()), (m.isSigned() ? "s" : "u"), (m.isFloating() ? "f" : "i"),
                                         QString::number(m.singleChannel()), QString::number(m.singleColumn()), QString::number(m.singleRow()), QString::number(m.singleFrame()));
}

static Matrix MatrixFromMat(const cv::Mat &mat)
{
    Matrix m;

    if (!mat.isContinuous()) qFatal("Matrix requires continuous data.");
    m.channels = mat.channels();
    m.columns = mat.cols;
    m.rows = mat.rows;
    m.frames = 1;

    switch (mat.depth()) {
      case CV_8U:  m.hash = Matrix::u8;  break;
      case CV_8S:  m.hash = Matrix::s8;  break;
      case CV_16U: m.hash = Matrix::u16; break;
      case CV_16S: m.hash = Matrix::s16; break;
      case CV_32S: m.hash = Matrix::s32; break;
      case CV_32F: m.hash = Matrix::f32; break;
      case CV_64F: m.hash = Matrix::f64; break;
      default:     qFatal("Unrecognized matrix depth.");
    }

    m.data = mat.data;
    return m;
}

static void AllocateMatrixFromMat(Matrix &m, cv::Mat &mat)
{
    int cvType = -1;
    switch (m.type()) {
      case Matrix::u8:  cvType = CV_8U; break;
      case Matrix::s8:  cvType = CV_8S; break;
      case Matrix::u16: cvType = CV_16U; break;
      case Matrix::s16: cvType = CV_16S; break;
      case Matrix::s32: cvType = CV_32S; break;
      case Matrix::f32: cvType = CV_32F; break;
      case Matrix::f64: cvType = CV_64F; break;
      default:          qFatal("OpenCV does not support Matrix format: %s", qPrintable(MatrixToString(m)));
    }

    m.deallocate();
    mat = Mat(m.rows, m.columns, CV_MAKETYPE(cvType, m.channels));
    m.data = mat.data;
}

QDebug operator<<(QDebug dbg, const Matrix &m)
{
    dbg.nospace() << MatrixToString(m);
    return dbg;
}

struct MatrixBuilder : public Matrix
{
    Value *m;
    IRBuilder<> *b;
    Function *f;
    Twine name;

    MatrixBuilder(const Matrix &matrix, Value *value, IRBuilder<> *builder, Function *function, const Twine &name_)
        : Matrix(matrix), m(value), b(builder), f(function), name(name_) {}

    static Constant *zero() { return constant(0); }
    static Constant *one() { return constant(1); }
    static Constant *constant(int value, int bits = 32) { return Constant::getIntegerValue(Type::getInt32Ty(getGlobalContext()), APInt(bits, value)); }
    static Constant *constant(float value) { return ConstantFP::get(Type::getFloatTy(getGlobalContext()), value == 0 ? -0.0f : value); }
    static Constant *constant(double value) { return ConstantFP::get(Type::getDoubleTy(getGlobalContext()), value == 0 ? -0.0 : value); }
    Constant *autoConstant(double value) const { return isFloating() ? ((bits() == 64) ? constant(value) : constant(float(value))) : constant(int(value), bits()); }
    AllocaInst *autoAlloca(double value, const Twine &name = "") const { AllocaInst *alloca = b->CreateAlloca(ty(), 0, name); b->CreateStore(autoConstant(value), alloca); return alloca; }

    Value *getData(bool cast = true) const { LoadInst *data = b->CreateLoad(b->CreateStructGEP(m, 0), name+"_data"); return cast ? b->CreatePointerCast(data, ptrTy()) : data; }
    Value *getChannels() const { return singleChannel() ? static_cast<Value*>(one()) : static_cast<Value*>(b->CreateLoad(b->CreateStructGEP(m, 1), name+"_channels")); }
    Value *getColumns() const { return singleColumn() ? static_cast<Value*>(one()) : static_cast<Value*>(b->CreateLoad(b->CreateStructGEP(m, 2), name+"_columns")); }
    Value *getRows() const { return singleRow() ? static_cast<Value*>(one()) : static_cast<Value*>(b->CreateLoad(b->CreateStructGEP(m, 3), name+"_rows")); }
    Value *getFrames() const { return singleFrame() ? static_cast<Value*>(one()) : static_cast<Value*>(b->CreateLoad(b->CreateStructGEP(m, 4), name+"_frames")); }
    Value *getHash() const { return b->CreateLoad(b->CreateStructGEP(m, 5), name+"_hash"); }

    void setData(Value *value) const { b->CreateStore(value, b->CreateStructGEP(m, 0)); }
    void setChannels(Value *value) const { b->CreateStore(value, b->CreateStructGEP(m, 1)); }
    void setColumns(Value *value) const { b->CreateStore(value, b->CreateStructGEP(m, 2)); }
    void setRows(Value *value) const { b->CreateStore(value, b->CreateStructGEP(m, 3)); }
    void setFrames(Value *value) const { b->CreateStore(value, b->CreateStructGEP(m, 4)); }
    void setHash(Value *value) const { b->CreateStore(value, b->CreateStructGEP(m, 5)); }

    void copyHeaderCode(const MatrixBuilder &other) const {
        setChannels(other.getChannels());
        setRows(other.getRows());
        setFrames(other.getFrames());
        setHash(other.getHash());
    }

    void allocateCode() const {
        Function *malloc = TheModule->getFunction("malloc");
        if (!malloc) {
            PointerType *mallocReturn = Type::getInt8PtrTy(getGlobalContext());
            std::vector<Type*> mallocParams;
            mallocParams.push_back(Type::getInt32Ty(getGlobalContext()));
            FunctionType* mallocType = FunctionType::get(mallocReturn, mallocParams, false);
            malloc = Function::Create(mallocType, GlobalValue::ExternalLinkage, "malloc", TheModule);
            malloc->setCallingConv(CallingConv::C);
        }

        std::vector<Value*> mallocArgs;
        mallocArgs.push_back(bytesCode());
        setData(b->CreateCall(malloc, mallocArgs));
    }

    Value *get(int mask) const { return b->CreateAnd(getHash(), constant(mask, 16)); }
    void set(int value, int mask) const { setHash(b->CreateOr(b->CreateAnd(getHash(), constant(~mask, 16)), b->CreateAnd(constant(value, 16), constant(mask, 16)))); }
    void setBit(bool on, int mask) const { on ? setHash(b->CreateOr(getHash(), constant(mask, 16))) : setHash(b->CreateAnd(getHash(), constant(~mask, 16))); }

    Value *bitsCode() const { return get(Bits); }
    void setBitsCode(int bits) const { set(bits, Bits); }
    Value *isFloatingCode() const { return get(Floating); }
    void setFloatingCode(bool isFloating) const { if (isFloating) setSignedCode(true); setBit(isFloating, Floating); }
    Value *isSignedCode() const { return get(Signed); }
    void setSignedCode(bool isSigned) const { setBit(isSigned, Signed); }
    Value *typeCode() const { return get(Bits + Floating + Signed); }
    void setTypeCode(int type) const { set(type, Bits + Floating + Signed); }
    Value *singleChannelCode() const { return get(SingleChannel); }
    void setSingleChannelCode(bool singleChannel) const { setBit(singleChannel, SingleChannel); }
    Value *singleColumnCode() const { return get(SingleColumn); }
    void setSingleColumnCode(bool singleColumn) { setBit(singleColumn, SingleColumn); }
    Value *singleRowCode() const { return get(SingleRow); }
    void setSingleRowCode(bool singleRow) const { setBit(singleRow, SingleRow); }
    Value *singleFrameCode() const { return get(SingleFrame); }
    void setSingleFrameCode(bool singleFrame) const { setBit(singleFrame, SingleFrame); }
    Value *elementsCode() const { return b->CreateMul(b->CreateMul(b->CreateMul(getChannels(), getColumns()), getRows()), getFrames()); }
    Value *bytesCode() const { return b->CreateMul(b->CreateUDiv(b->CreateCast(Instruction::ZExt, bitsCode(), Type::getInt32Ty(getGlobalContext())), constant(8, 32)), elementsCode()); }

    Value *columnStep() const { Value *columnStep = getChannels(); columnStep->setName(name+"_cStep"); return columnStep; }
    Value *rowStep() const { return b->CreateMul(getColumns(), columnStep(), name+"_rStep"); }
    Value *frameStep() const { return b->CreateMul(getRows(), rowStep(), name+"_tStep"); }
    Value *aliasColumnStep(const MatrixBuilder &other) const { return (channels == other.channels) ? other.columnStep() : columnStep(); }
    Value *aliasRowStep(const MatrixBuilder &other) const { return (columns == other.columns) ? other.rowStep() : rowStep(); }
    Value *aliasFrameStep(const MatrixBuilder &other) const { return (rows == other.rows) ? other.frameStep() : frameStep(); }

    Value *index(Value *c) const { return singleChannel() ? constant(0) : c; }
    Value *index(Value *c, Value *x) const { return singleColumn() ? index(c) : b->CreateAdd(b->CreateMul(x, columnStep()), index(c)); }
    Value *index(Value *c, Value *x, Value *y) const { return singleRow() ? index(c, x) : b->CreateAdd(b->CreateMul(y, rowStep()), index(c, x)); }
    Value *index(Value *c, Value *x, Value *y, Value *f) const { return singleFrame() ? index(c, x, y) : b->CreateAdd(b->CreateMul(f, frameStep()), index(c, x, y)); }
    Value *aliasIndex(const MatrixBuilder &other, Value *c, Value *x) const { return singleColumn() ? index(c) : b->CreateAdd(b->CreateMul(x, aliasColumnStep(other)), index(c)); }
    Value *aliasIndex(const MatrixBuilder &other, Value *c, Value *x, Value *y) const { return singleRow() ? aliasIndex(other, c, x) : b->CreateAdd(b->CreateMul(y, aliasRowStep(other)), aliasIndex(other, c, x)); }
    Value *aliasIndex(const MatrixBuilder &other, Value *c, Value *x, Value *y, Value *f) const { return singleFrame() ? aliasIndex(other, c, x, y) : b->CreateAdd(b->CreateMul(f, aliasFrameStep(other)), aliasIndex(other, c, x, y)); }

    void deindex(Value *i, Value **c) const { *c = singleChannel() ? constant(0) : i; }
    void deindex(Value *i, Value **c, Value **x) const {
        Value *rem;
        if (singleColumn()) {
            rem = i;
            *x = constant(0);
        } else {
            Value *step = columnStep();
            rem = b->CreateURem(i, step, name+"_xRem");
            *x = b->CreateExactUDiv(b->CreateSub(i, rem), step, name+"_x");
        }
        deindex(rem, c);
    }
    void deindex(Value *i, Value **c, Value **x, Value **y) const {
        Value *rem;
        if (singleRow()) {
            rem = i;
            *y = constant(0);
        } else {
            Value *step = rowStep();
            rem = b->CreateURem(i, step, name+"_yRem");
            *y = b->CreateExactUDiv(b->CreateSub(i, rem), step, name+"_y");
        }
        deindex(rem, c, x);
    }
    void deindex(Value *i, Value **c, Value **x, Value **y, Value **t) const {
        Value *rem;
        if (singleFrame()) {
            rem = i;
            *t = constant(0);
        } else {
            Value *step = frameStep();
            rem = b->CreateURem(i, step, name+"_tRem");
            *t = b->CreateExactUDiv(b->CreateSub(i, rem), step, name+"_t");
        }
        deindex(rem, c, x, y);
    }

    LoadInst *load(Value *i) const { return b->CreateLoad(b->CreateGEP(getData(), i)); }
    StoreInst *store(Value *i, Value *value) const { return b->CreateStore(value, b->CreateGEP(getData(), i)); }
    Value *cast(Value *i, const MatrixBuilder &dst) const { return (type() == dst.type()) ? i : b->CreateCast(CastInst::getCastOpcode(i, isSigned(), dst.ty(), dst.isSigned()), i, dst.ty()); }
    Value *add(Value *i, Value *j, const Twine &name = "") const { return isFloating() ? b->CreateFAdd(i, j, name) : b->CreateAdd(i, j, name); }
    Value *multiply(Value *i, Value *j, const Twine &name = "") const { return isFloating() ? b->CreateFMul(i, j, name) : b->CreateMul(i, j, name); }

    Value *compareLT(Value *i, Value *j) const { return isFloating() ? b->CreateFCmpOLT(i, j) : (isSigned() ? b->CreateICmpSLT(i, j) : b->CreateICmpULT(i, j)); }
    Value *compareGT(Value *i, Value *j) const { return isFloating() ? b->CreateFCmpOGT(i, j) : (isSigned() ? b->CreateICmpSGT(i, j) : b->CreateICmpUGT(i, j)); }

    static PHINode *beginLoop(IRBuilder<> &builder, Function *function, BasicBlock *parent, BasicBlock **current, const Twine &name = "") {
        *current = BasicBlock::Create(getGlobalContext(), "loop_"+name, function);
        builder.CreateBr(*current);
        builder.SetInsertPoint(*current);
        PHINode *j = builder.CreatePHI(Type::getInt32Ty(getGlobalContext()), 2, name);
        j->addIncoming(MatrixBuilder::zero(), parent);
        return j;
    }
    PHINode *beginLoop(BasicBlock *parent, BasicBlock **current, const Twine &name = "") const { return beginLoop(*b, f, parent, current, name); }
    static void endLoop(IRBuilder<> &builder, Function *function, BasicBlock *current, PHINode *j, Value *end, const Twine &name = "") {
        BasicBlock *loop = BasicBlock::Create(getGlobalContext(), "loop_"+name+"_end", function);
        builder.CreateBr(loop);
        builder.SetInsertPoint(loop);
        Value *increment = builder.CreateAdd(j, MatrixBuilder::one(), "increment_"+name);
        j->addIncoming(increment, loop);
        BasicBlock *exit = BasicBlock::Create(getGlobalContext(), "loop_"+name+"_exit", function);
        builder.CreateCondBr(builder.CreateICmpNE(increment, end, "loop_"+name+"_test"), current, exit);
        builder.SetInsertPoint(exit);
    }
    void endLoop(BasicBlock *current, PHINode *j, Value *end, const Twine &name = "") const { endLoop(*b, f, current, j, end, name); }

    template <typename T>
    inline static std::vector<T> toVector(T value) { std::vector<T> vector; vector.push_back(value); return vector; }

    static Type *ty(const Matrix &m)
    {
        const int bits = m.bits();
        if (m.isFloating()) {
            if      (bits == 16) return Type::getHalfTy(getGlobalContext());
            else if (bits == 32) return Type::getFloatTy(getGlobalContext());
            else if (bits == 64) return Type::getDoubleTy(getGlobalContext());
        } else {
            if      (bits == 1)  return Type::getInt1Ty(getGlobalContext());
            else if (bits == 8)  return Type::getInt8Ty(getGlobalContext());
            else if (bits == 16) return Type::getInt16Ty(getGlobalContext());
            else if (bits == 32) return Type::getInt32Ty(getGlobalContext());
            else if (bits == 64) return Type::getInt64Ty(getGlobalContext());
        }
        qFatal("Invalid matrix type.");
        return NULL;
    }
    inline Type *ty() const { return ty(*this); }
    inline std::vector<Type*> tys() const { return toVector<Type*>(ty()); }

    static Type *ptrTy(const Matrix &m)
    {
        const int bits = m.bits();
        if (m.isFloating()) {
            if      (bits == 16) return Type::getHalfPtrTy(getGlobalContext());
            else if (bits == 32) return Type::getFloatPtrTy(getGlobalContext());
            else if (bits == 64) return Type::getDoublePtrTy(getGlobalContext());
        } else {
            if      (bits == 1)  return Type::getInt1PtrTy(getGlobalContext());
            else if (bits == 8)  return Type::getInt8PtrTy(getGlobalContext());
            else if (bits == 16) return Type::getInt16PtrTy(getGlobalContext());
            else if (bits == 32) return Type::getInt32PtrTy(getGlobalContext());
            else if (bits == 64) return Type::getInt64PtrTy(getGlobalContext());
        }
        qFatal("Invalid matrix type.");
        return NULL;
    }
    inline Type *ptrTy() const { return ptrTy(*this); }
};

namespace br
{

/*!
 * \brief LLVM Unary Kernel
 * \author Josh Klontz \cite jklontz
 */
class UnaryKernel : public UntrainableMetaTransform
{
    Q_OBJECT

    UnaryKernel_t kernel;
    quint16 hash;

public:
    UnaryKernel() : kernel(NULL), hash(0) {}
    virtual int preallocate(const Matrix &src, Matrix &dst) const = 0; /*!< Preallocate destintation matrix based on source matrix. */
    virtual Value *buildPreallocate(const MatrixBuilder &src, const MatrixBuilder &dst) const { (void) src; (void) dst; return MatrixBuilder::constant(0); }
    virtual void build(const MatrixBuilder &src, const MatrixBuilder &dst, PHINode *i) const = 0; /*!< Build the kernel. */

    void apply(const Matrix &src, Matrix &dst) const
    {
        const int size = preallocate(src, dst);
        dst.allocate();
        invoke(src, dst, size);
    }

    void optimize(Function *f) const
    {
        while (TheFunctionPassManager->run(*f));
        TheExtraFunctionPassManager->run(*f);
    }

    UnaryKernel_t getKernel(const Matrix *src) const
    {
        const QString functionName = mangledName(*src);
        Function *function = TheModule->getFunction(qPrintable(functionName));
        if (function == NULL) function = compile(*src);
        return (UnaryKernel_t)TheExecutionEngine->getPointerToFunction(function);
    }

private:
    QString mangledName() const
    {
        static QHash<QString, int> argsLUT;
        const QString args = arguments().join(",");
        if (!argsLUT.contains(args)) argsLUT.insert(args, argsLUT.size());
        int uid = argsLUT.value(args);
        return "jitcv_" + name().remove("Transform") + (args.isEmpty() ? QString() : QString::number(uid));
    }

    QString mangledName(const Matrix &src) const
    {
        return mangledName() + "_" + MatrixToString(src);
    }

    Function *compile(const Matrix &m) const
    {
        Function *kernel = compileKernel(m);
        optimize(kernel);

        Constant *c = TheModule->getOrInsertFunction(qPrintable(mangledName()),
                                                     Type::getVoidTy(getGlobalContext()),
                                                     PointerType::getUnqual(TheMatrixStruct),
                                                     PointerType::getUnqual(TheMatrixStruct),
                                                     NULL);

        Function *function = cast<Function>(c);
        function->setCallingConv(CallingConv::C);

        Function::arg_iterator args = function->arg_begin();
        Value *src = args++;
        src->setName("src");
        Value *dst = args++;
        dst->setName("dst");

        BasicBlock *entry = BasicBlock::Create(getGlobalContext(), "entry", function);
        IRBuilder<> builder(entry);
        MatrixBuilder mb(m, src, &builder, function, "src");
        MatrixBuilder nb(m, dst, &builder, function, "dst");

        std::vector<Type*> kernelArgs;
        kernelArgs.push_back(PointerType::getUnqual(TheMatrixStruct));
        kernelArgs.push_back(PointerType::getUnqual(TheMatrixStruct));
        kernelArgs.push_back(Type::getInt32Ty(getGlobalContext()));
        PointerType *kernelType = PointerType::getUnqual(FunctionType::get(Type::getVoidTy(getGlobalContext()), kernelArgs, false));
        QString kernelFunctionName = mangledName()+"_kernel";
        TheModule->getOrInsertGlobal(qPrintable(kernelFunctionName), kernelType);
        GlobalVariable *kernelFunction = TheModule->getGlobalVariable(qPrintable(kernelFunctionName));
        kernelFunction->setInitializer(ConstantPointerNull::get(kernelType));

        QString kernelHashName = mangledName()+"_hash";
        TheModule->getOrInsertGlobal(qPrintable(kernelHashName), Type::getInt16Ty(getGlobalContext()));
        GlobalVariable *kernelHash = TheModule->getGlobalVariable(qPrintable(kernelHashName));
        kernelHash->setInitializer(MatrixBuilder::constant(0, 16));

        BasicBlock *getKernel = BasicBlock::Create(getGlobalContext(), "get_kernel", function);
        BasicBlock *preallocate = BasicBlock::Create(getGlobalContext(), "preallocate", function);
        Value *hashTest = builder.CreateICmpNE(mb.getHash(), builder.CreateLoad(kernelHash), "hash_fail_test");
        builder.CreateCondBr(hashTest, getKernel, preallocate);

        builder.SetInsertPoint(getKernel);
        builder.CreateStore(kernel, kernelFunction);
        builder.CreateStore(mb.getHash(), kernelHash);
        builder.CreateBr(preallocate);
        builder.SetInsertPoint(preallocate);
        Value *kernelSize = buildPreallocate(mb, nb);

        BasicBlock *allocate = BasicBlock::Create(getGlobalContext(), "allocate", function);
        builder.CreateBr(allocate);
        builder.SetInsertPoint(allocate);
        nb.allocateCode();

        BasicBlock *callKernel = BasicBlock::Create(getGlobalContext(), "call_kernel", function);
        builder.CreateBr(callKernel);
        builder.SetInsertPoint(callKernel);
        builder.CreateCall3(builder.CreateLoad(kernelFunction), src, dst, kernelSize);
        builder.CreateRetVoid();

        optimize(function);
        return kernel;
    }

    Function *compileKernel(const Matrix &m) const
    {
        Constant *c = TheModule->getOrInsertFunction(qPrintable(mangledName(m)),
                                                     Type::getVoidTy(getGlobalContext()),
                                                     PointerType::getUnqual(TheMatrixStruct),
                                                     PointerType::getUnqual(TheMatrixStruct),
                                                     Type::getInt32Ty(getGlobalContext()),
                                                     NULL);

        Function *function = cast<Function>(c);
        function->setCallingConv(CallingConv::C);

        Function::arg_iterator args = function->arg_begin();
        Value *src = args++;
        src->setName("src");
        Value *dst = args++;
        dst->setName("dst");
        Value *len = args++;
        len->setName("len");

        BasicBlock *entry = BasicBlock::Create(getGlobalContext(), "entry", function);
        IRBuilder<> builder(entry);

        BasicBlock *kernel;
        PHINode *i = MatrixBuilder::beginLoop(builder, function, entry, &kernel, "i");

        Matrix n;
        preallocate(m, n);
        build(MatrixBuilder(m, src, &builder, function, "src"), MatrixBuilder(n, dst, &builder, function, "dst"), i);

        MatrixBuilder::endLoop(builder, function, kernel, i, len, "i");

        builder.CreateRetVoid();
        return function;
    }

    void project(const Template &src, Template &dst) const
    {
        const Matrix m(MatrixFromMat(src));
        Matrix n;
        const int size = preallocate(m, n);
        AllocateMatrixFromMat(n, dst);
        invoke(m, n, size);
    }

    void invoke(const Matrix &src, Matrix &dst, int size) const
    {
        if (src.hash != hash) {
            static QMutex compilerLock;
            QMutexLocker locker(&compilerLock);

            if (src.hash != hash) {
                const_cast<UnaryKernel*>(this)->kernel = getKernel(&src);
                const_cast<UnaryKernel*>(this)->hash = src.hash;
            }
        }

        kernel(&src, &dst, size);
    }
};

/*!
 * \brief LLVM Binary Kernel
 * \author Josh Klontz \cite jklontz
 */
class BinaryKernel: public UntrainableMetaTransform
{
    Q_OBJECT

    BinaryKernel_t kernel;
    quint16 hashA, hashB;

public:
    BinaryKernel() : kernel(NULL), hashA(0), hashB(0) {}
    virtual int preallocate(const Matrix &srcA, const Matrix &srcB, Matrix &dst) const = 0; /*!< Preallocate destintation matrix based on source matrix. */
    virtual void build(const MatrixBuilder &srcA, const MatrixBuilder &srcB, const MatrixBuilder &dst, PHINode *i) const = 0; /*!< Build the kernel. */

    void apply(const Matrix &srcA, const Matrix &srcB, Matrix &dst) const
    {
        const int size = preallocate(srcA, srcB, dst);
        dst.allocate();
        invoke(srcA, srcB, dst, size);
    }

private:
    QString mangledName(const Matrix &srcA, const Matrix &srcB) const
    {
        return "jitcv_" + name().remove("Transform") + "_" + MatrixToString(srcA) + "_" + MatrixToString(srcB);
    }

    Function *compile(const Matrix &m, const Matrix &n) const
    {
        Constant *c = TheModule->getOrInsertFunction(qPrintable(mangledName(m, n)),
                                                     Type::getVoidTy(getGlobalContext()),
                                                     PointerType::getUnqual(TheMatrixStruct),
                                                     PointerType::getUnqual(TheMatrixStruct),
                                                     PointerType::getUnqual(TheMatrixStruct),
                                                     Type::getInt32Ty(getGlobalContext()),
                                                     NULL);

        Function *function = cast<Function>(c);
        function->setCallingConv(CallingConv::C);

        Function::arg_iterator args = function->arg_begin();
        Value *srcA = args++;
        srcA->setName("srcA");
        Value *srcB = args++;
        srcB->setName("srcB");
        Value *dst = args++;
        dst->setName("dst");
        Value *len = args++;
        len->setName("len");

        BasicBlock *entry = BasicBlock::Create(getGlobalContext(), "entry", function);
        IRBuilder<> builder(entry);

        BasicBlock *kernel;
        PHINode *i = MatrixBuilder::beginLoop(builder, function, entry, &kernel, "i");

        Matrix o;
        preallocate(m, n, o);
        build(MatrixBuilder(m, srcA, &builder, function, "srcA"), MatrixBuilder(n, srcB, &builder, function, "srcB"), MatrixBuilder(o, dst, &builder, function, "dst"), i);

        MatrixBuilder::endLoop(builder, function, kernel, i, len, "i");

        builder.CreateRetVoid();
        return function;
    }

    void invoke(const Matrix &srcA, const Matrix &srcB, Matrix &dst, int size) const
    {
        if ((srcA.hash != hashA) || (srcB.hash != hashB)) {
            static QMutex compilerLock;
            QMutexLocker locker(&compilerLock);

            if ((srcA.hash != hashA) || (srcB.hash != hashB)) {
                const QString functionName = mangledName(srcA, srcB);

                Function *function = TheModule->getFunction(qPrintable(functionName));
                if (function == NULL) {
                    function = compile(srcA, srcB);
                    while (TheFunctionPassManager->run(*function));
                    TheExtraFunctionPassManager->run(*function);
                    function = TheModule->getFunction(qPrintable(functionName));
                }

                const_cast<BinaryKernel*>(this)->kernel = (BinaryKernel_t)TheExecutionEngine->getPointerToFunction(function);
                const_cast<BinaryKernel*>(this)->hashA = srcA.hash;
                const_cast<BinaryKernel*>(this)->hashB = srcB.hash;
            }
        }

        kernel(&srcA, &srcB, &dst, size);
    }
};

/*!
 * \brief LLVM Stitchable Kernel
 * \author Josh Klontz \cite jklontz
 */
class StitchableKernel : public UnaryKernel
{
    Q_OBJECT

public:
    virtual Value *stitch(const MatrixBuilder &src, const MatrixBuilder &dst, Value *val) const = 0; /*!< A simplification of Kernel::build() for stitchable kernels. */

    virtual int preallocate(const Matrix &src, Matrix &dst) const
    {
        dst.copyHeader(src);
        return dst.elements();
    }

    virtual Value *buildPreallocate(const MatrixBuilder &src, const MatrixBuilder &dst) const
    {
        dst.copyHeaderCode(src);
        return dst.elementsCode();
    }

private:
    void build(const MatrixBuilder &src, const MatrixBuilder &dst, PHINode *i) const
    {
        dst.store(i, stitch(src, dst, src.load(i)));
    }
};

} // namespace br

/*!
 * \ingroup transforms
 * \brief LLVM stitch transform
 * \author Josh Klontz \cite jklontz
 */
class stitchTransform : public UnaryKernel
{
    Q_OBJECT
    Q_PROPERTY(QList<br::Transform*> kernels READ get_kernels WRITE set_kernels RESET reset_kernels STORED false)
    BR_PROPERTY(QList<br::Transform*>, kernels, QList<br::Transform*>())

    void init()
    {
        foreach (Transform *transform, kernels)
            if (dynamic_cast<StitchableKernel*>(transform) == NULL)
                qFatal("%s is not a stitchable kernel!", qPrintable(transform->name()));
    }

    int preallocate(const Matrix &src, Matrix &dst) const
    {
        Matrix tmp = src;
        foreach (const Transform *kernel, kernels) {
            static_cast<const UnaryKernel*>(kernel)->preallocate(tmp, dst);
            tmp = dst;
        }
        return dst.elements();
    }

    void build(const MatrixBuilder &src_, const MatrixBuilder &dst_, PHINode *i) const
    {
        MatrixBuilder src(src_);
        MatrixBuilder dst(dst_);
        Value *val = src.load(i);
        foreach (Transform *transform, kernels) {
            static_cast<UnaryKernel*>(transform)->preallocate(src, dst);
            val = static_cast<StitchableKernel*>(transform)->stitch(src, dst, val);
            src.copyHeader(dst);
            src.m = dst.m;
        }
        dst.store(i, val);
    }
};

BR_REGISTER(Transform, stitchTransform)

/*!
 * \ingroup transforms
 * \brief LLVM square transform
 * \author Josh Klontz \cite jklontz
 */
class squareTransform : public StitchableKernel
{
    Q_OBJECT

    Value *stitch(const MatrixBuilder &src, const MatrixBuilder &dst, Value *val) const
    {
        (void) src;
        return dst.multiply(val, val);
    }
};

BR_REGISTER(Transform, squareTransform)

/*!
 * \ingroup transforms
 * \brief LLVM pow transform
 * \author Josh Klontz \cite jklontz
 */
class powTransform : public StitchableKernel
{
    Q_OBJECT
    Q_PROPERTY(double exponent READ get_exponent WRITE set_exponent RESET reset_exponent STORED false)
    BR_PROPERTY(double, exponent, 2)

    int preallocate(const Matrix &src, Matrix &dst) const
    {
        dst.copyHeader(src);
        dst.setFloating(true);
        dst.setBits(max(src.bits(), 32));
        return dst.elements();
    }

    Value *stitch(const MatrixBuilder &src, const MatrixBuilder &dst, Value *val) const
    {
        Value *load = src.cast(val, dst);

        Value *pow;
        if (exponent == ceil(exponent)) {
            if      (exponent == 0) pow = dst.autoConstant(1);
            else if (exponent == 1) pow = load;
            else if (exponent == 2) pow = dst.multiply(load, load);
            else                    pow = src.b->CreateCall2(Intrinsic::getDeclaration(TheModule, Intrinsic::powi, dst.tys()), load, MatrixBuilder::constant(int(exponent)));
        } else {
            pow = src.b->CreateCall2(Intrinsic::getDeclaration(TheModule, Intrinsic::pow, dst.tys()), load, dst.autoConstant(exponent));
        }

        return pow;
    }
};

BR_REGISTER(Transform, powTransform)

/*!
 * \ingroup transforms
 * \brief LLVM sum transform
 * \author Josh Klontz \cite jklontz
 */
class sumTransform : public UnaryKernel
{
    Q_OBJECT
    Q_PROPERTY(bool channels READ get_channels WRITE set_channels RESET reset_channels STORED false)
    Q_PROPERTY(bool columns READ get_columns WRITE set_columns RESET reset_columns STORED false)
    Q_PROPERTY(bool rows READ get_rows WRITE set_rows RESET reset_rows STORED false)
    Q_PROPERTY(bool frames READ get_frames WRITE set_frames RESET reset_frames STORED false)
    BR_PROPERTY(bool, channels, true)
    BR_PROPERTY(bool, columns, true)
    BR_PROPERTY(bool, rows, true)
    BR_PROPERTY(bool, frames, true)

    int preallocate(const Matrix &src, Matrix &dst) const
    {
        dst = Matrix(channels ? 1 : src.channels, columns ? 1 : src.columns, rows ? 1 : src.rows, frames ? 1 : src.frames, src.hash);
        dst.setBits(std::min(2*dst.bits(), dst.isFloating() ? 64 : 32));
        return dst.elements();
    }

    Value *buildPreallocate(const MatrixBuilder &src, const MatrixBuilder &dst) const
    {
        (void) src;
        (void) dst;
        return MatrixBuilder::constant(0);
    }

    void build(const MatrixBuilder &src, const MatrixBuilder &dst, PHINode *i) const
    {
        Value *c, *x, *y, *t;
        dst.deindex(i, &c, &x, &y, &t);
        AllocaInst *sum = dst.autoAlloca(0, "sum");

        QList<PHINode*> loops;
        QList<BasicBlock*> blocks;
        blocks.push_back(i->getParent());
        Value *src_c, *src_x, *src_y, *src_t;

        if (frames && !src.singleFrame()) {
            BasicBlock *block;
            loops.append(dst.beginLoop(blocks.last(), &block, "src_t"));
            blocks.append(block);
            src_t = loops.last();
        } else {
            src_t = t;
        }

        if (rows && !src.singleRow()) {
            BasicBlock *block;
            loops.append(dst.beginLoop(blocks.last(), &block, "src_y"));
            blocks.append(block);
            src_y = loops.last();
        } else {
            src_y = y;
        }

        if (columns && !src.singleColumn()) {
            BasicBlock *block;
            loops.append(dst.beginLoop(blocks.last(), &block, "src_x"));
            blocks.append(block);
            src_x = loops.last();
        } else {
            src_x = x;
        }

        if (channels && !src.singleChannel()) {
            BasicBlock *block;
            loops.append(dst.beginLoop(blocks.last(), &block, "src_c"));
            blocks.append(block);
            src_c = loops.last();
        } else {
            src_c = c;
        }

        dst.b->CreateStore(dst.add(dst.b->CreateLoad(sum), src.cast(src.load(src.aliasIndex(dst, src_c, src_x, src_y, src_t)), dst), "accumulate"), sum);

        if (channels && !src.singleChannel()) dst.endLoop(blocks.takeLast(), loops.takeLast(), src.getChannels(), "src_c");
        if (columns && !src.singleColumn())   dst.endLoop(blocks.takeLast(), loops.takeLast(), src.getColumns(), "src_x");
        if (rows && !src.singleRow())         dst.endLoop(blocks.takeLast(), loops.takeLast(), src.getRows(), "src_y");
        if (frames && !src.singleFrame())     dst.endLoop(blocks.takeLast(), loops.takeLast(), src.getFrames(), "src_t");

        dst.store(i, dst.b->CreateLoad(sum));
    }
};

BR_REGISTER(Transform, sumTransform)

/*!
 * \ingroup transforms
 * \brief LLVM casting transform
 * \author Josh Klontz \cite jklontz
 */
class castTransform : public StitchableKernel
{
    Q_OBJECT
    Q_ENUMS(Type)
    Q_PROPERTY(Type type READ get_type WRITE set_type RESET reset_type STORED false)

public:
     /*!< */
    enum Type { u1 = Matrix::u1,
                u8 = Matrix::u8,
                u16 = Matrix::u16,
                u32 = Matrix::u32,
                u64 = Matrix::u64,
                s8 = Matrix::s8,
                s16 = Matrix::s16,
                s32 = Matrix::s32,
                s64 = Matrix::s64,
                f16 = Matrix::f16,
                f32 = Matrix::f32,
                f64 = Matrix::f64 };

private:
    BR_PROPERTY(Type, type, f32)

    int preallocate(const Matrix &src, Matrix &dst) const
    {
        dst.copyHeader(src);
        dst.setType(type);
        return dst.elements();
    }

    Value *stitch(const MatrixBuilder &src, const MatrixBuilder &dst, Value *val) const
    {
        return src.cast(val, dst);
    }
};

BR_REGISTER(Transform, castTransform)

/*!
 * \ingroup transforms
 * \brief LLVM scale transform
 * \author Josh Klontz \cite jklontz
 */
class scaleTransform : public StitchableKernel
{
    Q_OBJECT
    Q_PROPERTY(double a READ get_a WRITE set_a RESET reset_a STORED false)
    BR_PROPERTY(double, a, 1)

    Value *stitch(const MatrixBuilder &src, const MatrixBuilder &dst, Value *val) const
    {
        (void) src;
        return dst.multiply(val, dst.autoConstant(a));
    }
};

BR_REGISTER(Transform, scaleTransform)

/*!
 * \ingroup absTransform
 * \brief LLVM abs transform
 * \author Josh Klontz \cite jklontz
 */
class absTransform : public StitchableKernel
{
    Q_OBJECT

    Value *stitch(const MatrixBuilder &src, const MatrixBuilder &dst, Value *val) const
    {
        (void) dst;
        if (!src.isSigned()) return val;
        if (src.isFloating()) return src.b->CreateCall(Intrinsic::getDeclaration(TheModule, Intrinsic::fabs, src.tys()), val);
        else                  return src.b->CreateSelect(src.b->CreateICmpSLT(val, src.autoConstant(0)),
                                                         src.b->CreateSub(src.autoConstant(0), val),
                                                         val);
    }
};

BR_REGISTER(Transform, absTransform)

/*!
 * \ingroup transforms
 * \brief LLVM add transform
 * \author Josh Klontz \cite jklontz
 */
class addTransform : public StitchableKernel
{
    Q_OBJECT
    Q_PROPERTY(double b READ get_b WRITE set_b RESET reset_b STORED false)
    BR_PROPERTY(double, b, 0)

    Value *stitch(const MatrixBuilder &src, const MatrixBuilder &dst, Value *val) const
    {
        (void) src;
        return dst.add(val, dst.autoConstant(b));
    }
};

BR_REGISTER(Transform, addTransform)

/*!
 * \ingroup transforms
 * \brief LLVM clamp transform
 * \author Josh Klontz \cite jklontz
 */
class clampTransform : public StitchableKernel
{
    Q_OBJECT
    Q_PROPERTY(double min READ get_min WRITE set_min RESET reset_min STORED false)
    Q_PROPERTY(double max READ get_max WRITE set_max RESET reset_max STORED false)
    BR_PROPERTY(double, min, -std::numeric_limits<double>::max())
    BR_PROPERTY(double, max, std::numeric_limits<double>::max())

    Value *stitch(const MatrixBuilder &src, const MatrixBuilder &dst, Value *val) const
    {
        (void) src;
        Value *clampedVal = val;
        if (min > -std::numeric_limits<double>::max()) {
            Value *low = dst.autoConstant(min);
            clampedVal = dst.b->CreateSelect(dst.compareLT(clampedVal, low), low, clampedVal);
        }
        if (max < std::numeric_limits<double>::max()) {
            Value *high = dst.autoConstant(max);
            clampedVal = dst.b->CreateSelect(dst.compareGT(clampedVal, high), high, clampedVal);
        }
        return clampedVal;
    }
};

BR_REGISTER(Transform, clampTransform)

/*!
 * \ingroup transforms
 * \brief LLVM quantize transform
 * \author Josh Klontz \cite jklontz
 */
class _QuantizeTransform : public Transform
{
    Q_OBJECT
    Q_PROPERTY(float a READ get_a WRITE set_a RESET reset_a)
    Q_PROPERTY(float b READ get_b WRITE set_b RESET reset_b)
    BR_PROPERTY(float, a, 1)
    BR_PROPERTY(float, b, 0)

    QScopedPointer<Transform> transform;

    void init()
    {
        transform.reset(Transform::make(QString("stitch([scale(%1),add(%2),clamp(0,255),cast(u8)])").arg(QString::number(a), QString::number(b))));
    }

    void train(const TemplateList &data)
    {
        (void) data;
        qFatal("_Quantize::train not implemented.");
    }

    void project(const Template &src, Template &dst) const
    {
        transform->project(src, dst);
    }
};

BR_REGISTER(Transform, _QuantizeTransform)

/*!
 * \ingroup initializers
 * \brief Initializes LLVM
 * \author Josh Klontz \cite jklontz
 */
class LLVMInitializer : public Initializer
{
    Q_OBJECT

    void initialize() const
    {
        InitializeNativeTarget();

        TheModule = new Module("jitcv", getGlobalContext());

        std::string error;
        TheExecutionEngine = EngineBuilder(TheModule).setEngineKind(EngineKind::JIT).setErrorStr(&error).create();
        if (TheExecutionEngine == NULL)
            qFatal("Failed to create LLVM ExecutionEngine with error: %s", error.c_str());

        TheFunctionPassManager = new FunctionPassManager(TheModule);
        TheFunctionPassManager->add(createVerifierPass(PrintMessageAction));
        TheFunctionPassManager->add(createEarlyCSEPass());
        TheFunctionPassManager->add(createInstructionCombiningPass());
        TheFunctionPassManager->add(createDeadCodeEliminationPass());
        TheFunctionPassManager->add(createGVNPass());
        TheFunctionPassManager->add(createDeadInstEliminationPass());

        TheExtraFunctionPassManager = new FunctionPassManager(TheModule);
        TheExtraFunctionPassManager->add(createPrintFunctionPass("--------------------------------------------------------------------------------", &errs()));
//        TheExtraFunctionPassManager->add(createLoopUnrollPass(INT_MAX,8));

        TheMatrixStruct = StructType::create("Matrix",
                                             Type::getInt8PtrTy(getGlobalContext()), // data
                                             Type::getInt32Ty(getGlobalContext()),   // channels
                                             Type::getInt32Ty(getGlobalContext()),   // columns
                                             Type::getInt32Ty(getGlobalContext()),   // rows
                                             Type::getInt32Ty(getGlobalContext()),   // frames
                                             Type::getInt16Ty(getGlobalContext()),   // hash
                                             NULL);

        QSharedPointer<Transform> kernel(Transform::make("add(1)", NULL));

        Template src, dst;
        src.m() = (Mat_<qint8>(2,2) << -1, -2, 3, 4);
        kernel->project(src, dst);
        qDebug() << dst.m();

//        src.m() = (Mat_<qint32>(2,2) << -1, -3, 9, 27);
//        kernel->project(src, dst);
//        qDebug() << dst.m();

//        src.m() = (Mat_<float>(2,2) << -1.5, -2.5, 3.5, 4.5);
//        kernel->project(src, dst);
//        qDebug() << dst.m();

//        src.m() = (Mat_<double>(2,2) << 1.75, 2.75, -3.75, -4.75);
//        kernel->project(src, dst);
//        qDebug() << dst.m();
    }

    void finalize() const
    {
        delete TheFunctionPassManager;
        delete TheExecutionEngine;
        llvm_shutdown();
    }

    static void benchmark(const QString &transform)
    {
        static Template src;
        if (src.isEmpty()) {
            Mat m(4096, 4096, CV_32FC1);
            randu(m, 0, 255);
            src.m() = m;
        }

        QScopedPointer<Transform> original(Transform::make(transform, NULL));
        QScopedPointer<Transform> kernel(Transform::make(transform[0].toLower()+transform.mid(1), NULL));

        Template dstOriginal, dstKernel;
        original->project(src, dstOriginal);
        kernel->project(src, dstKernel);

        float difference = sum(dstKernel.m() - dstOriginal.m())[0]/(src.m().rows*src.m().cols);
        if (abs(difference) >= 0.0005)
            qWarning("Kernel result for %s differs by %.3f!", qPrintable(transform), difference);

        QTime time; time.start();
        for (int i=0; i<30; i++)
            kernel->project(src, dstKernel);
        double kernelTime = time.elapsed();

        time.start();
        for (int i=0; i<30; i++)
            original->project(src, dstOriginal);
        double originalTime = time.elapsed();

        qDebug("%s: %.3fx", qPrintable(transform), float(originalTime/kernelTime));
    }
};

BR_REGISTER(Initializer, LLVMInitializer)

UnaryFunction_t jit_unary_make(const char *description)
{
    (void) description;
    return NULL;
}

BinaryFunction_t jit_binary_make(const char *description)
{
    (void) description;
    return NULL;
}

#include "llvm.moc"
