// Copyright (c) 2023 The InterpretML Contributors
// Licensed under the MIT license.
// Author: Paul Koch <code@koch.ninja>

#include "precompiled_header_cpp.hpp"

#include <cmath> // exp, log
#include <limits> // numeric_limits
#include <type_traits> // is_unsigned
#include <immintrin.h> // SIMD.  Do not include in precompiled_header_cpp.hpp!

#include "libebm.h"
#include "logging.h"
#include "common_c.h"
#include "bridge_c.h"
#include "zones.h"

#include "common_cpp.hpp"
#include "bridge_cpp.hpp"

#include "Registration.hpp"
#include "Objective.hpp"

#include "approximate_math.hpp"
#include "compute_wrapper.hpp"

namespace DEFINED_ZONE_NAME {
#ifndef DEFINED_ZONE_NAME
#error DEFINED_ZONE_NAME must be defined
#endif // DEFINED_ZONE_NAME

static constexpr size_t k_cAlignment = 64;

struct alignas(k_cAlignment) Avx512f_32_Float;

struct alignas(k_cAlignment) Avx512f_32_Int final {
   friend Avx512f_32_Float;
   friend inline Avx512f_32_Float IfEqual(const Avx512f_32_Int & cmp1, const Avx512f_32_Int & cmp2, const Avx512f_32_Float & trueVal, const Avx512f_32_Float & falseVal) noexcept;

   using T = uint32_t;
   using TPack = __m512i;
   static_assert(std::is_unsigned<T>::value, "T must be an unsigned integer type");
   static_assert(std::is_same<UIntBig, T>::value || std::is_same<UIntSmall, T>::value,
      "T must be either UIntBig or UIntSmall");
   static constexpr bool k_bCpu = false;
   static constexpr int k_cSIMDShift = 4;
   static constexpr int k_cSIMDPack = 1 << k_cSIMDShift;

   ATTRIBUTE_WARNING_DISABLE_UNINITIALIZED_MEMBER
   inline Avx512f_32_Int() noexcept {
   }

   inline Avx512f_32_Int(const T & val) noexcept : m_data(_mm512_set1_epi32(val)) {
   }

   inline static Avx512f_32_Int Load(const T * const a) noexcept {
      return Avx512f_32_Int(_mm512_load_si512(a));
   }

   inline void Store(T * const a) const noexcept {
      _mm512_store_si512(a, m_data);
   }

   inline static Avx512f_32_Int LoadBytes(const uint8_t * const a) noexcept {
      return Avx512f_32_Int(_mm512_cvtepu8_epi32(_mm_load_si128(reinterpret_cast<const __m128i *>(a))));
   }

   template<typename TFunc>
   static inline void Execute(const TFunc & func, const Avx512f_32_Int & val0) noexcept {
      alignas(k_cAlignment) T a0[k_cSIMDPack];
      val0.Store(a0);

      // no loops because this will disable optimizations for loops in the caller
      func(0, a0[0]);
      func(1, a0[1]);
      func(2, a0[2]);
      func(3, a0[3]);
      func(4, a0[4]);
      func(5, a0[5]);
      func(6, a0[6]);
      func(7, a0[7]);
      func(8, a0[8]);
      func(9, a0[9]);
      func(10, a0[10]);
      func(11, a0[11]);
      func(12, a0[12]);
      func(13, a0[13]);
      func(14, a0[14]);
      func(15, a0[15]);
   }

   inline static Avx512f_32_Int MakeIndexes() noexcept {
      return Avx512f_32_Int(_mm512_set_epi32(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0));
   }

   inline Avx512f_32_Int operator+ (const Avx512f_32_Int & other) const noexcept {
      return Avx512f_32_Int(_mm512_add_epi32(m_data, other.m_data));
   }

   inline Avx512f_32_Int operator* (const T & other) const noexcept {
      return Avx512f_32_Int(_mm512_mullo_epi32(m_data, _mm512_set1_epi32(other)));
   }

   inline Avx512f_32_Int operator>> (int shift) const noexcept {
      return Avx512f_32_Int(_mm512_srli_epi32(m_data, shift));
   }

   inline Avx512f_32_Int operator<< (int shift) const noexcept {
      return Avx512f_32_Int(_mm512_slli_epi32(m_data, shift));
   }

