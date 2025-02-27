// Copyright (c) 2023 The InterpretML Contributors
// Licensed under the MIT license.
// Author: Paul Koch <code@koch.ninja>

#include "precompiled_header_cpp.hpp"

#include <stddef.h> // size_t, ptrdiff_t
#include <limits> // numeric_limits
#include <string.h> // memcpy

#include "libebm.h" // ErrorEbm
#include "logging.h" // EBM_ASSERT
#include "common_c.h" // FloatMain
#include "zones.h"

#include "Bin.hpp" // GetBinSize

#include "ebm_internal.hpp" // k_cDimensionsMax
#include "Feature.hpp"
#include "DataSetInteraction.hpp"
#include "InteractionCore.hpp"
#include "InteractionShell.hpp"

namespace DEFINED_ZONE_NAME {
#ifndef DEFINED_ZONE_NAME
#error DEFINED_ZONE_NAME must be defined
#endif // DEFINED_ZONE_NAME

extern void ConvertAddBin(
   const size_t cScores,
   const bool bHessian,
   const size_t cBins,
   const bool bUInt64Src,
   const bool bDoubleSrc,
   const void * const aSrc,
   const bool bUInt64Dest,
   const bool bDoubleDest,
   void * const aAddDest
);

extern void TensorTotalsBuild(
   const bool bHessian,
   const size_t cScores,
   const size_t cRealDimensions,
   const size_t * const acBins,
   BinBase * aAuxiliaryBinsBase,
   BinBase * const aBinsBase
#ifndef NDEBUG
   , BinBase * const aDebugCopyBinsBase
   , const BinBase * const pBinsEndDebug
#endif // NDEBUG
);

extern double PartitionTwoDimensionalInteraction(
   InteractionCore * const pInteractionCore,
   const size_t cRealDimensions,
   const size_t * const acBins,
   const CalcInteractionFlags flags,
   const size_t cSamplesLeafMin,
   BinBase * aAuxiliaryBinsBase,
   BinBase * const aBinsBase
#ifndef NDEBUG
   , const BinBase * const aDebugCopyBinsBase
   , const BinBase * const pBinsEndDebug
#endif // NDEBUG
);

// there is a race condition for decrementing this variable, but if a thread loses the 
// race then it just doesn't get decremented as quickly, which we can live with
static int g_cLogCalcInteractionStrength = 10;

EBM_API_BODY ErrorEbm EBM_CALLING_CONVENTION CalcInteractionStrength(
   InteractionHandle interactionHandle,
   IntEbm countDimensions,
   const IntEbm * featureIndexes,
   CalcInteractionFlags flags,
   IntEbm maxCardinality,
   IntEbm minSamplesLeaf,
   double * avgInteractionStrengthOut
) {
   LOG_COUNTED_N(
      &g_cLogCalcInteractionStrength,
      Trace_Info,
      Trace_Verbose,
      "CalcInteractionStrength: "
      "interactionHandle=%p, "
      "countDimensions=%" IntEbmPrintf ", "
      "featureIndexes=%p, "
      "flags=0x%" UCalcInteractionFlagsPrintf ", "
      "maxCardinality=%" IntEbmPrintf ", "
      "minSamplesLeaf=%" IntEbmPrintf ", "
      "avgInteractionStrengthOut=%p"
      ,
      static_cast<void *>(interactionHandle),
      countDimensions,
      static_cast<const void *>(featureIndexes),
      static_cast<UCalcInteractionFlags>(flags), // signed to unsigned conversion is defined behavior in C++
      maxCardinality,
      minSamplesLeaf,
      static_cast<void *>(avgInteractionStrengthOut)
   );

   ErrorEbm error;

   if(LIKELY(nullptr != avgInteractionStrengthOut)) {
      *avgInteractionStrengthOut = k_illegalGainDouble;
   }

   InteractionShell * const pInteractionShell = InteractionShell::GetInteractionShellFromHandle(interactionHandle);
   if(nullptr == pInteractionShell) {
      // already logged
      return Error_IllegalParamVal;
   }
   LOG_COUNTED_0(
      pInteractionShell->GetPointerCountLogEnterMessages(), 
      Trace_Info, 
      Trace_Verbose, 
      "Entered CalcInteractionStrength"
   );

   if(0 != (static_cast<UCalcInteractionFlags>(flags) & static_cast<UCalcInteractionFlags>(~(
      static_cast<UCalcInteractionFlags>(CalcInteractionFlags_Pure) | 
      static_cast<UCalcInteractionFlags>(CalcInteractionFlags_EnableNewton)
   )))) {
      LOG_0(Trace_Error, "ERROR CalcInteractionStrength flags contains unknown flags. Ignoring extras.");
   }

   size_t cCardinalityMax = std::numeric_limits<size_t>::max(); // set off by default
   if(IntEbm { 0 } <= maxCardinality) {
      if(IntEbm { 0 } != maxCardinality) {
         if(!IsConvertError<size_t>(maxCardinality)) {
            // we can never exceed a size_t number of samples, so let's just set it to the maximum if we were going to 
            // overflow because it will generate the same results as if we used the true number
            cCardinalityMax = static_cast<size_t>(maxCardinality);
         }
      }
   } else {
      LOG_0(Trace_Warning, "WARNING CalcInteractionStrength maxCardinality can't be less than 0. Turning off.");
   }

   size_t cSamplesLeafMin = size_t { 1 }; // this is the min value
   if(IntEbm { 1 } <= minSamplesLeaf) {
      cSamplesLeafMin = static_cast<size_t>(minSamplesLeaf);
      if(IsConvertError<size_t>(minSamplesLeaf)) {
         // we can never exceed a size_t number of samples, so let's just set it to the maximum if we were going to 
         // overflow because it will generate the same results as if we used the true number
         cSamplesLeafMin = std::numeric_limits<size_t>::max();
      }
   } else {
      LOG_0(Trace_Warning, "WARNING CalcInteractionStrength minSamplesLeaf can't be less than 1. Adjusting to 1.");
   }

   if(countDimensions <= IntEbm { 0 }) {
      if(IntEbm { 0 } == countDimensions) {
         LOG_0(Trace_Info, "INFO CalcInteractionStrength empty feature list");
         if(LIKELY(nullptr != avgInteractionStrengthOut)) {
            *avgInteractionStrengthOut = 0.0;
         }
         return Error_None;
      } else {
         LOG_0(Trace_Error, "ERROR CalcInteractionStrength countDimensions must be positive");
         return Error_IllegalParamVal;
      }
   }
   if(nullptr == featureIndexes) {
      LOG_0(Trace_Error, "ERROR CalcInteractionStrength featureIndexes cannot be nullptr if 0 < countDimensions");
      return Error_IllegalParamVal;
   }
   if(IntEbm { k_cDimensionsMax } < countDimensions) {
      LOG_0(Trace_Warning, "WARNING CalcInteractionStrength countDimensions too large and would cause out of memory condition");
      return Error_OutOfMemory;
   }
   size_t cDimensions = static_cast<size_t>(countDimensions);

   InteractionCore * const pInteractionCore = pInteractionShell->GetInteractionCore();

   const ptrdiff_t cClasses = pInteractionCore->GetCountClasses();
   if(ptrdiff_t { 0 } == cClasses || ptrdiff_t { 1 } == cClasses) {
      LOG_0(Trace_Info, "INFO CalcInteractionStrength target with 1 class perfectly predicts the target");
      if(nullptr != avgInteractionStrengthOut) {
         *avgInteractionStrengthOut = 0.0;
      }
      return Error_None;
   }

   const DataSetInteraction * const pDataSet = pInteractionCore->GetDataSetInteraction();
   EBM_ASSERT(nullptr != pDataSet);

   if(size_t { 0 } == pDataSet->GetCountSamples()) {
      // if there are zero samples, there isn't much basis to say whether there are interactions, so just return zero
      LOG_0(Trace_Info, "INFO CalcInteractionStrength zero samples");
      if(nullptr != avgInteractionStrengthOut) {
         *avgInteractionStrengthOut = 0.0;
      }
      return Error_None;
   }

   // TODO : we NEVER use the hessian term (currently) in GradientPair when calculating interaction scores, but we're spending time calculating 
   // it, and it's taking up precious memory.  We should eliminate the hessian term HERE in our datastructures OR we should think whether we can 
   // use the hessian as part of the gain function!!!

   BinSumsInteractionBridge binSums;

   const FeatureInteraction * const aFeatures = pInteractionCore->GetFeatures();
   const IntEbm countFeatures = static_cast<IntEbm>(pInteractionCore->GetCountFeatures());

   // situations with 0 dimensions should have been filtered out before this function was called (but still inside the C++)
   EBM_ASSERT(1 <= cDimensions);

   size_t iDimension = 0;
   size_t cAuxillaryBinsForBuildFastTotals = 0;
   size_t cTensorBins = 1;
   do {
      const IntEbm indexFeature = featureIndexes[iDimension];
      if(indexFeature < IntEbm { 0 }) {
         LOG_0(Trace_Error, "ERROR CalcInteractionStrength featureIndexes value cannot be negative");
         return Error_IllegalParamVal;
      }
      if(countFeatures <= indexFeature) {
         LOG_0(Trace_Error, "ERROR CalcInteractionStrength featureIndexes value must be less than the number of features");
         return Error_IllegalParamVal;
      }
      const size_t iFeature = static_cast<size_t>(indexFeature);

      const FeatureInteraction * const pFeature = &aFeatures[iFeature];

      const size_t cBins = pFeature->GetCountBins();
      if(UNLIKELY(cBins <= size_t { 1 })) {
         LOG_0(Trace_Info, "INFO CalcInteractionStrength term contains a feature with only 1 or 0 bins");
         if(nullptr != avgInteractionStrengthOut) {
            *avgInteractionStrengthOut = 0.0;
         }
         return Error_None;
      }
      binSums.m_acBins[iDimension] = cBins;

      // if cBins could be 1, then we'd need to check at runtime for overflow of cAuxillaryBinsForBuildFastTotals
      // if this wasn't true then we'd have to check IsAddError(cAuxillaryBinsForBuildFastTotals, cTensorBins) at runtime
      EBM_ASSERT(0 == cTensorBins || cAuxillaryBinsForBuildFastTotals < cTensorBins);
      // since cBins must be 2 or more, cAuxillaryBinsForBuildFastTotals must grow slower than cTensorBins, and we checked at allocation 
      // that cTensorBins would not overflow
      EBM_ASSERT(!IsAddError(cAuxillaryBinsForBuildFastTotals, cTensorBins));
      // this can overflow, but if it does then we're guaranteed to catch the overflow via the multiplication check below
      cAuxillaryBinsForBuildFastTotals += cTensorBins;
      if(IsMultiplyError(cTensorBins, cBins)) {
         // unlike in the boosting code where we check at allocation time if the tensor created overflows on multiplication
         // we don't know what group of features our caller will give us for calculating the interaction scores,
         // so we need to check if our caller gave us a tensor that overflows multiplication
         // if we overflow this, then we'd be above the cCardinalityMax value, so set it to 0.0
         LOG_0(Trace_Info, "INFO CalcInteractionStrength IsMultiplyError(cTensorBins, cBins)");
         if (nullptr != avgInteractionStrengthOut) {
            *avgInteractionStrengthOut = 0.0;
         }
         return Error_None;
      }
      cTensorBins *= cBins;
      // if this wasn't true then we'd have to check IsAddError(cAuxillaryBinsForBuildFastTotals, cTensorBins) at runtime
      EBM_ASSERT(0 == cTensorBins || cAuxillaryBinsForBuildFastTotals < cTensorBins);

      ++iDimension;
   } while(cDimensions != iDimension);

   if(cCardinalityMax < cTensorBins) {
      LOG_0(Trace_Info, "INFO CalcInteractionStrength cCardinalityMax < cTensorBins");
      if (nullptr != avgInteractionStrengthOut) {
         *avgInteractionStrengthOut = 0.0;
      }
      return Error_None;
   }

   const size_t cScores = GetCountScores(cClasses);

   static constexpr size_t cAuxillaryBinsForSplitting = 4;
   const size_t cAuxillaryBins = EbmMax(cAuxillaryBinsForBuildFastTotals, cAuxillaryBinsForSplitting);

   if(IsAddError(cTensorBins, cAuxillaryBins)) {
      LOG_0(Trace_Warning, "WARNING CalcInteractionStrength IsAddError(cTensorBins, cAuxillaryBins)");
      return Error_OutOfMemory;
   }
   const size_t cTotalMainBins = cTensorBins + cAuxillaryBins;

   const size_t cBytesPerMainBin = GetBinSize<FloatMain, UIntMain>(pInteractionCore->IsHessian(), cScores);
   if(IsMultiplyError(cBytesPerMainBin, cTotalMainBins)) {
      LOG_0(Trace_Warning, "WARNING CalcInteractionStrength IsMultiplyError(cBytesPerBin, cTotalMainBins)");
      return Error_OutOfMemory;
   }

   BinBase * const aMainBins = pInteractionShell->GetInteractionMainBins(cBytesPerMainBin, cTotalMainBins);
   if(UNLIKELY(nullptr == aMainBins)) {
      // already logged
      return Error_OutOfMemory;
   }

#ifndef NDEBUG
   const auto * const pDebugMainBinsEnd = IndexBin(aMainBins, cBytesPerMainBin * cTotalMainBins);
#endif // NDEBUG

   memset(aMainBins, 0, cBytesPerMainBin * cTensorBins);

   const bool bHessian = pInteractionCore->IsHessian();

   EBM_ASSERT(1 <= pInteractionCore->GetDataSetInteraction()->GetCountSubsets());
   DataSubsetInteraction * pSubset = pInteractionCore->GetDataSetInteraction()->GetSubsets();
   const DataSubsetInteraction * const pSubsetsEnd = pSubset + pInteractionCore->GetDataSetInteraction()->GetCountSubsets();
   do {
      size_t cBytesPerFastBin;
      if(sizeof(UIntBig) == pSubset->GetObjectiveWrapper()->m_cUIntBytes) {
         if(sizeof(FloatBig) == pSubset->GetObjectiveWrapper()->m_cFloatBytes) {
            cBytesPerFastBin = GetBinSize<FloatBig, UIntBig>(bHessian, cScores);
         } else {
            EBM_ASSERT(sizeof(FloatSmall) == pSubset->GetObjectiveWrapper()->m_cFloatBytes);
            cBytesPerFastBin = GetBinSize<FloatSmall, UIntBig>(bHessian, cScores);
         }
      } else {
         EBM_ASSERT(sizeof(UIntSmall) == pSubset->GetObjectiveWrapper()->m_cUIntBytes);
         if(sizeof(FloatBig) == pSubset->GetObjectiveWrapper()->m_cFloatBytes) {
            cBytesPerFastBin = GetBinSize<FloatBig, UIntSmall>(bHessian, cScores);
         } else {
            EBM_ASSERT(sizeof(FloatSmall) == pSubset->GetObjectiveWrapper()->m_cFloatBytes);
            cBytesPerFastBin = GetBinSize<FloatSmall, UIntSmall>(bHessian, cScores);
         }
      }
      if(IsMultiplyError(cBytesPerFastBin, cTensorBins)) {
         LOG_0(Trace_Warning, "WARNING CalcInteractionStrength IsMultiplyError(cBytesPerBin, cTensorBins)");
         return Error_OutOfMemory;
      }

      // this doesn't need to be freed since it's tracked and re-used by the class InteractionShell
      BinBase * const aFastBins = pInteractionShell->GetInteractionFastBinsTemp(cBytesPerFastBin * cTensorBins);
      if(UNLIKELY(nullptr == aFastBins)) {
         // already logged
         return Error_OutOfMemory;
      }

      aFastBins->ZeroMem(cBytesPerFastBin, cTensorBins);

#ifndef NDEBUG
      binSums.m_pDebugFastBinsEnd = IndexBin(aFastBins, cBytesPerFastBin * cTensorBins);
#endif // NDEBUG

      size_t iDimensionLoop = 0;
      do {
         const IntEbm indexFeature = featureIndexes[iDimensionLoop];
         const size_t iFeature = static_cast<size_t>(indexFeature);
         const FeatureInteraction * const pFeature = &aFeatures[iFeature];

         binSums.m_aaPacked[iDimensionLoop] = pSubset->GetFeatureData(iFeature);

         EBM_ASSERT(1 <= pFeature->GetBitsRequiredMin());
         binSums.m_acItemsPerBitPack[iDimensionLoop] =
            GetCountItemsBitPacked(pFeature->GetBitsRequiredMin(), pSubset->GetObjectiveWrapper()->m_cUIntBytes);
         
         ++iDimensionLoop;
      } while(cDimensions != iDimensionLoop);

      binSums.m_cRuntimeRealDimensions = cDimensions;

      binSums.m_bHessian = pInteractionCore->IsHessian() ? EBM_TRUE : EBM_FALSE;
      binSums.m_cScores = cScores;

      binSums.m_cSamples = pSubset->GetCountSamples();
      binSums.m_aGradientsAndHessians = pSubset->GetGradHess();
      binSums.m_aWeights = pSubset->GetWeights();

      binSums.m_aFastBins = aFastBins;

      error = pSubset->BinSumsInteraction(&binSums);
      if(Error_None != error) {
         return error;
      }

      ConvertAddBin(
         cScores,
         pInteractionCore->IsHessian(),
         cTensorBins,
         sizeof(UIntBig) == pSubset->GetObjectiveWrapper()->m_cUIntBytes,
         sizeof(FloatBig) == pSubset->GetObjectiveWrapper()->m_cFloatBytes,
         aFastBins,
         std::is_same<UIntMain, uint64_t>::value,
         std::is_same<FloatMain, double>::value,
         aMainBins
      );

      ++pSubset;
   } while(pSubsetsEnd != pSubset);



   // TODO: we can exit here back to python to allow caller modification to our bins



#ifndef NDEBUG
   // make a copy of the original bins for debugging purposes

   BinBase * aDebugCopyBins = nullptr;
   if(!IsMultiplyError(cBytesPerMainBin, cTensorBins)) {
      ANALYSIS_ASSERT(0 != cBytesPerMainBin);
      ANALYSIS_ASSERT(1 <= cTensorBins);
      aDebugCopyBins = static_cast<BinBase *>(malloc(cBytesPerMainBin * cTensorBins));
      if(nullptr != aDebugCopyBins) {
         // if we can't allocate, don't fail.. just stop checking
         memcpy(aDebugCopyBins, aMainBins, cTensorBins * cBytesPerMainBin);
      }
   }
#endif // NDEBUG

   BinBase * aAuxiliaryBins = IndexBin(aMainBins, cBytesPerMainBin * cTensorBins);
   aAuxiliaryBins->ZeroMem(cBytesPerMainBin, cAuxillaryBins);

   TensorTotalsBuild(
      pInteractionCore->IsHessian(),
      cScores,
      cDimensions,
      binSums.m_acBins,
      aAuxiliaryBins,
      aMainBins
#ifndef NDEBUG
      , aDebugCopyBins
      , pDebugMainBinsEnd
#endif // NDEBUG
   );

   if(2 == cDimensions) {
      LOG_0(Trace_Verbose, "CalcInteractionStrength Starting bin sweep loop");

      double bestGain = PartitionTwoDimensionalInteraction(
         pInteractionCore,
         cDimensions,
         binSums.m_acBins,
         flags,
         cSamplesLeafMin,
         aAuxiliaryBins,
         aMainBins
#ifndef NDEBUG
         , aDebugCopyBins
         , pDebugMainBinsEnd
#endif // NDEBUG
      );

      // if totalWeight < 1 then bestGain could overflow to +inf, so do the division first
      const double totalWeight = pDataSet->GetWeightTotal();
      EBM_ASSERT(0 < totalWeight); // if all are zeros we assume there are no weights and use the count
      bestGain /= totalWeight;
      if(0 != (static_cast<UCalcInteractionFlags>(flags) & static_cast<UCalcInteractionFlags>(CalcInteractionFlags_EnableNewton))) {
         bestGain /= pInteractionCore->HessianConstant();
         bestGain *= pInteractionCore->GainAdjustmentHessianBoosting();
      } else {
         bestGain *= pInteractionCore->GainAdjustmentGradientBoosting();
      }
      const double gradientConstant = pInteractionCore->GradientConstant();
      bestGain *= gradientConstant;
      bestGain *= gradientConstant;

      if(UNLIKELY(/* NaN */ !LIKELY(bestGain <= std::numeric_limits<double>::max()))) {
         // We simplify our caller's handling by returning -lowest as our error indicator. -lowest will sort to being the
         // least important item, which is good, but it also signals an overflow without the weirness of NaNs.
         EBM_ASSERT(std::isnan(bestGain) || std::numeric_limits<double>::infinity() == bestGain);
         bestGain = k_illegalGainDouble;
      } else if(UNLIKELY(bestGain < 0.0)) {
         // gain can't mathematically be legally negative, but it can be here in the following situations:
         //   1) for impure interaction gain we subtract the parent partial gain, and there can be floating point
         //      noise that makes this slightly negative
         //   2) for impure interaction gain we subtract the parent partial gain, but if there were no legal cuts
         //      then the partial gain before subtracting the parent partial gain was zero and we then get a 
         //      substantially negative value.  In this case we should not have subtracted the parent partial gain
         //      since we had never even calculated the 4 quadrant partial gain, but we handle this scenario 
         //      here instead of inside the templated function.

         EBM_ASSERT(!std::isnan(bestGain));
         // make bestGain k_illegalGainDouble if it's -infinity, otherwise make it zero
         bestGain = std::numeric_limits<double>::lowest() <= bestGain ? 0.0 : k_illegalGainDouble;
      } else {
         EBM_ASSERT(!std::isnan(bestGain));
         EBM_ASSERT(!std::isinf(bestGain));
      }

      if(nullptr != avgInteractionStrengthOut) {
         *avgInteractionStrengthOut = bestGain;
      }

      EBM_ASSERT(k_illegalGainDouble == bestGain || 0.0 <= bestGain);
      LOG_COUNTED_N(
         pInteractionShell->GetPointerCountLogExitMessages(),
         Trace_Info,
         Trace_Verbose,
         "Exited CalcInteractionStrength: "
         "bestGain=%le"
         ,
         bestGain
      );
   } else {
      LOG_0(Trace_Warning, "WARNING CalcInteractionStrength We only support pairs for interaction detection currently");

      // TODO: handle interaction detection for higher dimensions

      // for now, just return any interactions that have other than 2 dimensions as k_illegalGainDouble, 
      // which means they won't be considered but indicates they were not handled
   }

#ifndef NDEBUG
   free(aDebugCopyBins);
#endif // NDEBUG

   return Error_None;
}

} // DEFINED_ZONE_NAME
