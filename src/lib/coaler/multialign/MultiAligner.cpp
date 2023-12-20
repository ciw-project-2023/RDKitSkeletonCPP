/*
 * Copyright 2023 CoAler Group, all rights reserved.
 */

#include "MultiAligner.hpp"

#include <GraphMol/ShapeHelpers/ShapeUtils.h>
#include <omp.h>
#include <spdlog/spdlog.h>

#include <queue>
#include <utility>

#include "AssemblyScorer.hpp"
#include "LigandAlignmentAssembly.hpp"
#include "StartingAssemblyGenerator.hpp"

namespace coaler::multialign {
    using AssemblyWithScore = std::pair<LigandAlignmentAssembly, double>;

    class LigandAvailabilityMapping : public std::unordered_map<LigandID, bool> {
      public:
        void setAllAvailable() {
            for (auto &pair : *this) {
                pair.second = true;
            }
        }

        /*----------------------------------------------------------------------------------------------------------------*/

        void init(const LigandVector &ligands) {
            for (const Ligand &ligand : ligands) {
                this->emplace(ligand.getID(), true);
            }
        }
    };

    /*----------------------------------------------------------------------------------------------------------------*/

    struct LigandIsAvailable {
        bool operator()(std::pair<LigandID, bool> entry) { return entry.second; }
    };

    /*----------------------------------------------------------------------------------------------------------------*/

    struct AssemblyWithScoreLess {
        bool operator()(const AssemblyWithScore &lhs, const AssemblyWithScore &rhs) {
            if (lhs.first.getMissingLigandsCount() != lhs.first.getMissingLigandsCount()) {
                return lhs.first.getMissingLigandsCount() < lhs.first.getMissingLigandsCount();
            }

            return lhs.second < rhs.second;
        }
    };

    /*----------------------------------------------------------------------------------------------------------------*/

    struct AssemblyWithScoreGreater {
        bool operator()(const AssemblyWithScore &lhs, const AssemblyWithScore &rhs) {
            if (lhs.first.getMissingLigandsCount() != lhs.first.getMissingLigandsCount()) {
                return lhs.first.getMissingLigandsCount() > lhs.first.getMissingLigandsCount();
            }

            return lhs.second > rhs.second;
        }
    };

    /*----------------------------------------------------------------------------------------------------------------*/

    MultiAligner::MultiAligner(RDKit::MOL_SPTR_VECT molecules, unsigned maxStartingAssemblies, unsigned nofThreads)

        : m_maxStartingAssemblies(maxStartingAssemblies) {
        assert(m_maxStartingAssemblies > 0);
        for (LigandID id = 0; id < molecules.size(); id++) {
            UniquePoseSet poses;

            for (PoseID poseId = 0; poseId < molecules.at(id)->getNumConformers(); poseId++) {
                poses.emplace(id, poseId);
            }

            m_ligands.emplace_back(*molecules.at(id), poses, id);
        }
        omp_set_num_threads(nofThreads);  // this sets the number of threads used for ALL subsequent parallel regions.
    }

    /*----------------------------------------------------------------------------------------------------------------*/