   inline Avx512f_32_Int operator& (const Avx512f_32_Int & other) const noexcept {
      return Avx512f_32_Int(_mm512_and_si512(m_data, other.m_data));
   }

private:
   inline Avx512f_32_Int(const TPack & data) noexcept : m_data(data) {
   }

   TPack m_data;
};
static_assert(std::is_standard_layout<Avx512f_32_Int>::value && std::is_trivially_copyable<Avx512f_32_Int>::value,
   "This allows offsetof, memcpy, memset, inter-language, GPU and cross-machine use where needed");


struct alignas(k_cAlignment) Avx512f_32_Float final {
   using T = float;
   using TPack = __m512;
   using TInt = Avx512f_32_Int;
   static_assert(std::is_same<FloatBig, T>::value || std::is_same<FloatSmall, T>::value,
      "T must be either FloatBig or FloatSmall");
   static constexpr bool k_bCpu = TInt::k_bCpu;
   static constexpr int k_cSIMDShift = TInt::k_cSIMDShift;
   static constexpr int k_cSIMDPack = TInt::k_cSIMDPack;

   ATTRIBUTE_WARNING_DISABLE_UNINITIALIZED_MEMBER
   inline Avx512f_32_Float() noexcept {
   }

   inline Avx512f_32_Float(const double val) noexcept : m_data(_mm512_set1_ps(static_cast<T>(val))) {
   }
   inline Avx512f_32_Float(const float val) noexcept : m_data(_mm512_set1_ps(static_cast<T>(val))) {
   }
   inline Avx512f_32_Float(const int val) noexcept : m_data(_mm512_set1_ps(static_cast<T>(val))) {
   }


   inline Avx512f_32_Float operator+() const noexcept {
      return *this;
   }

   inline Avx512f_32_Float operator-() const noexcept {
      return Avx512f_32_Float(_mm512_castsi512_ps(_mm512_xor_si512(_mm512_castps_si512(m_data), _mm512_set1_epi32(0x80000000))));
   }


   inline Avx512f_32_Float operator+ (const Avx512f_32_Float & other) const noexcept {
      return Avx512f_32_Float(_mm512_add_ps(m_data, other.m_data));
   }

   inline Avx512f_32_Float operator- (const Avx512f_32_Float & other) const noexcept {
      return Avx512f_32_Float(_mm512_sub_ps(m_data, other.m_data));
   }

   inline Avx512f_32_Float operator* (const Avx512f_32_Float & other) const noexcept {
      return Avx512f_32_Float(_mm512_mul_ps(m_data, other.m_data));
   }

   inline Avx512f_32_Float operator/ (const Avx512f_32_Float & other) const noexcept {
      return Avx512f_32_Float(_mm512_div_ps(m_data, other.m_data));
   }


   inline Avx512f_32_Float & operator+= (const Avx512f_32_Float & other) noexcept {
      *this = (*this) + other;
      return *this;
   }

   inline Avx512f_32_Float & operator-= (const Avx512f_32_Float & other) noexcept {
      *this = (*this) - other;
      return *this;
   }

   inline Avx512f_32_Float & operator*= (const Avx512f_32_Float & other) noexcept {
      *this = (*this) * other;
      return *this;
   }

   inline Avx512f_32_Float & operator/= (const Avx512f_32_Float & other) noexcept {
      *this = (*this) / other;
      return *this;
   }


   friend inline Avx512f_32_Float operator+ (const double val, const Avx512f_32_Float & other) noexcept {
      return Avx512f_32_Float(val) + other;
   }

   friend inline Avx512f_32_Float operator- (const double val, const Avx512f_32_Float & other) noexcept {
      return Avx512f_32_Float(val) - other;
   }

   friend inline Avx512f_32_Float operator* (const double val, const Avx512f_32_Float & other) noexcept {
      return Avx512f_32_Float(val) * other;
   }

   friend inline Avx512f_32_Float operator/ (const double val, const Avx512f_32_Float & other) noexcept {
      return Avx512f_32_Float(val) / other;
   }


   friend inline Avx512f_32_Float operator+ (const float val, const Avx512f_32_Float & other) noexcept {
      return Avx512f_32_Float(val) + other;
   }

