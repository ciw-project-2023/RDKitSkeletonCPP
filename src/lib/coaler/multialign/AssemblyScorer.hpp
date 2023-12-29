/*
 * Copyright 2023 CoAler Group, all rights reserved.
 */

#pragma once
#include <coaler/multialign/models/PairwiseAlignments.hpp>

#include "Forward.hpp"
#include "LigandAlignmentAssembly.hpp"
#include "PoseRegisterCollection.hpp"

namespace coaler::multialign {
    class AssemblyScorer {
      public:
        static double calculateAssemblyScore(const coaler::multialign::LigandAlignmentAssembly& assembly,
                                             coaler::multialign::PairwiseAlignments& scores,
                                             const coaler::multialign::LigandVector& ligands);

        static double calculateScoreDeficitForLigand(LigandID ligandId, LigandID maxLigandId,
                                                     const LigandAlignmentAssembly& assembly,
                                                     const PoseRegisterCollection& registers,
                                                     PairwiseAlignments& scores, const LigandVector& ligands);

      private:
        /**
         * reads the score from the pairwise alignments or calculates it if missing
         * @param firstLigandID
         * @param secondLigandID
         * @param firstPoseID
         * @param secondPoseID
         * @param scores
         * @param ligands
         * @return
         */
        static double getScoreInAssembly(LigandID firstLigandID, LigandID secondLigandID, PoseID firstPoseID,
                                         PoseID secondPoseID, PairwiseAlignments& scores,
                                         const LigandVector& ligands);
    };

}  // namespace coaler::multialign
