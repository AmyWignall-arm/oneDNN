/*******************************************************************************
* Copyright 2021-2023 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#ifndef CPU_X64_JIT_BRDGMM_KERNEL_HPP
#define CPU_X64_JIT_BRDGMM_KERNEL_HPP

#include "common/c_types_map.hpp"
#include "common/nstl.hpp"
#include "common/type_helpers.hpp"
#include "common/utils.hpp"

#include "cpu/x64/brgemm/brgemm_types.hpp"
#include "cpu/x64/cpu_barrier.hpp"
#include "cpu/x64/injectors/jit_uni_postops_injector.hpp"
#include "cpu/x64/jit_avx512_core_bf16cvt.hpp"
#include "cpu/x64/jit_generator.hpp"

namespace dnnl {
namespace impl {
namespace cpu {
namespace x64 {

template <cpu_isa_t isa, typename Wmm>
struct jit_brdgmm_kernel_base_t : public jit_generator {
    jit_brdgmm_kernel_base_t(const brgemm_t &abrd);

    DECLARE_CPU_JIT_AUX_FUNCTIONS(jit_brdgmm_kernel_base_t)

    brgemm_t brg;

    static bool is_fast_vnni_int8(const brgemm_t &brg) {
        return brg.is_dgmm && brg.is_int8 && brg.isa_impl == avx512_core_vnni
                && brg.ldb_tail /*n_vlen_tail*/ == 0;
    }

    static int get_aux_vmm_count(const brgemm_t &brg) {
        // TODO: it would be nice to have a single code path between
        // this index counting and the one from the constructor.
        int vmm_idx_count = 0;
        if (is_fast_vnni_int8(brg)) vmm_idx_count++;
        if (brg.req_s8s8_compensation) vmm_idx_count++;
        if (brg.zp_type_a != brgemm_broadcast_t::none) {
            vmm_idx_count++;
            if (utils::one_of(brg.isa_impl, avx2_vnni, avx2_vnni_2))
                vmm_idx_count++; // need extra vmm for src_zp broadcast
        } else if (brg.with_sum && (!is_superset(brg.isa_impl, avx512_core))) {
            const bool p_sum_scale_reg_set = brg.sum_scale != 1.f;
            if (p_sum_scale_reg_set)
                vmm_idx_count++; // need extra vmm for broadcast
        }
        return vmm_idx_count;
    }