    PairwiseAlignment MultiAligner::calculateAlignmentScores(const LigandVector &ligands) {
        PairwiseAlignment scores;
        unsigned n = ligands.size();
        unsigned m = ligands.at(0).getNumPoses();
        unsigned combinations = 0.5 * n * (n - 1) * m * m;  // TODO how many?
        spdlog::info("Calculating {} combinations. This may take some time", combinations);

        for (LigandID firstMolId = 0; firstMolId < ligands.size(); firstMolId++) {
            spdlog::info("calculated {} combinations so far.", scores.size());
            for (LigandID secondMolId = firstMolId + 1; secondMolId < ligands.size(); secondMolId++) {
                unsigned nofPosesFirst = ligands.at(firstMolId).getNumPoses();
                unsigned nofPosesSecond = ligands.at(secondMolId).getNumPoses();
                omp_lock_t maplock;
                omp_init_lock(&maplock);

#pragma omp parallel for shared(maplock, ligands, scores, nofPosesFirst, nofPosesSecond, firstMolId, \
                                    secondMolId) default(none)
                for (unsigned firstMolPoseId = 0; firstMolPoseId < nofPosesFirst; firstMolPoseId++) {
                    for (unsigned secondMolPoseId = 0; secondMolPoseId < nofPosesSecond; secondMolPoseId++) {
                        RDKit::RWMol const firstMol = ligands.at(firstMolId).getMolecule();
                        RDKit::RWMol const secondMol = ligands.at(secondMolId).getMolecule();

                        double score = 1
                                       - RDKit::MolShapes::tanimotoDistance(firstMol, secondMol, firstMolPoseId,
                                                                            secondMolPoseId);
                        UniquePoseID firstPose(firstMolId, firstMolPoseId);
                        UniquePoseID secondPose(secondMolId, secondMolPoseId);
                        omp_set_lock(&maplock);
                        scores.emplace(PosePair(firstPose, secondPose), score);
                        omp_unset_lock(&maplock);
                    }
                }
            }
        }
        spdlog::info("finished calculating pairwise alignments");
        return scores;
    }

    /*----------------------------------------------------------------------------------------------------------------*/
    MultiAlignerResult MultiAligner::alignMolecules() {
        // calculate pairwise alignments
        m_pairwiseAlignments = this->calculateAlignmentScores(m_ligands);

        spdlog::info("Mols: {} | Confs/Mol: {} | total pairwise scores: {}", m_ligands.size(),
                     m_ligands.begin()->getNumPoses(), m_pairwiseAlignments.size());
        // build pose registers
        m_poseRegisters = PoseRegisterBuilder::buildPoseRegisters(m_pairwiseAlignments, m_ligands);

        // build starting ensembles from registers
        // AssemblyCollection assemblies;
        std::priority_queue<AssemblyWithScore, std::vector<AssemblyWithScore>, AssemblyWithScoreGreater> assemblies;
        for (const Ligand &ligand : m_ligands) {
            for (const UniquePoseID &pose : ligand.getPoses()) {
                LigandAlignmentAssembly assembly
                    = StartingAssemblyGenerator::generateStartingAssembly(pose, m_poseRegisters, m_ligands);

                double const score = AssemblyScorer::calculateAssemblyScore(assembly, m_pairwiseAlignments, m_ligands);
                AssemblyWithScore newAssembly = std::make_pair(assembly, score);

                // insert if queue no full or new assembly is larger that worst assembly in queue

                // TODO ensure this is called correctly
                if (assemblies.size() < m_maxStartingAssemblies) {
                    assemblies.push(newAssembly);
                    continue;
                }
                AssemblyWithScore topAssembly = assemblies.top();
                if (AssemblyWithScoreGreater()(newAssembly, topAssembly)) {
                    assemblies.pop();
                    assemblies.push(newAssembly);
                    continue;
                }
            }
        }

        // top #m_maxStartingAssemblies are now in queue. find best scoring assembly by optimizing all
        // TODO SYMMETRY?
        // optimize all starting assemblies.
        LigandAlignmentAssembly currentBestAssembly = assemblies.top().first;  // TODO default constructor for assembly?
        double currentBestAssemblyScore
            = AssemblyScorer::calculateAssemblyScore(currentBestAssembly, m_pairwiseAlignments, m_ligands);

        // write queue content to vector to allow for parallel for
        std::vector<AssemblyWithScore> assembliesList;
        while (!assemblies.empty()) {
            assembliesList.push_back(assemblies.top());
            assemblies.pop();
        }
        spdlog::info("start optimization of {} alignment assemblies.", assembliesList.size());

        // locks for shared variables
        omp_lock_t bestAssemblyScoreLock;
        omp_init_lock(&bestAssemblyScoreLock);
        omp_lock_t bestAssemblyLock;
        omp_init_lock(&bestAssemblyLock);

#pragma omp parallel for shared(bestAssemblyScoreLock, bestAssemblyLock, currentBestAssembly, \
                                    currentBestAssemblyScore, assembliesList) default(none)
        for (unsigned assemblyID = 0; assemblyID < assembliesList.size(); assemblyID++) {
            auto [currentAssembly, currentAssemblyScore] = assembliesList.at(assemblyID);
            spdlog::debug("score before opt: {}", currentAssemblyScore);
            if (currentAssembly.getMissingLigandsCount() != 0) {
                spdlog::warn("skipping assembly because it is missing ligands");
                continue;
            }

            LigandAvailabilityMapping ligandAvailable;
            ligandAvailable.init(m_ligands);

            // assembly optimization step
            while (std::any_of(ligandAvailable.begin(), ligandAvailable.end(), LigandIsAvailable())) {
                // determine ligand with highest score deficit TODO move to own func
                double maxScoreDeficit = 0;
                Ligand worstLigand = *m_ligands.begin();  // dummy init --> better idea?
                for (const Ligand &ligand : m_ligands) {
                    if (!ligandAvailable.at(ligand.getID())) {
                        continue;
                    }
                    double ligandScoreDeficit = AssemblyScorer::calculateScoreDeficitForLigand(
                        ligand.getID(), m_ligands.size() - 1, currentAssembly, m_poseRegisters, m_pairwiseAlignments);
                    if (maxScoreDeficit < ligandScoreDeficit) {
                        worstLigand = ligand;
                        maxScoreDeficit = ligandScoreDeficit;
                    }
                }
                if (maxScoreDeficit == 0) {
                    // all pairwise alignments are optimal
                    // TODO can we return this assembly and be sure its the optimum?
                    break;
                }

                bool swappedLigandPose = false;
                for (const UniquePoseID &pose : worstLigand.getPoses()) {
                    // check if using this pose improves currentAssembly
                    if (pose.getLigandInternalPoseId() == currentAssembly.getPoseOfLigand(worstLigand.getID())) {
                        continue;
                    }
                    LigandAlignmentAssembly assemblyCopy = currentAssembly;
                    assemblyCopy.swapPoseForLigand(worstLigand.getID(), pose.getLigandInternalPoseId());
                    // avoid identity swap
                    double const newAssemblyScore
                        = AssemblyScorer::calculateAssemblyScore(assemblyCopy, m_pairwiseAlignments, m_ligands);

                    if (newAssemblyScore > currentAssemblyScore) {
                        currentAssembly = assemblyCopy;
                        currentAssemblyScore = newAssemblyScore;
                        ligandAvailable.setAllAvailable();
                        swappedLigandPose = true;
                        break;
                    }
                }

                if (!swappedLigandPose) {
                    ligandAvailable.at(worstLigand.getID()) = false;
                }
            }

            double const assemblyScore
                = AssemblyScorer::calculateAssemblyScore(currentAssembly, m_pairwiseAlignments, m_ligands);
            spdlog::debug("Score after opt: {}", assemblyScore);
            if (assemblyScore > currentBestAssemblyScore) {
                currentBestAssembly = currentAssembly;
                currentBestAssemblyScore = assemblyScore;
            }
        }
        spdlog::info("finished alignment optimization.");

        return {currentBestAssemblyScore, currentBestAssembly.getAssemblyMapping(), m_ligands};
    }

}  // namespace coaler::multialign