   friend inline Avx512f_32_Float operator- (const float val, const Avx512f_32_Float & other) noexcept {
      return Avx512f_32_Float(val) - other;
   }

   friend inline Avx512f_32_Float operator* (const float val, const Avx512f_32_Float & other) noexcept {
      return Avx512f_32_Float(val) * other;
   }

   friend inline Avx512f_32_Float operator/ (const float val, const Avx512f_32_Float & other) noexcept {
      return Avx512f_32_Float(val) / other;
   }


   inline static Avx512f_32_Float Load(const T * const a) noexcept {
      return Avx512f_32_Float(_mm512_load_ps(a));
   }

   inline void Store(T * const a) const noexcept {
      _mm512_store_ps(a, m_data);
   }

   inline static Avx512f_32_Float Load(const T * const a, const TInt & i) noexcept {
      // i is treated as signed, so we should only use the lower 31 bits otherwise we'll read from memory before a
      return Avx512f_32_Float(_mm512_i32gather_ps(i.m_data, a, sizeof(a[0])));
   }

   inline void Store(T * const a, const TInt & i) const noexcept {
      // i is treated as signed, so we should only use the lower 31 bits otherwise we'll read from memory before a
      _mm512_i32scatter_ps(a, i.m_data, m_data, sizeof(a[0]));
   }

   template<typename TFunc>
   friend inline Avx512f_32_Float ApplyFunc(const TFunc & func, const Avx512f_32_Float & val) noexcept {
      alignas(k_cAlignment) T aTemp[k_cSIMDPack];
      val.Store(aTemp);

      aTemp[0] = func(aTemp[0]);
      aTemp[1] = func(aTemp[1]);
      aTemp[2] = func(aTemp[2]);
      aTemp[3] = func(aTemp[3]);
      aTemp[4] = func(aTemp[4]);
      aTemp[5] = func(aTemp[5]);
      aTemp[6] = func(aTemp[6]);
      aTemp[7] = func(aTemp[7]);
      aTemp[8] = func(aTemp[8]);
      aTemp[9] = func(aTemp[9]);
      aTemp[10] = func(aTemp[10]);
      aTemp[11] = func(aTemp[11]);
      aTemp[12] = func(aTemp[12]);
      aTemp[13] = func(aTemp[13]);
      aTemp[14] = func(aTemp[14]);
      aTemp[15] = func(aTemp[15]);

      return Load(aTemp);
   }

   template<typename TFunc>
   static inline void Execute(const TFunc & func) noexcept {
      func(0);
      func(1);
      func(2);
      func(3);
      func(4);
      func(5);
      func(6);
      func(7);
      func(8);
      func(9);
      func(10);
      func(11);
      func(12);
      func(13);
      func(14);
      func(15);
   }

   template<typename TFunc>
   static inline void Execute(const TFunc & func, const Avx512f_32_Float & val0) noexcept {
      alignas(k_cAlignment) T a0[k_cSIMDPack];
      val0.Store(a0);

      func(0, a0[0]);
      func(1, a0[1]);
      func(2, a0[2]);
      func(3, a0[3]);
      func(4, a0[4]);
      func(5, a0[5]);
      func(6, a0[6]);
      func(7, a0[7]);
      func(8, a0[8]);
      func(9, a0[9]);
      func(10, a0[10]);
      func(11, a0[11]);
      func(12, a0[12]);
      func(13, a0[13]);
      func(14, a0[14]);
      func(15, a0[15]);
   }

   template<typename TFunc>
   static inline void Execute(const TFunc & func, const Avx512f_32_Float & val0, const Avx512f_32_Float & val1) noexcept {
      alignas(k_cAlignment) T a0[k_cSIMDPack];
      val0.Store(a0);
      alignas(k_cAlignment) T a1[k_cSIMDPack];
      val1.Store(a1);

      func(0, a0[0], a1[0]);
      func(1, a0[1], a1[1]);
      func(2, a0[2], a1[2]);
      func(3, a0[3], a1[3]);
      func(4, a0[4], a1[4]);
      func(5, a0[5], a1[5]);
      func(6, a0[6], a1[6]);
      func(7, a0[7], a1[7]);
      func(8, a0[8], a1[8]);
      func(9, a0[9], a1[9]);
      func(10, a0[10], a1[10]);
      func(11, a0[11], a1[11]);
      func(12, a0[12], a1[12]);
      func(13, a0[13], a1[13]);
      func(14, a0[14], a1[14]);
      func(15, a0[15], a1[15]);
   }

