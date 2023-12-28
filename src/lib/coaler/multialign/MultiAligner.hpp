/*
 * Copyright 2023 CoAler Group, all rights reserved.
 */

#pragma once
#include "Forward.hpp"
#include "GraphMol/FMCS/FMCS.h"
#include "LigandAlignmentAssembly.hpp"
#include "MultiAlignerResult.hpp"
#include "PoseRegister.hpp"
#include "PoseRegisterBuilder.hpp"

namespace coaler::multialign {

    class MultiAligner {
      public:
        explicit MultiAligner(RDKit::MOL_SPTR_VECT molecules,
                              unsigned maxStartingAssemblies = Constants::DEFAULT_NOF_STARTING_ASSEMBLIES,
                              unsigned nofThreads = Constants::DEFAULT_NOF_THREADS);

        MultiAlignerResult alignMolecules();

        [[maybe_unused]] LigandAlignmentAssembly optimizeAssembly(LigandAlignmentAssembly& assembly);

      private:
        void ensurePairwiseAlignmentsForAssembly(const std::unordered_map<LigandID, PoseID>& assemblyIDs);

        static PairwiseAlignment calculateAlignmentScores(const LigandVector& ligands);

        static double getScore(const Ligand& ligand1, const Ligand& ligand2, unsigned pose1, unsigned pose2);

        [[maybe_unused]] void addPoseToPairwiseAlignments(LigandID ligandId, PoseID poseId,
                                                          const std::unordered_map<LigandID, PoseID>& assemblyIDs);

        unsigned m_maxStartingAssemblies;
        std::vector<Ligand> m_ligands;
        PoseRegisterCollection m_poseRegisters;
        PairwiseAlignment m_pairwiseAlignments;
        unsigned m_nofThreads;
    };

}  // namespace coaler::multialign
