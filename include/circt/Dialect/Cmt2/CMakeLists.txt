add_circt_dialect(Cmt2 cmt2)
add_circt_dialect_doc(Cmt2 cmt2)

set(LLVM_TARGET_DEFINITIONS Cmt2.td)

# mlir_tablegen(Cmt2Attributes.h.inc -gen-attrdef-decls)
# mlir_tablegen(Cmt2Attributes.cpp.inc -gen-attrdef-defs)
# add_public_tablegen_target(CIRCTCmt2AttrsIncGen)
# add_dependencies(circt-headers CIRCTCmt2AttrsIncGen)

set(LLVM_TARGET_DEFINITIONS Cmt2OpInterfaces.td)
mlir_tablegen(Cmt2OpInterfaces.h.inc -gen-op-interface-decls)
mlir_tablegen(Cmt2OpInterfaces.cpp.inc -gen-op-interface-defs)
add_public_tablegen_target(CIRCTCmt2OpInterfacesIncGen)
add_dependencies(circt-headers CIRCTCmt2OpInterfacesIncGen)

set(LLVM_TARGET_DEFINITIONS Cmt2Passes.td)
mlir_tablegen(Cmt2Passes.h.inc -gen-pass-decls)
add_public_tablegen_target(CIRCTCmt2TransformsIncGen)
add_dependencies(circt-headers CIRCTCmt2TransformsIncGen)