   friend inline Avx512f_32_Float IfLess(const Avx512f_32_Float & cmp1, const Avx512f_32_Float & cmp2, const Avx512f_32_Float & trueVal, const Avx512f_32_Float & falseVal) noexcept {
      const __mmask16 mask = _mm512_cmp_ps_mask(cmp1.m_data, cmp2.m_data, _CMP_LT_OQ);
      return Avx512f_32_Float(_mm512_mask_blend_ps(mask, falseVal.m_data, trueVal.m_data));
   }

   friend inline Avx512f_32_Float IfEqual(const Avx512f_32_Float & cmp1, const Avx512f_32_Float & cmp2, const Avx512f_32_Float & trueVal, const Avx512f_32_Float & falseVal) noexcept {
      const __mmask16 mask = _mm512_cmp_ps_mask(cmp1.m_data, cmp2.m_data, _CMP_EQ_OQ);
      return Avx512f_32_Float(_mm512_mask_blend_ps(mask, falseVal.m_data, trueVal.m_data));
   }

   friend inline Avx512f_32_Float IfNaN(const Avx512f_32_Float & cmp, const Avx512f_32_Float & trueVal, const Avx512f_32_Float & falseVal) noexcept {
      // rely on the fact that a == a can only be false if a is a NaN
      //
      // TODO: _mm256_cmp_ps has a latency of 4 and a throughput of 0.5.  It might be faster to convert to integers,
      //       use an AND with _mm256_and_si256 to select just the NaN bits, then compare to zero with 
      //       _mm256_cmpeq_epi32, but that has an overall latency of 2 and a throughput of 0.83333, which is lower 
      //       throughput, so experiment with this
      return IfEqual(cmp, cmp, falseVal, trueVal);
   }

   friend inline Avx512f_32_Float IfEqual(const Avx512f_32_Int & cmp1, const Avx512f_32_Int & cmp2, const Avx512f_32_Float & trueVal, const Avx512f_32_Float & falseVal) noexcept {
      const __mmask16 mask = _mm512_cmpeq_epi32_mask(cmp1.m_data, cmp2.m_data);
      return Avx512f_32_Float(_mm512_mask_blend_ps(mask, falseVal.m_data, trueVal.m_data));
   }

   friend inline Avx512f_32_Float Abs(const Avx512f_32_Float & val) noexcept {
      return Avx512f_32_Float(_mm512_castsi512_ps(_mm512_and_si512(_mm512_castps_si512(val.m_data), _mm512_set1_epi32(0x7FFFFFFF))));
   }

   friend inline Avx512f_32_Float FastApproxReciprocal(const Avx512f_32_Float & val) noexcept {
#ifdef FAST_DIVISION
      return Avx512f_32_Float(_mm512_rcp14_ps(val.m_data));
#else // FAST_DIVISION
      return Avx512f_32_Float(1.0) / val;
#endif // FAST_DIVISION
   }

   friend inline Avx512f_32_Float FastApproxDivide(const Avx512f_32_Float & dividend, const Avx512f_32_Float & divisor) noexcept {
#ifdef FAST_DIVISION
      return dividend * FastApproxReciprocal(divisor);
#else // FAST_DIVISION
      return dividend / divisor;
#endif // FAST_DIVISION
   }

   friend inline Avx512f_32_Float FusedMultiplyAdd(const Avx512f_32_Float & mul1, const Avx512f_32_Float & mul2, const Avx512f_32_Float & add) noexcept {
      return Avx512f_32_Float(_mm512_fmadd_ps(mul1.m_data, mul2.m_data, add.m_data));
   }

   friend inline Avx512f_32_Float FusedNegateMultiplyAdd(const Avx512f_32_Float & mul1, const Avx512f_32_Float & mul2, const Avx512f_32_Float & add) noexcept {
      // equivalent to: -(mul1 * mul2) + add
      return Avx512f_32_Float(_mm512_fnmadd_ps(mul1.m_data, mul2.m_data, add.m_data));
   }

