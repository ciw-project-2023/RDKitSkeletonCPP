/*
 * Copyright 2023 CoAler Group, all rights reserved.
 */

#include "LigandAlignmentAssembly.hpp"

namespace coaler::multialign {

    LigandAlignmentAssembly::LigandAlignmentAssembly(const std::unordered_map<LigandID, PoseID>& initialAssembly)
        : m_assembly(initialAssembly) {}

    /*----------------------------------------------------------------------------------------------------------------*/

    void LigandAlignmentAssembly::swapPoseForLigand(const LigandID ligandId, const PoseID newPoseId) {
        if(m_assembly.count(ligandId) == 0) {
            m_assembly.emplace(ligandId, newPoseId);
        }
        m_assembly.at(ligandId) = newPoseId;
    }

    /*----------------------------------------------------------------------------------------------------------------*/

    PoseID LigandAlignmentAssembly::getPoseOfLigand(LigandID ligandId) const {
        if (m_assembly.count(ligandId) == 0) {
            return std::numeric_limits<PoseID>::max();
        }
        return m_assembly.at(ligandId);
    }

    /*----------------------------------------------------------------------------------------------------------------*/

    void LigandAlignmentAssembly::incrementMissingLigandsCount() { m_missingLigandsCount++; }

    /*----------------------------------------------------------------------------------------------------------------*/

    bool LigandAlignmentAssembly::insertLigandPose(LigandID ligand, PoseID pose) {
        return m_assembly.emplace(ligand, pose).second;
    }

    /*----------------------------------------------------------------------------------------------------------------*/

    unsigned LigandAlignmentAssembly::getMissingLigandsCount() const noexcept { return m_missingLigandsCount; }

    /*----------------------------------------------------------------------------------------------------------------*/

    std::unordered_map<LigandID, PoseID> LigandAlignmentAssembly::getAssemblyMapping() const noexcept { return m_assembly; }

    /*----------------------------------------------------------------------------------------------------------------*/

    void LigandAlignmentAssembly::setMissingLigandsCount(unsigned count) { m_missingLigandsCount = count; }

}  // namespace coaler::multialign