private:
    // note: this kernel doesn't yet support TMM's. We differentiate Wmm and Vmm
    // just to follow same template style as brgemm_kernel.
    using Vmm =
            typename utils::conditional<std::is_same<Wmm, Xbyak::Tmm>::value,
                    Xbyak::Zmm, Wmm>::type;
    using Vmm_low_t = typename vreg_traits<Vmm>::Vmm_lower_t;
    static constexpr cpu_isa_t po_isa_t = utils::map(isa, avx512_core, avx2,
            avx2, avx2_vnni, avx2, avx2_vnni_2, avx2_vnni_2, avx512_core_fp16,
            avx512_core_fp16);
    using po_injector_t = injector::jit_uni_postops_injector_t<po_isa_t, Vmm>;
    std::unique_ptr<po_injector_t> postops_injector_;
    std::unique_ptr<bf16_emulation_t> bf16_emu_;

    Xbyak::Label permute_index_table;

    enum class compute_pad_kernel_t { s8s8_kernel, zero_point_kernel };

    using reg64_t = const Xbyak::Reg64;
    // Register decomposition
    const reg64_t param1 = abi_param1;
    const reg64_t reg_A = abi_not_param1;
    const reg64_t reg_B = r8;
    const reg64_t reg_aux_batch_addr = r15;
    const reg64_t reg_BS = rsi;

    // loop variables
    const reg64_t reg_BS_loop = r12;
    const reg64_t reg_aux_M = r13;
    const reg64_t reg_aux_D = rbx;
    const reg64_t reg_aux_C = rdx;
    const reg64_t reg_aux_A = r10;
    const reg64_t reg_aux_B = abi_param1;
    const reg64_t reg_aux1_A = reg_A; // brgemm_strd
    const reg64_t reg_aux1_B = reg_B; // brgemm_strd
    const reg64_t reg_a_offset = r9;
    const reg64_t reg_aux_N = r11;

    const reg64_t reg_aux_A_vpad_top = r14;
    const reg64_t reg_aux_A_vpad_bottom = rbp;

    const reg64_t reg_table_base = rax;
    const reg64_t reg_tmp = reg_table_base;
    const reg64_t reg_total_padding = reg_table_base;
    const reg64_t reg_aux_bias = reg_table_base;
    const reg64_t reg_aux_scales = reg_table_base;
    const reg64_t reg_aux_dst_scales = reg_table_base;
    const reg64_t reg_dst_zero_point = reg_table_base;
    const reg64_t reg_src_zero_point = reg_table_base;
    const reg64_t reg_zp_compensation = reg_aux_A_vpad_bottom;
    const reg64_t reg_binary_params = abi_param1; // default for binary ops
    const reg64_t reg_ptr_sum_scale = reg_aux_A_vpad_top;
    const reg64_t reg_ptr_sum_zp = reg_aux_A_vpad_bottom;
    const reg64_t reg_s8s8_comp = reg_aux_A_vpad_top;

    Xbyak::Opmask k_mask = Xbyak::Opmask(2);
    Xbyak::Opmask k_tail_mask = Xbyak::Opmask(3);
    Xbyak::Opmask kblend_mask = Xbyak::Opmask(4);

    /* used for bfloat16 */
    reg64_t bf16_emu_scratch = reg_table_base;
    Xbyak::Zmm bf16_emu_reserv_1 = Xbyak::Zmm(0);
    Xbyak::Zmm bf16_emu_reserv_2 = Xbyak::Zmm(1);
    Xbyak::Zmm bf16_emu_reserv_3 = Xbyak::Zmm(2);
    Xbyak::Zmm bf16_emu_reserv_4 = Xbyak::Zmm(3);
    // note 1: zmm reserv_5 is not necessary since it's only used for
    // 'vdpbf16ps'
    // note 2: zmm0 collides with vmm_permute, hence need to write this value
    // before every loop.

    const int simd_w_;
    const int max_vmms_;
    const bool compute_dst_zp_, compute_src_zp_;
    const bool compute_compensation_; // code-path for either s8s8 or src_zp
    const bool has_vpad_; // vertical padding w.r.t. M dimension
    const bool has_bpad_; // batch pad is computed for the overlap between the
            // weights and input height padding

    int idx_vmm_permute_ = -1;
    int idx_vmm_shift_ = -1;
    int idx_vmm_zp_comp_ = -1;
    int idx_vmm_bcast_ = -1;
    int idx_vmm_s8s8_comp_ = -1;
    int vmm_idx_count_ = 0;

    constexpr static int reg_batch0_addr_offs_ = 0;
    constexpr static int reg_bias_offs_ = 8;
    constexpr static int reg_scales_offs_ = 16;
    constexpr static int reg_A_offs_ = 24; // brgemm_strd
    constexpr static int reg_B_offs_ = 32; // brgemm_strd
    constexpr static int abi_param1_offs_ = 40;
    constexpr static int reg_dst_scales_offs_ = 48;
    constexpr static int reg_s8s8_comp_offs_ = 56;
    constexpr static int dst_zp_value_ = 64;
    constexpr static int src_zp_value_ = 72;
    constexpr static int zp_compensation_ = 80;
    constexpr static int stack_space_needed_ = 88;

    bool with_binary_non_scalar_bcast_ = false;

    inline int M() { return brg.bcast_dim; }
    inline int N() { return brg.load_dim; }
    inline int m_block1() { return brg.bd_block; }
    inline int nb_m_block1() { return brg.bdb; }
    inline int m_block1_tail() { return brg.bdb_tail; }
    inline int m_block2() { return brg.bd_block2; }
    inline int nb_m_block2() { return brg.bdb2; }
    inline int m_block2_tail() { return brg.bdb2_tail; }

    inline int n_block1() { return brg.ld_block; }
    inline int nb_n_block1() { return brg.ldb; }
    inline int n_block1_tail() { return brg.ldb_tail; }
    inline int n_block2() { return brg.ld_block2; }
    inline int nb_n_block2() { return brg.ldb2; }
    inline int n_block2_tail() { return brg.ldb2_tail; }

    int tail_length() { return n_block1_tail() % simd_w_; }
    bool is_fma_embd() { return brg.is_f32 && is_superset(isa, avx512_core); }
    bool is_fast_vnni_int8() { return is_fast_vnni_int8(brg); }

    bool req_vmm_reload() { return brg.is_bf16_emu; }
    bool assign_data_vmm_once() { return !req_vmm_reload(); }

    int vnni_substep() {
        return brg.isa_impl == avx2_vnni_2 && brg.is_xf16() ? 2 : 1;
    }
    int get_substep_simd(int n_i, int v_i, bool has_n_tail) {
        const int last_n_block_sz
                = n_block2_tail() > 0 ? n_block2_tail() : n_block2();
        if (has_n_tail && n_i + 1 == last_n_block_sz) {
            return nstl::min(simd_w_, n_block1_tail() - v_i * simd_w_);
        } else {
            return simd_w_;
        }
    }
    Vmm vmm_a() { return Vmm(get_vmm_base_idx()); }
    Vmm vmm_b(int bi = 0) {
        return Vmm(get_vmm_base_idx() + !is_fma_embd() + bi);
    }
    Vmm accm(int m_blocks, int n_blocks, int m, int n, int vnni_idx) {
        assert(m_blocks <= m_block2() && m < m_blocks);
        assert(n_blocks <= n_block2() && n < n_blocks);
        const int accm_start = max_vmms_ - m_blocks * n_blocks * vnni_substep();
        const int accm_rel_idx
                = m * n_blocks * vnni_substep() + n * vnni_substep() + vnni_idx;
        const int idx = accm_start + accm_rel_idx;
        assert(idx < max_vmms_ && idx > vmm_b(0).getIdx());
        return Vmm(idx);
    }
    int get_vmm_base_idx() { return get_aux_vmm_count(brg); }
    Vmm vmm_permute() {
        assert(idx_vmm_permute_ >= 0);
        return Vmm(idx_vmm_permute_);
    }
    Vmm vmm_shift() { // -128's
        assert(idx_vmm_shift_ >= 0);
        return Vmm(idx_vmm_shift_);
    }
    Vmm vmm_s8s8_comp() {
        assert(idx_vmm_s8s8_comp_ >= 0);
        return Vmm(idx_vmm_s8s8_comp_);
    }
    Vmm vmm_zp_comp() {
        assert(idx_vmm_zp_comp_ >= 0);
        return Vmm(idx_vmm_zp_comp_);
    }
    Vmm vmm_bcast() {
        assert(idx_vmm_bcast_ >= 0);
        return Vmm(idx_vmm_bcast_);
    }
    Vmm vmm_tmp(int i) {
        const int idx
                = max_vmms_ - m_block2() * n_block2() * vnni_substep() - 1 - i;
        assert(idx > (is_fast_vnni_int8() - 1));
        return Vmm(idx);
    }

    template <typename U>
    U maybe_mask(const U umm_in, bool mask_flag, bool store);
    void init_masks();
    void read_params();
    void load_permute_vmm();
    void load_accumulators(int m_blocks, int n_blocks);
    void restore_A_B_matrices();
    void set_A_B_matrices();
    void advance_A_B_matrices();
    void load_a(Vmm vmma, int m_i, int n_i, int v_i, bool has_n_tail);
    void load_b(
            Vmm vmmb, int n_i, int v_i, bool has_n_tail, bool wei_zp = false);
    void comp_dot_product(compute_pad_kernel_t kernel_type, Vmm vmm_acc,
            Vmm vmmb); // int8 compensation dot_product (zp and s8s8)
    void pad_comp_kernel(compute_pad_kernel_t kernel_type, int m_blocks,
            int n_blocks, int padding, const Xbyak::Reg64 reg_pad,
            const std::function<int(int)> &get_mi, bool has_tail = false);
    void vertical_pad_kernel(int m_blocks, int n_blocks, bool has_tail);
    void batch_pad_kernel(int m_blocks, int n_blocks, bool has_tail = false);
    void brdgmm_microkernel(int m_blocks, int n_blocks, bool has_top_padding,
            bool has_bottom_padding, bool has_tail = false);
    void compute_loop();
    void get_batch_padding_info();
    void get_vertical_padding_info(const int m_blocks);
    void call_brdgmm_microkernel(
            const int m_blocks, const int n_blocks, bool has_n_tail);
    void batch_loop(const int m_blocks, const int n_blocks, bool has_n_tail);
    void cvt2ps(data_type_t type_in, const Vmm vmm_in, const Xbyak::Operand &op,
            bool mask_flag, bool store);
    void apply_post_ops(int m_blocks, int n_blocks, bool has_n_tail);
    void maybe_transpose_interleaved_vnni_to_plain(
            int m_blocks, int n_blocks, bool has_n_tail);
    void compute_int8_compensation(int m_blocks, int n_blocks, bool has_n_tail);
    void store_accumulators(int m_blocks, int n_blocks, bool has_n_tail);
    void store_accumulators_without_post_ops(
            int m_blocks, int n_blocks, bool has_n_tail);
    void store_accumulators_apply_post_ops(
            int m_blocks, int n_blocks, bool has_n_tail);
    bool check_effective_padding() { return has_vpad_ && M() > m_block2(); }
    int oc_logical_offset(int n) { return n * n_block1(); }
    int A_offset(int m, int n) {
        return brg.typesize_A * (m * brg.LDA + n * n_block1());
    }
    int B_offset(int n) { return brg.typesize_B * n * n_block1(); }
    int C_offset(int m, int n, int v) {
        return brg.typesize_C * (m * brg.LDC + n * n_block1() + v * simd_w_);
    }
    int D_offset(int m, int n, int v) {
        return brg.typesize_D * (m * brg.LDD + n * n_block1() + v * simd_w_);
    }
    int bias_offset(int n, int v) {
        return brg.typesize_bias * (n * n_block1() + v * simd_w_);
    }
    int scales_offset(int n, int v) {
        return sizeof(float) * brg.is_oc_scale * (n * n_block1() + v * simd_w_);
    }
    size_t comp_offset(int n) { return sizeof(int32_t) * n * n_block1(); }

    void generate() override;
};

} // namespace x64
} // namespace cpu
} // namespace impl
} // namespace dnnl

#endif
