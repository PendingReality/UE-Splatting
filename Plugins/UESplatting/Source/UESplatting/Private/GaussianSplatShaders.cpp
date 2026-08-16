// SPDX-License-Identifier: MIT

#include "GaussianSplatShaders.h"
#include "ShaderParameterUtils.h"
#include "RHIStaticStates.h"

// Implement global shaders
IMPLEMENT_GLOBAL_SHADER(FGaussianSplatCalcViewDataCS, "/Plugin/UESplatting/Private/CalcViewData.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FGaussianSplatCalcDistancesCS, "/Plugin/UESplatting/Private/CalcDistances.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FGaussianSplatVS, "/Plugin/UESplatting/Private/GaussianSplatRendering.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FGaussianSplatPS, "/Plugin/UESplatting/Private/GaussianSplatRendering.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FGaussianSplatCompositeVS, "/Plugin/UESplatting/Private/GaussianSplatComposite.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FGaussianSplatCompositePS, "/Plugin/UESplatting/Private/GaussianSplatComposite.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FRadixSortCountCS, "/Plugin/UESplatting/Private/RadixSort.usf", "CountCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FRadixSortPrefixSumCS, "/Plugin/UESplatting/Private/RadixSort.usf", "PrefixSumCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FRadixSortDigitPrefixSumCS, "/Plugin/UESplatting/Private/RadixSort.usf", "DigitPrefixSumCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FRadixSortScatterCS, "/Plugin/UESplatting/Private/RadixSort.usf", "ScatterCS", SF_Compute);

// Cluster culling shaders
IMPLEMENT_GLOBAL_SHADER(FClusterCullingResetCS, "/Plugin/UESplatting/Private/ClusterCulling.usf", "ResetCounterCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FClusterCullingCS, "/Plugin/UESplatting/Private/ClusterCulling.usf", "MainCS", SF_Compute);

// Splat compaction shaders (GPU-driven work reduction)
IMPLEMENT_GLOBAL_SHADER(FCompactSplatsCS, "/Plugin/UESplatting/Private/CompactSplats.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FPrepareIndirectArgsCS, "/Plugin/UESplatting/Private/PrepareIndirectArgs.usf", "MainCS", SF_Compute);

// NOTE: GPU-driven LOD shaders removed in unified approach
// LOD splats are now processed by CalcViewData.usf using the same buffers as original splats

// Global accumulator + compaction prefix-sum shaders
IMPLEMENT_GLOBAL_SHADER(FGatherVisibleCountsCS,     "/Plugin/UESplatting/Private/GlobalAccumulatorPrefixSum.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FPrefixSumVisibleCountsCS,  "/Plugin/UESplatting/Private/GlobalAccumulatorPrefixSum.usf", "MainCS", SF_Compute);
