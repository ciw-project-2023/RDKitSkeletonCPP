//
// Created by malte on 12/4/23.
//

#include "Matcher.hpp"

#include <GraphMol/ForceFieldHelpers/MMFF/MMFF.h>
#include <GraphMol/RWMol.h>
#include <GraphMol/SmilesParse/SmartsWrite.h>
#include <spdlog/spdlog.h>

#include "GraphMol/ChemTransforms/ChemTransforms.h"
#include "GraphMol/DistGeomHelpers/Embedder.h"
#include "GraphMol/FMCS/FMCS.h"

namespace coaler::core {
    Matcher::Matcher(int threads) : m_threads(threads) {}

    void Matcher::murckoPruningRecursive(RDKit::RWMOL_SPTR& mol, int atomID, int parentID, std::vector<bool>& visit,
                                         std::vector<int>& delAtoms, std::vector<std::pair<int, int>>& delBonds,
                                         std::vector<int>& ringAtoms) {
        // mark atom as visited and find all neighbor atoms of atom with atomID
        visit.at(atomID) = true;
        for (const auto& neighborID : boost::make_iterator_range(mol->getAtomNeighbors(mol->getAtomWithIdx(atomID)))) {
            // only visit atoms not the parent, not yet visited and which are not part of any ring
            if (neighborID == parentID || visit.at(neighborID)
                || std::find(ringAtoms.begin(), ringAtoms.end(), neighborID) != ringAtoms.end()) {
                continue;
            }
            murckoPruningRecursive(mol, neighborID, atomID, visit, delAtoms, delBonds, ringAtoms);

            // find neighbors that are not on the to-be-deleted-list and count them
            int numNbrs = 0;
            for (const auto& nextNeighborID :
                 boost::make_iterator_range(mol->getAtomNeighbors(mol->getAtomWithIdx(neighborID)))) {
                if (std::find(delAtoms.begin(), delAtoms.end(), nextNeighborID) == delAtoms.end()) {
                    numNbrs++;
                }
            }

            // atoms with only one neighbor (parent) will be deleted after recursive call.
            // They are not deleted here cause internal IDs are updated after deletion.
            if (numNbrs == 1) {
                delBonds.push_back(std::make_pair(neighborID, atomID));
                delAtoms.push_back(neighborID);
            }
        }
    }

    void Matcher::murckoCheckDelAtoms(RDKit::RWMOL_SPTR& mol, int atomID, int parentID, std::vector<bool>& visit,
                                      std::vector<int>& ringAtoms, std::vector<int>& foundRingAtoms) {
        // mark atom as visited and check if they are part of any ring. If yes save and return to
        // recursive call before
        visit.at(atomID) = true;
        if (std::find(ringAtoms.begin(), ringAtoms.end(), atomID) != ringAtoms.end()) {
            foundRingAtoms.push_back(atomID);
            return;
        }
        // visit all neighbors if they are not the parent and are not yet visited
        for (const auto& neighborID : boost::make_iterator_range(mol->getAtomNeighbors(mol->getAtomWithIdx(atomID)))) {
            if (neighborID == parentID || visit.at(neighborID)) {
                continue;
            }
            murckoCheckDelAtoms(mol, neighborID, atomID, visit, ringAtoms, foundRingAtoms);
        }
    }

    std::optional<CoreResult> Matcher::calculateCoreMcs(RDKit::MOL_SPTR_VECT& mols) {
        // Generates all parameters needed for RDKit::findMCS()
        RDKit::MCSParameters mcsParams;
        RDKit::MCSAtomCompareParameters atomCompParams;
        atomCompParams.MatchChiralTag = true;
        atomCompParams.MatchFormalCharge = true;
        atomCompParams.MatchIsotope = false;
        atomCompParams.MatchValences = true;
        atomCompParams.RingMatchesRingOnly = true;
        atomCompParams.CompleteRingsOnly = false;
        mcsParams.AtomCompareParameters = atomCompParams;

        RDKit::MCSBondCompareParameters bondCompParams;
        bondCompParams.MatchStereo = true;
        bondCompParams.RingMatchesRingOnly = false;
        bondCompParams.CompleteRingsOnly = true;
        bondCompParams.MatchFusedRings = true;
        bondCompParams.MatchFusedRingsStrict = false;
        mcsParams.BondCompareParameters = bondCompParams;

        mcsParams.setMCSAtomTyperFromEnum(RDKit::AtomCompareAnyHeavyAtom);
        mcsParams.setMCSBondTyperFromEnum(RDKit::BondCompareAny);

        RDKit::MCSResult const mcs = RDKit::findMCS(mols, &mcsParams);
        if (mcs.QueryMol == nullptr) {
            return std::nullopt;
        }

        spdlog::info("MCS: {}", mcs.SmartsString);

        RDKit::RWMol first = *mols.at(0);

        RDKit::SubstructMatchParameters const substructMatchParams;

        std::vector<RDKit::MatchVectType> const structMatches
            = RDKit::SubstructMatch(first, *mcs.QueryMol, substructMatchParams);

        assert(!structMatches.empty());

        std::unordered_set<uint> matchedAtoms;
        for (auto const& match : structMatches) {
            for (auto const& [queryId, molId] : match) {
                matchedAtoms.insert(molId);
            }
        }

        std::vector<uint> atomsForRemoval;
        for (auto i = first.getNumAtoms() - 1; i == 0; i--) {
            if (!matchedAtoms.count(i)) {
                atomsForRemoval.push_back(i);
            }
        }

        for (auto const& atomId : atomsForRemoval) {
            first.removeAtom(atomId);
        }

        auto params = RDKit::DGeomHelpers::srETKDGv3;
        params.numThreads = m_threads;
        RDKit::DGeomHelpers::EmbedMolecule(first, params);

        std::vector<std::pair<int, double>> result;
        RDKit::MMFF::MMFFOptimizeMoleculeConfs(first, result, m_threads);

        return CoreResult{mcs.QueryMol, boost::make_shared<RDKit::ROMol>(first)};
    }