   friend inline Avx512f_32_Float Sqrt(const Avx512f_32_Float & val) noexcept {
      return Avx512f_32_Float(_mm512_sqrt_ps(val.m_data));
   }

   friend inline Avx512f_32_Float Exp(const Avx512f_32_Float & val) noexcept {
      return ApplyFunc([](T x) { return std::exp(x); }, val);
   }

   friend inline Avx512f_32_Float Log(const Avx512f_32_Float & val) noexcept {
      return ApplyFunc([](T x) { return std::log(x); }, val);
   }

   template<
      bool bNegateInput = false,
      bool bNaNPossible = true,
      bool bUnderflowPossible = true,
      bool bOverflowPossible = true,
      bool bSpecialCaseZero = false
   >
      static inline Avx512f_32_Float ApproxExp(
         const Avx512f_32_Float & val,
         const int32_t addExpSchraudolphTerm = k_expTermZeroMeanErrorForSoftmaxWithZeroedLogit
      ) noexcept {
      // This code will make no sense until you read the Nicol N. Schraudolph paper:
      // https://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.9.4508&rep=rep1&type=pdf
      // and also see approximate_math.hpp
#ifdef FAST_LOG
      static constexpr float signedExpMultiple = bNegateInput ? -k_expMultiple : k_expMultiple;
#ifdef EXP_INT_SIMD
      const __m512 product = (val * signedExpMultiple).m_data;
      const __m512i retInt = _mm512_add_epi32(_mm512_cvttps_epi32(product), _mm512_set1_epi32(addExpSchraudolphTerm));
#else // EXP_INT_SIMD
      const __m512 retFloat = FusedMultiplyAdd(val, signedExpMultiple, static_cast<T>(addExpSchraudolphTerm)).m_data;
      const __m512i retInt = _mm512_cvttps_epi32(retFloat);
#endif // EXP_INT_SIMD
      Avx512f_32_Float result = Avx512f_32_Float(_mm512_castsi512_ps(retInt));
      if(bSpecialCaseZero) {
         result = IfEqual(0.0, val, 1.0, result);
      }
      if(bOverflowPossible) {
         if(bNegateInput) {
            result = IfLess(val, static_cast<T>(-k_expOverflowPoint), std::numeric_limits<T>::infinity(), result);
         } else {
            result = IfLess(static_cast<T>(k_expOverflowPoint), val, std::numeric_limits<T>::infinity(), result);
         }
      }
      if(bUnderflowPossible) {
         if(bNegateInput) {
            result = IfLess(static_cast<T>(-k_expUnderflowPoint), val, 0.0, result);
         } else {
            result = IfLess(val, static_cast<T>(k_expUnderflowPoint), 0.0, result);
         }
      }
      if(bNaNPossible) {
         result = IfNaN(val, val, result);
      }
      return result;
#else // FAST_LOG
      UNUSED(addExpSchraudolphTerm);
      return Exp(bNegateInput ? -val : val);
#endif // FAST_LOG
      }

   template<
      bool bNegateOutput = false,
      bool bNaNPossible = true,
      bool bNegativePossible = false,
      bool bZeroPossible = false, // if false, positive zero returns a big negative number, negative zero returns a big positive number
      bool bPositiveInfinityPossible = false // if false, +inf returns a big positive number.  If val can be a double that is above the largest representable float, then setting this is necessary to avoid undefined behavior
   >
      static inline Avx512f_32_Float ApproxLog(
         const Avx512f_32_Float & val,
         const float addLogSchraudolphTerm = k_logTermLowerBoundInputCloseToOne
      ) noexcept {
      // This code will make no sense until you read the Nicol N. Schraudolph paper:
      // https://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.9.4508&rep=rep1&type=pdf
      // and also see approximate_math.hpp
#ifdef FAST_LOG
      const __m512i retInt = _mm512_castps_si512(val.m_data);
      Avx512f_32_Float result = Avx512f_32_Float(_mm512_cvtepi32_ps(retInt));
      if(bNegateOutput) {
         result = FusedMultiplyAdd(result, -k_logMultiple, -addLogSchraudolphTerm);
      } else {
         result = FusedMultiplyAdd(result, k_logMultiple, addLogSchraudolphTerm);
      }
      if(bPositiveInfinityPossible) {
         result = IfEqual(std::numeric_limits<T>::infinity(), val, bNegateOutput ? -std::numeric_limits<T>::infinity() : std::numeric_limits<T>::infinity(), result);
      }
      if(bZeroPossible) {
         result = IfEqual(0.0, val, bNegateOutput ? std::numeric_limits<T>::infinity() : -std::numeric_limits<T>::infinity(), result);
      }
      if(bNegativePossible) {
         result = IfLess(val, 0.0, std::numeric_limits<T>::quiet_NaN(), result);
      }
      if(bNaNPossible) {
         result = IfNaN(val, val, result);
      }
      return result;
#else // FAST_LOG
      UNUSED(addLogSchraudolphTerm);
      const Avx512f_32_Float ret = Log(val);
      return bNegateOutput ? -ret : ret;
#endif // FAST_LOG
   }

