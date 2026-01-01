// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Tracing implementation internals
#include "verilated_vcd_c.h"
#include "VMultiCycleALU__Syms.h"


void VMultiCycleALU___024root__trace_chg_0_sub_0(VMultiCycleALU___024root* vlSelf, VerilatedVcd::Buffer* bufp);

void VMultiCycleALU___024root__trace_chg_0(void* voidSelf, VerilatedVcd::Buffer* bufp) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    VMultiCycleALU___024root__trace_chg_0\n"); );
    // Init
    VMultiCycleALU___024root* const __restrict vlSelf VL_ATTR_UNUSED = static_cast<VMultiCycleALU___024root*>(voidSelf);
    VMultiCycleALU__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    if (VL_UNLIKELY(!vlSymsp->__Vm_activity)) return;
    // Body
    VMultiCycleALU___024root__trace_chg_0_sub_0((&vlSymsp->TOP), bufp);
}

void VMultiCycleALU___024root__trace_chg_0_sub_0(VMultiCycleALU___024root* vlSelf, VerilatedVcd::Buffer* bufp) {
    (void)vlSelf;  // Prevent unused variable warning
    VMultiCycleALU__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VMultiCycleALU___024root__trace_chg_0_sub_0\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
    // Init
    uint32_t* const oldp VL_ATTR_UNUSED = bufp->oldp(vlSymsp->__Vm_baseCode + 1);
    // Body
    if (VL_UNLIKELY(vlSelfRef.__Vm_traceActivity[1U])) {
        bufp->chgIData(oldp+0,(vlSelfRef.MultiCycleALU__DOT__reg_result__DOT__value),32);
        bufp->chgBit(oldp+1,(vlSelfRef.MultiCycleALU__DOT__busy__DOT__value));
        bufp->chgIData(oldp+2,(vlSelfRef.MultiCycleALU__DOT__reg_a__DOT__value),32);
        bufp->chgIData(oldp+3,(vlSelfRef.MultiCycleALU__DOT__reg_b__DOT__value),32);
        bufp->chgIData(oldp+4,((vlSelfRef.MultiCycleALU__DOT__reg_a__DOT__value 
                                + vlSelfRef.MultiCycleALU__DOT__reg_b__DOT__value)),32);
    }
    bufp->chgBit(oldp+5,(vlSelfRef.clk));
    bufp->chgBit(oldp+6,(vlSelfRef.rst));
    bufp->chgBit(oldp+7,(vlSelfRef.start_enable));
    bufp->chgBit(oldp+8,(vlSelfRef.start_ready));
    bufp->chgIData(oldp+9,(vlSelfRef.start_a),32);
    bufp->chgIData(oldp+10,(vlSelfRef.start_b),32);
    bufp->chgBit(oldp+11,(vlSelfRef.get_result_ready));
    bufp->chgIData(oldp+12,(vlSelfRef.get_result_res0),32);
    bufp->chgBit(oldp+13,(vlSelfRef.is_ready_ready));
    bufp->chgBit(oldp+14,(vlSelfRef.is_ready_res0));
    bufp->chgBit(oldp+15,(((~ ((~ (IData)(vlSelfRef.MultiCycleALU__DOT___GEN_2)) 
                               & (IData)(vlSelfRef.MultiCycleALU__DOT__busy__DOT__value))) 
                           & (IData)(vlSelfRef.MultiCycleALU__DOT___GEN_2))));
}

void VMultiCycleALU___024root__trace_cleanup(void* voidSelf, VerilatedVcd* /*unused*/) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    VMultiCycleALU___024root__trace_cleanup\n"); );
    // Init
    VMultiCycleALU___024root* const __restrict vlSelf VL_ATTR_UNUSED = static_cast<VMultiCycleALU___024root*>(voidSelf);
    VMultiCycleALU__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    // Body
    vlSymsp->__Vm_activity = false;
    vlSymsp->TOP.__Vm_traceActivity[0U] = 0U;
    vlSymsp->TOP.__Vm_traceActivity[1U] = 0U;
}