    std::optional<CoreResult> Matcher::calculateCoreMurcko(RDKit::MOL_SPTR_VECT& mols) {
        // calculate MCS first and sanitize molecule
        auto mcs = Matcher::calculateCoreMcs(mols);
        if (!mcs.has_value()) {
            return std::nullopt;
        }

        RDKit::RWMol mcsRWMol = *mcs.value().core;
        RDKit::MolOps::sanitizeMol(mcsRWMol);
        if (mcsRWMol.getRingInfo()->numRings() == 0) {
            return std::nullopt;
        }

        // initialize vars for DFS and find all atoms that are part of any ring
        std::vector<bool> visit(mcsRWMol.getNumAtoms(), false);

        std::vector<int> ringAtoms;
        std::vector<std::vector<int>> ringVec = mcsRWMol.getRingInfo()->atomRings();
        for (const auto ring : ringVec) {
            for (const auto atomID : ring) {
                if (visit.at(atomID)) {
                    continue;
                }

                ringAtoms.push_back(atomID);
            }
        }

        // start pruning of mcs, save to be deleted atoms and bonds
        std::vector<int> delAtomsMaybe;
        std::vector<std::pair<int, int>> delBonds;
        RDKit::RWMOL_SPTR murckoPtr = boost::make_shared<RDKit::RWMol>(mcsRWMol);

        // start from each ring atom
        for (const auto atomID : ringAtoms) {
            for (int i = 0; i < murckoPtr->getNumAtoms(); i++) {
                visit.at(i) = false;
            }
            murckoPruningRecursive(murckoPtr, atomID, -1, visit, delAtomsMaybe, delBonds, ringAtoms);
        }

        // delete duplicates from deletetion lists
        sort(delAtomsMaybe.begin(), delAtomsMaybe.end());
        delAtomsMaybe.erase(unique(delAtomsMaybe.begin(), delAtomsMaybe.end()), delAtomsMaybe.end());
        sort(delBonds.begin(), delBonds.end());
        delBonds.erase(unique(delBonds.begin(), delBonds.end()), delBonds.end());

        // check atoms in delAtomMaybe if they are part of the substructure between rings. If not
        // they are added to a delAtomsDefinitely.
        // An atom is part of the substructure between at least two rings if the DFS finds two or more
        // ring atoms.
        std::vector<int> foundRingAtoms;
        std::vector<int> delAtomsDefinitely;
        for (const auto delAtomsID : delAtomsMaybe) {
            for (int i = 0; i < murckoPtr->getNumAtoms(); i++) {
                visit.at(i) = false;
            }
            foundRingAtoms.clear();

            murckoCheckDelAtoms(murckoPtr, delAtomsID, -1, visit, ringAtoms, foundRingAtoms);
            if (foundRingAtoms.size() < 2) {
                delAtomsDefinitely.push_back(delAtomsID);
            }
        }

        // remove all bonds and atoms that are not part of  the murcko scaffold.
        // Deletion of atoms needs to be in order of atomIdx (high to low) to avoid
        // deletion errors.
        std::sort(delAtomsDefinitely.begin(), delAtomsDefinitely.end(), std::greater<>());
        for (auto [atom1, atom2] : delBonds) {
            if (std::find(delAtomsDefinitely.begin(), delAtomsDefinitely.end(), atom1) != delAtomsDefinitely.end()
                && std::find(delAtomsDefinitely.begin(), delAtomsDefinitely.end(), atom2) != delAtomsDefinitely.end()) {
                murckoPtr->removeBond(atom1, atom2);
            }
        }
        for (auto atom : delAtomsDefinitely) {
            murckoPtr->removeAtom(atom);
        }

        spdlog::info("Murco: {}", RDKit::MolToSmarts(*murckoPtr));

        // Embedding of core and calculation of atomCoords
        RDKit::RWMol first = *mols.at(0);

        RDKit::DGeomHelpers::EmbedParameters params;
        RDKit::DGeomHelpers::EmbedMolecule(first, params);

        std::vector<std::pair<int, double>> result;
        RDKit::MMFF::MMFFOptimizeMoleculeConfs(first, result);

        std::vector<RDKit::MatchVectType> const structMatches
            = RDKit::SubstructMatch(first, *murckoPtr, this->getMatchParams());
        assert(!structMatches.empty());

        return CoreResult{murckoPtr, boost::make_shared<RDKit::ROMol>(first)};
    }

    RDKit::SubstructMatchParameters Matcher::getMatchParams() const {
        RDKit::SubstructMatchParameters substructMatchParams;
        substructMatchParams.useChirality = false;
        substructMatchParams.useEnhancedStereo = true;
        substructMatchParams.aromaticMatchesConjugated = true;
        substructMatchParams.numThreads = m_threads;

        return substructMatchParams;
    }

}  // namespace coaler::core