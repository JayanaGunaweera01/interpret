// Copyright (c) 2023 The InterpretML Contributors
// Licensed under the MIT license.
// Author: Paul Koch <code@koch.ninja>

#include "precompiled_header_cpp.hpp"

#include <stdlib.h> // free
#include <stddef.h> // size_t, ptrdiff_t

#include "common_cpp.hpp"
#include "bridge_cpp.hpp"

#include "dataset_shared.hpp" // GetDataSetSharedHeader
#include "InteractionCore.hpp"
#include "InteractionShell.hpp"

namespace DEFINED_ZONE_NAME {
#ifndef DEFINED_ZONE_NAME
#error DEFINED_ZONE_NAME must be defined
#endif // DEFINED_ZONE_NAME

extern void InitializeRmseGradientsAndHessiansInteraction(
   const unsigned char * const pDataSetShared,
   const size_t cWeights,
   const BagEbm * const aBag,
   const double * const aInitScores,
   DataSetInteraction * const pDataSet
);

void InteractionShell::Free(InteractionShell * const pInteractionShell) {
   LOG_0(Trace_Info, "Entered InteractionShell::Free");

   if(nullptr != pInteractionShell) {
      AlignedFree(pInteractionShell->m_aInteractionFastBinsTemp);
      AlignedFree(pInteractionShell->m_aInteractionMainBins);
      InteractionCore::Free(pInteractionShell->m_pInteractionCore);
      
      // before we free our memory, indicate it was freed so if our higher level language attempts to use it we have
      // a chance to detect the error
      pInteractionShell->m_handleVerification = k_handleVerificationFreed;
      free(pInteractionShell);
   }

   LOG_0(Trace_Info, "Exited InteractionShell::Free");
}

InteractionShell * InteractionShell::Create(InteractionCore * const pInteractionCore) {
   LOG_0(Trace_Info, "Entered InteractionShell::Create");

   InteractionShell * const pNew = static_cast<InteractionShell *>(malloc(sizeof(InteractionShell)));
   if(UNLIKELY(nullptr == pNew)) {
      LOG_0(Trace_Error, "ERROR InteractionShell::Create nullptr == pNew");
      return nullptr;
   }

   pNew->InitializeUnfailing(pInteractionCore);

   LOG_0(Trace_Info, "Exited InteractionShell::Create");

   return pNew;
}

BinBase * InteractionShell::GetInteractionFastBinsTemp(const size_t cBytes) {
   ANALYSIS_ASSERT(0 != cBytes);

   BinBase * aBuffer = m_aInteractionFastBinsTemp;
   if(UNLIKELY(m_cBytesFastBins < cBytes)) {
      AlignedFree(aBuffer);
      m_aInteractionFastBinsTemp = nullptr;

      if(IsAddError(cBytes, cBytes)) {
         LOG_0(Trace_Warning, "WARNING InteractionShell::GetInteractionFastBinsTemp IsAddError(cBytes, cBytes)");
         return nullptr;
      }
      const size_t cNewAllocatedFastBins = cBytes + cBytes;

      m_cBytesFastBins = cNewAllocatedFastBins;
      LOG_N(Trace_Info, "Growing Interaction fast bins to %zu", cNewAllocatedFastBins);

      aBuffer = static_cast<BinBase *>(AlignedAlloc(cNewAllocatedFastBins));
      if(nullptr == aBuffer) {
         LOG_0(Trace_Warning, "WARNING InteractionShell::GetInteractionFastBinsTemp OutOfMemory");
         return nullptr;
      }
      m_aInteractionFastBinsTemp = aBuffer;
   }
   return aBuffer;
}

BinBase * InteractionShell::GetInteractionMainBins(const size_t cBytesPerMainBin, const size_t cMainBins) {
   ANALYSIS_ASSERT(0 != cBytesPerMainBin);

   BinBase * aBuffer = m_aInteractionMainBins;
   if(UNLIKELY(m_cAllocatedMainBins < cMainBins)) {
      AlignedFree(aBuffer);
      m_aInteractionMainBins = nullptr;

      const size_t cItemsGrowth = (cMainBins >> 2) + 16; // cannot overflow
      if(IsAddError(cItemsGrowth, cMainBins)) {
         LOG_0(Trace_Warning, "WARNING InteractionShell::GetInteractionMainBins IsAddError(cItemsGrowth, cMainBins)");
         return nullptr;
      }
      const size_t cNewAllocatedMainBins = cMainBins + cItemsGrowth;

      m_cAllocatedMainBins = cNewAllocatedMainBins;
      LOG_N(Trace_Info, "Growing Interaction big bins to %zu", cNewAllocatedMainBins);

      if(IsMultiplyError(cBytesPerMainBin, cNewAllocatedMainBins)) {
         LOG_0(Trace_Warning, "WARNING InteractionShell::GetInteractionMainBins IsMultiplyError(cBytesPerMainBin, cNewAllocatedMainBins)");
         return nullptr;
      }
      aBuffer = static_cast<BinBase *>(AlignedAlloc(cBytesPerMainBin * cNewAllocatedMainBins));
      if(nullptr == aBuffer) {
         LOG_0(Trace_Warning, "WARNING InteractionShell::GetInteractionMainBins OutOfMemory");
         return nullptr;
      }
      m_aInteractionMainBins = aBuffer;
   }
   return aBuffer;
}

EBM_API_BODY ErrorEbm EBM_CALLING_CONVENTION CreateInteractionDetector(
   const void * dataSet,
   const BagEbm * bag,
   const double * initScores, // only samples with non-zeros in the bag are included
   CreateInteractionFlags flags,
   const char * objective,
   const double * experimentalParams,
   InteractionHandle * interactionHandleOut
) {
   LOG_N(Trace_Info, "Entered CreateInteractionDetector: "
      "dataSet=%p, "
      "bag=%p, "
      "initScores=%p, "
      "flags=0x%" UCreateInteractionFlagsPrintf ", "
      "objective=%p, "
      "experimentalParams=%p, "
      "interactionHandleOut=%p"
      ,
      static_cast<const void *>(dataSet),
      static_cast<const void *>(bag),
      static_cast<const void *>(initScores),
      static_cast<UCreateInteractionFlags>(flags), // signed to unsigned conversion is defined behavior in C++
      static_cast<const void *>(objective), // do not print the string for security reasons
      static_cast<const void *>(experimentalParams),
      static_cast<const void *>(interactionHandleOut)
   );

   ErrorEbm error;

   if(nullptr == interactionHandleOut) {
      LOG_0(Trace_Error, "ERROR CreateInteractionDetector nullptr == interactionHandleOut");
      return Error_IllegalParamVal;
   }
   *interactionHandleOut = nullptr; // set this to nullptr as soon as possible so the caller doesn't attempt to free it

   if(0 != (static_cast<UCreateInteractionFlags>(flags) & static_cast<UCreateInteractionFlags>(~(
      static_cast<UCreateInteractionFlags>(CreateInteractionFlags_DifferentialPrivacy)
   )))) {
      LOG_0(Trace_Error, "ERROR CreateInteractionDetector flags contains unknown flags. Ignoring extras.");
   }

   if(nullptr == dataSet) {
      LOG_0(Trace_Error, "ERROR CreateInteractionDetector nullptr == dataSet");
      return Error_IllegalParamVal;
   }

   UIntShared countSamples;
   size_t cFeatures;
   size_t cWeights;
   size_t cTargets;
   error = GetDataSetSharedHeader(static_cast<const unsigned char *>(dataSet), &countSamples, &cFeatures, &cWeights, &cTargets);
   if(Error_None != error) {
      // already logged
      return error;
   }

   if(IsConvertError<size_t>(countSamples)) {
      LOG_0(Trace_Error, "ERROR CreateInteractionDetector IsConvertError<size_t>(countSamples)");
      return Error_IllegalParamVal;
   }
   size_t cSamples = static_cast<size_t>(countSamples);

   if(size_t { 1 } < cWeights) {
      LOG_0(Trace_Warning, "WARNING CreateInteractionDetector size_t { 1 } < cWeights");
      return Error_IllegalParamVal;
   }
   if(size_t { 1 } != cTargets) {
      LOG_0(Trace_Warning, "WARNING CreateInteractionDetector 1 != cTargets");
      return Error_IllegalParamVal;
   }

   InteractionCore * pInteractionCore = nullptr;
   error = InteractionCore::Create(
      static_cast<const unsigned char *>(dataSet),
      cSamples, 
      cFeatures,
      cWeights,
      bag,
      flags,
      objective,
      experimentalParams,
      &pInteractionCore
   );
   if(Error_None != error) {
      // legal to call if nullptr. On error we can get back a legal pInteractionCore to delete
      InteractionCore::Free(pInteractionCore);
      return error;
   }

   InteractionShell * const pInteractionShell = InteractionShell::Create(pInteractionCore);
   if(UNLIKELY(nullptr == pInteractionShell)) {
      // if the memory allocation for pInteractionShell failed then 
      // there was no place to put the pInteractionCore, so free it
      InteractionCore::Free(pInteractionCore);
      return Error_OutOfMemory;
   }

   if(ptrdiff_t { 0 } != pInteractionCore->GetCountClasses() && ptrdiff_t { 1 } != pInteractionCore->GetCountClasses()) {
      if(!pInteractionCore->IsRmse()) {
         error = pInteractionCore->InitializeInteractionGradientsAndHessians(
            static_cast<const unsigned char *>(dataSet),
            cWeights,
            bag,
            initScores
         );
         if(Error_None != error) {
            // DO NOT FREE pInteractionCore since it's owned by pInteractionShell, which we free here
            InteractionShell::Free(pInteractionShell);
            return error;
         }
      } else {
         InitializeRmseGradientsAndHessiansInteraction(
            static_cast<const unsigned char *>(dataSet),
            cWeights,
            bag,
            initScores,
            pInteractionCore->GetDataSetInteraction()
         );
      }
   }

   const InteractionHandle handle = pInteractionShell->GetHandle();

   LOG_N(Trace_Info, "Exited CreateInteractionDetector: *interactionHandleOut=%p", static_cast<void *>(handle));

   *interactionHandleOut = handle;
   return Error_None;
}

EBM_API_BODY void EBM_CALLING_CONVENTION FreeInteractionDetector(
   InteractionHandle interactionHandle
) {
   LOG_N(Trace_Info, "Entered FreeInteractionDetector: interactionHandle=%p", static_cast<void *>(interactionHandle));

   InteractionShell * const pInteractionShell = InteractionShell::GetInteractionShellFromHandle(interactionHandle);
   // if the conversion above doesn't work, it'll return null, and our free will not in fact free any memory,
   // but it will not crash. We'll leak memory, but at least we'll log that.

   // it's legal to call free on nullptr, just like for free().  This is checked inside InteractionCore::Free()
   InteractionShell::Free(pInteractionShell);

   LOG_0(Trace_Info, "Exited FreeInteractionDetector");
}

} // DEFINED_ZONE_NAME
