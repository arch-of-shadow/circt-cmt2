// RUN: circt-opt %s | FileCheck %s

// Test Bundle and FVector types in Cmt2

builtin.module {
    cmt2.circuit {
        // Test module with bundle and vector types
        // CHECK: cmt2.module @BundleVectorTest
        cmt2.module @BundleVectorTest() {
            // Test 1: Create a simple bundle with two fields
            // CHECK: %[[BUNDLE:.*]] = firrtl.invalidvalue : !firrtl.bundle<a: uint<8>, b: uint<16>>
            %bundle = firrtl.invalidvalue : !firrtl.bundle<a: uint<8>, b: uint<16>>

            // CHECK: %[[FIELD_A:.*]] = firrtl.subfield %[[BUNDLE]][a] : !firrtl.bundle<a: uint<8>, b: uint<16>>
            %field_a = firrtl.subfield %bundle[a] : !firrtl.bundle<a: uint<8>, b: uint<16>>

            // CHECK: %[[FIELD_B:.*]] = firrtl.subfield %[[BUNDLE]][b] : !firrtl.bundle<a: uint<8>, b: uint<16>>
            %field_b = firrtl.subfield %bundle[b] : !firrtl.bundle<a: uint<8>, b: uint<16>>

            // Test 2: Create a vector of uint<32> with 4 elements
            // CHECK: %[[VECTOR:.*]] = firrtl.invalidvalue : !firrtl.vector<uint<32>, 4>
            %vector = firrtl.invalidvalue : !firrtl.vector<uint<32>, 4>

            // CHECK: %[[ELEM_0:.*]] = firrtl.subindex %[[VECTOR]][0] : !firrtl.vector<uint<32>, 4>
            %elem_0 = firrtl.subindex %vector[0] : !firrtl.vector<uint<32>, 4>

            // CHECK: %[[ELEM_1:.*]] = firrtl.subindex %[[VECTOR]][1] : !firrtl.vector<uint<32>, 4>
            %elem_1 = firrtl.subindex %vector[1] : !firrtl.vector<uint<32>, 4>

            // Test 3: Nested structures - bundle containing a vector
            // CHECK: %[[NESTED:.*]] = firrtl.invalidvalue : !firrtl.bundle<data: vector<uint<8>, 2>, valid: uint<1>>
            %nested = firrtl.invalidvalue : !firrtl.bundle<data: vector<uint<8>, 2>, valid: uint<1>>

            // CHECK: %[[DATA_FIELD:.*]] = firrtl.subfield %[[NESTED]][data] : !firrtl.bundle<data: vector<uint<8>, 2>, valid: uint<1>>
            %data_field = firrtl.subfield %nested[data] : !firrtl.bundle<data: vector<uint<8>, 2>, valid: uint<1>>

            // CHECK: %[[VALID_FIELD:.*]] = firrtl.subfield %[[NESTED]][valid] : !firrtl.bundle<data: vector<uint<8>, 2>, valid: uint<1>>
            %valid_field = firrtl.subfield %nested[valid] : !firrtl.bundle<data: vector<uint<8>, 2>, valid: uint<1>>

            // CHECK: %[[DATA_0:.*]] = firrtl.subindex %[[DATA_FIELD]][0] : !firrtl.vector<uint<8>, 2>
            %data_0 = firrtl.subindex %data_field[0] : !firrtl.vector<uint<8>, 2>

            // Test 4: Vector of bundles
            // CHECK: %[[VEC_BUNDLE:.*]] = firrtl.invalidvalue : !firrtl.vector<bundle<x: uint<4>, y: uint<4>>, 3>
            %vec_bundle = firrtl.invalidvalue : !firrtl.vector<bundle<x: uint<4>, y: uint<4>>, 3>

            // CHECK: %[[VB_ELEM:.*]] = firrtl.subindex %[[VEC_BUNDLE]][0] : !firrtl.vector<bundle<x: uint<4>, y: uint<4>>, 3>
            %vb_elem = firrtl.subindex %vec_bundle[0] : !firrtl.vector<bundle<x: uint<4>, y: uint<4>>, 3>

            // CHECK: %[[VB_X:.*]] = firrtl.subfield %[[VB_ELEM]][x] : !firrtl.bundle<x: uint<4>, y: uint<4>>
            %vb_x = firrtl.subfield %vb_elem[x] : !firrtl.bundle<x: uint<4>, y: uint<4>>
        }

        // Test module with rule using bundle types
        // CHECK: cmt2.module @BundleInRule
        cmt2.module @BundleInRule() {
            // CHECK: cmt2.rule @test
            cmt2.rule @test() {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } {
                // Create bundle and manipulate fields
                %bundle = firrtl.invalidvalue : !firrtl.bundle<addr: uint<32>, data: uint<64>>
                %addr = firrtl.subfield %bundle[addr] : !firrtl.bundle<addr: uint<32>, data: uint<64>>
                %data = firrtl.subfield %bundle[data] : !firrtl.bundle<addr: uint<32>, data: uint<64>>

                %c0 = firrtl.constant 0 : !firrtl.uint<32>
                %c1_data = firrtl.constant 1 : !firrtl.uint<64>

                cmt2.return
            }
        }

        // Test module with method returning bundle
        // CHECK: cmt2.module @BundleMethod
        cmt2.module @BundleMethod() {
            // CHECK: cmt2.value @getBundle () -> (!firrtl.bundle<a: uint<8>, b: uint<8>>)
            cmt2.value @getBundle() -> (!firrtl.bundle<a: uint<8>, b: uint<8>>) {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } {
                %bundle = firrtl.invalidvalue : !firrtl.bundle<a: uint<8>, b: uint<8>>
                cmt2.return %bundle : !firrtl.bundle<a: uint<8>, b: uint<8>>
            }
        }
    }
}