   friend inline T Sum(const Avx512f_32_Float & val) noexcept {
      return _mm512_reduce_add_ps(val.m_data);
   }


   template<typename TObjective, size_t cCompilerScores, bool bValidation, bool bWeight, bool bHessian, int cCompilerPack>
   INLINE_RELEASE_TEMPLATED static ErrorEbm OperatorApplyUpdate(const Objective * const pObjective, ApplyUpdateBridge * const pData) noexcept {
      RemoteApplyUpdate<TObjective, cCompilerScores, bValidation, bWeight, bHessian, cCompilerPack>(pObjective, pData);
      return Error_None;
   }


   template<bool bHessian, size_t cCompilerScores, bool bWeight, bool bReplication, int cCompilerPack>
   INLINE_RELEASE_TEMPLATED static ErrorEbm OperatorBinSumsBoosting(BinSumsBoostingBridge * const pParams) noexcept {
      RemoteBinSumsBoosting<Avx512f_32_Float, bHessian, cCompilerScores, bWeight, bReplication, cCompilerPack>(pParams);
      return Error_None;
   }


   template<bool bHessian, size_t cCompilerScores, size_t cCompilerDimensions, bool bWeight>
   INLINE_RELEASE_TEMPLATED static ErrorEbm OperatorBinSumsInteraction(BinSumsInteractionBridge * const pParams) noexcept {
      RemoteBinSumsInteraction<Avx512f_32_Float, bHessian, cCompilerScores, cCompilerDimensions, bWeight>(pParams);
      return Error_None;
   }


private:

   inline Avx512f_32_Float(const TPack & data) noexcept : m_data(data) {
   }

   TPack m_data;
};
static_assert(std::is_standard_layout<Avx512f_32_Float>::value && std::is_trivially_copyable<Avx512f_32_Float>::value,
   "This allows offsetof, memcpy, memset, inter-language, GPU and cross-machine use where needed");

// FIRST, define the RegisterObjective function that we'll be calling from our registrations.  This is a static 
// function, so we can have duplicate named functions in other files and they'll refer to different functions
template<template <typename> class TRegistrable, bool bCpuOnly, typename... Args>
INLINE_ALWAYS static typename std::enable_if<bCpuOnly, std::shared_ptr<const Registration>>::type RegisterObjective(const char * const, const Args &...) {
   return nullptr;
}
template<template <typename> class TRegistrable, bool bCpuOnly, typename... Args>
INLINE_ALWAYS static typename std::enable_if<!bCpuOnly, std::shared_ptr<const Registration>>::type RegisterObjective(const char * const sRegistrationName, const Args &... args) {
   return Register<TRegistrable, Avx512f_32_Float>(bCpuOnly, sRegistrationName, args...);
}

// now include all our special objective registrations which will use the RegisterObjective function we defined above!
#include "objective_registrations.hpp"

INTERNAL_IMPORT_EXPORT_BODY ErrorEbm CreateObjective_Avx512f_32(
   const Config * const pConfig,
   const char * const sObjective,
   const char * const sObjectiveEnd,
   ObjectiveWrapper * const pObjectiveWrapperOut
) {
   ErrorEbm error = ComputeWrapper<Avx512f_32_Float>::FillWrapper(pObjectiveWrapperOut);
   if(Error_None != error) {
      return error;
   }
   return Objective::CreateObjective(&RegisterObjectives, pConfig, sObjective, sObjectiveEnd, pObjectiveWrapperOut);
}

} // DEFINED_ZONE_NAME
