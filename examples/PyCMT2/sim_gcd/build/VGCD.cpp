// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Model implementation (design independent parts)

#include "VGCD__pch.h"
#include "verilated_vcd_c.h"

//============================================================
// Constructors

VGCD::VGCD(VerilatedContext* _vcontextp__, const char* _vcname__)
    : VerilatedModel{*_vcontextp__}
    , vlSymsp{new VGCD__Syms(contextp(), _vcname__, this)}
    , clk{vlSymsp->TOP.clk}
    , rst{vlSymsp->TOP.rst}
    , load_enable{vlSymsp->TOP.load_enable}
    , load_ready{vlSymsp->TOP.load_ready}
    , result_ready{vlSymsp->TOP.result_ready}
    , load_a{vlSymsp->TOP.load_a}
    , load_b{vlSymsp->TOP.load_b}
    , result_res0{vlSymsp->TOP.result_res0}
    , rootp{&(vlSymsp->TOP)}
{
    // Register model with the context
    contextp()->addModel(this);
    contextp()->traceBaseModelCbAdd(
        [this](VerilatedTraceBaseC* tfp, int levels, int options) { traceBaseModel(tfp, levels, options); });
}

VGCD::VGCD(const char* _vcname__)
    : VGCD(Verilated::threadContextp(), _vcname__)
{
}

//============================================================
// Destructor

VGCD::~VGCD() {
    delete vlSymsp;
}

//============================================================
// Evaluation function

#ifdef VL_DEBUG
void VGCD___024root___eval_debug_assertions(VGCD___024root* vlSelf);
#endif  // VL_DEBUG
void VGCD___024root___eval_static(VGCD___024root* vlSelf);
void VGCD___024root___eval_initial(VGCD___024root* vlSelf);
void VGCD___024root___eval_settle(VGCD___024root* vlSelf);
void VGCD___024root___eval(VGCD___024root* vlSelf);

void VGCD::eval_step() {
    VL_DEBUG_IF(VL_DBG_MSGF("+++++TOP Evaluate VGCD::eval_step\n"); );
#ifdef VL_DEBUG
    // Debug assertions
    VGCD___024root___eval_debug_assertions(&(vlSymsp->TOP));
#endif  // VL_DEBUG
    vlSymsp->__Vm_activity = true;
    vlSymsp->__Vm_deleter.deleteAll();
    if (VL_UNLIKELY(!vlSymsp->__Vm_didInit)) {
        vlSymsp->__Vm_didInit = true;
        VL_DEBUG_IF(VL_DBG_MSGF("+ Initial\n"););
        VGCD___024root___eval_static(&(vlSymsp->TOP));
        VGCD___024root___eval_initial(&(vlSymsp->TOP));
        VGCD___024root___eval_settle(&(vlSymsp->TOP));
    }
    VL_DEBUG_IF(VL_DBG_MSGF("+ Eval\n"););
    VGCD___024root___eval(&(vlSymsp->TOP));
    // Evaluate cleanup
    Verilated::endOfEval(vlSymsp->__Vm_evalMsgQp);
}

//============================================================
// Events and timing
bool VGCD::eventsPending() { return false; }

uint64_t VGCD::nextTimeSlot() {
    VL_FATAL_MT(__FILE__, __LINE__, "", "%Error: No delays in the design");
    return 0;
}

//============================================================
// Utilities

const char* VGCD::name() const {
    return vlSymsp->name();
}

//============================================================
// Invoke final blocks

void VGCD___024root___eval_final(VGCD___024root* vlSelf);

VL_ATTR_COLD void VGCD::final() {
    VGCD___024root___eval_final(&(vlSymsp->TOP));
}

//============================================================
// Implementations of abstract methods from VerilatedModel

const char* VGCD::hierName() const { return vlSymsp->name(); }
const char* VGCD::modelName() const { return "VGCD"; }
unsigned VGCD::threads() const { return 1; }
void VGCD::prepareClone() const { contextp()->prepareClone(); }
void VGCD::atClone() const {
    contextp()->threadPoolpOnClone();
}
std::unique_ptr<VerilatedTraceConfig> VGCD::traceConfig() const {
    return std::unique_ptr<VerilatedTraceConfig>{new VerilatedTraceConfig{false, false, false}};
};

//============================================================
// Trace configuration

void VGCD___024root__trace_decl_types(VerilatedVcd* tracep);

void VGCD___024root__trace_init_top(VGCD___024root* vlSelf, VerilatedVcd* tracep);

VL_ATTR_COLD static void trace_init(void* voidSelf, VerilatedVcd* tracep, uint32_t code) {
    // Callback from tracep->open()
    VGCD___024root* const __restrict vlSelf VL_ATTR_UNUSED = static_cast<VGCD___024root*>(voidSelf);
    VGCD__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    if (!vlSymsp->_vm_contextp__->calcUnusedSigs()) {
        VL_FATAL_MT(__FILE__, __LINE__, __FILE__,
            "Turning on wave traces requires Verilated::traceEverOn(true) call before time 0.");
    }
    vlSymsp->__Vm_baseCode = code;
    if (strlen(vlSymsp->name())) tracep->pushPrefix(std::string{vlSymsp->name()}, VerilatedTracePrefixType::SCOPE_MODULE);
    VGCD___024root__trace_decl_types(tracep);
    VGCD___024root__trace_init_top(vlSelf, tracep);
    if (strlen(vlSymsp->name())) tracep->popPrefix();
}

VL_ATTR_COLD void VGCD___024root__trace_register(VGCD___024root* vlSelf, VerilatedVcd* tracep);

VL_ATTR_COLD void VGCD::traceBaseModel(VerilatedTraceBaseC* tfp, int levels, int options) {
    (void)levels; (void)options;
    VerilatedVcdC* const stfp = dynamic_cast<VerilatedVcdC*>(tfp);
    if (VL_UNLIKELY(!stfp)) {
        vl_fatal(__FILE__, __LINE__, __FILE__,"'VGCD::trace()' called on non-VerilatedVcdC object;"
            " use --trace-fst with VerilatedFst object, and --trace with VerilatedVcd object");
    }
    stfp->spTrace()->addModel(this);
    stfp->spTrace()->addInitCb(&trace_init, &(vlSymsp->TOP));
    VGCD___024root__trace_register(&(vlSymsp->TOP), stfp->spTrace());
}
