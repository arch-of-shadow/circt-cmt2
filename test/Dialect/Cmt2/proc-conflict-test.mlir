// RUN: circt-opt %s -cmt2-compile-invoke -cmt2-tdcc -cmt2-proc-stmt-to-action -cmt2-proc-to-gaa | FileCheck %s --check-prefix=CHECK-LOWERED

// Test case: proc.rule loop increments register by 1 each step
// Regular rule divides by 2 when register == 4
// Regular rule has higher priority, so it should block proc when reg == 4

builtin.module {
    firrtl.circuit "Reg32" {
        firrtl.module @Reg32(in %write: !firrtl.uint<32>, in %writeEnable: !firrtl.uint<1>,
                           in %clock: !firrtl.clock, in %reset: !firrtl.uint<1>,
                           out %writeReady: !firrtl.uint<1>, out %readReady: !firrtl.uint<1>,
                           out %read: !firrtl.uint<32>) {
            %c0_ui32 = firrtl.constant 0 : !firrtl.uint<32>
            %r = firrtl.regreset %clock, %reset, %c0_ui32 : !firrtl.clock, !firrtl.uint<1>, !firrtl.uint<32>, !firrtl.uint<32>
            %next = firrtl.mux(%writeEnable, %write, %r) : (!firrtl.uint<1>, !firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
            firrtl.connect %r, %next : !firrtl.uint<32>
            %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
            firrtl.connect %writeReady, %c1_ui1 : !firrtl.uint<1>
            firrtl.connect %readReady, %c1_ui1 : !firrtl.uint<1>
            firrtl.connect %read, %r : !firrtl.uint<32>
        }
    }

    cmt2.circuit {
        cmt2.module.extern.firrtl @reg : @Reg32(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.bind.bare %clk, @clock : !firrtl.clock
            cmt2.bind.bare %rst, @reset : !firrtl.uint<1>
            cmt2.bind.value @read : (!firrtl.uint<1>) -> (!firrtl.uint<32>) [
                ready = "readReady", arguments = [], results = ["read"]
            ]
            cmt2.bind.method @write : (!firrtl.uint<1>, !firrtl.uint<32>) -> (!firrtl.uint<1>) [
                enable = "writeEnable", ready = "writeReady", arguments = ["write"], results = []
            ]
        } {
            conflict = [[@write, @write]],
            conflictFree = [[@read, @read]],
            sequenceBefore = [[@read, @write]]
        }

        // CHECK-LOWERED-LABEL: cmt2.module @ProcConflictTest
        cmt2.module @ProcConflictTest(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @counter = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

            // Step: increment register by 1
            cmt2.proc.step @incr_step {
                %val = cmt2.call @counter @read() : () -> !firrtl.uint<32>
                %c1 = firrtl.constant 1 : !firrtl.uint<32>
                %sum = firrtl.add %val, %c1 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
                %new_val = firrtl.bits %sum 31 to 0 : (!firrtl.uint<33>) -> !firrtl.uint<32>
                cmt2.call @counter @write(%new_val) : (!firrtl.uint<32>) -> ()
                %c1_done = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.proc.step_done %c1_done : !firrtl.uint<1>
            }

            // Procedural rule: loop forever incrementing by 1
            // Guard is always true (infinite loop)
            // CHECK-LOWERED: cmt2.rule @incr_loop
            cmt2.proc.rule @incr_loop() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                // In a real case this would be a while loop, but for simplicity
                // we use a single enable (will repeat when fired again)
                cmt2.proc.enable @incr_step
                cmt2.proc.control_end
            }

            // Regular rule: when counter == 4, divide by 2
            // This has HIGHER priority than incr_loop (via precedence)
            cmt2.rule @div_by_2() -> () {
                %val = cmt2.call @counter @read() : () -> !firrtl.uint<32>
                %c4 = firrtl.constant 4 : !firrtl.uint<32>
                %is_4 = firrtl.eq %val, %c4 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<1>
                cmt2.return %is_4 : !firrtl.uint<1>
            } {
                %val = cmt2.call @counter @read() : () -> !firrtl.uint<32>
                // Divide by 2 = shift right by 1
                %c1 = firrtl.constant 1 : !firrtl.uint<32>
                %shifted = firrtl.shr %val, 1 : (!firrtl.uint<32>) -> !firrtl.uint<31>
                %new_val = firrtl.pad %shifted, 32 : (!firrtl.uint<31>) -> !firrtl.uint<32>
                cmt2.call @counter @write(%new_val) : (!firrtl.uint<32>) -> ()
                cmt2.return
            }
        } {
            // Precedence: div_by_2 has higher priority than incr_loop
            // When both are enabled, div_by_2 fires and blocks incr_loop
            precedence = [[@div_by_2, @incr_loop]]
        }
    }
}
