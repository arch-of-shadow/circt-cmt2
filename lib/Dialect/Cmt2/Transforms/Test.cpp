#include "PassDetails.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"
#include "circt/Support/LLVM.h"
#include "mlir/IR/PatternMatch.h"
#include "llvm/ADT/TypeSwitch.h"


using namespace circt;
using namespace cmt2;
using namespace mlir;

class Test
    : public TestBase<Test> {
  void runOnOperation() override;
};

void Test::runOnOperation() {
    exit(0);
    return;
}

std::unique_ptr<mlir::Pass> circt::cmt2::createTestPass() {
  return std::make_unique<Test>();
}