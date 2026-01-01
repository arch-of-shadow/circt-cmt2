// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Model implementation (design independent parts)

#include "VMultiCycleALU__pch.h"
#include "verilated_vcd_c.h"

//============================================================
// Constructors

VMultiCycleALU::VMultiCycleALU(VerilatedContext* _vcontextp__, const char* _vcname__)
    : VerilatedModel{*_vcontextp__}
    , vlSymsp{new VMultiCycleALU__Syms(contextp(), _vcname__, this)}
    , clk{vlSymsp->TOP.clk}
    , rst{vlSymsp->TOP.rst}
    , start_enable{vlSymsp->TOP.start_enable}
    , start_ready{vlSymsp->TOP.start_ready}
    , get_result_ready{vlSymsp->TOP.get_result_ready}
    , is_ready_ready{vlSymsp->TOP.is_ready_ready}
    , is_ready_res0{vlSymsp->TOP.is_ready_res0}
    , start_a{vlSymsp->TOP.start_a}
    , start_b{vlSymsp->TOP.start_b}
    , get_result_res0{vlSymsp->TOP.get_result_res0}
    , rootp{&(vlSymsp->TOP)}
{
    // Register model with the context
    contextp()->addModel(this);
    contextp()->traceBaseModelCbAdd(
        [this](VerilatedTraceBaseC* tfp, int levels, int options) { traceBaseModel(tfp, levels, options); });
}

VMultiCycleALU::VMultiCycleALU(const char* _vcname__)
    : VMultiCycleALU(Verilated::threadContextp(), _vcname__)
{
}

//============================================================
// Destructor

VMultiCycleALU::~VMultiCycleALU() {
    delete vlSymsp;
}

//============================================================
// Evaluation function

#ifdef VL_DEBUG
void VMultiCycleALU___024root___eval_debug_assertions(VMultiCycleALU___024root* vlSelf);
#endif  // VL_DEBUG
void VMultiCycleALU___024root___eval_static(VMultiCycleALU___024root* vlSelf);
void VMultiCycleALU___024root___eval_initial(VMultiCycleALU___024root* vlSelf);
void VMultiCycleALU___024root___eval_settle(VMultiCycleALU___024root* vlSelf);
void VMultiCycleALU___024root___eval(VMultiCycleALU___024root* vlSelf);

void VMultiCycleALU::eval_step() {
    VL_DEBUG_IF(VL_DBG_MSGF("+++++TOP Evaluate VMultiCycleALU::eval_step\n"); );
#ifdef VL_DEBUG
    // Debug assertions
    VMultiCycleALU___024root___eval_debug_assertions(&(vlSymsp->TOP));
#endif  // VL_DEBUG
    vlSymsp->__Vm_activity = true;
    vlSymsp->__Vm_deleter.deleteAll();
    if (VL_UNLIKELY(!vlSymsp->__Vm_didInit)) {
        vlSymsp->__Vm_didInit = true;
        VL_DEBUG_IF(VL_DBG_MSGF("+ Initial\n"););
        VMultiCycleALU___024root___eval_static(&(vlSymsp->TOP));
        VMultiCycleALU___024root___eval_initial(&(vlSymsp->TOP));
        VMultiCycleALU___024root___eval_settle(&(vlSymsp->TOP));
    }
    VL_DEBUG_IF(VL_DBG_MSGF("+ Eval\n"););
    VMultiCycleALU___024root___eval(&(vlSymsp->TOP));
    // Evaluate cleanup
    Verilated::endOfEval(vlSymsp->__Vm_evalMsgQp);
}

//============================================================
// Events and timing
bool VMultiCycleALU::eventsPending() { return false; }

uint64_t VMultiCycleALU::nextTimeSlot() {
    VL_FATAL_MT(__FILE__, __LINE__, "", "%Error: No delays in the design");
    return 0;
}

//============================================================
// Utilities

const char* VMultiCycleALU::name() const {
    return vlSymsp->name();
}

//============================================================
// Invoke final blocks

void VMultiCycleALU___024root___eval_final(VMultiCycleALU___024root* vlSelf);

VL_ATTR_COLD void VMultiCycleALU::final() {
    VMultiCycleALU___024root___eval_final(&(vlSymsp->TOP));
}

//============================================================
// Implementations of abstract methods from VerilatedModel

const char* VMultiCycleALU::hierName() const { return vlSymsp->name(); }
const char* VMultiCycleALU::modelName() const { return "VMultiCycleALU"; }
unsigned VMultiCycleALU::threads() const { return 1; }
void VMultiCycleALU::prepareClone() const { contextp()->prepareClone(); }
void VMultiCycleALU::atClone() const {
    contextp()->threadPoolpOnClone();
}
std::unique_ptr<VerilatedTraceConfig> VMultiCycleALU::traceConfig() const {
    return std::unique_ptr<VerilatedTraceConfig>{new VerilatedTraceConfig{false, false, false}};
};

//============================================================
// Trace configuration

void VMultiCycleALU___024root__trace_decl_types(VerilatedVcd* tracep);

void VMultiCycleALU___024root__trace_init_top(VMultiCycleALU___024root* vlSelf, VerilatedVcd* tracep);

VL_ATTR_COLD static void trace_init(void* voidSelf, VerilatedVcd* tracep, uint32_t code) {
    // Callback from tracep->open()
    VMultiCycleALU___024root* const __restrict vlSelf VL_ATTR_UNUSED = static_cast<VMultiCycleALU___024root*>(voidSelf);
    VMultiCycleALU__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    if (!vlSymsp->_vm_contextp__->calcUnusedSigs()) {
        VL_FATAL_MT(__FILE__, __LINE__, __FILE__,
            "Turning on wave traces requires Verilated::traceEverOn(true) call before time 0.");
    }
    vlSymsp->__Vm_baseCode = code;
    if (strlen(vlSymsp->name())) tracep->pushPrefix(std::string{vlSymsp->name()}, VerilatedTracePrefixType::SCOPE_MODULE);
    VMultiCycleALU___024root__trace_decl_types(tracep);
    VMultiCycleALU___024root__trace_init_top(vlSelf, tracep);
    if (strlen(vlSymsp->name())) tracep->popPrefix();
}

VL_ATTR_COLD void VMultiCycleALU___024root__trace_register(VMultiCycleALU___024root* vlSelf, VerilatedVcd* tracep);

VL_ATTR_COLD void VMultiCycleALU::traceBaseModel(VerilatedTraceBaseC* tfp, int levels, int options) {
    (void)levels; (void)options;
    VerilatedVcdC* const stfp = dynamic_cast<VerilatedVcdC*>(tfp);
    if (VL_UNLIKELY(!stfp)) {
        vl_fatal(__FILE__, __LINE__, __FILE__,"'VMultiCycleALU::trace()' called on non-VerilatedVcdC object;"
            " use --trace-fst with VerilatedFst object, and --trace with VerilatedVcd object");
    }
    stfp->spTrace()->addModel(this);
    stfp->spTrace()->addInitCb(&trace_init, &(vlSymsp->TOP));
    VMultiCycleALU___024root__trace_register(&(vlSymsp->TOP), stfp->spTrace());
}
